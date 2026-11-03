// The device-mode half of retroplug-device-mode-test: built with RP_DEVICE and with neither exceptions nor RTTI
// (cmake/tests.cmake), beside the device-mode builds of CoreNodes.cpp and DspNodes.cpp. It holds the echo node's
// package (echo, pair, shaper, bare) and the fault node's; the test reaches them, and the two node packages, only
// through their C entries.

#include <cstdint>

#include "DeviceModeEntries.hpp"
#include "EchoNode.hpp"

#ifndef RP_DEVICE
#error "DeviceModeNodes.cpp is the device-mode half: it is built with RP_DEVICE"
#endif

namespace rp::graph::devtest {
namespace {

// fault: fails at the step its settings name, so the device check's Create, Activate and Process stops are each seen
// naming it (DeviceCheck.test.cpp). A create fails on its resource, a "rom" the check's host does not hold; `at` makes
// activate refuse or the block fault.
struct FaultSettings {
    rp::choice<"none", "activate", "process"> at;
    rp::resource_ref<"rom">                   rom;
};

struct Fault {
    static constexpr const char* id = "rp.devtest.fault";
    using Settings = FaultSettings;

    struct { rp::audio_out<rp::mono> out; } outputs;

    std::uint32_t at = 0;
    void on_settings(const Settings& s) noexcept { at = s.at.value(); }

    bool activate(double, std::uint32_t) noexcept { return at != 1; }
    void deactivate() noexcept {}

    rp_status process(const rp::Block& b) noexcept {
        for (std::uint32_t i = 0; i < b.frames(); ++i) outputs.out[0][i] = 0.0f;
        return at == 2 ? RP_FAULT : RP_OK;
    }
};

const rp_node_type* const kFaultTypes[] = {rp::node_type<Fault>()};
const rp_package kFaultPackage{RP_ABI_VERSION, R"({"id":"rp.devtest.fault","version":"1.0.0"})", 1, kFaultTypes};

const rp_node_type* const kTypes[] = {rp::node_type<Echo>(), rp::node_type<Pair>(), rp::node_type<Shaper>(),
                                      rp::node_type<Bare>()};
const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.devtest","version":"1.0.0"})", 4, kTypes};

}  // namespace
}  // namespace rp::graph::devtest

extern "C" const rp_package* rp_devtest_package_device_mode(void) { return &rp::graph::devtest::kPackage; }
extern "C" const rp_package* rp_devtest_fault_package_device_mode(void) { return &rp::graph::devtest::kFaultPackage; }
