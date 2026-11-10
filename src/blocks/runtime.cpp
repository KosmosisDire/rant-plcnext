#include "blocks/runtime.h"

#include "blocks/layout.h"
#include "core/log.h"

#include "rant.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <sys/eventfd.h>
#include <unistd.h>

namespace blocks {

namespace log = plc::log;

namespace {

using Clock = std::chrono::steady_clock;
using Bytes = rant::Bytes;

// Blocks of the first cycles may come before the Node block that names the node, so the node
// opens this long after the first block ran.
// A handle holds its entity's index in 16 bits.
constexpr size_t MAX_ENTITIES = 0xffff;

constexpr auto OPEN_GRACE = std::chrono::milliseconds(500);
constexpr auto RETRY      = std::chrono::seconds(2);
constexpr auto IDLE_WAKE  = std::chrono::milliseconds(50);
// The node waits this long for a browsing mirror, so it opens once with room for its variables.
constexpr auto MIRROR_WAIT = std::chrono::seconds(20);

// The parts of an entity's value, as iec/Layout.cs numbers them.
constexpr int VALUE = 0, RESPONSE = 1, PROGRESS = 2;

// The longest reason a call fails with, and the longest log line or condition text: an IEC STRING's capacity.
constexpr size_t REASON_MAX = 80, TEXT_LINE = 80;
// What a call or a write made while the node is down fails with, since it would land late.
constexpr char DOWN_CALL[] = "the node is not open, the call was not sent";
constexpr char DOWN_WRITE[] = "the node is not open, the write was not applied";
// Log lines the tasks may queue before the node thread sends them, past it a line is dropped.
constexpr size_t LOG_LINES = 64;
// Keys of conditions other nodes cleared, waiting for the node thread.
constexpr size_t CLEARED_MAX = 64;

// A node's topic slots: what the entities and the mirror need, and what an open grants. A task takes three.
constexpr size_t SLOTS_BASE = 16, SLOTS_PER_ENTITY = 3;
constexpr size_t GRANT_BASE = 32, GRANT_PER_ENTITY = 4, SLOTS_MAX = 65535;

// Past the cap a reopen would gain nothing, so what is needed stops there and the extra topics fail.
size_t SlotsNeeded(size_t entities, size_t mirror)
{
    return std::min(SLOTS_BASE + SLOTS_PER_ENTITY * entities + mirror, SLOTS_MAX);
}
size_t SlotsGranted(size_t entities, size_t mirror)
{
    return std::min(GRANT_BASE + GRANT_PER_ENTITY * entities + mirror, SLOTS_MAX);
}

// What every entity has. Each family of blocks adds its own state and handle below.
struct Record {
    const Kind  kind;
    std::string name;
    std::atomic<bool> removed{false};   // retired by its block, the node thread frees it

    // The task's side: only the block's own task touches these.
    Layout               parts[3];
    std::vector<uint8_t> scratch;          // a value as the wire spells it
    std::vector<uint8_t> last;             // the last value sent, or a variable's last known
    bool                 hasLast = false;
    int                  shown = 0;        // the last answer to the block, given again while mu is busy

    // Shared with the node thread, under mu and held only for a copy. The task only tries mu.
    std::mutex           mu;
    std::string          schema[3];        // empty until that part's layout is done
    uint64_t             layoutGen = 0;     // moves whenever a schema changes
    std::vector<uint8_t> out;              // to the mesh: a value, a response or a request
    std::vector<uint8_t> in;               // from the mesh: a value, a request or a response
    bool                 fresh = false;    // in holds a value the task has not taken
    uint32_t             count = 0;        // wraps, as the block's Count does
    std::string          error;            // the handle: Build and refused sends, cleared by their success
    std::string          fault;            // the data: a value that does not fit, a call's failure
    // The node thread's copy of what it sends, so a swap with out keeps both capacities.
    std::vector<uint8_t> sendBuf;

    bool                 unlogged = false; // error holds a reason the node thread has not logged yet

    // The node thread's side.
    uint64_t builtGen = 0;
    uint64_t builtEpoch = 0;
    Clock::time_point buildAgainAt{};       // a Make that threw is tried again from then
    bool freeing = false;                   // dropped by this pass, so it is freed under s.mu

    explicit Record(Kind k) : kind(k) {}
    virtual ~Record() = default;
    virtual int Parts() const { return 1; }
    // Makes the Rant handle from the parts' schemas, value being the last one out. Throws when it cannot.
    virtual void Make(rant::Node& node, const std::string (&schema)[3], const std::vector<uint8_t>& value) = 0;
    // Moves what the task queued onto the mesh. Node thread only.
    virtual void Service() = 0;
    // Ends the calls in hand with why, then releases the Rant handle. Node thread only.
    virtual void Drop(const char* why) = 0;
};

// The entities by index, which a task reads without a lock: chunks are made once and kept, and a
// slot is set before its handle is handed out and cleared before its record is freed.
constexpr size_t CHUNK = 256, CHUNKS = (MAX_ENTITIES + CHUNK - 1) / CHUNK;

struct State {
    std::mutex                           mu;    // the entity list and the configuration
    std::vector<std::unique_ptr<Record>> entities;   // a freed slot is null and is reused
    std::atomic<std::atomic<Record*>*>   chunks[CHUNKS] = {};
    std::atomic<bool>                    touched{false};   // firstUse is set
    std::atomic<size_t>                  total{0};   // entities.size(), readable without the lock
    size_t                               active = 0; // the entities no block has retired
    std::string                          name;
    int                                  domain = 0;
    uint64_t                             configGen = 1;
    // The Node block that names the node, so a second one cannot rename it every cycle.
    int                                  nodeOwner = 0;
    int                                  nodeIds = 0;
    Clock::time_point                    ownerSeen;
    std::optional<Clock::time_point>     firstUse;
    size_t                               mirrorSlots = 0;
    bool                                 mirrorBrowsing = false;
    // The open node as the mirror sees it.
    std::shared_ptr<rant::Node>          shared;
    uint64_t                             sharedEpoch = 0;
    size_t                               sharedSlots = 0;

    std::thread             thread;
    std::atomic<bool>       running{false};
    std::atomic<bool>       stop{false};
    // Log lines from the tasks, a fixed ring the node thread sends from.
    struct LogLine {
        uint8_t level;
        uint8_t length;
        char    text[TEXT_LINE];
    };
    std::mutex              logMu;
    LogLine                 lines[LOG_LINES];
    size_t                  logHead = 0, logCount = 0;
    // Keys of conditions other nodes cleared, from Rant's event callback.
    std::mutex               clearedMu;
    std::vector<std::string> clearedKeys;
    // The task sets pending and writes the eventfd only when pending was clear, at most once a pass.
    std::atomic<bool>       pending{false};
    std::atomic<int>        wakeFd{-1};
    std::atomic<int>        peers{0};
    // A handle carries this run's generation, so one from before a restart finds nothing.
    uint32_t                generation = static_cast<uint32_t>(
        std::chrono::system_clock::now().time_since_epoch().count() & 0x7fff) | 1;
};

State& S()
{
    static State s;
    return s;
}

std::string DefaultName()
{
    char host[64] = {};
    if (gethostname(host, sizeof host - 1) != 0 || !host[0]) return "plc";
    std::string out = "plc_";
    for (const char* c = host; *c; ++c) out += (*c == '-' || *c == '.') ? '_' : *c;
    return out;
}

void Wake()
{
    State& s = S();
    if (s.pending.exchange(true)) return;
    const uint64_t one = 1;
    const int fd = s.wakeFd.load();
    if (fd >= 0) (void)!write(fd, &one, sizeof one);
}

// The slot of index, made when make is set. Under s.mu when it makes one.
std::atomic<Record*>* Slot(size_t index, bool make)
{
    std::atomic<std::atomic<Record*>*>& c = S().chunks[index / CHUNK];
    std::atomic<Record*>* chunk = c.load(std::memory_order_acquire);
    if (!chunk && make) {
        chunk = new std::atomic<Record*>[CHUNK];
        for (size_t i = 0; i < CHUNK; ++i) chunk[i].store(nullptr, std::memory_order_relaxed);
        c.store(chunk, std::memory_order_release);
    }
    return chunk ? &chunk[index % CHUNK] : nullptr;
}

Record* Find(int handle)
{
    State& s = S();
    if (handle <= 0 || static_cast<uint32_t>(handle >> 16) != s.generation) return nullptr;
    const size_t index = static_cast<size_t>(handle & 0xffff) - 1;
    std::atomic<Record*>* slot = Slot(index, false);
    Record* e = slot ? slot->load(std::memory_order_acquire) : nullptr;
    return e && !e->removed ? e : nullptr;
}

// count moved up by shift with room for the low bits, kept a positive int as the block reads it.
int Packed(uint32_t count, int shift) { return static_cast<int>((count << shift) & 0x7fffffffu); }

// The record's lock if it is free now. A task never waits on the node thread.
std::unique_lock<std::mutex> Try(Record& e) { return std::unique_lock<std::mutex>(e.mu, std::try_to_lock); }

void Fail(Record& e, const std::string& why)
{
    bool changed;
    {
        std::lock_guard<std::mutex> g(e.mu);
        changed = e.error != why;
        e.error = why;
    }
    if (changed) log::ArpError(e.name + ": " + why);
}

void ClearError(Record& e)
{
    std::lock_guard<std::mutex> g(e.mu);
    if (!e.error.empty()) e.error.clear();
}

std::string Status(const char* what, rant::SendStatus st)
{
    return std::string(what) + ", status " + std::to_string(static_cast<int>(st));
}

rant::Qos Catching()
{
    rant::Qos q;
    q.reliability = rant::Reliability::Reliable;
    q.catch_up = 1;
    return q;
}

Bytes View(const std::vector<uint8_t>& v) { return Bytes(v.data(), v.size()); }

// Publisher: sends a value the task changed, or one it asked to send.
struct PublisherRecord : Record {
    bool ready = false;    // out holds a value to send
    bool resend = false;   // the next value goes out even unchanged, since the handle is new
    bool sendAsked = false;   // the task's own: Send was set since the last value queued
    std::optional<rant::Publisher<Bytes>> pub;

    PublisherRecord() : Record(Kind::Publisher) {}
    static bool Holds(Kind k) { return k == Kind::Publisher; }

    void Make(rant::Node& node, const std::string (&schema)[3], const std::vector<uint8_t>&) override
    {
        // A block publishes on change, so a change must arrive and a late reader gets the last.
        rant::Qos q = Catching();
        q.keep_last = 1;
        pub = node.publisher<Bytes>(name, q, node.schema(schema[VALUE]));
        std::lock_guard<std::mutex> g(mu);
        resend = true;
    }
    void Service() override
    {
        {
            std::lock_guard<std::mutex> g(mu);
            if (!ready || !pub) return;
            out.swap(sendBuf);
            ready = false;
        }
        const rant::SendStatus st = pub->send(View(sendBuf));
        if (st == rant::SendStatus::Ok) {
            std::lock_guard<std::mutex> g(mu);
            ++count;
            error.clear();
        } else if (st == rant::SendStatus::NoTopic) {
            // The value found no topic, so the block sends it again.
            std::lock_guard<std::mutex> g(mu);
            resend = true;
        } else {
            Fail(*this, Status("a value was refused", st));
        }
    }
    void Drop(const char*) override { pub.reset(); }
};

// Subscriber: keeps the newest value for the task.
struct SubscriberRecord : Record {
    std::optional<rant::Subscriber<Bytes>> sub;

    SubscriberRecord() : Record(Kind::Subscriber) {}
    static bool Holds(Kind k) { return k == Kind::Subscriber; }

    void Make(rant::Node& node, const std::string (&schema)[3], const std::vector<uint8_t>&) override
    {
        sub = node.subscriber<Bytes>(name, [this](const rant::MessageView& m) {
            const Bytes b = m.data();
            std::lock_guard<std::mutex> g(mu);
            in.assign(b.data(), b.data() + b.size());
            fresh = true;
            ++count;
        }, Catching(), node.schema(schema[VALUE]));
    }
    void Service() override {}
    void Drop(const char*) override { sub.reset(); }
};

// VariableDefinition and RemoteVariable: a value both the task and the mesh may change.
struct VariableRecord : Record {
    enum class Send { Idle, Ready, Sending };
    Send send = Send::Idle;
    // A remote's set waits for the owner to echo it, so an owner value sent before it never wins.
    bool awaitEcho = false;
    bool held = false;     // in holds an owner value that came before the echo
    Clock::time_point echoDeadline;
    // The node closed after this record had a handle. Only a write made before Rant first started waits.
    bool down = false;
    std::optional<rant::VariableDefinition<Bytes>> var;
    std::optional<rant::RemoteVariable<Bytes>>     remote;

    explicit VariableRecord(Kind k) : Record(k) {}
    static bool Holds(Kind k) { return k == Kind::VariableDefinition || k == Kind::RemoteVariable; }
    bool Owner() const { return kind == Kind::VariableDefinition; }
    rant::VariableDefinition<Bytes>* Var()
    {
        if (var) return &*var;
        return remote ? &*remote : nullptr;
    }

    void Make(rant::Node& node, const std::string (&schema)[3], const std::vector<uint8_t>& value) override
    {
        const bool owner = Owner();
        rant::VariableOptions<Bytes> o;
        if (owner && !value.empty()) o.initial = View(value);
        if (owner) var = node.variable_definition<Bytes>(name, o, node.schema(schema[VALUE]));
        else remote = node.remote_variable<Bytes>(name, o, node.schema(schema[VALUE]));
        // A remote hears every owner value, since the echo of a set that changes nothing is one too.
        auto heard = [this, owner](const rant::VariableUpdate& u) {
            const Bytes b = u.value();
            std::lock_guard<std::mutex> g(mu);
            if (!owner && awaitEcho) {
                // The echo of the set ends the wait. A value before it is held for the deadline.
                const bool echo = b.size() == sendBuf.size() && std::equal(b.data(), b.data() + b.size(), sendBuf.data());
                awaitEcho = !echo;
                held = !echo;
                in.assign(b.data(), b.data() + b.size());
                fresh = false;
                return;
            }
            in.assign(b.data(), b.data() + b.size());
            // The owner's own sets come back here too, and only a write from elsewhere is news.
            fresh = !owner || u.source() != 0;
        };
        if (owner) Var()->on_change(heard);
        else Var()->on_write(heard);
        std::lock_guard<std::mutex> g(mu);
        down = false;
    }
    void Service() override
    {
        rant::VariableDefinition<Bytes>* v = Var();
        {
            std::lock_guard<std::mutex> g(mu);
            if (send != Send::Ready || !v) return;
            sendBuf = out;
            send = Send::Sending;
            // A mesh value that came before this set is older than it.
            fresh = false;
            held = false;
            if (!Owner()) {
                awaitEcho = true;
                echoDeadline = Clock::now() + std::chrono::seconds(1);
            }
        }
        const rant::SendStatus st = v->set(View(sendBuf));
        {
            std::lock_guard<std::mutex> g(mu);
            if (send == Send::Sending) send = Send::Idle;
            if (st != rant::SendStatus::Ok) awaitEcho = false;
        }
        // A write the owner cannot get is not kept for later, since it would land stale.
        if (st == rant::SendStatus::Ok) ClearError(*this);
        else if (st == rant::SendStatus::NoTopic) Fail(*this, "no owner of " + name + ", the write was not applied");
        else Fail(*this, Status("a write was refused", st));
    }
    void Drop(const char*) override
    {
        {
            std::lock_guard<std::mutex> g(mu);
            if (var || remote) down = true;
        }
        var.reset();
        remote.reset();
    }
};

// FunctionDefinition and TaskDefinition: the mesh calls, and the task answers one call at a time.
struct ServeRecord : Record {
    enum class Hold { Idle, Waiting, Answered };
    Hold hold = Hold::Idle;
    bool cancel = false;                   // a cancel asked for the task in hand
    // Why the call in hand fails, empty to answer it. Fixed, so the task sets it without allocating.
    char reason[REASON_MAX];
    size_t reasonLength = 0;
    void Because(const char* why, size_t length)
    {
        reasonLength = std::min(length, REASON_MAX);
        std::memcpy(reason, why, reasonLength);
    }
    std::vector<uint8_t> progress, sendProgress;
    bool progressFresh = false;
    // One call waits beside the one in hand and the next is refused busy, so a slow program never piles them up.
    std::optional<rant::Deferred<Bytes>> callIn;
    std::vector<uint8_t> callData;                           // callIn's request
    std::optional<rant::PendingTask<Bytes, Bytes>> taskIn;   // a task call not taken yet
    // The call the task holds. Only the node thread touches it once taken.
    rant::Deferred<Bytes>           current;
    rant::PendingTask<Bytes, Bytes> currentTask;
    std::optional<rant::FunctionDefinition<Bytes, Bytes>>    fn;
    std::optional<rant::TaskDefinition<Bytes, Bytes, Bytes>> task;

    explicit ServeRecord(Kind k) : Record(k) {}
    static bool Holds(Kind k) { return k == Kind::FunctionDefinition || k == Kind::TaskDefinition; }
    bool IsTask() const { return kind == Kind::TaskDefinition; }
    int Parts() const override { return IsTask() ? 3 : 2; }

    void Make(rant::Node& node, const std::string (&schema)[3], const std::vector<uint8_t>&) override
    {
        if (!IsTask()) {
            fn = node.function_definition<Bytes, Bytes>(name, [this](rant::Request<Bytes>& rq) {
                const Bytes b = rq.data();
                bool busy;
                {
                    std::lock_guard<std::mutex> g(mu);
                    busy = callIn.has_value();
                    if (!busy) {
                        callData.assign(b.data(), b.data() + b.size());
                        callIn = rq.defer();
                    }
                }
                if (busy) rq.fail("the function is busy");
            }, rant::FunctionOptions{}, node.schema(schema[VALUE]), node.schema(schema[RESPONSE]));
            return;
        }
        rant::TaskOptions o;
        o.exclusive = true;
        task = node.task_definition<Bytes, Bytes, Bytes>(name, [this](rant::TaskRequest<Bytes, Bytes>& rq) {
            const Bytes b = rq.data();
            bool busy;
            {
                std::lock_guard<std::mutex> g(mu);
                busy = taskIn || hold != Hold::Idle;
                if (!busy) {
                    in.assign(b.data(), b.data() + b.size());
                    taskIn = rq.defer();
                }
            }
            if (busy) rq.fail("the task is busy");
        }, o, node.schema(schema[VALUE]), node.schema(schema[PROGRESS]), node.schema(schema[RESPONSE]));
    }
    void Service() override
    {
        if (!IsTask()) {
            rant::Deferred<Bytes> call;
            std::string why;
            {
                std::lock_guard<std::mutex> g(mu);
                if (hold != Hold::Answered) return;
                call = std::move(current);
                out.swap(sendBuf);
                why.assign(reason, reasonLength);
                reasonLength = 0;
                hold = Hold::Idle;
            }
            if (!why.empty()) call.fail(why);
            else call.complete(View(sendBuf));
            return;
        }
        Hold now;
        bool sendNow = false;
        {
            std::lock_guard<std::mutex> g(mu);
            now = hold;
            if (now == Hold::Waiting && progressFresh) {
                progress.swap(sendProgress);
                progressFresh = false;
                sendNow = true;
            }
        }
        if (now == Hold::Waiting) {
            // The task never touches the call it handed over, so it is used here unlocked.
            if (sendNow && !sendProgress.empty()) currentTask.progress(View(sendProgress));
            if (currentTask.cancelled()) {
                std::lock_guard<std::mutex> g(mu);
                cancel = true;
            }
        } else if (now == Hold::Answered) {
            rant::PendingTask<Bytes, Bytes> call;
            std::string why;
            bool cancelled;
            {
                std::lock_guard<std::mutex> g(mu);
                call = std::move(currentTask);
                out.swap(sendBuf);
                why.assign(reason, reasonLength);
                reasonLength = 0;
                cancelled = cancel;
                cancel = false;
                hold = Hold::Idle;
            }
            if (!why.empty()) call.fail(why);
            else if (cancelled) call.complete_cancelled({}, View(sendBuf));
            else call.complete(View(sendBuf));
        }
    }
    void Drop(const char* why) override
    {
        rant::Deferred<Bytes> heldCall;
        rant::PendingTask<Bytes, Bytes> heldTask;
        std::optional<rant::PendingTask<Bytes, Bytes>> queuedTask;
        std::optional<rant::Deferred<Bytes>> queued;
        {
            std::lock_guard<std::mutex> g(mu);
            heldCall = std::move(current);
            heldTask = std::move(currentTask);
            queuedTask = std::move(taskIn);
            taskIn.reset();
            queued = std::move(callIn);
            callIn.reset();
            hold = Hold::Idle;
            cancel = false;
            reasonLength = 0;
        }
        heldCall.fail(why);
        heldTask.fail(why);
        if (queuedTask) queuedTask->fail(why);
        if (queued) queued->fail(why);
        fn.reset();
        task.reset();
    }
};

// RemoteFunction and RemoteTask: the task calls once on a rising Execute and reads the answer.
struct CallRecord : Record {
    enum class Step { Idle, Ready, Busy, Done, Failed };
    Step step = Step::Idle;
    bool lastExecute = false;   // the task's own, for the rising edge
    bool cancel = false;        // a cancel asked for the call in flight
    bool cancelSent = false;
    std::vector<uint8_t> progress;
    bool progressFresh = false;
    uint32_t callId = 0;        // a task call in flight
    // The node closed after this record had a handle. Only a call made before Rant first started waits.
    bool down = false;
    std::optional<rant::RemoteFunction<Bytes, Bytes>>        fn;
    std::optional<rant::RemoteTask<Bytes, Bytes, Bytes>>     task;

    explicit CallRecord(Kind k) : Record(k) {}
    static bool Holds(Kind k) { return k == Kind::RemoteFunction || k == Kind::RemoteTask; }
    bool IsTask() const { return kind == Kind::RemoteTask; }
    int Parts() const override { return IsTask() ? 3 : 2; }

    void Make(rant::Node& node, const std::string (&schema)[3], const std::vector<uint8_t>&) override
    {
        if (IsTask())
            task = node.remote_task<Bytes, Bytes, Bytes>(name, rant::TaskOptions{}, node.schema(schema[VALUE]),
                                                         node.schema(schema[PROGRESS]), node.schema(schema[RESPONSE]));
        else
            fn = node.remote_function<Bytes, Bytes>(name, rant::FunctionOptions{}, node.schema(schema[VALUE]),
                                                    node.schema(schema[RESPONSE]));
        std::lock_guard<std::mutex> g(mu);
        down = false;
    }
    // The response handler of a call.
    void Answered(const rant::ResponseView<Bytes>& r)
    {
        const Bytes b = r.data();
        std::string message;
        if (!r.ok()) message = std::string(r.message());
        std::lock_guard<std::mutex> g(mu);
        // A call a reopen already ended has no one waiting for its answer.
        if (step != Step::Busy) return;
        if (r.ok()) {
            in.assign(b.data(), b.data() + b.size());
            step = Step::Done;
        } else {
            fault = std::move(message);
            step = Step::Failed;
        }
    }
    void Service() override
    {
        bool cancelNow = false;
        {
            std::lock_guard<std::mutex> g(mu);
            if (step == Step::Busy && cancel && !cancelSent) {
                cancelNow = cancelSent = true;
            } else {
                if (step != Step::Ready || !(fn || task)) return;
                out.swap(sendBuf);
                step = Step::Busy;
            }
        }
        if (cancelNow) {
            if (task) task->cancel(callId);
            return;
        }
        rant::SendStatus st;
        auto done = [this](const rant::ResponseView<Bytes>& r) { Answered(r); };
        if (fn) {
            st = fn->call_async(View(sendBuf), done);
        } else {
            const rant::TaskCall call = task->call_async(View(sendBuf), [this](const rant::ProgressView<Bytes>& p) {
                if (!p.has_value()) return;
                const Bytes b = p.data();
                std::lock_guard<std::mutex> g(mu);
                progress.assign(b.data(), b.data() + b.size());
                progressFresh = true;
            }, done);
            callId = call.id;
            st = call.status;
        }
        if (st != rant::SendStatus::Ok) {
            std::string why = Status("the call was refused", st);
            std::lock_guard<std::mutex> g(mu);
            fault = std::move(why);
            step = Step::Failed;
        }
    }
    void Drop(const char* why) override
    {
        {
            std::lock_guard<std::mutex> g(mu);
            const bool had = fn || task;
            // A call queued for this handle would go out on the next node, late, so it fails as one in flight does.
            if (step == Step::Busy || (step == Step::Ready && had)) {
                fault = why;
                step = Step::Failed;
            }
            if (had) down = true;
        }
        fn.reset();
        task.reset();
    }
};

// Status: one condition of the node, raised while the task holds it active.
struct StatusRecord : Record {
    // The task's wish, under mu.
    bool   active = false;
    bool   clearable = false;
    int    level = 3;
    char   text[TEXT_LINE];
    size_t textLength = 0;
    bool   changed = false;
    bool   clearedByPeer = false;  // another node cleared it, so it stays down until active goes false
    bool   clearedNews = false;    // the task has not been told of that clear yet
    // The node thread's.
    rant::Node* node = nullptr;
    bool        raised = false;

    StatusRecord() : Record(Kind::Status) {}
    static bool Holds(Kind k) { return k == Kind::Status; }
    int Parts() const override { return 0; }

    void Make(rant::Node& n, const std::string (&)[3], const std::vector<uint8_t>&) override
    {
        node = &n;
        // A new node holds none of the conditions the old one did.
        std::lock_guard<std::mutex> g(mu);
        changed = true;
    }
    void Service() override
    {
        bool want, canClear;
        int at;
        char t[TEXT_LINE];
        size_t length;
        {
            std::lock_guard<std::mutex> g(mu);
            if (!changed || !node) return;
            changed = false;
            want = active && !clearedByPeer;
            canClear = clearable;
            at = level;
            length = textLength;
            std::memcpy(t, text, length);
        }
        rant::SendStatus st = rant::SendStatus::Ok;
        if (want) {
            st = node->set_status(name, static_cast<rant::Level>(at), std::string_view(t, length), canClear);
            raised = st == rant::SendStatus::Ok;
        } else if (raised) {
            node->clear_status(name);
            raised = false;
        }
        if (st == rant::SendStatus::Ok) ClearError(*this);
        else Fail(*this, Status("the condition was refused", st));
    }
    void Drop(const char*) override
    {
        if (node && raised) node->clear_status(name);
        raised = false;
        node = nullptr;
    }
    // Another node cleared the condition. Node thread only.
    void ClearedElsewhere()
    {
        raised = false;
        std::lock_guard<std::mutex> g(mu);
        if (!active) return;
        clearedByPeer = true;
        clearedNews = true;
    }
};

std::unique_ptr<Record> NewRecord(Kind k)
{
    switch (k) {
        case Kind::Publisher:          return std::make_unique<PublisherRecord>();
        case Kind::Subscriber:         return std::make_unique<SubscriberRecord>();
        case Kind::VariableDefinition:
        case Kind::RemoteVariable:     return std::make_unique<VariableRecord>(k);
        case Kind::FunctionDefinition:
        case Kind::TaskDefinition:     return std::make_unique<ServeRecord>(k);
        case Kind::RemoteFunction:
        case Kind::RemoteTask:         return std::make_unique<CallRecord>(k);
        case Kind::Status:             return std::make_unique<StatusRecord>();
    }
    return nullptr;
}

// The record as its family, or null when it is another.
template <class T> T* As(Record* e) { return e && T::Holds(e->kind) ? static_cast<T*>(e) : nullptr; }

// Makes the entity's Rant handle on node, or records why it cannot. False when Make threw, to try again.
bool Build(rant::Node& node, Record& e)
{
    std::string schema[3];
    std::vector<uint8_t> value;
    {
        std::lock_guard<std::mutex> g(e.mu);
        for (int i = 0; i < 3; ++i) schema[i] = e.schema[i];
        e.builtGen = e.layoutGen;
        value = e.out;
    }
    e.Drop("the block's type changed");
    // A part not walked yet builds once its layout moves layoutGen, so there is nothing to retry.
    for (int i = 0; i < e.Parts(); ++i)
        if (schema[i].empty()) return true;
    try {
        e.Make(node, schema, value);
        ClearError(e);
        return true;
    } catch (const std::exception& ex) {
        e.Drop("the block's type changed");
        Fail(e, ex.what());
        return false;
    }
}

// Closes the node even while the mirror still holds it, whose handles then answer NoTopic.
void Close(std::shared_ptr<rant::Node>& node)
{
    if (!node) return;
    State& s = S();
    {
        std::lock_guard<std::mutex> g(s.mu);
        s.shared.reset();
    }
    log::Detach();
    node->close();
    node.reset();
}

void Run()
{
    pthread_setname_np(pthread_self(), "rant_blocks");
    Demote();
    State& s = S();
    std::shared_ptr<rant::Node> node;
    uint64_t openGen = 0, epoch = 0;
    size_t slots = 0;
    Clock::time_point retryAt{};
    // This thread is no real time one, but the task's calls wait on s.mu, so nothing allocates under it.
    std::string name;
    name.reserve(128);
    std::vector<Record*> list, gone;
    Clock::time_point peersAt{};
    while (!s.stop) {
        // A wake from here on runs another pass, since this one may have read its record already.
        s.pending.store(false);
        int domain = 0;
        uint64_t configGen = 0;
        std::optional<Clock::time_point> firstUse;
        size_t mirrorSlots;
        bool mirrorBrowsing;
        for (;;) {
            list.reserve(s.total.load() + 16);
            std::lock_guard<std::mutex> g(s.mu);
            if (s.entities.size() > list.capacity()) continue;
            name.assign(s.name);
            domain = s.domain;
            configGen = s.configGen;
            firstUse = s.firstUse;
            mirrorSlots = s.mirrorSlots;
            mirrorBrowsing = s.mirrorBrowsing;
            list.clear();
            for (auto& e : s.entities)
                if (e) list.push_back(e.get());
            break;
        }
        if (name.empty()) name = DefaultName();
        // A block that gave an entity up leaves it here, so it neither counts nor stays.
        gone.clear();
        for (Record* e : list)
            if (e->removed) {
                e->Drop("the block was removed");
                e->freeing = true;
                gone.push_back(e);
            }
        if (!gone.empty()) {
            list.erase(std::remove_if(list.begin(), list.end(), [](Record* e) { return e->removed.load(); }), list.end());
            std::lock_guard<std::mutex> g(s.mu);
            for (size_t i = 0; i < s.entities.size(); ++i)
                if (s.entities[i] && s.entities[i]->freeing) {
                    Slot(i, false)->store(nullptr, std::memory_order_release);
                    s.entities[i].reset();
                }
        }
        const auto now = Clock::now();
        if (node && SlotsNeeded(list.size(), mirrorSlots) > slots) openGen = 0;
        const bool waitMirror = mirrorBrowsing && firstUse && now - *firstUse < MIRROR_WAIT;
        if ((!node || openGen != configGen) && firstUse && now - *firstUse >= OPEN_GRACE && now >= retryAt
            && !waitMirror) {
            for (Record* e : list) e->Drop("the node reopened");
            Close(node);
            try {
                rant::NodeOptions o;
                o.domain = static_cast<uint16_t>(domain);
                slots = SlotsGranted(list.size(), mirrorSlots);
                o.max_topics = static_cast<uint16_t>(slots);
                node = std::make_shared<rant::Node>(name, o);
                log::Attach(node);
                node->on_event([](const rant::Event& ev) {
                    if (ev.kind() != rant::EventKind::StatusCleared) return;
                    State& st = S();
                    std::lock_guard<std::mutex> g(st.clearedMu);
                    if (st.clearedKeys.size() < CLEARED_MAX) st.clearedKeys.push_back(ev.detail());
                });
                log::ArpInfo("node " + name + " open in domain " + std::to_string(domain));
                openGen = configGen;
                ++epoch;
                std::lock_guard<std::mutex> g(s.mu);
                s.shared = node;
                s.sharedEpoch = epoch;
                s.sharedSlots = slots;
            } catch (const std::exception& ex) {
                node.reset();
                log::ArpError("node " + name + " failed to open: " + ex.what());
                retryAt = now + RETRY;
            }
        }
        if (node) {
            for (Record* e : list) {
                bool stale;
                {
                    std::lock_guard<std::mutex> g(e->mu);
                    stale = e->builtGen != e->layoutGen;
                }
                if (stale || (e->builtEpoch != epoch && now >= e->buildAgainAt)) {
                    if (Build(*node, *e)) e->builtEpoch = epoch;
                    else e->buildAgainAt = now + RETRY;
                }
                std::string unlogged;
                {
                    std::lock_guard<std::mutex> g(e->mu);
                    if (e->unlogged) unlogged = e->error;
                    e->unlogged = false;
                }
                if (!unlogged.empty()) log::ArpError(e->name + ": " + unlogged);
                e->Service();
            }
            // The tasks' log lines, sent here since a Rant call may wait.
            for (;;) {
                State::LogLine line;
                {
                    std::lock_guard<std::mutex> g(s.logMu);
                    if (!s.logCount) break;
                    line = s.lines[s.logHead];
                    s.logHead = (s.logHead + 1) % LOG_LINES;
                    --s.logCount;
                }
                node->log(static_cast<rant::Level>(line.level), std::string_view(line.text, line.length));
            }
            {
                std::vector<std::string> cleared;
                {
                    std::lock_guard<std::mutex> g(s.clearedMu);
                    cleared.swap(s.clearedKeys);
                }
                for (const std::string& key : cleared)
                    for (Record* e : list)
                        if (StatusRecord* c = As<StatusRecord>(e); c && c->name == key) c->ClearedElsewhere();
            }
            if (now >= peersAt) {
                int active = 0;
                for (const rant::Peer& p : node->reflection().peers()) active += p.active;
                s.peers = active;
                peersAt = now + std::chrono::seconds(1);
            }
        }
        if (!s.pending.load()) {
            pollfd p{ s.wakeFd.load(), POLLIN, 0 };
            poll(&p, 1, static_cast<int>(IDLE_WAKE.count()));
        }
        uint64_t drained;
        (void)!read(s.wakeFd.load(), &drained, sizeof drained);
    }
    for (auto& e : s.entities)
        if (e) e->Drop("the node closed");
    Close(node);
}

void Touch()
{
    State& s = S();
    if (!s.touched.exchange(true)) {
        std::lock_guard<std::mutex> g(s.mu);
        if (!s.firstUse) s.firstUse = Clock::now();
    }
    bool expected = false;
    if (s.running.compare_exchange_strong(expected, true)) {
        s.stop = false;
        s.wakeFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        s.thread = std::thread(Run);
    }
}

Layout* Part(Record* e, int which)
{
    if (!e || which < 0 || which >= e->Parts()) return nullptr;
    return &e->parts[which];
}

bool Fits(const Layout& l, int length)
{
    return l.done && l.error.empty() && length == static_cast<int>(l.nodes[0].size);
}

// The value as the wire spells it, from the pin's bytes. False when the pin is not the type walked.
bool Encode(Record& e, int which, const uint8_t* data, int length)
{
    const Layout& l = e.parts[which];
    if (!data || !Fits(l, length)) return false;
    blocks::Encode(l, data, e.scratch);
    return true;
}

} // namespace

void Demote()
{
    sched_param p{};
    pthread_setschedparam(pthread_self(), SCHED_OTHER, &p);
    cpu_set_t all;
    CPU_ZERO(&all);
    const long n = sysconf(_SC_NPROCESSORS_CONF);
    for (long i = 0; i < n; ++i) CPU_SET(i, &all);
    pthread_setaffinity_np(pthread_self(), sizeof all, &all);
}

int Configure(int& owner, const char* name, size_t length, int domain, bool wasRefused)
{
    State& s = S();
    bool refused = false;
    {
        std::unique_lock<std::mutex> g(s.mu, std::try_to_lock);
        if (!g.owns_lock()) {
            Touch();
            return wasRefused ? -1 : s.peers.load();
        }
        if (owner == 0) owner = ++s.nodeIds;
        const Clock::time_point now = Clock::now();
        const bool same = s.domain == domain && s.name.size() == length && std::memcmp(s.name.data(), name, length) == 0;
        // An owner that stopped running for a second hands the node to the next Node block.
        if (s.nodeOwner != owner && s.nodeOwner != 0 && now - s.ownerSeen < std::chrono::seconds(1)) {
            refused = !same;
        } else {
            s.nodeOwner = owner;
            s.ownerSeen = now;
            if (!same) {
                s.name.assign(name, length);
                s.domain = domain;
                ++s.configGen;
            }
        }
    }
    Touch();
    return refused ? -1 : s.peers.load();
}

int Entity(int kind, int handle, const char* name, size_t length)
{
    if (kind < static_cast<int>(Kind::Publisher) || kind > static_cast<int>(Kind::Status) || length == 0) return 0;
    if (Record* e = Find(handle))
        if (static_cast<int>(e->kind) == kind && e->name.size() == length && std::memcmp(e->name.data(), name, length) == 0)
            return handle;
    State& s = S();
    {
        std::lock_guard<std::mutex> g(s.mu);
        if (handle > 0 && static_cast<uint32_t>(handle >> 16) == s.generation) {
            const size_t old = static_cast<size_t>(handle & 0xffff) - 1;
            if (old < s.entities.size() && s.entities[old] && !s.entities[old]->removed) {
                s.entities[old]->removed = true;
                --s.active;
            }
        }
        size_t slot = 0;
        while (slot < s.entities.size() && s.entities[slot]) ++slot;
        if (slot >= MAX_ENTITIES) return 0;
        std::unique_ptr<Record> e = NewRecord(static_cast<Kind>(kind));
        e->name.assign(name, length);
        Slot(slot, true)->store(e.get(), std::memory_order_release);
        if (slot == s.entities.size()) {
            s.entities.push_back(std::move(e));
            s.total = s.entities.size();
        } else {
            s.entities[slot] = std::move(e);
        }
        ++s.active;
        handle = static_cast<int>((s.generation << 16) | (slot + 1));
    }
    Touch();
    return handle;
}

bool NeedsLayout(int handle, int which, uintptr_t type)
{
    Layout* l = Part(Find(handle), which);
    return l && (!l->done || l->type != type);
}

int Field(int handle, int which, int parent, const char* name, const char* space, const char* type,
          int offset, int size, int code, int flags)
{
    Layout* l = Part(Find(handle), which);
    if (!l) return -1;
    if (parent < 0) Restart(*l, 0);
    return AddNode(*l, parent, name, space, type, static_cast<uint32_t>(offset), static_cast<uint32_t>(size), code, flags);
}

void Bound(int handle, int which, int node, const char* name, int value)
{
    if (Layout* l = Part(Find(handle), which)) AddBound(*l, node, name, value);
}

void LayoutDone(int handle, int which, uintptr_t type, const uint8_t* sample)
{
    Record* e = Find(handle);
    Layout* l = Part(e, which);
    if (!l) return;
    l->type = type;
    Finish(*l, sample);
    std::unique_lock<std::mutex> g = Try(*e);
    if (!g.owns_lock()) {
        // The block walks the type again next cycle.
        l->done = false;
        return;
    }
    if (!l->error.empty()) {
        // Once per type: the copy allocates, and the node thread logs it.
        if (e->error != l->error) {
            e->error = l->error;
            e->unlogged = true;
            Wake();
        }
        return;
    }
    e->schema[which] = l->schema;
    ++e->layoutGen;
    e->hasLast = false;
    // Room for the first values, so the cycle does not allocate for them.
    const size_t room = l->root.form == plc::Member::Form::String ? 4 + l->root.strCap : l->root.size;
    e->scratch.reserve(room);
    e->last.reserve(room);
    e->out.reserve(room);
    e->sendBuf.reserve(room);
    e->fault.reserve(REASON_MAX);
    Wake();
}

int Publish(int handle, uint8_t* data, int length, bool send)
{
    PublisherRecord* e = As<PublisherRecord>(Find(handle));
    if (!e || !Encode(*e, VALUE, data, length)) return 0;
    // A Send of one cycle the lock missed still goes out on the next.
    e->sendAsked = e->sendAsked || send;
    const bool changed = !e->hasLast || e->scratch != e->last;
    std::unique_lock<std::mutex> g = Try(*e);
    if (!g.owns_lock()) return e->shown;
    if (e->sendAsked || changed || e->resend) {
        e->last = e->scratch;
        e->hasLast = true;
        e->out = e->scratch;
        e->ready = true;
        e->resend = false;
        e->sendAsked = false;
        Wake();
    }
    return e->shown = Packed(e->count, 0);
}

int Take(int handle, uint8_t* data, int length)
{
    SubscriberRecord* e = As<SubscriberRecord>(Find(handle));
    if (!e) return 0;
    const Layout& l = e->parts[VALUE];
    std::unique_lock<std::mutex> g = Try(*e);
    if (!g.owns_lock()) return e->shown & ~1;
    int copied = 0;
    if (e->fresh && Fits(l, length)) {
        e->fresh = false;
        if (Decode(l, e->in.data(), e->in.size(), data)) {
            copied = 1;
            e->fault.clear();
        } else {
            e->fault = "a value arrived that does not fit Value";
        }
    }
    return e->shown = Packed(e->count, 1) + copied;
}

int Share(int handle, uint8_t* data, int length)
{
    using Send = VariableRecord::Send;
    VariableRecord* e = As<VariableRecord>(Find(handle));
    if (!e || !Encode(*e, VALUE, data, length)) return 0;
    const Layout& l = e->parts[VALUE];
    std::unique_lock<std::mutex> g = Try(*e);
    if (!g.owns_lock()) return e->shown & ~1;
    // A remote starts from the owner's value, so its own first value is no change.
    const bool baseline = !e->hasLast && !e->Owner();
    if (!e->hasLast || e->scratch != e->last) {
        e->last = e->scratch;
        e->hasLast = true;
        if (!baseline && e->down) {
            e->fault.assign(DOWN_WRITE);
        } else if (!baseline) {
            e->out = e->scratch;
            e->send = Send::Ready;
            Wake();
        }
    }
    // An echo that never came (the owner may skip a value) lets the held value through.
    if (e->awaitEcho && Clock::now() >= e->echoDeadline) {
        e->awaitEcho = false;
        e->fresh = e->held;
        e->held = false;
    }
    // While the program's change is on its way, the mesh's value waits, since the change follows it.
    if (!e->fresh || e->send != Send::Idle) return e->shown = Packed(e->count, 1);
    e->fresh = false;
    if (e->in.empty()) return e->shown = Packed(e->count, 1);
    if (!Decode(l, e->in.data(), e->in.size(), data)) {
        e->fault = "a value arrived that does not fit Value";
        return e->shown = Packed(e->count, 1);
    }
    e->fault.clear();
    blocks::Encode(l, data, e->scratch);
    if (e->scratch == e->last) return e->shown = Packed(e->count, 1);
    e->last = e->scratch;
    e->out = e->scratch;
    ++e->count;
    return e->shown = Packed(e->count, 1) + 1;
}

int Serve(int handle, uint8_t* request, int requestLength, uint8_t* progress, int progressLength,
          uint8_t* response, int responseLength, bool complete)
{
    using Hold = ServeRecord::Hold;
    ServeRecord* e = As<ServeRecord>(Find(handle));
    if (!e) return 0;
    const bool task = e->IsTask();
    const Layout& req = e->parts[VALUE];
    if (!Fits(req, requestLength)) return 0;
    // Complete is held until the call ends, so a cycle the lock missed answers on the next.
    std::unique_lock<std::mutex> g = Try(*e);
    if (!g.owns_lock()) return e->shown & ~SERVE_NEW;
    if (e->hold == Hold::Waiting) {
        if (complete) {
            if (Encode(*e, RESPONSE, response, responseLength)) {
                e->out = e->scratch;
            } else {
                static const char why[] = "the PLC's Response cannot be sent";
                e->Because(why, sizeof why - 1);
            }
            e->hold = Hold::Answered;
            ++e->count;
            Wake();
            return e->shown = Packed(e->count, 3);
        }
        if (task && Encode(*e, PROGRESS, progress, progressLength) && e->scratch != e->last) {
            e->last = e->scratch;
            e->progress = e->scratch;
            e->progressFresh = true;
            Wake();
        }
        return e->shown = Packed(e->count, 3) + SERVE_HELD + (e->cancel ? SERVE_CANCEL : 0);
    }
    if (e->hold != Hold::Idle) return e->shown = Packed(e->count, 3);
    if (task) {
        if (!e->taskIn) return e->shown = Packed(e->count, 3);
        e->currentTask = std::move(*e->taskIn);
        e->taskIn.reset();
    } else {
        if (!e->callIn) return e->shown = Packed(e->count, 3);
        e->current = std::move(*e->callIn);
        e->callIn.reset();
        // A swap keeps both buffers, so the task frees nothing.
        e->in.swap(e->callData);
    }
    e->cancel = false;
    // The node thread answers every call, since a Rant call from here could wait on its lock.
    if (!Decode(req, e->in.data(), e->in.size(), request)) {
        static const char why[] = "the request does not fit the PLC's Request";
        e->Because(why, sizeof why - 1);
        e->hold = Hold::Answered;
        Wake();
        return e->shown = Packed(e->count, 3);
    }
    // Progress goes out once the program changes it, not the last call's.
    if (task && Encode(*e, PROGRESS, progress, progressLength)) e->last = e->scratch;
    e->progressFresh = false;
    e->hold = Hold::Waiting;
    return e->shown = Packed(e->count, 3) + SERVE_NEW + SERVE_HELD;
}

void Reject(int handle, const char* reason, size_t length)
{
    ServeRecord* e = As<ServeRecord>(Find(handle));
    if (!e || !e->IsTask()) return;
    // Fail is held while the task runs, so a cycle the lock missed rejects on the next.
    std::unique_lock<std::mutex> g = Try(*e);
    if (!g.owns_lock() || e->hold != ServeRecord::Hold::Waiting) return;
    static const char failed[] = "the PLC failed the task";
    if (length) e->Because(reason, length);
    else e->Because(failed, sizeof failed - 1);
    e->hold = ServeRecord::Hold::Answered;
    ++e->count;
    Wake();
}

int Call(int handle, bool execute, bool cancel, uint8_t* request, int requestLength, uint8_t* progress,
         int progressLength, uint8_t* response, int responseLength)
{
    using Step = CallRecord::Step;
    CallRecord* e = As<CallRecord>(Find(handle));
    if (!e) return 0;
    std::unique_lock<std::mutex> g = Try(*e);
    // The edge of Execute stays unseen until a cycle holds the lock.
    if (!g.owns_lock()) return e->shown;
    const bool rising = execute && !e->lastExecute;
    e->lastExecute = execute;
    const Layout& rsp = e->parts[RESPONSE];
    if (rising && Encode(*e, VALUE, request, requestLength)
        && (e->step == Step::Idle || e->step == Step::Done || e->step == Step::Failed)) {
        if (e->down) {
            e->fault.assign(DOWN_CALL);
            e->step = Step::Failed;
            return e->shown = 3;
        }
        e->out = e->scratch;
        e->fault.clear();
        e->in.clear();
        e->cancel = e->cancelSent = false;
        e->progressFresh = false;
        e->step = Step::Ready;
        Wake();
        return e->shown = 1;
    }
    switch (e->step) {
        case Step::Ready:
        case Step::Busy:
            if (cancel && !e->cancel) {
                e->cancel = true;
                Wake();
            }
            if (e->progressFresh && Fits(e->parts[PROGRESS], progressLength)) {
                e->progressFresh = false;
                Decode(e->parts[PROGRESS], e->progress.data(), e->progress.size(), progress);
            }
            return e->shown = 1;
        case Step::Done:
            if (!e->in.empty()) {
                const bool fits = Fits(rsp, responseLength) && Decode(rsp, e->in.data(), e->in.size(), response);
                e->in.clear();
                if (!fits) {
                    e->fault = "the response does not fit the PLC's Response";
                    e->step = Step::Failed;
                    return e->shown = 3;
                }
                ++e->count;
            }
            if (!execute) e->step = Step::Idle;
            return e->shown = 2;
        case Step::Failed:
            if (!execute) e->step = Step::Idle;
            return e->shown = 3;
        default: return e->shown = 0;
    }
}

bool Condition(int handle, bool active, int level, bool clearable, const char* text, size_t length)
{
    StatusRecord* e = As<StatusRecord>(Find(handle));
    if (!e) return false;
    length = std::min(length, TEXT_LINE);
    if (level < 1 || level > 4) level = 3;
    std::unique_lock<std::mutex> g = Try(*e);
    // A clear's news keeps for the next cycle.
    if (!g.owns_lock()) return false;
    if (!active) e->clearedByPeer = false;
    if (e->active != active || e->clearable != clearable || e->level != level || e->textLength != length
        || std::memcmp(e->text, text, length) != 0) {
        e->active = active;
        e->clearable = clearable;
        e->level = level;
        e->textLength = length;
        std::memcpy(e->text, text, length);
        e->changed = true;
        Wake();
    }
    const bool news = e->clearedNews;
    e->clearedNews = false;
    return news;
}

int Log(int level, const char* text, size_t length)
{
    State& s = S();
    {
        std::unique_lock<std::mutex> g(s.logMu, std::try_to_lock);
        if (!g.owns_lock()) return 0;
        if (s.logCount == LOG_LINES) return -1;
        State::LogLine& line = s.lines[(s.logHead + s.logCount) % LOG_LINES];
        line.level = static_cast<uint8_t>(level < 1 || level > 4 ? 2 : level);
        line.length = static_cast<uint8_t>(std::min(length, TEXT_LINE));
        std::memcpy(line.text, text, line.length);
        ++s.logCount;
    }
    // A program with only Log blocks still needs the node.
    Touch();
    Wake();
    return 1;
}

std::shared_ptr<rant::Node> MirrorNode(size_t slots, bool browsing, uint64_t& epoch)
{
    State& s = S();
    std::shared_ptr<rant::Node> out;
    bool moved;
    {
        std::lock_guard<std::mutex> g(s.mu);
        moved = s.mirrorSlots != slots || s.mirrorBrowsing != browsing;
        s.mirrorSlots = slots;
        s.mirrorBrowsing = browsing;
        if (s.shared && SlotsNeeded(s.active, slots) <= s.sharedSlots) {
            out = s.shared;
            epoch = s.sharedEpoch;
        }
    }
    Touch();
    if (moved) Wake();
    return out;
}

size_t Failure(int handle, char* out, size_t capacity)
{
    Record* e = Find(handle);
    const char* text = nullptr;
    size_t length = 0;
    std::unique_lock<std::mutex> g;
    if (!e) {
        text = handle ? "the block lost its entity, it opens a new one"
                      : "no entity: the name is empty or there are too many blocks";
        length = std::strlen(text);
    } else {
        g = Try(*e);
        if (!g.owns_lock()) return FAILURE_UNCHANGED;
        const std::string& why = e->error.empty() ? e->fault : e->error;
        text = why.data();
        length = why.size();
    }
    length = std::min(length, capacity);
    if (length) std::memcpy(out, text, length);
    return length;
}

void Shutdown()
{
    State& s = S();
    if (!s.running) return;
    s.stop = true;
    // The thread must hear this one, even with a wake already pending.
    s.pending = false;
    Wake();
    if (s.thread.joinable()) s.thread.join();
    close(s.wakeFd.exchange(-1));
    std::lock_guard<std::mutex> g(s.mu);
    // No task runs at unload, so the slots go with their records.
    for (auto& c : s.chunks) delete[] c.exchange(nullptr);
    s.entities.clear();
    s.total = 0;
    s.active = 0;
    s.firstUse.reset();
    s.touched = false;
    s.name.clear();
    s.mirrorSlots = 0;
    s.mirrorBrowsing = false;
    s.generation = (s.generation + 1) & 0x7fff;
    if (!s.generation) s.generation = 1;
    s.running = false;
}

} // namespace blocks
