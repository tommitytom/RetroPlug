#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "host/launchpad/LaunchpadLink.hpp"
#include "host/launchpad/LaunchpadScanner.hpp"

namespace retroplug {

// The Launchpad config snapshot the UI reads - matches the __rp_getLaunchpadConfig object shape
// (launchpadDevices.ts LaunchpadConfig).
struct LaunchpadConfigDto {
    std::vector<std::string> inputs;   // every hardware MIDI input, unfiltered
    std::vector<std::string> outputs;  // every hardware MIDI output, unfiltered
    std::string              selectedInput;
    std::string              selectedOutput;
    std::string              deviceLabel;  // what the scan identified, verbatim from TS ("" = never scanned)
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

    /** Fired on both edges of a scan, on the UI thread. The embedding host uses it to take its shared MIDI
     *  stream's hardware inputs down for the duration: a Windows MIDI input is exclusive, so a scan cannot
     *  open the very interface the user selected as their input device - which is precisely the rig a DIN
     *  Launchpad is on. Like LinkChangedFn, the audio stop/start this needs lives in the host. */
    using ScanBusyFn = std::function<void(bool busy)>;

    LaunchpadHost(LaunchpadLink::PortFactory factory, LaunchpadScanner::OpenFn opener, PortLister lister,
                  std::string configDir);

    LaunchpadLink&       link() { return link_; }
    const LaunchpadLink& link() const { return link_; }

    void setOnLinkChanged(LinkChangedFn fn) { onLinkChanged_ = std::move(fn); }
    void setOnScanBusy(ScanBusyFn fn) { onScanBusy_ = std::move(fn); }

    /** The live snapshot (ports enumerated fresh, link state read live). */
    LaunchpadConfigDto getConfig();

    /** Choose the in/out pair by port name; live-switches if currently connected; persists. */
    void setPorts(const std::string& input, const std::string& output);

    /** Record what the scan identified. Opaque: TS parses the reply and names the model, native stores the
     *  string and hands it back. Persists. */
    void setDeviceLabel(const std::string& label);

    /** Write `probe` out of every enumerated output, listening on every input, and report what came back.
     *  Fire-and-forget; the UI polls scanStatus(). A no-op while a scan is running or while the link holds a
     *  pair (those ports cannot be reopened, and a connected device needs no finding). */
    bool scan(std::vector<std::uint8_t> probe, unsigned windowMs);

    /** The current/last scan. Call from the UI thread ONLY: this is also where the scan's finishing edge is
     *  noticed and onScanBusy(false) fired, so the host's audio dance runs on the thread that owns it. */
    LaunchpadScanStatusDto scanStatus();

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

    LaunchpadLink    link_;
    LaunchpadScanner scanner_;
    PortLister       lister_;
    LinkChangedFn    onLinkChanged_;
    ScanBusyFn       onScanBusy_;
    std::string      configDir_;
    std::string      input_;
    std::string      output_;
    std::string      deviceLabel_;
    bool             enabled_      = false;
    bool             scanReported_ = false;  // onScanBusy(true) fired and not yet matched by its false edge
};

}  // namespace retroplug
