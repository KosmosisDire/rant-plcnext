#include "mirror/mirror.h"

#include "blocks/runtime.h"
#include "core/log.h"
#include "mirror/sampler.h"
#include "mirror/sync.h"
#include "plc/browse.h"
#include "plc/plc.h"
#include "plc/tasks.h"

#include "Arp/System/Rsc/ServiceManager.hpp"

#include <cstring>
#include <pthread.h>
#include <sched.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

namespace plc {

Config g_cfg;

Arp::Plc::Meta::Services::IVariableBrowseService* g_browseSvc     = nullptr;
Arp::Plc::Gds::Services::ISubscriptionService*    g_subSvc        = nullptr;
Arp::Plc::Gds::Services::IDataAccessService*      g_dataAccessSvc = nullptr;
Arp::Plc::Gds::Services::IForceService*           g_forceSvc      = nullptr;

namespace {

using Clock = std::chrono::steady_clock;
using std::chrono::duration_cast;
using std::chrono::microseconds;
using std::chrono::milliseconds;

// A write from the mesh, queued by a variable's handler on the node's thread.
struct Write {
    size_t               var = 0;
    std::vector<uint8_t> value;
    bool                 forced = false;
};

struct State {
    std::mutex              mu;
    std::condition_variable wake;
    Config                  wanted;
    std::deque<Write>       inbox;
    std::string             failure;
    std::atomic<uint64_t>   gen{0};
    std::atomic<int>        published{0};
    std::atomic<bool>       running{false};
    std::atomic<bool>       stop{false};
    std::thread             thread;
    // The services stay referenced while the library is loaded.
    Arp::Plc::Meta::Services::IVariableBrowseService::Ptr browse;
    Arp::Plc::Gds::Services::ISubscriptionService::Ptr    subscription;
    Arp::Plc::Gds::Services::IDataAccessService::Ptr      dataAccess;
    Arp::Plc::Gds::Services::IForceService::Ptr           force;
};

State& M()
{
    static State s;
    return s;
}

template <class T>
T* Service(typename T::Ptr& keep)
{
    if (!keep) Arp::System::Rsc::ServiceManager::TryGetService<T>(keep);
    return keep.get();
}

void SetFailure(const std::string& why)
{
    State& s = M();
    std::lock_guard<std::mutex> g(s.mu);
    if (!why.empty() && why != s.failure) log::ArpError("mirror: " + why);
    s.failure = why;
}

// Stop, or new settings to start over with.
bool Leaving(uint64_t gen) { return M().stop || M().gen != gen; }

void Sleep(int ms, uint64_t gen)
{
    State& s = M();
    std::unique_lock<std::mutex> lk(s.mu);
    s.wake.wait_for(lk, milliseconds(ms), [gen] { return Leaving(gen); });
}

// Browse until something is found: an empty browse usually means the GDS is not serving yet.
std::vector<GdsVar> BrowseUntilFound(uint64_t gen)
{
    std::vector<GdsVar> vars;
    for (int attempt = 1; !Leaving(gen); ++attempt) {
        vars = BrowseVariables();
        if (!vars.empty()) break;
        log::ClearPending();   // this browse's notes would only flood the node's log
        SetFailure("no GDS variables found (attempt " + std::to_string(attempt) + "), retrying in 2s");
        Sleep(2000, gen);
    }
    return vars;
}

// The node once it holds a slot for every variable, each owning a value and a set channel.
std::shared_ptr<rant::Node> WaitNode(size_t vars, uint64_t& epoch, uint64_t gen)
{
    while (!Leaving(gen)) {
        if (auto node = blocks::MirrorNode(vars * 2 + 64, false, epoch)) return node;
        Sleep(100, gen);
    }
    return nullptr;
}

// One owner variable per GDS variable, seeded with the live value.
size_t CreateVariables(rant::Node& node, std::vector<GdsVar>& vars)
{
    size_t created = 0, primed = 0;
    std::vector<uint8_t> initBuf;
    for (size_t i = 0; i < vars.size(); ++i) {
        GdsVar& v = vars[i];
        v.var = {};
        // The node keeps one schema per shape, so equal types share it. A PLC type name the
        // node already holds with another shape (a standard type, say) falls back to the hash.
        std::string tried;
        for (bool hashName : { false, true }) {
            std::string text = SchemaText(v, hashName);
            if (text == tried) break;   // nothing took a PLC name, so the hash pass is the same text
            tried = text;
            try { v.schema = node.schema(text); break; }
            catch (const rant::Error& e) { log::Error("schema for " + v.topic + " refused: " + e.what()); }
        }
        const bool initOk = ReadLiveValue(v, initBuf);

        rant::VariableOptions<rant::Bytes> opts;
        opts.allow_force = v.forceable;   // remote force only where the PLC permits it
        opts.read_only   = false;         // remote writes flow back to the PLC
        // A watched variable can publish a whole ring in one drain, so the repair window holds one.
        opts.keep_last   = Sampler::RingRecords();
        // One transmit pass, then the oldest unacked sample is evicted: a slow subscriber never stalls
        // the loop and still converges on the latest value.
        opts.backpressure_wait = std::chrono::microseconds(1);
        if (initOk) opts.initial = rant::Bytes(initBuf.data(), initBuf.size());

        // Skip a single bad variable rather than give up on the rest.
        try { v.var = node.variable_definition<rant::Bytes>(v.topic, opts, v.schema); }
        catch (...) {}
        if (!v.var) { log::Error("failed to create variable " + v.topic); continue; }
        ++created;
        if (initOk) { v.lastValue = initBuf; v.hasValue = true; ++primed; }

        // Our own sets come back with source 0. The rest waits for the mirror thread.
        v.var.on_write([i](const rant::VariableUpdate& u) {
            if (u.source() == 0) return;
            const rant::Bytes b = u.value();
            State& s = M();
            {
                std::lock_guard<std::mutex> g(s.mu);
                s.inbox.push_back({ i, std::vector<uint8_t>(b.data(), b.data() + b.size()), u.forced() });
            }
            s.wake.notify_all();
        });
    }
    log::Info("created " + std::to_string(created) + " variable(s), primed "
              + std::to_string(primed) + "/" + std::to_string(vars.size()));
    return created;
}

// The next deadline on a fixed cadence. A period missed entirely (the loop was
// busy) is skipped, not replayed as a burst.
Clock::time_point Advance(Clock::time_point next, Clock::duration every, Clock::time_point now)
{
    next += every;
    if (next < now) next = now + every;
    return next;
}

void Apply(std::vector<GdsVar>& vars)
{
    std::deque<Write> writes;
    {
        std::lock_guard<std::mutex> g(M().mu);
        writes.swap(M().inbox);
    }
    for (const Write& w : writes)
        if (w.var < vars.size()) OnRemoteWrite(vars[w.var], w.value, w.forced);
}

void LogTasks(std::vector<GdsVar>& vars)
{
    std::string line;
    if (LoadEsmTasks()) {
        line = "esm tasks (" + g_tasks.source + "):";
        for (const auto& t : g_tasks.tasks)
            line += " " + t.name + " " + std::to_string(t.cycleUs) + " us/" + std::to_string(t.programs) + " program(s)";
    } else {
        line = "no ESM configuration found, task cycles unknown";
    }
    line += ", fast floor " + std::to_string(Sampler::FastFloorUs()) + " us";
    log::Info(line);
    log::ArpInfo(line);
    for (auto& v : vars) v.cycleUs = g_tasks.CycleUs(v.name);
}

// One run of the mirror with g_cfg, until it stops or the settings change.
void Serve(uint64_t gen)
{
    State& s = M();
    uint64_t epoch = 0;
    blocks::MirrorNode(0, true, epoch);
    std::vector<GdsVar> vars = BrowseUntilFound(gen);
    if (vars.empty()) return;
    // Which task samples each variable, and how fast: the fast path's floor, the ring size and
    // the sample budget all follow from it.
    LogTasks(vars);

    std::shared_ptr<rant::Node> node = WaitNode(vars.size(), epoch, gen);
    if (!node) return;
    s.published = static_cast<int>(CreateVariables(*node, vars));
    log::ArpInfo("mirroring " + std::to_string(s.published) + " variable(s) under "
                 + (g_cfg.prefix.empty() ? std::string("the mesh root") : g_cfg.prefix));

    const NameIndex names = BuildNameIndex(vars);
    Sampler sampler(vars, names);
    if (!sampler.Start()) {
        SetFailure("could not create the GDS subscription, retrying in 2s");
        s.published = 0;
        Sleep(2000, gen);
        return;
    }
    SetFailure("");

    // Reflect any force already active on the PLC before publishing starts.
    PollForces(vars, names);

    // Real time priority only from here on: the browse and variable creation above are the one
    // heavy stretch, and they ran time shared.
    if (g_cfg.schedPrio > 0) {
        sched_param sp{};
        sp.sched_priority = g_cfg.schedPrio;
        const int rc = pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
        if (rc != 0) log::ArpError("mirror: SCHED_FIFO refused (errno " + std::to_string(rc)
                                   + "), the mirror thread stays time shared");
    }

    // Each activity keeps its own deadline and the loop sleeps between them (docs/design.md, the
    // mirror thread). Writes from the mesh wake it at once.
    const size_t slowChunks   = std::max<size_t>(1, sampler.SlowChunks());
    const auto drainEvery     = milliseconds(g_cfg.loopMs);
    const auto slowEvery      = std::max<Clock::duration>(
        duration_cast<Clock::duration>(microseconds(sampler.SlowRateUs())) / slowChunks, milliseconds(1));
    const auto forceEvery     = milliseconds(200);
    const auto reconcileEvery = milliseconds(500);

    size_t            slowNext = 0;
    Clock::time_point now = Clock::now();
    Clock::time_point nextDrain = now, nextSlow = now, nextForce = now + forceEvery,
                      nextReconcile = now + reconcileEvery;

    while (!Leaving(gen)) {
        Apply(vars);
        now = Clock::now();
        if (!sampler.FastActive()) {
            nextDrain = now + drainEvery;   // first drain one period after the ring goes live
        } else if (now >= nextDrain) {
            sampler.DrainFast();
            nextDrain = Advance(nextDrain, drainEvery, now);
        }
        if (now >= nextSlow) {
            sampler.ReadSlow(slowNext);
            slowNext = (slowNext + 1) % slowChunks;
            nextSlow = Advance(nextSlow, slowEvery, now);
        }
        if (now >= nextForce) {
            PollForces(vars, names);
            nextForce = Advance(nextForce, forceEvery, now);
        }
        if (now >= nextReconcile) {
            // The blocks reopened the node, whose old handles now answer NoTopic.
            uint64_t at = epoch;
            if (!blocks::MirrorNode(vars.size() * 2 + 64, false, at) || at != epoch) {
                node = WaitNode(vars.size(), epoch, gen);
                if (!node) break;
                s.published = static_cast<int>(CreateVariables(*node, vars));
            }
            sampler.Reconcile();
            nextReconcile = Advance(nextReconcile, reconcileEvery, now);
        }

        Clock::time_point wake = std::min({nextSlow, nextForce, nextReconcile});
        if (sampler.FastActive()) wake = std::min(wake, nextDrain);
        std::unique_lock<std::mutex> lk(s.mu);
        s.wake.wait_until(lk, wake, [&s, gen] { return !s.inbox.empty() || Leaving(gen); });
    }

    sampler.Stop();
    for (auto& v : vars) v.var = {};
    s.published = 0;
    blocks::Demote();
    log::Info("mirror stopped");
}

void Run()
{
    pthread_setname_np(pthread_self(), "rant_mirror");
    blocks::Demote();
    State& s = M();
    g_browseSvc     = Service<Arp::Plc::Meta::Services::IVariableBrowseService>(s.browse);
    g_subSvc        = Service<Arp::Plc::Gds::Services::ISubscriptionService>(s.subscription);
    g_dataAccessSvc = Service<Arp::Plc::Gds::Services::IDataAccessService>(s.dataAccess);
    g_forceSvc      = Service<Arp::Plc::Gds::Services::IForceService>(s.force);
    if (!g_browseSvc || !g_subSvc || !g_dataAccessSvc) {
        SetFailure("the PLC offers no GDS browse, subscription or data access service");
        return;
    }
    while (!s.stop) {
        uint64_t gen;
        {
            std::lock_guard<std::mutex> g(s.mu);
            g_cfg = s.wanted;
            gen = s.gen;
            s.inbox.clear();
        }
        Serve(gen);
    }
    uint64_t epoch;
    blocks::MirrorNode(0, false, epoch);
}

} // namespace

int Mirror(const Config& cfg)
{
    State& s = M();
    bool changed;
    {
        std::lock_guard<std::mutex> g(s.mu);
        changed = s.wanted != cfg;
        if (changed) {
            s.wanted = cfg;
            ++s.gen;
        }
    }
    if (!s.running.exchange(true)) {
        s.stop = false;
        s.thread = std::thread(Run);
    } else if (changed) {
        s.wake.notify_all();
    }
    return s.published;
}

size_t MirrorFailure(char* out, size_t capacity)
{
    State& s = M();
    std::lock_guard<std::mutex> g(s.mu);
    const size_t length = s.failure.size() < capacity ? s.failure.size() : capacity;
    if (length) std::memcpy(out, s.failure.data(), length);
    return length;
}

void StopMirror()
{
    State& s = M();
    if (!s.running) return;
    s.stop = true;
    s.wake.notify_all();
    if (s.thread.joinable()) s.thread.join();
    std::lock_guard<std::mutex> g(s.mu);
    s.wanted = Config{};
    s.failure.clear();
    s.inbox.clear();
    s.published = 0;
    s.running = false;
}

} // namespace plc
