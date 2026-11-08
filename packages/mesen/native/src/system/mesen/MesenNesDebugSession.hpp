#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "system/DebugTarget.hpp"

class Emulator;
class Debugger;

// NES debugger / profiler session over Mesen's debugger. Owned (lazily) by
// MesenNesSystem; created on the first debugTarget() call so non-debug renders
// never initialise Mesen's (heavyweight) debugger.
//
// All methods must run on the emulator's emulation thread — see IDebugTarget.
class MesenNesDebugSession final : public rp::IDebugTarget {
public:
    explicit MesenNesDebugSession(Emulator* emu);
    ~MesenNesDebugSession() override;

    MesenNesDebugSession(const MesenNesDebugSession&)            = delete;
    MesenNesDebugSession& operator=(const MesenNesDebugSession&) = delete;

    void beginProfile() override;
    std::vector<rp::ProfiledFunction> readProfile() override;
    bool loadLabels(const std::string& path) override;
    std::optional<std::uint32_t> symbolAddress(const std::string& name) override;
    std::vector<rp::DisasmLine> disassemble(std::uint32_t addr, std::uint32_t count) override;
    void setTraceEnabled(bool on) override;
    std::vector<rp::TraceLine> readTrace(std::uint32_t count) override;
    std::vector<rp::CallFrame> getCallStack() override;
    rp::ApuState getApuState() override;
    rp::ExpansionAudioState getExpansionAudioState() override;
    rp::PpuState getPpuState() override;

    void setBreakpoints(const std::vector<rp::BreakpointSpec>& bps) override;
    rp::BreakInfo runUntilBreak(std::uint64_t maxCycles) override;
    rp::BreakInfo step() override;
    rp::BreakInfo stepOver() override;
    rp::BreakInfo stepOut() override;
    std::vector<rp::DebugEvent> drainEvents() override;
    std::vector<rp::BreakHit> drainBreakHits(std::uint32_t& overflow) override;

    // --- passive break capture (driven from MesenNesSystem's render loop) ---
    //
    // True once a non-empty breakpoint set is installed. Read once per instruction by the render
    // loop, so it is inline and trivially predictable; the whole mechanism costs nothing until a
    // test asks for it.
    bool breakCaptureArmed() const { return breakCaptureArmed_; }

    // Called after each cpu->Exec() while armed: if a breakpoint fired, record it and resume.
    // Mesen's headless SleepUntilResume does not block, so without this the break would simply be
    // overwritten by the next one and execution would carry on with _executionStopped stuck set.
    void captureBreakHit();

private:
    // Initialise Mesen's debugger on first use (claiming the emulation thread)
    // and put it in free-run mode. Returns the live Debugger* (nullptr if the
    // emulator isn't running).
    Debugger* ensureDebugger();

    Emulator* emu_ = nullptr;

    // Every name the loaded `.dbg` resolves (assembler labels + the C names behind them) -> CPU address,
    // for symbolAddress. Empty until loadLabels succeeds.
    std::unordered_map<std::string, std::uint32_t> symbols_;

    // Passive break capture. `breakHits_` is CAPPED: a watchpoint scoped too broadly fires on every
    // instruction, and this buffer grows on the audio thread. Past the cap only the count is kept -
    // enough to still fail a test, which is what an invariant is for.
    static constexpr std::size_t kMaxBreakHits = 4096;
    bool                      breakCaptureArmed_ = false;
    std::vector<rp::BreakHit> breakHits_;
    std::uint32_t             breakHitOverflow_  = 0;

    // drainEvents' cursor: the event-manager frame id the last call saw, and how many of that frame's
    // events it had already returned, so each call hands back only what is new.
    bool          drainPrimed_    = false;
    std::uint32_t lastFrameId_    = 0;
    std::size_t   lastFrameCount_ = 0;
};
