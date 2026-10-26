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
#include "transport/MidiTypes.hpp"

namespace rp::host {

// Named, so its JSON Schema $defs key is the same under every compiler.
struct SystemNodeSettings {
    // The system id buildCore parked this node's core under (docs/modular-graph-phase-b2-design.md section
    // 2.5). A new core is a new id, so it is always a new instance.
    std::uint64_t core = 0;
};

namespace {

constexpr std::size_t kMaxOutputs = 16;

// The inputs every system node has (docs/modular-graph-phase-b3a-design.md section 3.1), each mapped onto the core
// call SystemBase already has. A type declares them in its `inputs`, under these names.
//
//   serialIn  bytes   pushSerialIn(frame, byte), one call per byte
//   midi      events  onMidi; a message longer than a MidiEvent holds goes down pushCoreBytes, unframed
//   bytes     bytes   pushCoreBytes(frame, data, size, flush), the flush flag riding RP_MESSAGE_FLUSH
//   buttons   events  pressButton(button, down), from a 2-byte payload [button, down]

// What every system node shares: the core handoff, the connected-output mask, its inputs, and prepare / step /
// finish mapped one to one onto the core's block triad. `Self` supplies finish's lanes, and may write output
// queues after the core has run (writeQueues).
template <class Self, class Core>
struct SystemNode {
    using Settings = SystemNodeSettings;
    static bool needs_rebuild(const Settings& a, const Settings& b) { return a.core != b.core; }

    const HostCores*                       cores = nullptr;
    const EngineSyncTrace*                 trace = nullptr;  // null when the host keeps none
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
        trace = static_cast<const EngineSyncTrace*>(h->get_interface(h, RP_ENGINE_SYNC_TRACE_INTERFACE));
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
    // The inputs are applied here, where process starts too: a linked Game Boy's coupled group prepares every member
    // (rp.step/1) before any of them steps, and never calls process.
    void prepare(const rp::Block& b) noexcept {
        applyInputs();
        core->prepareForBlock(info(b));
    }
    bool step(std::uint32_t target) noexcept { return !core->stepIfBelowTarget(target); }

    // finishBlock adds into its lanes, so they start at zero. Every other connected audio output is written too
    // (the ABI's rule), as silence.
    void finish(const rp::Block& b) noexcept {
        std::array<float*, 2 * kMaxOutputs> lanes{};
        const std::size_t n = static_cast<Self*>(this)->lanes(b, lanes);
        for (std::uint32_t o = 0; o < b.raw.output_count && o < connected.size(); ++o)
            if (connected[o] && b.output(o).kind == RP_PORT_AUDIO)
                for (std::uint32_t c = 0; c < b.output(o).u.audio.channel_count; ++c)
                    std::memset(b.output(o).u.audio.channels[c], 0, b.frames() * sizeof(float));
        for (std::size_t l = 0; l < n; ++l) std::memset(lanes[l], 0, b.frames() * sizeof(float));
        core->finishBlock(info(b), lanes.data(), n);
        static_cast<Self*>(this)->writeQueues(b.frames());
    }
    void writeQueues(std::uint32_t) noexcept {}

    // Each input in queue order, the inputs in the order the kernel stage the kernel node replaced fanned its sinks
    // out: serialIn, midi, bytes, buttons. The order matters on the NES, where MIDI and bytes land in the same FIFO
    // (a flushed byte message drops the MIDI queued ahead of it). Before prepareForBlock, as that stage ran before
    // the core.
    void applyInputs() noexcept {
        auto& in = static_cast<Self*>(this)->inputs;
        for (const rp_message& m : in.serialIn.messages())
            for (const std::uint8_t byte : in.serialIn.data(m)) core->pushSerialIn(m.frame, byte);
        for (const rp_message& m : in.midi.messages()) {
            const auto data = in.midi.data(m);
            if (data.empty()) continue;
            // Longer than a MidiEvent holds (a SysEx, or several messages staged as one run): the core's raw byte
            // path, unframed and uncapped. A core with no byte device (SameBoy's mGB speaks 3-byte messages only)
            // ignores it, which also keeps a dangling MidiEvent::dataExt out of SameBoy's queued copy.
            if (data.size() > ::MidiEvent::kDataSize) {
                core->pushCoreBytes(m.frame, data.data(), data.size(), false);
                continue;
            }
            ::MidiEvent ev{};
            ev.frame = m.frame;
            ev.size = static_cast<std::uint32_t>(data.size());
            for (std::size_t j = 0; j < data.size(); ++j) ev.data[j] = data[j];
            core->onMidi(&ev, 1);
        }
        for (const rp_message& m : in.bytes.messages()) {
            const auto data = in.bytes.data(m);
            if (data.empty()) continue;
            if (trace) trace->bytes(trace->ctx, m.frame, data.data(), data.size());
            core->pushCoreBytes(m.frame, data.data(), data.size(), (m.flags & RP_MESSAGE_FLUSH) != 0);
        }
        for (const rp_message& m : in.buttons.messages()) {
            const auto data = in.buttons.data(m);
            if (data.size() >= 2) core->pressButton(data[0], data[1] != 0);
        }
    }

    // The stereo mix as lanes 0 and 1.
    static std::size_t mixLanes(const rp::Block& b, std::array<float*, 2 * kMaxOutputs>& lanes) {
        lanes[0] = b.output(0).u.audio.channels[0];
        lanes[1] = b.output(0).u.audio.channels[1];
        return 2;
    }
};

// The Game Boy's serialOut: a byte a message. While MI.OUT capture clocks the port (external clock, SC=0x80:
// SameBoySystem::writeAudioSample shifts one bit an audio frame) a byte takes eight frames, so the largest block a
// host can run (rp::graph::kMaxBlockFrames, 65536 frames; the plugin grows its plan to its buffer size) carries
// 8192. The other 64 are slack for what a block adds to that: up to 7 bits carried in from the block before, and
// the frames the core runs past its target (an instruction at a time, so rarely more than one; 64 bytes is 512
// frames of it). Fixed rather than sized from the plan's block size, so a feedback wire from it keeps its front
// across every plan swap. A Game Boy that clocks the port itself (internal clock, SC=0x81) shifts at its serial
// rate instead, and a CGB on its fast clock (32768 bytes a second, about 0.7 a frame at 48 kHz, twice that in
// double speed) can outrun this in a block past about 12000 frames (about 6000 in double speed): what does not
// fit is dropped and counted (GraphHost::droppedMessages), never lost silently.
constexpr rp::capacity kSerialOutCapacity{.messages = 8256, .bytes = 8256};

struct GameBoy : SystemNode<GameBoy, SameBoySystem> {
    static constexpr const char* id = "rp.sameboy.gb";
    static constexpr const char* name = "Game Boy";
    static constexpr const char* category = "system";
    struct {
        rp::link      link;
        rp::bytes_in  serialIn;
        rp::events_in midi;
        rp::bytes_in  bytes;
        rp::events_in buttons;
    } inputs;
    struct {
        rp::audio_out<rp::stereo>                mix;
        rp::audio_out<rp::stereo>                ch1;
        rp::audio_out<rp::stereo>                ch2;
        rp::audio_out<rp::stereo>                ch3;
        rp::audio_out<rp::stereo>                ch4;
        rp::bytes_out::sized<kSerialOutCapacity> serialOut;
    } outputs;
    static constexpr std::uint32_t kSerialOut = 5;  // its output index

    // The four stems (8 lanes) once any is connected, else the mix (2): finishBlock's two lane counts.
    std::size_t lanes(const rp::Block& b, std::array<float*, 2 * kMaxOutputs>& out) const {
        if (!(connected[1] || connected[2] || connected[3] || connected[4])) return mixLanes(b, out);
        for (std::uint32_t k = 0; k < 4; ++k) {
            out[2 * k] = b.output(1 + k).u.audio.channels[0];
            out[2 * k + 1] = b.output(1 + k).u.audio.channels[1];
        }
        return 8;
    }

    // MI.OUT (design section 3.2): the bytes the core captured on its serial port this block, each at the frame the core
    // completed it, which master sync's decoder times its stop by (docs/lsdj-master-sync-fix-design.md). The core runs an
    // instruction at a time, so it can complete a byte past the block's end; that byte is stamped at the block's last
    // frame, so a block's messages always lie inside it. Copied, not drained: the core clears the log at its next
    // prepareForBlock.
    void writeQueues(std::uint32_t frames) noexcept {
        if (!connected[kSerialOut]) return;
        for (const auto& entry : core->serialOutLog_)
            outputs.serialOut.push(std::min(entry.first, frames - 1), std::span<const std::uint8_t>(&entry.second, 1));
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
    struct {
        rp::bytes_in  serialIn;
        rp::events_in midi;
        rp::bytes_in  bytes;
        rp::events_in buttons;
    } inputs;
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
    struct {
        rp::bytes_in  serialIn;
        rp::events_in midi;
        rp::bytes_in  bytes;
        rp::events_in buttons;
    } inputs;
    struct { rp::audio_out<rp::stereo> mix; } outputs;
    std::size_t lanes(const rp::Block& b, std::array<float*, 2 * kMaxOutputs>& out) const { return mixLanes(b, out); }
};

struct Sms : SystemNode<Sms, MesenSmsSystem> {
    static constexpr const char* id = "rp.mesen.sms";
    static constexpr const char* name = "Master System / Game Gear";
    static constexpr const char* category = "system";
    struct {
        rp::bytes_in  serialIn;
        rp::events_in midi;
        rp::bytes_in  bytes;
        rp::events_in buttons;
    } inputs;
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
