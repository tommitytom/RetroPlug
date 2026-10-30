#include "host/graph/SystemNodes.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iterator>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include <rfl/Literal.hpp>
#include <rfl/Result.hpp>
#include <rfl/Validator.hpp>
#include <rfl/enums.hpp>
#include <rfl/parsing/schema/ValidationType.hpp>

#include "graph/CanonicalJson.hpp"
#include "graph/node.hpp"
#include "host/graph/HostServices.hpp"
#include "system/SystemBase.hpp"
#include "system/mesen/MesenGbaSystem.hpp"
#include "system/mesen/MesenNesConfig.hpp"
#include "system/mesen/MesenNesSystem.hpp"
#include "system/mesen/MesenSmsSystem.hpp"
#include "system/sameboy/SameBoyConfig.hpp"
#include "system/sameboy/SameBoySystem.hpp"
#include "transport/MidiTypes.hpp"

namespace rp::host {

// ---- settings ---------------------------------------------------------------------------------------------------

// The knobs' value types. An enum knob is a label as the role configs spell it (packages/retroplug/src/settingsEnums.ts),
// which is the name of its C++ enumerator with the first letter lowercased: SameBoyModel::CgbC is "cgbC",
// SameBoyHighpass::RemoveDcOffset "removeDcOffset", SameBoyModel::Auto "auto". So the enumerators are native's one
// spelling of every label: Label<E> is the rfl::Literal made of them, which reads exactly those strings (anything else
// refuses the settings), lists them in the descriptor's schema, and maps back to its enumerator by position (valueOf).
// Never an ordinal: the document and a saved project carry the same word (docs/modular-graph-phase-c2-design.md
// section 3).
namespace knobs {

template <class E, std::size_t I>
constexpr auto labelAt() {
    constexpr std::string_view name = rfl::get_enumerator_array<E>()[I].first;
    std::array<char, name.size() + 1> chars{};
    for (std::size_t k = 0; k < name.size(); ++k) chars[k] = name[k];
    if (chars[0] >= 'A' && chars[0] <= 'Z') chars[0] = static_cast<char>(chars[0] - 'A' + 'a');
    return rfl::internal::StringLiteral<name.size() + 1>(chars);
}

template <class E, std::size_t... I>
rfl::Literal<labelAt<E, I>()...> literalOf(std::index_sequence<I...>);

// A number refused outside [R::min, R::max], as a settings value out of its range is refused anywhere (a document is
// checked, never quietly changed; the store clamps every knob to the same range before it publishes), with the range
// in the descriptor's schema. The bounds ride a type rather than template values: a floating-point template argument
// is beyond some of the compilers the build supports.
template <class R>
struct Within {
    template <class T>
    static rfl::Result<T> validate(T value) noexcept {
        if (value >= R::min && value <= R::max) return value;  // NaN is refused too
        return rfl::error("Value expected to be between " + rp::graph::jsNumber(R::min) + " and " +
                          rp::graph::jsNumber(R::max) + ", but got " + rp::graph::jsNumber(value) + ".");
    }
    template <class T>
    static rfl::parsing::schema::ValidationType to_schema() {
        using V = rfl::parsing::schema::ValidationType;
        return V{V::AllOf{.types_ = {V{V::Minimum{.value_ = rfl::Variant<double, int>(R::min)}},
                                     V{V::Maximum{.value_ = rfl::Variant<double, int>(R::max)}}}}};
    }
};

struct LightTemperatureRange {
    static constexpr double min = -1.0, max = 1.0;  // SameBoy's own: applyDisplayConfig clamps to it
};
struct ApuLatencyRange {
    static constexpr double min = 0.25, max = 6.0;  // milliseconds
};

template <class E>
using Label = decltype(literalOf<E>(std::make_index_sequence<rfl::get_enumerator_array<E>().size()>{}));

template <class E>
E valueOf(const Label<E>& label) {
    return rfl::get_enumerator_array<E>()[label.value()].second;
}

using LightTemperature = rfl::Validator<double, Within<LightTemperatureRange>>;
using ApuLatencyMs = rfl::Validator<double, Within<ApuLatencyRange>>;

}  // namespace knobs

// Each type's settings, named so their JSON Schema $defs keys are the same under every compiler. `core` is the system
// id buildCore parked the node's core under (docs/modular-graph-phase-b2-design.md section 2.5): a new core is a new
// id, so it is always a new instance (rebuild_key). Every other field is a knob of that core
// (docs/modular-graph-phase-c2-design.md section 3), and optional: absent, the node leaves the core as it was built,
// or as the last document that carried the field left it.
struct SystemNodeSettings {  // rp.mesen.gba, rp.mesen.sms
    std::uint64_t core = 0;
};

struct GameBoySettings {
    std::uint64_t                                       core = 0;
    std::optional<knobs::Label<SameBoyModel>>           model;
    std::optional<bool>                                 fastBoot;
    std::optional<knobs::Label<SameBoyHighpass>>        highpass;
    std::optional<knobs::Label<SameBoyColorCorrection>> colorCorrection;
    std::optional<knobs::Label<SameBoyDmgPalette>>      dmgPalette;
    std::optional<knobs::LightTemperature>              lightTemperature;
    // What the core captures of its serial port for LSDj's decoder: "listen" for master sync, "drive" for MI.OUT.
    std::optional<knobs::Label<SerialOutCapture>>       serialOutCapture;
};

struct NesSettings {
    std::uint64_t                                  core = 0;
    std::optional<knobs::Label<NesRegion>>         region;
    std::optional<bool>                            removeSpriteLimit;
    std::optional<knobs::ApuLatencyMs>             apuLatencyMs;
    std::optional<knobs::Label<CartridgeAccuracy>> s5bNoise;
    std::optional<knobs::Label<CartridgeAccuracy>> mmc5PhaseReset;
};

namespace {

constexpr std::size_t kMaxOutputs = 16;

// The params (docs/modular-graph-phase-c2-design.md section 3): what is worth playing, automating or modulating, so
// each is also a control input, its value in the document and its live edits through Engine::setParam. Every system
// node has `gain`, in dB over the range the store clamps a gain to (systemSettings.ts GAIN_MIN / GAIN_MAX); a NES has
// `expansionVolume` too, its cartridge sound chip's level in whole percent of unity, as its core takes it. Each range is
// one constant, used by the param's declaration and by the node's own clamp of a wire-driven value.
constexpr double kGainMinDb = -90.0;
constexpr double kGainMaxDb = 12.0;
constexpr int    kExpansionVolumeMin = 0;
constexpr int    kExpansionVolumeMax = 200;
using GainParam = rp::float_param<"Gain", rp::range{kGainMinDb, kGainMaxDb, 0}, "dB">;
using ExpansionVolumeParam =
    rp::int_param<"Expansion volume", rp::range{kExpansionVolumeMin, kExpansionVolumeMax, 100}, "%">;

// What a param's last-applied value holds before the core has been given any: no value equals it, NaN included.
constexpr double kUnapplied = std::numeric_limits<double>::quiet_NaN();

// The inputs every system node has (docs/modular-graph-phase-b3a-design.md section 3.1), each mapped onto the core
// call SystemBase already has. A type declares them in its `inputs`, under these names.
//
//   serialIn  bytes   pushSerialIn(frame, byte), one call per byte
//   midi      events  onMidi; a message longer than a MidiEvent holds goes down pushCoreBytes, unframed
//   bytes     bytes   pushCoreBytes(frame, data, size, flush), the flush flag riding RP_MESSAGE_FLUSH
//   buttons   events  pressButton(button, down), from a 2-byte payload [button, down]

// Whether a knob is to be applied: present in `to`, and absent from `from`, what was applied last, or another value
// there. A knob absent from `to` is never applied, so a field a document drops leaves the core as it is.
inline bool same(bool a, bool b) { return a == b; }
template <rfl::internal::StringLiteral... L>
bool same(const rfl::Literal<L...>& a, const rfl::Literal<L...>& b) { return a.value() == b.value(); }
template <class T, class... V>
bool same(const rfl::Validator<T, V...>& a, const rfl::Validator<T, V...>& b) { return a.value() == b.value(); }
template <class F>
bool moved(const std::optional<F>& to, const std::optional<F>& from) { return to && !(from && same(*from, *to)); }

// What every system node shares: the core handoff, its settings and params reaching the core, the connected-output
// mask, its inputs, and prepare / step / finish mapped one to one onto the core's block triad. `Self` declares its
// params (`gain` at least), supplies finish's lanes, may write output queues after the core has run (writeQueues),
// and applies its own knobs (apply) and any param beyond the gain (applyParams).
//
// Only a new core rebuilds (rebuild_key): every other setting is a knob the node applies to the core it has, so its
// prepare reads nothing a commit writes and setGraph never waits on a system node's commit (stateless_prepare). A
// commit only points `settings` at the new settings (on_settings never allocates); the node applies what moved at the
// start of its next block, before the core's prepareForBlock, on the block thread as the command ring's knobs were,
// and everything present once at activation, as it takes its core, before that core first runs. Each knob is
// value-guarded by the core's own setter, so a palette change re-applies the display group and restarts nothing, while
// a model change restarts the core and a region change resets it (docs/modular-graph-phase-c2-design.md section 4).
//
// A param's value reaches the core at the start of each block whose control stream moves it, before the core's
// prepareForBlock and after the settings (a model's restart first, then the levels), each value-guarded against the
// value the core was given last: a publish carrying the value the core already has calls nothing. Every one is given
// at the node's first block, before the core's first prepareForBlock: a node reads its params only in a block (the
// host binds its control streams there, and lands the document's values in them as it adopts the plan), and a core a
// node has taken runs nothing before that, so it plays its first sample at the document's level. The gain is smoothed
// by the core, as every gain edit is.
template <class Self, class Core, class S = SystemNodeSettings>
struct SystemNode {
    using Settings = S;
    static std::uint64_t rebuild_key(const Settings& s) { return s.core; }
    static constexpr bool stateless_prepare = true;

    const HostCores*                       cores = nullptr;
    const EngineSyncTrace*                 trace = nullptr;  // null when the host keeps none
    std::uint64_t                          token = 0;
    Core*                                  core = nullptr;  // owned
    std::array<std::uint8_t, kMaxOutputs>  connected{};
    const Settings*                        settings = nullptr;  // the committed ones, alive until the next commit
    Settings                               applied{};           // what the core was given last (none before activation)
    bool                                   pending = false;     // settings committed and not yet applied
    double                                 gainApplied = kUnapplied;  // the gain param's value the core has, in dB

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
    void on_settings(const Settings& s) noexcept {
        settings = &s;
        token = s.core;
        pending = true;
    }

    // The first activation takes the core and gives it every knob the settings carry; later ones (a re-activation)
    // keep it. The factory activated the core at the engine's rate when it built it, and Engine::setSampleRate re-rates
    // live cores itself.
    bool activate(double, std::uint32_t) {
        if (core) return true;
        if (!cores) return false;
        SystemBase* built = cores->take(cores->ctx, token);
        core = dynamic_cast<Core*>(built);
        // An id naming another kind of core is a host bug (Engine::setGraph checks the kinds before it
        // plans). The engine keeps a snapshot slot for it, as for any core, or takes it back.
        if (!core && built && !cores->released(cores->ctx, built)) delete built;
        if (!core) return false;
        applySettings();  // against `applied`, which holds none yet, so every knob present
        return true;
    }

    void applySettings() noexcept {
        static_cast<Self*>(this)->apply(*settings, applied);
        applied = *settings;
        pending = false;
    }
    // A core with no knobs of its own (the GBA, the SMS).
    void apply(const Settings&, const Settings&) noexcept {}

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
    // The knobs that moved, the params that moved, then the inputs, are applied here, where process starts too: a
    // linked Game Boy's coupled group prepares every member (rp.step/1) before any of them steps, and never calls
    // process. Knobs first, as the command ring's were drained before the block ran: a model change restarts the core
    // before its inputs reach it.
    void prepare(const rp::Block& b) noexcept {
        if (pending) applySettings();
        static_cast<Self*>(this)->applyParams();
        applyInputs();
        core->prepareForBlock(info(b));
    }

    // Each param's latest value this block, where it differs from what the core has: a change anywhere in the block
    // lands at its start. This one for a type whose one param is its gain. A value a wire carries is clamped to the
    // param's range here, as the planner and setParam clamp a document's or a live edit's.
    void applyParams() noexcept { applyGain(); }
    void applyGain() noexcept {
        const double latest = static_cast<Self*>(this)->params.gain.latest();
        if (latest != latest) return;  // NaN reaches a param only over a wire, and moves nothing
        const double db = std::clamp(latest, kGainMinDb, kGainMaxDb);
        if (db == gainApplied) return;
        core->setGainDb(static_cast<float>(db));
        gainApplied = db;
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
// frames of it). Fixed rather than sized from the plan's block size, so a feedback wire from it would keep its
// front across every plan swap (no document places one). A Game Boy that clocks the port itself (internal clock,
// SC=0x81) shifts at its serial rate instead, and a CGB on its fast clock (32768 bytes a second, about 0.7 a frame
// at 48 kHz, twice that in double speed) can outrun this in a block past about 12000 frames (about 6000 in double
// speed): what does not fit is dropped and counted (GraphHost::droppedMessages), never lost silently.
constexpr rp::capacity kSerialOutCapacity{.messages = 8256, .bytes = 8256};

struct GameBoy : SystemNode<GameBoy, SameBoySystem, GameBoySettings> {
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
    struct { GainParam gain; } params;
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

    // MI.OUT and master sync (section 3.2 of docs/modular-graph-phase-b3a-design.md): the bytes the core captured on
    // its serial port this block, each at the frame the core completed it: rp.lsdj.serial-out sends each message it
    // decodes at its completing byte's frame (docs/lsdj-serial-out-design.md), and times master sync's stop from the
    // newest byte's (docs/lsdj-master-sync-fix-design.md). A stamp can lie at or past the block's end two ways: the
    // core runs an instruction at a time, so it can complete a byte past the block's end, and the drive path counts the
    // frame before it captures (SameBoySystem::writeAudioSample), so its stamps run 1..frames and a byte completed in
    // the block's last sample is stamped `frames`. Either is stamped at the block's last frame, so a block's messages
    // always lie inside it. Copied, not drained: the core clears the log at its next prepareForBlock.
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

    // The fast boot first, which waits for the next restart and so is what a model's restart boots with: the edits
    // that land in one block (several documents published between two blocks commit together) restart the core as if
    // made one at a time, a fast boot edit before a model edit included. Then the rest, in the order the store's role
    // config reached the core through the command ring, the capture last. The capture lands at the start of the first
    // block of the plan that carries it, the block rp.lsdj.serial-out first runs in its new mode, so no byte captured
    // in one mode is decoded in the other.
    void apply(const Settings& to, const Settings& from) noexcept {
        if (moved(to.fastBoot, from.fastBoot)) core->setFastBoot(*to.fastBoot);
        if (moved(to.model, from.model)) core->setModel(knobs::valueOf<SameBoyModel>(*to.model));
        if (moved(to.highpass, from.highpass)) core->setHighpass(knobs::valueOf<SameBoyHighpass>(*to.highpass));
        if (moved(to.colorCorrection, from.colorCorrection))
            core->setColorCorrection(knobs::valueOf<SameBoyColorCorrection>(*to.colorCorrection));
        if (moved(to.dmgPalette, from.dmgPalette))
            core->setDmgPalette(knobs::valueOf<SameBoyDmgPalette>(*to.dmgPalette));
        if (moved(to.lightTemperature, from.lightTemperature)) core->setLightTemperature(to.lightTemperature->value());
        if (moved(to.serialOutCapture, from.serialOutCapture))
            core->setSerialOutCapture(knobs::valueOf<SerialOutCapture>(*to.serialOutCapture));
    }
};

// Output order: mix, then the tap ports by the mode that drives them.
constexpr const char* kNesTaps[] = {"pins.pulse",  "pins.tnd",     "pins.expansion", "pins.mixref",   "channels.sq1",
                                    "channels.sq2", "channels.tri", "channels.noise", "channels.dmc"};
constexpr std::uint32_t kPinsFirst = 1;      // output index of pins.pulse
constexpr std::uint32_t kChannelsFirst = 5;  // output index of channels.sq1

struct Nes : SystemNode<Nes, MesenNesSystem, NesSettings> {
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
    struct {
        GainParam            gain;
        ExpansionVolumeParam expansionVolume;
    } params;
    double expansionApplied = kUnapplied;  // the expansionVolume param's value the core has

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

    // In the order the store's role config reached the core through the command ring. A region change resets the
    // core (setRegion); the rest apply in place.
    void apply(const Settings& to, const Settings& from) noexcept {
        if (moved(to.region, from.region))
            core->setRegion(static_cast<std::uint32_t>(knobs::valueOf<NesRegion>(*to.region)));
        if (moved(to.removeSpriteLimit, from.removeSpriteLimit)) core->setRemoveSpriteLimit(*to.removeSpriteLimit);
        if (moved(to.apuLatencyMs, from.apuLatencyMs)) core->setApuLatencyMs(to.apuLatencyMs->value());
        if (moved(to.s5bNoise, from.s5bNoise))
            core->setS5bNoise(static_cast<std::uint32_t>(knobs::valueOf<CartridgeAccuracy>(*to.s5bNoise)));
        if (moved(to.mmc5PhaseReset, from.mmc5PhaseReset))
            core->setMmc5PhaseReset(static_cast<std::uint32_t>(knobs::valueOf<CartridgeAccuracy>(*to.mmc5PhaseReset)));
    }

    // The gain, then the expansion volume, which the core's mixer re-reads each buffer: whole percent, as the core
    // takes it (the planner and setParam round an int param's value; a wire's is clamped and rounded here).
    void applyParams() noexcept {
        applyGain();
        const double latest = params.expansionVolume.latest();
        if (latest != latest) return;
        const double percent = std::lround(std::clamp(latest, static_cast<double>(kExpansionVolumeMin),
                                                     static_cast<double>(kExpansionVolumeMax)));
        if (percent == expansionApplied) return;
        core->setExpansionVolume(static_cast<std::uint32_t>(percent));
        expansionApplied = percent;
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
    struct { GainParam gain; } params;
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
    struct { GainParam gain; } params;
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
