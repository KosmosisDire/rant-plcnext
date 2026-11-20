#pragma once
// The runtime behind the blocks: one node on a thread of its own, and an entity per block.
// Every call comes from a PLC task through iec/Runtime.cs and never waits: when the node thread
// holds what a call needs, the call changes nothing and the block tries again next cycle.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace rant { class Node; }

namespace blocks {

// The same values as Kind in iec/Runtime.cs.
enum class Kind : int {
    Publisher = 1, Subscriber = 2, FunctionDefinition = 3, RemoteFunction = 4,
    VariableDefinition = 5, VariableReader = 6, TaskDefinition = 7, RemoteTask = 8, Status = 9,
    VariableWriter = 10
};

// The node's name and domain, reopening it when either changes. Returns 0, or -1 when another Node
// block names it otherwise, as it did last call while it cannot tell (refused).
// owner starts at 0 and a call sets it.
int Configure(int& owner, const char* name, size_t length, int domain, bool refused);

// The entity of kind named name: handle while both still match, else a new one, 0 on failure.
int Entity(int kind, int handle, const char* name, size_t length);

// The layout of an entity's value or request (which 0), response (1) or progress (2), walked in
// iec/Layout.cs. LayoutDone gets the value's memory, where a string's capacity is.
bool NeedsLayout(int handle, int which, uintptr_t type);
int  Field(int handle, int which, int parent, const char* name, const char* space, const char* type,
           int offset, int size, int code, int flags);
void Bound(int handle, int which, int node, const char* name, int value);
void Option(int handle, int which, int node, const char* name, int value);
void LayoutDone(int handle, int which, uintptr_t type, const uint8_t* sample);
// An optional pin the program left unconnected: its part takes any value and drops it.
void Unwired(int handle, int which);

// Publisher and VariableWriter: queues the value when it changed or send is set. When message is
// set, data is a Msg block's message, sent as the type named type. A writer's first value is no
// change. Returns the values sent.
int Publish(int handle, uint8_t* data, int length, bool send, bool message, const char* type, size_t typeLength);
// Subscriber and VariableReader: copies a value that arrived since the last call. Returns the
// values received times two, plus one when this call copied one.
int Take(int handle, uint8_t* data, int length);
// VariableDefinition: sends the value when the program changed it, else copies a write from the
// mesh. Returns the values received times two, plus one when it copied one.
int Share(int handle, uint8_t* data, int length);
// FunctionDefinition and TaskDefinition: answers the call in hand when complete is set, sends
// progress when it changed, else takes the next request. Returns SERVE_* bits plus calls ended times 8.
int Serve(int handle, uint8_t* request, int requestLength, uint8_t* progress, int progressLength,
          uint8_t* response, int responseLength, bool complete);
// TaskDefinition: fails the call in hand with reason.
void Reject(int handle, const char* reason, size_t length);
// RemoteFunction and RemoteTask: 0 idle, 1 busy with progress written as it comes, 2 done with
// response written, 3 failed. A null progress or response is a pin left unconnected. cancel asks the provider of a running task to stop. When message is
// set, request is a Msg block's message, or null for none, sent as the type named type.
int Call(int handle, bool execute, bool cancel, uint8_t* request, int requestLength, uint8_t* progress,
         int progressLength, uint8_t* response, int responseLength, bool message, const char* type,
         size_t typeLength);

// Log: queues text as a log line of the node at level (Rant's, 1 debug to 4 error). Returns 1 when
// queued, 0 while the queue is busy, -1 when the queue is full and the line was dropped.
int Log(int level, const char* text, size_t length);
// Status: the entity's condition, raised while active and cleared after. Returns true the call
// after another node cleared it, which holds it down until active goes false.
bool Condition(int handle, bool active, int level, bool clearable, const char* text, size_t length);

// The same values as Kind.Serve* in iec/Runtime.cs.
constexpr int SERVE_NEW = 1, SERVE_HELD = 2, SERVE_CANCEL = 4;

// What Failure and plc::MirrorFailure return while the reason cannot be read this cycle.
constexpr size_t FAILURE_UNCHANGED = static_cast<size_t>(-1);

// Copies why the entity cannot work into out, cut at capacity. Returns its length, 0 when it can.
size_t Failure(int handle, char* out, size_t capacity);

// The mirror's share of the node: slots topics, and while it browses the node waits a while for it.
// Returns the node once it holds them, with epoch moving on every open, else null.
std::shared_ptr<rant::Node> MirrorNode(size_t slots, bool browsing, uint64_t& epoch);

// A thread made from a PLC task inherits its real time priority and its core: this drops both.
void Demote();

// Closes the node and joins its thread, at library unload.
void Shutdown();

} // namespace blocks
