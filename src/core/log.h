#pragma once
// Two sinks: Info and Error to the node's @rant/log topic, queued until the node opens, and
// ArpInfo and ArpError to Arp.log for start up and teardown.

#include "rant.hpp"

#include <string>

namespace plc::log {

void Info(const std::string& message);
void Error(const std::string& message);

void ArpInfo(const std::string& message);
void ArpError(const std::string& message);

// Route Info/Error to `node` and flush anything queued so far.
void Attach(rant::Node& node);
// Stop routing (the node is about to be destroyed).
void Detach();
// Drop queued messages that no longer have a node to go to.
void ClearPending();

} // namespace plc::log
