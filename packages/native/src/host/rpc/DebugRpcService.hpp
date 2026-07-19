#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <rfl/Bytestring.hpp>

#include "system/CpuState.hpp"    // rp::CpuRegister
#include "system/DebugTarget.hpp" // rp::ApuState / PpuState / DebugEvent / TraceLine / BreakInfo /
                                  // BreakpointSpec / ProfiledFunction / DisasmLine / CallFrame

class Engine;

// The live-core debug / inspection surface (spec/09-cli-debugging.md) as a THIN RPC layer over the
// Engine. Unlike the emulator snapshot reads (EngineRpcService), these resolve the LIVE core via
// engine_.findSystem (NOT the snapshot registry), so — like dspAllocStats — they are only valid on the
// control thread while the audio thread is not started: the CLI's single-threaded direct-render regime.
// Most route through the system's rp::IDebugTarget (a Mesen NES target; null on SameBoy/GBA →
// empty/false); the CPU peek/poke/step and register/runUntilPc are plain SystemBase virtuals. Only the
// CLI binds this facet (registerDebugRpc); the plugin/UI channels never expose it.
class DebugRpcService {
public:
    explicit DebugRpcService(Engine& engine) : engine_(engine) {}

    // --- live-core reads ---
    // getApuState/getPpuState/drainEvents need a Mesen NES debug target (empty on SameBoy/GBA); the CPU
    // peek + register/step reads are SystemBase virtuals (null/empty when a backend can't serve).
    rp::ApuState                 getApuState(std::uint32_t id);
    rp::ExpansionAudioState       getExpansionAudioState(std::uint32_t id);
    rp::PpuState                 getPpuState(std::uint32_t id);
    std::optional<std::uint8_t>  readCpu(std::uint32_t id, std::uint32_t addr);
    bool                         writeCpu(std::uint32_t id, std::uint32_t addr, std::uint32_t value);
    rfl::Bytestring              readMemory(std::uint32_t id, std::uint32_t memType);
    std::vector<rp::CpuRegister> getCpuRegisters(std::uint32_t id);
    std::uint64_t                stepInstruction(std::uint32_t id);
    std::vector<rp::DebugEvent>  drainEvents(std::uint32_t id);

    // Load a cc65 `.dbg` symbol file so profiler/disassembly output shows function names. Needs a Mesen
    // NES debug target (false on SameBoy/GBA, a gone id, or read/parse failure).
    bool loadLabels(std::uint32_t id, std::string path);

    // --- writes / control-flow (plain SystemBase virtuals; false when the id is gone / unsupported) ---
    // setCpuRegister writes one register by name; runUntilPc single-steps until PC == target.
    bool setCpuRegister(std::uint32_t id, std::string name, std::uint32_t value);
    bool runUntilPc(std::uint32_t id, std::uint32_t target, std::uint64_t maxCycles);

    // --- breakpoints / run-until-break (needs a Mesen NES debug target; false/empty on SameBoy/GBA) ---
    // setBreakpoints installs the whole set at once (replacing any prior set); each spec is execute/read/
    // write over [start,end] with an optional Mesen condition expression. runUntilBreak steps the core
    // until a breakpoint fires or `maxCycles` elapses (broke=false + defaults on the cap / no target).
    bool          setBreakpoints(std::uint32_t id, std::vector<rp::BreakpointSpec> bps);
    rp::BreakInfo runUntilBreak(std::uint32_t id, std::uint64_t maxCycles);

    // --- execution trace + single-step (needs a Mesen NES debug target; empty/false on SameBoy/GBA) ---
    // setTrace toggles Mesen's per-instruction trace logger; readTrace returns up to `count` most-recent
    // rows (each a pc + formatted disassembly). stepInto/Over/Out advance one instruction / over a call /
    // out of the current frame, returning the resulting BreakInfo (broke=false + defaults when unserved).
    bool                       setTrace(std::uint32_t id, bool on);
    std::vector<rp::TraceLine> readTrace(std::uint32_t id, std::uint32_t count);
    rp::BreakInfo              stepInto(std::uint32_t id);
    rp::BreakInfo              stepOver(std::uint32_t id);
    rp::BreakInfo              stepOut(std::uint32_t id);

    // --- profiler + disassembler + call stack (needs a Mesen NES debug target; empty/false on else) ---
    // beginProfile inits the debugger + clears the profiler (drive execution via the render window, then
    // readProfile returns the per-function stats hottest-first). disassemble decodes `count` instructions
    // from `addr`. getCallStack returns the current frames (outermost first).
    bool                              beginProfile(std::uint32_t id);
    std::vector<rp::ProfiledFunction> readProfile(std::uint32_t id);
    std::vector<rp::DisasmLine>       disassemble(std::uint32_t id, std::uint32_t addr, std::uint32_t count);
    std::vector<rp::CallFrame>        getCallStack(std::uint32_t id);

private:
    Engine& engine_;
};
