#include "host/input/MidiIo.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

#include "RtMidi.h"

namespace retroplug {

MidiIo::MidiIo() = default;

MidiIo::~MidiIo() { close(); }

bool MidiIo::open(const char* clientName) {
    const std::string name = clientName && *clientName ? clientName : "RetroPlug";
    try {
        in_ = std::make_unique<RtMidiIn>(RtMidi::UNSPECIFIED, name);
        // Deliver NOTE/CC/etc. but ignore sysex + active-sensing; keep timing (MIDI clock) for a later
        // transport-sync step. (sysex, time, sense) — false on `time` means clock messages ARE delivered.
        in_->ignoreTypes(true, false, true);
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
    openHardwareInputs(name);
    return true;
}

void MidiIo::openHardwareInputs(const std::string& clientName) {
    try {
        RtMidiIn probe(RtMidi::UNSPECIFIED, clientName);
        const unsigned count = probe.getPortCount();
        for (unsigned i = 0; i < count; ++i) {
            const std::string portName = probe.getPortName(i);
            if (portName.find(clientName) != std::string::npos) continue;  // our own virtual port
            if (portName.find("Through") != std::string::npos) continue;    // ALSA MIDI-through (no hardware)
            auto in = std::make_unique<RtMidiIn>(RtMidi::UNSPECIFIED, clientName);
            in->ignoreTypes(true, false, true);
            in->setCallback(&MidiIo::onMidiIn, this);
            in->openPort(i, portName);
            std::fprintf(stderr, "[retroplug-sdl] MIDI in: connected hardware port '%s'\n", portName.c_str());
            hwIn_.push_back(std::move(in));
        }
    } catch (RtMidiError& e) {
        std::fprintf(stderr, "[retroplug-sdl] MIDI: hardware-port scan failed: %s\n", e.what());
    }
}

void MidiIo::close() {
    in_.reset();   // cancels the callback + closes the port
    hwIn_.clear();
    out_.reset();
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

    if (std::getenv("RETROPLUG_MIDI_LOG")) {
        std::fprintf(stderr, "[retroplug-sdl] MIDI in:");
        for (std::size_t i = 0; i < len; ++i) std::fprintf(stderr, " %02X", data[i]);
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
    try {
        out_->sendMessage(data, len);
    } catch (RtMidiError&) {
        // A transient send failure (port gone) shouldn't take down the audio thread.
    }
}

}  // namespace retroplug
