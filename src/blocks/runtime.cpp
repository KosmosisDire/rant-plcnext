#include "blocks/runtime.h"

#include "blocks/layout.h"
#include "core/log.h"

#include "rant.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include <pthread.h>
#include <sched.h>
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

// A node's topic slots: what the entities and the mirror need, and what an open grants. A task takes three.
constexpr size_t SLOTS_BASE = 16, SLOTS_PER_ENTITY = 3;
constexpr size_t GRANT_BASE = 32, GRANT_PER_ENTITY = 4, SLOTS_MAX = 65535;

size_t SlotsNeeded(size_t entities, size_t mirror) { return SLOTS_BASE + SLOTS_PER_ENTITY * entities + mirror; }
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
    int                  count = 0;
    std::string          error;            // the handle: Build and refused sends, cleared by their success
    std::string          fault;            // the data: a value that does not fit, a call's failure
    // The node thread's copy of what it sends, so a swap with out keeps both capacities.
    std::vector<uint8_t> sendBuf;

    // The node thread's side.
    uint64_t builtGen = 0;
    uint64_t builtEpoch = 0;

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
    std::condition_variable wake;
    std::mutex              wakeMu;
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

void Wake() { S().wake.notify_one(); }

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
        Var()->on_change([this, owner](const rant::VariableUpdate& u) {
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
        });
    }
    void Service() override
    {
        rant::VariableDefinition<Bytes>* v = Var();
        {
            std::lock_guard<std::mutex> g(mu);
            if (send != Send::Ready || !v) return;
            sendBuf = out;
            send = Send::Sending;
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
        if (st == rant::SendStatus::Ok) ClearError(*this);
        else if (st != rant::SendStatus::NoTopic) Fail(*this, Status("a write was refused", st));
    }
    void Drop(const char*) override
    {
        var.reset();
        remote.reset();
    }
};

// FunctionDefinition and TaskDefinition: the mesh calls, and the task answers one call at a time.
struct ServeRecord : Record {
    enum class Hold { Idle, Waiting, Answered };
    Hold hold = Hold::Idle;
    bool cancel = false;                   // a cancel asked for the task in hand
    std::string reason;                    // why the call in hand fails, empty to answer it
    std::vector<uint8_t> progress, sendProgress;
    bool progressFresh = false;
    std::deque<std::pair<rant::Deferred<Bytes>, std::vector<uint8_t>>> calls;
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
                std::vector<uint8_t> request(b.data(), b.data() + b.size());
                rant::Deferred<Bytes> later = rq.defer();
                std::lock_guard<std::mutex> g(mu);
                calls.emplace_back(std::move(later), std::move(request));
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
                why.swap(reason);
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
                why.swap(reason);
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
        std::deque<std::pair<rant::Deferred<Bytes>, std::vector<uint8_t>>> queued;
        {
            std::lock_guard<std::mutex> g(mu);
            heldCall = std::move(current);
            heldTask = std::move(currentTask);
            queuedTask = std::move(taskIn);
            taskIn.reset();
            queued.swap(calls);
            hold = Hold::Idle;
            cancel = false;
            reason.clear();
        }
        heldCall.fail(why);
        heldTask.fail(why);
        if (queuedTask) queuedTask->fail(why);
        for (auto& q : queued) q.first.fail(why);
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
            if (step == Step::Busy) {
                fault = why;
                step = Step::Failed;
            }
        }
        fn.reset();
        task.reset();
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
    }
    return nullptr;
}

// The record as its family, or null when it is another.
template <class T> T* As(Record* e) { return e && T::Holds(e->kind) ? static_cast<T*>(e) : nullptr; }

// Makes the entity's Rant handle on node, or records why it cannot.
void Build(rant::Node& node, Record& e)
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
    for (int i = 0; i < e.Parts(); ++i)
        if (schema[i].empty()) return;
    try {
        e.Make(node, schema, value);
        ClearError(e);
    } catch (const std::exception& ex) {
        e.Drop("the block's type changed");
        Fail(e, ex.what());
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
    while (!s.stop) {
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
                gone.push_back(e);
            }
        if (!gone.empty()) {
            list.erase(std::remove_if(list.begin(), list.end(), [](Record* e) { return e->removed.load(); }), list.end());
            std::lock_guard<std::mutex> g(s.mu);
            for (size_t i = 0; i < s.entities.size(); ++i)
                if (s.entities[i] && std::find(gone.begin(), gone.end(), s.entities[i].get()) != gone.end()) {
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
                log::Attach(*node);
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
                if (stale || e->builtEpoch != epoch) {
                    e->builtEpoch = epoch;
                    Build(*node, *e);
                }
                e->Service();
            }
            int active = 0;
            for (const rant::Peer& p : node->reflection().peers()) active += p.active;
            s.peers = active;
        }
        std::unique_lock<std::mutex> lk(s.wakeMu);
        s.wake.wait_for(lk, IDLE_WAKE);
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
    if (kind < static_cast<int>(Kind::Publisher) || kind > static_cast<int>(Kind::RemoteTask) || length == 0) return 0;
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
    if (!l->error.empty()) {
        Fail(*e, l->error);
        return;
    }
    std::unique_lock<std::mutex> g = Try(*e);
    if (!g.owns_lock()) {
        // The block walks the type again next cycle.
        l->done = false;
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
    return e->shown = e->count;
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
    return e->shown = e->count * 2 + copied;
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
        if (!baseline) {
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
    if (!e->fresh || e->send != Send::Idle) return e->shown = e->count * 2;
    e->fresh = false;
    if (e->in.empty()) return e->shown = e->count * 2;
    if (!Decode(l, e->in.data(), e->in.size(), data)) {
        e->fault = "a value arrived that does not fit Value";
        return e->shown = e->count * 2;
    }
    e->fault.clear();
    blocks::Encode(l, data, e->scratch);
    if (e->scratch == e->last) return e->shown = e->count * 2;
    e->last = e->scratch;
    e->out = e->scratch;
    ++e->count;
    return e->shown = e->count * 2 + 1;
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
        if (complete && Encode(*e, RESPONSE, response, responseLength)) {
            e->out = e->scratch;
            e->hold = Hold::Answered;
            ++e->count;
            Wake();
            return e->shown = e->count * 8;
        }
        if (task && Encode(*e, PROGRESS, progress, progressLength) && e->scratch != e->last) {
            e->last = e->scratch;
            e->progress = e->scratch;
            e->progressFresh = true;
            Wake();
        }
        return e->shown = e->count * 8 + SERVE_HELD + (e->cancel ? SERVE_CANCEL : 0);
    }
    if (e->hold != Hold::Idle) return e->shown = e->count * 8;
    if (task) {
        if (!e->taskIn) return e->shown = e->count * 8;
        e->currentTask = std::move(*e->taskIn);
        e->taskIn.reset();
    } else {
        if (e->calls.empty()) return e->shown = e->count * 8;
        e->current = std::move(e->calls.front().first);
        e->in = std::move(e->calls.front().second);
        e->calls.pop_front();
    }
    e->cancel = false;
    // The node thread answers every call, since a Rant call from here could wait on its lock.
    if (!Decode(req, e->in.data(), e->in.size(), request)) {
        e->reason = "the request does not fit the PLC's Request";
        e->hold = Hold::Answered;
        Wake();
        return e->shown = e->count * 8;
    }
    // Progress goes out once the program changes it, not the last call's.
    if (task && Encode(*e, PROGRESS, progress, progressLength)) e->last = e->scratch;
    e->progressFresh = false;
    e->hold = Hold::Waiting;
    return e->shown = e->count * 8 + SERVE_NEW + SERVE_HELD;
}

void Reject(int handle, const std::string& reason)
{
    ServeRecord* e = As<ServeRecord>(Find(handle));
    if (!e || !e->IsTask()) return;
    // Fail is held while the task runs, so a cycle the lock missed rejects on the next.
    std::unique_lock<std::mutex> g = Try(*e);
    if (!g.owns_lock() || e->hold != ServeRecord::Hold::Waiting) return;
    e->reason = reason.empty() ? "the PLC failed the task" : reason;
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
    Wake();
    if (s.thread.joinable()) s.thread.join();
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
