#pragma once

// The control surface's device link (docs/modular-graph-phase-f3-design.md section 3): the interface between the
// rp.launchpad.surface-in / -out nodes and whoever drives the Launchpad, which a host installs under the device name
// "launchpad" with the interface id "rp.launchpad.surface-link/2" (rp.host.devices/2, graph/rp_device.h). The engine
// knows nothing of it but that id. The host owns the device - the MIDI ports, pairing them, the farewell the device
// needs before a host lets it go - and a link is the whole of what a node may do with it: the device's own MIDI in both
// directions, kept out of the musical stream, its claim's generation, and what it lost.
//
// /2 is /1 plus `generation()`: a pair swapped faster than a block must still read as a NEW device, so the link numbers
// its claims and the node reports the number rather than a flag. /1 named the connected flag alone, which cannot tell a
// swap from nothing at all.
//
// As rp::n8::CartLink: no RTTI, a protected non-virtual destructor, and a link that outlives every plan whose nodes
// hold it, so a node may use it in its destroy.

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace rp::launchpad {

// What the nodes ask for, and what a host installs its surface link as.
inline constexpr const char* kDeviceName           = "launchpad";
inline constexpr const char* kSurfaceLinkInterface = "rp.launchpad.surface-link/2";

// The widest message one link carries in either direction: a bulk LED SysEx at its worst (the Pro MK3's 106 RGB colour
// specifications at five bytes each, plus the 8-byte frame, is 538). The device's own replies are far shorter; one
// bound serves both ways, and a message past it is dropped and counted, never truncated into something the other end
// would misread.
inline constexpr std::size_t kMaxDeviceMessage = 560;

// One message in storage the caller owns: a drain copies into the node's own array, so nothing the block thread drops
// is freed there. It is also the slot a device link's own receive ring holds (ControllerLink's), which is why a message
// wider than it is refused by the link rather than truncated into it. There is no arrival frame: the device's traffic is
// not timed against the block, so a surface node places what it drains at frame 0.
struct DeviceMessage {
    std::uint16_t size                     = 0;
    std::uint8_t  data[kMaxDeviceMessage] = {};
};

// A control surface (rp.launchpad.surface-in / -out).
class SurfaceLink {
public:
    // [audio] Copy the messages the device sent under the current claim since the last drain into `out` and answer how
    // many: min(available, capacity), and fewer only when nothing is left, so a caller draining in batches can stop on a
    // short answer. An earlier claim's messages are discarded here rather than delivered, and nothing is delivered while
    // no claim is held (the ring is left where it is, for the claim that is arriving).
    // It allocates and frees nothing: both ends keep every message in a fixed slot of their own, and one the caller
    // has no room for stays queued rather than being dropped or freed here.
    virtual std::size_t drainInput(DeviceMessage* out, std::size_t capacity) = 0;
    // [audio] Queue one message for the device (an LED frame, a mode change). Never blocks; drops (and counts in
    // dropped()) when nothing is connected, the message is wider than kMaxDeviceMessage, or what the link holds for the
    // device is full.
    virtual void pushOutput(const std::uint8_t* data, std::size_t size) = 0;
    // [any] Which claim is attached: 0 when nothing is, else a number the link never reuses, bumped on every connect
    // (a disconnect included). A node reports it as its `connected` stream, so a pair swapped inside one block still
    // reads as a new device and the session takes it again.
    virtual std::uint32_t generation() const = 0;
    // [any] What the link lost, both directions: a message handed to it for the device that went nowhere (nothing was
    // connected, or what the link holds for the device was full or could not carry it), and a message FROM the device
    // that its receive ring had no room for. A node reads one number for what the device and the session were each
    // promised and did not get, and repaints when it moves.
    virtual std::uint64_t dropped() const = 0;

protected:
    ~SurfaceLink() = default;
};

// What the surface nodes run on when the host answers their acquire with null (the render host, the harness, a product
// that starts no provider): nothing queued, nothing received, and every push counted as dropped. Each node's own
// member, so a document plans and runs the same in every host.
class NoSurface final : public SurfaceLink {
public:
    std::size_t   drainInput(DeviceMessage*, std::size_t) override { return 0; }
    void          pushOutput(const std::uint8_t*, std::size_t size) override {
        if (size != 0) dropped_.fetch_add(1, std::memory_order_relaxed);
    }
    std::uint32_t generation() const override { return 0; }
    std::uint64_t dropped() const override { return dropped_.load(std::memory_order_relaxed); }

private:
    // 32 bits, as NoCart's: a 64-bit atomic is a library call on the RP2350's Cortex-M33, and nothing reads a stub's
    // count but the node that owns it. The stub is what both nodes run in a device image, which has no host of this
    // package.
    std::atomic<std::uint32_t> dropped_{0};
};

}  // namespace rp::launchpad
