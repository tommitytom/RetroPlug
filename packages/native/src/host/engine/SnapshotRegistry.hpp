#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "system/SystemTypes.hpp"   // SystemId
#include "transport/FrameBufferTriple.hpp"
#include "transport/MemorySnapshotTriple.hpp"

class SystemBase;

// The control plane's ONE door to a system's video frame / savestate / SRAM — an owned, id-keyed
// snapshot store. The DSP thread copies each live core's already-published snapshot into a
// registry-OWNED buffer (publishSystem, at the end of each block); the control plane reads those owned
// copies by id (readFrame/readState/readSram) without ever walking the systems or dereferencing a live
// SystemBase. That severs the control plane from the cores: reads never touch the DSP's structure.
//
// (The DSP still copies from each core's own tear-free triple, because the shared SystemBase can't
// be made to publish straight into the registry without changing it. The savestate is that one copy:
// the core's triple slot is read straight into the registry's, whose size and layout match it.)
//
// Threading: claim()/readFrame()/readState()/readSram()/release() run on the control thread;
// publishSystem() on whichever thread drives the block (the audio thread while running, the control
// thread in a direct render). Slots live in a fixed-address array so the block thread can scan them
// by id without a rehash; each slot's id is atomic. Buffers are allocated at claim (control thread,
// before the system is handed off) and freed at release (control thread, after the audio thread has
// dropped the system), never on the audio thread. publishSystem only ever writes a slot whose system
// is still in the running plan, which is exactly the window before its release, so a published-to
// slot is never concurrently freed. A claim writes only the slot it claims, before its id is published,
// and publishSystem only the slot it matched by id: no
// buffer is shared between the two threads, so a claim inside a setGraph cannot move memory a running
// plan's publish is writing.
//
// That allocation rule is why the state slot is sized to a CEILING rather than to the live savestate:
// a SameBoy model switch arrives on the audio thread (its system node applies its settings there) and resizes the
// core's state, so growing this buffer to match would be an audio-thread realloc racing a control-thread read.
// Sizing once, big enough for any model the backend can switch into, removes the need entirely.
class SnapshotRegistry {
public:
    SnapshotRegistry() = default;
    SnapshotRegistry(const SnapshotRegistry&) = delete;
    SnapshotRegistry& operator=(const SnapshotRegistry&) = delete;

    // Reserve + SEED a slot for a freshly-built system from its live state (control thread, BEFORE
    // the system is handed to the audio thread). Seeding makes a read right after construct work
    // with no block rendered. `sys` must already have enableStateSnapshot()'d (for stateRegions()).
    // Returns false only if the pool is full (the caller fails the construct).
    bool claim(SystemId id, SystemBase& sys);

    // Copy one live system's latest frame, work RAM and core-sourced battery (every block) + its savestate and any
    // savestate-sliced battery (on a coarse interval) into its slot. Runs at the end of Engine::processBlock, on the
    // block-driving thread; the engine calls it for each of the running plan's systems, in slot order.
    void publishSystem(SystemBase& system, std::uint32_t frames, double sampleRate);
    // Just the work RAM's copy, on the thread that owns the core: what a capability write completed while no audio
    // thread runs republishes, so the read the control plane makes next sees it without a block (Engine::submitJob).
    void publishRam(SystemBase& system);

    // Control-thread reads of the OWNED copies.
    struct Frame {
        std::uint32_t             width = 0;
        std::uint32_t             height = 0;
        bool                      published = false;
        std::vector<std::uint8_t> data;   // raw XRGB8888, width*height*4 bytes
    };
    Frame readFrame(SystemId id);
    std::optional<std::vector<std::uint8_t>> readState(SystemId id);   // whole savestate
    std::optional<std::vector<std::uint8_t>> readSram(SystemId id);    // SRAM (savestate slice or live core)
    std::optional<std::vector<std::uint8_t>> readRam(SystemId id);     // work RAM (WRAM), published EVERY block
    // The published WRAM length WITHOUT copying it - so a bounds check costs nothing. 0 when the id is
    // gone or the core publishes no RAM (a region past kMaxRamBytes is skipped, so readRam is null too).
    std::size_t ramSize(SystemId id);
    // The published battery length, likewise copy-free. 0 when the id is gone or the cart has none.
    std::size_t sramSize(SystemId id);

    // Republish `sys`'s battery NOW instead of at its next publish - for a host write into it (Engine::writeSram,
    // applied as a capability write), after which a stale copy would hand the TS auto-save mirror the PRE-write
    // battery to flush over the file. Same threading as publishSystem: the thread that owns the core (the block
    // thread, or the control thread while quiescent). The live battery is copied at once; a savestate-sliced one
    // (SameBoy) also has a fresh savestate capture forced onto the next block's publish, which would otherwise slice
    // the pre-write battery back over it.
    void republishSram(SystemBase& sys);

    // Free a slot when its system is deleted (control thread). Idempotent (no-op for an unknown id).
    void release(SystemId id);

    // How many systems' snapshots the pool holds at once: a full pool fails the construct.
    static constexpr std::size_t kSlotCount = 64;

    // The slots in use (control thread): what a test holds the registry to across a removal or a refused document.
    std::size_t liveCount() const;

private:
    struct Slot {
        std::atomic<SystemId>                 id{0};          // 0 = free; the block thread scans this
        std::uint32_t                         width = 0;
        std::uint32_t                         height = 0;
        std::unique_ptr<FrameBufferTriple>    frame;
        std::unique_ptr<MemorySnapshotTriple> state;          // [len:4 LE][savestate][headroom tail]
        std::unique_ptr<MemorySnapshotTriple> sram;
        std::unique_ptr<MemorySnapshotTriple> ram;            // work RAM (WRAM), republished every block
        // NOTE: the SRAM slice offset is deliberately NOT cached here. It moves when the core is rebuilt
        // under a different SameBoy model (an HLE-SGB state carries a ~74 KB section ahead of the cart
        // RAM), so publishSystem re-reads it from the region table published alongside each blob.
        bool                                  sramFromCore = false; // SRAM published live (saveSramBytes), not sliced
        std::uint64_t                         sampleAccum = 0;    // samples since the last state/sram publish
    };

    // The state slot stores a 4-byte little-endian length prefix ahead of the savestate, so a
    // variable-size (Mesen) savestate stays tear-free in one publish and the slot can carry headroom
    // (mirrors SystemBase's own snapshot triple). SameBoy's fixed-size savestate uses the same layout.
    static constexpr std::size_t kStateLenPrefix = 4;
    static constexpr std::size_t kRamLenPrefix   = 4;               // WRAM slot mirrors the state layout: [len:4 LE][wram]
    static constexpr std::size_t kMaxSramBytes   = 4 * 1024 * 1024;  // sanity bound on one battery image
    static constexpr std::size_t kMaxRamBytes    = 64 * 1024;        // WRAM cap: fits GB 8/32 KB + NES 2 KB; a larger region (GBA EWRAM) is skipped → readRam null

    // Generous: RetroPlug never approaches this, but tests share one engine across a file's cases
    // so slots accumulate. A full pool fails the construct (logged) rather than corrupting.
    static constexpr std::size_t kMaxSlots         = kSlotCount;
    static constexpr double      kStateIntervalSec = 0.5;              // matches the core's own snapshot cadence
    static constexpr std::size_t kMaxStateBytes    = 16 * 1024 * 1024; // sanity bound on one savestate

    Slot* find(SystemId id);
    void  publishRamFor(Slot& s, SystemBase& system);
    Slot* findFree();

    // Write [len:4 LE][payload] into a triple's next slot and publish it (prefix + len must fit the
    // triple; callers check): the state slot's seed (claim) and the WRAM slot (claim + every-block
    // publishSystem). publishSystem's state republish reads the core's slot, already in this layout.
    static void writeSized(MemorySnapshotTriple& triple, const std::uint8_t* payload, std::size_t len);

    std::array<Slot, kMaxSlots> slots_;
    std::vector<std::uint8_t>   stateReadScratch_;  // control-thread reuse for readState (strips the prefix)
    std::vector<std::uint8_t>   ramReadScratch_;    // control-thread reuse for readRam (strips the prefix)
};
