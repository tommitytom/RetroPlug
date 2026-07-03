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

    // -- Per-block audio lockstep -------------------------------------------
    //
    // Every system advances through a 3-phase triad: prepareForBlock →
    // stepIfBelowTarget (looped until it returns false) → finishBlock. A link
    // group round-robins stepIfBelowTarget across its members so serial bits
    // ferry mid-block; a standalone system (or a Mesen backend) is the
    // degenerate 1-member unit. The runner (system/BlockRunner.cpp's runUnit())
    // drives these three directly for EVERY unit — singleton or link group; it
    // does not route singletons through onProcess().
    //
    // Output contract: `outs[0]`/`outs[1]` are planar L/R buffers (DPF
    // convention) the system must SUM into, not overwrite, so multiple systems
    // can mix into one output pair. The CALLER zeroes the buffers.
    //
    // Defaults are inert (no-op / "done") so trivial backends and test doubles
    // need not implement them.
    virtual void prepareForBlock(const AudioBlockInfo& /*info*/) {}
    virtual bool stepIfBelowTarget(std::uint32_t /*framesNeeded*/) { return false; }
    virtual void finishBlock(const AudioBlockInfo& /*info*/, float* const* /*outs*/) {}

    // True when this system is advanced as part of a multi-member link unit —
    // its block is stepped by the group's round-robin, so it must NOT be driven
    // standalone. Default false; SameBoySystem returns true when it has linked
    // peers.
    virtual bool isLinked() const { return false; }

    // Fused single-system convenience entry: prepare → step-to-done → finish.
    // NOT on the runner's hot path (runUnit drives the triad directly) — this is
    // for direct callers and test doubles that want to advance one system in one
    // call. Defined out-of-line in SystemBase.cpp. Backends implement the triad,
    // not this.
    virtual void onProcess(const AudioBlockInfo& info, float* const* outs);

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

    // Loose-battery disambiguator. 0 => this system owns the plain sibling
    // `<rom>.sav`; N>=2 => `<rom>-N.sav`. Lets multiple systems backed by the
    // same ROM file (Duplicate Instance / loading the same file twice) keep
    // independent battery files instead of clobbering one another. Persisted
    // in the per-system config; see SramAutoSave.hpp / siblingSavPath.
    virtual std::uint32_t savSuffix() const { return 0; }
    virtual void          setSavSuffix(std::uint32_t /*suffix*/) {}

    // Explicit battery-file override. Empty => the loose `.sav` is derived from
    // romPath + savSuffix (default). Non-empty => this exact file, set when the
    // user pairs a hand-picked `.sav` with the ROM; all battery I/O targets it.
    // See SramAutoSave.hpp / resolveSavPath.
    virtual const std::string& savPath() const {
        static const std::string empty;
        return empty;
    }
    virtual void          setSavPath(const std::string& /*path*/) {}

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

    // Like clone(), but seeds the copy from a pre-captured savestate (e.g. one
    // read from the DSP-published state snapshot) instead of the live
    // emulator. Lets Duplicate stay race-free. SRAM is taken from within the
    // savestate where the backend can locate it. Default: unsupported.
    virtual std::unique_ptr<SystemBase> cloneFromState(SystemId /*newId*/,
                                                       double /*sampleRate*/,
                                                       const std::vector<std::uint8_t>& /*savestate*/) const {
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

    // -- Whole-savestate snapshot -------------------------------------------
    //
    // The DSP thread captures the entire savestate into a triple-buffer on a
    // coarse interval; the UI thread reads the latest for Save State / Save
    // SRAM / Duplicate without ever touching the live emulator. Because the
    // savestate contains SRAM/RAM/VRAM, those regions are sliced out of it via
    // stateRegions() rather than re-read live. (RetroPlug v1's FetchMemory
    // type==MAX path.) The slot stores a 4-byte little-endian length prefix
    // followed by the savestate, so length + bytes stay tear-free in one
    // publish (Mesen savestates are variable-size).

    // Byte offset/size of a memory region WITHIN the savestate buffer.
    struct StateRegion { std::uint32_t offset = 0; std::uint32_t size = 0; };
    using StateRegionTable = std::array<StateRegion, rp::kMemoryTypeCount>;

    // DSP thread: idempotent. Allocates the snapshot triple sized to
    // stateSnapshotSize() (+ the length prefix) and captures region offsets,
    // arming an immediate first publish. No-op if already enabled or if the
    // backend doesn't support savestates (stateSnapshotSize() == 0).
    bool enableStateSnapshot();

    // DSP thread: accumulate `frames`; once past the interval, capture the
    // savestate into the triple and publish. Cheap no-op when disabled.
    void publishStateSnapshot(std::uint32_t frames, double sampleRate);

    // UI thread: copy the latest published savestate into `out` (length-prefix
    // stripped). False if no snapshot has been published yet.
    bool readStateSnapshot(std::vector<std::uint8_t>& out);

    // UI thread: region table for slicing SRAM/RAM/VRAM out of a snapshot read
    // via readStateSnapshot(). A region with size 0 is absent. Stable for the
    // life of the system (set once at enable).
    const StateRegionTable& stateRegions() const { return stateRegions_; }

protected:
    std::vector<::MidiEvent> midiOut_;

    // State-snapshot backend hooks (default: unsupported). Overridden by
    // SameBoySystem (with region offsets) and the Mesen systems (full state,
    // no offsets). All called on the DSP thread.
    virtual std::size_t stateSnapshotSize() const { return 0; }
    virtual bool captureStateSnapshot(std::vector<std::uint8_t>& /*dst*/) { return false; }
    virtual StateRegionTable stateSnapshotRegions() const { return {}; }

private:
    struct SnapshotEntry {
        std::unique_ptr<MemorySnapshotTriple> triple;
        std::uint32_t                         refcount = 0;
    };
    std::array<SnapshotEntry, rp::kMemoryTypeCount> snapshots_;

    // Whole-savestate snapshot. Triple allocated once at enable and freed only
    // in the destructor (never mid-life), so the UI-side raw pointer can't
    // dangle. interval ~0.5s.
    static constexpr double                kStateSnapshotIntervalSec = 0.5;
    // Sanity bound on a single savestate. Comfortably above any GB/NES/GBA
    // state; rejects an absurd size rather than allocating wildly.
    static constexpr std::size_t           kMaxStateSnapshotBytes = 16 * 1024 * 1024;
    std::unique_ptr<MemorySnapshotTriple>  stateSnapshot_;
    StateRegionTable                       stateRegions_{};
    std::vector<std::uint8_t>              stateScratch_;       // DSP-thread capture buffer
    std::vector<std::uint8_t>              stateReadScratch_;   // UI-thread read buffer
    std::uint64_t                          stateSnapSamples_ = 0;
    bool                                   stateSnapshotEnabled_ = false;

    SystemId id_;
};
