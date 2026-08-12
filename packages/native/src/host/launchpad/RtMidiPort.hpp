#pragma once

#include <string>

#include "host/launchpad/LaunchpadLink.hpp"

namespace retroplug {

// The real MIDI backend behind LaunchpadLink's injected IMidiPort: RtMidi in + out opened on two named
// hardware ports, with SysEx DELIVERED (the whole protocol is SysEx). This is the only file in
// host/launchpad/ that includes RtMidi.h, so the link and its unit test link without a MIDI library at all -
// the same split WjwwoodSerialPort has from ISerialPort.
//
// The returned factory throws std::runtime_error when either named port is absent or cannot be opened;
// LaunchpadLink::connect catches it and surfaces the message as lastError().
LaunchpadLink::PortFactory rtMidiPortFactory(std::string clientName);

}  // namespace retroplug
