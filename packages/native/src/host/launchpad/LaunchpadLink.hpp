#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "transport/SpscRing.hpp"

namespace retroplug {

// A bidirectional MIDI endpoint owned EXCLUSIVELY by one link - this subsystem's ISerialPort. Injected
// rather than concrete so LaunchpadLink depends on no MIDI library and is testable with nothing plugged in;
// RtMidiPort.cpp is the only translation unit here that includes RtMidi.h.
class IMidiPort {
public:
    // Delivered on the MIDI backend's OWN callback thread, once per received message.
    using Receiver = std::function<void(const std::uint8_t*, std::size_t)>;

    virtual ~IMidiPort() = default;
    virtual void send(const std::uint8_t* data, std::size_t n) = 0;
};

// Host-side link to a control surface (a Novation Launchpad) on its own in/out pair, kept out of the shared
// musical MIDI stream. Pad presses arrive on the backend's callback thread and are drained by the audio
// thread into the Engine's `controllerIn`; LED traffic the kernel produced during processBlock is pushed by
// the audio thread and written out by the host's MAIN loop.
//
// Three decisions worth knowing before touching anything here:
//
//   1. **The two rings are deliberately different**, because their producers are. Device -> audio copies
//      MidiIo's ring (a std::vector per slot): the producer is the backend's callback thread, where
//      allocating is that thread's own problem, and the audio thread just MOVES the vector out. Audio ->
//      main is a POD SpscRing, because there the producer IS the audio thread and a vector would allocate.
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

    /** One message in either direction. */
    using Message = std::vector<std::uint8_t>;

    // The worst case a controller app can produce in one message: a bulk LED SysEx carrying the Pro MK3's
    // maximum 106 colour specs at the RGB form's 5 bytes each, plus the 8-byte SysEx frame = 538. Rounded
    // up; a longer message is dropped and counted rather than truncated into nonsense.
    static constexpr std::size_t kMaxOutMessage = 560;

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
    /** Move the messages received since the last call into `out` (cleared first). Allocation-free: the
     *  vectors were built on the backend's callback thread and are moved, not copied. */
    void drainInput(std::vector<Message>& out);
    /** Queue one message for the device. Lock-free, never blocks; drops (and counts) when the ring is full,
     *  the message is oversized, or nothing is connected. */
    void pushOutput(const std::uint8_t* data, std::size_t n);

    // --- status ---
    std::uint64_t messagesSent() const { return sent_.load(std::memory_order_relaxed); }
    std::uint64_t messagesDropped() const { return dropped_.load(std::memory_order_relaxed); }

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

    // device -> audio. Fixed-capacity SPSC; the producer (backend callback) allocates into a slot's vector,
    // the consumer (audio thread) moves it out. Overflow drops the incoming message.
    static constexpr std::size_t kInCapacity = 256;
    std::array<Message, kInCapacity> inRing_;
    std::atomic<std::size_t>         inHead_{0};  // consumer index (audio thread)
    std::atomic<std::size_t>         inTail_{0};  // producer index (callback thread)

    // audio -> main. POD slots: the producer is the audio thread, so nothing here may allocate.
    SpscRing<OutMessage, 32> outRing_;
};

}  // namespace retroplug
