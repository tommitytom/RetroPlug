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
#include <type_traits>
#include <utility>

#include <rfl/Literal.hpp>
#include <rfl/Result.hpp>
#include <rfl/Validator.hpp>
#include <rfl/enums.hpp>
#include <rfl/parsing/schema/ValidationType.hpp>

#include "graph/CanonicalJson.hpp"
#include "graph/node.hpp"
#include "system/InputCapacity.hpp"
#include "host/graph/HostServices.hpp"
#include "system/SystemBase.hpp"
#include "system/SystemFactory.hpp"
#include "system/mesen/MesenGbaSystem.hpp"
#include "system/mesen/MesenNesConfig.hpp"
#include "system/mesen/MesenNesSystem.hpp"
#include "system/mesen/MesenSmsSystem.hpp"
#include "system/sameboy/SameBoyConfig.hpp"
#include "system/sameboy/SameBoySystem.hpp"
#include "transport/MidiTypes.hpp"

namespace rp::host {

// ---- settings ---------------------------------------------------------------------------------------------------

// The knobs' value types. An enum knob is a label as the role configs spell it
// (packages/retroplug/src/settingsEnums.ts), which is the name of its C++ enumerator with the first letter lowercased:
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

struct LightTemperatureRange {
    static constexpr double min = -1.0, max = 1.0;  // SameBoy's own: applyDisplayConfig clamps to it
};
struct ApuLatencyRange {
    // The store's own bounds (packages/retroplug/src/coreRoles.ts clampedNumber(0.25, 6.0, 1.4)).
    static constexpr double min = 0.25, max = 6.0;  // milliseconds
};

template <class E>
using Label = decltype(literalOf<E>(std::make_index_sequence<rfl::get_enumerator_array<E>().size()>{}));

template <class E>
E valueOf(const Label<E>& label) {
    return rfl::get_enumerator_array<E>()[label.value()].second;
}

using LightTemperature = rfl::Validator<double, Within<LightTemperatureRange>>;
// Optional wherever it appears: rfl::Validator's own default constructor validates T() (0.0), which this range
// refuses, so a field that ever defaulted it with rfl::DefaultIfMissing would throw instead of taking a value.
using ApuLatencyMs = rfl::Validator<double, Within<ApuLatencyRange>>;

// The construct-time fields' labels (packages/retroplug/src/settingsEnums.ts): the cart FIFO's profile, the N8
// reliable link's mode, and the NES per-channel export mode. As with C2's knobs, an enumerator's name is its label
// with the first letter lowercased, and the ordinals are the ones the core's config takes.
enum class FifoProfile : std::uint32_t { Instant = 0, Hardware = 1 };
enum class N8LinkMode : std::uint32_t { Auto = 0, Off = 1 };
enum class ChannelExportMode : std::uint32_t { Mix = 0, StereoModPins = 1, PinsPlusRef = 2, IndividualMono = 3 };

template <class E>
using Label = decltype(literalOf<E>(std::make_index_sequence<rfl::get_enumerator_array<E>().size()>{}));

// The ranges the store clamps its ints to (packages/retroplug/src/coreRoles.ts clampedInt), refused outside rather than
// clamped, as C2's numbers are: 0 means "take the profile's value" for the two FIFO overrides, so 0 is in range.
struct FifoDepthRange {
    static constexpr int min = 0, max = 1 << 20;
};
struct FifoRateRange {
    static constexpr int min = 0, max = 100'000'000;
};
struct N8LinkRttRange {
    static constexpr int min = 0, max = 100'000;
};
using FifoDepth          = rfl::Validator<int, Within<FifoDepthRange>>;
using FifoBytesPerSecond = rfl::Validator<int, Within<FifoRateRange>>;
using N8LinkRttUs        = rfl::Validator<int, Within<N8LinkRttRange>>;

}  // namespace knobs

// Each type's settings, named so their JSON Schema $defs keys are the same under every compiler.
//
// A node names the `system` whose core it builds at activation (docs/modular-graph-phase-c3-design.md section 3), with
// the ROM and seeds as resources: a new core is a new id, so a new system is a new instance (rebuild_key), and a node
// naming none is a create refusal. `rom` is always a resource (a node never touches the filesystem); `sram` and `state`
// are optional seeds, and with both given the savestate wins. `romName` is the ROM's file stem, which a Master System or
// Game Gear core stages its file under (its battery stem), now that its ROM is bytes.
//
// Every other field is either a knob of that core (docs/modular-graph-phase-c2-design.md section 3), optional and
// applied to the core the node has, or a construct-time field, which the core is built with and which therefore
// rebuilds the node (rebuild_key). The shared fields repeat in each type rather than nesting, so a document's settings
// stay one flat object and a type's schema lists only what it reads.
struct SystemNodeSettings {  // rp.mesen.gba
    std::uint64_t              system = 0;
    rp::resource_ref<"rom">    rom;
    rp::resource_ref<"sram">   sram;
    rp::resource_ref<"state">  state;
    std::optional<std::string> romName;

    // What a rebuild is decided by: which system the node names, and every field the build reads (the resource refs
    // are not here: node.hpp rebuilds a moved ref by itself).
    struct Key {
        std::uint64_t              system = 0;
        std::optional<std::string> romName;
        bool operator==(const Key&) const = default;
    };
    Key key() const { return {system, romName}; }

    // The live knobs alone, which is what the node's `applied` holds: a construct-time field or the ROM's name rebuilds
    // the node instead of being applied to the core it has, so the block thread's copy of what the core was given
    // copies no string. A Game Boy Advance has none of its own yet.
    struct Applied {};
    Applied liveKnobs() const { return {}; }
};

struct GameBoySettings {
    std::uint64_t                                      system = 0;
    rp::resource_ref<"rom">                            rom;
    rp::resource_ref<"sram">                           sram;
    rp::resource_ref<"state">                          state;
    std::optional<std::string>                         romName;
    std::optional<knobs::Label<SameBoyModel>>           model;
    std::optional<bool>                                 fastBoot;
    std::optional<knobs::Label<SameBoyHighpass>>        highpass;
    std::optional<knobs::Label<SameBoyColorCorrection>> colorCorrection;
    std::optional<knobs::Label<SameBoyDmgPalette>>      dmgPalette;
    std::optional<knobs::LightTemperature>              lightTemperature;
    // What the core captures of its serial port for LSDj's decoder: "listen" for master sync, "drive" for MI.OUT.
    std::optional<knobs::Label<SerialOutCapture>>       serialOutCapture;

    struct Key {
        std::uint64_t              system = 0;
        std::optional<std::string> romName;
        bool operator==(const Key&) const = default;
    };
    Key key() const { return {system, romName}; }

    // The live knobs alone (the comment on SystemNodeSettings::Applied says why).
    struct Applied {
        std::optional<knobs::Label<SameBoyModel>>           model;
        std::optional<bool>                                 fastBoot;
        std::optional<knobs::Label<SameBoyHighpass>>        highpass;
        std::optional<knobs::Label<SameBoyColorCorrection>> colorCorrection;
        std::optional<knobs::Label<SameBoyDmgPalette>>      dmgPalette;
        std::optional<knobs::LightTemperature>              lightTemperature;
        std::optional<knobs::Label<SerialOutCapture>>       serialOutCapture;
    };
    Applied liveKnobs() const {
        return {model, fastBoot, highpass, colorCorrection, dmgPalette, lightTemperature, serialOutCapture};
    }
};

struct NesSettings {
    std::uint64_t                                   system = 0;
    rp::resource_ref<"rom">                         rom;
    rp::resource_ref<"sram">                        sram;
    rp::resource_ref<"state">                       state;
    std::optional<std::string>                      romName;
    // The live knobs (C2).
    std::optional<knobs::Label<NesRegion>>         region;
    std::optional<bool>                            removeSpriteLimit;
    std::optional<knobs::ApuLatencyMs>             apuLatencyMs;
    std::optional<knobs::Label<CartridgeAccuracy>> s5bNoise;
    std::optional<knobs::Label<CartridgeAccuracy>> mmc5PhaseReset;
    // The construct-time fields: the core reads them as it boots and has no setter for one, so a change is a rebuild.
    std::optional<std::string>                      sdRoot;
    std::optional<knobs::Label<knobs::FifoProfile>> fifo;
    std::optional<knobs::FifoDepth>                 fifoDepth;
    std::optional<knobs::FifoBytesPerSecond>        fifoBytesPerSecond;
    std::optional<knobs::Label<knobs::N8LinkMode>>  n8Link;
    std::optional<knobs::N8LinkRttUs>               n8LinkRttUs;
    std::optional<knobs::Label<knobs::ChannelExportMode>> channelExportMode;  // the tap's mode, armed at boot

    struct Key {
        std::uint64_t              system = 0;
        std::optional<std::string> romName;
        std::optional<std::string> sdRoot;
        std::optional<int>         fifo;                 // labels compared by their ordinal
        std::optional<int>         fifoDepth;
        std::optional<int>         fifoBytesPerSecond;
        std::optional<int>         n8Link;
        std::optional<int>         n8LinkRttUs;
        std::optional<int>         channelExportMode;
        bool operator==(const Key&) const = default;
    };
    Key key() const {
        // A label compares by its ordinal (its place in the enumerator array), which only an equal label shares; the
        // numbers are already what the key compares.
        auto ofLabel = [](const auto& label) {
            return label ? std::optional<int>(static_cast<int>(label->value())) : std::nullopt;
        };
        auto ofNumber = [](const auto& number) {
            return number ? std::optional<int>(number->value()) : std::nullopt;
        };
        return {system,                 romName,
                sdRoot,                 ofLabel(fifo),
                ofNumber(fifoDepth),    ofNumber(fifoBytesPerSecond),
                ofLabel(n8Link),        ofNumber(n8LinkRttUs),
                ofLabel(channelExportMode)};
    }

    // The live knobs alone (the comment on SystemNodeSettings::Applied says why).
    struct Applied {
        std::optional<knobs::Label<NesRegion>>         region;
        std::optional<bool>                            removeSpriteLimit;
        std::optional<knobs::ApuLatencyMs>             apuLatencyMs;
        std::optional<knobs::Label<CartridgeAccuracy>> s5bNoise;
        std::optional<knobs::Label<CartridgeAccuracy>> mmc5PhaseReset;
    };
    Applied liveKnobs() const { return {region, removeSpriteLimit, apuLatencyMs, s5bNoise, mmc5PhaseReset}; }
};

struct SmsSettings {
    std::uint64_t              system = 0;
    rp::resource_ref<"rom">    rom;
    rp::resource_ref<"sram">   sram;
    rp::resource_ref<"state">  state;
    std::optional<std::string> romName;
    // The construct-time fields: the machine, and whether the YM2413 is emulated at all (configureSms runs before
    // LoadRom, so neither has a setter).
    std::optional<bool> enableFm;
    std::optional<bool> gameGear;

    struct Key {
        std::uint64_t              system = 0;
        std::optional<std::string> romName;
        std::optional<bool>        enableFm, gameGear;
        bool operator==(const Key&) const = default;
    };
    Key key() const { return {system, romName, enableFm, gameGear}; }

    // The live knobs alone (the comment on SystemNodeSettings::Applied says why): an SMS has none, every field it takes
    // being read when its core is built.
    struct Applied {};
    Applied liveKnobs() const { return {}; }
};

namespace {

constexpr std::size_t kMaxOutputs = 16;

// The params (docs/modular-graph-phase-c2-design.md section 3): what is worth playing, automating or modulating, so
// each is also a control input, its value in the document and its live edits through Engine::setParam. Every system
// node has `gain`, in dB over the range the store clamps a gain to (systemSettings.ts GAIN_MIN / GAIN_MAX); a NES has
// `expansionVolume` too, its cartridge sound chip's level in whole percent of unity, as its core takes it. Each range
// is one constant, used by the param's declaration and by the node's own clamp of a wire-driven value.
constexpr double kGainMinDb = -90.0;
constexpr double kGainMaxDb = 12.0;
using GainParam = rp::float_param<"Gain", rp::range{kGainMinDb, kGainMaxDb, 0}, "dB">;
using ExpansionVolumeParam =
    rp::int_param<"Expansion volume", rp::range{kExpansionVolumeMin, kExpansionVolumeMax, kExpansionVolumeDefault}, "%">;

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

// What every system node shares: the core it builds, its settings and params reaching that core, the connected-output
// mask, its inputs, and prepare / step / finish mapped one to one onto the core's block triad. `Self` declares its
// params (`gain` at least), supplies finish's lanes, may write output queues after the core has run (writeQueues), and
// applies its own knobs (apply) and any param beyond the gain (applyParams).
//
// A node builds its core at activation from the bytes its resources hold (rp.host.systems/1,
// docs/modular-graph-phase-c3-design.md section 5). The core is the host's: the snapshot slot under the system id, the
// live cores, and the delete when the node is destroyed. Which fields a build reads is rebuild_key's, the
// construct-time fields among them.
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
template <class Self, class Core, class S = SystemNodeSettings>
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
        build(static_cast<Self*>(this)->config(*settings), maxFrames);
        core->reserveAudio(maxFrames);
        return true;
    }

    // Build the core the settings name, through rp.host.systems/1, with the bytes this instance's resources pinned, the
    // config its settings made and the plan's largest block: the backend applies the config and reserves that block
    // before onActivate, and boots and restores the seeds there, so a non-default model never restarts the core it just
    // restored. A failure throws its reason, which rp::detail::activateFn reports and the planner shows as
    // activate-failed on this node.
    void build(SystemConfig config, std::uint32_t maxBlockFrames) {
        if (!systems) throw std::runtime_error("the host offers no system builder (rp.host.systems/1)");
        SystemBuildRequest request;
        request.rom = bytesOf(settings->rom);
        request.sram = bytesOf(settings->sram);
        request.savestate = bytesOf(settings->state);
        request.romName = settings->romName.value_or(std::string());
        request.maxBlockFrames = maxBlockFrames;
        request.config = std::move(config);
        rp_error err{};
        SystemBase* made = systems->build(systems->ctx, settings->system, std::move(request), &err);
        if (!made)
            throw std::runtime_error(err.message[0] ? std::string(err.message) : std::string("the core did not build"));
        // The config alternative the request carries names the console, so the core handed back must be that console's:
        // a host that built another reports another kind(), and every core class reports a kind no other one does, so
        // the comparison checks the type the cast below then assumes. A mismatch is a host or settings bug rather than
        // a document's, and the core it built is given back before the refusal.
        if (made->kind() != Core::kKind) {
            systems->destroy(systems->ctx, made);
            throw std::runtime_error(std::string("the built core is not a ") + Self::name);
        }
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
constexpr rp::capacity kSerialOutCapacity{.messages = rp::kSerialOutInput.messages,
                                           .bytes = rp::kSerialOutInput.bytes};

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

    // The config the core is built with: every knob the settings carry, the model and the fast boot
    // among them, which the backend applies before onActivate, so a loaded non-default model boots as that model and
    // never restarts afterwards. A knob absent from the settings keeps SameBoyConfig's default, which is the role
    // schema's.
    static SystemConfig config(const Settings& s) {
        SameBoyConfig c;
        if (s.model) c.model = knobs::valueOf<SameBoyModel>(*s.model);
        if (s.fastBoot) c.fastBoot = *s.fastBoot;
        if (s.highpass) c.highpass = knobs::valueOf<SameBoyHighpass>(*s.highpass);
        if (s.colorCorrection) c.colorCorrection = knobs::valueOf<SameBoyColorCorrection>(*s.colorCorrection);
        if (s.dmgPalette) c.dmgPalette = knobs::valueOf<SameBoyDmgPalette>(*s.dmgPalette);
        if (s.lightTemperature) c.lightTemperature = s.lightTemperature->value();
        return c;
    }

    // The capture mode is not part of SameBoyConfig: the core takes it through its own setter, so the build did not
    // apply it and the node's own first application must.
    void configApplied(Applied& a) const noexcept { a.serialOutCapture.reset(); }

    // The fast boot first, which waits for the next restart and so is what a model's restart boots with: a block that
    // carries both (several documents published between two blocks commit together) restarts on the new fast boot,
    // where making the edits one at a time would not have when the fast boot was published after the model. Then the
    // rest in the order the store's role config lists them, the capture last. The capture lands at the start of the
    // first block of the plan that carries it, the block rp.lsdj.serial-out first runs in its new mode, so no byte
    // captured in one mode is decoded in the other.
    void apply(const Settings& to, const Applied& from) noexcept {
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
        for (const char* port : kNesTaps)
            l.ports.push_back({port, PortDir::Out, PortKind::Audio, ChannelSpec::fixed(1, "mono"), {}, false});
        l.exclusive.push_back({"pins.*", "channels.*"});
    }

    // With mix connected, the mix (2 lanes), whether or not the tap is armed: a split routing on a host too narrow
    // for it still arms the tap, and this block drains the mix. Otherwise the armed mode's streams, each mono
    // stream's two lanes pointed at its one port buffer (the NES writes only lane 2k).
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

    // The config the core is built with: the construct-time fields, the region, the sprite limit, the
    // APU latency and the cartridge-accuracy pair, each as the settings carry it. The expansion volume is not here: it
    // is a param, given at the node's first block before the core steps, so the core boots at its own default and never
    // plays a frame at it.
    static SystemConfig config(const Settings& s) {
        MesenNesConfig c;
        if (s.region) c.region = static_cast<std::uint32_t>(knobs::valueOf<NesRegion>(*s.region));
        if (s.removeSpriteLimit) c.removeSpriteLimit = *s.removeSpriteLimit;
        if (s.apuLatencyMs) c.apuLatencyMs = s.apuLatencyMs->value();
        if (s.s5bNoise) c.s5bNoise = static_cast<std::uint32_t>(knobs::valueOf<CartridgeAccuracy>(*s.s5bNoise));
        if (s.mmc5PhaseReset)
            c.mmc5PhaseReset = static_cast<std::uint32_t>(knobs::valueOf<CartridgeAccuracy>(*s.mmc5PhaseReset));
        if (s.sdRoot) c.sdRoot = *s.sdRoot;
        if (s.fifo) c.fifo = static_cast<std::uint32_t>(knobs::valueOf<knobs::FifoProfile>(*s.fifo));
        if (s.fifoDepth) c.fifoDepth = static_cast<std::uint32_t>(s.fifoDepth->value());
        if (s.fifoBytesPerSecond) c.fifoBytesPerSecond = static_cast<std::uint32_t>(s.fifoBytesPerSecond->value());
        if (s.n8Link) c.n8Link = static_cast<std::uint32_t>(knobs::valueOf<knobs::N8LinkMode>(*s.n8Link));
        if (s.n8LinkRttUs) c.n8LinkRttUs = static_cast<std::uint32_t>(s.n8LinkRttUs->value());
        if (s.channelExportMode)
            c.channelExportMode =
                static_cast<std::uint32_t>(knobs::valueOf<knobs::ChannelExportMode>(*s.channelExportMode));
        return c;
    }

    // The region first, because a region change resets the core (setRegion); the rest apply in place, in the order
    // the store's role config lists them.
    void apply(const Settings& to, const Applied& from) noexcept {
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
    // A Game Boy Advance has no construct-time field of its own yet: its config is the struct's defaults.
    static SystemConfig config(const Settings&) { return MesenGbaConfig{}; }
};

struct Sms : SystemNode<Sms, MesenSmsSystem, SmsSettings> {
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
    // The machine and the FM unit, both of which configureSms sets before LoadRom and neither of which has a setter: a
    // change is a rebuild, which the settings' key decides.
    static SystemConfig config(const Settings& s) {
        MesenSmsConfig c;
        if (s.enableFm) c.enableFm = *s.enableFm;
        if (s.gameGear) c.gameGear = *s.gameGear;
        return c;
    }
};

}  // namespace

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

extern "C" const rp_package* rp_sameboy_package(void) { return rp::host::sameboyPackage(); }
extern "C" const rp_package* rp_mesen_package(void) { return rp::host::mesenPackage(); }
