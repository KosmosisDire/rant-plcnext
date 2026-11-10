#include "plc/codec.h"

#include "mirror/config.h"
#include "core/log.h"

#include "Arp/Base/Rsc/Commons/RscArrayReader.hpp"
#include "Arp/Base/Rsc/Commons/RscStructReader.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace plc {

namespace {

using Arp::Base::Rsc::Commons::RscArrayReader;
using Arp::Base::Rsc::Commons::RscStructReader;

// PLC to mesh

template <typename T>
void PushLE(std::vector<uint8_t>& buf, T val)
{
    const auto* p = reinterpret_cast<const uint8_t*>(&val);
    buf.insert(buf.end(), p, p + sizeof(T));
}

// One scalar variant's raw little-endian bytes. False for non-scalar types.
bool AppendScalar(std::vector<uint8_t>& buf, const RscVar& v)
{
    try {
        switch (v.GetType()) {
            case RscType::Bool:   { bool x; v.CopyTo(x); buf.push_back(x ? 1 : 0); return true; }
            case RscType::Int8:   { Arp::int8   x; v.CopyTo(x); PushLE(buf, x); return true; }
            case RscType::Uint8:  { Arp::uint8  x; v.CopyTo(x); PushLE(buf, x); return true; }
            case RscType::Int16:  { Arp::int16  x; v.CopyTo(x); PushLE(buf, x); return true; }
            case RscType::Uint16: { Arp::uint16 x; v.CopyTo(x); PushLE(buf, x); return true; }
            case RscType::Int32:  { Arp::int32  x; v.CopyTo(x); PushLE(buf, x); return true; }
            case RscType::Uint32: { Arp::uint32 x; v.CopyTo(x); PushLE(buf, x); return true; }
            case RscType::Int64:  { Arp::int64  x; v.CopyTo(x); PushLE(buf, x); return true; }
            case RscType::Uint64: { Arp::uint64 x; v.CopyTo(x); PushLE(buf, x); return true; }
            case RscType::Real32: { float  x; v.CopyTo(x); PushLE(buf, x); return true; }
            case RscType::Real64: { double x; v.CopyTo(x); PushLE(buf, x); return true; }
            case RscType::Char:   { char x; v.CopyTo(x); buf.push_back(static_cast<uint8_t>(x)); return true; }
            // CopyTo refuses the IEC time types, so they are read raw: TIME is ms, the rest ns.
            case RscType::IecTime: {
                int32_t ms; std::memcpy(&ms, v.GetValueAddress<int32_t>(), sizeof ms);
                PushLE(buf, static_cast<int64_t>(ms) * 1000); return true;
            }
            case RscType::IecTime64: case RscType::IecTimeOfDay64:
            case RscType::IecDate64: case RscType::IecDateTime64: {
                int64_t ns; std::memcpy(&ns, v.GetValueAddress<int64_t>(), sizeof ns);
                PushLE(buf, ns / 1000); return true;
            }
            default: return false;
        }
    } catch (...) { return false; }
}

// Every scalar element of an array, row-major (recursing for multi-dim).
void PackArray(std::vector<uint8_t>& buf, RscArrayReader& reader)
{
    const size_t n = reader.GetSize();
    const bool multiDim = reader.GetDimensions() > 1;
    RscVar elem;   // hoisted: every RscVariant<512> construction zero-fills its buffer
    for (size_t i = 0; i < n; ++i) {
        if (multiDim) {
            RscArrayReader sub = reader.ReadNextArray();
            PackArray(buf, sub);
        } else {
            reader.ReadNext(elem);
            AppendScalar(buf, elem);
        }
    }
}

void DrainArrayElements(RscArrayReader& reader)
{
    const size_t n = reader.GetSize();
    RscVar elem;
    for (size_t i = 0; i < n; ++i) {
        if (reader.GetDimensions() > 1) {
            RscArrayReader sub = reader.ReadNextArray();
            DrainArrayElements(sub);
        } else if (reader.GetElementType() == RscType::Struct) {
            RscStructReader sub = reader.ReadNextStruct<RSC_BUF_SIZE>();
            const size_t fields = sub.GetFieldCount();
            for (size_t f = 0; f < fields; ++f) {
                sub.ReadNextField(elem);
                DrainVariant(elem);
            }
        } else {
            reader.ReadNext(elem);
        }
    }
}

// A string member's capped slot: [u16 live-length][cap bytes], always 2+cap bytes
// so the member keeps its static offset. Longer values are truncated.
void AppendCappedString(std::vector<uint8_t>& buf, const RscVar& v, uint16_t cap, bool& truncated)
{
    const char* s = nullptr;
    try { s = v.GetChars(); } catch (...) {}
    size_t actual = s ? std::strlen(s) : 0;
    size_t emit   = actual > cap ? cap : actual;
    if (emit < actual) truncated = true;

    PushLE(buf, static_cast<uint16_t>(emit));
    if (emit) buf.insert(buf.end(), s, s + emit);
    buf.insert(buf.end(), cap - emit, 0);
}

// Walks the member tree and the RSC struct reader in step. A field count or width mismatch
// rejects the whole value.
bool PackStruct(std::vector<uint8_t>& buf, RscStructReader& reader,
                const std::vector<Member>& fields, bool& truncated)
{
    size_t count = 0;
    try { count = reader.GetFieldCount(); } catch (...) { return false; }
    if (count != fields.size()) return false;

    RscVar field;
    for (const auto& m : fields) {
        try { reader.ReadNextField(field); } catch (...) { return false; }

        const size_t before = buf.size();
        switch (m.form) {
            case Member::Form::Skipped:
                DrainVariant(field);
                break;
            case Member::Form::Scalar:
                if (!AppendScalar(buf, field) || buf.size() - before != m.size) return false;
                break;
            case Member::Form::String:
                AppendCappedString(buf, field, m.strCap, truncated);
                break;
            case Member::Form::Array:
                try { RscArrayReader ar(field); PackArray(buf, ar); }
                catch (...) { return false; }
                if (buf.size() - before != m.size) return false;
                break;
            case Member::Form::Struct:
                try { RscStructReader sub(field); if (!PackStruct(buf, sub, m.fields, truncated)) return false; }
                catch (...) { return false; }
                break;
        }
    }
    return true;
}

// Mesh to PLC

template <typename T>
bool MakeFromLE(RscVar& out, const uint8_t* d, size_t len)
{
    if (len < sizeof(T)) return false;
    T v; std::memcpy(&v, d, sizeof(T));
    out = RscVar(v);
    return true;
}

// Microseconds back into the PLC's own time type: TIME in ms, the rest in ns.
bool MakeIecTime(RscVar& out, Scalar kind, const uint8_t* d, size_t len)
{
    int64_t us;
    if (len < sizeof us) return false;
    std::memcpy(&us, d, sizeof us);
    if (kind == Scalar::Time) {
        const int32_t ms = static_cast<int32_t>(std::clamp<int64_t>(us / 1000, INT32_MIN, INT32_MAX));
        out = RscVar(RscType::IecTime);
        std::memcpy(out.GetDataAddress(), &ms, sizeof ms);
        return true;
    }
    const RscType type = kind == Scalar::LTime ? RscType::IecTime64
                       : kind == Scalar::LTod  ? RscType::IecTimeOfDay64
                       : kind == Scalar::LDate ? RscType::IecDate64 : RscType::IecDateTime64;
    const int64_t ns = std::clamp<int64_t>(us, INT64_MIN / 1000, INT64_MAX / 1000) * 1000;
    out = RscVar(type);
    std::memcpy(out.GetDataAddress(), &ns, sizeof ns);
    return true;
}

// Decode one scalar (or top-level [u32 length][bytes] string) into a variant.
bool MakeScalarVariant(RscVar& out, Scalar kind, const uint8_t* d, size_t len)
{
    switch (kind) {
        case Scalar::Bool: if (len < 1) return false; out = RscVar(bool(d[0] != 0)); return true;
        case Scalar::I8:   return MakeFromLE<Arp::int8>(out, d, len);
        case Scalar::U8:   return MakeFromLE<Arp::uint8>(out, d, len);
        case Scalar::I16:  return MakeFromLE<Arp::int16>(out, d, len);
        case Scalar::U16:  return MakeFromLE<Arp::uint16>(out, d, len);
        case Scalar::I32:  return MakeFromLE<Arp::int32>(out, d, len);
        case Scalar::U32:  return MakeFromLE<Arp::uint32>(out, d, len);
        case Scalar::I64:  return MakeFromLE<Arp::int64>(out, d, len);
        case Scalar::U64:  return MakeFromLE<Arp::uint64>(out, d, len);
        case Scalar::F32:  return MakeFromLE<float>(out, d, len);
        case Scalar::F64:  return MakeFromLE<double>(out, d, len);
        case Scalar::Time: case Scalar::LTime: case Scalar::LTod:
        case Scalar::LDate: case Scalar::LDt: return MakeIecTime(out, kind, d, len);
        case Scalar::String: {
            if (len < 4) return false;
            uint32_t slen; std::memcpy(&slen, d, 4);
            if (len < 4u + slen) return false;
            std::string s(reinterpret_cast<const char*>(d + 4), slen);
            out = RscVar(RscStr(s.c_str()));
            return true;
        }
        default: return false;
    }
}

// One (name, variant) per array element, from `count` packed elements at `at`.
void CollectArrayWrites(const std::string& base, const std::vector<ArrDim>& dims,
                        Scalar kind, uint32_t count, const uint8_t* at, WriteList& out)
{
    const size_t esz = ScalarByteSize(kind);
    if (esz == 0) return;
    NameList names;
    ArrayElementNames(base, dims, names);
    if (names.size() != count) return;
    for (size_t i = 0; i < names.size(); ++i) {
        RscVar v;
        if (MakeScalarVariant(v, kind, at + i * esz, esz)) out.emplace_back(std::move(names[i]), std::move(v));
    }
}

// One write item per leaf, each from its own offset, so a bad member drops alone.
void CollectStructWrites(const std::vector<Member>& fields, const uint8_t* data, size_t len, WriteList& out)
{
    for (const auto& m : fields) {
        if (m.form == Member::Form::Skipped) continue;
        if (m.offset + m.size > len) continue;              // truncated message
        const uint8_t* at = data + m.offset;

        switch (m.form) {
            case Member::Form::Struct:
                CollectStructWrites(m.fields, data, len, out);
                break;
            case Member::Form::Scalar: {
                RscVar v;
                if (MakeScalarVariant(v, m.kind, at, m.size)) out.emplace_back(m.gdsName, std::move(v));
                break;
            }
            case Member::Form::String: {
                uint16_t slen; std::memcpy(&slen, at, 2);
                if (slen > m.strCap) break;                 // malformed slot
                std::string s(reinterpret_cast<const char*>(at + 2), slen);
                out.emplace_back(m.gdsName, RscVar(RscStr(s.c_str())));
                break;
            }
            case Member::Form::Array:
                CollectArrayWrites(m.gdsName, m.dims, m.kind, m.count, at, out);
                break;
            case Member::Form::Skipped:
                break;
        }
    }
}

} // namespace

void DrainVariant(const RscVar& v)
{
    try {
        if (v.GetType() == RscType::Array) {
            RscArrayReader r(v);
            DrainArrayElements(r);
        } else if (v.GetType() == RscType::Struct) {
            RscStructReader r(v);
            const size_t fields = r.GetFieldCount();
            RscVar field;
            for (size_t f = 0; f < fields; ++f) {
                r.ReadNextField(field);
                DrainVariant(field);
            }
        }
    } catch (...) {}
}

void SerializeVar(std::vector<uint8_t>& buf, const RscVar& v, const GdsVar& gv)
{
    buf.clear();
    if (gv.isStruct) {
        bool truncated = false;
        try {
            RscStructReader reader(v);
            if (!PackStruct(buf, reader, gv.fields, truncated)) buf.clear();
        } catch (...) { buf.clear(); }
        if (truncated && !gv.strWarned) {
            gv.strWarned = true;
            log::Error(gv.topic + " has a string member longer than "
                       + std::to_string(g_cfg.structStringCap)
                       + " bytes; truncated (raise the Mirror block's StringCap)");
        }
        return;
    }
    if (gv.isArray) {
        try { RscArrayReader reader(v); PackArray(buf, reader); }
        catch (...) { buf.clear(); }
        return;
    }
    if (gv.kind == Scalar::String) {
        const char* s = nullptr;
        try { s = v.GetChars(); } catch (...) {}
        size_t len = s ? std::strlen(s) : 0;
        PushLE(buf, static_cast<uint32_t>(len));
        if (s && len) buf.insert(buf.end(), s, s + len);
        return;
    }
    AppendScalar(buf, v);
}

void BuildWriteItems(const GdsVar& gv, const uint8_t* data, size_t len, WriteList& out)
{
    if (gv.isStruct) {
        CollectStructWrites(gv.fields, data, len, out);
    } else if (gv.isArray) {
        if (gv.count * ScalarByteSize(gv.kind) != len) return;   // bytes don't match elem[N]
        CollectArrayWrites(gv.name, gv.dims, gv.kind, gv.count, data, out);
    } else {
        RscVar v;
        if (MakeScalarVariant(v, gv.kind, data, len)) out.emplace_back(gv.name, std::move(v));
    }
}

} // namespace plc
