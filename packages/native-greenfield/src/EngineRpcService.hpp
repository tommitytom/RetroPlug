#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <rfl/Bytestring.hpp>

#include "BackendTypes.hpp"
#include "DspRuntime.hpp"  // DspAllocStats / DspGcResult (lean header, no quickjs types)
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
    std::optional<std::uint8_t>  readCpu(std::uint32_t id, std::uint32_t addr);
    rfl::Bytestring              readMemory(std::uint32_t id, std::uint32_t memType);
    std::vector<rp::CpuRegister> getCpuRegisters(std::uint32_t id);
    std::uint64_t                stepInstruction(std::uint32_t id);

    // --- DSP-side JS runtime (the role kernel) ---
    std::optional<rfl::Bytestring> compileScript(std::string source);
    bool dspLoadKernel(std::vector<std::uint8_t> bytecode);
    bool dspSetSystems(std::string json);

    // --- audio render / input drive / transport ---
    bool            pressButton(std::uint32_t id, std::uint32_t button, bool down);
    rfl::Bytestring renderAudio(double ms);
    bool            setTransport(bool running);
    bool            setBpm(double bpm);
    bool            setAudioRouting(std::uint32_t mode);
    bool            stageMidiIn(std::vector<std::uint8_t> bytes);

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
    Engine&        engine_;
    SystemFactory& factory_;
    QueuedInvoker& invoker_;   // the one mutation path (push; flushes inline when quiescent)

    static constexpr std::uint32_t kBlockSize = 1024;
    std::vector<float>       scratchL_;  // renderAudio pull-path scratch (control thread)
    std::vector<float>       scratchR_;
};
