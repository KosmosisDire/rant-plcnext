#pragma once
// The slow and fast GDS subscriptions that feed the mirror thread (docs/design.md, sampling).

#include "plc/model.h"
#include "mirror/sync.h"

#include <cstdint>
#include <vector>

namespace plc {

class Sampler {
public:
    Sampler(std::vector<GdsVar>& vars, const NameIndex& names) : vars_(vars), names_(names) {}
    Sampler(const Sampler&) = delete;
    Sampler& operator=(const Sampler&) = delete;

    // Subscribes the slow chunks and creates the idle fast ring. False when a slow subscription
    // cannot be made, since the mirror cannot run without them.
    bool Start();
    void Stop();

    // Publish the latest slow sample of every variable in one chunk that is not
    // on the fast path. Chunks are 0 .. SlowChunks()-1.
    void   ReadSlow(size_t chunk);
    size_t SlowChunks() const { return slow_.size(); }
    // Publish every ring record of every watched variable since the last drain.
    void DrainFast();
    // Move variables on / off the fast ring as remotes come and go.
    void Reconcile();

    // The ring is live (>= 1 watched variable and a valid layout). The drain
    // timer only needs to run while this is true.
    bool    FastActive()  const { return fastActive_; }
    // The slow subscription's requested rate after the budget, microseconds.
    int64_t SlowRateUs()  const { return slowRateUs_; }

    // The fast subscription's floor: the fast rate setting, or the fastest cyclic
    // task from the ESM configuration, or 1000 us when neither is known.
    static int64_t FastFloorUs();
    // Ring records for two drain periods at the floor, also every variable's repair window depth.
    static uint16_t RingRecords();

private:
    struct SlowEntry { bool isTimestamp; int varIdx; };
    struct SlowSub {
        uint32_t               id = 0;
        std::vector<SlowEntry> layout;   // stream order: a "timestamp" per ESM task, then its variables
    };

    // The time-stamped stream layout of a subscription, in stream order: one
    // "timestamp" entry per ESM task followed by that task's variables.
    bool ReadLayout(uint32_t subId, std::vector<std::string>& names);
    bool LearnSlowLayout(SlowSub& sub);
    bool RebuildFastLayout();
    void PublishAt(int varIdx, int64_t ts, const RscVar& value);

    // RSC values per second a subscription over these variables pulls when it
    // asks for rateUs, each variable at its own task's effective interval.
    double  LoadPerSecond(const std::vector<int>& members, int64_t rateUs) const;
    // The smallest rate, a multiple of floorUs and never below it, at which that
    // load fits the sample budget.
    int64_t BudgetRate(const std::vector<int>& members, int64_t floorUs) const;

    std::vector<GdsVar>& vars_;
    const NameIndex&     names_;

    std::vector<SlowSub> slow_;
    int64_t              slowRateUs_ = 0;

    uint32_t fastSub_ = 0;                      // 0 = the service refused, slow path only
    std::vector<std::vector<int>> fastLayout_;  // per ESM task, var indices in record order
    bool fastActive_ = false;          // subscribed with >= 1 member and a valid layout
    bool fastEverSubscribed_ = false;  // after the first Subscribe only Resubscribe applies changes
    bool fastDirty_ = false;           // a (re)subscribe failed: retry even if membership is unchanged
    bool fastShapeWarned_ = false;     // one-shot per layout: records didn't match it
};

} // namespace plc
