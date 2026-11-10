#include "mirror/sampler.h"

#include "plc/codec.h"
#include "plc/tasks.h"
#include "mirror/config.h"
#include "core/log.h"

#include "Arp/Plc/Gds/Services/VariableInfo.hpp"
#include "Arp/Plc/Gds/Services/DataAccessError.hpp"
#include "Arp/Base/Rsc/Commons/RscArrayReader.hpp"

#include <algorithm>
#include <cmath>

namespace plc {

using Arp::Base::Rsc::Commons::RscArrayReader;
using Arp::Plc::Gds::Services::ISubscriptionService;
using Arp::Plc::Gds::Services::SubscriptionKind;
using Arp::Plc::Gds::Services::VariableInfo;
using Arp::Plc::Gds::Services::DataAccessError;

namespace {

// The interval a task of cycle cycleUs samples at when rateUs is asked: the GDS rounds down to
// a multiple of the cycle and never goes below it. An unknown cycle (0) meets the request.
int64_t EffectiveInterval(int64_t cycleUs, int64_t rateUs)
{
    if (cycleUs <= 0) return std::max<int64_t>(rateUs, 1);
    if (rateUs <= cycleUs) return cycleUs;
    return (rateUs / cycleUs) * cycleUs;
}

std::string LoadText(double load)
{
    return std::to_string(static_cast<long long>(std::llround(load))) + " values/s";
}

} // namespace

bool Sampler::ReadLayout(uint32_t subId, std::vector<std::string>& names)
{
    names.clear();
    using Delegate = ISubscriptionService::GetTimeStampedVariableInfosVariableInfoDelegate;
    Delegate d = Delegate::create([&names](IRscReadEnumerator<VariableInfo>& reader) {
        reader.BeginRead();
        VariableInfo info;
        while (reader.ReadNext(info)) names.emplace_back(info.Name.CStr());
        reader.EndRead();
    });
    try { return g_subSvc->GetTimeStampedVariableInfos(subId, d) == DataAccessError::None; }
    catch (...) { return false; }
}

bool Sampler::LearnSlowLayout(SlowSub& sub)
{
    std::vector<std::string> names;
    if (!ReadLayout(sub.id, names)) return false;
    sub.layout.clear();
    sub.layout.reserve(names.size());
    for (const auto& nm : names) {
        if (nm == "timestamp") { sub.layout.push_back({true, -1}); continue; }
        auto it = names_.find(nm);
        sub.layout.push_back({false, it != names_.end() ? it->second : -1});
    }
    return !sub.layout.empty();
}

// Publishing records against a wrong layout would put values on the wrong
// variables, so no layout means no fast drain.
bool Sampler::RebuildFastLayout()
{
    fastLayout_.clear();
    fastShapeWarned_ = false;
    std::vector<std::string> names;
    if (!ReadLayout(fastSub_, names)) return false;
    for (const auto& nm : names) {
        if (nm == "timestamp") { fastLayout_.emplace_back(); continue; }
        if (fastLayout_.empty()) fastLayout_.emplace_back();   // tolerate a missing leading marker
        auto it = names_.find(nm);
        fastLayout_.back().push_back(it != names_.end() ? it->second : -1);
    }
    return !fastLayout_.empty();
}

void Sampler::PublishAt(int varIdx, int64_t ts, const RscVar& value)
{
    if (varIdx < 0 || static_cast<size_t>(varIdx) >= vars_.size()) { DrainVariant(value); return; }
    PublishSample(vars_[varIdx], ts, value);
}

double Sampler::LoadPerSecond(const std::vector<int>& members, int64_t rateUs) const
{
    double load = 0;
    for (int i : members) {
        const GdsVar& gv = vars_[i];
        load += static_cast<double>(SampleWeight(gv)) * 1e6
              / static_cast<double>(EffectiveInterval(gv.cycleUs, rateUs));
    }
    return load;
}

// Rounded up to a multiple of the floor, since the GDS rounds down per cycle, then checked again
// because that rounding makes the cost step shaped (docs/design.md, cost and the budget).
int64_t Sampler::BudgetRate(const std::vector<int>& members, int64_t floorUs) const
{
    floorUs = std::max<int64_t>(floorUs, 1);
    if (g_cfg.sampleBudget <= 0 || members.empty()) return floorUs;
    const double budget = g_cfg.sampleBudget;
    int64_t rate = floorUs;
    for (int guard = 0; guard < 1000; ++guard) {
        if (LoadPerSecond(members, rate) <= budget) return rate;
        double fixed = 0, scaling = 0;
        int64_t nextCycle = 0;
        for (int i : members) {
            const GdsVar& gv = vars_[i];
            const double  w  = static_cast<double>(SampleWeight(gv));
            if (gv.cycleUs > rate) {
                fixed += w * 1e6 / static_cast<double>(gv.cycleUs);
                if (nextCycle == 0 || gv.cycleUs < nextCycle) nextCycle = gv.cycleUs;
            } else {
                scaling += w * 1e6;
            }
        }
        int64_t next;
        if (budget - fixed <= 0 || scaling <= 0) {
            // The slower tasks alone exceed the budget: only a request beyond
            // the next task cycle starts decimating them.
            if (nextCycle == 0) return rate;
            next = nextCycle;
        } else {
            next = static_cast<int64_t>(std::ceil(scaling / (budget - fixed)));
        }
        next = std::max(next, rate + 1);
        rate = ((next + floorUs - 1) / floorUs) * floorUs;
    }
    return rate;
}

int64_t Sampler::FastFloorUs()
{
    if (g_cfg.fastRateUs > 0) return g_cfg.fastRateUs;
    return g_tasks.Known() ? g_tasks.fastestUs : 1000;
}

// Two drain periods of floor-rate samples plus slack, so a drain that runs late
// by a whole period still loses nothing.
uint16_t Sampler::RingRecords()
{
    if (g_cfg.fastRing > 0) return static_cast<uint16_t>(g_cfg.fastRing);
    const int64_t rate     = std::max<int64_t>(FastFloorUs(), 100);
    const int64_t perDrain = (static_cast<int64_t>(g_cfg.loopMs) * 1000 + rate - 1) / rate;
    return static_cast<uint16_t>(std::clamp<int64_t>(perDrain * 2 + 8, 8, 4096));
}

bool Sampler::Start()
{
    std::vector<int> all;
    size_t totalValues = 0;
    for (size_t i = 0; i < vars_.size(); ++i) { all.push_back(static_cast<int>(i)); totalValues += SampleWeight(vars_[i]); }
    slowRateUs_ = BudgetRate(all, g_cfg.slowRateUs);

    // Chunks are cut by values, not variables: one big struct is a chunk of its own.
    const size_t chunk = std::max<size_t>(1, static_cast<size_t>(g_cfg.slowChunk));
    for (size_t first = 0; first < vars_.size(); ) {
        size_t last = first, values = 0;
        while (last < vars_.size() && (last == first || values + SampleWeight(vars_[last]) <= chunk))
            values += SampleWeight(vars_[last++]);
        SlowSub sub;
        try {
            sub.id = g_subSvc->CreateSubscription(SubscriptionKind::HighPerformance);
            if (!sub.id) { Stop(); return false; }
            for (size_t i = first; i < last; ++i) g_subSvc->AddVariable(sub.id, RscStr(vars_[i].name.c_str()));
            g_subSvc->Subscribe(sub.id, static_cast<Arp::uint64>(slowRateUs_));
        } catch (...) { Stop(); return false; }
        LearnSlowLayout(sub);
        slow_.push_back(std::move(sub));
        first = last;
    }
    if (slow_.empty()) return false;
    log::Info("slow snapshot: " + std::to_string(vars_.size()) + " variable(s), " + std::to_string(totalValues)
              + " value(s), in " + std::to_string(slow_.size()) + " subscription(s) of <= " + std::to_string(chunk)
              + " values, each sampled every " + std::to_string(slowRateUs_) + " us, "
              + LoadText(LoadPerSecond(all, slowRateUs_))
              + (slowRateUs_ != g_cfg.slowRateUs ? " (the slow rate raised to fit the sample budget)" : ""));

    // Created empty and left unsubscribed until a remote shows up.
    const uint16_t ring = RingRecords();
    try { fastSub_ = g_subSvc->CreateRecordingSubscription(ring); }
    catch (...) { fastSub_ = 0; }
    if (!fastSub_)
        log::Error("CreateRecordingSubscription failed; watched variables are limited to the slow sample rate");
    else
        log::Info("fast ring holds " + std::to_string(ring) + " records per task, floor "
                  + std::to_string(FastFloorUs()) + " us");
    return true;
}

void Sampler::Stop()
{
    for (const auto& sub : slow_) { if (sub.id) { try { g_subSvc->Unsubscribe(sub.id); } catch (...) {} } }
    slow_.clear();
    if (fastSub_ && fastActive_) { try { g_subSvc->Unsubscribe(fastSub_); } catch (...) {} }
    fastSub_ = 0;
    fastActive_ = false;
}

void Sampler::ReadSlow(size_t chunk)
{
    if (chunk >= slow_.size()) return;
    const SlowSub& sub = slow_[chunk];
    using Delegate = ISubscriptionService::ReadTimeStampedValuesValuesDelegate;
    Delegate d = Delegate::create([this, &sub](IRscReadEnumerator<RscVar>& reader) {
        reader.BeginRead();
        RscVar current;
        size_t pos = 0;
        int64_t taskTs = 0;   // each task's timestamp precedes that task's variables
        while (reader.ReadNext(current) && pos < sub.layout.size()) {
            const auto& entry = sub.layout[pos++];
            if (entry.isTimestamp) {
                int64_t ts = 0;
                try { current.CopyTo(ts); } catch (...) { ts = 0; }
                if (ts != 0) taskTs = ts;
                continue;
            }
            // A variable on the fast path has usually had something newer
            // published already, so serialising this snapshot would be wasted work.
            if (entry.varIdx < 0 || (fastActive_ && vars_[entry.varIdx].inFast)) {
                DrainVariant(current);
                continue;
            }
            PublishAt(entry.varIdx, taskTs, current);
        }
        reader.EndRead();
    });
    try { g_subSvc->ReadTimeStampedValues(sub.id, d); } catch (...) {}
}

void Sampler::DrainFast()
{
    if (!fastActive_) return;

    // One array per ESM task, each holding records of [timestamp, value...] in fastLayout_ order.
    // Anything else is drained, not published.
    using Delegate = ISubscriptionService::ReadRecordsRecordsDelegate;
    Delegate d = Delegate::create([this](IRscReadEnumerator<RscVar>& reader) {
        reader.BeginRead();
        // Hoisted out of the loops: every RscVariant<512> construction zero-fills
        // its buffer, which at hundreds of values per drain is most of the work.
        RscVar taskArr, rec, el;
        size_t taskPos = 0;
        while (reader.ReadNext(taskArr)) {
            const std::vector<int>* group = taskPos < fastLayout_.size() ? &fastLayout_[taskPos] : nullptr;
            ++taskPos;
            if (taskArr.GetType() != RscType::Array) continue;
            if (!group) {
                if (!fastShapeWarned_) {
                    fastShapeWarned_ = true;
                    log::Error("fast drain saw more task groups than the layout lists; extra group dropped");
                }
                DrainVariant(taskArr);
                continue;
            }
            try {
                RscArrayReader records(taskArr);
                const size_t nRecords = records.GetSize();
                for (size_t r = 0; r < nRecords; ++r) {
                    records.ReadNext(rec);
                    if (rec.GetType() != RscType::Array) { DrainVariant(rec); continue; }
                    RscArrayReader values(rec);
                    const size_t nVals = values.GetSize();
                    if (nVals != group->size() + 1) {
                        if (!fastShapeWarned_) {
                            fastShapeWarned_ = true;
                            log::Error("fast record has " + std::to_string(nVals)
                                       + " element(s) but the layout expects "
                                       + std::to_string(group->size() + 1) + "; records dropped");
                        }
                        for (size_t k = 0; k < nVals; ++k) { values.ReadNext(el); DrainVariant(el); }
                        continue;
                    }
                    values.ReadNext(el);
                    int64_t ts = 0;
                    try { el.CopyTo(ts); } catch (...) { ts = 0; }
                    for (int varIdx : *group) {
                        values.ReadNext(el);
                        PublishAt(varIdx, ts, el);
                    }
                }
            } catch (...) {}
        }
        reader.EndRead();
    });

    // ReadRecords reports failure by return value, not by throwing. A failure
    // demotes the members to the slow path and asks Reconcile to rebuild.
    bool ok = false;
    try { ok = g_subSvc->ReadRecords(fastSub_, 0, d) == DataAccessError::None; }
    catch (...) { ok = false; }
    if (!ok) {
        fastActive_ = false;
        fastDirty_  = true;
        log::Error("ReadRecords failed; rebuilding the fast subscription");
    }
}

void Sampler::Reconcile()
{
    if (!fastSub_) return;

    // match_count() > 0 means some remote is matched to the variable right now.
    bool changed = false;
    for (auto& gv : vars_) {
        if (!gv.var) continue;
        const bool want = gv.var.match_count() > 0;
        if (want == gv.inFast) continue;
        try {
            if (want) {
                if (g_subSvc->AddVariable(fastSub_, RscStr(gv.name.c_str())) != DataAccessError::None) {
                    if (!gv.fastWarned) {
                        gv.fastWarned = true;
                        log::Error("could not add " + gv.name + " to the fast subscription; it stays at the slow rate");
                    }
                    continue;
                }
            } else {
                g_subSvc->RemoveVariable(fastSub_, RscStr(gv.name.c_str()));
            }
        } catch (...) { continue; }
        gv.inFast = want;
        changed = true;
    }
    if (!changed && !fastDirty_) return;

    std::vector<int> watched;
    size_t values = 0;
    for (size_t i = 0; i < vars_.size(); ++i)
        if (vars_[i].inFast) { watched.push_back(static_cast<int>(i)); values += SampleWeight(vars_[i]); }

    if (watched.empty()) {
        // Nobody watches anything: stop the ring. The Resubscribe that reactivates
        // it rebuilds the buffers, so nothing stale survives.
        if (fastActive_) {
            try { g_subSvc->Unsubscribe(fastSub_); } catch (...) {}
            log::Info("fast subscription idle (no watched variables)");
        }
        fastActive_  = false;
        fastDirty_   = false;
        fastLayout_.clear();
        return;
    }

    // Each watched variable is sampled at its own task's rate, slowed by the budget as more are watched.
    const int64_t floorUs = FastFloorUs();
    const int64_t rate    = BudgetRate(watched, floorUs);
    bool ok = false;
    try {
        const auto rateArg = static_cast<Arp::uint64>(rate);
        ok = (fastEverSubscribed_ ? g_subSvc->Resubscribe(fastSub_, rateArg)
                                  : g_subSvc->Subscribe(fastSub_, rateArg)) == DataAccessError::None;
    } catch (...) { ok = false; }
    if (ok) fastEverSubscribed_ = true;
    if (ok) ok = RebuildFastLayout();

    if (ok) {
        fastActive_  = true;
        fastDirty_   = false;
        log::Info("fast subscription covers " + std::to_string(watched.size()) + " watched variable(s) ("
                  + std::to_string(values) + " values) across " + std::to_string(fastLayout_.size())
                  + " task group(s), requesting every " + std::to_string(rate) + " us"
                  + (rate != floorUs ? " (raised from " + std::to_string(floorUs) + " us by the sample budget)" : "")
                  + ", " + LoadText(LoadPerSecond(watched, rate)));
    } else {
        // The members keep updating at the slow rate until the next retry.
        fastActive_  = false;
        fastDirty_   = true;
        fastLayout_.clear();
        log::Error("fast subscription build failed ((re)subscribe or layout); retrying, members stay on the slow path");
    }
}

} // namespace plc
