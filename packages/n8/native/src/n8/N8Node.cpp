#include "n8/N8Node.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

#include "graph/node.hpp"
#include "graph/rp_device.h"
#include "graph/rp_jobs.h"
#include "graph/rp_stage.h"
#include "n8/CartLink.hpp"

namespace rp::n8 {

int expansionVolumeRegister(double percent) {
    const long value = std::lround(percent * 128.0 / 100.0);
    return static_cast<int>(std::clamp(value, 0L, 255L));
}

namespace {

using ExpansionVolumeParam =
    rp::int_param<"Expansion volume", rp::range{kExpansionVolumeMin, kExpansionVolumeMax, kExpansionVolumeDefault}, "%">;

// The real cart, a node of its own (docs/modular-graph-phase-c5-design.md section 4). It borrows the host's cart link
// for the life of its plan: acquired in activate, kept across a reactivate, released in its destructor (the reclaim's,
// on the control thread). Placed by a setting rather than by the device's state, so it always activates: in a host with
// no cart (its acquire answers null, and the node runs on a NoCart of its own), or with the cart unplugged, the link
// reads disconnected and takes every push nowhere, and nothing in the graph changes when the cart comes or goes.
struct N8Nes {
    static constexpr const char* id = "rp.n8.nes";
    static constexpr const char* name = "NES (N8 cart)";
    static constexpr const char* category = "device";
    // No `threads`: whether a link runs a thread is the link's business, not the node's (the desktop's N8 link streams
    // from a serial thread of its own, and a box's link runs on the box's loop). All the node asks of a link is a push
    // that never blocks, which CartLink promises, so a target with no threads (the Pico) plans the type.
    // One cart, one link: the planner refuses a second node of this type in a plan (buildGraph places one, for the
    // first NES whose "drive the real cart" setting is on).
    static constexpr bool unique = true;
    // rp.mesen.nes's two inputs the cart's FIFO is fed from, shaped as that node's are, so a source wired to one wires
    // to the other.
    struct {
        rp::events_in midi;
        rp::bytes_in  bytes;
    } inputs;
    struct { rp::control_out connected; } outputs;
    struct { ExpansionVolumeParam expansionVolume; } params;

    const rp_host_devices* devices = nullptr;
    // The host's report interface (rp.host.jobs/1), handed to the link with every job it takes: the record a job runs
    // into is the host's, so the host's interface is what finishes it. A host that offers none takes no device job.
    const rp_host_jobs*    reports = nullptr;
    CartLink*              cart = nullptr;  // the host's link, or `none`; set by the first activate
    void*                  held = nullptr;  // what the acquire answered, handed back to the release
    NoCart                 none;
    // What the node presents as rp.system.sram/1 and rp.system.rom/1: every job goes to the link, the node its owner.
    rp_device_jobs jobs{this, &submitJob};
    // The volume the link was last given, in whole percent (NaN: none yet, so the first block gives it), and whether
    // the link read connected in the last block, so a reconnect gives it again.
    double volumeSent = std::numeric_limits<double>::quiet_NaN();
    bool   wasConnected = false;
    // The notes the cart was sent a note-on for and no note-off yet (a channel's 128 notes a channel, 16 channels), and
    // of those, the ones whose note-off the link dropped (it was down, or a job held it). An owed note-off goes out at
    // the start of the next block the link reads connected in: a pause drops what is pushed while it lasts, and a
    // note-off lost there would leave the console holding the note for good. What is still sounding when the node
    // leaves the plan is handed to the link (the destructor).
    CartNotes sounding, owed;

    void bind_host(const rp_host* h) noexcept {
        devices = static_cast<const rp_host_devices*>(h->get_interface(h, RP_HOST_DEVICES_INTERFACE));
        reports = static_cast<const rp_host_jobs*>(h->get_interface(h, RP_HOST_JOBS_INTERFACE));
    }
    bool activate(double, std::uint32_t) {
        if (!devices) return false;
        // A reactivate runs activate again on this instance: the link already held is kept, one acquire to one
        // release. A host with no cart answers null once, and the node keeps its own stub across a reactivate as it
        // would the host's link.
        if (!cart) {
            held = devices->acquire(devices->ctx, kDeviceName, kCartLinkInterface);
            cart = held ? static_cast<CartLink*>(held) : &none;
        }
        return true;
    }
    ~N8Nes() {
        if (!devices || !cart) return;
        // The notes the cart still holds go to the link, whose own thread sends their note-offs behind what this node
        // pushed: no later message can reach the cart through a node that is gone, so a note held now (the setting
        // turned off, the NES removed, the drive moving, a project closed while the DAW plays) would sound until a
        // power-cycle. Not pushed here: this runs on the control thread after the swap, and the audio thread, or the
        // node taking over (whose books start empty), may be pushing at the ring now.
        if (sounding.any()) cart->releaseNotes(sounding);
        // A job this node submitted that is still running fails now, and the link reports into its record no more, so
        // the engine may free the record; the device stops at its next safe point.
        cart->abandonJobs(this);
        if (held) devices->release(devices->ctx, held);
    }

    // rp.stage/1: a message a host aimed at this node (Engine::stageNodeMidi's path), onto the link's ring at frame 0
    // exactly as this block's `midi` input would put it there, note bookkeeping and all: a UI's settings message
    // reaches the cart whose menu it is whatever the project's musical routing does. A push while the link is down
    // goes nowhere and is counted; a note-off lost there stays owed and goes out on the next connected block.
    rp_host_stage staged{this, &stageFn};
    static bool stageFn(void* ctx, const std::uint8_t* data, std::size_t size, double sampleRate) {
        auto* self = static_cast<N8Nes*>(ctx);
        if (!self->cart || data == nullptr || size == 0) return false;
        self->pushMidi(0, data, size, sampleRate);
        return true;
    }

    const void* get_interface(std::string_view want) noexcept {
        if (want == RP_STAGE_INTERFACE) return &staged;
        return want == RP_SYSTEM_SRAM_INTERFACE || want == RP_SYSTEM_ROM_INTERFACE ? &jobs : nullptr;
    }
    static bool submitJob(void* ctx, const rp_job_request* request, rp_job* job, char* error, std::size_t errorCap) {
        auto* self = static_cast<N8Nes*>(ctx);
        if (!self->cart || !request) {
            rp_job_error(error, errorCap, "not connected");
            return false;
        }
        if (!self->reports) {
            rp_job_error(error, errorCap, "this host takes no device jobs");
            return false;
        }
        return self->cart->submitJob(*request, job, self->reports, self, error, errorCap);
    }

    void process(const rp::Block& b) noexcept {
        const double rate = b.transport().sample_rate;
        const bool   connected = cart->connected();
        // `connected` holds 1 or 0 from frame 0: a change at frame 0 on the block it flips, since a stream's start
        // value is the last block's end.
        const double flag = connected ? 1.0 : 0.0;
        if (flag != outputs.connected.queue->start) outputs.connected.push(0, flag);
        if (connected && !wasConnected) volumeSent = std::numeric_limits<double>::quiet_NaN();  // every connect again
        wasConnected = connected;
        applyVolume();
        if (connected && owed.any()) payOwed(rate);
        // The cart's FIFO is fed as the emulated NES's is: the MIDI, then the bytes, each in queue order, an empty
        // message skipped, the bytes' flush flag carried.
        for (const rp_message& m : inputs.midi.messages()) {
            const auto data = inputs.midi.data(m);
            if (!data.empty()) pushMidi(m.frame, data.data(), data.size(), rate);
        }
        for (const rp_message& m : inputs.bytes.messages()) {
            const auto data = inputs.bytes.data(m);
            if (!data.empty()) cart->push(m.frame, data.data(), data.size(), (m.flags & RP_MESSAGE_FLUSH) != 0, rate);
        }
    }

    // The param's latest value, as the emulated NES takes it (clamped, whole percent), to the link when it moved or the
    // link came back: the link keeps the value and its serial thread writes it, so this neither blocks nor allocates.
    void applyVolume() noexcept {
        const double latest = params.expansionVolume.latest();
        if (latest != latest) return;  // NaN reaches a param only over a wire, and moves nothing
        const double percent = static_cast<double>(std::lround(
            std::clamp(latest, static_cast<double>(kExpansionVolumeMin), static_cast<double>(kExpansionVolumeMax))));
        if (percent == volumeSent) return;
        cart->setExpansionVolume(expansionVolumeRegister(percent));
        volumeSent = percent;
    }

    // One MIDI message to the link, keeping the books on its notes. Whether it went is the push's own answer, so the
    // books never read a count that another thread moves.
    void pushMidi(std::uint32_t frame, const std::uint8_t* data, std::size_t size, double rate) noexcept {
        const bool sent = cart->push(frame, data, size, false, rate);
        if (size < 3) return;
        const std::uint8_t kind = data[0] & 0xF0;
        const std::size_t  note = static_cast<std::size_t>(data[0] & 0x0F) * 128 + (data[1] & 0x7F);
        const bool         on = kind == 0x90 && data[2] != 0;
        const bool         off = kind == 0x80 || (kind == 0x90 && data[2] == 0);
        if (on && sent) {
            sounding.set(note);
            owed.reset(note);
        } else if (off) {
            if (sent) {
                sounding.reset(note);
                owed.reset(note);
            } else if (sounding.test(note)) {
                owed.set(note);
            }
        }
    }

    // The note-offs the link dropped for notes the cart is holding, at frame 0, before anything this block carries. One
    // the link still cannot take stays owed for the next block.
    void payOwed(double rate) noexcept {
        for (std::size_t note = 0; note < owed.size(); ++note) {
            if (!owed.test(note)) continue;
            const std::uint8_t off[3] = {static_cast<std::uint8_t>(0x80 | (note / 128)),
                                         static_cast<std::uint8_t>(note % 128), 0};
            if (!cart->push(0, off, sizeof off, false, rate)) return;
            owed.reset(note);
            sounding.reset(note);
        }
    }
};

}  // namespace

const rp_package* n8Package() {
    static const rp_node_type* const kTypes[] = {rp::node_type<N8Nes>()};
    static const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.n8","version":"1.0.0"})",
                                     static_cast<std::uint32_t>(std::size(kTypes)), kTypes};
    return &kPackage;
}

}  // namespace rp::n8

extern "C" const rp_package* rp_n8_package(void) { return rp::n8::n8Package(); }
