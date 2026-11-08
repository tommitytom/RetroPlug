#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class RtMidiIn;
class RtMidiOut;

namespace retroplug {

/** True when an RtMidiIn/RtMidiOut can safely be CONSTRUCTED. On macOS this creates (and then keeps)
 *  the process's CoreMIDI client first, because RtMidi aborts the process instead of reporting a failure
 *  -- see the comment on coreMidiAvailable() in MidiIo.cpp. Elsewhere it is always true. Anything that
 *  builds an RtMidi object must ask this first, including outside this header's own class. */
bool midiSystemUsable();

/** Whether a control-surface SCAN's held edge means anything for this stream: true only where a MIDI input is EXCLUSIVE
 *  (Windows), because there the stream has to let one go for the scan to hear it. An ALSA sequencer input and a CoreMIDI
 *  endpoint are shared - the scan opens its own connection and hears everything the stream does - so closing the stream's
 *  inputs would cost the user their MIDI, and an audio restart around the reopen, for the scan's seconds, and buy nothing.
 *  The pack asks every product the same question (rp_device_host::suspend_hardware_inputs), and the SDL answers it by this
 *  (sdl/main.cpp's onSuspendInputs): a no-op off Windows. It is a function rather than a constant in the SDL so the rule
 *  is held by this header's test on whichever platform builds it. */
inline bool scanSuspendsInputs() {
#if defined(_WIN32)
    return true;
#else
    return false;
#endif
}

// Pure, RtMidi-free port-selection helpers (shared by MidiIo + its unit test). A port is "hardware" when its
// name is neither our own virtual port (contains `clientName`) nor an ALSA MIDI-through (contains "Through").
//
// `reserved` is a port some OTHER owner has claimed exclusively - today the ControllerLink, which holds its
// own in/out pair. Skipping it is not tidiness: a pad press is a NoteOn, and a tracker's MIDI-map translator
// reads a NoteOn as a row launch, so a surface sharing the musical stream fires every launch twice (once
// quantised by the controller app, once raw). Matched exactly, since the claimant picked the name out of
// this very list.

/** Whether `name` passes the allow list `spec` (RP_MIDI_PORT_ALLOW's text): a comma-separated list of SUBSTRINGS a port's
 *  name must contain. An empty spec (or one of separators alone) allows every port, so an ordinary run is unchanged.
 *
 *  The list is TEST-ONLY, and it is what keeps a check off real hardware: tools/run-sdl-controller.sh drives a real
 *  standalone against a fake Launchpad on the ALSA sequencer, and a shared stream on "All Devices" would otherwise open
 *  every MIDI input the machine has, a mixer's and a USB cable's included. The harness names its fakes by their WHOLE port
 *  names, client number included, so an entry it wrote cannot match a real device. The Launchpad pack holds the same rule
 *  for its own ports (packages/launchpad/native/src/launchpad/desktop/MidiPortFilter.hpp): a package may not include this
 *  tree, so the two read the one variable and are kept in step by hand - and in `retroplug-midi-test`, which runs both
 *  bodies over one table of inputs. A product never sets it. */
inline bool midiPortAllowedBy(const std::string& name, const std::string& spec) {
    std::string entry;
    std::string::size_type at = 0;
    bool any = false;
    while (at <= spec.size()) {
        const std::string::size_type comma = spec.find(',', at);
        entry = spec.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
        const std::string::size_type first = entry.find_first_not_of(" \t");
        const std::string::size_type last  = entry.find_last_not_of(" \t");
        entry = first == std::string::npos ? std::string() : entry.substr(first, last - first + 1);
        if (!entry.empty()) {
            any = true;
            if (name.find(entry) != std::string::npos) return true;
        }
        if (comma == std::string::npos) break;
        at = comma + 1;
    }
    return !any;
}

/** `midiPortAllowedBy(name, RP_MIDI_PORT_ALLOW)`. Read on every call: the callers list or open a port, never the audio
 *  thread, so a test can set the filter up and take it down around a case. */
inline bool midiPortAllowed(const std::string& name) {
    const char* spec = std::getenv("RP_MIDI_PORT_ALLOW");
    return midiPortAllowedBy(name, spec ? spec : "");
}

/** Indices of every hardware port in `names` (skipping our own virtual port, ALSA "Through", `reserved`, and any port
 *  the test-only allow list hides - so a filtered process neither lists nor opens it, whichever selection asks). */
inline std::vector<std::size_t> hardwarePortIndices(const std::vector<std::string>& names, const std::string& clientName,
                                                   const std::string& reserved = {}) {
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (names[i].find(clientName) != std::string::npos) continue;  // our own virtual port
        if (names[i].find("Through") != std::string::npos) continue;    // ALSA MIDI-through (no hardware)
        if (!reserved.empty() && names[i] == reserved) continue;         // claimed by a control surface
        if (!midiPortAllowed(names[i])) continue;                        // hidden by RP_MIDI_PORT_ALLOW
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
 *  in every case, and a stream whose inputs are `suspended` (a control-surface scan holds them, on the one platform
 *  where a scan needs it) opens nothing at all.
 *
 *  Pure so the policy itself is testable (retroplug-midi-test) rather than only observable by watching which
 *  RtMidi ports a running standalone happens to open. */
inline std::vector<std::size_t> inputPortsToOpen(const std::vector<std::string>& names, const std::string& clientName,
                                                 const std::string& selection, const std::string& reserved = {},
                                                 bool suspended = false) {
    if (suspended) return {};
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

    // Keep a port pair OUT of this stream because another owner holds it (the control surface's link). Empty
    // releases. The input matters everywhere - a pad NoteOn here is a second, unquantised row launch - and so
    // does the output: notes meant for a synth would land on the surface and light its pads at random. On
    // Windows both are also exclusive, so a port this stream holds could not be claimed at all.
    //
    // Applied immediately if open() has run, and only ports whose open state actually changes are reopened.
    // The host stops audio around it when reservationChanges() says something will move (send() mirrors to
    // the hardware output from the audio thread), and skips that dance when nothing would.
    void setReserved(const std::string& input, const std::string& output);
    bool reservationChanges(const std::string& input, const std::string& output) const;
    const std::string& reservedInput() const { return reservedIn_; }
    const std::string& reservedOutput() const { return reservedOut_; }

    // Close every hardware INPUT for the duration of a control-surface SCAN, without disturbing the user's
    // selection (resuming re-applies it). Not the same lever as setReserved, which names ONE pair a surface
    // holds for as long as it is connected: a scan needs every input, briefly. Only Windows needs it - a
    // Windows MIDI input is exclusive, so a scan could not hear the interface the user picked as their input,
    // which is exactly the rig a TRS-attached Launchpad is on. The OUTPUT is left alone on purpose: it is the
    // user's synth, and cutting it mid-note would hang whatever was held. Applied immediately if open() has
    // run; suspensionChanges() says whether that closes anything (so whether the host must stop audio). A
    // product reaches this only where scanSuspendsInputs() says the edge is real, which is Windows alone.
    void setHardwareInputsSuspended(bool suspended);
    bool suspensionChanges(bool suspended) const;
    bool hardwareInputsSuspended() const { return inputsSuspended_; }

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
    std::string                             reservedOut_;  // ditto, the pair's output
    bool                                    inputsSuspended_ = false;  // a scan is holding the inputs

    // The input ports / output port this stream WOULD open under the given reservation (and, for inputs,
    // suspension). What the *Changes() predicates compare, so a change touching nothing we hold costs no
    // reopen - and so no audio restart.
    std::vector<std::string> inputsFor(const std::string& reservedIn, bool suspended) const;
    std::string              outputFor(const std::string& reservedOut) const;

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
