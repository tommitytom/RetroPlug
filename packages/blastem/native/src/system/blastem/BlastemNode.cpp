// The blastem package's entry (packages/blastem/package.json names it): a product registers the node types this
// returns. One so far: rp.blastem.md, the Sega Mega Drive. Its settings name the system whose core it builds at
// activation, the ROM and seed resources that core is built from, and the machine's region, which is construct-time
// (a region fixes the master clock and the frame height), so a change to it is a rebuild. The build is the package's
// own (rp::blastem::buildMdCore), which the node's request carries to rp.host.systems/2 so the host that runs it names
// no console; the knobs the core takes after that are none yet, so `Applied` is empty and the node applies nothing.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "graph/node.hpp"
#include "system/SystemNode.hpp"
#include "system/blastem/BlastemBackend.hpp"
#include "system/blastem/BlastemMdConfig.hpp"
#include "system/blastem/BlastemMdSystem.hpp"

namespace rp::host {

namespace {

// The settings the node reads and nothing else (docs/modular-graph-phase-c2-design.md section 3): the system it builds
// a core for, the resources that core is built from, and the console's construct-time fields.
struct MdSettings {
    std::uint64_t             system = 0;
    rp::resource_ref<"rom">   rom;
    rp::resource_ref<"sram">  sram;
    rp::resource_ref<"state"> state;
    // The ROM's file stem: BlastemMdSystem stages nothing (BlastEm takes the bytes), so this only names the image in
    // host-facing messages. It rebuilds, as every string field does.
    std::optional<std::string> romName;
    // Construct-time: a region fixes the master clock (53,693,175 NTSC against 53,203,395 PAL) and the frame's height
    // (224 against 240), so a change is a rebuild, which is what the settings' key decides.
    //
    // The labels ARE MdRegion's enumerator names (knobs::Label<E>, SystemNode.hpp): "auto", "ntscUs", "ntscJp", "pal",
    // which is the four words packages/blastem/ts/enums.ts spells. A label list beside the enum would be a second
    // spelling of one fact.
    std::optional<knobs::Label<rp::blastem::MdRegion>> region;
    // Whether a role claimed controller port 2 for this system: TS writes it from the protocol nodes the system's
    // roles place (graph/buildGraph.ts), so the cart that clocks itself from the DAW transport holds the lines and
    // every other Mega Drive game reads its player 2. A live knob, not a construct-time field: adding or dropping the
    // role's node is one document edit that must not rebuild the machine the cart is running.
    std::optional<bool> coreBytesIn;

    struct Key {
        std::uint64_t              system = 0;
        std::optional<std::string> romName;
        std::optional<int>         region;  // a label compares by its ordinal
        bool operator==(const Key&) const = default;
    };
    Key key() const {
        return {system, romName,
                region ? std::optional<int>(static_cast<int>(region->value())) : std::nullopt};
    }

    // The live knobs alone (SystemNode::Applied says why): every other field this node takes is read when its core is
    // built.
    struct Applied {
        std::optional<bool> coreBytesIn;
    };
    Applied liveKnobs() const { return {coreBytesIn}; }
};

struct Md : SystemNode<Md, rp::blastem::BlastemMdSystem, MdSettings> {
    static constexpr const char* id = "rp.blastem.md";
    static constexpr const char* name = "Mega Drive";
    static constexpr const char* category = "system";
    // The Mega Drive's build: the package's own, which the node's request carries to rp.host.systems/2.
    static constexpr SystemBuildFn kBuild = rp::blastem::buildMdCore;
    // All four, as the Master System's node declares them (MesenNode.cpp): SystemNode::applyInputs reads serialIn,
    // midi, bytes and buttons unconditionally, so a node that names three of them does not compile. `bytes` is where
    // the sync role's levels arrive (the rp.genmddj.sync node writes them), and a MIDI run reaches the same door when
    // a message is too long for the framed one: both are levels only for a system whose node says the port is claimed
    // (`coreBytesIn`), which is what keeps them off player 2 on every other cart.
    struct {
        rp::bytes_in  serialIn;
        rp::events_in midi;
        rp::bytes_in  bytes;
        rp::events_in buttons;
    } inputs;
    struct { rp::audio_out<rp::stereo> mix; } outputs;
    struct { GainParam gain; } params;
    std::size_t lanes(const rp::Block& b, std::array<float*, 2 * kMaxOutputs>& out) const { return mixLanes(b, out); }
    // The config the core is built with: the region, as the ordinal BlastemMdConfig takes. The gain is not here: it is
    // a param, given at the node's first block before the core steps.
    static rp::blastem::BlastemMdConfig config(const Settings& s) {
        rp::blastem::BlastemMdConfig c;
        if (s.region) c.region = static_cast<std::uint32_t>(knobs::valueOf<rp::blastem::MdRegion>(*s.region));
        return c;
    }

    // The port claim is not part of BlastemMdConfig: the core takes it through its own setter, so the build did not
    // apply it and the node's own first application must.
    void configApplied(Applied& a) const noexcept { a.coreBytesIn.reset(); }

    void apply(const Settings& to, const Applied& from) noexcept {
        if (knobs::moved(to.coreBytesIn, from.coreBytesIn)) core->setPort2Claimed(*to.coreBytesIn);
    }
};

// No declaration of this leaves the translation unit: the package's only public name is the `extern "C"` entry its
// manifest names.
const rp_package* blastemPackage() {
    static const rp_node_type* const kTypes[] = {rp::node_type<Md>()};
    static const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.blastem","version":"1.0.0"})",
                                     static_cast<std::uint32_t>(std::size(kTypes)), kTypes};
    return &kPackage;
}

}  // namespace

}  // namespace rp::host

// The package's entry, named in packages/blastem/package.json: what a product's generated registration calls
// (rp_register_packages, graph/Packages.hpp).
extern "C" const rp_package* rp_blastem_package(void) { return rp::host::blastemPackage(); }
