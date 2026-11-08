#include "launchpad/desktop/RtMidiPort.hpp"

#include <cstdio>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "RtMidi.h"
#include "launchpad/desktop/MidiPortFilter.hpp"

#if defined(__APPLE__)
#include <CoreMIDI/CoreMIDI.h>

#include <chrono>
#include <thread>
#endif

namespace rp::launchpad {
namespace {

/** True when an RtMidiIn/RtMidiOut can safely be CONSTRUCTED. On macOS this creates the process's CoreMIDI client first:
 *  RtMidi makes it inside a function declared throw(), whose failure path throws, and a throw across that boundary is
 *  std::terminate - so a failed MIDIClientCreate would abort the host and no catch here could see it. The client is kept
 *  for the process's life on purpose, so RtMidi's own singleton rides on a warm connection. Elsewhere RtMidi reports a
 *  failure as an exception, which the opener catches. Everything that builds an RtMidi object asks this first: this
 *  file's opener and its lister, which are the package's only two.
 *  (packages/native/src/host/input/MidiIo.cpp guards its own RtMidi objects the same way, for the same reason; a package
 *  cannot include it, so the probe is said here too.) */
bool midiSystemUsable() {
#if defined(__APPLE__)
    static const bool ok = [] {
        MIDIClientRef client = 0;
        for (int attempt = 0; attempt < 3; ++attempt) {
            if (MIDIClientCreate(CFSTR("RetroPlug"), nullptr, nullptr, &client) == noErr) return true;  // never disposed
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        std::fprintf(stderr, "[retroplug] MIDI unavailable: CoreMIDI would not create a client (no Launchpad ports)\n");
        return false;
    }();
    return ok;
#else
    return true;
#endif
}

/** Index of the port named exactly `name` on an open RtMidi endpoint, or none. Exact match, because the caller picked
 *  this name out of the very list these functions enumerated. */
template <class T>
std::optional<unsigned> findPort(T& endpoint, const std::string& name) {
    const unsigned count = endpoint.getPortCount();
    for (unsigned i = 0; i < count; ++i)
        if (endpoint.getPortName(i) == name) return i;
    return std::nullopt;
}

// One port, one direction. An input keeps its RtMidiIn (destroyed first by declaration order, cancelling the callback
// before the receiver dies); an output keeps only its RtMidiOut and ignores send() when it has none.
class RtMidiSinglePort final : public IMidiPort {
public:
    RtMidiSinglePort(const std::string& clientName, bool input, const std::string& name, Receiver receiver)
        : receiver_(std::move(receiver)) {
        if (input) {
            in_            = std::make_unique<RtMidiIn>(RtMidi::UNSPECIFIED, clientName);
            const auto idx = findPort(*in_, name);
            if (!idx) throw std::runtime_error("MIDI input port not found: " + name);
            // (sysex, time, sense) - false means DELIVER. SysEx is a control surface's language; clock is kept because
            // what to do with it is the reader's call, not the port's; active sensing (a keepalive nothing here wants)
            // is dropped.
            in_->ignoreTypes(false, false, true);
            in_->setCallback(&RtMidiSinglePort::onMidiIn, this);
            in_->openPort(*idx, name);
        } else {
            out_           = std::make_unique<RtMidiOut>(RtMidi::UNSPECIFIED, clientName);
            const auto idx = findPort(*out_, name);
            if (!idx) throw std::runtime_error("MIDI output port not found: " + name);
            out_->openPort(*idx, name);
        }
    }

    ~RtMidiSinglePort() override {
        in_.reset();
        out_.reset();
    }

    void send(const std::uint8_t* data, std::size_t n) override {
        if (!out_ || n == 0) return;
        try {
            out_->sendMessage(data, static_cast<std::size_t>(n));
        } catch (RtMidiError&) {
            // A transient send failure (the device was unplugged) must not take down the caller's frame.
        }
    }

private:
    static void onMidiIn(double, std::vector<unsigned char>* message, void* userData) {
        if (!message || message->empty()) return;
        auto* self = static_cast<RtMidiSinglePort*>(userData);
        if (self->receiver_) self->receiver_(message->data(), message->size());
    }

    Receiver                   receiver_;
    std::unique_ptr<RtMidiOut> out_;
    std::unique_ptr<RtMidiIn>  in_;  // LAST: destroyed first, cancelling the callback
};

}  // namespace

MidiPortOpener rtMidiPortOpener(std::string clientName) {
    return [clientName = std::move(clientName)](bool input, const std::string& name,
                                                IMidiPort::Receiver receiver) -> std::unique_ptr<IMidiPort> {
        // The test-only allow list (MidiPortFilter.hpp): a filtered process may not open a port it may not list either.
        if (!midiPortAllowed(name)) throw std::runtime_error("MIDI port not allowed: " + name);
        if (!midiSystemUsable()) throw std::runtime_error("no MIDI system");
        try {
            return std::make_unique<RtMidiSinglePort>(clientName, input, name, std::move(receiver));
        } catch (RtMidiError& e) {
            throw std::runtime_error(e.what());  // one exception type for the link and the pool to catch
        }
    };
}

PortNames listMidiPorts() {
    PortNames out;
    if (!midiSystemUsable()) return out;
    try {
        std::vector<std::string> inputs, outputs;
        RtMidiIn in(RtMidi::UNSPECIFIED, "RetroPlug");
        for (unsigned i = 0; i < in.getPortCount(); ++i) inputs.push_back(in.getPortName(i));
        RtMidiOut op(RtMidi::UNSPECIFIED, "RetroPlug");
        for (unsigned i = 0; i < op.getPortCount(); ++i) outputs.push_back(op.getPortName(i));
        // The client name is the stream's, so this enumeration's own ports are the ones dropped below as "not
        // hardware" - with ALSA's Through, and with another instance's `RetroPlug Launchpad` link ports.
        out.inputs  = allowedMidiPorts(inputs, "RetroPlug");
        out.outputs = allowedMidiPorts(outputs, "RetroPlug");
    } catch (const std::exception& e) {
        // A MIDI system that comes up but refuses an endpoint (a bare container): no ports, said once.
        std::fprintf(stderr, "[retroplug] the MIDI system is unavailable: %s\n", e.what());
        out.inputs.clear();
        out.outputs.clear();
    }
    return out;
}

}  // namespace rp::launchpad
