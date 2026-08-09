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

/** Indices of every hardware port in `names` (skipping our own virtual port + ALSA "Through"). */
inline std::vector<std::size_t> hardwarePortIndices(const std::vector<std::string>& names, const std::string& clientName) {
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (names[i].find(clientName) != std::string::npos) continue;  // our own virtual port
        if (names[i].find("Through") != std::string::npos) continue;    // ALSA MIDI-through (no hardware)
        out.push_back(i);
    }
    return out;
}

/** Index of the hardware port named exactly `selection`, or none (empty selection / no match / a skipped port). */
inline std::optional<std::size_t> matchPortIndex(const std::vector<std::string>& names, const std::string& clientName, const std::string& selection) {
    if (selection.empty()) return std::nullopt;
    for (std::size_t i : hardwarePortIndices(names, clientName))
        if (names[i] == selection) return i;
    return std::nullopt;
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

    // Choose which hardware device to use, by port name; applied immediately if open() has run. An empty input
    // selection means "All Devices" (open every hardware input, the default); an empty output selection means
    // "None" (the virtual output only, the default). A name that isn't currently present is remembered and
    // re-applied on the next open()/reconnect. Persisted by the host (midi.json).
    void setInputSelection(const std::string& name);
    void setOutputSelection(const std::string& name);
    const std::string& inputSelection() const { return selectedIn_; }
    const std::string& outputSelection() const { return selectedOut_; }

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
    std::string                             selectedIn_;   // "" = All Devices (open every hardware input)
    std::string                             selectedOut_;  // "" = None (virtual output only)

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
    bool                           log_ = false;  // RETROPLUG_MIDI_LOG: dump in + out bytes to stderr
};

}  // namespace retroplug
