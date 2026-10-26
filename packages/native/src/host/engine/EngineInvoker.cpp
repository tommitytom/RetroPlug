#include "host/engine/EngineInvoker.hpp"

#include <utility>

#include "host/engine/Engine.hpp"

// --- producer half (control thread): pack + push, then flush inline unless the audio thread drains ---

bool QueuedInvoker::push(const DspCommand& c) { return engine_->pushCommand(c); }

void QueuedInvoker::loadKernel(std::vector<std::uint8_t> bytecode) {
    DspCommand c;
    c.kind = DspCommand::Kind::LoadKernel;
    c.loadKernel.bytecode = new std::vector<std::uint8_t>(std::move(bytecode));
    if (!push(c)) delete c.loadKernel.bytecode;
}

void QueuedInvoker::setSystems(std::string json) {
    DspCommand c;
    c.kind = DspCommand::Kind::SetSystems;
    c.setSystems.json = new std::string(std::move(json));
    if (!push(c)) delete c.setSystems.json;
}

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
        // owning payload, freed by the audio thread after applying - the writeRam pattern.
        c.stageMidi.len = 0;
        c.stageMidi.ext = new std::vector<std::uint8_t>(std::move(bytes));
        if (!push(c)) delete c.stageMidi.ext;  // full ring: drop the message, never leak it
    }
}

bool QueuedInvoker::stageControllerMidi(std::vector<std::uint8_t> bytes) {
    // Inline-only: drainInto reads `len` bytes out of the 4-byte slot, so a longer message would read past it.
    if (bytes.empty() || bytes.size() > 4) return false;
    DspCommand c;
    c.kind = DspCommand::Kind::StageControllerMidi;
    c.stageMidi.ext = nullptr;
    c.stageMidi.len = static_cast<std::uint8_t>(bytes.size());
    for (std::size_t i = 0; i < bytes.size(); ++i) c.stageMidi.data[i] = bytes[i];
    push(c);
    return true;
}

void QueuedInvoker::stageSystemMidi(SystemId id, std::vector<std::uint8_t> bytes) {
    if (bytes.empty()) return;
    DspCommand c;
    c.kind = DspCommand::Kind::StageSystemMidi;
    c.stageSystemMidi.id = static_cast<std::uint32_t>(id);
    c.stageSystemMidi.ext = nullptr;
    if (bytes.size() <= 4) {
        c.stageSystemMidi.len = static_cast<std::uint8_t>(bytes.size());
        for (std::size_t i = 0; i < bytes.size(); ++i) c.stageSystemMidi.data[i] = bytes[i];
        push(c);  // no heap payload: dropped on a full ring (a lost knob step, not a leak)
    } else {
        c.stageSystemMidi.len = 0;
        c.stageSystemMidi.ext = new std::vector<std::uint8_t>(std::move(bytes));
        if (!push(c)) delete c.stageSystemMidi.ext;  // full ring: drop it, never leak it
    }
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

void QueuedInvoker::applyConfigField(SystemId id, std::uint8_t field, double value) {
    DspCommand c;
    c.kind = DspCommand::Kind::SetConfigField;
    c.setConfigField = { static_cast<std::uint32_t>(id), field, value };
    push(c);  // no heap payload: dropped on a full ring
}

void QueuedInvoker::pressButton(SystemId id, std::uint8_t button, bool down) {
    DspCommand c;
    c.kind = DspCommand::Kind::PressButton;
    c.pressButton = { static_cast<std::uint32_t>(id), button, down };
    push(c);  // no heap payload: dropped on a full ring (a lost key edge, not a leak)
}

void QueuedInvoker::writeRam(SystemId id, std::uint32_t offset, std::vector<std::uint8_t> bytes) {
    DspCommand c;
    c.kind = DspCommand::Kind::WriteRam;
    c.writeRam = { static_cast<std::uint32_t>(id), offset, new std::vector<std::uint8_t>(std::move(bytes)) };
    if (!push(c)) delete c.writeRam.bytes;  // full ring: drop the poke, never leak it
}

// --- consumer half: apply every queued command into the Engine (audio loop, or the inline flush) -----

void QueuedInvoker::drainInto(Engine& engine) { engine.drainCommands(); }

void QueuedInvoker::setAudioThreadOwns(bool owns) {
    audioThreadOwns_.store(owns, std::memory_order_release);
    engine_->setAudioThreadOwns(owns);
}

std::uint32_t QueuedInvoker::reclaimReleased() { return static_cast<std::uint32_t>(engine_->reclaim()); }

void QueuedInvoker::freePending() { engine_->discardPending(); }
