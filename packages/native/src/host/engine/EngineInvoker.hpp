#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "system/SystemTypes.hpp"  // SystemId

#include "host/engine/DspCommand.hpp"

class Engine;
class SystemBase;
namespace rp::host { struct HostShape; }

// The ONE mutation path to the Engine. Structure (add / replace / remove, the routing mode, a Game Boy's
// link group, the host's output shape) goes to the Engine's translator, which plans it as a graph
// document; every other edit is packed into a POD DspCommand and pushed onto the GraphHost's one ring
// (docs/modular-graph-phase-b1-design.md section 5), so all of them keep their push order. Who DRAINS the
// ring is the only mode, held in `audioThreadOwns_`:
//   - while the audio thread owns the Engine (running), it drains each block; the producer just pushes.
//   - otherwise every push applies inline (the GraphHost flushes as it is pushed onto), so the ring is
//     empty at the moment the audio thread takes ownership, and the handoff is clean.
// Reads never go through here (they read the snapshot registry). A removed or displaced core leaves with
// the plan that last ran it, and is freed on the control thread when that plan is reclaimed.
class QueuedInvoker {
public:
    explicit QueuedInvoker(Engine& engine) : engine_(&engine) {}

    // Drain ownership. Set true BEFORE spawning/activating the audio thread (pushes become push-only);
    // set false AFTER joining it (pushes apply inline again, and whatever is queued applies now). Written
    // on the control thread only; read by the control thread and the audio loop (its run condition).
    void setAudioThreadOwns(bool owns);
    bool audioThreadOwns() const { return audioThreadOwns_.load(std::memory_order_acquire); }

    // producer (control thread)
    void adoptSystem(std::unique_ptr<SystemBase> sys);
    void replaceSystem(SystemId id, std::unique_ptr<SystemBase> sys);
    void removeSystem(SystemId id);
    void loadKernel(std::vector<std::uint8_t> bytecode);
    void setSystems(std::string json);
    // Any length: a channel message rides the command inline, a longer run (SysEx) as an owning payload.
    void stageMidi(std::vector<std::uint8_t> bytes);
    // Control-surface traffic (a pad press), kept out of the musical stream. Inline-only (4 bytes),
    // which a Note/CC press fits inside; a surface's sysex never comes this way. False, with nothing
    // staged, for an empty message or one longer than 4 bytes.
    bool stageControllerMidi(std::vector<std::uint8_t> bytes);
    // MIDI for ONE system, bypassing the routing kernel entirely (see DspCommand::stageSystemMidi).
    void stageSystemMidi(SystemId id, std::vector<std::uint8_t> bytes);
    void setBpm(double bpm);
    void setTransport(bool playing);
    void setPpq(double ppq);
    void setAudioRouting(std::uint8_t mode);
    void applyConfigField(SystemId id, std::uint8_t field, double value);
    void pressButton(SystemId id, std::uint8_t button, bool down);
    void writeRam(SystemId id, std::uint32_t offset, std::vector<std::uint8_t> bytes);
    // The output shape the next blocks render into. The render calls set it before they render.
    void setHostShape(rp::host::HostShape shape);

    // consumer: apply every queued command (the block thread, or the control thread while quiescent).
    void drainInto(Engine& engine);
    // control thread: free the plans the audio thread handed back, and the cores only they held. Returns
    // how many plans it freed.
    std::uint32_t reclaimReleased();
    // after the audio thread is joined: DISCARD what is still queued (its payloads freed, never applied).
    // Teardown only: drainInto APPLIES a pending command; this drops it.
    void freePending();

private:
    bool push(const DspCommand& c);

    Engine*           engine_ = nullptr;
    std::atomic<bool> audioThreadOwns_{false};
};
