#pragma once
// Walks Arp.Plc.Eclr and builds one GdsVar per published variable (docs/design.md).

#include "plc/model.h"

#include <vector>

namespace plc {

// Empty if the browse service is unavailable or nothing publishable was found.
std::vector<GdsVar> BrowseVariables();

} // namespace plc
