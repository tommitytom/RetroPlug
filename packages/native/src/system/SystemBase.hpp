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
#include "transport/FrameBufferTriple.hpp"
#include "transport/MemorySnapshotTriple.hpp"
#include "transport/MidiTypes.hpp"

namespace rp { class IDebugTarget; }

// What the core's host<->cartridge transport is holding, and what it has lost. Cross-core by name
// rather than NES-specific: any core that grows a lossy transport reports the same five numbers.
// `droppedBytes` is CUMULATIVE for the run - reading it does not clear it - so a timeline can sample
// it repeatedly without racing itself. Field names match the TS keys (reflect-cpp serialises this).
struct CoreTransportStats {
    std::uint64_t rxDepth      = 0;  // delivered to the core, not yet read by it
    std::uint64_t rxCapacity   = 0;  // 0 = unbounded (nothing can be dropped)
    std::uint64_t droppedBytes = 0;  // lost because the queue was full
    std::uint64_t wirePending  = 0;  // handed over by the host, not yet carried to the core
    std::uint64_t txDepth      = 0;  // sent by the core, not yet drained by the host
};

// Polymorphic runtime representation of one emulator instance. Owned by the
// DSP thread inside Project. Concrete subclasses: SameBoySystem, MesenNesSystem
// (NES), MesenGbaSystem.
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
    // Output contract: `outs` is `laneCount` planar buffers the system must SUM
    // into, not overwrite, so multiple systems can mix into one destination. The
    // CALLER zeroes the buffers. `laneCount` is 2 for the default single stereo
    // stream (`outs[0]`/`outs[1]` = L/R); a backend that reports a wider
    // channelLayout() receives `2 * streamCount` lanes (stream k → `outs[2k]` /
    // `outs[2k+1]`) and must branch on `laneCount` — the router the runner built
    // is the authority for how many lanes arrive.
    //
    // Defaults are inert (no-op / "done") so trivial backends and test doubles
    // need not implement them.
    virtual void prepareForBlock(const AudioBlockInfo& /*info*/) {}
    virtual bool stepIfBelowTarget(std::uint32_t /*framesNeeded*/) { return false; }
    virtual void finishBlock(const AudioBlockInfo& /*info*/, float* const* /*outs*/,
                             std::size_t /*laneCount*/) {}

    // The output streams this system can emit. Default = one stereo "Mix" stream
    // (the mixed console output), i.e. today's behaviour. A backend that can split
    // its audio reports one entry per stream; that layout only takes effect under
    // a router built to split it (finishBlock still receives whatever laneCount
    // the router sized), so reporting a wide layout is inert on the default path.
    virtual std::vector<ChannelStream> channelLayout() const { return {{"Mix", true}}; }

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

    // Audio-thread: push `size` RAW bytes into the core's device byte-input, scheduled at intra-block
    // sample offset `frame` — no MIDI framing / length cap. The cross-core analog of pushSerialIn for a
    // byte transport (the NES routes it to the N8 FIFO); default no-op for cores without one.
    // `flush` discards whatever the device still holds first, for a protocol message that is a barrier
    // (risa's host-sync arm) rather than a continuation of the stream.
    virtual void pushCoreBytes(std::uint32_t /*frame*/, const std::uint8_t* /*data*/, std::size_t /*size*/,
                               bool /*flush*/ = false) {}

    // Does pushCoreBytes actually go anywhere on this system? The default above is a silent no-op, which is
    // right for a stream a core simply has no use for — but a caller with ONE message to deliver (a host
    // settings SysEx) needs to know whether it was delivered or dropped, and a message too long for the
    // MIDI-framed door has nowhere else to go. Ask before choosing a door; see Engine::stageSystemMidi.
    virtual bool hasCoreBytesIn() const { return false; }

    // The inverse of pushCoreBytes: take the RAW bytes the core has sent host-ward since the last
    // drain, oldest first. On the NES that is the N8 FIFO's `CMD_USB_WR` payload — the back-channel a
    // cartridge uses to talk to the host (on hardware the MCU forwards it out of the USB port). Empty
    // for cores with no such transport. Control-thread read; the NES implementation is mutex-guarded.
    virtual std::vector<std::uint8_t> drainCoreBytes() { return {}; }

    // Depths of the core's host<->cart transport, and what it has cost. On the NES these are the N8
    // FIFO's two stages plus its cumulative dropped-byte count, which is the only way a test can see
    // that the transport lost something rather than infer it from garbled output. All zero for a core
    // with no such transport. Control-thread read; the NES implementation is mutex-guarded.
    virtual CoreTransportStats coreTransportStats() { return {}; }

    // Audio-thread: enqueue a button transition.
    virtual void pressButton(std::uint8_t /*button*/, bool /*down*/) {}

    // Audio-thread: push one byte into the system's serial input (drained
    // MSB-first over the link/serial port), scheduled at intra-block sample
    // offset `frame`. Default no-op; a core with a serial port overrides it.
    virtual void pushSerialIn(std::uint32_t /*frame*/, std::uint8_t /*byte*/) {}

    // Returns nullptr for systems without video (or before activation).
    virtual FrameBufferTriple* framebuffer() { return nullptr; }

    // Per-block MIDI output, drained by PluginDSP into DPF's writeMidiEvent
    // after onProcess.
    std::vector<::MidiEvent>&       midiOut()       { return midiOut_; }
    const std::vector<::MidiEvent>& midiOut() const { return midiOut_; }

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
    // in the per-system config; the sav-path derivation lives in TS (savPaths.ts).
    virtual std::uint32_t savSuffix() const { return 0; }
    virtual void          setSavSuffix(std::uint32_t /*suffix*/) {}

    // Explicit battery-file override. Empty => the loose `.sav` is derived from
    // romPath + savSuffix (default). Non-empty => this exact file, set when the
    // user pairs a hand-picked `.sav` with the ROM; all battery I/O targets it.
    // The sav-path derivation lives in TS (savPaths.ts).
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

    // Live output gain in dB — a universal per-system setting every backend
    // honours (smoothed at the audio frame). Default no-op so trivial backends /
    // test doubles need not implement it; SameBoy + both Mesen systems override.
    virtual void setGainDb(float /*dB*/) {}

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

    // Debugger-style write of one byte into the CPU's address space (the write
    // counterpart of readCpuByte). Returns false when the backend can't serve
    // it. The Mesen backends route it through the memory manager's debug write.
    virtual bool writeCpuByte(std::uint32_t /*addr*/, std::uint8_t /*value*/) {
        return false;
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
    // stateSnapshotSize() — a CEILING, not the live size (see the hook below) — plus the length
    // prefix, seeds the region table, and arms an immediate first publish. No-op if already enabled
    // or if the backend doesn't support savestates (stateSnapshotSize() == 0).
    bool enableStateSnapshot();

    // DSP thread: accumulate `frames`; once past the interval, capture the
    // savestate into the triple and publish. Cheap no-op when disabled.
    void publishStateSnapshot(std::uint32_t frames, double sampleRate);

    // UI thread: copy the latest published savestate into `out` (length-prefix
    // stripped). False if no snapshot has been published yet.
    bool readStateSnapshot(std::vector<std::uint8_t>& out);

    // Region table for slicing SRAM/RAM/VRAM out of a snapshot read via readStateSnapshot(). A region
    // with size 0 is absent.
    //
    // It describes the snapshot CURRENTLY PUBLISHED, not the core as it stands: publishStateSnapshot
    // refreshes it in lockstep with the blob, and only when that blob is actually published. That pairing
    // is the correctness argument — a core rebuilt under a different SameBoy model moves these offsets,
    // so a table refreshed on the rebuild instead would describe a blob that the publish may have
    // skipped, and the SRAM slice would read the wrong bytes straight into the user's .sav.
    //
    // NOT thread-safe, and deliberately not atomic. There are exactly two legal readers: the block
    // thread in SnapshotRegistry::publishAll (same thread as the writer, later in the same block), and
    // SnapshotRegistry::claim on the control thread BEFORE the system is handed to the audio thread.
    // A third caller from any other thread would be a silent data race.
    const StateRegionTable& stateRegions() const { return stateRegions_; }

    // Control/UI thread: the max savestate PAYLOAD the live snapshot triple can
    // hold (0 if state snapshots aren't enabled). An external shadow buffer — the
    // SnapshotRegistry — sizes its own slot to this so neither a variable-size (Mesen) savestate nor a
    // SameBoy model switch can overflow it, mirroring the headroom this triple was allocated with
    // (stateSnapshotSize() is the backend's ceiling).
    std::size_t stateSnapshotCapacity() const;

protected:
    std::vector<::MidiEvent> midiOut_;

    // DSP thread (or the control thread while quiescent): the backend has rebuilt its core in place —
    // a SameBoy model switch — so the published blob and region table now describe a core that is gone.
    // Arms an immediate republish, which refreshes both together within one block instead of up to
    // kStateSnapshotIntervalSec later. Allocates NOTHING: the triple was sized to a ceiling covering
    // every model the backend can switch into, which is exactly what makes this callable from the audio
    // thread (where Engine::applyConfigField runs while the audio thread owns the Engine).
    void rearmStateSnapshot();

    // State-snapshot backend hooks (default: unsupported). Overridden by
    // SameBoySystem (with region offsets) and the Mesen systems (full state,
    // no offsets). All called on the DSP thread.
    //
    // stateSnapshotSize() is the MAXIMUM payload this backend will ever capture for this cart, not the
    // current one: Mesen adds headroom because its savestates grow within a session, SameBoy because a
    // live model switch resizes the core's state. The triple is allocated once from it and never resized.
    virtual std::size_t stateSnapshotSize() const { return 0; }
    virtual bool captureStateSnapshot(std::vector<std::uint8_t>& /*dst*/) { return false; }
    virtual StateRegionTable stateSnapshotRegions() const { return {}; }

private:
    struct SnapshotEntry {
        std::unique_ptr<MemorySnapshotTriple> triple;
        std::uint32_t                         refcount = 0;
    };
    std::array<SnapshotEntry, rp::kMemoryTypeCount> snapshots_;

    // Whole-savestate snapshot. The triple is allocated once at enable and freed only in the destructor,
    // never resized mid-life, for two reasons: publishStateSnapshot can run on the audio thread, where
    // allocating is not allowed; and SnapshotRegistry sizes its own shadow slot off
    // stateSnapshotCapacity() at claim, so a resize here would silently desync the two. Backends size it
    // to a ceiling instead (see stateSnapshotSize). interval ~0.5s.
    static constexpr double                kStateSnapshotIntervalSec = 0.5;
    // Sanity bound on a single savestate. Comfortably above any GB/NES/GBA
    // state; rejects an absurd size rather than allocating wildly.
    static constexpr std::size_t           kMaxStateSnapshotBytes = 16 * 1024 * 1024;
    std::unique_ptr<MemorySnapshotTriple>  stateSnapshot_;
    StateRegionTable                       stateRegions_{};
    std::vector<std::uint8_t>              stateScratch_;       // DSP-thread capture buffer
    std::vector<std::uint8_t>              stateReadScratch_;   // read buffer (block thread, via the registry)
    std::uint64_t                          stateSnapSamples_ = 0;
    bool                                   stateSnapshotEnabled_ = false;
    // Latches the "capture too large" warning so a capture that keeps overflowing logs once per arming
    // rather than twice a second forever.
    bool                                   stateOverflowLogged_ = false;

    SystemId id_;
};
