#include "blocks/message.h"

#include "blocks/layout.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace blocks::msg {

namespace {

namespace d = rant::detail;

// The eCLR type codes a value may have, as layout.cpp reads them.
enum Code : uint8_t {
    CODE_BOOL = 2, CODE_SINT = 4, CODE_USINT = 5, CODE_INT = 6, CODE_UINT = 7, CODE_DINT = 8,
    CODE_UDINT = 9, CODE_LINT = 10, CODE_ULINT = 11, CODE_REAL = 12, CODE_LREAL = 13
};

// Where a message's parts sit in its struct. The used count and fault length are 0 in a
// Message variable the program never assigned, which makes it a message with no fields.
constexpr size_t USED_AT = 0;          // u16
constexpr size_t FAULT_LENGTH_AT = 2;  // u8
constexpr size_t FAULT_AT = 4;
constexpr size_t FAULT_BYTES = 80;     // an IEC STRING's capacity
constexpr size_t ENTRIES_AT = FAULT_AT + FAULT_BYTES;
constexpr size_t ENTRY_BYTES = MESSAGE_BYTES - ENTRIES_AT;   // [u8 name length][name][u32 type][u32 length][value]
constexpr size_t NAME_BYTES = 64;      // as long as an entity name may be

static_assert(ENTRY_BYTES == 1024, "iec/Message.cs sizes the struct for 1024 bytes of entries");

uint8_t  CodeOf(uint32_t type) { return static_cast<uint8_t>(type); }
uint8_t  FlagsOf(uint32_t type) { return static_cast<uint8_t>(type >> 8); }
uint32_t ElementSizeOf(uint32_t type) { return type >> 16; }

bool IsSigned(uint8_t code) { return code == CODE_SINT || code == CODE_INT || code == CODE_DINT || code == CODE_LINT; }
bool IsUnsigned(uint8_t code) { return code == CODE_USINT || code == CODE_UINT || code == CODE_UDINT || code == CODE_ULINT; }

// The Rant kind an elementary IEC value has on the wire, or 0xff for none.
uint8_t RantKind(uint8_t code)
{
    switch (code) {
        case CODE_BOOL:  return d::RANT_BOOL;
        case CODE_SINT:  return d::RANT_I8;
        case CODE_USINT: return d::RANT_U8;
        case CODE_INT:   return d::RANT_I16;
        case CODE_UINT:  return d::RANT_U16;
        case CODE_DINT:  return d::RANT_I32;
        case CODE_UDINT: return d::RANT_U32;
        case CODE_LINT:  return d::RANT_I64;
        case CODE_ULINT: return d::RANT_U64;
        case CODE_REAL:  return d::RANT_F32;
        case CODE_LREAL: return d::RANT_F64;
        default: return 0xff;
    }
}

// An IEC string's memory is a 2 byte capacity, a 2 byte length, then the characters.
bool Text(const Item& i, const char*& chars, size_t& n)
{
    if (!(FlagsOf(i.type) & FLAG_STRING) || !i.data || i.length < 4) return false;
    int16_t live = 0;
    std::memcpy(&live, i.data + 2, 2);
    n = live < 0 ? 0 : std::min<size_t>(static_cast<size_t>(live), i.length - 4);
    chars = reinterpret_cast<const char*>(i.data + 4);
    return true;
}

uint16_t Used(const uint8_t* m)
{
    uint16_t used = 0;
    std::memcpy(&used, m + USED_AT, 2);
    return std::min<uint16_t>(used, ENTRY_BYTES);
}

// The first fault is kept, since it is the one to fix.
void Fault(uint8_t* m, const char* why)
{
    if (m[FAULT_LENGTH_AT]) return;
    const size_t n = std::min(std::strlen(why), FAULT_BYTES);
    std::memcpy(m + FAULT_AT, why, n);
    m[FAULT_LENGTH_AT] = static_cast<uint8_t>(n);
}

// Whether v fits a field of kind, an integer of either sign.
bool Fits(uint8_t kind, bool negative, uint64_t magnitude)
{
    switch (kind) {
        case d::RANT_U8:  return !negative && magnitude <= 0xffu;
        case d::RANT_U16: return !negative && magnitude <= 0xffffu;
        case d::RANT_U32: return !negative && magnitude <= 0xffffffffu;
        case d::RANT_U64: return !negative;
        case d::RANT_I8:  return negative ? magnitude <= 0x80u : magnitude <= 0x7fu;
        case d::RANT_I16: return negative ? magnitude <= 0x8000u : magnitude <= 0x7fffu;
        case d::RANT_I32: return negative ? magnitude <= 0x80000000u : magnitude <= 0x7fffffffu;
        case d::RANT_I64: return negative ? magnitude <= 0x8000000000000000u : magnitude <= 0x7fffffffffffffffu;
        default: return false;
    }
}

bool Set(const d::RantSchema* s, uint8_t* buf, size_t cap, const std::string& name, uint32_t type,
         const uint8_t* v, uint32_t length, std::string& why)
{
    const int at = d::rant_schema_field_index(s, name.c_str());
    d::RantSchemaFieldInfo f;
    if (at < 0 || !d::rant_schema_field_at(s, static_cast<uint16_t>(at), &f)) {
        why = name + " is not a field of the type";
        return false;
    }
    const char* path = name.c_str();
    const uint8_t code = CodeOf(type), flags = FlagsOf(type), kind = f.kind;
    bool ok = false;
    if (flags & FLAG_ARRAY) {
        // Packed elements go as they are, so the element must be the field's own.
        d::RantSchemaFieldInfo e;
        ok = (kind == d::RANT_ARR || kind == d::RANT_VARR) && d::rant_schema_field_at(s, static_cast<uint16_t>(at + 1), &e)
          && RantKind(code) == e.kind && ElementSizeOf(type) == e.size
          && d::rant_set_array(buf, cap, s, path, d::rant_bytes(v, length));
    } else if (flags & FLAG_STRING) {
        if (kind == d::RANT_ENUM) {
            ok = d::rant_set_enum(buf, cap, s, path, std::string(reinterpret_cast<const char*>(v), length).c_str());
        } else {
            const d::RantString t{ reinterpret_cast<const char*>(v), length };
            ok = (kind == d::RANT_STR || kind == d::RANT_VSTR) && d::rant_set_string(buf, cap, s, path, t);
        }
    } else if (code == CODE_BOOL) {
        ok = kind == d::RANT_BOOL && length >= 1 && d::rant_set_uint(buf, cap, s, path, v[0] != 0);
    } else if (code == CODE_REAL || code == CODE_LREAL) {
        double x = 0;
        if (code == CODE_REAL && length >= 4) { float r; std::memcpy(&r, v, 4); x = r; }
        else if (code == CODE_LREAL && length >= 8) std::memcpy(&x, v, 8);
        ok = (kind == d::RANT_F32 || kind == d::RANT_F64) && d::rant_set_f64(buf, cap, s, path, x);
    } else if ((IsSigned(code) || IsUnsigned(code)) && length >= 1 && length <= 8) {
        // An IEC integer or enum, widened by its own sign.
        uint64_t raw = 0;
        std::memcpy(&raw, v, length);
        int64_t i = 0;
        if (IsSigned(code)) {
            const int shift = 64 - 8 * static_cast<int>(length);
            i = static_cast<int64_t>(raw << shift) >> shift;
        }
        const bool negative = IsSigned(code) && i < 0;
        const uint64_t magnitude = !IsSigned(code) ? raw : negative ? 0 - static_cast<uint64_t>(i) : static_cast<uint64_t>(i);
        if (kind == d::RANT_ENUM) {
            const int64_t number = IsSigned(code) ? i : static_cast<int64_t>(raw);
            const d::RantString option = d::rant_enum_name_of(s, static_cast<uint16_t>(at), number);
            ok = option.data && d::rant_set_enum(buf, cap, s, path, std::string(option.data, option.len).c_str());
        } else if (kind == d::RANT_F32 || kind == d::RANT_F64) {
            ok = d::rant_set_f64(buf, cap, s, path, IsSigned(code) ? static_cast<double>(i) : static_cast<double>(raw));
        } else if (Fits(kind, negative, magnitude)) {
            ok = kind <= d::RANT_U64 ? d::rant_set_uint(buf, cap, s, path, magnitude)
                                     : d::rant_set_int(buf, cap, s, path, IsSigned(code) ? i : static_cast<int64_t>(raw));
        }
    }
    if (!ok) why = "the value of " + name + " does not fit the field";
    return ok;
}

} // namespace

bool IsMessageType(const char* space, const char* type)
{
    return space && type && std::strcmp(space, "Rant") == 0 && std::strcmp(type, "Message") == 0;
}

void Begin(uint8_t* m)
{
    std::memset(m, 0, ENTRIES_AT);
}

namespace {

// Writes one entry, its value already as an entry keeps it.
void Put(uint8_t* m, const char* name, size_t n, uint32_t type, const uint8_t* bytes, size_t length)
{
    const uint16_t used = Used(m);
    const size_t need = 1 + n + 4 + 4 + length;
    if (used + need > ENTRY_BYTES) return Fault(m, "the message is over 1024 bytes");
    uint8_t* p = m + ENTRIES_AT + used;
    *p++ = static_cast<uint8_t>(n);
    std::memcpy(p, name, n);
    p += n;
    const uint32_t size = static_cast<uint32_t>(length);
    std::memcpy(p, &type, 4);
    std::memcpy(p + 4, &size, 4);
    if (length) std::memcpy(p + 8, bytes, length);
    const uint16_t now = static_cast<uint16_t>(used + need);
    std::memcpy(m + USED_AT, &now, 2);
}

// A value's bytes as an entry keeps them: a string's characters without its header.
void EntryBytes(const Item& value, const uint8_t*& bytes, size_t& length)
{
    bytes = value.data;
    length = value.data ? value.length : 0;
    if (FlagsOf(value.type) & FLAG_STRING) {
        const char* chars = nullptr;
        Text(value, chars, length);
        bytes = reinterpret_cast<const uint8_t*>(chars);
    }
}

// The Field of pair index, empty only where the fields of a message or struct go to the top.
bool FieldName(uint8_t* m, int index, const Item& field, bool emptyOk, const char*& name, size_t& n)
{
    char why[FAULT_BYTES + 1];
    if (!Text(field, name, n)) {
        std::snprintf(why, sizeof why, "Field%d wants a field name in a STRING", index);
        Fault(m, why);
        return false;
    }
    if ((n == 0 && !emptyOk) || n > NAME_BYTES) {
        std::snprintf(why, sizeof why, "Field%d is empty or over 64 bytes", index);
        Fault(m, why);
        return false;
    }
    return true;
}

// Appends part to the path in out of length n, after a dot unless it starts an index.
bool Join(char (&out)[NAME_BYTES], size_t& n, const char* part, size_t length)
{
    const bool dot = n > 0 && part[0] != '[';
    if (n + dot + length > NAME_BYTES) return false;
    if (dot) out[n++] = '.';
    std::memcpy(out + n, part, length);
    n += length;
    return true;
}

// The member names of a struct value down to the one being added, each spelled as Rant spells
// fields. The task's own, since a StructBuilder runs on its task's thread start to end.
constexpr int DEPTH_MAX = 8;
thread_local char t_members[DEPTH_MAX][NAME_BYTES];
thread_local size_t t_memberLength[DEPTH_MAX];

} // namespace

void Add(uint8_t* m, int index, Item field, Item value)
{
    const char* name = nullptr;
    size_t n = 0;
    if (!FieldName(m, index, field, false, name, n)) return;
    const uint8_t* bytes = nullptr;
    size_t length = 0;
    EntryBytes(value, bytes, length);
    Put(m, name, n, value.type, bytes, length);
}

void Nest(uint8_t* m, int index, Item field, const uint8_t* inner, size_t length)
{
    char why[FAULT_BYTES + 1];
    const char* prefix = nullptr;
    size_t n = 0;
    if (!FieldName(m, index, field, true, prefix, n)) return;
    if (length != MESSAGE_BYTES) return Fault(m, "a StructBuilder was given a struct of another size");
    if (inner[FAULT_LENGTH_AT]) {
        std::snprintf(why, sizeof why, "Value%d: %.*s", index, static_cast<int>(inner[FAULT_LENGTH_AT]),
                      reinterpret_cast<const char*>(inner + FAULT_AT));
        return Fault(m, why);
    }
    const uint8_t* entries = inner + ENTRIES_AT;
    const size_t used = Used(inner);
    for (size_t at = 0; at < used;) {
        const uint8_t* p = entries + at;
        const size_t nameLength = p[0];
        uint32_t type = 0, size = 0;
        if (at + 9 + nameLength > used) break;
        std::memcpy(&type, p + 1 + nameLength, 4);
        std::memcpy(&size, p + 5 + nameLength, 4);
        if (at + 9 + nameLength + size > used) break;
        char path[NAME_BYTES];
        size_t pathLength = 0;
        if (!Join(path, pathLength, prefix, n) || !Join(path, pathLength, reinterpret_cast<const char*>(p + 1), nameLength)) {
            std::snprintf(why, sizeof why, "Field%d makes a path over 64 bytes", index);
            return Fault(m, why);
        }
        Put(m, path, pathLength, type, p + 9 + nameLength, size);
        at += 9 + nameLength + size;
    }
}

void Path(int depth, const char* member)
{
    if (depth < 0 || depth >= DEPTH_MAX || !member) return;
    // An IEC member name as a Rant field name, as plc::SanitizeDslName spells it.
    char raw[NAME_BYTES + 2];
    size_t n = 0;
    if (member[0] >= '0' && member[0] <= '9') raw[n++] = 'f';
    for (const char* c = member; *c && n < NAME_BYTES; ++c) {
        const bool ok = (*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_';
        raw[n++] = ok ? *c : '_';
    }
    raw[n] = '\0';
    t_memberLength[depth] = rant::detail::rant_field_name(raw, t_members[depth], NAME_BYTES);
}

void Leaf(uint8_t* m, int index, Item field, int depth, Item value)
{
    char why[FAULT_BYTES + 1];
    const char* prefix = nullptr;
    size_t n = 0;
    if (!FieldName(m, index, field, true, prefix, n)) return;
    if (depth > DEPTH_MAX) {
        std::snprintf(why, sizeof why, "Value%d nests structs deeper than %d", index, DEPTH_MAX);
        return Fault(m, why);
    }
    char path[NAME_BYTES];
    size_t pathLength = 0;
    bool fits = Join(path, pathLength, prefix, n);
    for (int i = 0; fits && i < depth; ++i) fits = t_memberLength[i] && Join(path, pathLength, t_members[i], t_memberLength[i]);
    if (!fits) {
        std::snprintf(why, sizeof why, "Field%d makes a path over 64 bytes", index);
        return Fault(m, why);
    }
    const uint8_t* bytes = nullptr;
    size_t length = 0;
    EntryBytes(value, bytes, length);
    Put(m, path, pathLength, value.type, bytes, length);
}

void Read(const uint8_t* m, size_t length, const char* type, size_t typeLength, Message& out)
{
    out.type.assign(type, typeLength);
    out.fault.clear();
    out.entries.clear();
    if (!m) return;
    if (length != MESSAGE_BYTES) {
        out.fault.assign("the Message holds no message of this library");
        return;
    }
    out.fault.assign(reinterpret_cast<const char*>(m + FAULT_AT), std::min<size_t>(m[FAULT_LENGTH_AT], FAULT_BYTES));
    out.entries.assign(m + ENTRIES_AT, m + ENTRIES_AT + Used(m));
}

bool Apply(const Message& m, const d::RantSchema* s, std::vector<uint8_t>& out, std::string& why)
{
    if (!m.fault.empty()) {
        why = m.fault;
        return false;
    }
    // A string's bytes are in the entries, so the entries bound what the message can grow by.
    const size_t cap = d::rant_schema_msg_min(s) + m.entries.size() + 64;
    out.assign(cap, 0);
    if (!d::rant_schema_message_default(s, out.data(), cap)) {
        why = "the type has no default message";
        return false;
    }
    std::string name;
    for (size_t at = 0; at < m.entries.size();) {
        const uint8_t* p = m.entries.data() + at;
        const size_t nameLength = p[0];
        uint32_t type = 0, length = 0;
        if (at + 1 + nameLength + 8 > m.entries.size()) break;
        name.assign(reinterpret_cast<const char*>(p + 1), nameLength);
        std::memcpy(&type, p + 1 + nameLength, 4);
        std::memcpy(&length, p + 5 + nameLength, 4);
        const size_t value = at + 9 + nameLength;
        if (value + length > m.entries.size()) break;
        if (!Set(s, out.data(), cap, name, type, m.entries.data() + value, length, why)) return false;
        at = value + length;
    }
    out.resize(d::rant_schema_msg_len(s, out.data(), cap));
    return true;
}

void Names(const Message& m, std::vector<std::string>& out)
{
    out.clear();
    for (size_t at = 0; at < m.entries.size();) {
        const uint8_t* p = m.entries.data() + at;
        const size_t nameLength = p[0];
        uint32_t length = 0;
        if (at + 9 + nameLength > m.entries.size()) break;
        std::memcpy(&length, p + 5 + nameLength, 4);
        std::string name(reinterpret_cast<const char*>(p + 1), nameLength);
        if (std::find(out.begin(), out.end(), name) == out.end()) out.push_back(std::move(name));
        at += 9 + nameLength + length;
    }
}

namespace {

const char* KindName(uint8_t kind)
{
    static const char* const names[] = { "u8", "u16", "u32", "u64", "i8", "i16", "i32", "i64", "f32", "f64", "bool" };
    return kind <= d::RANT_BOOL ? names[kind] : nullptr;
}

// Writes the definitions a cut down type needs, each name once, nested ones ahead of their users.
struct Cut {
    const d::RantSchema* s;
    std::vector<d::RantSchemaFieldInfo> rows;
    std::vector<int> parent;
    std::vector<char> keep;
    std::vector<std::pair<std::string, std::string>> defined;   // name and its definition
    std::string text, why;

    std::string Name(int row) const { return std::string(rows[row].type_name.data, rows[row].type_name.len); }

    void Define(const std::string& name, const std::string& definition)
    {
        for (const auto& [n, def] : defined)
            if (n == name) {
                if (def != definition && why.empty()) why = name + " would need two shapes, set its fields through one path";
                return;
            }
        defined.emplace_back(name, definition);
        text += definition + "\n";
    }

    std::string Body(int of)
    {
        std::string out;
        for (size_t i = 0; i < rows.size(); ++i)
            if (parent[i] == of && keep[i])
                out += "    " + std::string(rows[i].name.data, rows[i].name.len) + ": " + Type(static_cast<int>(i)) + ",\n";
        return out;
    }

    std::string Type(int row)
    {
        const d::RantSchemaFieldInfo& f = rows[row];
        switch (f.kind) {
            case d::RANT_STRUCT: {
                const std::string name = Name(row);
                Define(name, name + "\n{\n" + Body(row) + "}");
                return name;
            }
            case d::RANT_ENUM: {
                const std::string name = Name(row);
                std::string def = name + " = enum<" + (KindName(f.backing) ? KindName(f.backing) : "i32") + "> {";
                const uint16_t n = d::rant_schema_enum_count(s, static_cast<uint16_t>(row));
                for (uint16_t i = 0; i < n; ++i) {
                    int64_t value = 0;
                    d::RantString option{};
                    d::rant_schema_enum_variant(s, static_cast<uint16_t>(row), i, &value, &option);
                    def += (i ? ", " : " ") + std::string(option.data, option.len) + "=" + std::to_string(value);
                }
                Define(name, def + " }");
                return name;
            }
            case d::RANT_STR:  return "string<" + std::to_string(f.str_cap) + ">";
            case d::RANT_VSTR: return "string";
            case d::RANT_MAP:  return "map";
            case d::RANT_ARR:  return Type(row + 1) + "[" + std::to_string(f.count) + "]";
            case d::RANT_VARR: return Type(row + 1) + "[]";
            default: {
                const char* kind = KindName(f.kind);
                if (!kind) {
                    if (why.empty()) why = "a field of the type has a kind a writer cannot name";
                    return "u8";
                }
                // A scalar that carries a type name is an alias of it.
                if (f.type_name.len == 0) return kind;
                const std::string name = Name(row);
                Define(name, name + " = " + kind);
                return name;
            }
        }
    }
};

} // namespace

std::string SubsetText(const d::RantSchema* s, const std::vector<std::string>& names, std::string& why)
{
    const d::RantString root = d::rant_schema_name(s);
    Cut c{ s, {}, {}, {}, {}, {}, {} };
    const uint16_t n = d::rant_schema_field_count(s);
    c.rows.resize(n);
    c.parent.assign(n, -1);
    c.keep.assign(n, 0);
    std::vector<int> stack;
    for (uint16_t i = 0; i < n; ++i) {
        d::rant_schema_field_at(s, i, &c.rows[i]);
        stack.resize(c.rows[i].depth);
        c.parent[i] = stack.empty() ? -1 : stack.back();
        stack.push_back(i);
    }
    if (root.len == 0 || n == 0 || (n >= 1 && c.rows[0].name.len == 0)) {
        why = "the variable's type has no fields, write it whole through Value";
        return "";
    }
    for (const std::string& name : names) {
        const int at = d::rant_schema_field_index(s, name.c_str());
        if (at < 0) {
            why = name + " is not a field of the type";
            return "";
        }
        if (c.rows[at].arr_depth > 0) {
            why = name + " is inside an array, which a writer sets whole";
            return "";
        }
        for (int r = c.parent[at]; r >= 0; r = c.parent[r])
            if (d::rant_std_by_name(c.rows[r].type_name) != d::RANT_STD_NONE) {
                why = name + " is inside " + c.Name(r) + ", a standard type a message cannot split";
                return "";
            }
        // A field is written whole, with everything inside it, and so is the way to it.
        for (int r = at; r >= 0; r = c.parent[r]) c.keep[r] = 1;
        for (int r = at + 1; r < n && c.rows[r].depth > c.rows[at].depth; ++r) c.keep[r] = 1;
    }
    const std::string rootName(root.data, root.len);
    c.Define(rootName, rootName + "\n{\n" + c.Body(-1) + "}");
    why = c.why;
    return why.empty() ? c.text : "";
}

} // namespace blocks::msg
