#pragma once

// The MIDI backend contract (docs/modular-graph-phase-f5b-design.md section 4.1): the one interface the host's MIDI
// goes through, so what the ports are called stops being RtMidi's vocabulary and a second implementation (the fake)
// can fill it. Nothing above this header names a MIDI library: RtMidiBackend.cpp is the only file in the package that
// includes RtMidi.h.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace rp::midi {

/** One received message: the bytes, on the backend's OWN callback thread. */
using Receiver = std::function<void(const std::uint8_t*, std::size_t)>;

/** One endpoint, one direction, owned by whoever opened it. Destroying an input cancels its callback: when the
 *  destructor returns, no receiver call is running and none will run. An output's send may block (a WinMM bulk SysEx
 *  waits for the bytes to leave the wire), which is why the Launchpad pack writes on a thread of its own. */
class MidiPort {
public:
    virtual ~MidiPort() = default;
    /** An input's send is a no-op. */
    virtual void send(const std::uint8_t* data, std::size_t n) = 0;
};

/** A MIDI system: the ports it names and the ones it opens. Every open throws std::runtime_error when the port is
 *  absent or will not open - one exception type, so a caller catches the refusal and reads its words rather than
 *  knowing a library's error class. */
class MidiBackend {
public:
    virtual ~MidiBackend() = default;
    /** "rtmidi", "fake": what a log line or a test names the backend by. */
    virtual std::string id() const = 0;
    /** False when the system refuses to be used at all (a macOS CoreMIDI client that would not be created), so a
     *  caller answers "no MIDI" instead of opening a port that cannot exist. */
    virtual bool usable() = 0;
    /** Every port the system names, unfiltered: the one copy of the port filters (PortFilter.hpp) decides in the
     *  callers which of them are hardware, which are allowed, and which a control surface holds. */
    virtual std::vector<std::string> listInputs() = 0;
    virtual std::vector<std::string> listOutputs() = 0;
    /** The client name this system opens its ports under: what other tools see, and what the port filters judge a name
     *  by. The backend owns it, once, so the ports it opens and the ports it publishes cannot be named differently. */
    virtual std::string clientName() const = 0;
    /** This system again under `clientName`, for a caller whose virtual ports must be told apart from this instance's.
     *  The CLI's facet publishes one stream per session, each named after the session ("RetroPlug N8", "RetroPlug
     *  Loopback"), which is a name a DAW routes to; the Launchpad pack likewise holds its own instance under its own
     *  name. Null when the system has one fixed name, in which case a caller keeps the instance it was given. */
    virtual std::unique_ptr<MidiBackend> withClientName(std::string /*clientName*/) { return nullptr; }
    virtual std::unique_ptr<MidiPort> openInput(const std::string& name, Receiver receiver) = 0;
    virtual std::unique_ptr<MidiPort> openOutput(const std::string& name) = 0;
    /** Whether this system can publish a port others connect to. False on Windows, where WinMM's openVirtualPort
     *  prints a warning and returns: said rather than found out by a catch, so the stream logs what is true. */
    virtual bool supportsVirtual() const = 0;
    /** A virtual port a DAW or an aconnect user connects to: "<clientName> In" / "<clientName> Out", on a backend
     *  that supports them. Throws std::runtime_error where it does not, like every other refused open. */
    virtual std::unique_ptr<MidiPort> openVirtualInput(Receiver receiver) = 0;
    virtual std::unique_ptr<MidiPort> openVirtualOutput() = 0;
};

}  // namespace rp::midi
