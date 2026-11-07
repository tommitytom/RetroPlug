#include "host/engine/EngineInvoker.hpp"

#include <utility>

#include "host/engine/Engine.hpp"

// --- producer half (control thread): pack + push, then flush inline unless the audio thread drains ---

bool QueuedInvoker::push(const DspCommand& c) { return engine_->pushCommand(c); }

void QueuedInvoker::stageMidi(std::vector<std::uint8_t> bytes, std::uint32_t frame) {
    if (bytes.empty()) return;
    DspCommand c;
    c.kind = DspCommand::Kind::StageMidi;
    c.stageMidi.frame = frame;
    c.stageMidi.ext = nullptr;
    if (bytes.size() <= 4) {
        c.stageMidi.len = static_cast<std::uint8_t>(bytes.size());
        for (std::size_t i = 0; i < bytes.size(); ++i) c.stageMidi.data[i] = bytes[i];
        push(c);
    } else {
        // Too long for the inline slot (a SysEx, or several messages staged as one run): ship it as an
        // owning payload, freed by the audio thread after applying.
        c.stageMidi.len = 0;
        c.stageMidi.ext = new std::vector<std::uint8_t>(std::move(bytes));
        if (!push(c)) delete c.stageMidi.ext;  // full ring: drop the message, never leak it
    }
}

void QueuedInvoker::stageNodeMidi(std::string nodeId, std::vector<std::uint8_t> bytes) {
    if (bytes.empty()) return;
    DspCommand c;
    c.kind = DspCommand::Kind::StageNodeMidi;
    auto* staged = new rp::host::StagedNodeMessage{std::move(nodeId), std::move(bytes)};
    c.stageNodeMidi.ext = staged;
    if (!push(c)) delete staged;  // full ring: drop it, never leak it
}

void QueuedInvoker::setBpm(double bpm) {
    DspCommand c;
    c.kind = DspCommand::Kind::SetBpm;
    c.setBpm = { bpm };
    push(c);
}

void QueuedInvoker::setTransport(bool playing) {
    DspCommand c;
    c.kind = DspCommand::Kind::SetTransport;
    c.setTransport = { playing };
    push(c);
}

void QueuedInvoker::setPpq(double ppq) {
    DspCommand c;
    c.kind = DspCommand::Kind::SetPpq;
    c.setPpq = { ppq };
    push(c);
}

void QueuedInvoker::pressButton(SystemId id, std::uint8_t button, bool down) {
    DspCommand c;
    c.kind = DspCommand::Kind::PressButton;
    c.pressButton = { static_cast<std::uint32_t>(id), button, down };
    push(c);  // no heap payload: dropped on a full ring (a lost key edge, not a leak)
}

bool QueuedInvoker::writeRam(SystemId id, std::uint32_t offset, std::vector<std::uint8_t> bytes) {
    // A capability write that nobody collects (Engine::writeRam): its record and bytes stay the control thread's.
    return engine_->writeRam(id, offset, std::move(bytes));
}

bool QueuedInvoker::writeSram(SystemId id, std::uint32_t offset, std::vector<std::uint8_t> bytes,
                              std::vector<std::uint8_t> expect) {
    // The same, aimed at the battery (Engine::writeSram).
    return engine_->writeSram(id, offset, std::move(bytes), std::move(expect));
}

// --- consumer half: apply every queued command into the Engine (audio loop, or the inline flush) -----

void QueuedInvoker::drainInto(Engine& engine) { engine.drainCommands(); }

void QueuedInvoker::setAudioThreadOwns(bool owns) {
    audioThreadOwns_.store(owns, std::memory_order_release);
    engine_->setAudioThreadOwns(owns);
}

std::uint32_t QueuedInvoker::reclaimReleased() { return static_cast<std::uint32_t>(engine_->reclaim()); }

void QueuedInvoker::freePending() { engine_->discardPending(); }
