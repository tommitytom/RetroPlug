#pragma once

// The cart device link rp.n8.nes binds through rp.host.devices/1, over the N8Host a host already owns: the streaming
// ring, the connected state, the pushes it lost, the expansion-audio master volume N8Host keeps for the project and
// re-applies on every connect, and the cart's capability jobs. Nothing here opens the port or decides when the link
// comes up: that stays the host's (Settings > N8, the port being exclusive so one instance has it, and the pause and
// resume a job's transfer needs). Header-only, as it is a few forwards over what N8Host already does; the plugin and
// the standalone, which own the N8Host, are its only callers.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

#include "host/devices/DeviceLinks.hpp"
#include "host/n8/N8Host.hpp"

namespace retroplug {

class N8CartLink final : public rp::host::CartLink {
public:
    explicit N8CartLink(N8Host& host) : host_(host) {}

    // [audio] N8Link::push. `frame` is the intra-block sample offset and `sampleRate` the block's rate, which the link
    // turns into the moment it releases the bytes at, through its timed scheduler and lookahead. `flush` goes no
    // further: on the physical side the arm byte itself is the barrier the bytes port's flag stands for, so the link's
    // ring carries the barrier and no extra byte.
    bool push(std::uint32_t frame, const std::uint8_t* data, std::size_t size, bool flush,
              double sampleRate) override {
        (void)flush;
        if (host_.link().push(frame, data, size, sampleRate)) return true;
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    bool          connected() const override { return host_.link().isConnected(); }
    // Every push of this adapter that lost its bytes, plus the chunks a stream stopped after a job found in its ring
    // (N8Link::chunksDiscarded: pushes that reached the ring after the pause's drain, whose caller the link cannot
    // tell). A node keeps its own books on push()'s answer; this is the count for a caller that reads the link.
    std::uint64_t dropped() const override {
        return dropped_.load(std::memory_order_relaxed) + host_.link().chunksDiscarded();
    }

    // [audio, any thread] The cart's master-volume register: 0..255, or -1 to leave it to the cart
    // (N8Host::EXP_VOL_AUTO). N8Host keeps the value in an atomic and its serial thread writes it at its next turn
    // while streaming, without interrupting the stream, and applies it again inside every connect.
    void setExpansionVolume(int value) override { host_.setExpVol(value); }

    // [control] The nodes bound to this link. A node acquires in its activate (once: a reactivate keeps its handle) and
    // releases once in its destructor, at reclaim on the control thread. The last release puts the register back to
    // AUTO, so a project that stops driving the cart - the setting off, its NES removed, another console loaded - does
    // not leave the console's expansion chip at the level it last played at. A handover never reaches zero: the node
    // taking over acquires before the one it replaces is reclaimed, so the reset cannot race the new node's first
    // value.
    void acquire() override { holders_.fetch_add(1, std::memory_order_acq_rel); }
    void release() override {
        if (holders_.fetch_sub(1, std::memory_order_acq_rel) == 1) host_.setExpVol(N8Host::EXP_VOL_AUTO);
    }

    // [control] The cart's battery file and ROM as capability jobs, run on the link's serial thread with streaming
    // paused (N8Host::submitCartJob says which operations, and what each does).
    bool submitJob(const rp::host::JobRequest& request, rp::host::Job& job, const void* owner,
                   std::string* error) override {
        return host_.submitCartJob(request, job, owner, error);
    }
    void abandonJobs(const void* owner) override { host_.abandonCartJobs(owner); }
    // [control] N8Link::releaseNotes: the serial thread writes their note-offs behind what was already pushed.
    void releaseNotes(const rp::host::CartNotes& notes) override { host_.link().releaseNotes(notes); }

private:
    N8Host&                    host_;
    std::atomic<std::uint64_t> dropped_{0};
    std::atomic<int>           holders_{0};
};

}  // namespace retroplug
