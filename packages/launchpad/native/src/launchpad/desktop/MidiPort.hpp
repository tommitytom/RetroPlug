#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace rp::launchpad {

// A MIDI endpoint owned EXCLUSIVELY by whoever opened it. Injected rather than concrete so everything above it depends
// on no MIDI library and is testable with nothing plugged in; RtMidiPort.cpp is the only translation unit here that
// includes RtMidi.h.
//
// Nothing here knows what device is on the other end. Every byte that crosses - LED traffic, the probe a scan sends,
// the bytes that release a device on close - is built and parsed in TS (packages/launchpad/ts), so supporting another
// control surface is a TS change, never a C++ one.
class IMidiPort {
public:
    // Delivered on the MIDI backend's OWN callback thread, once per received message.
    using Receiver = std::function<void(const std::uint8_t*, std::size_t)>;

    virtual ~IMidiPort() = default;
    virtual void send(const std::uint8_t* data, std::size_t n) = 0;
};

/** Open ONE port: an input when `input` (delivering to `receiver`), else an output (whose `receiver` is unused; an
 *  output port never calls it, and an input port's send() is a no-op). Throws when the named port is absent or will
 *  not open. */
using MidiPortOpener =
    std::function<std::unique_ptr<IMidiPort>(bool input, const std::string& name, IMidiPort::Receiver receiver)>;

}  // namespace rp::launchpad
