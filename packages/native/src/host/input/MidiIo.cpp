#include "host/input/MidiIo.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

#include "RtMidi.h"

namespace retroplug {

MidiIo::MidiIo() = default;

MidiIo::~MidiIo() { close(); }

bool MidiIo::open(const char* clientName) {
    clientName_ = clientName && *clientName ? clientName : "RetroPlug";
    const std::string& name = clientName_;
    log_ = std::getenv("RETROPLUG_MIDI_LOG") != nullptr;  // set before the callback thread starts
    try {
        in_ = std::make_unique<RtMidiIn>(RtMidi::UNSPECIFIED, name);
        // (sysex, time, sense) — false means DELIVER. Sysex is on because a control surface speaks it: a
        // Launchpad attached over TRS/DIN rather than its own USB port arrives on an ordinary hardware
        // input, and its mode-select + bulk-LED messages are all sysex. Clock stays on for transport sync;
        // active sensing stays off (a keepalive nothing here wants).
        in_->ignoreTypes(false, false, true);
        in_->setCallback(&MidiIo::onMidiIn, this);
        in_->openVirtualPort(name + " In");

        out_ = std::make_unique<RtMidiOut>(RtMidi::UNSPECIFIED, name);
        out_->openVirtualPort(name + " Out");
    } catch (RtMidiError& e) {
        std::fprintf(stderr, "[retroplug-sdl] MIDI unavailable: %s (running without MIDI)\n", e.what());
        in_.reset();
        out_.reset();
        return false;
    }
    std::fprintf(stderr, "[retroplug-sdl] MIDI: virtual ports '%s In' / '%s Out' open\n", name.c_str(), name.c_str());
    openHardwareInputs();
    openHardwareOutput();
    return true;
}

// Names of the hardware input/output ports currently present (skipping our own virtual port + ALSA "Through").
static std::vector<std::string> probePortNames(bool input, const std::string& clientName) {
    std::vector<std::string> names;
    try {
        if (input) {
            RtMidiIn probe(RtMidi::UNSPECIFIED, clientName);
            const unsigned count = probe.getPortCount();
            for (unsigned i = 0; i < count; ++i) names.push_back(probe.getPortName(i));
        } else {
            RtMidiOut probe(RtMidi::UNSPECIFIED, clientName);
            const unsigned count = probe.getPortCount();
            for (unsigned i = 0; i < count; ++i) names.push_back(probe.getPortName(i));
        }
    } catch (RtMidiError&) {
        return {};  // no MIDI system
    }
    return names;
}

// Filter a raw port-name list to just the hardware ports (the pure helper decides which to keep).
static std::vector<std::string> hardwarePortNames(const std::vector<std::string>& raw, const std::string& clientName) {
    std::vector<std::string> out;
    for (std::size_t i : hardwarePortIndices(raw, clientName)) out.push_back(raw[i]);
    return out;
}

// The pickers list every hardware port, INCLUDING one a control surface has claimed: the reservation only
// governs what this stream opens, and hiding the port would make the Settings > MIDI list change under the
// user whenever a Launchpad connects.
std::vector<std::string> MidiIo::listInputs() const { return hardwarePortNames(probePortNames(true, clientName_), clientName_); }
std::vector<std::string> MidiIo::listOutputs() const { return hardwarePortNames(probePortNames(false, clientName_), clientName_); }

void MidiIo::openHardwareInputs() {
    hwIn_.clear();
    const std::vector<std::string> names = probePortNames(true, clientName_);
    // The policy lives in the pure helper so it can be unit-tested: none (the default) / every device / the
    // named one, with a control surface's claimed port skipped throughout.
    const std::vector<std::size_t> open = inputPortsToOpen(names, clientName_, selectedIn_, reservedIn_);
    if (open.empty() && selectedIn_.empty())
        std::fprintf(stderr, "[retroplug-sdl] MIDI in: no hardware device selected (Settings > MIDI)\n");
    for (std::size_t i : open) {
        try {
            auto in = std::make_unique<RtMidiIn>(RtMidi::UNSPECIFIED, clientName_);
            in->ignoreTypes(false, false, true);  // sysex delivered — see open()
            in->setCallback(&MidiIo::onMidiIn, this);
            in->openPort(static_cast<unsigned>(i), names[i]);
            std::fprintf(stderr, "[retroplug-sdl] MIDI in: connected hardware port '%s'\n", names[i].c_str());
            hwIn_.push_back(std::move(in));
        } catch (RtMidiError& e) {
            std::fprintf(stderr, "[retroplug-sdl] MIDI: input open failed for '%s': %s\n", names[i].c_str(), e.what());
        }
    }
}

void MidiIo::openHardwareOutput() {
    hwOut_.reset();
    if (selectedOut_.empty()) return;  // None = virtual output only
    const std::vector<std::string> names = probePortNames(false, clientName_);
    auto idx = matchPortIndex(names, clientName_, selectedOut_);
    if (!idx) {  // saved device not currently present — remembered, re-applied on reconnect
        std::fprintf(stderr, "[retroplug-sdl] MIDI out: selected port '%s' not present\n", selectedOut_.c_str());
        return;
    }
    try {
        hwOut_ = std::make_unique<RtMidiOut>(RtMidi::UNSPECIFIED, clientName_);
        hwOut_->openPort(static_cast<unsigned>(*idx), names[*idx]);
        std::fprintf(stderr, "[retroplug-sdl] MIDI out: connected hardware port '%s'\n", names[*idx].c_str());
    } catch (RtMidiError& e) {
        std::fprintf(stderr, "[retroplug-sdl] MIDI: output open failed for '%s': %s\n", selectedOut_.c_str(), e.what());
        hwOut_.reset();
    }
}

void MidiIo::setInputSelection(const std::string& name) {
    selectedIn_ = name;
    if (in_) openHardwareInputs();  // apply live (host pauses audio around this)
}

void MidiIo::setOutputSelection(const std::string& name) {
    selectedOut_ = name;
    if (out_) openHardwareOutput();
}

void MidiIo::setReservedInput(const std::string& name) {
    if (reservedIn_ == name) return;  // reapplied every frame by the host; only a real change reopens ports
    reservedIn_ = name;
    if (in_) openHardwareInputs();  // apply live (host pauses audio around this)
}

void MidiIo::close() {
    in_.reset();   // cancels the callback + closes the port
    hwIn_.clear();
    out_.reset();
    hwOut_.reset();
    head_.store(0, std::memory_order_relaxed);
    tail_.store(0, std::memory_order_relaxed);
}

void MidiIo::onMidiIn(double /*timeStamp*/, std::vector<unsigned char>* message, void* userData) {
    if (!message || message->empty()) return;
    static_cast<MidiIo*>(userData)->pushRing(message->data(), message->size());
}

void MidiIo::pushRing(const unsigned char* data, std::size_t len) {
    const std::size_t t = tail_.load(std::memory_order_relaxed);
    const std::size_t n = (t + 1) % kCapacity;
    if (n == head_.load(std::memory_order_acquire)) return;  // full → drop
    ring_[t].seq = ++seq_;
    ring_[t].bytes.assign(data, data + len);
    tail_.store(n, std::memory_order_release);

    if (log_) {
        std::fprintf(stderr, "[retroplug-sdl] MIDI in: ");
        for (std::size_t i = 0; i < len; ++i) std::fprintf(stderr, "%02X ", data[i]);
        std::fprintf(stderr, "\n");
    }
}

void MidiIo::poll(std::vector<Message>& out) {
    out.clear();
    std::size_t h = head_.load(std::memory_order_relaxed);
    const std::size_t t = tail_.load(std::memory_order_acquire);
    while (h != t) {
        out.push_back(std::move(ring_[h]));
        h = (h + 1) % kCapacity;
    }
    head_.store(h, std::memory_order_release);
}

void MidiIo::send(const std::uint8_t* data, std::size_t len) {
    if (!out_ || len == 0) return;
    if (log_) {
        std::fprintf(stderr, "[retroplug-sdl] MIDI out: ");
        for (std::size_t i = 0; i < len; ++i) std::fprintf(stderr, "%02X ", data[i]);
        std::fprintf(stderr, "\n");
    }
    try {
        out_->sendMessage(data, len);
        if (hwOut_) hwOut_->sendMessage(data, len);  // mirror to the selected hardware output, if any
    } catch (RtMidiError&) {
        // A transient send failure (port gone) shouldn't take down the audio thread.
    }
}

}  // namespace retroplug
