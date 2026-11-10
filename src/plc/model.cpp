#include "plc/model.h"

#include <algorithm>
#include <cstdio>

namespace plc {

std::string SanitizeDslName(const std::string& raw, std::vector<std::string>& used)
{
    std::string out;
    for (char c : raw) {
        bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                  (c >= '0' && c <= '9') || c == '_';
        out.push_back(ok ? c : '_');
    }
    if (out.empty() || (out[0] >= '0' && out[0] <= '9')) out.insert(out.begin(), 'f');
    if (out.size() > 255) out.resize(255);

    std::string candidate = out;
    for (int n = 2; ; ++n) {
        char wire[300];
        const size_t len = rant::detail::rant_field_name(candidate.c_str(), wire, sizeof wire);
        if (len && std::find(used.begin(), used.end(), std::string(wire, len)) == used.end()) {
            used.emplace_back(wire, len);
            return candidate;
        }
        candidate = (len ? out : std::string("f")) + "_" + std::to_string(n);
    }
}

uint32_t AssignMemberOffsets(std::vector<Member>& fields, uint32_t base)
{
    uint32_t cursor = base;
    for (auto& m : fields) {
        m.offset = cursor;
        switch (m.form) {
            case Member::Form::Scalar:  m.size = static_cast<uint32_t>(ScalarByteSize(m.kind)); break;
            case Member::Form::String:  m.size = 2u + m.strCap; break;
            case Member::Form::Array:   m.size = m.count * static_cast<uint32_t>(ScalarByteSize(m.kind)); break;
            case Member::Form::Struct:  m.size = AssignMemberOffsets(m.fields, cursor); break;
            case Member::Form::Skipped: m.size = 0; break;
        }
        cursor += m.size;
    }
    return cursor - base;
}

void ArrayElementNames(const std::string& base, const std::vector<ArrDim>& dims, NameList& out)
{
    const size_t nd = dims.size();
    if (nd == 0) return;
    std::vector<uint64_t> stride(nd, 1);
    for (size_t k = nd - 1; k-- > 0; ) stride[k] = stride[k + 1] * dims[k + 1].count;
    uint64_t total = 1;
    for (const auto& d : dims) total *= d.count;
    out.reserve(out.size() + total);
    for (uint64_t m = 0; m < total; ++m) {
        std::string nm = base;
        for (size_t k = 0; k < nd; ++k) {
            uint64_t ik = (m / stride[k]) % dims[k].count;
            nm += "[" + std::to_string(dims[k].offset + static_cast<int64_t>(ik)) + "]";
        }
        out.push_back(std::move(nm));
    }
}

void MemberLeafNames(const std::vector<Member>& fields, NameList& out)
{
    for (const auto& m : fields) {
        switch (m.form) {
            case Member::Form::Skipped: break;
            case Member::Form::Scalar:
            case Member::Form::String:  out.push_back(m.gdsName); break;
            case Member::Form::Array:   ArrayElementNames(m.gdsName, m.dims, out); break;
            case Member::Form::Struct:  MemberLeafNames(m.fields, out); break;
        }
    }
}

void MemberForceNames(const std::vector<Member>& fields, NameList& out)
{
    for (const auto& m : fields) {
        switch (m.form) {
            case Member::Form::Skipped: break;
            case Member::Form::Scalar:
            case Member::Form::String:
            case Member::Form::Array:   out.push_back(m.gdsName); break;
            case Member::Form::Struct:  MemberForceNames(m.fields, out); break;
        }
    }
}

namespace {
std::string ShapeName(const std::string& body)
{
    uint64_t h = 1469598103934665603ull;   // FNV-1a over the body, so one shape is one name
    for (char c : body) { h ^= static_cast<uint8_t>(c); h *= 1099511628211ull; }
    char hashed[24];
    std::snprintf(hashed, sizeof hashed, "S%016llx", static_cast<unsigned long long>(h));
    return hashed;
}

// One size per dimension, the innermost first as the DSL reads it: a 2 by 3 INT array is i16[3][2],
// two arrays of three, row major as the bytes are.
std::string ArrayType(const Member& m)
{
    if (m.standard) return m.typeName;
    std::string out = SCALAR_TYPES[static_cast<int>(m.kind)];
    if (m.dims.empty()) return out + "[" + std::to_string(m.count) + "]";
    for (auto d = m.dims.rbegin(); d != m.dims.rend(); ++d) out += "[" + std::to_string(d->count) + "]";
    return out;
}

// Rant refuses an unnamed struct, so each nested one is defined in defs ahead of its user.
std::string StructBody(const std::vector<Member>& fields, bool hashName, std::string& defs,
                       NameList& defined)
{
    std::string out;
    for (const auto& m : fields) {
        switch (m.form) {
            case Member::Form::Skipped: break;
            case Member::Form::Scalar:
                out += "    " + m.dslName + ": " + SCALAR_TYPES[static_cast<int>(m.kind)] + ",\n";
                break;
            case Member::Form::String:
                out += "    " + m.dslName + ": string<" + std::to_string(m.strCap) + ">,\n";
                break;
            case Member::Form::Array:
                out += "    " + m.dslName + ": " + ArrayType(m) + ",\n";
                break;
            case Member::Form::Struct: {
                if (m.standard) {
                    out += "    " + m.dslName + ": " + m.typeName + ",\n";
                    break;
                }
                const std::string body = StructBody(m.fields, hashName, defs, defined);
                const std::string name = hashName || m.typeName.empty() ? ShapeName(body) : m.typeName;
                if (std::find(defined.begin(), defined.end(), name) == defined.end()) {
                    defined.push_back(name);
                    defs += name + "\n{\n" + body + "}\n";
                }
                out += "    " + m.dslName + ": " + name + ",\n";
                break;
            }
        }
    }
    return out;
}
} // namespace

std::string SchemaText(const Member& root, bool hashName)
{
    switch (root.form) {
        case Member::Form::Struct: {
            if (root.standard) return root.typeName;
            std::string defs;
            NameList defined;
            const std::string body = StructBody(root.fields, hashName, defs, defined);
            const std::string name = hashName || root.typeName.empty() ? ShapeName(body) : root.typeName;
            return defs + name + "\n{\n" + body + "}";
        }
        case Member::Form::String: return "string";
        case Member::Form::Array: return ArrayType(root);
        case Member::Form::Scalar: return SCALAR_TYPES[static_cast<int>(root.kind)];
        case Member::Form::Skipped: break;
    }
    return "";
}

std::string SchemaText(const GdsVar& gv, bool hashName)
{
    Member root;
    root.form = gv.isStruct ? Member::Form::Struct
              : gv.kind == Scalar::String ? Member::Form::String
              : gv.isArray ? Member::Form::Array : Member::Form::Scalar;
    root.kind = gv.kind;
    root.count = gv.count;
    root.dims = gv.dims;
    root.fields = gv.fields;
    root.typeName = gv.typeName;
    return SchemaText(root, hashName);
}

void ElementNames(const GdsVar& gv, NameList& out)
{
    if (gv.isStruct)     MemberLeafNames(gv.fields, out);
    else if (gv.isArray) ArrayElementNames(gv.name, gv.dims, out);
    else                 out.push_back(gv.name);
}

namespace {
size_t MemberWeight(const std::vector<Member>& fields)
{
    size_t w = 0;
    for (const auto& m : fields) {
        switch (m.form) {
            case Member::Form::Skipped:                       // drained, so still read
            case Member::Form::Scalar:
            case Member::Form::String: w += 1; break;
            case Member::Form::Array:  w += m.count ? m.count : 1; break;
            case Member::Form::Struct: w += MemberWeight(m.fields); break;
        }
    }
    return w;
}
} // namespace

size_t SampleWeight(const GdsVar& gv)
{
    size_t w = 1;
    if (gv.isStruct)     w = MemberWeight(gv.fields);
    else if (gv.isArray) w = gv.count;
    return w ? w : 1;
}

} // namespace plc
