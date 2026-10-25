#include "host/graph/SystemNodes.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <iterator>
#include <span>
#include <string>

#include "graph/node.hpp"
#include "host/graph/HostServices.hpp"
#include "system/SystemBase.hpp"
#include "system/mesen/MesenGbaSystem.hpp"
#include "system/mesen/MesenNesSystem.hpp"
#include "system/mesen/MesenSmsSystem.hpp"
#include "system/sameboy/SameBoySystem.hpp"

namespace rp::host {

// Named, so its JSON Schema $defs key is the same under every compiler.
struct SystemNodeSettings {
    // The system id buildCore parked this node's core under (docs/modular-graph-phase-b2-design.md section
    // 2.5). A new core is a new id, so it is always a new instance.
    std::uint64_t core = 0;
};

namespace {

constexpr std::size_t kMaxOutputs = 16;

// What every system node shares: the core handoff, the connected-output mask, and prepare / step / finish
// mapped one to one onto the core's block triad. `Self` supplies finish's lanes.
template <class Self, class Core>
struct SystemNode {
    using Settings = SystemNodeSettings;
    static bool needs_rebuild(const Settings& a, const Settings& b) { return a.core != b.core; }

    const HostCores*                       cores = nullptr;
    std::uint64_t                          token = 0;
    Core*                                  core = nullptr;  // owned
    std::array<std::uint8_t, kMaxOutputs>  connected{};

    SystemNode() = default;
    SystemNode(const SystemNode&) = delete;
    SystemNode& operator=(const SystemNode&) = delete;
    // Destroyed on the control thread, at reclaim once no plan runs it or as a refused plan is unwound: the
    // snapshot slot first, then the core, unless the host took the core back.
    ~SystemNode() {
        if (!core) return;
        if (cores && cores->released(cores->ctx, core)) return;
        delete core;
    }

    void bind_host(const rp_host* h) noexcept {
        cores = static_cast<const HostCores*>(h->get_interface(h, RP_HOST_CORES_INTERFACE));
    }
    void on_settings(const Settings& s) noexcept { token = s.core; }

    // The first activation takes the core; later ones (a re-activation) keep it. The factory activated the
    // core at the engine's rate when it built it, and Engine::setSampleRate re-rates live cores itself.
    bool activate(double, std::uint32_t) {
        if (core) return true;
        if (!cores) return false;
        SystemBase* built = cores->take(cores->ctx, token);
        core = dynamic_cast<Core*>(built);
        // An id naming another kind of core is a host bug (Engine::setGraph checks the kinds before it
        // plans). The engine keeps a snapshot slot for it, as for any core, or takes it back.
        if (!core && built && !cores->released(cores->ctx, built)) delete built;
        return core != nullptr;
    }

    void ports_changed(std::span<const std::uint8_t> mask) noexcept {
        connected.fill(0);
        std::copy_n(mask.begin(), std::min(mask.size(), connected.size()), connected.begin());
    }

    static AudioBlockInfo info(const rp::Block& b) {
        const rp_transport& t = b.transport();
        return AudioBlockInfo{b.frames(), t.sample_rate, t.tempo, t.ppq, (t.flags & RP_TRANSPORT_PLAYING) != 0};
    }

    // An unlinked system: prepare, step until it has the block, finish - a coupled group of one.
    void process(const rp::Block& b) noexcept {
        prepare(b);
        while (core->stepIfBelowTarget(b.frames())) {
        }
        finish(b);
    }
    void prepare(const rp::Block& b) noexcept { core->prepareForBlock(info(b)); }
    bool step(std::uint32_t target) noexcept { return !core->stepIfBelowTarget(target); }

    // finishBlock adds into its lanes, so they start at zero. Every other connected output is written too
    // (the ABI's rule), as silence.
    void finish(const rp::Block& b) noexcept {
        std::array<float*, 2 * kMaxOutputs> lanes{};
        const std::size_t n = static_cast<Self*>(this)->lanes(b, lanes);
        for (std::uint32_t o = 0; o < b.raw.output_count && o < connected.size(); ++o)
            if (connected[o])
                for (std::uint32_t c = 0; c < b.output(o).u.audio.channel_count; ++c)
                    std::memset(b.output(o).u.audio.channels[c], 0, b.frames() * sizeof(float));
        for (std::size_t l = 0; l < n; ++l) std::memset(lanes[l], 0, b.frames() * sizeof(float));
        core->finishBlock(info(b), lanes.data(), n);
    }

    // The stereo mix as lanes 0 and 1.
    static std::size_t mixLanes(const rp::Block& b, std::array<float*, 2 * kMaxOutputs>& lanes) {
        lanes[0] = b.output(0).u.audio.channels[0];
        lanes[1] = b.output(0).u.audio.channels[1];
        return 2;
    }
};

struct GameBoy : SystemNode<GameBoy, SameBoySystem> {
    static constexpr const char* id = "rp.sameboy.gb";
    static constexpr const char* name = "Game Boy";
    static constexpr const char* category = "system";
    struct { rp::link link; } inputs;
    struct {
        rp::audio_out<rp::stereo> mix;
        rp::audio_out<rp::stereo> ch1;
        rp::audio_out<rp::stereo> ch2;
        rp::audio_out<rp::stereo> ch3;
        rp::audio_out<rp::stereo> ch4;
    } outputs;

    // The four stems (8 lanes) once any is connected, else the mix (2): finishBlock's two lane counts.
    std::size_t lanes(const rp::Block& b, std::array<float*, 2 * kMaxOutputs>& out) const {
        if (!(connected[1] || connected[2] || connected[3] || connected[4])) return mixLanes(b, out);
        for (std::uint32_t k = 0; k < 4; ++k) {
            out[2 * k] = b.output(1 + k).u.audio.channels[0];
            out[2 * k + 1] = b.output(1 + k).u.audio.channels[1];
        }
        return 8;
    }

    // The coupled group's other members, in member order: the cable the serial callbacks read and write.
    // linkPeers_ reserves 8, so a cable of up to 9 Game Boys never allocates here.
    void set_peers(std::span<rp_node* const> peers) noexcept {
        core->linkPeers_.clear();
        for (rp_node* p : peers)
            if (GameBoy* gb = rp::peer_as<GameBoy>(p)) core->linkPeers_.push_back(gb->core);
    }
};

// Output order: mix, then the tap ports by the mode that drives them.
constexpr const char* kNesTaps[] = {"pins.pulse",  "pins.tnd",     "pins.expansion", "pins.mixref",   "channels.sq1",
                                    "channels.sq2", "channels.tri", "channels.noise", "channels.dmc"};
constexpr std::uint32_t kPinsFirst = 1;      // output index of pins.pulse
constexpr std::uint32_t kChannelsFirst = 5;  // output index of channels.sq1

struct Nes : SystemNode<Nes, MesenNesSystem> {
    static constexpr const char* id = "rp.mesen.nes";
    static constexpr const char* name = "NES";
    static constexpr const char* category = "system";
    struct { rp::audio_out<rp::stereo> mix; } outputs;

    static void adjust_layout(rp::graph::Layout& l, const Settings&) {
        using namespace rp::graph;
        for (const char* port : kNesTaps) l.ports.push_back({port, PortDir::Out, PortKind::Audio, ChannelSpec::fixed(1, "mono"), {}, false});
        l.exclusive.push_back({"pins.*", "channels.*"});
    }

    // With mix connected, the mix (2 lanes), whether or not the tap is armed: the narrow-host defect's
    // path. Otherwise the armed mode's streams, each mono stream's two lanes pointed at its one port
    // buffer (the NES writes only lane 2k).
    std::size_t lanes(const rp::Block& b, std::array<float*, 2 * kMaxOutputs>& out) const {
        bool tap = false;
        for (std::uint32_t o = kPinsFirst; o < 1 + std::size(kNesTaps); ++o) tap = tap || connected[o];
        const std::uint32_t mode = core->channelExportMode();
        const std::uint32_t streams = mode == 1 ? 3 : mode == 2 ? 4 : mode == 3 ? 5 : 0;
        if (connected[0] || !tap || streams == 0) return mixLanes(b, out);
        const std::uint32_t first = mode == 3 ? kChannelsFirst : kPinsFirst;
        for (std::uint32_t k = 0; k < streams; ++k) out[2 * k] = out[2 * k + 1] = b.output(first + k).u.audio.channels[0];
        return 2 * streams;
    }
};

struct Gba : SystemNode<Gba, MesenGbaSystem> {
    static constexpr const char* id = "rp.mesen.gba";
    static constexpr const char* name = "Game Boy Advance";
    static constexpr const char* category = "system";
    struct { rp::audio_out<rp::stereo> mix; } outputs;
    std::size_t lanes(const rp::Block& b, std::array<float*, 2 * kMaxOutputs>& out) const { return mixLanes(b, out); }
};

struct Sms : SystemNode<Sms, MesenSmsSystem> {
    static constexpr const char* id = "rp.mesen.sms";
    static constexpr const char* name = "Master System / Game Gear";
    static constexpr const char* category = "system";
    struct { rp::audio_out<rp::stereo> mix; } outputs;
    std::size_t lanes(const rp::Block& b, std::array<float*, 2 * kMaxOutputs>& out) const { return mixLanes(b, out); }
};

}  // namespace

const char* systemNodeType(SystemKind kind) {
    switch (kind) {
        case SystemKind::SameBoy: return GameBoy::id;
        case SystemKind::MesenNes: return Nes::id;
        case SystemKind::MesenGba: return Gba::id;
        case SystemKind::MesenSms: return Sms::id;
    }
    return "";
}

const rp_package* sameboyPackage() {
    static const rp_node_type* const kTypes[] = {rp::node_type<GameBoy>()};
    static const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.sameboy","version":"1.0.0"})",
                                     static_cast<std::uint32_t>(std::size(kTypes)), kTypes};
    return &kPackage;
}

const rp_package* mesenPackage() {
    static const rp_node_type* const kTypes[] = {rp::node_type<Nes>(), rp::node_type<Gba>(), rp::node_type<Sms>()};
    static const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.mesen","version":"1.0.0"})",
                                     static_cast<std::uint32_t>(std::size(kTypes)), kTypes};
    return &kPackage;
}

}  // namespace rp::host
