#pragma once

// The control surface's device link (docs/modular-graph-phase-e2-design.md section 5): the interface between the
// rp.launchpad.surface-in / -out nodes and whoever drives the Launchpad, which a host installs under the device name
// "launchpad" with the interface id "rp.launchpad.surface-link/1" (rp.host.devices/2, graph/rp_device.h). Beside the
// surface's nodes in packages/native until the Launchpad has a pack of its own; the engine knows nothing of it but that
// id. The host owns the device - the MIDI ports, pairing them, the farewell the device needs before a host lets it go -
// and a link is the whole of what a node may do with it: the device's own MIDI in both directions, kept out of the
// musical stream, and its connected state.
//
// As rp::n8::CartLink: no RTTI, a protected non-virtual destructor, and a link that outlives every plan whose nodes
// hold it, so a node may use it in its destroy.

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace rp::launchpad {

// What the nodes ask for, and what a host installs its surface link as.
inline constexpr const char* kDeviceName           = "launchpad";
inline constexpr const char* kSurfaceLinkInterface = "rp.launchpad.surface-link/1";

// The widest message one link carries in either direction: a bulk LED SysEx at its worst (the Pro MK3's 106 RGB colour
// specifications at five bytes each, plus the 8-byte frame, is 538). The device's own replies are far shorter; one
// bound serves both ways, and a message past it is dropped and counted, never truncated into something the other end
// would misread.
inline constexpr std::size_t kMaxDeviceMessage = 560;

// One message in storage the caller owns: a drain copies into the node's own array, so nothing the block thread drops
// is freed there. Also the slot a device link's own receive ring holds (LaunchpadLink's). There is no arrival frame:
// the device's traffic is not timed against the block, so a surface node places what it drains at frame 0.
struct DeviceMessage {
    std::uint16_t size                     = 0;
    std::uint8_t  data[kMaxDeviceMessage] = {};
};

// A control surface (rp.launchpad.surface-in / -out).
class SurfaceLink {
public:
    // [audio] Copy the messages the device sent since the last drain into `out` and answer how many: min(available,
    // capacity), and fewer only when nothing is left, so a caller draining in batches can stop on a short answer.
    // It allocates and frees nothing: both ends keep every message in a fixed slot of their own, and one the caller
    // has no room for stays queued rather than being dropped or freed here.
    virtual std::size_t drainInput(DeviceMessage* out, std::size_t capacity) = 0;
    // [audio] Queue one message for the device (an LED frame, a mode change). Lock-free, never blocks; drops (and
    // counts in dropped()) when nothing is connected, the message is wider than kMaxDeviceMessage, or the ring is full.
    virtual void pushOutput(const std::uint8_t* data, std::size_t size) = 0;
    // [any] Whether the device is attached.
    virtual bool connected() const = 0;
    // [any] The pushes that lost their bytes: the device was detached, or its send ring was full. As CartLink's, the
    // count is the link's own, so it covers what any path pushing at the same device lost. A real surface link answers
    // the pushes it lost while attached and the paint sent while nothing was connected (LaunchpadLink's
    // messagesDropped and messagesDetached), so a node reads one number for what the device never got; a message the
    // device sent that its receive ring had no room for is counted apart (messagesInboundDropped), and this does not
    // answer it.
    virtual std::uint64_t dropped() const = 0;

protected:
    ~SurfaceLink() = default;
};

// What the surface nodes run on when the host answers their acquire with null (every host but the standalone): nothing
// queued, nothing received, and every push counted as dropped (a node reads that count as its device's drop). Each
// node's own member, so a document plans and runs the same in every host.
class NoSurface final : public SurfaceLink {
public:
    std::size_t   drainInput(DeviceMessage*, std::size_t) override { return 0; }
    void          pushOutput(const std::uint8_t*, std::size_t size) override {
        if (size != 0) dropped_.fetch_add(1, std::memory_order_relaxed);
    }
    bool          connected() const override { return false; }
    std::uint64_t dropped() const override { return dropped_.load(std::memory_order_relaxed); }

private:
    // 32 bits, as NoCart's: a 64-bit atomic is a library call on the RP2350's Cortex-M33, and nothing reads a stub's
    // count but the node that owns it. Not for the device yet, but a Launchpad pack inherits this stub.
    std::atomic<std::uint32_t> dropped_{0};
};

}  // namespace rp::launchpad
