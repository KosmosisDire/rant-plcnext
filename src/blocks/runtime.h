#pragma once
// The runtime behind the blocks: one node on a thread of its own, and an entity per block. Every
// call here comes from a PLC task through iec/Runtime.cs and returns without waiting on the node.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace rant { class Node; }

namespace blocks {

enum class Kind : int {
    Publisher = 1, Subscriber = 2, FunctionDefinition = 3, RemoteFunction = 4,
    VariableDefinition = 5, RemoteVariable = 6, TaskDefinition = 7, RemoteTask = 8
};

// The node's name and domain, reopening it when either changes. Returns the peers it sees.
int Configure(const std::string& name, int domain);

// The entity of kind named name: handle while both still match, else a new one, 0 on failure.
int Entity(int kind, int handle, const std::string& name);

// The layout of an entity's value or request (which 0), response (1) or progress (2), walked in
// iec/Layout.cs.
bool NeedsLayout(int handle, int which, uintptr_t type);
int  Field(int handle, int which, int parent, const char* name, const char* space, const char* type,
           int offset, int size, int code, int flags);
void Bound(int handle, int which, int node, const char* name, int value);
void LayoutDone(int handle, int which, uintptr_t type);

// Publisher: queues the value when it changed or send is set. Returns the values sent.
int Publish(int handle, uint8_t* data, int length, bool send);
// Subscriber: copies a value that arrived since the last call. Returns the values received
// times two, plus one when this call copied a value.
int Take(int handle, uint8_t* data, int length);
// VariableDefinition and RemoteVariable: sends the value when the program changed it, else
// copies one that came from the mesh. Returns the values received times two, plus one when this
// call copied a value.
int Share(int handle, uint8_t* data, int length);
// FunctionDefinition and TaskDefinition: ends the call in hand with response when complete is
// set, sends progress when it changed, else hands the next request over. Returns SERVE_* bits
// plus the calls ended times eight.
int Serve(int handle, uint8_t* request, int requestLength, uint8_t* progress, int progressLength,
          uint8_t* response, int responseLength, bool complete);
// TaskDefinition: fails the call in hand with reason.
void Reject(int handle, const std::string& reason);
// RemoteFunction and RemoteTask: 0 idle, 1 busy with progress written as it comes, 2 done with
// response written, 3 failed. cancel asks the provider of a running task to stop.
int Call(int handle, bool execute, bool cancel, uint8_t* request, int requestLength, uint8_t* progress,
         int progressLength, uint8_t* response, int responseLength);

constexpr int SERVE_NEW = 1, SERVE_HELD = 2, SERVE_CANCEL = 4;

// Why the entity cannot work, empty when it can.
std::string Failure(int handle);

// The mirror's share of the node: slots topics, and while it browses the node waits for it a
// while. The node once it holds them, with epoch moving on every open, else null.
std::shared_ptr<rant::Node> MirrorNode(size_t slots, bool browsing, uint64_t& epoch);

// A thread made from a PLC task inherits its real time priority and its core: this drops both.
void Demote();

// Closes the node and joins its thread, at library unload.
void Shutdown();

} // namespace blocks
