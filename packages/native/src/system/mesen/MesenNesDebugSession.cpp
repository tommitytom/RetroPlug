#include "system/mesen/MesenNesDebugSession.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>

#include "debug/Cc65DbgParser.hpp"

#include "Core/Shared/Emulator.h"
#include "Core/Shared/EmuSettings.h"
#include "Core/Shared/CpuType.h"
#include "Core/Shared/MemoryType.h"
#include "Core/Debugger/AddressInfo.h"
#include "Core/Debugger/Debugger.h"
#include "Core/Debugger/CallstackManager.h"
#include "Core/Debugger/Profiler.h"
#include "Core/Debugger/LabelManager.h"
#include "Core/Debugger/Disassembler.h"
#include "Core/Debugger/DisassemblyInfo.h"
#include "Core/Debugger/MemoryDumper.h"
#include "Core/Debugger/ITraceLogger.h"
#include "Core/Debugger/DebugTypes.h"
#include "Core/Debugger/Breakpoint.h"
#include "Core/NES/NesConsole.h"
#include "Core/NES/NesCpu.h"
#include "Core/NES/BaseMapper.h"
#include "Core/NES/BaseNesPpu.h"
#include "Core/NES/NesTypes.h"
#include "Core/NES/APU/NesApu.h"
#include "Core/Debugger/BaseEventManager.h"
#include "Core/NES/Debugger/NesEventManager.h"

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
            // model is single-threaded here: a break sets the stop flag + records
            // the event instead of blocking for a UI thread (see Debugger.cpp).
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
        if (dbg->IsExecutionStopped()) {
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
    dbg->ResumeFromBreak();
    dbg->Step(CpuType::Nes, 1, type);
    // Generous cap: a single step is one instruction; StepOut/StepOver can run
    // many (a whole subroutine). ~50M cycles ≈ a few frames.
    rp::BreakInfo bi = execLoop(dbg, cpu, 50'000'000ull, /*reportPcBefore*/ false);

    // DISARM the step request before handing control back. ResumeFromBreak (in execLoop) only clears the
    // current break; the StepRequest this call installed outlives it, and a spent one breaks on the NEXT
    // instruction. Nothing outside this function calls ResumeFromBreak, so ordinary emulation - the audio
    // render that resumes after a step - would re-break immediately and stall forever, with the core making
    // no progress. Debugger::Run() reinstates a fresh (empty) StepRequest, which is exactly the free-running
    // state runUntilBreak sets up for the same reason; user breakpoints live in the breakpoint manager and
    // are unaffected. Also covers the cycle-cap exit, where the request was never satisfied at all.
    dbg->Run();
    return bi;
}
} // namespace

rp::ApuState MesenNesDebugSession::getApuState() {
    // A pure live-state read (no debugger needed): pull Mesen's decoded APU
    // snapshot straight off the console, mirroring nesCpuOf's console access.
    rp::ApuState out;
    auto* console = dynamic_cast<NesConsole*>(emu_->GetConsole().get());
    if (!console) return out;
    NesApu* apu = console->GetApu();
    if (!apu) return out;
    const ApuState s = apu->GetState();

    const auto square = [](const ApuSquareState& q) {
        rp::ApuSquareState o;
        o.enabled        = q.Enabled;
        o.period         = q.Period;
        o.timer          = q.Timer;
        o.duty           = q.Duty;
        o.outputVolume   = q.OutputVolume;
        o.frequency      = q.Frequency;
        o.lengthCounter  = q.LengthCounter.Counter;
        o.constantVolume = q.Envelope.ConstantVolume;
        o.envelopeVolume = q.Envelope.Volume;
        o.sweepEnabled   = q.SweepEnabled;
        o.sweepNegate    = q.SweepNegate;
        o.sweepPeriod    = q.SweepPeriod;
        o.sweepShift     = q.SweepShift;
        return o;
    };
    out.pulse1 = square(s.Square1);
    out.pulse2 = square(s.Square2);

    out.triangle.enabled       = s.Triangle.Enabled;
    out.triangle.period        = s.Triangle.Period;
    out.triangle.timer         = s.Triangle.Timer;
    out.triangle.outputVolume  = s.Triangle.OutputVolume;
    out.triangle.frequency     = s.Triangle.Frequency;
    out.triangle.lengthCounter = s.Triangle.LengthCounter.Counter;
    out.triangle.linearCounter = s.Triangle.LinearCounter;

    out.noise.enabled        = s.Noise.Enabled;
    out.noise.period         = s.Noise.Period;
    out.noise.timer          = s.Noise.Timer;
    out.noise.outputVolume   = s.Noise.OutputVolume;
    out.noise.frequency      = s.Noise.Frequency;
    out.noise.lengthCounter  = s.Noise.LengthCounter.Counter;
    out.noise.modeFlag       = s.Noise.ModeFlag;
    out.noise.constantVolume = s.Noise.Envelope.ConstantVolume;
    out.noise.envelopeVolume = s.Noise.Envelope.Volume;

    out.dmc.enabled        = s.Dmc.BytesRemaining > 0;
    out.dmc.sampleAddr     = s.Dmc.SampleAddr;
    out.dmc.sampleLength   = s.Dmc.SampleLength;
    out.dmc.bytesRemaining = s.Dmc.BytesRemaining;
    out.dmc.period         = s.Dmc.Period;
    out.dmc.outputVolume   = s.Dmc.OutputVolume;
    out.dmc.loop           = s.Dmc.Loop;
    out.dmc.irqEnabled     = s.Dmc.IrqEnabled;
    out.dmc.sampleRate     = s.Dmc.SampleRate;

    return out;
}

rp::ExpansionAudioState MesenNesDebugSession::getExpansionAudioState() {
    // Pull the active mapper's decoded expansion-audio snapshot (empty/"none"
    // when the cart has no expansion sound). Mirrors getApuState's console read.
    rp::ExpansionAudioState out;
    auto* console = dynamic_cast<NesConsole*>(emu_->GetConsole().get());
    if (!console) return out;
    BaseMapper* mapper = console->GetMapper();
    if (!mapper) return out;

    NesExpansionAudioState s = mapper->GetExpansionAudioState();
    out.chip = s.chip;
    out.channels.reserve(s.channels.size());
    for (const NesExpansionAudioChannel& c : s.channels) {
        rp::ExpansionAudioChannel o;
        o.enabled        = c.Enabled;
        o.volume         = c.Volume;
        o.outputLevel    = c.OutputLevel;
        o.period         = c.Period;
        o.frequency      = c.Frequency;
        o.block          = c.Block;
        o.duty           = c.Duty;
        o.constantOutput = c.ConstantOutput;
        o.instrument     = c.Instrument;
        o.waveLength     = c.WaveLength;
        o.activeChannels = c.ActiveChannels;
        out.channels.push_back(o);
    }
    return out;
}

rp::PpuState MesenNesDebugSession::getPpuState() {
    // A pure live-state read (no debugger needed): pull Mesen's PPU state
    // straight off the console, mirroring getApuState's console access.
    rp::PpuState out;
    auto* console = dynamic_cast<NesConsole*>(emu_->GetConsole().get());
    if (!console) return out;
    BaseNesPpu* ppu = console->GetPpu();
    if (!ppu) return out;
    NesPpuState s;
    ppu->GetState(s);

    out.scanline        = s.Scanline;
    out.cycle           = s.Cycle;
    out.frameCount      = s.FrameCount;
    out.scrollX         = s.ScrollX;
    out.videoRamAddr    = s.VideoRamAddr;
    out.tmpVideoRamAddr = s.TmpVideoRamAddr;
    out.writeToggle     = s.WriteToggle;
    out.spriteRamAddr   = s.SpriteRamAddr;

    // Reconstruct the $2000 PPUCTRL byte from the decoded flags; the nametable
    // base bits (0-1) live in the internal temp VRAM address (t bits 10-11).
    std::uint8_t control = static_cast<std::uint8_t>((s.TmpVideoRamAddr >> 10) & 0x03);
    control |= s.Control.VerticalWrite                    ? 0x04 : 0;
    control |= (s.Control.SpritePatternAddr == 0x1000)    ? 0x08 : 0;
    control |= (s.Control.BackgroundPatternAddr == 0x1000)? 0x10 : 0;
    control |= s.Control.LargeSprites                     ? 0x20 : 0;
    control |= s.Control.SecondaryPpu                     ? 0x40 : 0;
    control |= s.Control.NmiOnVerticalBlank              ? 0x80 : 0;
    out.control = control;

    std::uint8_t mask = 0;
    mask |= s.Mask.Grayscale         ? 0x01 : 0;
    mask |= s.Mask.BackgroundMask    ? 0x02 : 0;
    mask |= s.Mask.SpriteMask        ? 0x04 : 0;
    mask |= s.Mask.BackgroundEnabled ? 0x08 : 0;
    mask |= s.Mask.SpritesEnabled    ? 0x10 : 0;
    mask |= s.Mask.IntensifyRed      ? 0x20 : 0;
    mask |= s.Mask.IntensifyGreen    ? 0x40 : 0;
    mask |= s.Mask.IntensifyBlue     ? 0x80 : 0;
    out.mask = mask;

    std::uint8_t status = 0;
    status |= s.StatusFlags.SpriteOverflow ? 0x20 : 0;
    status |= s.StatusFlags.Sprite0Hit     ? 0x40 : 0;
    status |= s.StatusFlags.VerticalBlank  ? 0x80 : 0;
    out.status = status;

    // Palette RAM ($3F00-$3F1F): read the 32 entries (ReadPaletteRam normalizes the
    // $10/$14/$18/$1C mirrors), so [0] is the applied universal background color and
    // [1] the first bg-palette color — enough to read a ROM's applied bg/text colors.
    std::uint8_t pal[32];
    for (std::uint16_t i = 0; i < 32; ++i) pal[i] = ppu->ReadPaletteRam(i);
    const auto* palBytes = reinterpret_cast<const std::byte*>(pal);
    out.paletteRam = rfl::Bytestring(palBytes, palBytes + 32);

    return out;
}

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

    // Decode instructions straight off the live CPU bus instead of Mesen's disassembly cache. That
    // cache is only built for executed code while the trace logger / NesDebuggerEnabled flag is on
    // (neither is set in this manual-drive harness), so GetDisassemblyOutput returns empty "unknown
    // region" blocks at the live PC. DisassemblyInfo reads the opcode + operand bytes via the
    // MemoryDumper (mapper-resolved NesMemory) and renders through the label manager — cache-independent,
    // count-exact, and anchored exactly at `addr`. NES opcodes have fixed sizes, so cpuFlags is unused.
    MemoryDumper* dumper   = dbg->GetMemoryDumper();
    LabelManager* labels   = dbg->GetLabelManager();
    EmuSettings*  settings = dbg->GetEmulator() ? dbg->GetEmulator()->GetSettings() : nullptr;
    if (!dumper || !settings) return out;

    std::uint32_t pc = addr & 0xFFFF;
    out.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        DisassemblyInfo info(pc, /*cpuFlags*/ 0, CpuType::Nes, MemoryType::NesMemory, dumper);
        std::string text;
        info.GetDisassembly(text, pc, labels, settings);
        std::uint8_t bytes[8] = {0};
        info.GetByteCode(bytes);
        const std::uint8_t opSize = info.GetOpSize() ? info.GetOpSize() : 1; // NES: always 1..3
        out.push_back({ static_cast<std::int32_t>(pc), std::move(text), hexBytes(bytes, opSize) });
        pc = (pc + opSize) & 0xFFFF; // NES CPU space wraps at 64 KiB
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

std::vector<rp::DebugEvent> MesenNesDebugSession::drainEvents() {
    std::vector<rp::DebugEvent> out;
    Debugger* dbg = ensureDebugger();
    if (!dbg) return out;
    BaseEventManager* em = dbg->GetEventManager(CpuType::Nes);
    if (!em) return out;

    // The event manager filters snapshots by a per-category Visible flag; a
    // default config is all-invisible, so GetEvents would return nothing. Turn
    // every category on (and include the previous frame) so a caller sees the
    // full register-access log for the frame just rendered.
    NesEventViewerConfig cfg = {};
    const EventViewerCategoryCfg on{ true, 0 };
    cfg.Irq = on;                   cfg.Nmi = on;                   cfg.MarkedBreakpoints = on;
    cfg.MapperRegisterWrites = on;  cfg.MapperRegisterReads = on;
    cfg.ApuRegisterWrites = on;     cfg.ApuRegisterReads = on;
    cfg.ControlRegisterWrites = on; cfg.ControlRegisterReads = on;
    cfg.Ppu2000Write = on;          cfg.Ppu2001Write = on;          cfg.Ppu2003Write = on;
    cfg.Ppu2004Write = on;          cfg.Ppu2005Write = on;          cfg.Ppu2006Write = on;
    cfg.Ppu2007Write = on;
    cfg.Ppu2002Read = on;           cfg.Ppu2004Read = on;           cfg.Ppu2007Read = on;
    cfg.DmcDmaReads = on;           cfg.OtherDmaReads = on;         cfg.SpriteZeroHit = on;
    cfg.ShowPreviousFrameEvents = true;
    cfg.ShowNtscBorders = false;
    em->SetConfiguration(static_cast<BaseEventViewerConfig&>(cfg));

    em->TakeEventSnapshot(false);
    std::uint32_t count = em->GetEventCount();   // also filters into the sent buffer
    if (count == 0) return out;
    std::vector<DebugEventInfo> buf(count);
    em->GetEvents(buf.data(), count);            // count clamped to what was written
    out.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const DebugEventInfo& e = buf[i];
        rp::DebugEvent r;
        r.type           = static_cast<std::uint8_t>(e.Type);
        r.operationType  = static_cast<std::uint8_t>(e.Operation.Type);
        r.address        = e.Operation.Address;
        r.value          = e.Operation.Value;
        r.programCounter = e.ProgramCounter;
        r.scanline       = e.Scanline;
        r.cycle          = e.Cycle;
        out.push_back(r);
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
