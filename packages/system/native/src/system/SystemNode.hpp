#pragma once

// The generic system node (docs/modular-graph-phase-f2-design.md section 2): what every console's node shares, and the
// knob helpers its settings are written with. A console's package keeps its settings type and the settings themselves;
// this header keeps the part that is no console's - the load of the core through the host's builder, the settings and
// params that reach it, the connected-output mask, the node's inputs, and prepare / step / finish mapped one to one
// onto the core's block triad.
//
// It is a header because a console's package is where its node type lives, and every one of them instantiates this
// template from its own translation unit. It depends on node-sdk (graph/node.hpp for the authoring layer and
// graph/rp_stage.h for the staged message), on the system contract it is part of (SystemBase, the build request, the
// host's services) and on nothing else.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <rfl/Literal.hpp>
#include <rfl/Result.hpp>
#include <rfl/Validator.hpp>
#include <rfl/enums.hpp>
#include <rfl/parsing/schema/ValidationType.hpp>

#include "graph/CanonicalJson.hpp"
#include "graph/node.hpp"
#include "graph/rp_stage.h"
#include "system/HostSystems.hpp"
#include "system/SystemBase.hpp"
#include "system/SystemBuild.hpp"
#include "transport/MidiTypes.hpp"

namespace rp::host {

// The knobs' value types. An enum knob is a label as the role configs spell it
// (packages/sameboy/ts/enums.ts, packages/mesen/ts/enums.ts), which is the name of its C++ enumerator with the first
// letter lowercased:
// SameBoyModel::CgbC is "cgbC", SameBoyHighpass::RemoveDcOffset "removeDcOffset", SameBoyModel::Auto "auto". So the
// enumerators are native's one spelling of every label: Label<E> is the rfl::Literal made of them, which reads exactly
// those strings (anything else refuses the settings), lists them in the descriptor's schema, and maps back to its
// enumerator by position (valueOf). Never an ordinal: the document and a saved project carry the same word
// (docs/modular-graph-phase-c2-design.md section 3). labelAt and same's Literal overloads name
// rfl::internal::StringLiteral, an internal reflect-cpp type, so a reflect-cpp bump is to check them.

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

template <class E>
using Label = decltype(literalOf<E>(std::make_index_sequence<rfl::get_enumerator_array<E>().size()>{}));

template <class E>
E valueOf(const Label<E>& label) {
    return rfl::get_enumerator_array<E>()[label.value()].second;
}

// Whether a knob is to be applied: present in `to`, and absent from `from`, what was applied last, or another value
// there. A knob absent from `to` is never applied, so a field a document drops leaves the core as it is.
inline bool same(bool a, bool b) { return a == b; }
template <rfl::internal::StringLiteral... L>
bool same(const rfl::Literal<L...>& a, const rfl::Literal<L...>& b) { return a.value() == b.value(); }
template <class T, class... V>
bool same(const rfl::Validator<T, V...>& a, const rfl::Validator<T, V...>& b) { return a.value() == b.value(); }
template <class F>
bool moved(const std::optional<F>& to, const std::optional<F>& from) { return to && !(from && same(*from, *to)); }

}  // namespace knobs

// The output lanes a plan can wire to one node: the stack array prepare() and finish() collect them in is this wide, so
// no node type's port count (the Game Boy's four stems, a NES's five channels) passes it.
inline constexpr std::size_t kMaxOutputs = 16;

// The params (docs/modular-graph-phase-c2-design.md section 3): what is worth playing, automating or modulating, so
// each is also a control input, its value in the document and its live edits through Engine::setParam. Every system
// node has `gain`, in dB over the range the store clamps a gain to (systemSettings.ts GAIN_MIN / GAIN_MAX); a console
// with a param beyond it declares that one beside its own type (the NES's expansion volume). The range is one constant,
// used by the param's declaration and by the node's own clamp of a wire-driven value.
inline constexpr double kGainMinDb = -90.0;
inline constexpr double kGainMaxDb = 12.0;
using GainParam = rp::float_param<"Gain", rp::range{kGainMinDb, kGainMaxDb, 0}, "dB">;

// What a param's last-applied value holds before the core has been given any: no value equals it, NaN included.
inline constexpr double kUnapplied = std::numeric_limits<double>::quiet_NaN();

// The inputs every system node has (docs/modular-graph-phase-b3a-design.md section 3.1), each mapped onto the core
// call SystemBase already has. A type declares them in its `inputs`, under these names.
//
//   serialIn  bytes   pushSerialIn(frame, byte), one call per byte
//   midi      events  onMidi; a message longer than a MidiEvent holds goes down pushCoreBytes, unframed
//   bytes     bytes   pushCoreBytes(frame, data, size, flush), the flush flag riding RP_MESSAGE_FLUSH
//   buttons   events  pressButton(button, down), from a 2-byte payload [button, down]

// What every system node shares: the core it builds, its settings and params reaching that core, the connected-output
// mask, its inputs, and prepare / step / finish mapped one to one onto the core's block triad. `Self` declares its
// params (`gain` at least), supplies finish's lanes, may write output queues after the core has run (writeQueues), and
// applies its own knobs (apply) and any param beyond the gain (applyParams).
//
// A node builds its core at activation from the bytes its resources hold (rp.host.systems/2,
// docs/modular-graph-phase-c3-design.md section 5). The core is the host's: the snapshot slot under the system id, the
// live cores, and the delete when the node is destroyed. Which fields a build reads is rebuild_key's, the
// construct-time fields among them. The build itself is the node's own package's (docs/modular-graph-phase-f2-design.md
// section 3): `Self::kBuild` is the function the request carries and the config `Self::config` makes is its subject, so
// the host that runs it knows no console, and the cast below has no console to disagree with.
//
// Only a new core rebuilds (rebuild_key): every other setting is a knob the node applies to the core it has, so its
// prepare reads nothing a commit writes and setGraph never waits on a system node's commit (stateless_prepare). A
// commit only points `settings` at the new settings (on_settings never allocates); the node applies what moved at the
// start of its next block, before the core's prepareForBlock, and everything present once at activation, as it takes
// or builds its core, before that core first runs. What the core was given is `applied`, a Settings::Applied, which is
// the live knobs alone: the fields carrying a string (romName, sdRoot, the resource refs) all rebuild the node or move
// a resource, which no application sees, so the copy the node makes when it applies a commit, on the block thread,
// copies no string. So a setting lands at the node's next prepare, after every host command queued behind its plan
// (the host drains its ring before it runs a block): a caller that must act on the restarted core renders a block
// first, since a RAM write or staged core bytes sent after a model change reach the core it is about to replace. Each
// knob is guarded twice, first by `moved` against what this node last applied, so a document's unchanged field is not
// re-applied even where the core was changed by another path, then by the core's own setter where it has one
// (setFastBoot and a NES's setRemoveSpriteLimit write unconditionally). A palette change re-applies the display group
// and restarts nothing, while a model change restarts the core and a region change resets it
// (docs/modular-graph-phase-c2-design.md section 4). A settings field the type does not know is ignored (node.hpp
// reads with rfl::DefaultIfMissing and no rfl::NoExtraFields), so a misspelt knob plans and does nothing.
//
// A param's value reaches the core at the start of each block whose control stream moves it, before the core's
// prepareForBlock and after the settings (a model's restart first, then the levels), each value-guarded against the
// value the core was given last: a publish carrying the value the core already has calls nothing. Every one is given
// at the node's first block, before the core's first prepareForBlock: a node reads its params only in a block (the
// host binds its control streams there, and lands the document's values in them as it adopts the plan), and a core a
// node has built runs nothing before that. The first value is snapped into the core, whose gain smoother
// starts at 0 dB, so the node plays its first sample at the document's level; every later gain change is smoothed by
// the core, as every gain edit is.
template <class Self, class Core, class S>
struct SystemNode {
    using Settings = S;
    // What the core was given (the live knobs alone, per Settings::Applied): trivially copyable, since the block thread
    // writes it, so a string-valued live knob would have to be added here deliberately rather than ride a copy of the
    // whole settings.
    using Applied = typename Settings::Applied;
    static_assert(std::is_trivially_copyable_v<Applied>,
                  "a settings application must not allocate on the block thread");
    static auto rebuild_key(const Settings& s) { return s.key(); }
    static constexpr bool stateless_prepare = true;

    const HostSystems*                     systems = nullptr;  // null when the host offers no builder
    const EngineSyncTrace*                 trace = nullptr;  // null when the host keeps none
    Core*                                  core = nullptr;  // owned
    std::array<std::uint8_t, kMaxOutputs>  connected{};
    const Settings*                        settings = nullptr;  // the committed ones, alive until the next commit
    Applied                                applied{};           // the live knobs the core was given last (none before)
    bool                                   pending = false;     // settings committed and not yet applied
    double                                 gainApplied = kUnapplied;  // the gain param's value the core has, in dB

    SystemNode() = default;
    SystemNode(const SystemNode&) = delete;
    SystemNode& operator=(const SystemNode&) = delete;
    // Destroyed on the control thread, at reclaim once no plan runs it or as a refused plan is unwound. The core it
    // built is the host's to release (its snapshot slot, and the live-core map, go with it); a node that never built
    // one has no core, and build throws before setting it when the host offers no systems service.
    ~SystemNode() {
        if (core) systems->destroy(systems->ctx, core);
    }

    void bind_host(const rp_host* h) noexcept {
        systems = static_cast<const HostSystems*>(h->get_interface(h, RP_HOST_SYSTEMS_INTERFACE));
        trace = static_cast<const EngineSyncTrace*>(h->get_interface(h, RP_ENGINE_SYNC_TRACE_INTERFACE));
    }

    // rp.stage/1: a message a host aimed at this node, straight into its core's ingress (Engine::stageNodeMidi's
    // path). The UI's knob traffic reaches the core whatever the project's musical routing does, and to the node
    // whose menu it came from. Frame 0: a knob has no musical position.
    rp_host_stage staged{this, &stageFn};
    static bool stageFn(void* ctx, const std::uint8_t* data, std::size_t size, double) {
        return static_cast<SystemNode*>(ctx)->stageMessage(data, size);
    }
    // pushCoreBytes, not onMidi, where the core has a raw ingress: onMidi is the MIDI-FRAMED door, and the NES role's
    // implementation clamps every event to MidiEvent::kDataSize and ignores dataExt (NesN8FifoRole.cpp), so a SysEx
    // through it arrives as its first four bytes and the cart waits forever for an F7 that was thrown away.
    // pushCoreBytes is the raw door a protocol node's bytes reach the core through, with no framing and no cap. A core
    // with no raw ingress falls back to the framed one, where a channel message still fits; nothing longer goes.
    bool stageMessage(const std::uint8_t* data, std::size_t size) {
        if (core == nullptr || data == nullptr || size == 0) return false;
        if (core->hasCoreBytesIn()) {
            // flush=false: a flush DISCARDS whatever the device still holds, which for the N8 FIFO is the performance
            // queued behind this. A settings message is not a barrier; it joins the stream.
            core->pushCoreBytes(0, data, size, false);
            return true;
        }
        if (size > ::MidiEvent::kDataSize) return false;
        ::MidiEvent ev{};
        ev.frame = 0;
        ev.size = static_cast<std::uint32_t>(size);
        for (std::size_t i = 0; i < size; ++i) ev.data[i] = data[i];
        core->onMidi(&ev, 1);
        return true;
    }
    const void* get_interface(std::string_view id) noexcept { return id == RP_STAGE_INTERFACE ? &staged : nullptr; }
    void on_settings(const Settings& s) noexcept {
        settings = &s;
        pending = true;
    }

    // A create that names no system refuses here rather than planning a document whose node would run nothing
    // (docs/modular-graph-phase-c3-design.md section 3). A node also names its ROM; the seeds are optional (a blank
    // battery is legitimate).
    static std::optional<std::string> validate(const Settings& s) {
        if (s.system == 0)
            return std::string("a system node names the system it builds ({\"system\": <id>, \"rom\": \"res:N\"})");
        if (s.rom.empty()) return std::string("a system node names its ROM as {\"rom\": \"res:N\"}");
        return std::nullopt;
    }

    // The first activation builds the core the settings name, and gives it every knob the settings carry; later ones
    // (a re-activation) keep it. A core is built at the rate the plan runs at, which Engine::setSampleRate re-rates. A
    // plan that is then refused destroys it as it unwinds. Either way the core learns the largest block it will run
    // before it runs one: a build is handed the plan's maximum, so the core sizes its audio ring, its tap's rings and
    // its per-block scratches to it as it activates (a default-sized core would keep that memory for a small host
    // block's whole life), and a plan that grows a host's block reactivates the whole graph quiescent, where
    // reserveAudio moves the sizes with no block in flight.
    bool activate(double, std::uint32_t maxFrames) {
        if (core) {
            core->reserveAudio(maxFrames);
            return true;
        }
        // The config `Self::config` makes lives for this call and no longer: the build runs synchronously inside it,
        // and only `Self::kBuild` reads it.
        const auto config = static_cast<Self*>(this)->config(*settings);
        build(&config, maxFrames);
        core->reserveAudio(maxFrames);
        return true;
    }

    // Build the core the settings name, through rp.host.systems/2, with the bytes this instance's resources pinned, the
    // config its settings made, the plan's largest block and the build function its package carries: the build applies
    // the config and reserves that block before onActivate, and boots and restores the seeds there, so a non-default
    // model never restarts the core it just restored. A failure throws its reason, which rp::detail::activateFn reports
    // and the planner shows as activate-failed on this node. `config` is what `Self::config` made, and `Self::kBuild`
    // alone reads it.
    void build(const void* config, std::uint32_t maxBlockFrames) {
        if (!systems) throw std::runtime_error("the host offers no system builder (rp.host.systems/2)");
        SystemBuildRequest request;
        request.rom = bytesOf(settings->rom);
        request.sram = bytesOf(settings->sram);
        request.savestate = bytesOf(settings->state);
        request.romName = settings->romName.value_or(std::string());
        request.maxBlockFrames = maxBlockFrames;
        request.build = Self::kBuild;
        request.config = config;
        rp_error err{};
        SystemBase* made = systems->build(systems->ctx, settings->system, std::move(request), &err);
        if (!made)
            throw std::runtime_error(err.message[0] ? std::string(err.message) : std::string("the core did not build"));
        // The cast is by construction, which is why nothing checks it: the function that built the core is this type's
        // own, so a core of another class is a package's broken build function and not something a document or a host
        // can ask for.
        core = static_cast<Core*>(made);
        // The build applied the config, so its fields are on the core already: `applied` is seeded with the settings'
        // live knobs, less what the config does not carry, so the node's own first application finds them equal and
        // calls nothing (a model change would restart the core and lose the savestate the build restored). What is left
        // over is a live knob, which reaches the core now.
        applied = settings->liveKnobs();
        static_cast<Self*>(this)->configApplied(applied);
        applySettings();
    }

    static std::vector<std::uint8_t> bytesOf(const auto& field) {
        std::vector<std::uint8_t> out;
        if (const rp_bytes* bytes = field.bytes()) out.assign(bytes->data, bytes->data + bytes->size);
        return out;
    }

    // A type whose config carries none of its knobs clears them here, so the node's first block applies them.
    void configApplied(Applied&) const noexcept {}

    void applySettings() noexcept {
        static_cast<Self*>(this)->apply(*settings, applied);
        applied = settings->liveKnobs();
        pending = false;
    }
    // A core with no knobs of its own (the GBA, the SMS).
    void apply(const Settings&, const Applied&) noexcept {}

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
    // process. Knobs first, because a model change restarts the core before its inputs reach it, and because this
    // prepare is where a setting lands.
    void prepare(const rp::Block& b) noexcept {
        if (pending) applySettings();
        static_cast<Self*>(this)->applyParams();
        applyInputs();
        // The core learns how many lanes this block drains before it steps: a core whose step gates on a ring of its
        // own fills the one finish() will read, and finishBlock gets the same count. Only the lane COUNT is wanted
        // here, so this is the same lookup finish() makes, into a throwaway array.
        std::array<float*, 2 * kMaxOutputs> lanes{};
        core->setBlockLanes(static_cast<Self*>(this)->lanes(b, lanes));
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
        // The core's smoother starts at 0 dB, so its first value is snapped: a node placed at -90 dB is silent from
        // its first sample rather than ramping down from unity. Later edits ramp as every gain edit does.
        core->setGainDb(static_cast<float>(db), gainApplied != gainApplied);
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

}  // namespace rp::host
