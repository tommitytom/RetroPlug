#pragma once

// What retroplug-device-mode-test's two halves share: the device-mode objects (built with RP_DEVICE and no exceptions
// or RTTI, cmake/tests.cmake) export their packages through C entries renamed for this binary, and the echo node
// reports the settings it decoded through a plain struct. Nothing here names an SDK type, so both builds read it alike.

#include <cstddef>
#include <cstdint>

#include "graph/rp_node.h"

extern "C" {
// rp.core, rp.dsp, rp.n8 and rp.device as a device builds them: CoreNodes.cpp, DspNodes.cpp, N8Node.cpp and
// DeviceIoNodes.cpp compiled in device mode, their entries (rp_core_package, rp_dsp_package, rp_n8_package,
// rp_device_io_package) renamed by the objects' compile definitions so they sit beside the desktop's.
const rp_package* rp_core_package_device_mode(void);
const rp_package* rp_dsp_package_device_mode(void);
const rp_package* rp_n8_package_device_mode(void);
const rp_package* rp_device_io_package_device_mode(void);
const rp_package* rp_device_io_package(void);  // the desktop's, which no product of this binary lists
// rp.devtest: the echo, pair, shaper and bare nodes (EchoNode.hpp) in device mode.
const rp_package* rp_devtest_package_device_mode(void);
// rp.devtest.fault: a node that fails at the step its settings name (PlanNodes.hpp), for the device check and the
// loader's run-up.
const rp_package* rp_devtest_fault_package_device_mode(void);
// rp.devtest.plan: the source, digest and lockstep nodes (PlanNodes.hpp) the exported-plan cases run.
const rp_package* rp_devtest_plan_package_device_mode(void);
// How many plan and fault nodes the device-mode build holds alive (PlanNodes.hpp's gLive).
int rp_devtest_plan_live_device_mode(void);
}

// What a fake cart link (FakeCart.hpp) was handed: each push, in order, and the expansion volume. The device-mode
// build's link is reached through rp_devtest_cart_device_mode, which answers it as the void* an rp.host.devices/2
// acquire hands rp.n8.nes; the desktop's is FakeCart.hpp's own type.
struct CartPush {
    std::uint32_t frame = 0;
    std::uint32_t size = 0;
    bool          flush = false;
    std::uint8_t  bytes[16] = {};
};

struct CartRecord {
    static constexpr std::uint32_t kPushes = 256;
    CartPush      pushes[kPushes];
    std::uint32_t count = 0;    // every push, recorded or past kPushes
    std::uint32_t refused = 0;  // pushes answered false (the link read disconnected)
    int           volume = -2;  // the last setExpansionVolume, -2 for none
    bool          connected = true;
};

extern "C" void* rp_devtest_cart_device_mode(CartRecord* record);

// The box's cart link (pico/n8-host/BoxCartLink.hpp) as the device builds it, for the box's whole pass over the
// device-mode rp.n8.nes: what its drain wrote, each FIFO write and each volume, and its counts, copied out field for
// field. The link is reached through the rp_devtest_box_* calls below, which answer it as the void* an
// rp.host.devices/2 acquire hands rp.n8.nes; the desktop's build of the same class is BoxCartLink.hpp's own type.
struct BoxWrite {
    std::uint32_t size = 0;
    std::uint32_t messages = 0;
    std::uint32_t firstUs = 0;
    std::uint8_t  bytes[192] = {};
};

struct BoxRecord {
    static constexpr std::uint32_t kWrites = 64;
    BoxWrite      writes[kWrites];
    std::uint32_t writeCount = 0;  // every write, recorded or past kWrites
    int           volumes[8] = {};
    std::uint32_t volumeCount = 0;
    bool          fifoFails = false;
    // When what the plan pushes arrived: the clock for one the plan made, and the staging's arrival (MidiStaging) for
    // one it was handed, as the box gives its link.
    void*         clockCtx = nullptr;
    std::uint32_t (*now)(void* ctx) = nullptr;
    bool (*arrival)(void* ctx, std::uint32_t frame, const std::uint8_t* data, std::size_t size,
                    std::uint32_t* us) = nullptr;
};

struct BoxCounts {
    std::uint32_t received, made, notReady, full, released, forwarded, failed, sysex, writes, maxBatch;
    std::uint32_t fromPlanInput;
    int           volume, holders;
};

extern "C" {
// A fresh link (its ring empty, disconnected, its counts zero) writing into `record`; the link, as rp.n8.nes takes it.
void* rp_devtest_box_link_device_mode(BoxRecord* record);
void  rp_devtest_box_connect_device_mode(bool connected);
void  rp_devtest_box_drain_device_mode(std::uint32_t nowMs);
void  rp_devtest_box_counts_device_mode(BoxCounts* out);
void  rp_devtest_box_acquired_device_mode(void* ctx);
void  rp_devtest_box_released_device_mode(void* ctx);
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

// What a digest node has seen, as it counted it: get_interface(kDigestInterface) answers one.
inline constexpr const char* kDigestInterface = "rp.devtest.digest/1";

struct DigestView {
    std::uint64_t hash = 1469598103934665603ull;  // FNV-1a 64 over every block's inputs
    std::uint32_t blocks = 0;
    bool          heard = false;  // anything but silence and empty queues reached it
};
