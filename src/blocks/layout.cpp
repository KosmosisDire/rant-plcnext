#include "blocks/layout.h"

#include <cstring>

namespace blocks {

using plc::Member;
using plc::Scalar;

namespace {

// The eCLR type codes, as eCLR reflection reports them on the PLC.
enum TypeCode : int {
    CODE_BOOL = 2, CODE_SINT = 4, CODE_USINT = 5, CODE_INT = 6, CODE_UINT = 7, CODE_DINT = 8,
    CODE_UDINT = 9, CODE_LINT = 10, CODE_ULINT = 11, CODE_REAL = 12, CODE_LREAL = 13, CODE_STRUCT = 17
};

Scalar ScalarOf(int code)
{
    switch (code) {
        case CODE_BOOL:  return Scalar::Bool;
        case CODE_SINT:  return Scalar::I8;
        case CODE_USINT: return Scalar::U8;
        case CODE_INT:   return Scalar::I16;
        case CODE_UINT:  return Scalar::U16;
        case CODE_DINT:  return Scalar::I32;
        case CODE_UDINT: return Scalar::U32;
        case CODE_LINT:  return Scalar::I64;
        case CODE_ULINT: return Scalar::U64;
        case CODE_REAL:  return Scalar::F32;
        case CODE_LREAL: return Scalar::F64;
        default: return Scalar::None;
    }
}

// An IEC string is a 2 byte capacity, a 2 byte length, the characters and a nul, padded to an even size.
constexpr uint32_t IEC_STRING_LEN_AT = 2;
constexpr uint32_t IEC_STRING_HEAD = 4;

// The capacity is the header's own when it fits the size, since an odd and an even one can pad to one size.
uint32_t IecStringCap(uint32_t size, const uint8_t* sample, uint32_t offset)
{
    const uint32_t byPad = size > IEC_STRING_HEAD + 2 ? size - IEC_STRING_HEAD - 2 : 0;
    if (!sample) return byPad;
    uint16_t cap = 0;
    std::memcpy(&cap, sample + offset, 2);
    return cap > 0 && (cap == byPad || cap == byPad + 1) ? cap : byPad;
}

// Whether Rant defines the type itself, by its name: Rant Math's own types share the namespace.
bool IsStandardName(const std::string& name)
{
    const rant::detail::RantString s{ name.data(), name.size() };
    return rant::detail::rant_std_by_name(s) != rant::detail::RANT_STD_NONE;
}

bool IsStandard(const Node& n) { return n.space == "Rant" && IsStandardName(n.type); }

bool ValidTypeName(const std::string& s)
{
    if (s.empty() || !((s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= 'a' && s[0] <= 'z'))) return false;
    for (char c : s)
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    return true;
}

// The member of one node: its PLC offset is set here and its wire offset later.
bool Build(const Layout& l, int index, const uint8_t* sample, Member& m, std::string& error)
{
    const Node& n = l.nodes[index];
    m.plcOffset = n.offset;
    if (n.flags & FLAG_STRING) {
        m.form = Member::Form::String;
        m.kind = Scalar::String;
        m.strCap = static_cast<uint16_t>(IecStringCap(n.size, sample, n.offset));
        return true;
    }
    if (n.flags & FLAG_ARRAY) {
        // An array of arrays, the way an IEC program spells two dimensions, is one array of every dimension.
        std::vector<const Node*> levels{ &n };
        const Node* elem = n.children.empty() ? nullptr : &l.nodes[n.children[0]];
        while (elem && (elem->flags & FLAG_ARRAY)) {
            levels.push_back(elem);
            elem = elem->children.empty() ? nullptr : &l.nodes[elem->children[0]];
        }
        const Scalar k = elem && elem->flags == 0 ? ScalarOf(elem->code) : Scalar::None;
        if (k == Scalar::None) {
            error = n.type + ": only an array of elementary values reaches the mesh";
            return false;
        }
        m.form = Member::Form::Array;
        m.kind = k;
        m.standard = IsStandard(n);
        if (m.standard) m.typeName = n.type;
        m.count = 1;
        for (const Node* a : levels) {
            // Every dimension is a lower bound followed by its upper one.
            bool paired = !a->bounds.empty() && a->bounds.size() % 2 == 0;
            for (size_t i = 0; paired && i < a->bounds.size(); i += 2)
                paired = a->boundKinds[i] == 'L' && a->boundKinds[i + 1] == 'U' && a->bounds[i + 1] >= a->bounds[i];
            if (!paired) {
                error = a->type + ": the bounds of the array could not be read";
                return false;
            }
            for (size_t i = 0; i + 1 < a->bounds.size(); i += 2) {
                const uint32_t count = static_cast<uint32_t>(a->bounds[i + 1] - a->bounds[i] + 1);
                m.dims.push_back({ a->bounds[i], count });
                m.count *= count;
            }
        }
        if (n.size < m.count * static_cast<uint32_t>(plc::ScalarByteSize(k))) {
            error = n.type + ": the array is smaller than its bounds say";
            return false;
        }
        return true;
    }
    if (n.code != CODE_STRUCT) {
        m.kind = ScalarOf(n.code);
        if (m.kind == Scalar::None) {
            error = n.type + ": this type does not reach the mesh";
            return false;
        }
        if (n.size < static_cast<uint32_t>(plc::ScalarByteSize(m.kind))) {
            error = n.type + ": the value is smaller than the wire's";
            return false;
        }
        m.form = Member::Form::Scalar;
        return true;
    }
    m.form = Member::Form::Struct;
    m.standard = IsStandard(n);
    m.typeName = m.standard || ValidTypeName(n.type) ? n.type : "";
    // IEC has no empty struct, so the library's Empty holds a placeholder Rant's has not.
    if (m.standard && n.type == "Empty") return true;
    std::vector<std::string> used;
    for (int c : n.children) {
        Member f;
        if (!Build(l, c, sample, f, error)) return false;
        f.dslName = plc::SanitizeDslName(l.nodes[c].name, used);
        m.fields.push_back(std::move(f));
    }
    return true;
}

void Put(const Member& m, const uint8_t* plc, uint8_t* wire)
{
    switch (m.form) {
        case Member::Form::Scalar:
        case Member::Form::Array:
            std::memcpy(wire + m.offset, plc + m.plcOffset, m.size);
            break;
        case Member::Form::String: {
            int16_t live = 0;
            std::memcpy(&live, plc + m.plcOffset + IEC_STRING_LEN_AT, 2);
            const uint16_t n = live < 0 ? 0 : live > m.strCap ? m.strCap : static_cast<uint16_t>(live);
            std::memcpy(wire + m.offset, &n, 2);
            std::memcpy(wire + m.offset + 2, plc + m.plcOffset + IEC_STRING_HEAD, n);
            std::memset(wire + m.offset + 2 + n, 0, m.strCap - n);
            break;
        }
        case Member::Form::Struct:
            for (const Member& f : m.fields) Put(f, plc, wire);
            break;
        case Member::Form::Skipped: break;
    }
}

// Writes an IEC string, cut at the capacity the PLC gave it.
void PutIecString(uint8_t* at, uint32_t cap, const uint8_t* chars, size_t n)
{
    int16_t max = 0;
    std::memcpy(&max, at, 2);
    const size_t room = max > 0 && static_cast<uint32_t>(max) < cap ? static_cast<uint32_t>(max) : cap;
    const int16_t live = static_cast<int16_t>(n < room ? n : room);
    std::memcpy(at + IEC_STRING_LEN_AT, &live, 2);
    std::memcpy(at + IEC_STRING_HEAD, chars, live);
    at[IEC_STRING_HEAD + live] = 0;
}

bool Get(const Member& m, const uint8_t* wire, uint8_t* plc)
{
    switch (m.form) {
        case Member::Form::Scalar:
        case Member::Form::Array:
            std::memcpy(plc + m.plcOffset, wire + m.offset, m.size);
            return true;
        case Member::Form::String: {
            uint16_t n = 0;
            std::memcpy(&n, wire + m.offset, 2);
            if (n > m.strCap) return false;
            PutIecString(plc + m.plcOffset, m.strCap, wire + m.offset + 2, n);
            return true;
        }
        case Member::Form::Struct:
            for (const Member& f : m.fields)
                if (!Get(f, wire, plc)) return false;
            return true;
        case Member::Form::Skipped: return true;
    }
    return true;
}

} // namespace

void Restart(Layout& l, uintptr_t type)
{
    l = Layout{};
    l.type = type;
}

int AddNode(Layout& l, int parent, const char* name, const char* space, const char* type, uint32_t offset,
            uint32_t size, int code, int flags)
{
    if (parent >= static_cast<int>(l.nodes.size()) || (parent < 0 && !l.nodes.empty())) return -1;
    Node n;
    n.parent = parent;
    n.name = name ? name : "";
    n.space = space ? space : "";
    n.type = type ? type : "";
    n.offset = offset;
    n.size = size;
    n.code = code;
    n.flags = flags;
    l.nodes.push_back(std::move(n));
    const int index = static_cast<int>(l.nodes.size()) - 1;
    if (parent >= 0) l.nodes[parent].children.push_back(index);
    return index;
}

void AddBound(Layout& l, int node, const char* name, int32_t value)
{
    if (node < 0 || node >= static_cast<int>(l.nodes.size()) || !name) return;
    // The order they arrive in is checked in Build, which wants each LB followed by its UB.
    if (name[0] != 'L' && name[0] != 'U') return;
    l.nodes[node].bounds.push_back(value);
    l.nodes[node].boundKinds.push_back(name[0]);
}

void Finish(Layout& l, const uint8_t* sample)
{
    l.done = true;
    if (l.nodes.empty()) { l.error = "the walk reported no type"; return; }
    Member root;
    if (!Build(l, 0, sample, root, l.error)) return;
    switch (root.form) {
        case Member::Form::Struct: root.size = plc::AssignMemberOffsets(root.fields, 0); break;
        case Member::Form::Scalar: root.size = static_cast<uint32_t>(plc::ScalarByteSize(root.kind)); break;
        case Member::Form::Array:  root.size = root.count * static_cast<uint32_t>(plc::ScalarByteSize(root.kind)); break;
        default: break;
    }
    l.root = std::move(root);
    l.schema = plc::SchemaText(l.root);
}

void Encode(const Layout& l, const uint8_t* plc, std::vector<uint8_t>& wire)
{
    const Member& r = l.root;
    if (r.form == Member::Form::String) {
        int16_t live = 0;
        std::memcpy(&live, plc + IEC_STRING_LEN_AT, 2);
        const uint32_t n = live < 0 ? 0 : live > r.strCap ? r.strCap : static_cast<uint32_t>(live);
        wire.resize(4 + n);
        std::memcpy(wire.data(), &n, 4);
        std::memcpy(wire.data() + 4, plc + IEC_STRING_HEAD, n);
        return;
    }
    wire.assign(r.size, 0);
    Put(r, plc, wire.data());
}

bool Decode(const Layout& l, const uint8_t* wire, size_t len, uint8_t* plc)
{
    const Member& r = l.root;
    if (r.form == Member::Form::String) {
        uint32_t n = 0;
        if (len < 4) return false;
        std::memcpy(&n, wire, 4);
        if (n > len - 4) return false;
        PutIecString(plc, r.strCap, wire + 4, n);
        return true;
    }
    if (len != r.size) return false;
    return Get(r, wire, plc);
}

} // namespace blocks
