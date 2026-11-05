#pragma once

// The fault node the device check and the exported-plan cases run (docs/modular-graph-phase-e3-design.md section 8),
// one source built twice into retroplug-device-mode-test as EchoNode.hpp's are: in device mode by DeviceModeNodes.cpp,
// and on the desktop by DevicePlan.test.cpp. The plans those cases load run the rp.devtest.plan package's source,
// digest and lockstep beside it (packages/devtest), whose live count it joins, so the loader's unwinding is seen
// leaving none of them alive.
//
// - fault: fails at the step its settings name; the device check runs its device build, and the loader's cases
//   export a plan holding it. It fails only as the device builds it: the exporter plans its desktop build, which
//   activates, so a plan that names it exports and the loader is what sees it fail.
// - flood and miscount (rp.devtest.misfit): what the device check's plan run must refuse though nothing faults. A
//   flood pushes twice what its events output holds each block, so its queue drops; a miscount answers the digest's
//   interface, hearing everything, but counts every other block, as a digest a plan skipped would.

#include <cstdint>
#include <string_view>

#include "devtest/DevtestNodes.hpp"
#include "graph/node.hpp"

namespace rp::graph::devtest {

using rp::devtest::Live;

struct FaultSettings {
    rp::choice<"none", "activate", "process"> at;
    rp::resource_ref<"rom">                   rom;
};

// A create fails on its resource, a "rom" the device check's host does not hold; `at` makes activate refuse or the
// block fault.
struct Fault {
    static constexpr const char* id = "rp.devtest.fault";
    using Settings = FaultSettings;

    struct { rp::audio_out<rp::mono> out; } outputs;

    Live          live;
    std::uint32_t at = 0;
    void on_settings(const Settings& s) noexcept { at = s.at.value(); }

#ifdef RP_DEVICE
    bool activate(double, std::uint32_t) noexcept { return at != 1; }
#else
    bool activate(double, std::uint32_t) noexcept { return true; }
#endif
    void deactivate() noexcept {}

    rp_status process(const rp::Block& b) noexcept {
        for (std::uint32_t i = 0; i < b.frames(); ++i) outputs.out[0][i] = 0.0f;
        return at == 2 ? RP_FAULT : RP_OK;
    }
};

struct Flood {
    static constexpr const char* id = "rp.devtest.flood";
    struct { rp::events_out::sized<rp::capacity{.messages = 4, .bytes = 64}> out; } outputs;

    Live live;
    void process(const rp::Block&) noexcept {
        for (std::uint8_t k = 0; k < 8; ++k) {
            const std::uint8_t note[3] = {0x90, k, 100};
            outputs.out.push(0, note);
        }
    }
};

struct Miscount {
    static constexpr const char* id = "rp.devtest.miscount";
    struct { rp::audio_out<rp::mono> out; } outputs;

    Live              live;
    rp_devtest_digest view{};
    std::uint32_t     calls = 0;

    const void* get_interface(std::string_view want) noexcept {
        return want == RP_DEVTEST_DIGEST_INTERFACE ? &view : nullptr;
    }
    void process(const rp::Block& b) noexcept {
        for (std::uint32_t i = 0; i < b.frames(); ++i) outputs.out[0][i] = 0.0f;
        view.heard = true;
        if (calls++ % 2 == 0) ++view.blocks;
    }
};

#ifndef RP_DEVICE
// The desktop's build of the fault node's package, which plans and activates whatever `at` says (the device-mode
// objects export theirs as rp_devtest_fault_package_device_mode).
inline const rp_package* faultPackageDesktop() {
    static const rp_node_type* const kTypes[] = {rp::node_type<Fault>()};
    static const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.devtest.fault","version":"1.0.0"})", 1, kTypes};
    return &kPackage;
}
// And of the misfits' (rp_devtest_misfit_package_device_mode is the device's).
inline const rp_package* misfitPackageDesktop() {
    static const rp_node_type* const kTypes[] = {rp::node_type<Flood>(), rp::node_type<Miscount>()};
    static const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.devtest.misfit","version":"1.0.0"})", 2, kTypes};
    return &kPackage;
}
#endif

}  // namespace rp::graph::devtest
