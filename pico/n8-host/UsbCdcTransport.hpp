// The N8's Edio link on the Pico (docs/modular-graph-phase-e2-design.md section 8): rp::n8::Transport over TinyUSB's
// CDC host, so the box runs the pack's one Edio (packages/n8/native/src/n8/Edio.hpp) over the N8 in its USB socket.
//
// Each wait pumps the USB host (tuh_task) and the box's idle hook (box_feed: the watchdog and the status LED), never a
// bare sleep, so the long ones (a ROM upload, the menu loading a ROM off the SD for up to 12 s) keep the box alive.
// So it runs from the main loop alone, never from a tuh_* callback, and nothing the hook does may call the Edio over
// it.
//
// The link is up from bind() to unbind(). unbind() is the CDC unmount's (tuh_cdc_umount_cb, which fires inside a
// pump); bind() is the main loop's, before the probe of a new mount, so it never happens inside a wait. From an
// unmount until the box binds the next N8, every wait fails at once (linkUp() false) instead of running out its timeout
// against a device that has gone, and a call that was mid-command when its N8 went can never carry on into the next
// one: the new N8 hears nothing until the box has started over at its probe.
#pragma once

#include <cstddef>
#include <cstdint>

#include "n8/Transport.hpp"

class UsbCdcTransport final : public rp::n8::Transport {
public:
    // How long a write may go without the TX FIFO taking a byte, the best-effort wait for a write's bytes to leave,
    // and the silence that ends an input flush: the C Edio's values, which the baseline was measured with.
    static constexpr std::uint32_t kWriteMs      = 2000;
    static constexpr std::uint32_t kDrainMs      = 500;
    static constexpr std::uint32_t kFlushQuietMs = 20;

    // Constant-initialised, so the box's one transport is in static storage with nothing run to make it.
    explicit constexpr UsbCdcTransport(void (*idle)()) : idle_(idle) {}

    // The N8's CDC interface, from its mount: the link is up until unbind().
    void bind(std::uint8_t cdcIndex) {
        idx_ = cdcIndex;
        up_  = true;
    }
    void unbind() { up_ = false; }

    // All of `size` once TinyUSB has taken it, then a wait of up to kDrainMs for the TX FIFO to drain (a short write
    // under a packet does not go out by itself, and a flush is a no-op while the endpoint is busy, so the bytes are
    // flushed until they have left). The drain is best effort: still queued at kDrainMs is a success. Short when the
    // FIFO took nothing for kWriteMs, and 0 when the link went down before the bytes left.
    std::size_t write(const std::uint8_t* data, std::size_t size) override;
    // What arrives within `timeoutMs`, counted from the call (0: one pump, and the bytes it found): whatever has come
    // when nothing more is waiting, so Edio reads again for the rest and each wait counts from the last arrival.
    std::size_t read(std::uint8_t* buffer, std::size_t size, std::uint32_t timeoutMs) override;
    // Drops input until kFlushQuietMs pass with nothing arriving.
    void flushInput() override;
    bool linkUp() const override { return up_; }

private:
    void pump() const;

    void (*idle_)();
    std::uint8_t idx_ = 0;
    // Cleared by the unmount callback inside a pump, set by the main loop: both on the one thread, so no fence.
    bool up_ = false;
};
