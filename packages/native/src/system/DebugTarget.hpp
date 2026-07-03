#pragma once

#include <cstdint>
#include <string>
#include <vector>

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
};

} // namespace rp
