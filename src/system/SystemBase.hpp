#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "system/CpuState.hpp"
#include "system/InputTypes.hpp"
#include "system/MemoryAccessor.hpp"
#include "system/MemoryType.hpp"
#include "system/SystemTypes.hpp"
#include "system/SystemConfig.hpp"
#include "transport/FrameBufferTriple.hpp"
#include "transport/MemorySnapshotTriple.hpp"
#include "transport/MidiTypes.hpp"

namespace rp { class IDebugTarget; }

// Polymorphic runtime representation of one emulator instance. Owned by the
// DSP thread inside Project. Concrete subclasses: SameBoySystem, MesenNesSystem
// (NES), MesenGbaSystem.
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

    // -- Per-system menu actions --------------------------------------------
    //
    // Surfaces used by the UI menu (Save SRAM, Save State, Duplicate, etc.).
    // Default returns mean "this backend doesn't support the feature" — the
    // UI gates the menu row off when appropriate.

    // Source ROM path. Empty when the system was constructed from embedded
    // bytes only (no file on disk).
    virtual const std::string& romPath() const {
        static const std::string empty;
        return empty;
    }

    // Boot-time toggle. SameBoy maps this to fastBoot; GBA to skipBootScreen.
    // Mesen returns nullopt → the UI hides the Fast boot row.
    virtual std::optional<bool> fastBoot() const { return std::nullopt; }
    virtual void                setFastBoot(bool /*on*/) {}

    // "Reload when the ROM file changes on disk" — polled by
    // PluginRpcService::pumpRomWatchers.
    virtual bool wantsRomReload() const  { return false; }
    virtual void setRomReload(bool /*on*/) {}

    // Cartridge battery RAM. Empty vector when the cartridge has no battery
    // or the backend doesn't yet support snapshotting.
    virtual std::vector<std::uint8_t> saveSramBytes() const { return {}; }
    virtual bool loadSramBytes(const std::vector<std::uint8_t>& /*bytes*/) { return false; }
    virtual void                       clearSram() {}

    // Savestate, byte-for-byte. False on unsupported backends or malformed
    // buffers.
    virtual std::vector<std::uint8_t> saveStateBytes() const { return {}; }
    virtual bool loadStateBytes(const std::vector<std::uint8_t>& /*bytes*/) { return false; }

    // Deep clone for Duplicate. Caller supplies the new SystemId and the
    // current sample rate; the returned system has already been onActivate'd.
    // Returns nullptr if the backend can't clone (rare; shouldn't happen on
    // a constructed instance).
    virtual std::unique_ptr<SystemBase> clone(SystemId /*newId*/,
                                              double  /*sampleRate*/) const {
        return nullptr;
    }

    // -- Memory access -------------------------------------------------------

    // Cross-system memory region view. The default returns an invalid
    // accessor (so systems that don't yet implement memory access compile
    // cleanly); SameBoySystem / MesenNesSystem / MesenGbaSystem override.
    //
    // Lifetime: the returned accessor's backing pointer is the live
    // emulator region. Don't store it across activation boundaries (cart
    // swap / reset may relocate the buffer).
    virtual rp::MemoryAccessor getMemory(rp::MemoryType /*type*/, rp::AccessType /*access*/) {
        return rp::MemoryAccessor{};
    }

    // -- CPU state ----------------------------------------------------------
    //
    // Optional, like getMemory: the default implementations report "this
    // backend doesn't expose CPU state" (empty list / false / nullopt / 0) so
    // systems compile cleanly until they override. Concrete backends report
    // their own (heterogeneous) register files by name; every supported
    // backend includes a "pc" register and a getProgramCounter().
    //
    // SameBoy implements all of these. The Mesen backends (NES/GBA) implement
    // registers + PC + register writes; instruction stepping and the
    // side-effect-free readCpuByte are gated on the Mesen debugger and ship
    // later (see porting/19-mesen-debugger.md) — until then they return the
    // unsupported defaults (0 / nullopt).

    // Live register file (empty when unsupported). Names are canonical lower
    // case ("pc", "sp", "a", "af", "r15", "cpsr", …).
    virtual std::vector<rp::CpuRegister> getCpuRegisters() const { return {}; }

    // Write one register by name. False on unsupported backend / unknown name.
    virtual bool setCpuRegister(std::string_view /*name*/, std::uint32_t /*value*/) {
        return false;
    }

    // The program counter — the one register meaningful across every CPU.
    // nullopt when unsupported. Used by runUntilPc().
    virtual std::optional<std::uint32_t> getProgramCounter() const {
        return std::nullopt;
    }

    // Side-effect-free read of one byte of the CPU's address space (banking
    // aware where the backend supports it). nullopt when unsupported.
    virtual std::optional<std::uint8_t> readCpuByte(std::uint32_t /*addr*/) const {
        return std::nullopt;
    }

    // Execute one CPU instruction; returns the cycles it consumed. Returns 0
    // when the backend can't instruction-step (the "unsupported" signal — no
    // real instruction costs zero cycles).
    virtual std::uint64_t stepInstruction() { return 0; }

    // Run until PC == target or `maxCycles` elapse. Returns true if the target
    // PC was reached. Implemented once here on top of stepInstruction() +
    // getProgramCounter(); returns false immediately if the backend can't step
    // or has no program counter. Not virtual — identical for every backend.
    bool runUntilPc(std::uint32_t target, std::uint64_t maxCycles);

    // -- Debugger / profiler ------------------------------------------------
    //
    // Optional debug capability (profiler, disassembly, breakpoints, …).
    // Returns nullptr when the backend has no debugger (SameBoy). The Mesen
    // backends return a live session. One capability object rather than a
    // dozen virtuals; callers branch on nullptr (no dynamic_cast). The pointer
    // is owned by the system and invalidated by reset/deactivation.
    virtual rp::IDebugTarget* debugTarget() { return nullptr; }

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
