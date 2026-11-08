#include "host/graph/SystemNodes.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "graph/node.hpp"
#include "system/InputCapacity.hpp"
#include "system/SystemBase.hpp"
#include "system/SystemNode.hpp"
#include "system/mesen/MesenGbaConfig.hpp"
#include "system/mesen/MesenBackend.hpp"
#include "system/mesen/MesenGbaSystem.hpp"
#include "system/mesen/MesenNesConfig.hpp"
#include "system/mesen/MesenNesSystem.hpp"
#include "system/mesen/MesenSmsSystem.hpp"
#include "system/sameboy/SameBoyBackend.hpp"
#include "system/sameboy/SameBoyConfig.hpp"
#include "system/sameboy/SameBoySystem.hpp"
#include "transport/MidiTypes.hpp"

namespace rp::host {

// ---- settings ---------------------------------------------------------------------------------------------------

// The knobs one console owns: its range types, its labels' enums and the validators made of them, kept beside the node
// whose settings read them. The generic ones are the system package's (system/SystemNode.hpp).
namespace knobs {

struct LightTemperatureRange {
    static constexpr double min = -1.0, max = 1.0;  // SameBoy's own: applyDisplayConfig clamps to it
};
struct ApuLatencyRange {
    // The store's own bounds (packages/mesen/ts/app.ts clampedNumber(0.25, 6.0, 1.4)).
    static constexpr double min = 0.25, max = 6.0;  // milliseconds
};

using LightTemperature = rfl::Validator<double, Within<LightTemperatureRange>>;
// Optional wherever it appears: rfl::Validator's own default constructor validates T() (0.0), which this range
// refuses, so a field that ever defaulted it with rfl::DefaultIfMissing would throw instead of taking a value.
using ApuLatencyMs = rfl::Validator<double, Within<ApuLatencyRange>>;

// The construct-time fields' labels (packages/mesen/ts/enums.ts): the cart FIFO's profile, the N8
// reliable link's mode, and the NES per-channel export mode. As with C2's knobs, an enumerator's name is its label
// with the first letter lowercased, and the ordinals are the ones the core's config takes.
enum class FifoProfile : std::uint32_t { Instant = 0, Hardware = 1 };
enum class N8LinkMode : std::uint32_t { Auto = 0, Off = 1 };
enum class ChannelExportMode : std::uint32_t { Mix = 0, StereoModPins = 1, PinsPlusRef = 2, IndividualMono = 3 };

// The ranges the store clamps its ints to (packages/mesen/ts/app.ts clampedInt), refused outside rather than
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

// The NES's second param (docs/modular-graph-phase-c2-design.md section 3): its cartridge sound chip's level in whole
// percent of unity, as its core takes it and as rp.n8.nes rescales it onto the real cart's master-volume register, so
// the same document value sounds the same on both. SystemNodes.hpp holds the one range both types read.
using ExpansionVolumeParam =
    rp::int_param<"Expansion volume", rp::range{kExpansionVolumeMin, kExpansionVolumeMax, kExpansionVolumeDefault}, "%">;

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
    // The Game Boy's build: the shell's SameBoy builder, until the console's package owns it.
    static constexpr SystemBuildFn kBuild = rp::sameboy::buildCore;
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
    static SameBoyConfig config(const Settings& s) {
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
        if (knobs::moved(to.fastBoot, from.fastBoot)) core->setFastBoot(*to.fastBoot);
        if (knobs::moved(to.model, from.model)) core->setModel(knobs::valueOf<SameBoyModel>(*to.model));
        if (knobs::moved(to.highpass, from.highpass)) core->setHighpass(knobs::valueOf<SameBoyHighpass>(*to.highpass));
        if (knobs::moved(to.colorCorrection, from.colorCorrection))
            core->setColorCorrection(knobs::valueOf<SameBoyColorCorrection>(*to.colorCorrection));
        if (knobs::moved(to.dmgPalette, from.dmgPalette))
            core->setDmgPalette(knobs::valueOf<SameBoyDmgPalette>(*to.dmgPalette));
        if (knobs::moved(to.lightTemperature, from.lightTemperature)) core->setLightTemperature(to.lightTemperature->value());
        if (knobs::moved(to.serialOutCapture, from.serialOutCapture))
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
    // The NES's build: the shell's Mesen builder, until the console's package owns it.
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
        const double percent = std::lround(std::clamp(latest, static_cast<double>(kExpansionVolumeMin),
                                                     static_cast<double>(kExpansionVolumeMax)));
        if (percent == expansionApplied) return;
        core->setExpansionVolume(static_cast<std::uint32_t>(percent));
        expansionApplied = percent;
    }
};

struct Gba : SystemNode<Gba, MesenGbaSystem, SystemNodeSettings> {
    static constexpr const char* id = "rp.mesen.gba";
    static constexpr const char* name = "Game Boy Advance";
    static constexpr const char* category = "system";
    // The GBA's build: the shell's Mesen builder, until the console's package owns it.
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
    // The Master System's build: the shell's Mesen builder, until the console's package owns it.
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
