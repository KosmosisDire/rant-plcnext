#pragma once
// Which ESM task samples each GDS variable and its cycle, from the project's ESM configuration
// (docs/design.md, task rates).

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace plc {

struct EsmTask {
    std::string name;
    int64_t     cycleUs  = 0;
    int         programs = 0;    // programs assigned to it
};

struct EsmTasks {
    std::vector<EsmTask>                 tasks;
    std::unordered_map<std::string, int> programTask;   // "Arp.Plc.Eclr/MainInstance" -> index in tasks
    int64_t                              fastestUs = 0; // shortest cycle, 0 = nothing known
    std::string                          source;        // file the tasks came from

    bool Known() const { return fastestUs > 0; }

    // The cycle of the task sampling this variable: its program's task, else the fastest one.
    // 0 when nothing is known.
    int64_t CycleUs(const std::string& gdsName) const;
};

extern EsmTasks g_tasks;

// Read the active project's ESM configuration into g_tasks. False (g_tasks
// empty) if no configuration was found or it declares no cyclic task.
bool LoadEsmTasks();

} // namespace plc
