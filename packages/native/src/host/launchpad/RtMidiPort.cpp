#include "host/launchpad/RtMidiPort.hpp"

#include <cstdio>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

#include "RtMidi.h"

namespace retroplug {
namespace {

/** Index of the port named exactly `name` on an open RtMidi endpoint, or none. Exact match, because the
 *  caller picked this name out of the very list MidiIo enumerated. */
template <class T>
std::optional<unsigned> findPort(T& endpoint, const std::string& name) {
    const unsigned count = endpoint.getPortCount();
    for (unsigned i = 0; i < count; ++i)
        if (endpoint.getPortName(i) == name) return i;
    return std::nullopt;
}

// One claimed in/out pair. The RtMidiIn is destroyed FIRST (declared last), which cancels its callback, so
// no receiver can run after this object is gone.
class RtMidiPort final : public IMidiPort {
public:
    RtMidiPort(const std::string& clientName, const std::string& inName, const std::string& outName,
               Receiver receiver)
        : receiver_(std::move(receiver)) {
        out_ = std::make_unique<RtMidiOut>(RtMidi::UNSPECIFIED, clientName);
        const auto outIdx = findPort(*out_, outName);
        if (!outIdx) throw std::runtime_error("MIDI output port not found: " + outName);
        out_->openPort(*outIdx, outName);

        in_ = std::make_unique<RtMidiIn>(RtMidi::UNSPECIFIED, clientName);
        const auto inIdx = findPort(*in_, inName);
        if (!inIdx) throw std::runtime_error("MIDI input port not found: " + inName);
        // (sysex, time, sense) - false means DELIVER. SysEx is the device's whole language here; clock is
        // kept because a Launchpad free-runs one and the link is not the place to decide it is uninteresting;
        // active sensing is dropped (a keepalive nothing here wants).
        in_->ignoreTypes(false, false, true);
        in_->setCallback(&RtMidiPort::onMidiIn, this);
        in_->openPort(*inIdx, inName);
    }

    ~RtMidiPort() override {
        in_.reset();   // cancels the callback + closes the port before receiver_ dies
        out_.reset();
    }

    void send(const std::uint8_t* data, std::size_t n) override {
        if (!out_ || n == 0) return;
        try {
            out_->sendMessage(data, n);
        } catch (RtMidiError&) {
            // A transient send failure (the device was unplugged) must not take down the caller's frame.
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

}  // namespace

LaunchpadLink::PortFactory rtMidiPortFactory(std::string clientName) {
    return [clientName = std::move(clientName)](const std::string& inName, const std::string& outName,
                                                IMidiPort::Receiver receiver) -> std::unique_ptr<IMidiPort> {
        try {
            return std::make_unique<RtMidiPort>(clientName, inName, outName, std::move(receiver));
        } catch (RtMidiError& e) {
            throw std::runtime_error(e.what());  // one exception type for LaunchpadLink::connect to catch
        }
    };
}

}  // namespace retroplug
