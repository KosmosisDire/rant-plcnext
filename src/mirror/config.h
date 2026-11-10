#pragma once
// The mirror's settings: the Mirror block's inputs and the tuning docs/design.md explains.

#include <cstdint>
#include <string>

namespace plc {

struct Config {
    // Goes with '/' in front of every variable name. Empty = the mesh root.
    std::string prefix = "Plc";
    // Probe IsForcable per variable. The PLC logs an error per unforcable one.
    bool probeForcable = true;
    // Publish a program's local variables too. Globals and exposed program variables always are.
    bool publishLocals = false;
    // Bytes of a string member in a struct, which stays one fixed block. Longer values are cut.
    uint16_t structStringCap = 80;

    // SCHED_FIFO priority once the loop runs, 0 = time shared (docs/design.md, the mirror thread).
    int schedPrio = 5;
    // How often the fast ring is drained. Writes from the mesh are served as they arrive regardless.
    int loopMs = 20;
    // The fast subscription's floor, 0 = the fastest task from the ESM configuration, or 1000.
    int64_t fastRateUs = 0;
    // GDS sample rate of the slow (every variable) subscription.
    int64_t slowRateUs = 100000;
    // RSC values decoded per second per subscription, 0 = no cap (docs/design.md, the budget).
    int sampleBudget = 3000;
    // Values per slow subscription, so no one read holds the loop for long.
    int slowChunk = 32;
    // Fast ring records per task, 0 = two drain periods of floor rate samples plus slack.
    int fastRing = 0;

    // Only the block's inputs ever change.
    bool operator==(const Config& o) const
    {
        return prefix == o.prefix && probeForcable == o.probeForcable && publishLocals == o.publishLocals
            && structStringCap == o.structStringCap;
    }
    bool operator!=(const Config& o) const { return !(*this == o); }
};

// The running mirror's settings, written by its thread before each start.
extern Config g_cfg;

} // namespace plc
