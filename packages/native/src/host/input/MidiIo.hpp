#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class RtMidiIn;
class RtMidiOut;

namespace retroplug {

// Cross-platform MIDI I/O for the SDL standalone, wrapping RtMidi (deps/rtmidi). The DPF plugin gets MIDI
// from its DAW host, so this is standalone-only.
//
// Threading: RtMidi delivers input on its OWN callback thread. The callback pushes each message into a
// lock-free SPSC ring; the audio thread drains it via poll() at block start (never allocating). Output is
// sent directly with send(). One producer (RtMidi callback), one consumer (audio thread) — a plain
// seqlock-free SPSC ring is sufficient.
class MidiIo {
public:
    struct Message {
        std::uint64_t             seq = 0;   // monotonic arrival order; a frame offset is derived later
        std::vector<std::uint8_t> bytes;
    };

    MidiIo();
    ~MidiIo();
    MidiIo(const MidiIo&) = delete;
    MidiIo& operator=(const MidiIo&) = delete;

    // Open a virtual "<clientName>" input + output port (so DAWs/controllers connect like the plugin does in
    // a host). Returns false if RtMidi is unavailable (no MIDI system) — the host then runs without MIDI.
    // Hardware-port auto-connect is a later step; a virtual input already receives from anything patched to it.
    bool open(const char* clientName);
    void close();

    bool isOpen() const { return in_ != nullptr; }

    // Drain queued input into `out` (called from the audio thread each block; clears `out` first). Lock-free
    // w.r.t. the RtMidi callback thread.
    void poll(std::vector<Message>& out);

    // Send one short MIDI message out (from the audio thread each block — MI.OUT etc.). No-op if not open.
    void send(const std::uint8_t* data, std::size_t len);

    // Test-only: push a message into the input ring as if RtMidi had received it (for headless verification
    // of the drain path where there's no MIDI hardware). Not used in normal operation.
    void injectForTest(const std::uint8_t* data, std::size_t len) { pushRing(data, len); }

private:
    // The RtMidi input callback (static thunk → pushRing). Runs on RtMidi's thread.
    static void onMidiIn(double timeStamp, std::vector<unsigned char>* message, void* userData);
    void pushRing(const unsigned char* data, std::size_t len);

    // Open every current hardware input port (each its own RtMidiIn → the shared ring), skipping our own
    // virtual port + ALSA "Through" ports. A virtual port alone doesn't receive from hardware controllers.
    void openHardwareInputs(const std::string& clientName);

    std::unique_ptr<RtMidiIn>               in_;    // our virtual input port
    std::vector<std::unique_ptr<RtMidiIn>>  hwIn_;  // hardware input ports (USB-MIDI, etc.)
    std::unique_ptr<RtMidiOut>              out_;    // our virtual output port

    // Fixed-capacity SPSC ring. The producer (callback thread) allocs into a slot's vector; the consumer
    // (audio thread) moves it out. Overflow drops the incoming message (a drained-every-block consumer never
    // realistically fills 256; MIDI loss under an extreme flood is acceptable).
    static constexpr std::size_t kCapacity = 256;
    std::array<Message, kCapacity> ring_;
    std::atomic<std::size_t>       head_{0};  // consumer index (audio thread)
    std::atomic<std::size_t>       tail_{0};  // producer index (callback thread)
    std::uint64_t                  seq_ = 0;  // producer-only monotonic counter
};

}  // namespace retroplug
