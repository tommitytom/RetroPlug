#include "system/mesen/MesenNesDebugSession.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>

#include "debug/Cc65DbgParser.hpp"

#include "Core/Shared/Emulator.h"
#include "Core/Shared/CpuType.h"
#include "Core/Shared/MemoryType.h"
#include "Core/Debugger/AddressInfo.h"
#include "Core/Debugger/Debugger.h"
#include "Core/Debugger/CallstackManager.h"
#include "Core/Debugger/Profiler.h"
#include "Core/Debugger/LabelManager.h"
#include "Core/Debugger/Disassembler.h"
#include "Core/Debugger/ITraceLogger.h"
#include "Core/Debugger/DebugTypes.h"
#include "Core/Debugger/Breakpoint.h"
#include "Core/NES/NesConsole.h"
#include "Core/NES/NesCpu.h"

MesenNesDebugSession::MesenNesDebugSession(Emulator* emu) : emu_(emu) {}

MesenNesDebugSession::~MesenNesDebugSession() {
    // Tear the debugger down while the Emulator (and its DebugHud) is still
    // fully alive — ~Emulator destroys DebugHud before the Debugger, so letting
    // the debugger outlive us into ~Emulator segfaults in
    // ScriptManager::~ScriptManager -> DebugHud::ClearScreen(). The session is
    // destroyed before emu_ (member order in MesenNesSystem), so emu_ is valid
    // here. Must run on the emulation thread.
    if (emu_ && emu_->InternalGetDebugger()) {
        emu_->SetEmulationThreadId(std::this_thread::get_id());
        emu_->StopDebugger();
    }
}

Debugger* MesenNesDebugSession::ensureDebugger() {
    if (!emu_ || !emu_->IsRunning()) return nullptr;
    // Every debugger call must run on the emulation thread so Mesen's
    // DebugBreakHelper no-ops (the harness is single-threaded). Claim it.
    emu_->SetEmulationThreadId(std::this_thread::get_id());
    if (!emu_->InternalGetDebugger()) {
        emu_->InitDebugger();
        if (Debugger* dbg = emu_->InternalGetDebugger()) {
            // This build drives the CPU manually on one thread (Emulator::Run()
            // is never called, so there is no emulation thread). Mesen's break
            // model assumes a second thread to resume it; headless mode makes a
            // break capture + return instead of blocking. Required here, not
            // optional.
            dbg->SetHeadlessMode(true);
            dbg->Run(); // clear any power-on/reset break -> free-run
        }
    }
    return emu_->InternalGetDebugger();
}

void MesenNesDebugSession::beginProfile() {
    Debugger* dbg = ensureDebugger();
    if (!dbg) return;
    CallstackManager* cs = dbg->GetCallstackManager(CpuType::Nes);
    if (Profiler* p = cs ? cs->GetProfiler() : nullptr) {
        p->Reset();
    }
}

std::vector<rp::ProfiledFunction> MesenNesDebugSession::readProfile() {
    std::vector<rp::ProfiledFunction> out;
    Debugger* dbg = ensureDebugger();
    if (!dbg) return out;
    CallstackManager* cs = dbg->GetCallstackManager(CpuType::Nes);
    Profiler* p = cs ? cs->GetProfiler() : nullptr;
    if (!p) return out;

    // GetProfilerData fills up to 100k entries (Profiler.cpp).
    std::vector<ProfiledFunction> buf(100000);
    std::uint32_t count = 0;
    p->GetProfilerData(buf.data(), count);

    LabelManager* labels = dbg->GetLabelManager();
    out.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const ProfiledFunction& f = buf[i];
        rp::ProfiledFunction r;
        r.address         = f.Address.Address;
        r.exclusiveCycles = f.ExclusiveCycles;
        r.inclusiveCycles = f.InclusiveCycles;
        r.callCount       = f.CallCount;
        r.minCycles       = (f.MinCycles == UINT64_MAX) ? 0 : f.MinCycles;
        r.maxCycles       = f.MaxCycles;
        if (labels) r.label = labels->GetLabel(f.Address);
        out.push_back(std::move(r));
    }
    std::sort(out.begin(), out.end(),
              [](const rp::ProfiledFunction& a, const rp::ProfiledFunction& b) {
                  return a.exclusiveCycles > b.exclusiveCycles;
              });
    return out;
}

bool MesenNesDebugSession::loadLabels(const std::string& path) {
    Debugger* dbg = ensureDebugger();
    if (!dbg) return false;
    const std::vector<rp::DbgSymbol> syms = rp::parseCc65Dbg(path);
    if (syms.empty()) return false;
    LabelManager* labels = dbg->GetLabelManager();
    if (!labels) return false;
    for (const auto& s : syms) {
        // The .dbg `val` is a CPU-space address; translate to the absolute ROM
        // address the profiler/labels key on (mapper-agnostic).
        const AddressInfo abs = dbg->GetAbsoluteAddress(
            { static_cast<std::int32_t>(s.address), MemoryType::NesMemory });
        if (abs.Address >= 0)
            labels->SetLabel(static_cast<std::uint32_t>(abs.Address), abs.Type, s.name, "");
    }
    return true;
}

namespace {
std::string hexBytes(const std::uint8_t* b, std::size_t n) {
    static const char* k = "0123456789abcdef";
    std::string out;
    out.reserve(n * 3);
    for (std::size_t i = 0; i < n; ++i) {
        if (i) out += ' ';
        out += k[(b[i] >> 4) & 0xF];
        out += k[b[i] & 0xF];
    }
    return out;
}

NesCpu* nesCpuOf(Emulator* emu) {
    if (!emu) return nullptr;
    auto* console = dynamic_cast<NesConsole*>(emu->GetConsole().get());
    return console ? console->GetCpu() : nullptr;
}

// Drive the CPU one instruction at a time until the debugger reports a stop
// (breakpoint or step) or `maxCycles` elapse. Execution stops just AFTER the
// triggering instruction (the headless SleepUntilResume returns rather than
// blocking), so `reportPcBefore` chooses whether the result PC is the
// triggering instruction's address (breakpoints) or the new PC (steps).
rp::BreakInfo execLoop(Debugger* dbg, NesCpu* cpu, std::uint64_t maxCycles,
                       bool reportPcBefore) {
    rp::BreakInfo bi;
    std::uint64_t cyc = 0;
    while (cyc < maxCycles) {
        const std::uint16_t pcBefore = cpu->GetState().PC;
        const std::uint64_t cBefore  = cpu->GetState().CycleCount;
        cpu->Exec();
        cyc += cpu->GetState().CycleCount - cBefore;
        if (dbg->IsHeadlessStopped()) {
            bi.broke = true;
            bi.pc = reportPcBefore ? pcBefore : cpu->GetState().PC;
            bi.breakpointId = dbg->GetLastBreakEvent().BreakpointId;
            dbg->ResumeFromBreak();
            return bi;
        }
    }
    bi.pc = cpu->GetState().PC;
    return bi;
}

rp::BreakInfo doStep(Debugger* dbg, NesCpu* cpu, StepType type) {
    dbg->SetHeadlessMode(true);
    dbg->ResumeFromBreak();
    dbg->Step(CpuType::Nes, 1, type);
    // Generous cap: a single step is one instruction; StepOut/StepOver can run
    // many (a whole subroutine). ~50M cycles ≈ a few frames.
    return execLoop(dbg, cpu, 50'000'000ull, /*reportPcBefore*/ false);
}
} // namespace

void MesenNesDebugSession::setBreakpoints(const std::vector<rp::BreakpointSpec>& specs) {
    Debugger* dbg = ensureDebugger();
    if (!dbg) return;
    std::vector<Breakpoint> bps;
    bps.reserve(specs.size());
    for (const auto& s : specs) {
        BreakpointTypeFlags t = BreakpointTypeFlags::Execute;
        if (s.type == "read")       t = BreakpointTypeFlags::Read;
        else if (s.type == "write") t = BreakpointTypeFlags::Write;
        const std::int32_t start = static_cast<std::int32_t>(s.start);
        const std::int32_t end   = static_cast<std::int32_t>(s.end ? s.end : s.start);
        // Breakpoints are matched in the CPU address space (NesMemory): execute
        // against PC, read/write against the accessed address.
        bps.push_back(Breakpoint::Create(CpuType::Nes, MemoryType::NesMemory, t,
                                         start, end, s.condition.c_str(), true));
    }
    dbg->SetBreakpoints(bps.empty() ? nullptr : bps.data(),
                        static_cast<std::uint32_t>(bps.size()));
}

rp::BreakInfo MesenNesDebugSession::runUntilBreak(std::uint64_t maxCycles) {
    Debugger* dbg = ensureDebugger();
    NesCpu* cpu = nesCpuOf(emu_);
    if (!dbg || !cpu) return {};
    dbg->SetHeadlessMode(true);
    dbg->ResumeFromBreak();
    dbg->Run(); // free-run (no pending step); breakpoints still fire
    return execLoop(dbg, cpu, maxCycles, /*reportPcBefore*/ true);
}

rp::BreakInfo MesenNesDebugSession::step() {
    Debugger* dbg = ensureDebugger();
    NesCpu* cpu = nesCpuOf(emu_);
    if (!dbg || !cpu) return {};
    return doStep(dbg, cpu, StepType::Step);
}

rp::BreakInfo MesenNesDebugSession::stepOver() {
    Debugger* dbg = ensureDebugger();
    NesCpu* cpu = nesCpuOf(emu_);
    if (!dbg || !cpu) return {};
    return doStep(dbg, cpu, StepType::StepOver);
}

rp::BreakInfo MesenNesDebugSession::stepOut() {
    Debugger* dbg = ensureDebugger();
    NesCpu* cpu = nesCpuOf(emu_);
    if (!dbg || !cpu) return {};
    return doStep(dbg, cpu, StepType::StepOut);
}

std::vector<rp::DisasmLine> MesenNesDebugSession::disassemble(std::uint32_t addr,
                                                             std::uint32_t count) {
    std::vector<rp::DisasmLine> out;
    Debugger* dbg = ensureDebugger();
    if (!dbg || count == 0) return out;
    Disassembler* dis = dbg->GetDisassembler();
    if (!dis) return out;

    std::vector<CodeLineData> rows(count);
    const std::uint32_t n = dis->GetDisassemblyOutput(CpuType::Nes, addr, rows.data(), count);
    out.reserve(n);
    for (std::uint32_t i = 0; i < n; ++i) {
        const CodeLineData& r = rows[i];
        out.push_back({ r.Address, std::string(r.Text),
                        hexBytes(r.ByteCode, r.OpSize) });
    }
    return out;
}

void MesenNesDebugSession::setTraceEnabled(bool on) {
    Debugger* dbg = ensureDebugger();
    if (!dbg) return;
    ITraceLogger* tl = dbg->GetTraceLogger(CpuType::Nes);
    if (!tl) return;
    TraceLoggerOptions o = {};
    o.Enabled = on;
    o.UseLabels = true;
    std::snprintf(o.Format, sizeof(o.Format),
                  "[PC,4h]  [Disassembly]  A:[A,2h] X:[X,2h] Y:[Y,2h] SP:[SP,2h]");
    tl->SetOptions(o);
}

std::vector<rp::TraceLine> MesenNesDebugSession::readTrace(std::uint32_t count) {
    std::vector<rp::TraceLine> out;
    Debugger* dbg = ensureDebugger();
    if (!dbg || count == 0) return out;
    std::vector<TraceRow> rows(count);
    const std::uint32_t n = dbg->GetExecutionTrace(rows.data(), 0, count);
    out.reserve(n);
    for (std::uint32_t i = 0; i < n; ++i) {
        const TraceRow& r = rows[i];
        out.push_back({ r.ProgramCounter, std::string(r.LogOutput, r.LogSize) });
    }
    return out;
}

std::vector<rp::CallFrame> MesenNesDebugSession::getCallStack() {
    std::vector<rp::CallFrame> out;
    Debugger* dbg = ensureDebugger();
    if (!dbg) return out;
    CallstackManager* cs = dbg->GetCallstackManager(CpuType::Nes);
    if (!cs) return out;
    LabelManager* labels = dbg->GetLabelManager();

    std::vector<StackFrameInfo> frames(256);
    std::uint32_t n = 0;
    cs->GetCallstack(frames.data(), n);
    out.reserve(n);
    for (std::uint32_t i = 0; i < n; ++i) {
        const StackFrameInfo& f = frames[i];
        rp::CallFrame cf;
        cf.address = f.AbsTarget.Address;
        if (labels) cf.label = labels->GetLabel(f.AbsTarget);
        out.push_back(std::move(cf));
    }
    return out;
}
