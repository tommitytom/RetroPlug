#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <rfl/Bytestring.hpp>

namespace rp {

// One function's profiler sample. Cycles are emulated master-clock cycles.
// `address` is the absolute ROM offset the profiler keys on; `label` is the
// resolved symbol name (empty until symbols are loaded). Sorted by the harness
// on `exclusiveCycles` (the time spent in this function itself = the bottleneck
// signal).
struct ProfiledFunction {
    std::int32_t  address         = 0;
    std::string   label;
    std::uint64_t exclusiveCycles = 0;
    std::uint64_t inclusiveCycles = 0;
    std::uint64_t callCount        = 0;
    std::uint64_t minCycles        = 0;
    std::uint64_t maxCycles        = 0;
};

// One disassembled instruction. `address` is the CPU address; `text` is the
// mnemonic + operands (symbol-resolved when labels are loaded); `bytes` is the
// instruction's machine bytes as hex.
struct DisasmLine {
    std::int32_t address = 0;
    std::string  text;
    std::string  bytes;
};

// One row of the execution trace (most-recent first). `text` is the formatted
// disassembly + register state Mesen logged for that instruction.
struct TraceLine {
    std::uint32_t pc = 0;
    std::string   text;
};

// One call-stack frame: the entered function's address + resolved label
// (innermost frame last).
struct CallFrame {
    std::int32_t address = 0;
    std::string  label;
};

// A breakpoint to install. `type`: "execute" (break when PC enters [start,end])
// | "read" | "write" (break on a CPU access to [start,end]). `end` 0 means a
// single address (== start). `condition` is an optional Mesen expression
// (e.g. "A == 0x90").
struct BreakpointSpec {
    std::string   type;
    std::uint32_t start     = 0;
    std::uint32_t end       = 0;
    std::string   condition;
};

// Result of runUntilBreak / step. `broke` is false when the cycle cap was hit
// instead. `pc` is the triggering address (runUntilBreak) or the new PC (step).
// Note: execution stops just AFTER the triggering instruction — registers
// reflect its effect. `breakpointId` is -1 for a step or the cap.
struct BreakInfo {
    bool          broke        = false;
    std::uint32_t pc           = 0;
    std::int32_t  breakpointId = -1;
};

// -- NES APU per-channel state ----------------------------------------------
//
// A snapshot of the five NES APU channels' DECODED musical state — period,
// duty, volume, length, enable, live output — mirroring a curated subset of
// Mesen's ApuState (deps/mesen/Core/NES/NesTypes.h). The raw $4000-$4013 APU
// registers are WRITE-ONLY on the NES (they read back as open bus), so a
// MIDI-driven ROM's effect on the sound chip can't be seen via readMemory; this
// exposes what the emulator decodes internally instead. `frequency` is the
// derived pitch in Hz. `outputVolume` is the instantaneous mixer output — for
// square/triangle it is gated by the duty/sequence position, so it oscillates.
// A *sounding note* shows as `period` > 0 and (square/noise) `envelopeVolume` >
// 0; `enabled` is only the channel's $4015 on/off switch, which a ROM often
// sets once at init, so it is NOT "a note is sounding". (`period` == 0 makes
// `frequency` meaninglessly high, not 0 — gate on `period`/`outputVolume`.)

struct ApuSquareState {
    bool          enabled        = false;  // $4015 channel switch (not "sounding")
    std::uint16_t period         = 0;      // raw 11-bit timer reload
    std::uint16_t timer          = 0;      // live countdown
    std::uint8_t  duty           = 0;      // 0-3
    std::uint8_t  outputVolume   = 0;      // live mixer output 0-15 (duty-gated)
    double        frequency      = 0.0;    // Hz
    std::uint8_t  lengthCounter  = 0;
    bool          constantVolume = false;  // fixed volume vs envelope decay
    std::uint8_t  envelopeVolume = 0;      // the set volume 0-15
    bool          sweepEnabled   = false;
    bool          sweepNegate    = false;
    std::uint8_t  sweepPeriod    = 0;
    std::uint8_t  sweepShift     = 0;
};

struct ApuTriangleState {
    bool          enabled       = false;
    std::uint16_t period        = 0;
    std::uint16_t timer         = 0;
    std::uint8_t  outputVolume  = 0;       // triangle step 0-15 (no volume ctrl)
    double        frequency     = 0.0;     // Hz
    std::uint8_t  lengthCounter = 0;
    std::uint8_t  linearCounter = 0;
};

struct ApuNoiseState {
    bool          enabled        = false;
    std::uint16_t period         = 0;      // period index -> pitch
    std::uint16_t timer          = 0;
    std::uint8_t  outputVolume   = 0;
    double        frequency      = 0.0;    // Hz
    std::uint8_t  lengthCounter  = 0;
    bool          modeFlag       = false;  // metallic/periodic vs normal noise
    bool          constantVolume = false;
    std::uint8_t  envelopeVolume = 0;      // the set volume 0-15
};

struct ApuDmcState {
    bool          enabled        = false;  // bytes remaining > 0 (sample playing)
    std::uint16_t sampleAddr     = 0;      // $C000-$FFFF start
    std::uint16_t sampleLength   = 0;      // bytes
    std::uint16_t bytesRemaining = 0;
    std::uint16_t period         = 0;      // rate-index timer
    std::uint8_t  outputVolume   = 0;      // 7-bit DAC level 0-127
    bool          loop           = false;
    bool          irqEnabled     = false;
    double        sampleRate     = 0.0;    // Hz
};

// The five NES APU channels' decoded state. Channel names follow NES/evermidi
// convention (pulse1/pulse2), not Mesen's Square1/Square2.
struct ApuState {
    ApuSquareState   pulse1;
    ApuSquareState   pulse2;
    ApuTriangleState triangle;
    ApuNoiseState    noise;
    ApuDmcState      dmc;
};

// -- NES expansion audio state ----------------------------------------------
//
// Decoded live state of the cart's expansion audio chip (VRC6/VRC7/Sunsoft-5B/
// Namco-163) — the analogue of ApuState for the mapper sound hardware, whose
// registers are also write-only. `chip` names the active chip ("none" when the
// cart has no expansion audio); `channels` holds its voices in chip order.
//
// The per-channel struct is a superset — a field is populated when meaningful
// for the chip and left 0/false otherwise. `volume` is NORMALIZED to 0 (silent)
// .. 15 (loudest) across all chips so "silent means low volume" reads the same
// everywhere; `period` stays the chip-native pitch register. See the three
// diagnostic fields: `constantOutput` (VRC6 "ignore duty" mode → DC/no tone),
// `instrument` (VRC7 patch), and the normalized `volume`.
struct ExpansionAudioChannel {
    bool          enabled        = false;  // channel enabled / keyed on
    std::uint8_t  volume         = 0;      // normalized 0=silent .. 15=loudest
    std::uint32_t outputLevel    = 0;      // live decoded output magnitude (0 = silent right now)
    std::uint32_t period         = 0;      // chip-native pitch reg (VRC6/5B timer, N163 18-bit, VRC7 fnum)
    std::uint8_t  block          = 0;      // VRC7 octave 0-7 (0 for other chips)
    std::uint8_t  duty           = 0;      // VRC6 pulse duty 0-7 (0 for other chips)
    bool          constantOutput = false;  // VRC6 pulse "ignore duty" mode bit → DC, no tone
    std::uint8_t  instrument     = 0;      // VRC7 patch 0=custom,1-15 ROM (0 for other chips)
};

struct ExpansionAudioState {
    std::string chip;                                   // "none"|"vrc6"|"vrc7"|"s5b"|"n163"
    std::vector<ExpansionAudioChannel> channels;        // in the chip's channel order
};

// -- NES PPU state ----------------------------------------------------------
//
// A snapshot of the NES PPU's live timing + register state, mirroring a curated
// subset of Mesen's NesPpuState (deps/mesen/Core/NES/NesTypes.h). `scanline`
// (-1..260, -1 = pre-render) and `cycle` (0..340) are the current dot position;
// `frameCount` increments once per rendered frame (so > 0 once the emulator has
// advanced). `control`/`mask`/`status` are the $2000/$2001/$2002 register bytes
// reconstructed from the decoded flag structs (the nametable-base bits of
// $2000 come from the internal temp VRAM address). `scrollX` is the fine-X
// scroll; `videoRamAddr`/`tmpVideoRamAddr` are the current/temp VRAM addresses
// (v/t); `writeToggle` is the $2005/$2006 first/second-write latch (w);
// `spriteRamAddr` is the OAM address ($2003). This is the flat state only — the
// tilemap / sprite VIEWERS (caller-allocated buffers) are not exposed. `paletteRam` is
// the 32-byte palette RAM ($3F00-$3F1F): [0] = universal background color, then the bg /
// sprite palettes (each a 6-bit NES index) — enough to read a ROM's applied bg/text colors.
struct PpuState {
    std::int32_t  scanline        = 0;
    std::uint32_t cycle           = 0;
    std::uint32_t frameCount      = 0;
    std::uint8_t  control         = 0;      // $2000 PPUCTRL
    std::uint8_t  mask            = 0;      // $2001 PPUMASK
    std::uint8_t  status          = 0;      // $2002 PPUSTATUS
    std::uint8_t  scrollX         = 0;      // fine-X scroll
    std::uint16_t videoRamAddr    = 0;      // current VRAM address (v)
    std::uint16_t tmpVideoRamAddr = 0;      // temp VRAM address (t)
    bool          writeToggle     = false;  // $2005/$2006 write latch (w)
    std::uint8_t  spriteRamAddr   = 0;      // $2003 OAM address
    rfl::Bytestring paletteRam;             // 32-byte palette RAM ($3F00-$3F1F)
};

// One Mesen event-viewer event (a register read/write, NMI, IRQ, DMA read,
// etc.) captured for the most recent PPU frame. Mesen's event manager logs
// these per frame and wipes them at each frame boundary, so drainEvents()
// snapshots the current (in-progress) frame plus the previous one. `type` is
// the DebugEventType ordinal (0=Register, 1=Nmi, 2=Irq, 3=Breakpoint,
// 4=BgColorChange, 5=SpriteZeroHit, 6=DmcDmaRead, 7=DmaRead); `operationType`
// is the MemoryOperationType ordinal (0=Read, 1=Write, ...). `address`/`value`
// are the register access (value is -1 for a read with no captured value).
// `scanline`/`cycle` are the PPU position the event fired at.
struct DebugEvent {
    std::uint8_t  type           = 0;
    std::uint8_t  operationType  = 0;
    std::uint32_t address        = 0;
    std::int32_t  value          = 0;
    std::uint32_t programCounter = 0;
    std::int32_t  scanline       = 0;
    std::uint16_t cycle          = 0;
};

// Optional per-system debugger / profiler, returned by
// SystemBase::debugTarget() (nullptr when the backend has no debugger).
// Implemented by the Mesen backends on top of Mesen's debugger; SameBoy has
// none. Kept off SystemBase as a single capability object so the (large) debug
// surface doesn't bloat the base class and callers branch on nullptr rather
// than dynamic_cast.
//
// Threading: all calls must come from the emulator's emulation thread (the
// harness's single thread). Mesen's DebugBreakHelper no-ops there, so there is
// no cross-thread break/deadlock.
class IDebugTarget {
public:
    virtual ~IDebugTarget() = default;

    // Profiling. `beginProfile` ensures the debugger is initialised and clears
    // the profiler; drive execution via the normal run path (the harness's
    // runMs) between begin and read. `readProfile` returns the accumulated
    // per-function stats sorted by exclusive cycles (descending).
    virtual void beginProfile() = 0;
    virtual std::vector<ProfiledFunction> readProfile() = 0;

    // Load a cc65 `.dbg` symbol file so profiler/disassembly/call-stack output
    // shows function names. Returns false on read/parse failure or if
    // unsupported. Safe to call before or after beginProfile.
    virtual bool loadLabels(const std::string& path) = 0;

    // Disassemble `count` instructions starting at CPU address `addr`.
    virtual std::vector<DisasmLine> disassemble(std::uint32_t addr, std::uint32_t count) = 0;

    // Enable/disable the execution trace logger. Enable before the run window;
    // read the captured rows afterwards with readTrace().
    virtual void setTraceEnabled(bool on) = 0;
    // Most recent `count` executed instructions (row 0 = most recent).
    virtual std::vector<TraceLine> readTrace(std::uint32_t count) = 0;

    // Current call stack (outermost first), each frame's entered function +
    // resolved label.
    virtual std::vector<CallFrame> getCallStack() = 0;

    // Snapshot the NES APU's five channels' decoded state (period/duty/volume/
    // length/enable/output). The raw APU registers are write-only on the NES,
    // so this is the way to observe what a MIDI-driven ROM did to the sound
    // chip. Call after advancing the emulator.
    virtual ApuState getApuState() = 0;

    // Snapshot the cart's expansion audio chip (VRC6/VRC7/Sunsoft-5B/Namco-163)
    // decoded per-channel state. Like getApuState, the registers are write-only,
    // so this is how a test observes what a MIDI-driven ROM programmed into the
    // expansion sound chip. Default {} (chip "none") for backends / carts with
    // no expansion audio. Call after advancing the emulator.
    virtual ExpansionAudioState getExpansionAudioState() { return {}; }

    // Snapshot the NES PPU's live timing + register state (scanline/cycle/
    // frameCount + the $2000/$2001/$2002 register bytes + scroll). Call after
    // advancing the emulator. Default {} for backends without a NES PPU. The
    // tilemap/sprite/palette viewers are deliberately not exposed here.
    virtual PpuState getPpuState() { return {}; }

    // -- Breakpoints / stepping ---------------------------------------------
    //
    // Install breakpoints (replacing any existing; empty clears). Don't drive
    // with runMs while breakpoints are active — use runUntilBreak, which steps
    // the CPU one instruction at a time and stops when a breakpoint fires.
    virtual void setBreakpoints(const std::vector<BreakpointSpec>& bps) = 0;
    virtual BreakInfo runUntilBreak(std::uint64_t maxCycles) = 0;

    // Single-step: into the next instruction, over a subroutine call, or out of
    // the current subroutine.
    virtual BreakInfo step() = 0;
    virtual BreakInfo stepOver() = 0;
    virtual BreakInfo stepOut() = 0;

    // Drain the events Mesen's event viewer logged for the most recent frame
    // (register reads/writes to APU/PPU/mapper regs, NMI/IRQ, DMA reads).
    // Frame-scoped: cleared at each PPU frame boundary, so call after advancing
    // the emulator. Default {} for backends without an event viewer.
    virtual std::vector<DebugEvent> drainEvents() { return {}; }
};

} // namespace rp
