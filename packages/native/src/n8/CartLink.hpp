#pragma once

// The cart's device link (docs/modular-graph-phase-e2-design.md section 5): the interface between rp.n8.nes and
// whoever drives a real cart, which a host installs under the device name "n8" with the interface id
// "rp.n8.cart-link/1" (rp.host.devices/2, graph/rp_device.h). The engine knows nothing of it but that id: the node and
// the link that drives the cart are the pack's own, and this is their contract. The host owns the device - the serial
// port, reconnecting, the pause a job's transfer needs - and a link is the whole of what a node may do with it: the
// streaming ring, the connected state, the expansion-audio register and the capability jobs it runs on its own thread.
//
// Portable, so the node builds for the device as it does for the desktop: no std::string, std::vector or std::bitset,
// no RTTI, and a protected non-virtual destructor (a link is never deleted through this interface, and a virtual one
// makes the deleting destructor reach operator delete).
//
// The link outlives every plan whose nodes hold it: a host tears its plans down while its links are alive, and a
// reactivate keeps the link across it. So a node may use it in destroy, not only give it back: a job it cancels, the
// notes a cart still holds that it hands over, a connected() or dropped() it reads.

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "graph/rp_jobs.h"

namespace rp::n8 {

// What the node asks for, and what a host installs its cart link as.
inline constexpr const char* kDeviceName        = "n8";
inline constexpr const char* kCartLinkInterface = "rp.n8.cart-link/1";

// The notes a cart holds, one bit a note: channel * 128 + note, 16 channels of 128 (what rp.n8.nes keeps its books in,
// and what it hands its link when it leaves the plan holding some). A fixed array of words, so it is the same type on
// the device, where std::bitset's out-of-range checks would throw.
class CartNotes {
public:
    static constexpr std::size_t kNotes = 16 * 128;

    static constexpr std::size_t size() { return kNotes; }
    // The note `i` (below size(); one past it is ignored or answers false).
    void set(std::size_t i) {
        if (i < kNotes) words_[i / 32] |= bit(i);
    }
    void reset(std::size_t i) {
        if (i < kNotes) words_[i / 32] &= ~bit(i);
    }
    bool test(std::size_t i) const { return i < kNotes && (words_[i / 32] & bit(i)) != 0; }
    bool any() const {
        for (std::uint32_t w : words_)
            if (w != 0) return true;
        return false;
    }
    bool none() const { return !any(); }
    bool operator==(const CartNotes&) const = default;

private:
    static constexpr std::uint32_t bit(std::size_t i) { return std::uint32_t{1} << (i % 32); }

    std::uint32_t words_[kNotes / 32] = {};
};

// A cart in a real console (rp.n8.nes): the same inputs an emulated NES takes, pushed at the cart's FIFO.
class CartLink {
public:
    // [audio] Hand one message to the device at its intra-block `frame`, a sample offset `sampleRate` turns into the
    // moment the link releases it at. Lock-free, never allocates, never blocks; `flush` is the bytes port's barrier
    // flag, which the cart needs no separate byte for. False when any of the bytes were lost (the device is detached, a
    // job holds it, or its ring was full), which is the answer a node keeps its own books on; dropped() is the same
    // losses as a count.
    virtual bool push(std::uint32_t frame, const std::uint8_t* data, std::size_t size, bool flush,
                      double sampleRate) = 0;
    // [any] Whether the device is attached. Plain state, read afresh: a link may come up or go away between two reads.
    virtual bool connected() const = 0;
    // [any] The pushes that lost their bytes: the device was detached, a job held it, or its ring was full. An adapter
    // counts its own, so a caller reads the pushes it made that went nowhere even while another path pushes at the same
    // device.
    virtual std::uint64_t dropped() const = 0;
    // [audio, any thread] The cart's expansion-audio master-volume register: 0..255, or -1 to leave the register to
    // the cart (N8Host::EXP_VOL_AUTO: unity for a mapper that carries the sound chip, otherwise whatever the device's
    // own menu set). The link keeps the value and a live one reaches the device through its own serial thread at that
    // thread's next turn, so a node's param may call this from the block (no lock, no allocation, no std::function on
    // the caller's side), and every connect re-applies the value.
    virtual void setExpansionVolume(int value) = 0;
    // [control] Run a capability job on the device (graph/rp_jobs.h, docs/modular-graph-phase-c5-design.md section 6):
    // the node that presents rp.system.sram/1 or rp.system.rom/1 hands each job it is asked for here, with the host's
    // report interface and itself as `owner`. True when the device took it into `job` (it finishes it through
    // `reports`, on its own thread or at once); false with the reason in `error` (graph/rp_jobs.h's rp_job_error),
    // leaving `job` untouched: one job at a time per device ("busy: ..."), and none while the device is not attached
    // ("not connected"), which is all a link with no device ever answers. While a job runs the link reads disconnected
    // and drops (and counts) what is pushed, and the job completes once streaming has resumed.
    virtual bool submitJob(const rp_job_request& request, rp_job* job, const rp_host_jobs* reports, const void* owner,
                           char* error, std::size_t errorCap) {
        (void)request;
        (void)job;
        (void)reports;
        (void)owner;
        rp_job_error(error, errorCap, "not connected");
        return false;
    }
    // [control] The node `owner` is leaving the plan: a job it submitted that is still running fails now (its node's
    // removal) and the device reports into its record no more, stopping at the next point that leaves the device
    // coherent. Nothing when it holds no job.
    virtual void abandonJobs(const void* owner) { (void)owner; }
    // [control] The node is leaving the plan while the cart holds `notes` (it pushed their note-ons and no note-offs):
    // the device sends each a note-off from its own thread, after every byte pushed before this call, so no console is
    // left holding a tone no node will ever release. The node's destroy calls it, on the control thread after the plan
    // swap, where it may not push itself: the audio thread (and the node taking over) may still be pushing. A link with
    // no device, or one not streaming, drops them; nothing a node does depends on whether they went.
    virtual void releaseNotes(const CartNotes& notes) { (void)notes; }

    // There is deliberately no error string here: why a cart would not open is the host UI's (it reads N8Host's
    // lastError through Settings > N8), not something a document's node can act on. Nor an acquire or a release: a
    // host installs the link with hooks of its own for those (rp.host.devices/2), which the cart adapter uses to count
    // the nodes holding it and put the expansion volume back to AUTO on the last release.

protected:
    ~CartLink() = default;
};

// What rp.n8.nes runs on when the host answers its acquire with null (a host that owns no cart, as the CLI, the Node
// addon, the render host and the UI harness are): never connected, taking every push nowhere and counting it (a node
// keeps its own books on push()'s answer), an expansion volume it has nowhere to send, and every job refused "not
// connected" (CartLink's own answer). The node's own member, so a document plans and runs the same in every host.
class NoCart final : public CartLink {
public:
    bool push(std::uint32_t, const std::uint8_t*, std::size_t size, bool, double) override {
        if (size != 0) dropped_.fetch_add(1, std::memory_order_relaxed);
        return size == 0;
    }
    bool          connected() const override { return false; }
    std::uint64_t dropped() const override { return dropped_.load(std::memory_order_relaxed); }
    void          setExpansionVolume(int) override {}

private:
    // 32 bits, where the interface answers 64: a 64-bit atomic is a library call on the RP2350's Cortex-M33 (no
    // doubleword exclusives), and nothing reads a stub's count but the node that owns it, which keeps its own books.
    std::atomic<std::uint32_t> dropped_{0};
};

}  // namespace rp::n8
