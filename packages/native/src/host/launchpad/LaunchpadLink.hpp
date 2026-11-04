#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "launchpad/SurfaceLink.hpp"
#include "transport/SpscRing.hpp"

namespace retroplug {

// A bidirectional MIDI endpoint owned EXCLUSIVELY by one link - this subsystem's transport, as rp::n8::Transport is
// the N8's. Injected rather than concrete so LaunchpadLink depends on no MIDI library and is testable with nothing
// plugged in; RtMidiPort.cpp is the only translation unit here that includes RtMidi.h.
class IMidiPort {
public:
    // Delivered on the MIDI backend's OWN callback thread, once per received message.
    using Receiver = std::function<void(const std::uint8_t*, std::size_t)>;

    virtual ~IMidiPort() = default;
    virtual void send(const std::uint8_t* data, std::size_t n) = 0;
};

// Host-side link to a control surface (a Novation Launchpad) on its own in/out pair, kept out of the shared
// musical MIDI stream. Pad presses arrive on the backend's callback thread and are drained by the audio thread; LED
// traffic the controller session produced during processBlock is pushed by the audio thread and written out by the
// host's MAIN loop.
//
// Three decisions worth knowing before touching anything here:
//
//   1. **Both rings are POD, because one end of each is the audio thread.** Device -> audio keeps one message per
//      fixed slot (rp::launchpad::DeviceMessage) in a single allocation made at construction: the producer is the
//      backend's callback thread, where allocating would be that thread's own problem, and the audio thread copies
//      bytes out and frees nothing (a vector per slot would be freed there by whoever dropped it). Audio -> main is
//      an SpscRing of fixed slots for the same reason.
//   2. **LEDs leave on the main loop, not the audio thread.** A frame of latency is invisible on a light,
//      and it keeps a possible 538-byte USB write off the RT path without adding a thread to own.
//   3. **The farewell is an opaque blob.** Programmer mode locks the device's front panel, so the message
//      that releases it has to be replayed on a path where the audio thread may already be stopped. TS hands
//      the bytes down at connect; this class replays them in disconnect() and in its destructor, and never
//      parses them. Native learning the protocol is exactly what that avoids.
class LaunchpadLink {
public:
    // Opens the named input + output ports and routes received messages to `receiver`. May throw if either
    // port cannot be opened; connect() catches it. Port ENUMERATION for the picker is the caller's job
    // (LaunchpadHost takes an injected lister over MidiIo).
    using PortFactory = std::function<std::unique_ptr<IMidiPort>(
        const std::string& inName, const std::string& outName, IMidiPort::Receiver receiver)>;

    // The worst case a controller app can produce in one message: a bulk LED SysEx carrying the Pro MK3's
    // maximum 106 colour specs at the RGB form's 5 bytes each, plus the 8-byte SysEx frame = 538. Rounded
    // up; a longer message is dropped and counted rather than truncated into nonsense. The same bound sizes a
    // received slot, in either direction: the device's own replies are far shorter.
    static constexpr std::size_t kMaxOutMessage = rp::launchpad::kMaxDeviceMessage;

    // The messages the link holds for the block thread, in slots of one kMaxDeviceMessage: 256 x 562 bytes is 140
    // KiB, and a host's link is a member of its own state (the SDL standalone's AppState is a local of main, whose
    // thread Windows gives 1 MiB of stack), so the ring is allocated once when the link is built and never rides a
    // stack. Public so a case that fills it counts its own capacity rather than spelling the number.
    static constexpr std::size_t kInCapacity = 256;

    explicit LaunchpadLink(PortFactory factory);
    ~LaunchpadLink();
    LaunchpadLink(const LaunchpadLink&)            = delete;
    LaunchpadLink& operator=(const LaunchpadLink&) = delete;

    // --- control (UI / main thread) ---
    bool        connect(const std::string& inName, const std::string& outName);
    void        disconnect();
    bool        isConnected() const { return connected_.load(std::memory_order_acquire); }
    std::string lastError() const { return error_; }
    /** The opaque bytes replayed when the device is given back. Set before connecting; kept across
     *  reconnects, so a host that sets it once stays safe. */
    void setFarewell(std::vector<std::uint8_t> bytes) { farewell_ = std::move(bytes); }

    /** Write everything the audio thread queued to the device. Call once per host frame. */
    void pump();

    // --- audio thread ---
    /** Copy the messages received since the last call into `out`, up to `capacity`, and answer how many. This is the
     *  only drain, and the one the block thread uses (the device link forwards it): the messages stay in the link's
     *  own fixed slots until they are copied, so nothing is allocated or freed here, and one the caller has no room
     *  for waits for its next call. The answer is min(queued, capacity), fewer only when nothing is left, which is
     *  what lets a node that drains in batches stop on a short answer. A message carries no arrival frame: the drain's
     *  caller places it at frame 0. */
    std::size_t drainInput(rp::launchpad::DeviceMessage* out, std::size_t capacity);
    /** Queue one message for the device. Lock-free, never blocks; drops (and counts) when the ring is full,
     *  the message is oversized, or nothing is connected. */
    void pushOutput(const std::uint8_t* data, std::size_t n);

    // --- status ---
    std::uint64_t messagesSent() const { return sent_.load(std::memory_order_relaxed); }
    // The pushes that lost their bytes while the device was attached: the send ring was full, or the message was wider
    // than a slot. What a session pushed while nothing was connected is counted apart (messagesDetached): the device
    // was never going to take it, and the session repaints, so it is no frame the surface is missing. A message the
    // device sent that the receive ring had no room for is counted apart too (messagesInboundDropped): that is a press
    // the audio side missed, not a frame the device is missing. The handle a node holds (LaunchpadSurfaceLink::dropped)
    // answers the first two; the status line's `dropped` is messagesDropped alone, it shows messagesDetached as
    // `detached`, and it shows the inbound loss nowhere.
    std::uint64_t messagesDropped() const { return dropped_.load(std::memory_order_relaxed); }
    std::uint64_t messagesDetached() const { return detached_.load(std::memory_order_relaxed); }
    std::uint64_t messagesInboundDropped() const { return inboundDropped_.load(std::memory_order_relaxed); }

private:
    void pushInputRing(const std::uint8_t* data, std::size_t n);  // backend callback thread

    struct OutMessage {
        std::uint16_t len                 = 0;
        std::uint8_t  data[kMaxOutMessage] = {0};
    };

    PortFactory                  factory_;
    std::unique_ptr<IMidiPort>   port_;      // owned; reset() cancels the callback before we return
    std::vector<std::uint8_t>    farewell_;  // opaque, set by TS
    std::string                  error_;     // main thread only (connect writes, getConfig reads)

    std::atomic<bool>          connected_{false};
    std::atomic<std::uint64_t> sent_{0};
    std::atomic<std::uint64_t> dropped_{0};
    std::atomic<std::uint64_t> detached_{0};        // pushed while nothing was connected, counted apart from dropped_
    std::atomic<std::uint64_t> inboundDropped_{0};  // received messages the ring had no room for, counted apart

    // device -> audio. Fixed-capacity SPSC ring of fixed slots: the producer (backend callback) copies the bytes into
    // a slot, the consumer (audio thread) copies them out, so neither end of the block thread's path allocates or
    // frees. Overflow, and a message wider than a slot, drop and count in inboundDropped_.
    std::unique_ptr<rp::launchpad::DeviceMessage[]> inRing_;
    std::atomic<std::size_t>         inHead_{0};  // consumer index (audio thread)
    std::atomic<std::size_t>         inTail_{0};  // producer index (callback thread)

    // audio -> main. POD slots: the producer is the audio thread, so nothing here may allocate.
    SpscRing<OutMessage, 32> outRing_;
};

}  // namespace retroplug
