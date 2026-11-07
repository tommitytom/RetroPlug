#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "system/ChannelTap.hpp"
#include "system/CpuState.hpp"
#include "system/InputTypes.hpp"
#include "system/MemoryAccessor.hpp"
#include "system/MemoryType.hpp"
#include "system/SystemTypes.hpp"
#include "transport/FrameBufferTriple.hpp"
#include "transport/MemorySnapshotTriple.hpp"
#include "transport/MidiTypes.hpp"

namespace rp { class IDebugTarget; class MemoryCapability; class SramCapability; class RomCapability; }

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
    // BlipToaster's reliable link (NES; 0 when it is off): frames sent, and frames sent AGAIN because the ROM's ack
    // showed them lost - on an NTSC console, how many bytes a DMC-DMA halt took and the link recovered.
    std::uint64_t linkFrames   = 0;
    std::uint64_t linkResent   = 0;
};

// Polymorphic runtime representation of one emulator instance. Owned by its
// system node in the engine's graph (host/graph/SystemNodes.cpp). Concrete
// subclasses: SameBoySystem, MesenNesSystem (NES), MesenGbaSystem, MesenSmsSystem.
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

    // -- Per-block audio lockstep -------------------------------------------
    //
    // Every system advances through a 3-phase triad: prepareForBlock →
    // stepIfBelowTarget (looped until it returns false) → finishBlock. A coupled
    // link group's members step round-robin (the executor's coupled group over
    // rp.step/1) so serial bits ferry mid-block; an unlinked system is the
    // 1-member case. The engine's system nodes (host/graph/SystemNodes.cpp) drive
    // these three directly; nothing on the engine's path calls onProcess().
    //
    // Output contract: `outs` is `laneCount` planar buffers the system must SUM
    // into, not overwrite, so multiple systems can mix into one destination. The
    // CALLER zeroes the buffers. `laneCount` is 2 for the default single stereo
    // stream (`outs[0]`/`outs[1]` = L/R); a backend that reports a wider
    // channelLayout() receives `2 * streamCount` lanes (stream k → `outs[2k]` /
    // `outs[2k+1]`) and must branch on `laneCount` - the system node, from the
    // ports the plan wired, is the authority for how many lanes arrive.
    //
    // Defaults are inert (no-op / "done") so trivial backends and test doubles
    // need not implement them.
    virtual void prepareForBlock(const AudioBlockInfo& /*info*/) {}
    virtual bool stepIfBelowTarget(std::uint32_t /*framesNeeded*/) { return false; }
    virtual void finishBlock(const AudioBlockInfo& /*info*/, float* const* /*outs*/,
                             std::size_t /*laneCount*/) {}

    // How many lanes this block will drain into, told at the start of the block: the system node, from the ports the
    // plan wired, is the authority, and finishBlock receives the same number. A core whose step gates on a ring of its
    // own uses it to fill the ring the block actually drains, the one finishBlock will read (a NES's tap's streams
    // when the tap's ports are wired, its mix otherwise), so a host too narrow for a split, which the node wires to the
    // mix, steps on the mix's clock. 0 is "not told": a driver that does not say (a test, a tool) leaves the core its
    // own rule, and a node always says.
    virtual void setBlockLanes(std::size_t /*laneCount*/) {}

    // The largest host block this core will be asked to run (`maxBlockFrames` frames), so a backend can size the
    // per-block buffers it must not allocate in while a block runs. A system node gives it at activation - as it
    // builds the core, and again when a host grows its block, which reactivates the graph while it is quiescent - and
    // a core built directly (a test, a tool) keeps its backend's own default. Growth only: a plan's maximum never
    // shrinks. The reservation is recorded here; a backend that sizes per-block buffers overrides this, sizes from
    // the larger of `maxBlockFrames` and what is already recorded (`blockFrames(0)`), and calls this LAST: a sizing
    // that throws then leaves the recorded maximum as it was, so reactivating at it for a rollback does not retry the
    // allocation that failed.
    virtual void reserveAudio(std::uint32_t maxBlockFrames) {
        if (maxBlockFrames > maxBlockFrames_) maxBlockFrames_ = maxBlockFrames;
    }

    // The block a core built outside a plan (a test, a tool) sizes its per-block buffers for: the engine's default
    // plan size, which a test holds this to (a system core cannot include the engine), so its buffers are already
    // sized when no node has reserved for it.
    static constexpr std::uint32_t kDefaultBlockFrames = 8192;

    // The output streams this system can emit. Default = one stereo "Mix" stream
    // (the mixed console output). A backend that can split its audio reports one
    // entry per stream; that layout only takes effect when the plan wires the
    // node's stream ports (finishBlock still receives whatever laneCount the node
    // hands it), so reporting a wide layout is inert on the default path.
    virtual std::vector<ChannelStream> channelLayout() const { return {{"Mix", true}}; }

    // Fused single-system convenience entry: prepare → step-to-done → finish.
    // NOT on the engine's path (its system nodes drive the triad directly); this is
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
    // right for a stream a core simply has no use for - but a caller with ONE message to deliver (a host
    // settings SysEx) needs to know whether it was delivered or dropped, and a message too long for the
    // MIDI-framed door has nowhere else to go. The system node's rp.stage/1 asks it before delivering a
    // staged message (SystemNodes.cpp).
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

    // What this core's ingress queues have taken and dropped since it was built: bytes for the serial / N8
    // byte queues, presses for a button queue. A test reads these to hold that staged input reached the core and
    // that a push past a queue's bound is counted rather than growing it. Zero for a core with no inputs.
    // Block thread (or the control thread while quiescent): the counters are plain integers the block thread writes,
    // like the queues they count. A dropped count is a refused push, plus - on the NES - the FIFO's own modelled drops
    // (a hardware-profile fifo_a overflow, a dropped Edio reply), so a byte can be accepted by the role and dropped
    // later, counted in both.
    virtual std::uint64_t inputAccepted() const { return 0; }
    virtual std::uint64_t inputDropped() const { return 0; }

    // Audio-thread: enqueue a button transition.
    virtual void pressButton(std::uint8_t /*button*/, bool /*down*/) {}

    // Audio-thread: push one byte into the system's serial input (drained
    // MSB-first over the link/serial port), scheduled at intra-block sample
    // offset `frame`. Default no-op; a core with a serial port overrides it.
    virtual void pushSerialIn(std::uint32_t /*frame*/, std::uint8_t /*byte*/) {}

    // Returns nullptr for systems without video (or before activation).
    virtual FrameBufferTriple* framebuffer() { return nullptr; }

    // Boot-time toggle. SameBoy's node setting maps it to fastBoot; the GBA core maps it to skipBootScreen, but no
    // node setting reaches that one today (its node builds from the config's default). A core that has no boot-screen
    // skip keeps the no-op default, and the node does not offer it.
    virtual void setFastBoot(bool /*on*/) {}

    // Live output gain in dB — a universal per-system setting every backend
    // honours (smoothed at the audio frame). `immediate` snaps to the value
    // rather than ramping to it: a core's first value, from the document at the
    // node's first block. Default no-op so trivial backends / test doubles need
    // not implement it; SameBoy + both Mesen systems override.
    virtual void setGainDb(float /*dB*/, bool /*immediate*/ = false) {}

    // Cartridge battery RAM. Empty vector when the cartridge has no battery
    // or the backend doesn't yet support snapshotting.
    virtual std::vector<std::uint8_t> saveSramBytes() const { return {}; }

    // Savestate, byte for byte. Empty when the backend doesn't support savestates.
    virtual std::vector<std::uint8_t> saveStateBytes() const { return {}; }

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
    // SameBoy implements all of these. So do the Mesen NES and GBA backends, over
    // Mesen's own debugger. SMS is the one partial backend: registers, PC and the
    // side-effect-free readCpuByte, but no stepInstruction() — it keeps the
    // unsupported default (0).

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

    // -- Per-channel audio tap ----------------------------------------------
    //
    // Optional audio capability: the object that exports the core's streams one by one, behind a split routing. Null
    // when the core has no tap (every core but the NES today). One capability object rather than a virtual per caller;
    // callers branch on nullptr (no dynamic_cast), as with debugTarget(). The system owns it and it lives as long as
    // the core does: a reset or deactivation leaves the pointer valid, and a caller re-asks only after the core is
    // replaced. It may be asked on the control thread while the core runs a block (the engine reads a running plan's
    // answer before it publishes a new one), so it is a pure read: no allocation, no mutation, and the same pointer for
    // the core's lifetime.
    virtual ChannelTap* channelTap() { return nullptr; }

    // -- The control plane's capabilities (system/SystemCapabilities.hpp) ----
    //
    // rp.system.memory/1, rp.system.sram/1 and rp.system.rom/1, each null when the core does not present it. As with
    // channelTap(), one capability object per interface rather than a virtual per operation, owned by the system and
    // the same pointer for its lifetime, so the engine runs a job without naming the console. Asking for one is a pure
    // read the control thread makes while the core may run a block, and so is every [any] member of the objects
    // (memory's region list, rom's bytes and title); MemoryCapability::write is the block-side half of a write, which
    // only the block thread (or the control thread while no audio thread runs) calls.
    virtual rp::MemoryCapability* memory() { return nullptr; }
    virtual rp::SramCapability*   sram() { return nullptr; }
    virtual rp::RomCapability*    rom() { return nullptr; }

    // -- Debugger / profiler ------------------------------------------------
    //
    // Optional debug capability (profiler, disassembly, breakpoints, …).
    // Returns nullptr when the backend has no debugger (SameBoy). The Mesen
    // backends return a live session. One capability object rather than a
    // dozen virtuals; callers branch on nullptr (no dynamic_cast). The pointer
    // is owned by the system and invalidated by reset/deactivation.
    virtual rp::IDebugTarget* debugTarget() { return nullptr; }

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

    // Control thread, as a core activates (both backends call it there): idempotent. Allocates the snapshot triple
    // sized to stateSnapshotSize(), a CEILING rather than the live size (see the hook below), plus the length
    // prefix, seeds the region table, and arms an immediate first publish. No-op if already enabled
    // or if the backend doesn't support savestates (stateSnapshotSize() == 0).
    bool enableStateSnapshot();

    // DSP thread: accumulate `frames`; once past the interval, capture the
    // savestate into the triple and publish. Cheap no-op when disabled.
    void publishStateSnapshot(std::uint32_t frames, double sampleRate);

    // The block thread, from the registry's publish (SnapshotRegistry::publishSystem): copy the latest published
    // snapshot slot whole, its [len:4 LE] prefix and the savestate after it, into `dst`, which must hold the triple's
    // whole size (stateSnapshotCapacity() plus the prefix: the registry's state slot, sized from it at claim). Returns
    // the savestate's length, or 0 if nothing has been published, `dst` is too small, or the slot's length is invalid;
    // on 0, `dst`'s contents are unspecified. One copy, into memory the caller owns, so it allocates nothing and keeps
    // no buffer of its own.
    std::size_t readStateSnapshot(std::uint8_t* dst, std::size_t dstCapacity);

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
    // thread in SnapshotRegistry::publishSystem (same thread as the writer, later in the same block), and
    // SnapshotRegistry::claim on the control thread BEFORE the system is handed to the audio thread.
    // A third caller from any other thread would be a silent data race.
    const StateRegionTable& stateRegions() const { return stateRegions_; }

    // Control/UI thread: the max savestate PAYLOAD the live snapshot triple can
    // hold (0 if state snapshots aren't enabled). An external shadow buffer — the
    // SnapshotRegistry — sizes its own slot to this so neither a variable-size (Mesen) savestate nor a
    // SameBoy model switch can overflow it, mirroring the headroom this triple was allocated with
    // (stateSnapshotSize() is the backend's ceiling).
    std::size_t stateSnapshotCapacity() const;

    // DSP thread (or the control thread while quiescent): the published blob no longer describes the core - a
    // SameBoy model switch rebuilt it in place, or a host write into its battery (Engine::writeSram, through
    // SnapshotRegistry::republishSram) changed it. Arms an immediate republish, which refreshes the blob and region
    // table together within one block instead of up to kStateSnapshotIntervalSec later. Allocates NOTHING: the
    // triple was sized to a ceiling covering every model the backend can switch into, which is exactly what makes
    // this callable from the audio thread (where a system node applies its settings while the audio thread owns the
    // Engine).
    void rearmStateSnapshot();

protected:
    /** Seed a freshly-built core from its construct spec: cartridge battery first, then a savestate over
     *  the top if one was supplied. The Mesen backends end onActivate with exactly this, so it lives here
     *  rather than three times over; SameBoy seeds its own battery and restores its state inline. A
     *  savestate LAST is deliberate - it carries its own SRAM, so restoring it after the seed is what makes
     *  "boot from a savestate" mean what it says. */
    void seedSramAndState(const std::vector<std::uint8_t>& sram, const std::vector<std::uint8_t>& state) {
        if (!sram.empty()) {
            auto accessor = getMemory(rp::MemoryType::Sram, rp::AccessType::ReadWrite);
            if (accessor.valid() && accessor.size() > 0) {
                const std::size_t n = std::min(sram.size(), accessor.size());
                if (n > 0) std::memcpy(accessor.data(), sram.data(), n);
            }
        }
        if (!state.empty()) loadStateBytes(state);
    }

    // The block size a node has reserved for, in frames, or `fallback` (the backend's own default) until one does.
    std::uint32_t blockFrames(std::uint32_t fallback) const {
        return maxBlockFrames_ != 0 ? maxBlockFrames_ : fallback;
    }

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
    // Restore a savestate over the core seedSramAndState() has already put its battery into. Private
    // because a savestate is a build seed: the only restore is that one, on a core that was just
    // built, and a core never has a state loaded into it mid-life.
    virtual bool loadStateBytes(const std::vector<std::uint8_t>& /*bytes*/) { return false; }

    // Whole-savestate snapshot. The triple is allocated once at enable and freed only in the destructor,
    // never resized mid-life, for two reasons: publishStateSnapshot can run on the audio thread, where
    // allocating is not allowed; and SnapshotRegistry sizes its own shadow slot off
    // stateSnapshotCapacity() at claim, so a resize here would silently desync the two. Backends size it
    // to a ceiling instead (see stateSnapshotSize). interval ~0.5s.
    static constexpr double                kStateSnapshotIntervalSec = 0.5;
    // The largest block a node has reserved for (reserveAudio), in frames; 0 until one does.
    std::uint32_t                          maxBlockFrames_ = 0;
    // Sanity bound on a single savestate. Comfortably above any GB/NES/GBA
    // state; rejects an absurd size rather than allocating wildly.
    static constexpr std::size_t           kMaxStateSnapshotBytes = 16 * 1024 * 1024;
    std::unique_ptr<MemorySnapshotTriple>  stateSnapshot_;
    StateRegionTable                       stateRegions_{};
    std::vector<std::uint8_t>              stateScratch_;       // DSP-thread capture buffer
    std::uint64_t                          stateSnapSamples_ = 0;
    bool                                   stateSnapshotEnabled_ = false;
    // Latches the "capture too large" warning so a capture that keeps overflowing logs once per arming
    // rather than twice a second forever.
    bool                                   stateOverflowLogged_ = false;

    SystemId id_;
};
