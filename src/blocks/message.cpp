#include "blocks/message.h"

#include "blocks/layout.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <limits>

namespace blocks::msg {

namespace {

namespace d = rant::detail;

// The eCLR type codes a value may have, as layout.cpp reads them.
enum Code : uint8_t {
    CODE_BOOL = 2, CODE_SINT = 4, CODE_USINT = 5, CODE_INT = 6, CODE_UINT = 7, CODE_DINT = 8,
    CODE_UDINT = 9, CODE_LINT = 10, CODE_ULINT = 11, CODE_REAL = 12, CODE_LREAL = 13
};

constexpr size_t SLOTS = 256;          // messages in flight across every task, the low byte of a handle
constexpr size_t ENTRY_BYTES = 1024;
constexpr size_t NAME_BYTES = 64;        // as long as an entity name may be
constexpr size_t FAULT_BYTES = 80;       // an IEC STRING's capacity

// One message. Its handle is 0 while a new message fills it, so a reader of the old one sees it go.
struct Slot {
    std::atomic<uint32_t> handle{0};
    uint16_t used = 0;
    uint8_t  typeLength = 0, pendingLength = 0, faultLength = 0;
    bool     pending = false;          // a field name waits for its value
    char     type[NAME_BYTES];
    char     pendingName[NAME_BYTES];
    char     fault[FAULT_BYTES];
    uint8_t  entries[ENTRY_BYTES];     // [u8 name length][name][u32 type][u32 length][value]
};

Slot g_slots[SLOTS];
std::atomic<uint32_t> g_next{0};

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

// The first fault is kept, since it is the one to fix.
void Fault(Slot& s, const char* why)
{
    if (s.faultLength) return;
    const size_t n = std::min(std::strlen(why), FAULT_BYTES);
    std::memcpy(s.fault, why, n);
    s.faultLength = static_cast<uint8_t>(n);
}

void Name(Slot& s, const Item& b)
{
    const char* chars = nullptr;
    size_t n = 0;
    if (!Text(b, chars, n)) return Fault(s, "Msg wants a field name before each value");
    if (n == 0 || n > NAME_BYTES) return Fault(s, "Msg was given an empty field name or one over 64 bytes");
    std::memcpy(s.pendingName, chars, n);
    s.pendingLength = static_cast<uint8_t>(n);
    s.pending = true;
}

void Value(Slot& s, const Item& b)
{
    s.pending = false;
    const uint8_t* bytes = b.data;
    size_t n = b.data ? b.length : 0;
    if (FlagsOf(b.type) & FLAG_STRING) {
        const char* chars = nullptr;
        Text(b, chars, n);
        bytes = reinterpret_cast<const uint8_t*>(chars);
    }
    const size_t need = 1 + s.pendingLength + 4 + 4 + n;
    if (s.used + need > ENTRY_BYTES) return Fault(s, "the message is over 1024 bytes");
    uint8_t* p = s.entries + s.used;
    *p++ = s.pendingLength;
    std::memcpy(p, s.pendingName, s.pendingLength);
    p += s.pendingLength;
    const uint32_t length = static_cast<uint32_t>(n);
    std::memcpy(p, &b.type, 4);
    std::memcpy(p + 4, &length, 4);
    if (n) std::memcpy(p + 8, bytes, n);
    s.used = static_cast<uint16_t>(s.used + need);
}

// The next slot round the ring, emptied, with the handle it will carry once filled.
Slot& Claim(uint32_t& handle)
{
    const uint32_t n = g_next.fetch_add(1, std::memory_order_relaxed);
    Slot& s = g_slots[n % SLOTS];
    handle = ((n / SLOTS) % 0xffffffu + 1) << 8 | (n % SLOTS);
    s.handle.store(0, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    s.used = 0;
    s.typeLength = s.pendingLength = s.faultLength = 0;
    s.pending = false;
    return s;
}

uint32_t Begin(const char* fault)
{
    uint32_t h = 0;
    Slot& s = Claim(h);
    Fault(s, fault);
    s.handle.store(h, std::memory_order_release);
    return h;
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

uint32_t Step(Item a, Item b)
{
    const char* chars = nullptr;
    size_t n = 0;
    if (Text(a, chars, n)) {
        uint32_t h = 0;
        Slot& s = Claim(h);
        if (n > NAME_BYTES) Fault(s, "Msg was given a type name over 64 bytes");
        else {
            std::memcpy(s.type, chars, n);
            s.typeLength = static_cast<uint8_t>(n);
        }
        Name(s, b);
        s.handle.store(h, std::memory_order_release);
        return h;
    }
    // Every later step gets the handle the step before it returned.
    if (CodeOf(a.type) == CODE_UDINT && FlagsOf(a.type) == 0 && a.data && a.length == 4) {
        uint32_t h = 0;
        std::memcpy(&h, a.data, 4);
        Slot& s = g_slots[h & 0xffu];
        if (h == 0 || s.handle.load(std::memory_order_acquire) != h) return Begin("Msg was given a message of an earlier cycle");
        if (s.pending) Value(s, b);
        else Name(s, b);
        return h;
    }
    return Begin("Msg starts with the type name, '' for the target's own");
}

bool Read(uint32_t handle, Message& out)
{
    if (handle == 0) return false;
    const Slot& s = g_slots[handle & 0xffu];
    if (s.handle.load(std::memory_order_acquire) != handle) return false;
    out.type.assign(s.type, std::min<size_t>(s.typeLength, NAME_BYTES));
    out.fault.assign(s.fault, std::min<size_t>(s.faultLength, FAULT_BYTES));
    if (out.fault.empty() && s.pending) {
        out.fault.assign("the field ");
        out.fault.append(s.pendingName, std::min<size_t>(s.pendingLength, NAME_BYTES));
        out.fault.append(" has no value");
    }
    out.entries.assign(s.entries, s.entries + std::min<size_t>(s.used, ENTRY_BYTES));
    // A new message claiming the slot meanwhile clears the handle first.
    std::atomic_thread_fence(std::memory_order_acquire);
    return s.handle.load(std::memory_order_relaxed) == handle;
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
