#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "host/launchpad/LaunchpadLink.hpp"

namespace retroplug {

// The Launchpad config snapshot the UI reads - matches the __rp_getLaunchpadConfig object shape
// (launchpadDevices.ts LaunchpadConfig).
struct LaunchpadConfigDto {
    std::vector<std::string> inputs;   // every hardware MIDI input, unfiltered
    std::vector<std::string> outputs;  // every hardware MIDI output, unfiltered
    std::string              selectedInput;
    std::string              selectedOutput;
    bool                     connected = false;
    bool                     enabled   = false;
    std::uint64_t            sent      = 0;
    std::uint64_t            dropped   = 0;
    std::string              error;
};

// Owner of the control-surface link + its config, and the thing the __rp_* hooks talk to. Holds a
// LaunchpadLink, the selected port names + enabled toggle, and persistence to <configDir>/launchpad.cfg.
// The port factory and the port ENUMERATION are both injected, so this is testable hardware-free (a fake
// IMidiPort, no MIDI library). The N8Host twin, minus the SD worker.
//
// The port lists are deliberately UNFILTERED. A Launchpad also speaks TRS/DIN, so on a machine short of USB
// ports it arrives through an ordinary MIDI interface on a port named after the INTERFACE - nothing about
// the name says "Launchpad". Filtering to a device-name hint would make exactly that setup unconfigurable,
// so the hint stays a UI concern (it picks the default and tags a row) and native offers every port.
class LaunchpadHost {
public:
    /** Enumerate hardware MIDI ports: inputs when `input`, else outputs. The standalone satisfies this with
     *  MidiIo::listInputs/listOutputs, already free of our own virtual ports and ALSA Through ports. */
    using PortLister = std::function<std::vector<std::string>(bool input)>;

    /** Fired after any change to what the link holds (connect / disconnect / port switch). The embedding
     *  host uses it to re-apply the reserved port to its shared MIDI stream, which has to happen with audio
     *  stopped - so the stop/start dance lives in the host and this class stays free of it. */
    using LinkChangedFn = std::function<void()>;

    LaunchpadHost(LaunchpadLink::PortFactory factory, PortLister lister, std::string configDir);

    LaunchpadLink&       link() { return link_; }
    const LaunchpadLink& link() const { return link_; }

    void setOnLinkChanged(LinkChangedFn fn) { onLinkChanged_ = std::move(fn); }

    /** The live snapshot (ports enumerated fresh, link state read live). */
    LaunchpadConfigDto getConfig();

    /** Choose the in/out pair by port name; live-switches if currently connected; persists. */
    void setPorts(const std::string& input, const std::string& output);

    /** Toggle the link. Persists. Enabling with no port chosen leaves it down rather than guessing: the UI
     *  resolves the default (it owns the device hint), and native stores exactly what it is told. */
    void connect(bool enable);

    /** The opaque bytes replayed when the device is given back (TS hands down exitToLiveMode). */
    void setFarewell(std::vector<std::uint8_t> bytes) { link_.setFarewell(std::move(bytes)); }

    /** Load launchpad.cfg and reconnect if it was left enabled. Call once at host startup. */
    void restore();

    /** The input port to keep OUT of the shared musical MIDI stream, or "" when nothing is connected.
     *  Without this a pad press arrives as music too and every launch fires twice: once quantised through
     *  the controller app, once raw through the tracker's own MIDI translator. */
    std::string reservedInputPort() const;

private:
    void save();
    void applyLink();  // (re)connect or disconnect to match enabled_ + the selected ports, then notify

    LaunchpadLink link_;
    PortLister    lister_;
    LinkChangedFn onLinkChanged_;
    std::string   configDir_;
    std::string   input_;
    std::string   output_;
    bool          enabled_ = false;
};

}  // namespace retroplug
