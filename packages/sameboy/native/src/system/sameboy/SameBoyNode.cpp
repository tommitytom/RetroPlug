// rp.sameboy.gb, the Game Boy as a node (docs/modular-graph-phase-b1-design.md section 3.1, and
// docs/modular-graph-phase-f2-design.md section 2 for the package it lives in): the node adapts SameBoySystem, builds
// its own core at activation from the ROM and seed resources its settings name (rp.host.systems/2,
// docs/modular-graph-phase-c3-design.md), and carries its own package's build function (rp::sameboy::buildCore) in the
// request, so the host that runs it names no console.
//
// Its settings are the fields the type reads and nothing else: the system it names, the resources its core is built
// from, and the Game Boy's knobs (docs/modular-graph-phase-c2-design.md section 3), each applied to the core the node
// has, or - the model, the fast boot and the display group - a rebuild. The settings and the console's knob helpers are
// this package's, and the generic ones are the system package's (system/SystemNode.hpp).

#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <utility>

#include "graph/node.hpp"
#include "system/InputCapacity.hpp"
#include "system/SystemNode.hpp"
#include "system/sameboy/SameBoyBackend.hpp"
#include "system/sameboy/SameBoyConfig.hpp"
#include "system/sameboy/SameBoySystem.hpp"

namespace rp::host {

namespace {

using namespace rp::host::knobs;  // Within, Label, valueOf and moved: the system package's generic helpers

// The Game Boy's own knob helper, beside the node whose settings read it: the light temperature's range, which is
// SameBoy's own (applyDisplayConfig clamps to it). The generic helpers are the system package's
// (system/SystemNode.hpp). This unnamed namespace rather than `knobs` itself, which every package reaches: a type two
// packages defined differently would be an ODR violation no linker reports.
struct LightTemperatureRange {
    static constexpr double min = -1.0, max = 1.0;
};

using LightTemperature = rfl::Validator<double, Within<LightTemperatureRange>>;

struct GameBoySettings {
    std::uint64_t                             system = 0;
    rp::resource_ref<"rom">                   rom;
    rp::resource_ref<"sram">                  sram;
    rp::resource_ref<"state">                 state;
    std::optional<std::string>                romName;
    std::optional<knobs::Label<SameBoyModel>>           model;
    std::optional<bool>                                 fastBoot;
    std::optional<knobs::Label<SameBoyHighpass>>        highpass;
    std::optional<knobs::Label<SameBoyColorCorrection>> colorCorrection;
    std::optional<knobs::Label<SameBoyDmgPalette>>      dmgPalette;
    std::optional<LightTemperature>                     lightTemperature;
    // What the core captures of its serial port for LSDj's decoder: "listen" for master sync, "drive" for MI.OUT.
    std::optional<knobs::Label<SerialOutCapture>>       serialOutCapture;

    // What a rebuild is decided by: which system the node names, and every field the build reads (the resource refs are
    // not here: node.hpp rebuilds a moved ref by itself).
    struct Key {
        std::uint64_t              system = 0;
        std::optional<std::string> romName;
        bool operator==(const Key&) const = default;
    };
    Key key() const { return {system, romName}; }

    // The live knobs alone, which is what the node's `applied` holds (SystemNode::Applied says why).
    struct Applied {
        std::optional<knobs::Label<SameBoyModel>>           model;
        std::optional<bool>                                 fastBoot;
        std::optional<knobs::Label<SameBoyHighpass>>        highpass;
        std::optional<knobs::Label<SameBoyColorCorrection>> colorCorrection;
        std::optional<knobs::Label<SameBoyDmgPalette>>      dmgPalette;
        std::optional<LightTemperature>                     lightTemperature;
        std::optional<knobs::Label<SerialOutCapture>>       serialOutCapture;
    };
    Applied liveKnobs() const {
        return {model, fastBoot, highpass, colorCorrection, dmgPalette, lightTemperature, serialOutCapture};
    }
};

// The Game Boy's serialOut: a byte a message. While MI.OUT capture clocks the port (external clock, SC=0x80:
// SameBoySystem::writeAudioSample shifts one bit an audio frame) a byte takes eight frames, so the largest block a host
// can run (rp::graph::kMaxBlockFrames, 65536 frames; the plugin grows its plan to its buffer size) carries 8192. The
// other 64 are slack for what a block adds to that: up to 7 bits carried in from the block before, and the frames the
// core runs past its target (an instruction at a time, so rarely more than one; 64 bytes is 512 frames of it). Fixed
// rather than sized from the plan's block size, so a feedback wire from it would keep its front across every plan swap
// (no document places one). A Game Boy that clocks the port itself (internal clock, SC=0x81) shifts at its serial rate
// instead, and a CGB on its fast clock (32768 bytes a second, about 0.7 a frame at 48 kHz, twice that in double speed)
// can outrun this in a block past about 12000 frames (about 6000 in double speed): what does not fit is dropped and
// counted (GraphHost::droppedMessages), never lost silently.
constexpr rp::capacity kSerialOutCapacity{.messages = rp::kSerialOutInput.messages,
                                          .bytes = rp::kSerialOutInput.bytes};

struct GameBoy : SystemNode<GameBoy, SameBoySystem, GameBoySettings> {
    static constexpr const char* id = "rp.sameboy.gb";
    static constexpr const char* name = "Game Boy";
    static constexpr const char* category = "system";
    // The Game Boy's build: the package's own, which the node's request carries to rp.host.systems/2.
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
    // core runs an instruction at a time, so it can complete a byte past the block's end, and the drive path counts
    // the frame before it captures (SameBoySystem::writeAudioSample), so its stamps run 1..frames and a byte
    // completed in the block's last sample is stamped `frames`. Either is stamped at the block's last frame, so a
    // block's messages always lie inside it. Copied, not drained: the core clears the log at its next prepareForBlock.
    void writeQueues(std::uint32_t frames) noexcept {
        if (!connected[kSerialOut]) return;
        for (const auto& entry : core->serialOutLog_)
            outputs.serialOut.push(std::min(entry.first, frames - 1), std::span<const std::uint8_t>(&entry.second, 1));
    }

    // The coupled group's other members, in member order: the cable the serial callbacks read and write. linkPeers_
    // reserves 8, so a cable of up to 9 Game Boys never allocates here.
    void set_peers(std::span<rp_node* const> peers) noexcept {
        core->linkPeers_.clear();
        for (rp_node* p : peers)
            if (GameBoy* gb = rp::peer_as<GameBoy>(p)) core->linkPeers_.push_back(gb->core);
    }

    // The config the core is built with: every knob the settings carry, the model and the fast boot among them, which
    // the build applies before onActivate, so a loaded non-default model boots as that model and never restarts
    // afterwards. A knob absent from the settings keeps SameBoyConfig's default, which is the role schema's.
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
    // first block
    // of the plan that carries it, the block rp.lsdj.serial-out first runs in its new mode, so no byte captured in one
    // mode is decoded in the other.
    void apply(const Settings& to, const Applied& from) noexcept {
        if (knobs::moved(to.fastBoot, from.fastBoot)) core->setFastBoot(*to.fastBoot);
        if (knobs::moved(to.model, from.model)) core->setModel(knobs::valueOf<SameBoyModel>(*to.model));
        if (knobs::moved(to.highpass, from.highpass))
            core->setHighpass(knobs::valueOf<SameBoyHighpass>(*to.highpass));
        if (knobs::moved(to.colorCorrection, from.colorCorrection))
            core->setColorCorrection(knobs::valueOf<SameBoyColorCorrection>(*to.colorCorrection));
        if (knobs::moved(to.dmgPalette, from.dmgPalette))
            core->setDmgPalette(knobs::valueOf<SameBoyDmgPalette>(*to.dmgPalette));
        if (knobs::moved(to.lightTemperature, from.lightTemperature))
            core->setLightTemperature(to.lightTemperature->value());
        if (knobs::moved(to.serialOutCapture, from.serialOutCapture))
            core->setSerialOutCapture(knobs::valueOf<SerialOutCapture>(*to.serialOutCapture));
    }
};

// No declaration of this leaves the translation unit: the package's only public name is the `extern "C"` entry its
// manifest names.
const rp_package* sameboyPackage() {
    static const rp_node_type* const kTypes[] = {rp::node_type<GameBoy>()};
    static const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.sameboy","version":"1.0.0"})",
                                     static_cast<std::uint32_t>(std::size(kTypes)), kTypes};
    return &kPackage;
}

}  // namespace

}  // namespace rp::host

// The package's entry, named in packages/sameboy/package.json: what a product's generated registration calls
// (rp_register_packages, graph/Packages.hpp).
extern "C" const rp_package* rp_sameboy_package(void) { return rp::host::sameboyPackage(); }
