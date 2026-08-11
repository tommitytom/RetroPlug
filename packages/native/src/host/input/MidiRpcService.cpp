#include "host/input/MidiRpcService.hpp"

#include <cstddef>

namespace retroplug {

std::vector<std::string> MidiRpcService::midiListInputs() {
    return midi_.listInputs();
}

std::vector<std::string> MidiRpcService::midiListOutputs() {
    return midi_.listOutputs();
}

bool MidiRpcService::midiOpen(std::string clientName, std::string input) {
    midi_.setInputSelection(input);  // "" = all hardware inputs; set before open() so it's applied once
    return midi_.open(clientName.c_str());
}

void MidiRpcService::midiSelectOutput(std::string output) {
    midi_.setOutputSelection(output);  // applied live once open() has run
}

void MidiRpcService::midiSend(rfl::Bytestring bytes) {
    if (bytes.empty()) return;
    midi_.send(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
}

std::vector<RpcMidiIn> MidiRpcService::midiPoll() {
    midi_.poll(scratch_);  // clears + fills scratch_
    std::vector<RpcMidiIn> out;
    out.reserve(scratch_.size());
    for (const MidiIo::Message& m : scratch_) {
        const auto* b = reinterpret_cast<const std::byte*>(m.bytes.data());
        out.push_back({rfl::Bytestring(b, b + m.bytes.size())});
    }
    return out;
}

void MidiRpcService::midiClose() {
    midi_.close();
}

}  // namespace retroplug
