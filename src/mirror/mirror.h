#pragma once
// The mirror: every PLC variable as a Rant variable on the library's node, served by a thread of
// its own (docs/design.md, the mirror thread).

#include "mirror/config.h"

#include <cstddef>
#include <string>

namespace plc {

// The Mirror block: starts the mirror with cfg, and starts it over when cfg changes. Returns the
// variables it publishes. Never waits.
int Mirror(const Config& cfg);

// Copies why the mirror publishes nothing into out, cut at capacity. Returns its length, 0 while it
// works, blocks::FAILURE_UNCHANGED while the mirror thread holds the reason.
size_t MirrorFailure(char* out, size_t capacity);

// Stops the mirror and joins its thread, at library unload, before the node closes.
void StopMirror();

} // namespace plc
