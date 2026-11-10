#include "plc/browse.h"

#include "mirror/config.h"
#include "core/log.h"
#include "plc/plc.h"

#include "Arp/Plc/Meta/Services/VariableBrowseResult.hpp"
#include "Arp/Plc/Meta/Services/VariableInformation.hpp"
#include "Arp/Plc/Meta/Commons/DataType.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <unordered_set>

namespace plc {

namespace {

using Arp::Plc::Meta::Commons::DataType;
using Arp::Plc::Meta::Services::VariableBrowseHandle;
using Arp::Plc::Meta::Services::VariableBrowseResult;
using Arp::Plc::Meta::Services::VariableInformation;

const std::string ECLR_PREFIX = "Arp.Plc.Eclr/";

// The variable count is bounded by the node's topic slots, two per variable (OpenNode's
// max_topics).
constexpr size_t MAX_VARS = 15000;

// Guard against pathological / cyclic nesting when descending containers.
constexpr int MAX_BROWSE_DEPTH = 8;

// Rant nests RANT_SCHEMA_MAX_DEPTH structs, the root counting as 1. A deeper member is skipped alone.
constexpr int STRUCT_BODY_DEPTH_MIN = 1;
constexpr int STRUCT_BODY_DEPTH_MAX = RANT_SCHEMA_MAX_DEPTH;

// Name filters

// Keep only ECLR (user program) variables, minus the runtime's diagnostic mirrors.
bool IsUserVariable(const std::string& name)
{
    if (name.compare(0, ECLR_PREFIX.size(), ECLR_PREFIX) != 0) return false;
    static const char* const prefixes[] = {
        "Arp.Plc.Eclr/AXIO_DIAG_", "Arp.Plc.Eclr/IB_DIAG_", "Arp.Plc.Eclr/PNIO_",
        "Arp.Plc.Eclr/PND_S1_",    "Arp.Plc.Eclr/EIPD_",
    };
    for (auto* p : prefixes) if (name.compare(0, std::strlen(p), p) == 0) return false;
    static const char* const exact[] = {
        "Arp.Plc.Eclr/PND_IO_DRIVEN_BY_PLC", "Arp.Plc.Eclr/DEVICE_STATE",
        "Arp.Plc.Eclr/RTC",                   "Arp.Plc.Eclr/USER_PARTITION",
        "Arp.Plc.Eclr/ESM_DATA",              "Arp.Plc.Eclr/HMI_CONTROL",
        "Arp.Plc.Eclr/HMI_STATUS2",           "Arp.Plc.Eclr/IP_ACTIVE_SOCKETS",
        "Arp.Plc.Eclr/TLS_ACTIVE_SOCKETS",    "Arp.Plc.Eclr/PLC_CRC_PRJ",
        "Arp.Plc.Eclr/ALARM_PROGRAM_STATUS",  "Arp.Plc.Eclr/FAN_STATE",
        "Arp.Plc.Eclr/PLC_STATE",             "Arp.Plc.Eclr/PTP_TIMESYNC_DATA",
        "Arp.Plc.Eclr/UPS_DIAGNOSTICS",
    };
    for (auto* e : exact) if (name == e) return false;
    return true;
}

// A node's own name: the segment after the last '.' (nesting) or '/' (component),
// whichever comes later. ".../Process1.__0000" -> "__0000", ".../D100" -> "D100".
std::string LeafName(const std::string& full)
{
    size_t dot = full.rfind('.'), slash = full.rfind('/');
    size_t sep = (dot == std::string::npos) ? slash
               : (slash == std::string::npos) ? dot
               : std::max(dot, slash);
    return (sep == std::string::npos) ? full : full.substr(sep + 1);
}

// Ignore rules on a node's own name segment, so ignoring a container drops all under it.
bool IsIgnoredName(const std::string& full)
{
    std::string n = LeafName(full);
    if (n.empty()) return false;
    if (n[0] == '_' || n == "BLANK") return true;
    return n.rfind("HMI", 0) == 0 || n.rfind("<StringTable>", 0) == 0;
}

// The GDS name without the component, '.' and '_' turned into '/'. Not one to one, which
// ClaimTopic checks.
std::string TopicLeaf(const std::string& full)
{
    std::string leaf = (full.compare(0, ECLR_PREFIX.size(), ECLR_PREFIX) == 0)
                     ? full.substr(ECLR_PREFIX.size()) : full;
    std::replace(leaf.begin(), leaf.end(), '_', '/');
    std::replace(leaf.begin(), leaf.end(), '.', '/');
    return leaf;
}

// Topics claimed so far this browse. The first claim wins and the loser is logged.
std::unordered_set<std::string> s_usedTopics;

// The prefix and '/' go in front, and Rant adds a channel suffix such as "@set" after.
bool ClaimTopic(const std::string& gdsName, const std::string& topicLeaf, std::string& out)
{
    const size_t full = (g_cfg.prefix.empty() ? 0 : g_cfg.prefix.size() + 1) + topicLeaf.size();
    if (full > RANT_TOPIC_NAME_MAX - 4) {
        log::Error("variable name too long, skipping: " + topicLeaf);
        return false;
    }
    if (!s_usedTopics.insert(topicLeaf).second) {
        log::Error("topic " + topicLeaf + " already claimed; skipping " + gdsName
                   + " (two GDS names collapse to one topic, rename one)");
        return false;
    }
    out = g_cfg.prefix.empty() ? topicLeaf : g_cfg.prefix + "/" + topicLeaf;
    return true;
}

// Type classification

DataType BaseType(DataType dt)
{
    return static_cast<DataType>(static_cast<uint32_t>(dt)
                                 & static_cast<uint32_t>(DataType::BaseTypeMask));
}

bool HasFlag(DataType dt, DataType flag)
{
    return (static_cast<uint32_t>(dt) & static_cast<uint32_t>(flag)) != 0;
}

bool IsArrayType(DataType dt) { return HasFlag(dt, DataType::Array); }

// Program/Class/Subsystem group the user's variables but are not values: they are
// descended and each member becomes its own variable.
bool IsContainerType(DataType dt)
{
    DataType b = BaseType(dt);
    return b == DataType::Program || b == DataType::Class || b == DataType::Subsystem;
}

// A plain data struct IS a value and is published whole. (An FB instance is a
// separate base type and stays unpublished: its members are internals.)
bool IsStructType(DataType dt) { return BaseType(dt) == DataType::Struct; }

// Scope

using Arp::Plc::Meta::Commons::StandardAttribute;

bool HasAttr(StandardAttribute attrs, StandardAttribute flag)
{
    return (static_cast<uint64_t>(attrs) & static_cast<uint64_t>(flag)) != 0;
}

// A program variable is exposed when it is a port or ticked for OPC UA, HMI or Proficloud.
// Anything else is a local (docs/design.md, which variables are published).
bool IsExposed(const VariableInformation& info)
{
    const StandardAttribute a = info.StandardAttributes;
    return HasAttr(a, StandardAttribute::Input) || HasAttr(a, StandardAttribute::Output)
        || HasAttr(a, StandardAttribute::Opc)   || HasAttr(a, StandardAttribute::Ehmi)
        || HasAttr(a, StandardAttribute::ProfiCloud);
}

// Program-local variables left out of this browse, for the summary line.
size_t s_localsSkipped = 0;

// The RANT scalar a PLC type advertises. An enum publishes as its underlying
// base type. Arrays, structs, FBs and anything else map to None.
Scalar ScalarForType(DataType dt)
{
    if (IsArrayType(dt)) return Scalar::None;
    if (HasFlag(dt, DataType::Enum)) dt = BaseType(dt);
    switch (dt) {
        case DataType::Boolean:        return Scalar::Bool;
        case DataType::Int8:           return Scalar::I8;
        case DataType::UInt8:
        case DataType::Char8:          return Scalar::U8;
        case DataType::Int16:          return Scalar::I16;
        case DataType::UInt16:
        case DataType::Char16:         return Scalar::U16;
        case DataType::Int32:          return Scalar::I32;
        case DataType::UInt32:         return Scalar::U32;
        case DataType::Int64:          return Scalar::I64;
        case DataType::IecTime:        return Scalar::Time;
        case DataType::IecTime64:      return Scalar::LTime;
        case DataType::IecTimeOfDay64: return Scalar::LTod;
        case DataType::IecDate64:      return Scalar::LDate;
        case DataType::IecDateTime64:  return Scalar::LDt;
        case DataType::UInt64:         return Scalar::U64;
        case DataType::Float32:        return Scalar::F32;
        case DataType::Float64:        return Scalar::F64;
        case DataType::String:
        case DataType::StaticString:
        case DataType::IecString:      return Scalar::String;
        default:                       return Scalar::None;
    }
}

// Element type of an array node (the Array flag cleared).
Scalar ArrayElementKind(DataType dt)
{
    return ScalarForType(static_cast<DataType>(static_cast<uint32_t>(dt)
                                               & ~static_cast<uint32_t>(DataType::Array)));
}

// Read an array's dimensions. False if there are none or the total element count
// is outside 1..65535 (a RANT fixed-array count is u16).
bool ReadDims(const VariableInformation& info, std::vector<ArrDim>& dims, uint32_t& total)
{
    dims.clear();
    uint64_t n = 1;
    for (const auto& d : info.ArrayDimensions) {
        dims.push_back({ static_cast<int64_t>(d.Offset), d.Count });
        n *= d.Count;
    }
    if (dims.empty() || n == 0 || n > 0xFFFFu) return false;
    total = static_cast<uint32_t>(n);
    return true;
}

// PLC access helpers

// Children of a browse node, empty on any failure so one bad subtree cannot abort
// the browse. `ok` tells the caller whether the call itself succeeded.
std::vector<VariableBrowseResult> Children(const VariableBrowseHandle& handle, bool& ok)
{
    std::vector<VariableBrowseResult> out;
    ok = true;
    try {
        g_browseSvc->GetChildren(handle, [&out](IRscReadEnumerator<VariableBrowseResult>& e) {
            VariableBrowseResult r; e.BeginRead();
            while (e.ReadNext(r)) out.push_back(r);
        });
    } catch (...) { ok = false; out.clear(); }
    return out;
}

// Whether the PLC lets `leaf` be forced. A struct or array is never forcible by
// its own name, so callers probe a representative element / member instead.
bool ProbeForceable(const std::string& leaf)
{
    if (!g_forceSvc || !g_cfg.probeForcable) return false;
    try { return g_forceSvc->IsForcable(RscStr(leaf.c_str())); }
    catch (...) { return false; }
}

// Struct schema
// A struct is one variable whose fields are its members, packed in declaration order.


// The PLC type name as a DSL type name, its last '.' segment. "" when it is not an identifier.
std::string TypeIdentifier(const std::string& typeName)
{
    const size_t dot = typeName.rfind('.');
    std::string id = dot == std::string::npos ? typeName : typeName.substr(dot + 1);
    if (id.empty() || !((id[0] >= 'A' && id[0] <= 'Z') || (id[0] >= 'a' && id[0] <= 'z'))) return "";
    for (char c : id)
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return "";
    return id;
}


// True if the list contributes at least one field to the schema. An all-Skipped
// struct has nothing to publish (and an empty body is not valid DSL).
bool HasEmittedMember(const std::vector<Member>& fields)
{
    for (const auto& m : fields) {
        if (m.form == Member::Form::Skipped) continue;
        if (m.form == Member::Form::Struct) { if (HasEmittedMember(m.fields)) return true; continue; }
        return true;
    }
    return false;
}

// The member tree of one struct node, in GDS declaration order. An unrepresentable member is
// Skipped so its RSC field is still drained.
void BuildStructMembers(const VariableBrowseHandle& handle, std::vector<Member>& out, int bodyDepth)
{
    bool ok;
    auto children = Children(handle, ok);
    if (!ok) { out.clear(); return; }

    std::vector<std::string> used;
    out.reserve(children.size());

    for (const auto& child : children) {
        const auto& info = child.VariableInfo;
        Member m;
        // The handle's FullName is already the qualified GDS path writes and
        // forces address ("Arp.Plc.Eclr/gSafetyStatus.estop").
        m.gdsName = child.BrowseHandle.FullName.CStr();

        if (IsIgnoredName(m.gdsName)) {
            // still occupies a field on the RSC stream: skip publishing, not draining
        } else if (IsArrayType(info.Type)) {
            // Only a fixed array of a fixed scalar fits inside a struct. An array of strings or
            // structs has no Rant form here.
            Scalar elem = ArrayElementKind(info.Type);
            std::vector<ArrDim> dims; uint32_t total = 0;
            if (elem != Scalar::None && elem != Scalar::String && ReadDims(info, dims, total)) {
                m.form = Member::Form::Array; m.kind = elem;
                m.dims = std::move(dims);     m.count = total;
            }
        } else if (Scalar kind = ScalarForType(info.Type); kind == Scalar::String) {
            m.form = Member::Form::String; m.kind = kind; m.strCap = g_cfg.structStringCap;
        } else if (kind != Scalar::None) {
            m.form = Member::Form::Scalar; m.kind = kind;
        } else if (IsStructType(info.Type) || IsContainerType(info.Type)) {
            if (bodyDepth + 1 > STRUCT_BODY_DEPTH_MAX) {
                log::Error("struct member " + m.gdsName + " nests deeper than RANT allows; skipping it");
            } else {
                std::vector<Member> nested;
                BuildStructMembers(child.BrowseHandle, nested, bodyDepth + 1);
                if (HasEmittedMember(nested)) {
                    m.form     = Member::Form::Struct;
                    m.fields   = std::move(nested);
                    m.typeName = TypeIdentifier(info.TypeId.TypeName.CStr());
                }
            }
        }
        // else: FB instance / unsupported type -> Skipped
        if (m.form != Member::Form::Skipped) m.dslName = SanitizeDslName(LeafName(m.gdsName), used);
        out.push_back(std::move(m));
    }
}

// Collecting variables

void PushVar(std::vector<GdsVar>& vars, const std::string& full, Scalar kind,
             bool isArray, std::vector<ArrDim> dims, uint32_t count)
{
    if (vars.size() >= MAX_VARS) return;
    GdsVar gv;
    if (!ClaimTopic(full, TopicLeaf(full), gv.topic)) return;
    gv.name    = full;
    gv.kind    = kind;
    gv.isArray = isArray;
    gv.dims    = std::move(dims);
    gv.count   = count;

    // The array name itself is usually not forcible: probe its first element.
    std::string probe = full;
    for (const auto& d : gv.dims) probe += "[" + std::to_string(d.offset) + "]";
    gv.forceable = ProbeForceable(probe);

    vars.push_back(std::move(gv));
}

void PushStructVar(std::vector<GdsVar>& vars, const std::string& full, std::vector<Member> fields,
                   const std::string& typeName)
{
    if (vars.size() >= MAX_VARS) return;
    GdsVar gv;
    if (!ClaimTopic(full, TopicLeaf(full), gv.topic)) return;
    gv.name     = full;
    gv.isStruct = true;
    gv.fields   = std::move(fields);
    AssignMemberOffsets(gv.fields, 0);
    gv.typeName = TypeIdentifier(typeName);

    // A force targets a leaf, never the struct: probe the first publishable one.
    NameList leaves;
    MemberLeafNames(gv.fields, leaves);
    if (!leaves.empty()) gv.forceable = ProbeForceable(leaves[0]);

    vars.push_back(std::move(gv));
}

void CollectNode(std::vector<GdsVar>& vars, const std::string& full,
                 const VariableInformation& info, const VariableBrowseHandle& handle, int depth);

// Collect every child of a container node.
void FlattenChildren(std::vector<GdsVar>& vars, const VariableBrowseHandle& handle, int depth)
{
    bool ok;
    for (const auto& child : Children(handle, ok)) {
        if (vars.size() >= MAX_VARS) return;
        CollectNode(vars, child.BrowseHandle.FullName.CStr(), child.VariableInfo,
                    child.BrowseHandle, depth);
    }
}

// Collect one browsed node: a scalar or string leaf, a whole array, a whole data struct, or the
// members of a program or instance container.
void CollectNode(std::vector<GdsVar>& vars, const std::string& full,
                 const VariableInformation& info, const VariableBrowseHandle& handle, int depth)
{
    if (vars.size() >= MAX_VARS) return;
    if (IsIgnoredName(full)) return;

    // Reached by descending a program: one of its own variables, not a global.
    if (depth > 0 && !IsContainerType(info.Type) && !g_cfg.publishLocals && !IsExposed(info)) {
        ++s_localsSkipped;
        log::Info("skipping " + full + " (program-local variable)");
        return;
    }

    if (IsArrayType(info.Type)) {
        Scalar elem = ArrayElementKind(info.Type);
        if (elem == Scalar::None || elem == Scalar::String) {
            log::Info("skipping array " + full + " (element type has no RANT array form)");
            return;
        }
        std::vector<ArrDim> dims; uint32_t total = 0;
        if (!ReadDims(info, dims, total)) {
            log::Error("array " + full + " has no dimension info or an element count outside 1..65535; skipping");
            return;
        }
        PushVar(vars, full, elem, /*isArray=*/true, std::move(dims), total);
        return;
    }

    if (Scalar kind = ScalarForType(info.Type); kind != Scalar::None) {
        PushVar(vars, full, kind, /*isArray=*/false, {}, 0);
        return;
    }

    if (IsContainerType(info.Type)) {
        if (depth >= MAX_BROWSE_DEPTH) { log::Error("browse depth exceeded at " + full); return; }
        log::Info("descending " + full);
        FlattenChildren(vars, handle, depth + 1);
        return;
    }

    if (IsStructType(info.Type)) {
        std::vector<Member> fields;
        BuildStructMembers(handle, fields, STRUCT_BODY_DEPTH_MIN);
        if (fields.empty()) { log::Error("struct " + full + " has no browsable members; skipping"); return; }
        if (!HasEmittedMember(fields)) { log::Info("skipping struct " + full + " (no publishable members)"); return; }
        PushStructVar(vars, full, std::move(fields), info.TypeId.TypeName.CStr());
        return;
    }

    log::Info("skipping " + full + " (data type " + std::to_string(static_cast<uint32_t>(info.Type))
              + ", not published)");
}

} // namespace

std::vector<GdsVar> BrowseVariables()
{
    std::vector<GdsVar> vars;
    if (!g_browseSvc) return vars;
    s_usedTopics.clear();
    s_localsSkipped = 0;

    std::vector<std::string> components;
    try {
        g_browseSvc->GetComponentNames([&components](IRscReadEnumerator<RscStr>& e) {
            RscStr name; e.BeginRead();
            while (e.ReadNext(name)) components.emplace_back(name.CStr());
        });
    } catch (...) { return vars; }

    for (const auto& comp : components) {
        if (comp.rfind("Arp.Plc.Eclr", 0) != 0) continue;

        std::vector<VariableBrowseResult> roots;
        try {
            g_browseSvc->GetRoots(RscStr(comp.c_str()), [&roots](IRscReadEnumerator<VariableBrowseResult>& e) {
                VariableBrowseResult r; e.BeginRead();
                while (e.ReadNext(r)) roots.push_back(r);
            });
        } catch (...) { continue; }

        for (const auto& root : roots) {
            if (vars.size() >= MAX_VARS) break;
            std::string full = comp + "/" + std::string(root.VariableInfo.Name.CStr());
            if (!IsUserVariable(full)) continue;
            CollectNode(vars, full, root.VariableInfo, root.BrowseHandle, 0);
        }
    }
    std::string summary = std::to_string(vars.size()) + " variable(s) found";
    if (s_localsSkipped)
        summary += ", " + std::to_string(s_localsSkipped) + " program-local variable(s) not published (Locals is off)";
    log::Info(summary);
    if (!vars.empty()) log::ArpInfo(summary);
    return vars;
}

} // namespace plc
