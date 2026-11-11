#pragma once

// The RtMidi systems this package offers (docs/modular-graph-phase-f5b-design.md sections 4.1 and 4.5): the process's
// one real MIDI system, and a second instance under another client name. This is the door to the implementation in
// RtMidiBackend.cpp; the interface beside it (MidiBackend.hpp) names no MIDI library, so a caller that only uses the
// interface does not include this header.

#include <memory>
#include <string>

#include "midi/MidiBackend.hpp"

namespace rp::midi {

/** The process's real MIDI system (RtMidi), under the macOS CoreMIDI guard: one instance, its client named
 *  "RetroPlug", which is what the stream's virtual ports and every name the filters judge carry. */
MidiBackend& systemMidi();

/** Another RtMidi instance under `clientName`, for a caller whose ports must be told apart from the stream's (the
 *  Launchpad pack's link ports are "RetroPlug Launchpad:<device>", which the filter drops as not hardware). One
 *  backend per client name; the caller owns it. */
std::unique_ptr<MidiBackend> rtMidiBackend(std::string clientName);

}  // namespace rp::midi
