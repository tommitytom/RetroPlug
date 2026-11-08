#pragma once

#include <string>
#include <vector>

#include "launchpad/desktop/MidiPort.hpp"

namespace rp::launchpad {

// The real MIDI backend behind MidiPortOpener: one RtMidi port, one direction, with SysEx DELIVERED (a control
// surface's whole protocol tends to be SysEx). This is the only file in the package that includes RtMidi.h, so the
// link, the pool, the provider and their unit test link without a MIDI library at all - which is what lets the test
// binary run with no MIDI system and no hardware.
//
// The opener throws std::runtime_error when the named port is absent or cannot be opened; ControllerLink and
// MidiPortPool catch it and surface the message.
MidiPortOpener rtMidiPortOpener(std::string clientName);

// The names of every HARDWARE input and output port the system offers, as the control plane's pickers and a scan need
// them: RetroPlug's own virtual ports and ALSA's "Midi Through" are not ports (MidiPortFilter.hpp's
// isHardwareMidiPort), and a scan writes a Device Inquiry out of every output it is handed. Empty when there is no MIDI
// system (a bare container): a product then simply finds no Launchpad.
struct PortNames {
    std::vector<std::string> inputs;
    std::vector<std::string> outputs;
};
PortNames listMidiPorts();

}  // namespace rp::launchpad
