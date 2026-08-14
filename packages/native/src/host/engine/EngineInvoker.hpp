#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "system/SystemTypes.hpp"  // SystemId
#include "transport/SpscRing.hpp"

#include "host/engine/DspCommand.hpp"
#include "host/engine/DspEvent.hpp"

class Engine;
class SystemBase;
class SnapshotRegistry;

// The ONE mutation path to the Engine — there is no Direct/Queued fork. Every control-plane edit is
// packed into a POD DspCommand and pushed onto the SPSC command ring. Who DRAINS the ring is the only
// mode, held in a single control-thread-written bit `audioThreadOwns_`:
//   - while the audio thread owns the Engine (running), it drains each block; the producer just pushes.
//   - otherwise the control thread flushes INLINE right after each push (push-then-drain) — the same
//     thread is both producer and consumer, so the SPSC invariant is trivially satisfied.
// Because the quiescent path flushes every push, the ring is empty at the moment the audio thread takes
// ownership, so the handoff is clean. Reads never go through here (they read the snapshot registry).
// Removed/displaced cores are routed back through the release ring and deleted on the control thread.
class QueuedInvoker {
public:
    // engine_ = drained on the inline flush; registry_ = to release a deleted core's snapshot slot
    // (an unadopted build on a full ring, or a reclaimed release).
    QueuedInvoker(Engine& engine, SnapshotRegistry& registry) : engine_(&engine), registry_(&registry) {}

    // Drain ownership. Set true BEFORE spawning/activating the audio thread (pushes become push-only);
    // set false AFTER joining it (pushes flush inline again). Written on the control thread only; read
    // by the control thread (the flush decision) and the audio loop (its run condition).
    void setAudioThreadOwns(bool owns) { audioThreadOwns_.store(owns, std::memory_order_release); }
    bool audioThreadOwns() const { return audioThreadOwns_.load(std::memory_order_acquire); }

    // producer (control thread): pack + push, then flush inline unless the audio thread owns the drain.
    void adoptSystem(std::unique_ptr<SystemBase> sys);
    void replaceSystem(SystemId id, std::unique_ptr<SystemBase> sys);
    void removeSystem(SystemId id);
    void loadKernel(std::vector<std::uint8_t> bytecode);
    void setSystems(std::string json);
    void stageMidi(std::vector<std::uint8_t> bytes);
    void setBpm(double bpm);
    void setTransport(bool playing);
    void setPpq(double ppq);
    void setAudioRouting(std::uint8_t mode);
    void applyConfigField(SystemId id, std::uint8_t field, double value);
    void pressButton(SystemId id, std::uint8_t button, bool down);
    void writeRam(SystemId id, std::uint32_t offset, std::vector<std::uint8_t> bytes);

    // consumer: apply every queued command INTO the Engine. Called by the audio loop each block (audio
    // thread) and by the inline flush (control thread).
    void drainInto(Engine& engine);
    // control thread: delete every core the audio thread released, freeing each snapshot slot. Returns
    // the count freed.
    std::uint32_t reclaimReleased();
    // after the audio thread is joined: DISCARD un-applied command payloads (+ their slots). Teardown
    // only — drainInto APPLIES a pending command; this drops it.
    void freePending();

private:
    void handBackReleased(SystemBase* sys);      // audio thread → release ring
    std::unique_ptr<SystemBase> popReleased();   // control thread ← release ring
    void flush();                                // control-thread inline: drainInto(engine_) + reclaimReleased()
    void maybeFlush() { if (!audioThreadOwns()) flush(); }

    SpscRing<DspCommand, 256> commands_;   // control → audio
    SpscRing<DspEvent, 256>   released_;    // audio  → control (raw SystemBase* to delete)
    Engine*                   engine_ = nullptr;
    SnapshotRegistry*         registry_ = nullptr;
    std::atomic<bool>         audioThreadOwns_{false};
};
