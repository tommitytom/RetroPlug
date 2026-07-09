#pragma once

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
    std::vector<rp::DisasmLine> disassemble(std::uint32_t addr, std::uint32_t count) override;
    void setTraceEnabled(bool on) override;
    std::vector<rp::TraceLine> readTrace(std::uint32_t count) override;
    std::vector<rp::CallFrame> getCallStack() override;
    rp::ApuState getApuState() override;

    void setBreakpoints(const std::vector<rp::BreakpointSpec>& bps) override;
    rp::BreakInfo runUntilBreak(std::uint64_t maxCycles) override;
    rp::BreakInfo step() override;
    rp::BreakInfo stepOver() override;
    rp::BreakInfo stepOut() override;
    std::vector<rp::DebugEvent> drainEvents() override;

private:
    // Initialise Mesen's debugger on first use (claiming the emulation thread)
    // and put it in free-run mode. Returns the live Debugger* (nullptr if the
    // emulator isn't running).
    Debugger* ensureDebugger();

    Emulator* emu_ = nullptr;
};
