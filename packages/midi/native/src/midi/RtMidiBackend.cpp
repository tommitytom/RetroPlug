#include "midi/RtMidiBackend.hpp"

#include <cstdio>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "RtMidi.h"
#include "midi/PortFilter.hpp"

#if defined(__APPLE__)
#include <CoreMIDI/CoreMIDI.h>

#include <chrono>
#include <thread>
#endif

namespace rp::midi {
namespace {

/** True when an RtMidiIn/RtMidiOut can safely be CONSTRUCTED. On macOS this creates (and then keeps) the process's one
 *  CoreMIDI client first.
 *
 *  RtMidi makes that client inside MidiInCore::getCoreMidiClientSingleton(), which is declared throw() -- while its
 *  failure path calls error(DRIVER_ERROR), and that THROWS. A throw crossing a noexcept boundary is an immediate
 *  std::terminate (__cxa_call_unexpected), so when MIDIClientCreate fails the host ABORTS outright and not one catch
 *  block can see it - they never get to unwind. Upstream thestk/rtmidi, so it is not ours to fix in place.
 *
 *  Observed as an Abort trap out of the port listing at startup, when several short-lived hosts run back to back:
 *  MIDIClientCreate intermittently returns -304 or -2 while MIDIServer - an on-demand daemon that stops once its last
 *  client goes - is still coming back up. So make the client ourselves first and let a failure mean "no MIDI" rather
 *  than a dead host. It is kept for the process lifetime deliberately: RtMidi's own singleton then rides on an
 *  already-warm connection, and holding it keeps MIDIServer up for the rest of the run. Every RtMidi object this file
 *  builds asks this first; everything above it is the interface's. */
bool midiSystemUsable() {
#if defined(__APPLE__)
    static const bool ok = [] {
        for (int attempt = 0; attempt < 3; ++attempt) {
            MIDIClientRef client = 0;
            if (MIDIClientCreate(CFSTR("RetroPlug"), nullptr, nullptr, &client) == noErr)
                return true;  // never disposed: it is the keepalive
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        std::fprintf(stderr, "[retroplug] MIDI unavailable: CoreMIDI would not create a client "
                             "(continuing without MIDI)\n");
        return false;
    }();
    return ok;
#else
    return true;
#endif
}

/** The names of every port an endpoint of one direction names, as the system reports them (nothing filtered here: the
 *  callers hold the one rule). The probe object is created per call, so a port plugged in since the last listing is
 *  there the next time - there is no hotplug watch, a list is read when it is asked for. */
std::vector<std::string> probePortNames(bool input, const std::string& clientName) {
    std::vector<std::string> names;
    if (!midiSystemUsable()) return names;
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
        names.clear();  // no MIDI system
    }
    return names;
}

/** Index of the port named exactly `name` on an open RtMidi endpoint, or none. Exact, because the caller picked this
 *  name out of the very list this enumerated, and by NAME on the object that opens it: a port hot-plugged between the
 *  two shifts every index after it, so opening by number could take a port no filter judged. */
template <class T>
std::optional<unsigned> findPort(T& endpoint, const std::string& name) {
    const unsigned count = endpoint.getPortCount();
    for (unsigned i = 0; i < count; ++i)
        if (endpoint.getPortName(i) == name) return i;
    return std::nullopt;
}

// One port, one direction. An input keeps its RtMidiIn LAST, so it is destroyed first and its callback is cancelled
// before the receiver it calls goes away.
class RtMidiPort final : public MidiPort {
public:
    RtMidiPort(const std::string& clientName, bool input, const std::string& name, Receiver receiver)
        : receiver_(std::move(receiver)) {
        if (input) {
            in_            = std::make_unique<RtMidiIn>(RtMidi::UNSPECIFIED, clientName);
            const auto idx = findPort(*in_, name);
            if (!idx) throw std::runtime_error("MIDI input port not found: " + name);
            // (sysex, time, sense) - false means DELIVER. SysEx is a control surface's language and a cart bridge's;
            // clock is kept because what to do with it is the reader's call; active sensing (a keepalive nothing here
            // wants) is dropped.
            in_->ignoreTypes(false, false, true);
            in_->setCallback(&RtMidiPort::onMidiIn, this);
            in_->openPort(*idx, name);
        } else {
            out_           = std::make_unique<RtMidiOut>(RtMidi::UNSPECIFIED, clientName);
            const auto idx = findPort(*out_, name);
            if (!idx) throw std::runtime_error("MIDI output port not found: " + name);
            out_->openPort(*idx, name);
        }
    }

    ~RtMidiPort() override {
        in_.reset();  // cancels the callback before receiver_ dies
        out_.reset();
    }

    void send(const std::uint8_t* data, std::size_t n) override {
        if (!out_ || !data || n == 0) return;  // an input's send is a no-op
        try {
            out_->sendMessage(data, n);
        } catch (RtMidiError&) {
            // A transient send failure (the device was unplugged) must not take down the caller's block.
        }
    }

private:
    static void onMidiIn(double, std::vector<unsigned char>* message, void* userData) {
        if (!message || message->empty()) return;
        auto* self = static_cast<RtMidiPort*>(userData);
        if (self->receiver_) self->receiver_(message->data(), message->size());
    }

    Receiver                   receiver_;
    std::unique_ptr<RtMidiOut> out_;
    std::unique_ptr<RtMidiIn>  in_;  // LAST: destroyed first, cancelling the callback
};

// A virtual port: one the system publishes for others to connect to, so a DAW or an aconnect user reaches the stream
// the way it reaches the plugin in a host.
class RtMidiVirtualPort final : public MidiPort {
public:
    RtMidiVirtualPort(const std::string& clientName, bool input, const std::string& portName, Receiver receiver)
        : receiver_(std::move(receiver)) {
        if (input) {
            in_ = std::make_unique<RtMidiIn>(RtMidi::UNSPECIFIED, clientName);
            in_->ignoreTypes(false, false, true);  // as an opened port: sysex delivered
            in_->setCallback(&RtMidiVirtualPort::onMidiIn, this);
            in_->openVirtualPort(portName);
        } else {
            out_ = std::make_unique<RtMidiOut>(RtMidi::UNSPECIFIED, clientName);
            out_->openVirtualPort(portName);
        }
    }

    ~RtMidiVirtualPort() override {
        in_.reset();
        out_.reset();
    }

    void send(const std::uint8_t* data, std::size_t n) override {
        if (!out_ || !data || n == 0) return;
        try {
            out_->sendMessage(data, n);
        } catch (RtMidiError&) {
            // As a hardware output's send: a transient failure is not the audio thread's to die of.
        }
    }

private:
    static void onMidiIn(double, std::vector<unsigned char>* message, void* userData) {
        if (!message || message->empty()) return;
        auto* self = static_cast<RtMidiVirtualPort*>(userData);
        if (self->receiver_) self->receiver_(message->data(), message->size());
    }

    Receiver                   receiver_;
    std::unique_ptr<RtMidiOut> out_;
    std::unique_ptr<RtMidiIn>  in_;  // LAST: destroyed first, cancelling the callback
};

/** RtMidi behind the interface, under one client name: the process's "RetroPlug" (systemMidi) and the Launchpad pack's
 *  "RetroPlug Launchpad", whose link ports a product's stream must not take for a device. The client name is what other
 *  tools see and what the port filters judge a name by, so two instances of this are two named systems. */
class RtMidiBackend final : public MidiBackend {
public:
    explicit RtMidiBackend(std::string clientName) : clientName_(std::move(clientName)) {}

    std::string id() const override { return "rtmidi"; }
    std::string clientName() const override { return clientName_; }
    // Another RtMidi system under the requested name: a separate client, so its virtual ports are published beside this
    // one's rather than replacing them (that is how a CLI session's "RetroPlug N8 In" sits next to the app's
    // "RetroPlug In").
    std::unique_ptr<MidiBackend> withClientName(std::string clientName) override {
        return std::make_unique<RtMidiBackend>(std::move(clientName));
    }
    bool usable() override { return midiSystemUsable(); }
    std::vector<std::string> listInputs() override { return probePortNames(true, clientName_); }
    std::vector<std::string> listOutputs() override { return probePortNames(false, clientName_); }
    bool supportsVirtual() const override {
#if defined(_WIN32)
        // WinMM's openVirtualPort does not throw: it raises a WARNING (RtMidi.cpp's openVirtualPort on Windows) that
        // prints and returns, so a port announced as open would not exist. Said here, once, rather than found out by a
        // catch that never fires.
        return false;
#else
        return true;
#endif
    }

    std::unique_ptr<MidiPort> openInput(const std::string& name, Receiver receiver) override {
        // The test-only allow list, refused here as well as by the callers: a filtered process may not open a port it
        // may not list either, whatever path reaches this.
        if (!midiPortAllowed(name)) throw std::runtime_error("MIDI port not allowed: " + name);
        if (!midiSystemUsable()) throw std::runtime_error("no MIDI system");
        try {
            return std::make_unique<RtMidiPort>(clientName_, true, name, std::move(receiver));
        } catch (RtMidiError& e) {
            throw std::runtime_error(e.what());  // one exception type for every caller to catch
        }
    }

    std::unique_ptr<MidiPort> openOutput(const std::string& name) override {
        if (!midiPortAllowed(name)) throw std::runtime_error("MIDI port not allowed: " + name);
        if (!midiSystemUsable()) throw std::runtime_error("no MIDI system");
        try {
            return std::make_unique<RtMidiPort>(clientName_, false, name, nullptr);
        } catch (RtMidiError& e) {
            throw std::runtime_error(e.what());
        }
    }

    std::unique_ptr<MidiPort> openVirtualInput(Receiver receiver) override {
        refuseVirtual();
        try {
            return std::make_unique<RtMidiVirtualPort>(clientName_, true, clientName_ + " In", std::move(receiver));
        } catch (RtMidiError& e) {
            throw std::runtime_error(e.what());
        }
    }

    std::unique_ptr<MidiPort> openVirtualOutput() override {
        refuseVirtual();
        try {
            return std::make_unique<RtMidiVirtualPort>(clientName_, false, clientName_ + " Out", nullptr);
        } catch (RtMidiError& e) {
            throw std::runtime_error(e.what());
        }
    }

private:
    /** WinMM's openVirtualPort does not throw: it raises a WARNING that prints and returns, leaving a port that does
     *  not exist announced as open. A caller that asked anyway gets the refusal every other failed open gives, rather
     *  than a silent nothing. */
    void refuseVirtual() const {
        if (!supportsVirtual()) throw std::runtime_error("virtual ports are not supported");
        if (!midiSystemUsable()) throw std::runtime_error("no MIDI system");
    }

    std::string clientName_;
};

}  // namespace

MidiBackend& systemMidi() {
    static RtMidiBackend backend{"RetroPlug"};
    return backend;
}

std::unique_ptr<MidiBackend> rtMidiBackend(std::string clientName) {
    return std::make_unique<RtMidiBackend>(std::move(clientName));
}

}  // namespace rp::midi
