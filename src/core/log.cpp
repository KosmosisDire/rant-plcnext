#include "core/log.h"

#include "Arp/Base/Commons/Logging/Log.hpp"

#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace plc::log {

namespace {

// A logger holds the node while it logs, so a close on the node thread never frees it under the call.
std::mutex                  s_mutex;
std::shared_ptr<rant::Node> s_node;
std::vector<std::pair<rant::Level, std::string>> s_pending;
// Lines kept while no node is open, the newest dropped past it so a node that never opens costs no more.
constexpr size_t PENDING_MAX = 256;

void Emit(rant::Level level, const std::string& message)
{
    std::shared_ptr<rant::Node> node;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        node = s_node;
        if (!node) {
            if (s_pending.size() < PENDING_MAX) s_pending.emplace_back(level, message);
            return;
        }
    }
    node->log(level, message);
}

} // namespace

void Info(const std::string& message)  { Emit(rant::Level::Info, message); }
void Error(const std::string& message) { Emit(rant::Level::Error, message); }

void ArpInfo(const std::string& message)  { Arp::Base::Commons::Logging::Log::Info("Rant: {0}", message.c_str()); }
void ArpError(const std::string& message) { Arp::Base::Commons::Logging::Log::Error("Rant: {0}", message.c_str()); }

void Attach(const std::shared_ptr<rant::Node>& node)
{
    std::vector<std::pair<rant::Level, std::string>> pending;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_node = node;
        pending.swap(s_pending);
    }
    for (const auto& [level, message] : pending) node->log(level, message);
}

void Detach()
{
    std::shared_ptr<rant::Node> was;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        was.swap(s_node);
    }
}

void ClearPending()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_pending.clear();
}

} // namespace plc::log
