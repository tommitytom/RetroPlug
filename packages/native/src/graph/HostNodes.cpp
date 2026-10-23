#include "graph/HostNodes.hpp"

#include <array>
#include <cstring>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include "graph/GraphHost.hpp"
#include "graph/node.hpp"

namespace rp::graph {

// Named, so AudioOutSettings has the same $defs key under every compiler.
namespace host {

struct AudioOutSettings {
    std::vector<std::uint32_t> buses = {2, 2, 2, 2};
};

constexpr std::size_t   kMaxBuses = 32;
constexpr std::uint64_t kMaxHostChannels = 256;

struct AudioOut {
    static constexpr const char* id = "rp.host.audio-out";
    static constexpr const char* name = "Audio Out";
    static constexpr const char* category = "host";
    using Settings = AudioOutSettings;

    static std::optional<std::string> validate(const Settings& s) {
        if (s.buses.size() > kMaxBuses) return "at most " + std::to_string(kMaxBuses) + " buses";
        std::uint64_t total = 0;
        for (const auto n : s.buses) {
            if (n < 1 || n > kMaxChannels) return "every bus needs 1 to " + std::to_string(kMaxChannels) + " channels";
            total += n;
        }
        if (total > kMaxHostChannels) return "the buses add up to more than " + std::to_string(kMaxHostChannels) + " channels";
        return std::nullopt;
    }

    static void adjust_layout(Layout& l, const Settings& s) {
        std::uint32_t first = 1;
        for (std::size_t i = 0; i < s.buses.size() && i < kMaxBuses; ++i) {
            const std::uint32_t n = s.buses[i];
            PortDesc p;
            p.id = n == 1 ? "out." + std::to_string(first)
                          : "out." + std::to_string(first) + "-" + std::to_string(first + n - 1);
            p.dir = PortDir::In;
            p.kind = PortKind::Audio;
            p.channels = ChannelSpec::fixed(n, layoutForCount(n));
            l.ports.push_back(std::move(p));
            first += n;
        }
    }

    std::array<std::uint32_t, kMaxBuses> firstChannel{};
    std::uint32_t                        busCount = 0;
    const rp_host*                       host = nullptr;

    void bind_host(const rp_host* h) noexcept { host = h; }

    void on_settings(const Settings& s) noexcept {
        busCount = 0;
        std::uint32_t ch = 0;
        for (std::size_t i = 0; i < s.buses.size() && i < kMaxBuses; ++i) {
            firstChannel[busCount++] = ch;
            ch += s.buses[i];
        }
    }

    void process(const rp::Block& b) noexcept {
        const auto* gh = static_cast<const GraphHost*>(host->ctx);
        float* const* outs = gh->hostOutputs();
        const std::uint32_t outCount = gh->hostOutputCount();
        for (std::uint32_t i = 0; i < busCount && i < b.raw.input_count; ++i) {
            const rp_audio_view& in = b.input(i).u.audio;
            for (std::uint32_t c = 0; c < in.channel_count; ++c) {
                const std::uint32_t dst = firstChannel[i] + c;
                if (outs && dst < outCount) std::memcpy(outs[dst], in.channels[c], b.frames() * sizeof(float));
            }
        }
    }
};

}  // namespace host

const rp_package* hostPackage() {
    static const rp_node_type* const kTypes[] = {rp::node_type<host::AudioOut>()};
    static const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.host","version":"1.0.0"})",
                                     static_cast<std::uint32_t>(std::size(kTypes)), kTypes};
    return &kPackage;
}

}  // namespace rp::graph
