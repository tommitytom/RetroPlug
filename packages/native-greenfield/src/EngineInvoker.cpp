#include "EngineInvoker.hpp"

#include <cstdio>
#include <utility>

#include "system/SystemBase.hpp"  // complete type for unique_ptr<SystemBase>

#include "Engine.hpp"

// --- DirectInvoker: apply now, on the calling thread ------------------------

void DirectInvoker::adoptSystem(std::unique_ptr<SystemBase> sys) {
    engine_.adoptSystem(std::move(sys));
}

void DirectInvoker::replaceSystem(SystemId id, std::unique_ptr<SystemBase> sys) {
    engine_.replaceSystem(id, std::move(sys));  // returned displaced core → delete
}

void DirectInvoker::removeSystem(SystemId id) {
    engine_.removeSystem(id);  // returned removed core → delete
}

void DirectInvoker::loadKernel(std::vector<std::uint8_t> bytecode) {
    engine_.loadKernel(bytecode);
}

void DirectInvoker::setSystems(std::string json) {
    engine_.setSystems(std::vector<std::uint8_t>(json.begin(), json.end()));
}

void DirectInvoker::stageMidi(std::vector<std::uint8_t> bytes) {
    engine_.stageMidi(std::move(bytes));
}

void DirectInvoker::setBpm(double bpm) {
    engine_.setBpm(bpm);
}

void DirectInvoker::setTransport(bool playing) {
    engine_.setTransport(playing);
}

// --- QueuedInvoker: producer half (control thread) --------------------------

void QueuedInvoker::adoptSystem(std::unique_ptr<SystemBase> sys) {
    DspCommand c;
    c.kind = DspCommand::Kind::AddSystem;
    c.addSystem = { sys.get() };
    if (commands_.tryPush(c)) sys.release();  // else: unique_ptr deletes the build (full ring)
}

void QueuedInvoker::replaceSystem(SystemId id, std::unique_ptr<SystemBase> sys) {
    DspCommand c;
    c.kind = DspCommand::Kind::ReplaceSystem;
    c.replaceSystem = { sys.get(), id };
    if (commands_.tryPush(c)) sys.release();  // else: unique_ptr deletes the build
}

void QueuedInvoker::removeSystem(SystemId id) {
    DspCommand c;
    c.kind = DspCommand::Kind::RemoveSystem;
    c.removeSystem = { id };
    commands_.tryPush(c);  // no heap payload — dropped on a full ring
}

void QueuedInvoker::loadKernel(std::vector<std::uint8_t> bytecode) {
    DspCommand c;
    c.kind = DspCommand::Kind::LoadKernel;
    c.loadKernel.bytecode = new std::vector<std::uint8_t>(std::move(bytecode));
    if (!commands_.tryPush(c)) delete c.loadKernel.bytecode;
}

void QueuedInvoker::setSystems(std::string json) {
    DspCommand c;
    c.kind = DspCommand::Kind::SetSystems;
    c.setSystems.json = new std::string(std::move(json));
    if (!commands_.tryPush(c)) delete c.setSystems.json;
}

void QueuedInvoker::stageMidi(std::vector<std::uint8_t> bytes) {
    DspCommand c;
    c.kind = DspCommand::Kind::StageMidi;
    c.stageMidi.len = static_cast<std::uint8_t>(bytes.size());
    for (std::size_t i = 0; i < bytes.size() && i < 4; ++i) c.stageMidi.data[i] = bytes[i];
    commands_.tryPush(c);
}

void QueuedInvoker::setBpm(double bpm) {
    DspCommand c;
    c.kind = DspCommand::Kind::SetBpm;
    c.setBpm = { bpm };
    commands_.tryPush(c);
}

void QueuedInvoker::setTransport(bool playing) {
    DspCommand c;
    c.kind = DspCommand::Kind::SetTransport;
    c.setTransport = { playing };
    commands_.tryPush(c);
}

// --- QueuedInvoker: consumer half (audio thread) ----------------------------

void QueuedInvoker::drainInto(Engine& engine) {
    DspCommand cmd;
    while (commands_.tryPop(cmd)) {
        switch (cmd.kind) {
            case DspCommand::Kind::SetSystems:
                engine.setSystems(std::vector<std::uint8_t>(cmd.setSystems.json->begin(), cmd.setSystems.json->end()));
                delete cmd.setSystems.json;  // owning payload — free on the audio thread (rare op)
                break;
            case DspCommand::Kind::LoadKernel:
                engine.loadKernel(*cmd.loadKernel.bytecode);
                delete cmd.loadKernel.bytecode;
                break;
            case DspCommand::Kind::StageMidi:
                engine.stageMidi(std::vector<std::uint8_t>(cmd.stageMidi.data, cmd.stageMidi.data + cmd.stageMidi.len));
                break;
            case DspCommand::Kind::SetBpm:
                engine.setBpm(cmd.setBpm.value);
                break;
            case DspCommand::Kind::SetTransport:
                engine.setTransport(cmd.setTransport.value);
                break;
            // Lifecycle: alloc-free pointer swaps into the pre-reserved Project; displaced/removed
            // cores are handed back to the control thread for delete (never freed here).
            case DspCommand::Kind::AddSystem:
                engine.adoptSystem(std::unique_ptr<SystemBase>(cmd.addSystem.sys));
                break;
            case DspCommand::Kind::ReplaceSystem:
                handBackReleased(engine.replaceSystem(cmd.replaceSystem.id,
                                                      std::unique_ptr<SystemBase>(cmd.replaceSystem.sys)).release());
                break;
            case DspCommand::Kind::RemoveSystem:
                handBackReleased(engine.removeSystem(cmd.removeSystem.id).release());
                break;
            default:
                break;
        }
    }
}

std::unique_ptr<SystemBase> QueuedInvoker::popReleased() {
    DspEvent e;
    if (!released_.tryPop(e)) return nullptr;
    if (e.kind == DspEvent::Kind::SystemReleased) return std::unique_ptr<SystemBase>(e.released.sys);
    return nullptr;
}

void QueuedInvoker::freePending() {
    // Only safe once the audio thread is joined (the ring has a single accessor again). Free the
    // heap payloads of any un-applied commands — built-but-unadopted systems, config/bytecode blobs.
    DspCommand cmd;
    while (commands_.tryPop(cmd)) {
        switch (cmd.kind) {
            case DspCommand::Kind::SetSystems:    delete cmd.setSystems.json; break;
            case DspCommand::Kind::LoadKernel:    delete cmd.loadKernel.bytecode; break;
            case DspCommand::Kind::AddSystem:     delete cmd.addSystem.sys; break;      // built but never adopted
            case DspCommand::Kind::ReplaceSystem: delete cmd.replaceSystem.sys; break;  // built but never swapped in
            default: break;  // StageMidi/RemoveSystem/SetBpm/SetTransport carry no heap payload
        }
    }
}

void QueuedInvoker::handBackReleased(SystemBase* sys) {
    if (!sys) return;
    DspEvent e;
    e.kind = DspEvent::Kind::SystemReleased;
    e.released.sys = sys;
    if (!released_.tryPush(e)) {
        // Ring full (256 pending releases undrained) — can't free on the audio thread; leak rather
        // than block/free in the render loop. In practice a test/app drains far more often than this.
        std::fprintf(stderr, "[greenfield] released-system ring full; leaking a core\n");
    }
}
