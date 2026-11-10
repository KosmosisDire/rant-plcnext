#pragma once
// Bytes to and from RscVariant in each variable's schema layout (docs/design.md, the codec).

#include "plc/model.h"
#include "plc/plc.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace plc {

// PLC to mesh. buf stays empty for an unsupported variant, a reader failure or a struct whose
// field count no longer matches the browse.
void SerializeVar(std::vector<uint8_t>& buf, const RscVar& v, const GdsVar& gv);

// Pulls every element of an array or struct variant off the RSC stream, for that alone.
void DrainVariant(const RscVar& v);

// Mesh to PLC: one (GDS name, value) per leaf, empty when the bytes do not match the layout.
using WriteList = std::vector<std::pair<std::string, RscVar>>;
void BuildWriteItems(const GdsVar& gv, const uint8_t* data, size_t len, WriteList& out);

} // namespace plc
