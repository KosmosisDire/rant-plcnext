#pragma once
// One GDS variable and how it maps onto a Rant variable this node owns (docs/design.md, types).

#include "rant.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace plc {

// One RANT type per PLC scalar type. Enum order matches SCALAR_TYPES.
enum class Scalar : int {
    Bool = 0, I8, U8, I16, U16, I32, U32, I64, U64, F32, F64,
    Time, LTime, LTod, LDate, LDt,   // TIME, LTIME, LTOD, LDATE, LDT: microseconds on the mesh
    String,   // published as an unbounded RANT string, not a fixed scalar
    None      // unsupported / composite
};
constexpr int NUM_FIXED_SCALARS = 16;   // Bool .. LDt

// DSL spelling of each fixed scalar, indexed by Scalar. Doubles as the bare
// schema text of a scalar variable and as a member's type inside a struct.
inline constexpr const char* SCALAR_TYPES[NUM_FIXED_SCALARS] = {
    "bool", "i8", "u8", "i16", "u16", "i32", "u32", "i64", "u64", "f32", "f64",
    "Duration", "Duration", "Duration", "Timestamp", "Timestamp",
};

// Byte width of one fixed scalar on the wire, 0 for String and None.
inline size_t ScalarByteSize(Scalar k)
{
    switch (k) {
        case Scalar::Bool: case Scalar::I8:  case Scalar::U8:  return 1;
        case Scalar::I16:  case Scalar::U16:                   return 2;
        case Scalar::I32:  case Scalar::U32: case Scalar::F32: return 4;
        case Scalar::I64:  case Scalar::U64: case Scalar::F64: return 8;
        case Scalar::Time: case Scalar::LTime: case Scalar::LTod:
        case Scalar::LDate: case Scalar::LDt:                  return 8;
        default:                                               return 0;
    }
}

// One array dimension, kept so a remote array write can be decomposed back into
// per-element PLC names: (offset,count) reproduces "MyArr[offset .. offset+count-1]".
struct ArrDim { int64_t offset; uint32_t count; };

// One option of an enum: its name and value.
struct EnumOption { std::string name; int64_t value; };

// One member of a published struct, in declaration order. Skipped members are absent from the
// schema but still drained.
struct Member {
    enum class Form : uint8_t { Scalar, String, Array, Struct, Skipped };
    Form                form   = Form::Skipped;
    std::string         dslName;         // identifier in the schema text
    std::string         gdsName;         // full GDS path, for write-back / force
    Scalar              kind   = Scalar::None;   // Scalar: its type. Array: element type.
    uint16_t            strCap = 0;      // String: capped capacity
    uint32_t            count  = 0;      // Array: total element count
    std::vector<ArrDim> dims;            // Array: dimensions
    std::vector<Member> fields;          // Struct: its own members
    std::string         typeName;        // Struct, standard Array, enum Scalar: the PLC type as a DSL name, "" if unusable
    std::vector<EnumOption> options;     // Scalar named by typeName: an enum of kind with these options
    bool                standard = false;   // Struct, Array: a Rant standard type, named but never defined
    uint32_t            offset = 0;     // absolute byte offset in the RANT message
    uint32_t            size   = 0;      // fixed byte width in the message (0 if Skipped)
    uint32_t            plcOffset = 0;   // absolute byte offset in the PLC's own memory, for blocks
};

struct GdsVar {
    std::string         name;            // full GDS name, e.g. "Arp.Plc.Eclr/myVar"
    std::string         topic;           // RANT variable name under the prefix, e.g. "MainInstance/myVar"
    Scalar              kind = Scalar::None;   // scalar type, or element type when isArray
    bool                isArray  = false;
    bool                isStruct = false;
    uint32_t            count = 0;       // isArray: element count (drives elem[N])
    std::vector<ArrDim> dims;            // isArray: per-dimension offset/count
    std::vector<Member> fields;          // isStruct: members in declaration order
    std::string         typeName;       // isStruct: the PLC type as a DSL name, "" if unusable
    bool                forceable = false;      // IForceService::IsForcable
    int64_t             cycleUs = 0;         // cycle of the ESM task that samples it (tasks.h), 0 = unknown

    // Runtime state, mirror thread only
    rant::Schema                schema;           // empty only if compile failed
    rant::VariableDefinition<rant::Bytes> var;    // invalid until created
    std::vector<uint8_t>      lastValue;        // last value published / applied
    bool    hasValue  = false;
    bool    wasForced = false;           // the last remote op left this forced
    bool    inFast    = false;           // member of the fast (recording) subscription
    int64_t last_writeback_us = 0;       // our last PLC write/force: samples at or
                                         // before this are pre-write reads (flicker guard)
    int64_t last_sample_us    = 0;       // newest sample applied: an older one must
                                         // not regress the published value
    // one-shot diagnostics
    bool         sizeWarned = false;     // runtime bytes != schema size
    bool         fastWarned = false;     // fast subscription rejected this variable
    mutable bool strWarned  = false;     // a struct string member overflowed its cap
                                         // (mutable: detected while serialising a const var)
};

using NameList = std::vector<std::string>;

// Element GDS names of an array in row-major order (dim 0 outermost):
// base over {(1,3),(0,2)} -> base[1][0], base[1][1], base[2][0], ...
void ArrayElementNames(const std::string& base, const std::vector<ArrDim>& dims, NameList& out);

// Every PLC leaf name behind a member list, in message order: one per scalar /
// string member, one per array ELEMENT, recursing into nested structs.
void MemberLeafNames(const std::vector<Member>& fields, NameList& out);

// The names a PLC-side force on a struct member can carry once its "[index]"
// suffix is stripped: one per scalar / string member and one per ARRAY member.
void MemberForceNames(const std::vector<Member>& fields, NameList& out);

// A PLC member name as a legal DSL identifier. Rant spells fields snake_case, so motorSpeed and
// motor_speed collide: a spelling already in used gets a number.
std::string SanitizeDslName(const std::string& raw, std::vector<std::string>& used);

// Stamps each member's absolute message offset in declaration order, with no padding on the
// wire. Returns the width of the list. Skipped members advance nothing.
uint32_t AssignMemberOffsets(std::vector<Member>& fields, uint32_t base);

// The schema text of a value: a bare type word, `elem[N]`, `string` or the struct DSL. A struct
// and each nested one are named by their PLC type, or by a hash of their shape when hashName is
// set, and a standard type by its own name.
std::string SchemaText(const Member& root, bool hashName = false);
std::string SchemaText(const GdsVar& gv, bool hashName = false);

// Every PLC leaf name behind one RANT variable: the set a remote write targets.
void ElementNames(const GdsVar& gv, NameList& out);

// RSC values one sample pulls off the stream, the unit the budget counts (docs/design.md).
size_t SampleWeight(const GdsVar& gv);

} // namespace plc
