#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <rfl/Bytestring.hpp>

#include "host/rpc/BackendTypes.hpp"
#include "host/dsp/DspRuntime.hpp"  // DspAllocStats / DspGcResult (lean header, no quickjs types)
#include "system/CpuState.hpp"    // rp::CpuRegister
#include "system/DebugTarget.hpp" // rp::ApuState (the live-core debug reads)

class Engine;
class SystemFactory;
class QueuedInvoker;

// The emulator surface (lifecycle / reads / DSP kernel / MIDI / transport) as a THIN RPC layer over
// (SystemFactory + Engine + the one Invoker). No threading branches: every mutation just pushes onto
// the invoker (which flushes inline when quiescent, or hands to the audio thread when it owns the
// Engine); reads come from the snapshot registry, so they need no guard.
class EngineRpcService {
public:
    EngineRpcService(Engine& engine, SystemFactory& factory, QueuedInvoker& invoker);

    // --- emulator lifecycle / reads ---
    // (duplicate + reload live in the TS SystemsStore as constructSystem-with-state orchestration.)
    bool constructSystem(BackendConstructSpec spec);   // TS-owned id in spec.id; returns "did it build"
    bool removeSystem(std::uint32_t id);
    bool applySystemSetting(std::uint32_t id, std::string key, double value);
    bool applyRoleConfig(std::uint32_t id, std::string kind, std::string config);
    std::optional<rfl::Bytestring> readState(std::uint32_t id);
    std::optional<rfl::Bytestring> readSram(std::uint32_t id);
    bool screenshot(std::uint32_t id, std::string path);
    GreenfieldFrame getFrame(std::uint32_t id);

    // --- live-core debug reads (spec/09-cli-debugging.md) ---
    // Read the LIVE core (via engine_.findSystem), unlike the snapshot-registry reads above — so, like
    // the dspAllocStats reads, they are only valid on the control thread while the audio thread is not
    // started (the CLI's single-threaded direct-render regime). getApuState needs a Mesen NES system
    // (empty on SameBoy/GBA); the rest are SystemBase virtuals (empty/null when a backend can't serve).
    rp::ApuState                 getApuState(std::uint32_t id);
    rp::PpuState                 getPpuState(std::uint32_t id);
    std::optional<std::uint8_t>  readCpu(std::uint32_t id, std::uint32_t addr);
    bool                         writeCpu(std::uint32_t id, std::uint32_t addr, std::uint32_t value);
    rfl::Bytestring              readMemory(std::uint32_t id, std::uint32_t memType);
    std::vector<rp::CpuRegister> getCpuRegisters(std::uint32_t id);
    std::uint64_t                stepInstruction(std::uint32_t id);
    std::vector<rp::DebugEvent>  drainEvents(std::uint32_t id);

    // --- execution trace + single-step (needs a Mesen NES debug target; empty/false on SameBoy/GBA) ---
    // setTrace toggles Mesen's per-instruction trace logger; readTrace returns up to `count` most-recent
    // rows (each a pc + formatted disassembly). stepInto/Over/Out advance one instruction / over a call /
    // out of the current frame, returning the resulting BreakInfo (broke=false + defaults when unserved).
    bool                        setTrace(std::uint32_t id, bool on);
    std::vector<rp::TraceLine>  readTrace(std::uint32_t id, std::uint32_t count);
    rp::BreakInfo               stepInto(std::uint32_t id);
    rp::BreakInfo               stepOver(std::uint32_t id);
    rp::BreakInfo               stepOut(std::uint32_t id);

    // Load a cc65 `.dbg` symbol file so profiler/disassembly output shows function names. Needs a Mesen
    // NES debug target (false on SameBoy/GBA, a gone id, or read/parse failure). Reached via debugTarget().
    bool loadLabels(std::uint32_t id, std::string path);

    // --- profiler + disassembler + call stack (needs a Mesen NES debug target; empty/false on else) ---
    // beginProfile inits the debugger + clears the profiler (drive execution via the render window, then
    // readProfile returns the per-function stats hottest-first). disassemble decodes `count` instructions
    // from `addr`. getCallStack returns the current frames (outermost first).
    bool                            beginProfile(std::uint32_t id);
    std::vector<rp::ProfiledFunction> readProfile(std::uint32_t id);
    std::vector<rp::DisasmLine>     disassemble(std::uint32_t id, std::uint32_t addr, std::uint32_t count);
    std::vector<rp::CallFrame>      getCallStack(std::uint32_t id);

    // --- live-core debug writes / control-flow (spec/09-cli-debugging.md) ---
    // Plain SystemBase virtuals (no debugTarget needed); false when the id is gone or the backend can't
    // serve. setCpuRegister writes one register by name; runUntilPc single-steps until PC == target.
    bool setCpuRegister(std::uint32_t id, std::string name, std::uint32_t value);
    bool runUntilPc(std::uint32_t id, std::uint32_t target, std::uint64_t maxCycles);

    // --- breakpoints / run-until-break (needs a Mesen NES debug target; false/empty on SameBoy/GBA) ---
    // setBreakpoints installs the whole set at once (replacing any prior set); each spec is execute/read/
    // write over [start,end] with an optional Mesen condition expression. runUntilBreak steps the core
    // until a breakpoint fires or `maxCycles` elapses (broke=false + defaults on the cap / no target).
    bool          setBreakpoints(std::uint32_t id, std::vector<rp::BreakpointSpec> bps);
    rp::BreakInfo runUntilBreak(std::uint32_t id, std::uint64_t maxCycles);

    // --- DSP-side JS runtime (the role kernel) ---
    std::optional<rfl::Bytestring> compileScript(std::string source);
    bool dspLoadKernel(std::vector<std::uint8_t> bytecode);
    bool dspSetSystems(std::string json);

    // --- audio render / input drive / transport ---
    bool            pressButton(std::uint32_t id, std::uint32_t button, bool down);
    rfl::Bytestring renderAudio(double ms);
    // Per-system audio: each live system's interleaved-stereo PCM, in Project-slot order (marshals to
    // Uint8Array[]). Isolates each core so LSDj link-cable sync is provable (a follower's own RMS).
    std::vector<rfl::Bytestring> renderAudioPerSystem(double ms);
    bool            setTransport(bool running);
    bool            setBpm(double bpm);
    bool            setAudioRouting(std::uint32_t mode);
    bool            stageMidiIn(std::vector<std::uint8_t> bytes);
    // Arm/disarm a SameBoy's serial-out capture (LSDj MI.OUT). Control-plane, via the invoker's config
    // path (ConfigField::SerialOutCapture) — the TS store calls it when a system's lsdj-sync mode is MIDIOUT.
    bool            setSerialOutCapture(std::uint32_t id, bool on);
    // Drain the MIDI-out the kernel emitted (LSDj MI.OUT decoder), accumulated across renderAudio blocks
    // since the last drain. For headless tests; the plugin drains Engine::midiOut() to the DAW directly.
    std::vector<GreenfieldMidiOut> drainMidiOut();

    // --- DSP-runtime allocation/GC profiling (spec/08-profiling.md) ---
    // Reads/GC on the DSP JS runtime, reached directly through the Engine (the quiescent renderAudio
    // pull path owns it — the benchmark never starts the audio thread). enabled=false off-profile.
    DspAllocStats dspAllocStats();
    bool          dspResetAllocStats(bool disableAutoGc);
    DspGcResult   dspRunGc();

    // --- per-role runtime tracing (spec/08-profiling.md Tier B; enabled off-profile returns empty) ---
    bool                      dspTraceReset(bool arm);
    std::vector<DspTraceSpan> dspTrace();
    std::vector<std::string>  dspTraceNames();

private:
    // Append the kernel's per-block MIDI-out (Engine::midiOut(), cleared each block) to accumMidiOut_.
    // Called after each processBlock in the render loops so drainMidiOut() sees the whole window.
    void accumulateMidiOut();

    Engine&        engine_;
    SystemFactory& factory_;
    QueuedInvoker& invoker_;   // the one mutation path (push; flushes inline when quiescent)

    static constexpr std::uint32_t kBlockSize = 1024;
    std::vector<float>       scratchL_;  // renderAudio pull-path scratch (control thread)
    std::vector<float>       scratchR_;
    std::vector<GreenfieldMidiOut> accumMidiOut_;  // kernel MIDI-out gathered across a render window (drainMidiOut)
};
