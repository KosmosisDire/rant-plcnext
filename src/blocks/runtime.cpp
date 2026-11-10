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

enum class Stage { Idle, Ready, Sending, Waiting, Answered, Busy, Done, Failed };

struct Record {
    Kind        kind = Kind::Publisher;
    std::string name;
    std::atomic<bool> removed{false};   // retired by its block, the node thread frees it

    // The task's side: only the block's own task touches these.
    Layout               parts[3];
    std::vector<uint8_t> scratch;          // a value as the wire spells it
    std::vector<uint8_t> last;             // the last value sent, or a variable's last known
    bool                 hasLast = false;
    bool                 lastExecute = false;

    // Shared with the node thread, under mu and held only for a copy.
    std::mutex           mu;
    std::string          schema[3];        // empty until that part's layout is done
    uint64_t             layoutGen = 0;     // moves whenever a schema changes
    std::vector<uint8_t> out;              // to the mesh: a value, a response or a request
    std::vector<uint8_t> in;               // from the mesh: a value, a request or a response
    bool                 fresh = false;    // in holds a value the task has not taken
    std::vector<uint8_t> progress;         // a task's progress, either way
    bool                 progressFresh = false;
    bool                 cancel = false;   // a cancel asked for the call in hand
    Stage                stage = Stage::Idle;
    int                  count = 0;
    std::string          error;            // the handle: Build and refused sends, cleared by their success
    std::string          fault;            // the data: a value that does not fit, a call's failure
    bool                 resend = false;   // a Publisher sends its value again, since its handle is new
    // A RemoteVariable's set waits for the owner to echo it, so an owner value sent before it never wins.
    bool                 awaitEcho = false;
    bool                 held = false;     // in holds an owner value that came before the echo
    std::chrono::steady_clock::time_point echoDeadline;
    std::string          reason;           // why the call in hand fails, empty to answer it
    std::deque<std::pair<rant::Deferred<Bytes>, std::vector<uint8_t>>> calls;
    std::optional<rant::PendingTask<Bytes, Bytes>> taskIn;   // a task call not taken yet
    // The node thread's copies of what it sends, so a swap with out keeps both capacities.
    std::vector<uint8_t> sendBuf, sendProgress;
    // The call the task holds. Only the node thread touches it once taken.
    rant::Deferred<Bytes>             current;
    rant::PendingTask<Bytes, Bytes>   currentTask;
    uint32_t                          callId = 0;   // RemoteTask: the call in flight
    bool                              cancelSent = false;

    // The node thread's side.
    uint64_t builtGen = 0;
    uint64_t builtEpoch = 0;
    std::optional<rant::Publisher<Bytes>>                    pub;
    std::optional<rant::Subscriber<Bytes>>                   sub;
    std::optional<rant::FunctionDefinition<Bytes, Bytes>>    fn;
    std::optional<rant::RemoteFunction<Bytes, Bytes>>        remote;
    std::optional<rant::VariableDefinition<Bytes>>           var;
    std::optional<rant::RemoteVariable<Bytes>>               remoteVar;
    std::optional<rant::TaskDefinition<Bytes, Bytes, Bytes>> task;
    std::optional<rant::RemoteTask<Bytes, Bytes, Bytes>>     remoteTask;

    // Ends the calls in hand with why, then releases the Rant handles. Node thread only.
    void Drop(const char* why)
    {
        rant::Deferred<Bytes> held;
        rant::PendingTask<Bytes, Bytes> heldTask;
        std::optional<rant::PendingTask<Bytes, Bytes>> queuedTask;
        std::deque<std::pair<rant::Deferred<Bytes>, std::vector<uint8_t>>> queued;
        {
            std::lock_guard<std::mutex> g(mu);
            held = std::move(current);
            heldTask = std::move(currentTask);
            queuedTask = std::move(taskIn);
            taskIn.reset();
            queued.swap(calls);
            if ((kind == Kind::FunctionDefinition || kind == Kind::TaskDefinition) && stage != Stage::Idle) {
                stage = Stage::Idle;
                cancel = false;
                reason.clear();
            }
            if ((kind == Kind::RemoteFunction || kind == Kind::RemoteTask) && stage == Stage::Busy) {
                fault = why;
                stage = Stage::Failed;
            }
        }
        held.fail(why);
        heldTask.fail(why);
        if (queuedTask) queuedTask->fail(why);
        for (auto& q : queued) q.first.fail(why);
        pub.reset(); sub.reset(); fn.reset(); remote.reset();
        var.reset(); remoteVar.reset(); task.reset(); remoteTask.reset();
    }
    int Parts() const
    {
        switch (kind) {
            case Kind::FunctionDefinition:
            case Kind::RemoteFunction: return 2;
            case Kind::TaskDefinition:
            case Kind::RemoteTask: return 3;
            default: return 1;
        }
    }
    rant::VariableDefinition<Bytes>* Var()
    {
        if (var) return &*var;
        return remoteVar ? &*remoteVar : nullptr;
    }
};

struct State {
    std::mutex                           mu;    // the entity list and the configuration
    std::vector<std::unique_ptr<Record>> entities;   // a freed slot is null and is reused
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

Record* Find(int handle)
{
    State& s = S();
    if (handle <= 0 || static_cast<uint32_t>(handle >> 16) != s.generation) return nullptr;
    const size_t index = static_cast<size_t>(handle & 0xffff) - 1;
    std::lock_guard<std::mutex> g(s.mu);
    if (index >= s.entities.size() || !s.entities[index] || s.entities[index]->removed) return nullptr;
    return s.entities[index].get();
}

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
        rant::Schema first = node.schema(schema[VALUE]);
        switch (e.kind) {
            case Kind::Publisher: {
                // A block publishes on change, so a change must arrive and a late reader gets the last.
                rant::Qos q = Catching();
                q.keep_last = 1;
                e.pub = node.publisher<Bytes>(e.name, q, first);
                break;
            }
            case Kind::Subscriber:
                e.sub = node.subscriber<Bytes>(e.name, [&e](const rant::MessageView& m) {
                    const Bytes b = m.data();
                    std::lock_guard<std::mutex> g(e.mu);
                    e.in.assign(b.data(), b.data() + b.size());
                    e.fresh = true;
                    ++e.count;
                }, Catching(), first);
                break;
            case Kind::FunctionDefinition:
                e.fn = node.function_definition<Bytes, Bytes>(e.name, [&e](rant::Request<Bytes>& rq) {
                    const Bytes b = rq.data();
                    std::vector<uint8_t> request(b.data(), b.data() + b.size());
                    rant::Deferred<Bytes> later = rq.defer();
                    std::lock_guard<std::mutex> g(e.mu);
                    e.calls.emplace_back(std::move(later), std::move(request));
                }, rant::FunctionOptions{}, first, node.schema(schema[RESPONSE]));
                break;
            case Kind::RemoteFunction:
                e.remote = node.remote_function<Bytes, Bytes>(e.name, rant::FunctionOptions{}, first,
                                                              node.schema(schema[RESPONSE]));
                break;
            case Kind::VariableDefinition:
            case Kind::RemoteVariable: {
                const bool owner = e.kind == Kind::VariableDefinition;
                rant::VariableOptions<Bytes> o;
                if (owner && !value.empty()) o.initial = View(value);
                if (owner) e.var = node.variable_definition<Bytes>(e.name, o, first);
                else e.remoteVar = node.remote_variable<Bytes>(e.name, o, first);
                e.Var()->on_change([&e, owner](const rant::VariableUpdate& u) {
                    const Bytes b = u.value();
                    std::lock_guard<std::mutex> g(e.mu);
                    if (!owner && e.awaitEcho) {
                        // The echo of the set ends the wait. A value before it is held for the deadline.
                        const bool echo = b.size() == e.sendBuf.size() && std::equal(b.data(), b.data() + b.size(), e.sendBuf.data());
                        e.awaitEcho = !echo;
                        e.held = !echo;
                        e.in.assign(b.data(), b.data() + b.size());
                        e.fresh = false;
                        return;
                    }
                    e.in.assign(b.data(), b.data() + b.size());
                    // The owner's own sets come back here too, and only a write from elsewhere is news.
                    e.fresh = !owner || u.source() != 0;
                });
                break;
            }
            case Kind::TaskDefinition: {
                rant::TaskOptions o;
                o.exclusive = true;
                e.task = node.task_definition<Bytes, Bytes, Bytes>(e.name, [&e](rant::TaskRequest<Bytes, Bytes>& rq) {
                    const Bytes b = rq.data();
                    bool busy;
                    {
                        std::lock_guard<std::mutex> g(e.mu);
                        busy = e.taskIn || e.stage != Stage::Idle;
                        if (!busy) {
                            e.in.assign(b.data(), b.data() + b.size());
                            e.taskIn = rq.defer();
                        }
                    }
                    if (busy) rq.fail("the task is busy");
                }, o, first, node.schema(schema[PROGRESS]), node.schema(schema[RESPONSE]));
                break;
            }
            case Kind::RemoteTask:
                e.remoteTask = node.remote_task<Bytes, Bytes, Bytes>(e.name, rant::TaskOptions{}, first,
                                                                     node.schema(schema[PROGRESS]),
                                                                     node.schema(schema[RESPONSE]));
                break;
        }
        std::lock_guard<std::mutex> g(e.mu);
        e.error.clear();
        if (e.kind == Kind::Publisher) e.resend = true;
    } catch (const std::exception& ex) {
        e.Drop("the block's type changed");
        Fail(e, ex.what());
    }
}

// The response handler of a call from a RemoteFunction or RemoteTask.
void Answered(Record& e, const rant::ResponseView<Bytes>& r)
{
    const Bytes b = r.data();
    std::string message;
    if (!r.ok()) message = std::string(r.message());
    std::lock_guard<std::mutex> g(e.mu);
    // A call a reopen already ended has no one waiting for its answer.
    if (e.stage != Stage::Busy) return;
    if (r.ok()) {
        e.in.assign(b.data(), b.data() + b.size());
        e.stage = Stage::Done;
    } else {
        e.fault = std::move(message);
        e.stage = Stage::Failed;
    }
}

// Moves what the task queued onto the mesh.
void Service(Record& e)
{
    switch (e.kind) {
        case Kind::Publisher: {
            {
                std::lock_guard<std::mutex> g(e.mu);
                if (e.stage != Stage::Ready || !e.pub) return;
                e.out.swap(e.sendBuf);
                e.stage = Stage::Idle;
            }
            const rant::SendStatus st = e.pub->send(View(e.sendBuf));
            if (st == rant::SendStatus::Ok) {
                std::lock_guard<std::mutex> g(e.mu);
                ++e.count;
                e.error.clear();
            } else if (st == rant::SendStatus::NoTopic) {
                // The value found no topic, so the block sends it again.
                std::lock_guard<std::mutex> g(e.mu);
                e.resend = true;
            } else {
                Fail(e, Status("a value was refused", st));
            }
            break;
        }
        case Kind::VariableDefinition:
        case Kind::RemoteVariable: {
            rant::VariableDefinition<Bytes>* var = e.Var();
            {
                std::lock_guard<std::mutex> g(e.mu);
                if (e.stage != Stage::Ready || !var) return;
                e.sendBuf = e.out;
                e.stage = Stage::Sending;
                if (e.kind == Kind::RemoteVariable) {
                    e.awaitEcho = true;
                    e.echoDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
                }
            }
            const rant::SendStatus st = var->set(View(e.sendBuf));
            {
                std::lock_guard<std::mutex> g(e.mu);
                if (e.stage == Stage::Sending) e.stage = Stage::Idle;
                if (st != rant::SendStatus::Ok) e.awaitEcho = false;
            }
            if (st == rant::SendStatus::Ok) ClearError(e);
            else if (st != rant::SendStatus::NoTopic) Fail(e, Status("a write was refused", st));
            break;
        }
        case Kind::FunctionDefinition: {
            rant::Deferred<Bytes> call;
            std::string reason;
            {
                std::lock_guard<std::mutex> g(e.mu);
                if (e.stage != Stage::Answered) return;
                call = std::move(e.current);
                e.out.swap(e.sendBuf);
                reason.swap(e.reason);
                e.stage = Stage::Idle;
            }
            if (!reason.empty()) call.fail(reason);
            else call.complete(View(e.sendBuf));
            break;
        }
        case Kind::TaskDefinition: {
            Stage stage;
            bool sendProgress = false;
            {
                std::lock_guard<std::mutex> g(e.mu);
                stage = e.stage;
                if (stage == Stage::Waiting && e.progressFresh) {
                    e.progress.swap(e.sendProgress);
                    e.progressFresh = false;
                    sendProgress = true;
                }
            }
            if (stage == Stage::Waiting) {
                // The task never touches the call it handed over, so it is used here unlocked.
                if (sendProgress && !e.sendProgress.empty()) e.currentTask.progress(View(e.sendProgress));
                if (e.currentTask.cancelled()) {
                    std::lock_guard<std::mutex> g(e.mu);
                    e.cancel = true;
                }
            } else if (stage == Stage::Answered) {
                rant::PendingTask<Bytes, Bytes> call;
                std::string reason;
                bool cancelled;
                {
                    std::lock_guard<std::mutex> g(e.mu);
                    call = std::move(e.currentTask);
                    e.out.swap(e.sendBuf);
                    reason.swap(e.reason);
                    cancelled = e.cancel;
                    e.cancel = false;
                    e.stage = Stage::Idle;
                }
                if (!reason.empty()) call.fail(reason);
                else if (cancelled) call.complete_cancelled({}, View(e.sendBuf));
                else call.complete(View(e.sendBuf));
            }
            break;
        }
        case Kind::RemoteFunction:
        case Kind::RemoteTask: {
            bool cancel = false;
            {
                std::lock_guard<std::mutex> g(e.mu);
                if (e.stage == Stage::Busy && e.cancel && !e.cancelSent) {
                    cancel = e.cancelSent = true;
                } else {
                    if (e.stage != Stage::Ready || !(e.remote || e.remoteTask)) return;
                    e.out.swap(e.sendBuf);
                    e.stage = Stage::Busy;
                }
            }
            if (cancel) {
                if (e.remoteTask) e.remoteTask->cancel(e.callId);
                return;
            }
            rant::SendStatus st;
            auto done = [&e](const rant::ResponseView<Bytes>& r) { Answered(e, r); };
            if (e.remote) {
                st = e.remote->call_async(View(e.sendBuf), done);
            } else {
                const rant::TaskCall call = e.remoteTask->call_async(View(e.sendBuf), [&e](const rant::ProgressView<Bytes>& p) {
                    if (!p.has_value()) return;
                    const Bytes b = p.data();
                    std::lock_guard<std::mutex> g(e.mu);
                    e.progress.assign(b.data(), b.data() + b.size());
                    e.progressFresh = true;
                }, done);
                e.callId = call.id;
                st = call.status;
            }
            if (st != rant::SendStatus::Ok) {
                std::string why = Status("the call was refused", st);
                std::lock_guard<std::mutex> g(e.mu);
                e.fault = std::move(why);
                e.stage = Stage::Failed;
            }
            break;
        }
        case Kind::Subscriber: break;
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
            for (auto& p : s.entities)
                if (p && std::find(gone.begin(), gone.end(), p.get()) != gone.end()) p.reset();
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
                Service(*e);
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
    {
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

bool Is(const Record* e, Kind a, Kind b) { return e && (e->kind == a || e->kind == b); }

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

int Configure(int& owner, const char* name, size_t length, int domain)
{
    State& s = S();
    bool refused = false;
    {
        std::lock_guard<std::mutex> g(s.mu);
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
        auto e = std::make_unique<Record>();
        e->kind = static_cast<Kind>(kind);
        e->name.assign(name, length);
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
    {
        std::lock_guard<std::mutex> g(e->mu);
        e->schema[which] = l->schema;
        ++e->layoutGen;
        e->hasLast = false;
    }
    // Room for the first values, so the cycle does not allocate for them.
    const size_t room = l->root.form == plc::Member::Form::String ? 4 + l->root.strCap : l->root.size;
    e->scratch.reserve(room);
    e->last.reserve(room);
    {
        std::lock_guard<std::mutex> g(e->mu);
        e->out.reserve(room);
        e->sendBuf.reserve(room);
    }
    Wake();
}

int Publish(int handle, uint8_t* data, int length, bool send)
{
    Record* e = Find(handle);
    if (!e || e->kind != Kind::Publisher || !Encode(*e, VALUE, data, length)) return 0;
    const bool changed = !e->hasLast || e->scratch != e->last;
    std::lock_guard<std::mutex> g(e->mu);
    if (send || changed || e->resend) {
        e->last = e->scratch;
        e->hasLast = true;
        e->out = e->scratch;
        e->stage = Stage::Ready;
        e->resend = false;
        Wake();
    }
    return e->count;
}

int Take(int handle, uint8_t* data, int length)
{
    Record* e = Find(handle);
    if (!e || e->kind != Kind::Subscriber) return 0;
    const Layout& l = e->parts[VALUE];
    std::lock_guard<std::mutex> g(e->mu);
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
    return e->count * 2 + copied;
}

int Share(int handle, uint8_t* data, int length)
{
    Record* e = Find(handle);
    if (!Is(e, Kind::VariableDefinition, Kind::RemoteVariable) || !Encode(*e, VALUE, data, length)) return 0;
    const Layout& l = e->parts[VALUE];
    std::lock_guard<std::mutex> g(e->mu);
    // A remote starts from the owner's value, so its own first value is no change.
    const bool baseline = !e->hasLast && e->kind == Kind::RemoteVariable;
    if (!e->hasLast || e->scratch != e->last) {
        e->last = e->scratch;
        e->hasLast = true;
        if (!baseline) {
            e->out = e->scratch;
            e->stage = Stage::Ready;
            Wake();
        }
    }
    // An echo that never came (the owner may skip a value) lets the held value through.
    if (e->awaitEcho && std::chrono::steady_clock::now() >= e->echoDeadline) {
        e->awaitEcho = false;
        e->fresh = e->held;
        e->held = false;
    }
    // While the program's change is on its way, the mesh's value waits, since the change follows it.
    if (!e->fresh || e->stage == Stage::Ready || e->stage == Stage::Sending) return e->count * 2;
    e->fresh = false;
    if (e->in.empty()) return e->count * 2;
    if (!Decode(l, e->in.data(), e->in.size(), data)) {
        e->fault = "a value arrived that does not fit Value";
        return e->count * 2;
    }
    e->fault.clear();
    blocks::Encode(l, data, e->scratch);
    if (e->scratch == e->last) return e->count * 2;
    e->last = e->scratch;
    e->out = e->scratch;
    ++e->count;
    return e->count * 2 + 1;
}

int Serve(int handle, uint8_t* request, int requestLength, uint8_t* progress, int progressLength,
          uint8_t* response, int responseLength, bool complete)
{
    Record* e = Find(handle);
    if (!Is(e, Kind::FunctionDefinition, Kind::TaskDefinition)) return 0;
    const bool task = e->kind == Kind::TaskDefinition;
    const Layout& req = e->parts[VALUE];
    if (!Fits(req, requestLength)) return 0;
    Stage stage;
    bool cancel;
    {
        std::lock_guard<std::mutex> g(e->mu);
        stage = e->stage;
        cancel = e->cancel;
    }
    // Only this task moves a call out of Waiting, so its values encode outside the lock.
    if (stage == Stage::Waiting) {
        if (complete && Encode(*e, RESPONSE, response, responseLength)) {
            std::lock_guard<std::mutex> g(e->mu);
            e->out = e->scratch;
            e->stage = Stage::Answered;
            ++e->count;
            Wake();
            return e->count * 8;
        }
        if (task && Encode(*e, PROGRESS, progress, progressLength) && e->scratch != e->last) {
            e->last = e->scratch;
            std::lock_guard<std::mutex> g(e->mu);
            e->progress = e->scratch;
            e->progressFresh = true;
            Wake();
        }
        std::lock_guard<std::mutex> g(e->mu);
        return e->count * 8 + SERVE_HELD + (cancel ? SERVE_CANCEL : 0);
    }
    std::lock_guard<std::mutex> g(e->mu);
    if (stage != Stage::Idle) return e->count * 8;
    if (task) {
        if (!e->taskIn) return e->count * 8;
        e->currentTask = std::move(*e->taskIn);
        e->taskIn.reset();
    } else {
        if (e->calls.empty()) return e->count * 8;
        e->current = std::move(e->calls.front().first);
        e->in = std::move(e->calls.front().second);
        e->calls.pop_front();
    }
    e->cancel = false;
    // The node thread answers every call, since a Rant call from here could wait on its lock.
    if (!Decode(req, e->in.data(), e->in.size(), request)) {
        e->reason = "the request does not fit the PLC's Request";
        e->stage = Stage::Answered;
        Wake();
        return e->count * 8;
    }
    // Progress goes out once the program changes it, not the last call's.
    if (task && Encode(*e, PROGRESS, progress, progressLength)) e->last = e->scratch;
    e->progressFresh = false;
    e->stage = Stage::Waiting;
    return e->count * 8 + SERVE_NEW + SERVE_HELD;
}

void Reject(int handle, const std::string& reason)
{
    Record* e = Find(handle);
    if (!e || e->kind != Kind::TaskDefinition) return;
    std::lock_guard<std::mutex> g(e->mu);
    if (e->stage != Stage::Waiting) return;
    e->reason = reason.empty() ? "the PLC failed the task" : reason;
    e->stage = Stage::Answered;
    ++e->count;
    Wake();
}

int Call(int handle, bool execute, bool cancel, uint8_t* request, int requestLength, uint8_t* progress,
         int progressLength, uint8_t* response, int responseLength)
{
    Record* e = Find(handle);
    if (!Is(e, Kind::RemoteFunction, Kind::RemoteTask)) return 0;
    const bool rising = execute && !e->lastExecute;
    e->lastExecute = execute;
    const Layout& rsp = e->parts[RESPONSE];
    const bool encoded = rising && Encode(*e, VALUE, request, requestLength);
    std::lock_guard<std::mutex> g(e->mu);
    if (encoded && (e->stage == Stage::Idle || e->stage == Stage::Done || e->stage == Stage::Failed)) {
        e->out = e->scratch;
        e->fault.clear();
        e->in.clear();
        e->cancel = e->cancelSent = false;
        e->progressFresh = false;
        e->stage = Stage::Ready;
        Wake();
        return 1;
    }
    switch (e->stage) {
        case Stage::Ready:
        case Stage::Busy:
            if (cancel && !e->cancel) {
                e->cancel = true;
                Wake();
            }
            if (e->progressFresh && Fits(e->parts[PROGRESS], progressLength)) {
                e->progressFresh = false;
                Decode(e->parts[PROGRESS], e->progress.data(), e->progress.size(), progress);
            }
            return 1;
        case Stage::Done:
            if (!e->in.empty()) {
                const bool fits = Fits(rsp, responseLength) && Decode(rsp, e->in.data(), e->in.size(), response);
                e->in.clear();
                if (!fits) {
                    e->fault = "the response does not fit the PLC's Response";
                    e->stage = Stage::Failed;
                    return 3;
                }
                ++e->count;
            }
            if (!execute) e->stage = Stage::Idle;
            return 2;
        case Stage::Failed:
            if (!execute) e->stage = Stage::Idle;
            return 3;
        default: return 0;
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
    if (!e) {
        text = handle ? "the block lost its entity, it opens a new one"
                      : "no entity: the name is empty or there are too many blocks";
        length = std::strlen(text);
    }
    std::lock_guard<std::mutex> g(e ? e->mu : S().mu);
    if (e) {
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
    s.entities.clear();
    s.total = 0;
    s.active = 0;
    s.firstUse.reset();
    s.name.clear();
    s.mirrorSlots = 0;
    s.mirrorBrowsing = false;
    s.generation = (s.generation + 1) & 0x7fff;
    if (!s.generation) s.generation = 1;
    s.running = false;
}

} // namespace blocks
