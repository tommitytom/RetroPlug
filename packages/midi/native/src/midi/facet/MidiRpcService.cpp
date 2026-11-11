#include "midi/facet/MidiRpcService.hpp"

#include <cstddef>
#include <utility>

#include "midi/PortFilter.hpp"

namespace rp::midi {

std::vector<std::string> MidiRpcService::midiListInputs() {
    return stream_->listInputs();
}

std::vector<std::string> MidiRpcService::midiListOutputs() {
    return stream_->listOutputs();
}

bool MidiRpcService::midiOpen(std::string clientName, std::string input) {
    // A session's own name is a backend of its own: two sessions publishing "RetroPlug In/Out" would take each other's
    // ports (a bridge beside the standalone, two bridges), and a DAW routed to "RetroPlug N8 In" would find nothing.
    if (clientName.empty()) clientName = base_.clientName();
    if (clientName != clientName_) {
        std::unique_ptr<MidiBackend> renamed = base_.withClientName(clientName);
        if (renamed) {
            stream_->close();  // the old name's ports are given back before the new instance opens its own
            stream_.reset();   // and the stream is gone BEFORE the backend it points at is freed
            renamed_    = std::move(renamed);
            stream_     = std::make_unique<MidiStream>(*renamed_);
            clientName_ = clientName;
        }
    }
    // The selection is set BEFORE open(), so it is applied once, as the port is published: "" and "*" are every
    // hardware input (the contract's meaning; "*" is what an older CLI sent), any other string is the one port it
    // names. midiOpen itself lets every port go first (open() replaces the stream's ports), so a session that calls it
    // again is how it heals a port that was replugged.
    if (input.empty() || input == kAllInputs) stream_->setInputs({}, true);
    else stream_->setInputs({std::move(input)}, false);
    return stream_->open();
}

void MidiRpcService::midiSelectOutput(std::string output) {
    // "" is the virtual port only; a name is that one output. Applied live once open() has run, and diffed: the port a
    // session stops mirroring is let go and the one it keeps is not disturbed.
    if (output.empty()) stream_->setOutputs({});
    else stream_->setOutputs({std::move(output)});
}

void MidiRpcService::midiSend(rfl::Bytestring bytes) {
    if (bytes.empty()) return;
    stream_->send(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
}

std::vector<RpcMidiIn> MidiRpcService::midiPoll() {
    stream_->poll(scratch_);  // clears + fills scratch_
    std::vector<RpcMidiIn> out;
    out.reserve(scratch_.size());
    for (const MidiStream::Message& m : scratch_) {
        const auto* b = reinterpret_cast<const std::byte*>(m.bytes.data());
        out.push_back({rfl::Bytestring(b, b + m.bytes.size())});
    }
    return out;
}

void MidiRpcService::midiClose() {
    stream_->close();
}

}  // namespace rp::midi
