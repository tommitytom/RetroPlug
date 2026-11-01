#include "host/graph/N8Nodes.hpp"

#include <algorithm>
#include <bitset>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>

#include "graph/node.hpp"
#include "host/devices/DeviceLinks.hpp"
#include "host/engine/CapabilityJobs.hpp"
#include "host/graph/HostServices.hpp"
#include "host/graph/SystemNodes.hpp"  // kExpansionVolume*

namespace rp::host {

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
// no cart, or with the cart unplugged, the link reads disconnected and takes every push nowhere, and nothing in the
// graph changes when the cart comes or goes.
struct N8Nes {
    static constexpr const char* id = "rp.n8.nes";
    static constexpr const char* name = "NES (N8 cart)";
    static constexpr const char* category = "device";
    // Its device runs threads of its own (the link's serial thread, which a job's transfer runs on), so a target with
    // none (the Pico) refuses the type.
    static constexpr bool threads = true;
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

    const HostDevices* devices = nullptr;
    CartLink*          cart = nullptr;
    // What the node presents as rp.system.sram/1 and rp.system.rom/1: every job goes to the link, the node its owner.
    DeviceJobs jobs{this, &submitJob};
    // The volume the link was last given, in whole percent (NaN: none yet, so the first block gives it), and whether
    // the link read connected in the last block, so a reconnect gives it again.
    double volumeSent = std::numeric_limits<double>::quiet_NaN();
    bool   wasConnected = false;
    // The notes the cart was sent a note-on for and no note-off yet (a channel's 128 notes a channel, 16 channels), and
    // of those, the ones whose note-off the link dropped (it was down, or a job held it). An owed note-off goes out at
    // the start of the next block the link reads connected in: a pause drops what is pushed while it lasts, and a
    // note-off lost there would leave the console holding the note for good.
    std::bitset<16 * 128> sounding, owed;

    void bind_host(const rp_host* h) noexcept {
        devices = static_cast<const HostDevices*>(h->get_interface(h, RP_HOST_DEVICES_INTERFACE));
    }
    bool activate(double, std::uint32_t) {
        if (!devices) return false;
        // A reactivate runs activate again on this instance: the handle already held is kept, one acquire to one
        // release.
        if (!cart) cart = devices->acquireCart(devices->ctx, kCartDeviceName);
        return cart != nullptr;
    }
    ~N8Nes() {
        if (!devices || !cart) return;
        // A job this node submitted that is still running fails now, and the link reports into its record no more, so
        // the engine may free the record; the device stops at its next safe point.
        cart->abandonJobs(this);
        devices->releaseCart(devices->ctx, cart);
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

    const void* get_interface(std::string_view id) noexcept {
        if (id == RP_STAGE_INTERFACE) return &staged;
        return id == RP_SYSTEM_SRAM_INTERFACE || id == RP_SYSTEM_ROM_INTERFACE ? &jobs : nullptr;
    }
    static bool submitJob(void* ctx, const JobRequest& request, Job& job, std::string* error) {
        auto* self = static_cast<N8Nes*>(ctx);
        if (!self->cart) {
            if (error) *error = "not connected";
            return false;
        }
        return self->cart->submitJob(request, job, self, error);
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

    // One MIDI message to the link, keeping the books on its notes. Whether the push went out is read off the link's
    // own drop count around it, which only the block thread moves.
    void pushMidi(std::uint32_t frame, const std::uint8_t* data, std::size_t size, double rate) noexcept {
        const std::uint64_t before = cart->dropped();
        cart->push(frame, data, size, false, rate);
        const bool sent = cart->dropped() == before;
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
            const std::uint64_t before = cart->dropped();
            cart->push(0, off, sizeof off, false, rate);
            if (cart->dropped() != before) return;
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

}  // namespace rp::host
