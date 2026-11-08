// The pack's portable half (src/launchpad/SurfaceLink.hpp, SurfaceNodes.cpp): what a surface node may do with the
// device - the drain, the push, the claim's generation, the loss count - over the real transport's fixed-slot link, and
// the two types' descriptors as the goldens beside this file record them (docs/modular-graph-phase-f3-design.md section
// 8: the nodes' `connected` carries the generation, the drops output exists, and neither declares threads).
//
// The link here is the desktop one (ControllerLink) over a fake MIDI backend: no hardware, no MIDI system. The nodes
// themselves are Surface.test.cpp's, in the engine-graph binary, where a plan places them.

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "graph/Descriptor.hpp"
#include "launchpad/SurfaceNodes.hpp"
#include "launchpad/SurfaceLink.hpp"
#include "launchpad/desktop/ControllerLink.hpp"

#include "FakePorts.hpp"

namespace {

using rp::launchpad::ControllerLink;
using rp::launchpad::DeviceMessage;
using rp::launchpad::NoSurface;
using rp::launchpad::SurfaceLink;
using rp::launchpad::test::Bytes;
using rp::launchpad::test::openerFor;
using rp::launchpad::test::Rig;

const rp_node_type* typeOf(const char* id) {
    const rp_package* package = rp::launchpad::launchpadPackage();
    for (std::uint32_t i = 0; i < package->node_type_count; ++i)
        if (std::string(package->node_types[i]->id) == id) return package->node_types[i];
    return nullptr;
}

// A descriptor golden, recorded with RP_GRAPH_GOLDEN_UPDATE=1 beside this file (golden/<type>.descriptor.json), the
// same convention the engine-graph suite uses: the bytes are rp::graph's canonical form, so a changed field shows up
// here by name rather than as a silently different plan.
std::string goldenPath(const std::string& type) {
    return std::string(RP_LAUNCHPAD_GOLDEN_DIR) + "/" + type + ".descriptor.json";
}

void holdDescriptorToGolden(const rp_node_type* t) {
    REQUIRE(t != nullptr);
    const std::string bytes(t->descriptor(t));
    const std::string path = goldenPath(t->id);
    if (std::getenv("RP_GRAPH_GOLDEN_UPDATE") != nullptr) {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out << bytes;
        return;
    }
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.good());
    std::ostringstream want;
    want << in.rdbuf();
    CHECK(bytes == want.str());
}

}  // namespace

TEST_CASE("a link's generation is a claim's number, and 0 when nothing is held", "[launchpad][surface-link]") {
    Rig            rig;
    ControllerLink link(openerFor(rig), false);
    CHECK(link.generation() == 0);
    REQUIRE(link.connect("A in", "A out", {}));
    const std::uint32_t first = link.generation();
    CHECK(first != 0);
    REQUIRE(link.connect("B in", "B out", {}));
    CHECK(link.generation() != first);  // a swap inside one block is a new device, not a detached one
    link.disconnect();
    CHECK(link.generation() == 0);
}

TEST_CASE("a link's drain copies whole slots out, and its pushes are the device's LED traffic",
          "[launchpad][surface-link]") {
    Rig            rig;
    ControllerLink link(openerFor(rig), false);
    REQUIRE(link.connect("LP in", "LP out", {}));
    rig.deliver("LP in", {0x90, 0x51, 0x7f});
    DeviceMessage slots[4];
    REQUIRE(link.drainInput(slots, 4) == 1);
    CHECK(slots[0].size == 3);
    CHECK(Bytes(slots[0].data, slots[0].data + 3) == Bytes{0x90, 0x51, 0x7f});

    const Bytes leds{0xF0, 0x00, 0x20, 0x29, 0x02, 0x0E, 0x03, 0x00, 0xF7};
    link.pushOutput(leds.data(), leds.size());
    link.pump();
    CHECK(rig.written["LP out"] == std::vector<Bytes>{leds});
    CHECK(link.dropped() == 0);
}

TEST_CASE("a host with no surface answers generation 0 and counts every push as lost", "[launchpad][surface-link]") {
    // Every host but a product that starts the provider runs the nodes on this stub (NoSurface), so a document plans
    // and runs the same everywhere and a session still repaints when a device appears.
    NoSurface none;
    CHECK(none.generation() == 0);
    CHECK(none.drainInput(nullptr, 0) == 0);
    const std::uint8_t bytes[3] = {0x90, 0x01, 0x01};
    none.pushOutput(bytes, 3);
    CHECK(none.dropped() == 1);
}

TEST_CASE("both surface types declare unique and no threads, and carry the drops output", "[launchpad][surface-link]") {
    // The descriptor is the plan's whole view of a type: `unique` keeps one device to one link, and the absence of
    // `threads` is what lets a target that has none place the node (the pack's device entry).
    const rp_node_type* in  = typeOf("rp.launchpad.surface-in");
    const rp_node_type* out = typeOf("rp.launchpad.surface-out");
    for (const rp_node_type* t : {in, out}) {
        REQUIRE(t != nullptr);
        const std::string descriptor(t->descriptor(t));
        INFO(t->id);
        CHECK(descriptor.find(R"("unique":true)") != std::string::npos);
        CHECK(descriptor.find(R"("threads":true)") == std::string::npos);
    }
    REQUIRE(in != nullptr);
    const std::string inDescriptor(in->descriptor(in));
    CHECK(inDescriptor.find("connected") != std::string::npos);
    CHECK(inDescriptor.find("drops") != std::string::npos);  // the session's repaint trigger
    holdDescriptorToGolden(in);
    holdDescriptorToGolden(out);
}
