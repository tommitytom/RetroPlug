// The Mesen consoles as nodes - rp.mesen.nes, rp.mesen.gba and rp.mesen.sms (docs/modular-graph-phase-b1-design.md
// section 3.1, and docs/modular-graph-phase-f2-design.md section 2 for the package they live in): each adapts one of the
// Mesen systems, builds its own core at activation from the ROM and seed resources its settings name
// (rp.host.systems/2, docs/modular-graph-phase-c3-design.md), and carries its own package's build function in the
// request, so the host that runs it names no console.
//
// Their settings are the fields each type reads and nothing else: the system it names, the resources its core is built
// from, and the console's knobs (docs/modular-graph-phase-c2-design.md section 3), each applied to the core the node has
// or - a construct-time field - a rebuild. The settings and the consoles' knob helpers are this package's, the generic
// ones are the system package's (system/SystemNode.hpp), and the NES's expansion-volume bounds are the N8 pack's
// (n8/N8Node.hpp), which rescales the same document value onto the real cart's master-volume register: one range, so
// the emulated NES and rp.n8.nes cannot disagree.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <utility>

#include "graph/node.hpp"
#include "n8/N8Node.hpp"
#include "system/SystemNode.hpp"
#include "system/mesen/MesenBackend.hpp"
#include "system/mesen/MesenGbaConfig.hpp"
#include "system/mesen/MesenGbaSystem.hpp"
#include "system/mesen/MesenNesConfig.hpp"
#include "system/mesen/MesenNesSystem.hpp"
#include "system/mesen/MesenSmsConfig.hpp"
#include "system/mesen/MesenSmsSystem.hpp"

namespace rp::host {

namespace {

using namespace rp::host::knobs;  // Within, Label, valueOf and moved: the system package's generic helpers

// The consoles' own knob helpers, beside the nodes whose settings read them. The generic ones are the system package's
// (system/SystemNode.hpp). This unnamed namespace rather than `knobs` itself, which every package reaches: a type two
// packages defined differently would be an ODR violation no linker reports.

// The store's own bounds (packages/mesen/ts/app.ts clampedNumber(0.25, 6.0, 1.4)).
struct ApuLatencyRange {
    static constexpr double min = 0.25, max = 6.0;  // milliseconds
};

// Optional wherever it appears: rfl::Validator's own default constructor validates T() (0.0), which this range refuses,
// so a field that ever defaulted it with rfl::DefaultIfMissing would throw instead of taking a value.
using ApuLatencyMs = rfl::Validator<double, Within<ApuLatencyRange>>;

// The construct-time fields' labels (packages/mesen/ts/enums.ts): the cart FIFO's profile, the N8 reliable link's mode,
// and the NES per-channel export mode. An enumerator's name is its label with the first letter lowercased, and the
// ordinals are the ones the core's config takes.
enum class FifoProfile : std::uint32_t { Instant = 0, Hardware = 1 };
enum class N8LinkMode : std::uint32_t { Auto = 0, Off = 1 };
enum class ChannelExportMode : std::uint32_t { Mix = 0, StereoModPins = 1, PinsPlusRef = 2, IndividualMono = 3 };

// The ranges the store clamps its ints to (packages/mesen/ts/app.ts clampedInt), refused outside rather than clamped, as
// C2's numbers are: 0 means "take the profile's value" for the two FIFO overrides, so 0 is in range.
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

// The NES's second param (docs/modular-graph-phase-c2-design.md section 3): its cartridge sound chip's level in whole
// percent of unity, as its core takes it and as rp.n8.nes rescales it onto the real cart's master-volume register, so the
// same document value sounds the same on both. n8/N8Node.hpp holds the one range both types read.
using ExpansionVolumeParam =
    rp::int_param<"Expansion volume", rp::range{rp::n8::kExpansionVolumeMin, rp::n8::kExpansionVolumeMax,
                                                rp::n8::kExpansionVolumeDefault}, "%">;

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

struct GbaSettings {  // rp.mesen.gba
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

    // The live knobs alone, which is what the node's `applied` holds (SystemNode::Applied says why). A Game Boy
    // Advance has none of its own yet.
    struct Applied {};
    Applied liveKnobs() const { return {}; }
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
    std::optional<ApuLatencyMs>                    apuLatencyMs;
    std::optional<knobs::Label<CartridgeAccuracy>> s5bNoise;
    std::optional<knobs::Label<CartridgeAccuracy>> mmc5PhaseReset;
    // The construct-time fields: the core reads them as it boots and has no setter for one, so a change is a rebuild.
    std::optional<std::string>                     sdRoot;
    std::optional<knobs::Label<FifoProfile>>       fifo;
    std::optional<FifoDepth>                       fifoDepth;
    std::optional<FifoBytesPerSecond>              fifoBytesPerSecond;
    std::optional<knobs::Label<N8LinkMode>>        n8Link;
    std::optional<N8LinkRttUs>                     n8LinkRttUs;
    std::optional<knobs::Label<ChannelExportMode>> channelExportMode;  // the tap's mode, armed at boot

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

    // The live knobs alone (SystemNode::Applied says why).
    struct Applied {
        std::optional<knobs::Label<NesRegion>>         region;
        std::optional<bool>                            removeSpriteLimit;
        std::optional<ApuLatencyMs>                    apuLatencyMs;
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

    // The live knobs alone (SystemNode::Applied says why): an SMS has none, every field it takes being read when
    // its core is built.
    struct Applied {};
    Applied liveKnobs() const { return {}; }
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
    // The NES's build: the package's own, which the node's request carries to rp.host.systems/2.
    static constexpr SystemBuildFn kBuild = rp::mesen::buildNesCore;
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
    static MesenNesConfig config(const Settings& s) {
        MesenNesConfig c;
        if (s.region) c.region = static_cast<std::uint32_t>(knobs::valueOf<NesRegion>(*s.region));
        if (s.removeSpriteLimit) c.removeSpriteLimit = *s.removeSpriteLimit;
        if (s.apuLatencyMs) c.apuLatencyMs = s.apuLatencyMs->value();
        if (s.s5bNoise) c.s5bNoise = static_cast<std::uint32_t>(knobs::valueOf<CartridgeAccuracy>(*s.s5bNoise));
        if (s.mmc5PhaseReset)
            c.mmc5PhaseReset = static_cast<std::uint32_t>(knobs::valueOf<CartridgeAccuracy>(*s.mmc5PhaseReset));
        if (s.sdRoot) c.sdRoot = *s.sdRoot;
        if (s.fifo) c.fifo = static_cast<std::uint32_t>(knobs::valueOf<FifoProfile>(*s.fifo));
        if (s.fifoDepth) c.fifoDepth = static_cast<std::uint32_t>(s.fifoDepth->value());
        if (s.fifoBytesPerSecond) c.fifoBytesPerSecond = static_cast<std::uint32_t>(s.fifoBytesPerSecond->value());
        if (s.n8Link) c.n8Link = static_cast<std::uint32_t>(knobs::valueOf<N8LinkMode>(*s.n8Link));
        if (s.n8LinkRttUs) c.n8LinkRttUs = static_cast<std::uint32_t>(s.n8LinkRttUs->value());
        if (s.channelExportMode)
            c.channelExportMode =
                static_cast<std::uint32_t>(knobs::valueOf<ChannelExportMode>(*s.channelExportMode));
        return c;
    }

    // The region first, because a region change resets the core (setRegion); the rest apply in place, in the order
    // the store's role config lists them.
    void apply(const Settings& to, const Applied& from) noexcept {
        if (knobs::moved(to.region, from.region))
            core->setRegion(static_cast<std::uint32_t>(knobs::valueOf<NesRegion>(*to.region)));
        if (knobs::moved(to.removeSpriteLimit, from.removeSpriteLimit)) core->setRemoveSpriteLimit(*to.removeSpriteLimit);
        if (knobs::moved(to.apuLatencyMs, from.apuLatencyMs)) core->setApuLatencyMs(to.apuLatencyMs->value());
        if (knobs::moved(to.s5bNoise, from.s5bNoise))
            core->setS5bNoise(static_cast<std::uint32_t>(knobs::valueOf<CartridgeAccuracy>(*to.s5bNoise)));
        if (knobs::moved(to.mmc5PhaseReset, from.mmc5PhaseReset))
            core->setMmc5PhaseReset(static_cast<std::uint32_t>(knobs::valueOf<CartridgeAccuracy>(*to.mmc5PhaseReset)));
    }

    // The gain, then the expansion volume, which the core's mixer re-reads each buffer: whole percent, as the core
    // takes it (the planner and setParam round an int param's value; a wire's is clamped and rounded here).
    void applyParams() noexcept {
        applyGain();
        const double latest = params.expansionVolume.latest();
        if (latest != latest) return;
        const double percent =
            std::lround(std::clamp(latest, static_cast<double>(rp::n8::kExpansionVolumeMin),
                                   static_cast<double>(rp::n8::kExpansionVolumeMax)));
        if (percent == expansionApplied) return;
        core->setExpansionVolume(static_cast<std::uint32_t>(percent));
        expansionApplied = percent;
    }
};

struct Gba : SystemNode<Gba, MesenGbaSystem, GbaSettings> {
    static constexpr const char* id = "rp.mesen.gba";
    static constexpr const char* name = "Game Boy Advance";
    static constexpr const char* category = "system";
    // The GBA's build: the package's own, which the node's request carries to rp.host.systems/2.
    static constexpr SystemBuildFn kBuild = rp::mesen::buildGbaCore;
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
    static MesenGbaConfig config(const Settings&) { return MesenGbaConfig{}; }
};


struct Sms : SystemNode<Sms, MesenSmsSystem, SmsSettings> {
    static constexpr const char* id = "rp.mesen.sms";
    static constexpr const char* name = "Master System / Game Gear";
    static constexpr const char* category = "system";
    // The Master System's build: the package's own, which the node's request carries to rp.host.systems/2.
    static constexpr SystemBuildFn kBuild = rp::mesen::buildSmsCore;
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
    static MesenSmsConfig config(const Settings& s) {
        MesenSmsConfig c;
        if (s.enableFm) c.enableFm = *s.enableFm;
        if (s.gameGear) c.gameGear = *s.gameGear;
        return c;
    }
};

// No declaration of this leaves the translation unit: the package's only public name is the `extern "C"` entry its
// manifest names.
const rp_package* mesenPackage() {
    static const rp_node_type* const kTypes[] = {rp::node_type<Nes>(), rp::node_type<Gba>(), rp::node_type<Sms>()};
    static const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.mesen","version":"1.0.0"})",
                                     static_cast<std::uint32_t>(std::size(kTypes)), kTypes};
    return &kPackage;
}

}  // namespace

}  // namespace rp::host

// The package's entry, named in packages/mesen/package.json: what a product's generated registration calls
// (rp_register_packages, graph/Packages.hpp).
extern "C" const rp_package* rp_mesen_package(void) { return rp::host::mesenPackage(); }
