#pragma once

// The control surface a headless host stages into: a SurfaceLink with no device behind it, so the RPC harness can drive
// a controller session with no Launchpad attached (docs/modular-graph-phase-c5-design.md section 5). The plugin and the
// SDL standalone install their real links on the engine; the test host (src/main.cpp, retroplug-host) installs this one
// instead, so stageControllerIn and renderBlocks' controller field still put a pad press where a session reads it,
// now through the surface nodes the document places rather than through an engine-owned staging arena.
//
// Nothing is ever connected, so a session runs unattached exactly as it did when the harness staged into that arena
// (it decodes presses and sends its LED diff, without the Programmer-mode handshake a link first reporting one brings).
// What the session sends back is counted, not stored: no headless test reads a Launchpad's LED stream, and counting
// keeps pushOutput allocation-free as a link's must be.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include "transport/SpscRing.hpp"
#include "host/devices/DeviceLinks.hpp"

namespace rp::host {

class HarnessSurfaceLink final : public SurfaceLink {
public:
    // The messages one host may stage before a block drains them, and the bytes each may hold: the surface link's own
    // receive ring depth and the message bound (kMaxDeviceMessage) are what a real link holds too.
    static constexpr std::size_t kStagedMessages = 256;

    // [control] Stage one message as the device would have sent it, at frame 0 of the next block. False, and nothing
    // staged, for an empty one, one wider than kMaxDeviceMessage, or a full ring - the last of which is the one counted
    // in dropped(); an empty or oversized message is refused without being counted, where a real link counts an
    // oversized arrival too.
    bool queueInput(std::span<const std::uint8_t> bytes) {
        if (bytes.empty() || bytes.size() > kMaxDeviceMessage) return false;
        DeviceMessage m;
        m.size = static_cast<std::uint16_t>(bytes.size());
        for (std::size_t i = 0; i < bytes.size(); ++i) m.data[i] = bytes[i];
        if (staged_->tryPush(m)) return true;
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    // ---- SurfaceLink ----
    std::size_t drainInput(DeviceMessage* out, std::size_t capacity) override {
        std::size_t n = 0;
        while (n < capacity && staged_->tryPop(out[n])) ++n;
        return n;
    }
    void pushOutput(const std::uint8_t* /*data*/, std::size_t /*size*/) override {
        pushed_.fetch_add(1, std::memory_order_relaxed);
    }
    bool connected() const override { return false; }
    // As a real link's: the messages this one had no room for (queueInput's return value is the caller's too).
    std::uint64_t dropped() const override { return dropped_.load(std::memory_order_relaxed); }

    // How many messages the session has sent the device since the link was made: a test's proof that the wiring reaches
    // the surface at all, without storing LED frames nothing reads.
    std::uint64_t pushed() const { return pushed_.load(std::memory_order_relaxed); }

private:
    // 256 slots of one 562-byte message is about 140 KiB, and the test host's link is a local of main (a thread
    // Windows gives 1 MiB of stack, where every suite here has 8), so the ring is one allocation made when the link is
    // built, as LaunchpadLink's receive ring is, and never rides a stack. Staging and draining copy in and out of its
    // fixed slots.
    std::unique_ptr<SpscRing<DeviceMessage, kStagedMessages>> staged_ =
        std::make_unique<SpscRing<DeviceMessage, kStagedMessages>>();
    std::atomic<std::uint64_t>                          pushed_{0};
    std::atomic<std::uint64_t>                          dropped_{0};
};

}  // namespace rp::host
