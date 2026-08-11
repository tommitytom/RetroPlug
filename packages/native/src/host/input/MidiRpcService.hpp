#pragma once

#include <string>
#include <vector>

#include <rfl/Bytestring.hpp>

#include "host/input/MidiIo.hpp"

namespace retroplug {

// One incoming MIDI message for the TS layer. MidiIo carries no per-message timing (RtMidi's timestamp is
// discarded), so this is just the raw bytes - the MIDI-in twin of RpcMidiOut.
struct RpcMidiIn {
    rfl::Bytestring bytes;
};

// The thin native live-MIDI-input transport exposed to the TS control plane over the Backend RPC channel -
// the MIDI twin of SerialRpcService. Owns one MidiIo; a TS session enumerates inputs, opens (a virtual port
// + the selected hardware inputs), and drains messages by polling. RtMidi + its callback thread + the ring
// stay native behind it. Mounted CLI-only (registerMidiRpc, under RETROPLUG_N8_BRIDGE).
class MidiRpcService {
public:
    // Hardware MIDI input port names (skips our own virtual port + ALSA "Through").
    std::vector<std::string> midiListInputs();

    // The same for outputs - the other half of talking to a control surface, which has to be LIT as well as
    // read.
    std::vector<std::string> midiListOutputs();

    // Open MIDI: create the virtual "<clientName> In/Out" ports and open `input` ("" = every hardware input).
    // Returns false if no MIDI system is available.
    bool midiOpen(std::string clientName, std::string input);

    // Choose the hardware output to mirror sends to ("" = the virtual port only). Applied immediately.
    void midiSelectOutput(std::string output);

    // Drain the input messages queued since the last poll (each = raw bytes). Empty when nothing arrived.
    std::vector<RpcMidiIn> midiPoll();

    // Send one raw message. Any length - a control surface's bulk-LED sysex runs to hundreds of bytes, and
    // MidiIo imposes no cap.
    void midiSend(rfl::Bytestring bytes);

    // Close all MIDI ports.
    void midiClose();

private:
    MidiIo                       midi_;
    std::vector<MidiIo::Message> scratch_;  // reused poll buffer
};

// Mount the MIDI-input facet onto an rpcpp server - same cross-object addMethod pattern as
// registerSerialRpc. Header-only + templated so it stays OUT of the shared registration union; only the
// CLI includes + calls it, so the plugin/test hosts never link rtmidi.
template <class Server>
void registerMidiRpc(Server& s, MidiRpcService& svc) {
    s.template addMethod<&MidiRpcService::midiListInputs>(svc);
    s.template addMethod<&MidiRpcService::midiListOutputs>(svc);
    s.template addMethod<&MidiRpcService::midiOpen>(svc);
    s.template addMethod<&MidiRpcService::midiSelectOutput>(svc);
    s.template addMethod<&MidiRpcService::midiPoll>(svc);
    s.template addMethod<&MidiRpcService::midiSend>(svc);
    s.template addMethod<&MidiRpcService::midiClose>(svc);
}

}  // namespace retroplug
