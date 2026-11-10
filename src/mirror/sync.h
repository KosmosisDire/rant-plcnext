#pragma once
// Keeps the PLC and the mesh agreeing on each variable's value and forced state, on the mirror
// thread only (docs/design.md, keeping the two sides agreeing).

#include "plc/codec.h"
#include "plc/model.h"
#include "plc/plc.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace plc {

// GDS name -> index in the variable table. A struct also maps each member's
// force name, because a PLC-side force lands on a leaf, never on the struct.
using NameIndex = std::unordered_map<std::string, int>;
NameIndex BuildNameIndex(const std::vector<GdsVar>& vars);

// Read a variable's current value via ReadSingle and serialize it. False (buf
// cleared) on failure or a size that disagrees with the fixed schema.
bool ReadLiveValue(const GdsVar& gv, std::vector<uint8_t>& buf);

// Apply one sample with task timestamp ts (0 = unknown): publish only a real change, never an
// older one. A declined sample is still drained.
void PublishSample(GdsVar& gv, int64_t ts, const RscVar& value);

// Reflect PLC-side force state into the RANT variables.
void PollForces(std::vector<GdsVar>& vars, const NameIndex& names);

// A write or force from another node, which the variable's on_write handler queued.
void OnRemoteWrite(GdsVar& gv, const std::vector<uint8_t>& value, bool forced);

// One batched GDS write. The first item the PLC refused, with the reason, or empty.
std::string WritePlc(const WriteList& items);

} // namespace plc
