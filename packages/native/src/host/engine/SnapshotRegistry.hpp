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
class Project;

// The control plane's ONE door to a system's video frame / savestate / SRAM — an owned, id-keyed
// snapshot store. The DSP thread copies each live core's already-published snapshot into a
// registry-OWNED buffer (publishAll, at the end of each block); the control plane reads those owned
// copies by id (readFrame/readState/readSram) without ever walking Project or dereferencing a live
// SystemBase. That severs the control plane from the cores: reads never touch the DSP's structure.
//
// (The DSP still copies from each core's own tear-free triple, because the shared SystemBase can't
// be made to publish straight into the registry without changing it. When that core publishes
// straight into the registry the second copy collapses; the read-side contract here is unaffected.)
//
// Threading: claim()/readFrame()/readState()/readSram()/release() run on the control thread;
// publishAll() on whichever thread drives the block (the audio thread while running, the control
// thread in a direct render). Slots live in a fixed-address array so the block thread can scan them
// by id without a rehash; each slot's id is atomic. Buffers are allocated at claim (control thread,
// before the system is handed off) and freed at release (control thread, after the audio thread has
// dropped the system) — never on the audio thread. publishAll only ever writes a slot whose system
// is still in project.systems(), which is exactly the window before its release, so a published-to
// slot is never concurrently freed.
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

    // Copy each live system's latest frame (every block) + savestate/SRAM (on a coarse interval)
    // into its slot. Runs at the end of Engine::processBlock, on the block-driving thread.
    void publishAll(const Project& project, std::uint32_t frames, double sampleRate);

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

    // Free a slot when its system is deleted (control thread). Idempotent (no-op for an unknown id).
    void release(SystemId id);

private:
    struct Slot {
        std::atomic<SystemId>                 id{0};          // 0 = free; the block thread scans this
        std::uint32_t                         width = 0;
        std::uint32_t                         height = 0;
        std::unique_ptr<FrameBufferTriple>    frame;
        std::unique_ptr<MemorySnapshotTriple> state;          // [len:4 LE][savestate][headroom tail]
        std::unique_ptr<MemorySnapshotTriple> sram;
        std::unique_ptr<MemorySnapshotTriple> ram;            // work RAM (WRAM), republished every block
        std::uint32_t                         sramOffset = 0;     // SRAM slice offset within the savestate
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

    // Generous: RetroPlug never approaches this, but tests share one Project across a file's cases
    // so slots accumulate. A full pool fails the construct (logged) rather than corrupting.
    static constexpr std::size_t kMaxSlots         = 64;
    static constexpr double      kStateIntervalSec = 0.5;              // matches the core's own snapshot cadence
    static constexpr std::size_t kMaxStateBytes    = 16 * 1024 * 1024; // sanity bound on one savestate

    Slot* find(SystemId id);
    Slot* findFree();

    // Write [len:4 LE][payload] into a triple's next slot and publish it (prefix + len must fit the
    // triple; callers check). The one place the len-prefixed slot layout is written — the state slot
    // (claim + publishAll) and the WRAM slot (claim + every-block publishAll) share it.
    static void writeSized(MemorySnapshotTriple& triple, const std::uint8_t* payload, std::size_t len);

    std::array<Slot, kMaxSlots> slots_;
    std::vector<std::uint8_t>   publishScratch_;    // block-thread reuse for readStateSnapshot
    std::vector<std::uint8_t>   sramScratch_;       // block-thread reuse for a live saveSramBytes() copy
    std::vector<std::uint8_t>   stateReadScratch_;  // control-thread reuse for readState (strips the prefix)
    std::vector<std::uint8_t>   ramReadScratch_;    // control-thread reuse for readRam (strips the prefix)
};
