#pragma once

// The device links a node binds by name through rp.host.devices/1 (docs/modular-graph-phase-c5-design.md sections 3 to
// 5). The hosts own the devices - the serial port, the MIDI ports, reconnecting, the farewell - and a link is the whole
// of what a node may do with one: the streaming rings, the connected state and, for a cart, the expansion-audio
// register and the capability jobs it runs on its own thread. The two interfaces live here, beside host/n8 and
// host/launchpad rather than inside either, because a node, a host's adapter and the tests all bind to them, and
// neither device's own headers are a node's business.
//
// The link outlives every plan whose nodes hold a handle: a host tears its plans down while its links are alive, and a
// reactivate keeps the handle across it. So a node may use its handle in destroy, not only give it back: a job it
// cancels, a queue it flushes, a connected() or dropped() it reads.

#include <cstddef>
#include <cstdint>
#include <string>

#include "host/engine/CapabilityJobs.hpp"  // JobRequest, Job

namespace rp::host {

// What a node asks for. A host installs its link under the same name; a host that owns no such device answers with a
// link that is never connected, so a document plans and runs the same in every host (design section 3).
inline constexpr const char* kCartDeviceName    = "n8";
inline constexpr const char* kSurfaceDeviceName = "launchpad";

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

// A cart in a real console (rp.n8.nes): the same inputs an emulated NES takes, pushed at the cart's FIFO.
class CartLink {
public:
    virtual ~CartLink() = default;

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
    // [control] Run a capability job on the device (docs/modular-graph-phase-c5-design.md section 6): the node that
    // presents rp.system.sram/1 or rp.system.rom/1 hands each job it is asked for here, with itself as `owner`. True
    // when the device took it into `job` (it finishes it on its own thread, or at once); false with the reason in
    // *error, leaving `job` untouched: one job at a time per device ("busy: ..."), and none while the device is not
    // attached ("not connected"), which is all a link with no device ever answers. While a job runs the link reads
    // disconnected and drops (and counts) what is pushed, and the job completes once streaming has resumed.
    virtual bool submitJob(const JobRequest& request, Job& job, const void* owner, std::string* error) {
        (void)request;
        (void)job;
        (void)owner;
        if (error) *error = "not connected";
        return false;
    }
    // [control] The node `owner` is leaving the plan: a job it submitted that is still running fails now (Removed) and
    // the device reports into its record no more, stopping at the next point that leaves the device coherent. Nothing
    // when it holds no job.
    virtual void abandonJobs(const void* owner) { (void)owner; }

    // There is deliberately no error string here: why a cart would not open is the host UI's (it reads N8Host's
    // lastError through Settings > N8), not something a document's node can act on.
    //
    // [control] A node took this handle, in its activate, and gave it back, in its destroy. The registry forwards
    // both: the cart adapter counts the nodes holding it and puts the expansion volume back to AUTO on the last
    // release, the surface adapter implements neither. Acquire runs more than once for one node when a sample-rate
    // change or a block growth reactivates the plan (GraphHost::reactivate), so a node that already holds its handles
    // keeps them across deactivate / activate and releases them once, in destroy: a link that arms or pauses something
    // in acquire must see one release for one acquire.
    virtual void acquire() {}
    virtual void release() {}
};

// A control surface (rp.launchpad.surface-in / -out): the device's own MIDI in both directions, kept out of the
// musical stream.
class SurfaceLink {
public:
    virtual ~SurfaceLink() = default;

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

    // As CartLink's: a node keeps a handle it holds across a reactivate, and releases it once in destroy.
    virtual void acquire() {}
    virtual void release() {}
};

}  // namespace rp::host
