#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "system/InputTypes.hpp"
#include "system/MemoryAccessor.hpp"
#include "system/MemoryType.hpp"
#include "system/SystemTypes.hpp"
#include "system/SystemConfig.hpp"
#include "transport/FrameBufferTriple.hpp"
#include "transport/MemorySnapshotTriple.hpp"
#include "transport/MidiTypes.hpp"

// Polymorphic runtime representation of one emulator instance. Owned by the
// DSP thread inside Project. Concrete subclasses: SameBoySystem, MesenSystem
// (NES), GbaSystem.
//
// Persistence: snapshotConfig() returns a plain-data SystemConfig that the
// DSP can serialize via reflectcpp from getState(). The runtime polymorphic
// state never enters the JSON path.
class SystemBase {
public:
    explicit SystemBase(SystemId id) : id_(id) {}
    virtual ~SystemBase() = default;

    SystemBase(const SystemBase&) = delete;
    SystemBase& operator=(const SystemBase&) = delete;

    SystemId id() const { return id_; }

    virtual SystemKind kind() const = 0;

    virtual void onActivate(double sampleRate) = 0;
    virtual void onDeactivate() {}
    virtual void onSampleRateChanged(double sampleRate) = 0;
    virtual void onReset() {}

    // Audio-thread per-block entry. `outs[0]` and `outs[1]` are planar L/R
    // buffers (DPF convention). Implementations must SUM into outs, not
    // overwrite, so multiple systems can mix into a single output pair.
    virtual void onProcess(const AudioBlockInfo& info, float* const* outs) = 0;

    // Audio-thread MIDI delivery.
    virtual void onMidi(const ::MidiEvent* /*events*/, std::uint32_t /*count*/) {}

    // Audio-thread: enqueue a button transition.
    virtual void pressButton(std::uint8_t /*button*/, bool /*down*/) {}

    // Returns nullptr for systems without video (or before activation).
    virtual FrameBufferTriple* framebuffer() { return nullptr; }

    // Per-block MIDI output, drained by PluginDSP into DPF's writeMidiEvent
    // after onProcess.
    std::vector<::MidiEvent>&       midiOut()       { return midiOut_; }
    const std::vector<::MidiEvent>& midiOut() const { return midiOut_; }

    // Round-trips current state back to a plain-data config. Called from
    // Plugin::getState (rare; off-path). May allocate.
    virtual SystemConfig snapshotConfig() const = 0;

    // -- Memory access -------------------------------------------------------

    // Cross-system memory region view. The default returns an invalid
    // accessor (so systems that don't yet implement memory access compile
    // cleanly); SameBoySystem / MesenSystem / GbaSystem override.
    //
    // Lifetime: the returned accessor's backing pointer is the live
    // emulator region. Don't store it across activation boundaries (cart
    // swap / reset may relocate the buffer).
    virtual rp::MemoryAccessor getMemory(rp::MemoryType /*type*/, rp::AccessType /*access*/) {
        return rp::MemoryAccessor{};
    }

    // -- Live memory snapshots ----------------------------------------------
    //
    // The DSP thread publishes tear-free snapshots into MemorySnapshotTriple
    // buffers after each onProcess(). The UI thread reads them at uiIdle.
    //
    // Subscription is refcounted: multiple UI consumers can subscribe to the
    // same (system, type); the triple-buffer is allocated on the 0→1
    // transition and freed on the 1→0 transition. enable/disable are called
    // from the DSP thread (via the SubscribeMemory / UnsubscribeMemory
    // command drain) so memorySnapshot() and publishSnapshots() never race.

    // DSP thread: bump refcount. Allocates the triple on first reference.
    // Returns true on success; false if `type` is unsupported on this
    // system (the matching getMemory() returns an invalid accessor).
    bool enableMemorySnapshot(rp::MemoryType type);

    // DSP thread: drop refcount. Frees the triple on last release. No-op if
    // not subscribed.
    void disableMemorySnapshot(rp::MemoryType type);

    // UI thread: get the triple to read from. Returns nullptr if not
    // subscribed. Cheap atomic load.
    MemorySnapshotTriple* memorySnapshot(rp::MemoryType type);

    // Cap on the size we'll triple-buffer for live streaming. Larger
    // regions (ROM, GBA EWRAM, large SRAM) refuse subscription and are
    // one-shot only via getMemory RPC. 64 KiB covers GB RAM (8 KB), GBA
    // IWRAM (32 KB), NES nametables (2 KB), OAM (≤ 1 KB) comfortably.
    static constexpr std::size_t kMaxStreamableBytes = 64 * 1024;

    // DSP thread: called from each concrete system's onProcess() AFTER the
    // emulator step finishes. Walks active subscriptions, copies each
    // region into its triple-buffer, publishes. Cheap when no subs.
    void publishMemorySnapshots();

protected:
    std::vector<::MidiEvent> midiOut_;

private:
    struct SnapshotEntry {
        std::unique_ptr<MemorySnapshotTriple> triple;
        std::uint32_t                         refcount = 0;
    };
    std::array<SnapshotEntry, rp::kMemoryTypeCount> snapshots_;

    SystemId id_;
};
