#pragma once

// What retroplug-device-mode-test's two halves share: the device-mode objects (built with RP_DEVICE and no exceptions
// or RTTI, cmake/tests.cmake) export their packages through C entries renamed for this binary, and the echo node
// reports the settings it decoded through a plain struct. Nothing here names an SDK type, so both builds read it alike.

#include <cstdint>

#include "graph/rp_node.h"

extern "C" {
// rp.core, rp.dsp and rp.n8 as a device builds them: CoreNodes.cpp, DspNodes.cpp and N8Node.cpp compiled in device
// mode, their entries (rp_core_package, rp_dsp_package, rp_n8_package) renamed by the objects' compile definitions so
// they sit beside the desktop's.
const rp_package* rp_core_package_device_mode(void);
const rp_package* rp_dsp_package_device_mode(void);
const rp_package* rp_n8_package_device_mode(void);
// rp.devtest: the echo, pair, shaper and bare nodes (EchoNode.hpp) in device mode.
const rp_package* rp_devtest_package_device_mode(void);
// rp.devtest.fault: a node that fails at the step its settings name (DeviceModeNodes.cpp), for the device check.
const rp_package* rp_devtest_fault_package_device_mode(void);
}

// A package's `device` entry as its manifest names it, with both builds of it: the desktop's (`desktop`) and the
// device-mode objects' (`device`, the entry renamed <entry>_device_mode). The generated device-entries.cpp lists every
// one (packages/native/cmake/tests.cmake), in the order the packages are added.
struct DeviceEntry {
    const char*         package;
    const char*         entry;
    rp_package_entry_fn desktop;
    rp_package_entry_fn device;
};

// The settings an echo node decoded, as its on_settings copied them: get_interface(kEchoInterface) answers one.
inline constexpr const char* kEchoInterface = "rp.devtest.echo/1";

struct EchoView {
    bool          flag = false;
    std::uint8_t  u8 = 0;
    std::uint32_t u32 = 0;
    std::int32_t  i32 = 0;
    std::int64_t  i64 = 0;
    float         f = 0.0f;
    double        d = 0.0;
    std::uint32_t pick = 0;
    char          name[9] = {};
    std::uint32_t nameSize = 0;
    std::uint16_t counts[4] = {};
    std::uint32_t countsSize = 0;
    std::uint32_t rom = 0;
    bool          romResolved = false;
    int           mode = 0;  // the enum field, as its value
};
