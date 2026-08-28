#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class RtMidiIn;
class RtMidiOut;

namespace retroplug {

// Pure, RtMidi-free port-selection helpers (shared by MidiIo + its unit test). A port is "hardware" when its
// name is neither our own virtual port (contains `clientName`) nor an ALSA MIDI-through (contains "Through").
//
// `reserved` is a port some OTHER owner has claimed exclusively - today the Launchpad link, which holds its
// own in/out pair. Skipping it is not tidiness: a pad press is a NoteOn, and a tracker's MIDI-map translator
// reads a NoteOn as a row launch, so a surface sharing the musical stream fires every launch twice (once
// quantised by the controller app, once raw). Matched exactly, since the claimant picked the name out of
// this very list.

/** Indices of every hardware port in `names` (skipping our own virtual port, ALSA "Through", and `reserved`). */
inline std::vector<std::size_t> hardwarePortIndices(const std::vector<std::string>& names, const std::string& clientName,
                                                   const std::string& reserved = {}) {
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (names[i].find(clientName) != std::string::npos) continue;  // our own virtual port
        if (names[i].find("Through") != std::string::npos) continue;    // ALSA MIDI-through (no hardware)
        if (!reserved.empty() && names[i] == reserved) continue;         // claimed by a control surface
        out.push_back(i);
    }
    return out;
}

/** Index of the hardware port named exactly `selection`, or none (empty selection / no match / a skipped port). */
inline std::optional<std::size_t> matchPortIndex(const std::vector<std::string>& names, const std::string& clientName,
                                                 const std::string& selection, const std::string& reserved = {}) {
    if (selection.empty()) return std::nullopt;
    for (std::size_t i : hardwarePortIndices(names, clientName, reserved))
        if (names[i] == selection) return i;
    return std::nullopt;
}

/** The input selection meaning "every hardware port at once". An explicit CHOICE, not the default: opening
 *  every device turns out to be a surprising thing to do by default, because a device you plugged in for one
 *  purpose is then also a MIDI source - a control surface's free-running clock ends up driving the host
 *  tempo, a controller's mixer ports send notes at the cart. So the empty selection is "None" (matching the
 *  output side), and this is what a user picks when they want the old behaviour back. */
inline constexpr const char* kAllInputs = "*";

/** Indices of the input ports to open for `selection`: none when empty (the default), every hardware port
 *  for kAllInputs, else just the one whose name matches. A port `reserved` by a control surface is skipped
 *  in every case.
 *
 *  Pure so the policy itself is testable (retroplug-midi-test) rather than only observable by watching which
 *  RtMidi ports a running standalone happens to open. */
inline std::vector<std::size_t> inputPortsToOpen(const std::vector<std::string>& names, const std::string& clientName,
                                                 const std::string& selection, const std::string& reserved = {}) {
    if (selection.empty()) return {};
    if (selection == kAllInputs) return hardwarePortIndices(names, clientName, reserved);
    if (auto idx = matchPortIndex(names, clientName, selection, reserved)) return { *idx };
    return {};
}

/** Pull the System Real-Time bytes (0xF8..0xFF: clock, start/continue/stop, reset) out of one incoming
 *  message, handing each to `onRealtime` in order, and leave `bytes` holding whatever else was there.
 *
 *  Real-time bytes are a stream WITHIN the stream: the spec lets them appear at any byte boundary,
 *  including between the data bytes of another message, and a transport is free to hand several events
 *  over as one buffer. Matching only a one-byte message therefore both MISSES clock (a batched pair reads
 *  as no clock at all - a tempo estimate built on those pulses comes out a whole ratio slow) and corrupts
 *  what is left (a clock byte staged into the middle of a NoteOn goes down a cart's link port as noise).
 *
 *  Shrinks in place: this runs per message on the audio thread, so it must not allocate. */
template <typename F>
inline void extractRealtime(std::vector<std::uint8_t>& bytes, F&& onRealtime) {
    std::size_t write = 0;
    for (std::size_t read = 0; read < bytes.size(); ++read) {
        const std::uint8_t b = bytes[read];
        if (b >= 0xF8) onRealtime(b);
        else bytes[write++] = b;
    }
    bytes.resize(write);
}

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
    // a host), then apply the current input/output device selections (see setInput/OutputSelection). Returns
    // false if RtMidi is unavailable (no MIDI system) — the host then runs without MIDI.
    bool open(const char* clientName);
    void close();

    bool isOpen() const { return in_ != nullptr; }

    // Enumerate the available hardware ports by name (skipping our own virtual port + ALSA "Through"), without
    // opening them — for the Settings > MIDI device pickers. Returns {} if there's no MIDI system.
    std::vector<std::string> listInputs() const;
    std::vector<std::string> listOutputs() const;

    // Choose which hardware device to use, by port name; applied immediately if open() has run. An empty
    // selection means "None" for BOTH directions - the virtual ports only, and the default. `kAllInputs`
    // ("*") is the explicit "every hardware input" choice. A name that isn't currently present is remembered
    // and re-applied on the next open()/reconnect. Persisted by the host (midi.json).
    //
    // Input used to default to every device, which is a surprising amount of behaviour to get without asking
    // for it: anything plugged in becomes a MIDI source, so a control surface's free-running clock drives the
    // host tempo and a controller's mixer ports send notes at the cart. Note the virtual "<client> In" port
    // is ALWAYS open regardless, so a DAW or an aconnect user is unaffected by the default - only physical
    // devices became opt-in.
    void setInputSelection(const std::string& name);
    void setOutputSelection(const std::string& name);
    /** Messages dropped because the ring was full since startup — always 0 in a healthy run. Non-zero
     *  means the audio thread stopped draining, and any transport clock in what was lost is simply gone,
     *  which downstream reads as a tempo that is too slow. Reported by the RETROPLUG_DEBUG_AUDIO dump. */
    std::uint64_t droppedCount() const { return dropped_.load(std::memory_order_relaxed); }

    const std::string& inputSelection() const { return selectedIn_; }
    const std::string& outputSelection() const { return selectedOut_; }

    // Keep an input port OUT of this stream because another owner holds it exclusively (the Launchpad link).
    // Empty releases the reservation. Applied immediately if open() has run, so the host must stop audio
    // around this exactly as it does for setInputSelection.
    void setReservedInput(const std::string& name);
    const std::string& reservedInput() const { return reservedIn_; }

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

    // Connect the selected hardware input(s) — every hardware port when selectedIn_ is empty ("All Devices"),
    // else only the one whose name matches. Each opened port gets its own RtMidiIn feeding the shared ring.
    void openHardwareInputs();
    // Open (or clear) the selected hardware output — the one whose name matches selectedOut_, or none when it's
    // empty ("None"). send() mirrors to it alongside the always-open virtual output.
    void openHardwareOutput();

    std::string                             clientName_ = "RetroPlug"; // set by open(); used for port skip + reconnect
    std::string                             selectedIn_;   // "" = None (virtual input only); "*" = every device
    std::string                             selectedOut_;  // "" = None (virtual output only)
    std::string                             reservedIn_;   // "" = nothing claimed by a control surface

    std::unique_ptr<RtMidiIn>               in_;    // our virtual input port
    std::vector<std::unique_ptr<RtMidiIn>>  hwIn_;  // hardware input ports (USB-MIDI, etc.)
    std::unique_ptr<RtMidiOut>              out_;    // our virtual output port
    std::unique_ptr<RtMidiOut>              hwOut_;  // the selected hardware output port (optional)

    // Fixed-capacity SPSC ring. The producer (callback thread) allocs into a slot's vector; the consumer
    // (audio thread) moves it out. Overflow drops the incoming message (a drained-every-block consumer never
    // realistically fills 256; MIDI loss under an extreme flood is acceptable).
    static constexpr std::size_t kCapacity = 256;
    std::array<Message, kCapacity> ring_;
    std::atomic<std::size_t>       head_{0};  // consumer index (audio thread)
    std::atomic<std::size_t>       tail_{0};  // producer index (callback thread)
    std::uint64_t                  seq_ = 0;  // producer-only monotonic counter
    // Messages the ring had no room for. A full ring means the audio thread stopped draining (a stall, a
    // device that never started), and the symptom downstream is silent: a tempo estimate built on the
    // pulses that DID land reads slow, in proportion to what was lost. Counted so it can be reported
    // rather than guessed at.
    std::atomic<std::uint64_t>     dropped_{0};
    bool                           log_ = false;  // RETROPLUG_MIDI_LOG: dump in + out bytes to stderr
};

}  // namespace retroplug
