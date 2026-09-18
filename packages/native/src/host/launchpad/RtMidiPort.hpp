#pragma once

#include <string>

#include "host/launchpad/LaunchpadLink.hpp"
#include "host/launchpad/LaunchpadScanner.hpp"

namespace retroplug {

// The real MIDI backend behind LaunchpadLink's injected IMidiPort: RtMidi in + out opened on two named
// hardware ports, with SysEx DELIVERED (the whole protocol is SysEx). This is the only file in
// host/launchpad/ that includes RtMidi.h, so the link and its unit test link without a MIDI library at all -
// the same split WjwwoodSerialPort has from ISerialPort.
//
// The returned factory throws std::runtime_error when either named port is absent or cannot be opened;
// LaunchpadLink::connect catches it and surfaces the message as lastError().
LaunchpadLink::PortFactory rtMidiPortFactory(std::string clientName);

// The same backend for LaunchpadScanner, which needs the two directions SEPARATELY: a scan holds every input
// open at once while probing one output at a time, so it cannot go through the in/out pair above. An input-only
// port's send() is a no-op; an output-only port never calls its receiver. Throws like the factory when the
// named port will not open, which a scan counts and steps over rather than failing on.
LaunchpadScanner::OpenFn rtMidiPortOpener(std::string clientName);

}  // namespace retroplug
