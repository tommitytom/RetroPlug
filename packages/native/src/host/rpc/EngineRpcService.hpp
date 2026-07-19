#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <rfl/Bytestring.hpp>

#include "host/rpc/BackendTypes.hpp"
#include "host/dsp/DspRuntime.hpp"  // DspAllocStats / DspGcResult (lean header, no quickjs types)

class Engine;
class SystemFactory;
class QueuedInvoker;
namespace rp::lsdj { class KitCompiler; }  // fwd: lazy, kept out of the header (enkiTS/r8brain heavy)

// The emulator surface (lifecycle / reads / DSP kernel / MIDI / transport) as a THIN RPC layer over
// (SystemFactory + Engine + the one Invoker). No threading branches: every mutation just pushes onto
// the invoker (which flushes inline when quiescent, or hands to the audio thread when it owns the
// Engine); reads come from the snapshot registry, so they need no guard.
class EngineRpcService {
public:
    EngineRpcService(Engine& engine, SystemFactory& factory, QueuedInvoker& invoker);
    ~EngineRpcService();   // out-of-line: kitCompiler_ holds a fwd-declared KitCompiler

    // --- emulator lifecycle / reads ---
    // (duplicate + reload live in the TS SystemsStore as constructSystem-with-state orchestration.)
    bool constructSystem(BackendConstructSpec spec);   // TS-owned id in spec.id; returns "did it build"
    bool removeSystem(std::uint32_t id);
    bool applySystemSetting(std::uint32_t id, std::string key, double value);
    bool applyRoleConfig(std::uint32_t id, std::string kind, std::string config);
    std::optional<rfl::Bytestring> readState(std::uint32_t id);
    std::optional<rfl::Bytestring> readSram(std::uint32_t id);
    std::optional<rfl::Bytestring> readRam(std::uint32_t id);   // work RAM (WRAM), per-block published copy
    bool screenshot(std::uint32_t id, std::string path);
    RpcFrame getFrame(std::uint32_t id);

    // (Live-core debug/inspection — getApuState/readCpu/breakpoints/trace/profiler/… — moved to
    // DebugRpcService; only the CLI binds that facet.)

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
    // Per-channel audio for ONE system: each of its channelLayout() streams as its own interleaved-stereo
    // PCM (Game Boy = 4: Pulse 1/Pulse 2/Wave/Noise). Single-system only (empty if systemCount() != 1 or
    // `id` is unknown) — the per-channel router keys off streamIndex, not slot. Marshals to Uint8Array[].
    std::vector<rfl::Bytestring> renderAudioPerChannel(std::uint32_t id, double ms);
    // Compile an LSDJ sample kit from source audio files → a 16 KB kit bank (harness/tooling only; the
    // plugin never binds this). Resample (r8brain) + effects + 4-bit nibble-pack per sample, fanned across
    // an enkiTS pool by the owned KitCompiler; a per-sample load failure just leaves that slot empty.
    rfl::Bytestring compileKit(KitCompileSpec spec);
    // The engine's audio sample rate (Hz), so callers can label WAV output correctly.
    double          sampleRate() const;
    // Set the host sample rate (Hz). Baked into each core at construct, so it only takes effect BEFORE any
    // system is built — rejected (false) once a system exists, since there's no resample-on-change today.
    bool            setSampleRate(double sr);
    bool            setTransport(bool running);
    bool            setBpm(double bpm);
    bool            setAudioRouting(std::uint32_t mode);
    bool            stageMidiIn(std::vector<std::uint8_t> bytes);
    // Arm/disarm a SameBoy's serial-out capture (LSDj MI.OUT). Control-plane, via the invoker's config
    // path (ConfigField::SerialOutCapture) — the TS store calls it when a system's lsdj-sync mode is MIDIOUT.
    bool            setSerialOutCapture(std::uint32_t id, bool on);
    // Drain the MIDI-out the kernel emitted (LSDj MI.OUT decoder), accumulated across renderAudio blocks
    // since the last drain. For headless tests; the plugin drains Engine::midiOut() to the DAW directly.
    std::vector<RpcMidiOut> drainMidiOut();

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
    std::vector<RpcMidiOut> accumMidiOut_;  // kernel MIDI-out gathered across a render window (drainMidiOut)
    std::unique_ptr<rp::lsdj::KitCompiler> kitCompiler_;  // lazy: built on first compileKit (enkiTS pool)
};
