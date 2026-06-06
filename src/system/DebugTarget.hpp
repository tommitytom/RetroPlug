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
