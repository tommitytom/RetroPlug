#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "launchpad/SurfaceLink.hpp"
#include "launchpad/desktop/MidiPort.hpp"
#include "transport/SpscRing.hpp"

namespace rp::launchpad {

// Host-side link to a control surface on its own in/out pair, kept out of the shared musical MIDI stream, and the
// rp::launchpad::SurfaceLink a plan's surface nodes bind (docs/modular-graph-phase-f3-design.md section 3). Messages
// from the device arrive on the backend's callback thread and are drained by the audio thread; what the session
// produced for the device during processBlock is pushed by the audio thread and written out by the link's own WRITER
// thread.
//
// This is the transport and nothing else: the protocol, the choice of ports and their persistence all live in TS
// (packages/launchpad/ts, its ui/launchpadDevices.ts). Four decisions worth knowing before touching anything
// here:
//
//   1. **Both rings are fixed slots, because the audio thread is one end of each.** Device -> audio keeps one
//      DeviceMessage per slot, tagged with the claim's generation: the producer is the callback thread, and the audio
//      thread copies bytes out and frees nothing. Audio -> writer is a POD SpscRing of the same bound, because there
//      the producer IS the audio thread.
//   2. **Every inbound slot carries the claim it belongs to** (its generation), and the audio thread is the ONLY
//      consumer of the ring. A disconnect never rewinds it from the control thread - that would make the UI thread a
//      second consumer, and the race a UI-thread drain of a vector-slot ring was (the branch's c60a3cf86). Instead
//      whoever consumes a slot drops it when it belongs to an earlier claim.
//   3. **Output leaves on a writer thread, not the audio thread or the UI's frame.** A frame of latency is invisible on
//      a light, and the write can block: WinMM waits for a SysEx to leave the wire, which is ~170 ms for a 538-byte
//      repaint over DIN - a frozen UI if the frame loop did it. So the UI's own messages (send: the bytes that let a
//      device go while the pair stays claimed) go into a queue of their OWN, which the writer drains; the UI thread never
//      writes the port, and never waits behind the session's queue.
//   4. **The close bytes are an opaque blob.** A device can need telling that it is being let go (a Launchpad in
//      Programmer mode has its own front panel locked until it is), and that has to happen on paths where the JS
//      runtimes may already be gone. TS hands the bytes over with connect(); this class replays them in disconnect()
//      and in its destructor, and never parses them.
class ControllerLink final : public SurfaceLink {
public:
    // The messages the receive ring holds, one DeviceMessage each: 256 slots is a burst of a pad's presses, a scan's
    // replies and a bulk inquiry, with room to spare.
    static constexpr std::size_t kInCapacity = 256;
    // The audio thread's send ring: a whole repaint is one message, and a frame's worth of LED traffic is a handful.
    static constexpr std::size_t kOutCapacity = 32;

    /** `writerThread` false keeps output on the caller's pump() - what the unit tests use, so a write can be asserted
     *  on deterministically. The provider runs the thread. */
    explicit ControllerLink(MidiPortOpener opener, bool writerThread = true);
    ~ControllerLink();  // not virtual: a SurfaceLink is deleted through the host, which owns it, never through this
    ControllerLink(const ControllerLink&)            = delete;
    ControllerLink& operator=(const ControllerLink&) = delete;

    // --- control (UI / main thread) ---
    /** Claim the named pair. Releases any pair already held first (replaying ITS close bytes). On failure nothing is
     *  held and lastError() says why. */
    bool connect(const std::string& inName, const std::string& outName, std::vector<std::uint8_t> closeBytes);
    void disconnect();
    bool isConnected() const { return connected_.load(std::memory_order_acquire); }
    /** The claim's number: a new one on every successful connect, 0 while nothing is held. What the surface nodes report
     *  as the device's generation, so a pair swapped faster than a block still reads as a NEW device. */
    std::uint32_t generation() const override {
        return connected_.load(std::memory_order_acquire) ? generation_.load(std::memory_order_acquire) : 0;
    }
    const std::string& inputName() const { return inName_; }
    const std::string& outputName() const { return outName_; }
    std::string        lastError() const;

    /** Wake the writer: call once per host frame (never blocks). Without a writer thread this writes queued output
     *  here instead. */
    void pump();

    /** Hand `bytes` to the device (UI thread), after whatever the session had already queued. For a message that is not
     *  the session's - letting the device go when the app driving it is switched off, while the pair stays claimed.
     *  Returns as soon as the message is QUEUED: the writer thread writes it, so the UI never waits for a write in flight
     *  or for the queue in front of it (a WinMM bulk SysEx blocks ~170 ms). False when nothing is connected, the bytes are
     *  oversized, or the writer's control queue is full. Without a writer thread (the unit tests) this thread IS the
     *  writer and writes them now, so a case can assert on the device's record. */
    bool send(const std::uint8_t* data, std::size_t n);

    // --- audio thread ---
    /** Copy the messages received under the CURRENT claim since the last call into `out`, at most `capacity`, and answer
     *  how many: fewer than `capacity` only when the ring has nothing left. A slot from an earlier claim is skipped and
     *  freed in place. Allocation-free. */
    std::size_t drainInput(DeviceMessage* out, std::size_t capacity) override;
    /** Queue one message for the device. Lock-free, never blocks; drops (and counts) when the ring is full, the
     *  message is oversized, or nothing is connected. */
    void pushOutput(const std::uint8_t* data, std::size_t size) override;

    // --- status ---
    std::uint64_t messagesSent() const { return sent_.load(std::memory_order_relaxed); }
    /** Lost to a full ring, an oversized message, or a push while nothing was attached, since connect. A change in this
     *  between blocks means the device is missing LED updates the session believes it sent. */
    std::uint64_t dropped() const override { return dropped_.load(std::memory_order_relaxed); }

private:
    void pushInputRing(const std::uint8_t* data, std::size_t n, std::uint32_t gen);  // backend callback thread
    void writeQueued();  // the send ring's consumer: the writer thread, or pump() without one
    void writeControl();  // the UI's own messages, drained by the writer after the send ring
    void wakeWriter();
    void startWriter();
    void stopWriter();

    struct InSlot {
        DeviceMessage msg;
        std::uint32_t gen   = 0;  // the claim this arrived under
    };
    struct OutMessage {
        std::uint32_t gen                  = 0;  // the claim the session produced it for
        std::uint16_t len                  = 0;
        std::uint8_t  data[kMaxDeviceMessage] = {0};
    };
    struct ControlMessage {
        std::uint32_t             gen = 0;  // the claim the UI produced it for
        std::vector<std::uint8_t> bytes;
    };

    MidiPortOpener             opener_;
    bool                       useWriter_;
    std::unique_ptr<IMidiPort> out_;
    std::unique_ptr<IMidiPort> in_;          // reset() cancels the callback before we return
    std::vector<std::uint8_t>  closeBytes_;  // opaque, from TS
    std::string                inName_, outName_;
    std::string                error_;       // main thread only (connect writes, the status reads)
    std::uint32_t              nextGen_ = 0; // main thread only

    std::atomic<bool>          connected_{false};
    std::atomic<std::uint32_t> generation_{0};
    std::atomic<std::uint64_t> sent_{0};
    std::atomic<std::uint64_t> dropped_{0};

    // device -> audio. Fixed slots, ONE producer (the current port's callback: the previous port's is cancelled before
    // the next opens) and ONE consumer (the audio thread). Overflow drops and counts.
    std::array<InSlot, kInCapacity> inRing_{};
    std::atomic<std::size_t>        inHead_{0};  // consumer index (audio thread ONLY)
    std::atomic<std::size_t>        inTail_{0};  // producer index (callback thread)

    // audio -> writer. POD slots: the producer is the audio thread, so nothing here may allocate.
    SpscRing<OutMessage, kOutCapacity> outRing_;

    // UI -> writer: what send() hands over. Its own queue, not the send ring, because the ring's consumer seat is held by
    // whoever is draining it for as long as the drain takes, and a send() that waited for that seat would wait behind
    // every message queued before it (the UI freeze the writer exists to avoid). Bounded, because a UI that queued without
    // limit would grow on a device that never drains; the writer swaps the whole batch out under ctlMutex_, so the UI's
    // hold on it is a push.
    static constexpr std::size_t       kControlCapacity = 16;
    std::mutex                         ctlMutex_;
    std::vector<ControlMessage>        control_;      // guarded by ctlMutex_

    // The writer thread and what it waits on. popMutex_ is the send ring's single-consumer seat (the writer's drain, or
    // disconnect()'s flush), and sendMutex_ serialises every write to out_ (the writer, the UI's send without a writer,
    // and disconnect's flush), so the port is never written from two threads at once.
    std::mutex              popMutex_;
    std::mutex              sendMutex_;
    std::mutex              wakeMutex_;
    std::condition_variable wake_;
    bool                    wakeFlag_ = false;  // guarded by wakeMutex_
    std::atomic<bool>       writerRun_{false};
    std::thread             writer_;  // LAST: joined before the members it uses go away
};

}  // namespace rp::launchpad
