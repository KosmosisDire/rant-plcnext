#include "plc/tasks.h"

#include <dirent.h>

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace plc {

EsmTasks g_tasks;

namespace {

// "current" is the runtime's symlink to the active project; PCWE is what
// PLCnext Engineer always downloads to, in case the link is missing.
const char* const ESM_DIRS[] = {
    "/opt/plcnext/projects/current/Plc/Esm",
    "/opt/plcnext/projects/PCWE/Plc/Esm",
};

// Value of attribute `key` inside one XML start tag, "" if absent.
std::string Attr(const std::string& tag, const char* key)
{
    const std::string needle = std::string(" ") + key + "=\"";
    const size_t at = tag.find(needle);
    if (at == std::string::npos) return "";
    const size_t start = at + needle.size();
    const size_t end   = tag.find('"', start);
    return end == std::string::npos ? "" : tag.substr(start, end - start);
}

// Every "<element ...>" start tag with exactly this element name.
void Tags(const std::string& xml, const char* element, std::vector<std::string>& out)
{
    const std::string open = std::string("<") + element;
    for (size_t pos = xml.find(open); pos != std::string::npos; pos = xml.find(open, pos + 1)) {
        const size_t nameEnd = pos + open.size();
        const char   after   = nameEnd < xml.size() ? xml[nameEnd] : '\0';
        if (after != ' ' && after != '/' && after != '>' && after != '\n' && after != '\r' && after != '\t') continue;
        const size_t end = xml.find('>', pos);
        if (end == std::string::npos) break;
        out.push_back(xml.substr(pos, end - pos + 1));
    }
}

std::string FindConfig()
{
    const std::string suffix = ".esm.config";
    for (const char* dir : ESM_DIRS) {
        DIR* d = ::opendir(dir);
        if (!d) continue;
        std::string found;
        while (const dirent* e = ::readdir(d)) {
            const std::string name = e->d_name;
            if (name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
                found = std::string(dir) + "/" + name;
                break;
            }
        }
        ::closedir(d);
        if (!found.empty()) return found;
    }
    return "";
}

} // namespace

int64_t EsmTasks::CycleUs(const std::string& gdsName) const
{
    if (!Known()) return 0;
    const size_t slash = gdsName.find('/');
    const size_t dot   = gdsName.find('.', slash == std::string::npos ? 0 : slash + 1);
    if (dot != std::string::npos) {
        auto it = programTask.find(gdsName.substr(0, dot));
        if (it != programTask.end()) return tasks[it->second].cycleUs;
    }
    return fastestUs;
}

bool LoadEsmTasks()
{
    g_tasks = EsmTasks{};
    const std::string path = FindConfig();
    if (path.empty()) return false;
    std::ifstream in(path);
    if (!in) return false;
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string xml = ss.str();

    std::vector<std::string> tags;
    Tags(xml, "CyclicTask", tags);
    for (const auto& t : tags) {
        EsmTask task;
        task.name = Attr(t, "name");
        const long long ns = std::atoll(Attr(t, "cycleTime").c_str());
        task.cycleUs = ns > 0 ? (ns + 999) / 1000 : 0;
        if (task.name.empty() || task.cycleUs <= 0) continue;
        g_tasks.tasks.push_back(task);
    }
    // Event and idle tasks have no cycle: a program in one of those stays
    // unmapped and its variables are charged at the fastest cycle.
    tags.clear();
    Tags(xml, "TaskProgramRelation", tags);
    for (const auto& t : tags) {
        const std::string task = Attr(t, "taskName"), program = Attr(t, "programName");
        for (size_t i = 0; i < g_tasks.tasks.size(); ++i) {
            if (g_tasks.tasks[i].name != task) continue;
            g_tasks.programTask[program] = static_cast<int>(i);
            ++g_tasks.tasks[i].programs;
        }
    }
    for (const auto& t : g_tasks.tasks)
        if (g_tasks.fastestUs == 0 || t.cycleUs < g_tasks.fastestUs) g_tasks.fastestUs = t.cycleUs;
    g_tasks.source = path;
    return g_tasks.Known();
}

} // namespace plc
