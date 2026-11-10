// subwatch: subscribe to the variables one Rant node owns, the way an explorer browsing every
// variable does, and print the round trip to it and the update rate once a second.
//
//   subwatch <peer-name> <count|all> <seconds> [multicast-if] [peer-address-prefix] [meta-sections-mask]
#include "rant.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <thread>
#include <vector>

int main(int argc, char** argv)
{
    const std::string peerName = argc > 1 ? argv[1] : "plc";
    const std::string countArg = argc > 2 ? argv[2] : "all";
    const int seconds          = argc > 3 ? std::atoi(argv[3]) : 30;
    const std::string wantAddr = argc > 5 ? argv[5] : "";   // the peer whose address starts with this
    const uint32_t metaSections = argc > 6 ? static_cast<uint32_t>(std::strtoul(argv[6], nullptr, 0)) : 0;

    rant::NodeOptions o;
    o.max_topics    = 8192;
    o.fetch_details = true;
    if (argc > 4) o.multicast_interface = argv[4];

    std::optional<rant::Node> node;
    try { node.emplace("subwatch", o); }
    catch (const rant::Error& e) { std::fprintf(stderr, "open failed: %s\n", e.what()); return 1; }
    node->on_event([](const rant::Event& e) { std::fprintf(stderr, "event: %s\n", e.to_string().c_str()); });
    node->on_log([](const rant::LogLine& l) {
        std::printf("  [%.*s/%u%s] %.*s\n", static_cast<int>(l.node.size()), l.node.data(), l.node_id,
                    l.level == rant::Level::Error ? " ERROR" : "",
                    static_cast<int>(l.text.size()), l.text.data());
    });
    rant::Reflection mesh = node->reflection();

    uint32_t peerId = 0;
    for (int i = 0; i < 100 && !peerId; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        for (const auto& p : mesh.peers())
            if (p.active && p.name == peerName && (wantAddr.empty() || p.address.rfind(wantAddr, 0) == 0)) peerId = p.id;
    }
    if (!peerId) { std::fprintf(stderr, "peer '%s' not found\n", peerName.c_str()); return 1; }
    std::printf("peers:\n");
    for (const auto& p : mesh.peers())
        std::printf("  id %-3u %-16s %-22s %s%s\n", p.id, p.name.c_str(), p.address.c_str(),
                    p.active ? "active" : "inactive", p.id == peerId ? "  <== target" : "");

    // Give the details two seconds to page in.
    std::vector<std::string> names;
    for (int i = 0; i < 60; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        names.clear();
        for (const auto& e : mesh.entities(peerId))
            if (e.kind == rant::EntityKind::Variable && e.provides) names.push_back(e.name);
        if (!names.empty() && i >= 20) break;
    }
    std::printf("peer %s (id %u) owns %zu variables\n", peerName.c_str(), peerId, names.size());

    const size_t want = countArg == "all" ? names.size()
                                          : std::min<size_t>(names.size(), std::strtoul(countArg.c_str(), nullptr, 10));

    std::atomic<uint64_t> updates{0};
    std::vector<std::atomic<uint64_t>> sources(256);   // updates by source peer id
    std::vector<rant::RemoteVariable<rant::Bytes>> vars;
    rant::VariableOptions<rant::Bytes> vo;
    vo.reflect_from_mesh = true;
    for (size_t i = 0; i < want; ++i) {
        try {
            auto v = node->remote_variable<rant::Bytes>(names[i], vo);
            v.on_write([&updates, &sources](const rant::VariableUpdate& u) {
                ++updates;
                if (u.source() < sources.size()) ++sources[u.source()];
            });
            vars.push_back(std::move(v));
        } catch (const rant::Error& e) {
            std::fprintf(stderr, "could not create remote %s: %s\n", names[i].c_str(), e.what());
        }
    }
    std::printf("subscribed to %zu variables; running %d s\n", vars.size(), seconds);

    uint64_t lastUpdates = 0;
    for (int t = 1; t <= seconds; ++t) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        // A reliable send is what samples the round trip, and the explorer's periodic
        // @rant/meta request is one, so ask for the peer's snapshot once a second.
        mesh.meta_async(peerId, [](const rant::MetaSnapshot&) {}, metaSections);
        const uint64_t u = updates.load();
        for (const auto& p : mesh.peers()) {
            if (p.id != peerId) continue;
            std::printf("%3ds rtt %.2f ms (jitter %.2f, min %.2f, %u samples)  updates %llu/s\n",
                        t, p.rtt_us / 1000.0, p.rtt_jitter_us / 1000.0, p.rtt_min_us / 1000.0,
                        p.rtt_samples, static_cast<unsigned long long>(u - lastUpdates));
        }
        lastUpdates = u;
        std::fflush(stdout);
    }
    std::printf("updates by source peer id:");
    for (size_t i = 0; i < sources.size(); ++i)
        if (sources[i]) std::printf(" %zu:%llu", i, static_cast<unsigned long long>(sources[i].load()));
    std::printf("\n");
    vars.clear();
    return 0;
}
