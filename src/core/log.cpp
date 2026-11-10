#include "core/log.h"

#include "Arp/Base/Commons/Logging/Log.hpp"

#include <mutex>
#include <utility>
#include <vector>

namespace plc::log {

namespace {

std::mutex   s_mutex;
rant::Node*    s_node = nullptr;
std::vector<std::pair<rant::Level, std::string>> s_pending;

void Emit(rant::Level level, const std::string& message)
{
    rant::Node* node;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        node = s_node;
        if (!node) {
            s_pending.emplace_back(level, message);
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

void Attach(rant::Node& node)
{
    std::vector<std::pair<rant::Level, std::string>> pending;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_node = &node;
        pending.swap(s_pending);
    }
    for (const auto& [level, message] : pending) node.log(level, message);
}

void Detach()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_node = nullptr;
}

void ClearPending()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_pending.clear();
}

} // namespace plc::log
