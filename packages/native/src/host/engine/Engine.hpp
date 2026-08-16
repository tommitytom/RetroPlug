#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "project/Project.hpp"
#include "system/AudioRouting.hpp"

#include "host/dsp/DspRuntime.hpp"
#include "host/engine/HostSyncTrace.hpp"
#include "host/engine/SnapshotRegistry.hpp"

struct AudioRouter;  // BlockRunner.hpp — the per-block bus-placement policy (used by ref below)

// A per-system config field a control-plane edit applies to the live core (SameBoy today). Carried
// as a double across the command ring: gain in dB; a bool as 0/1; an enum/int as its integer value.
enum class ConfigField : std::uint8_t {
    Gain = 0, ReloadOnRomChange = 1,
    Model = 2, Highpass = 3, LinkGroup = 4, FastBoot = 5,     // SameBoy
    NesRegion = 6, NesRemoveSpriteLimit = 7,                  // Mesen (NES)
    SerialOutCapture = 8,                                     // SameBoy (LSDj MI.OUT)
    NesApuLatencyMs = 9,                                      // Mesen (NES) — APU flush window as latency (ms)
    // SameBoy display group — all live (they land on the next rendered frame), all routed through
    // SameBoySystem::applyDisplayConfig.
    ColorCorrection = 10, DmgPalette = 11, LightTemperature = 12,
    // Mesen (NES) cartridge-accuracy switches — 0 = chip, 1 = Everdrive N8. Both live.
    NesS5bNoise = 13, NesMmc5PhaseReset = 14,
};

// One system's video frame, read from its lock-free FrameBufferTriple. `data` is raw XRGB8888
// (little-endian B,G,R,X), width*height*4 bytes — the LVGL Canvas's native format, so no transcode.
// `published` is false (and `data` empty) until the core has rendered its first frame.
struct EngineFrame {
    std::uint32_t             width = 0;
    std::uint32_t             height = 0;
    bool                      published = false;
    std::vector<std::uint8_t> data;
};

// The single-threaded core of the host: owns the Project of live systems and the DSP
// role kernel, and knows nothing about audio threads, queues, or RPC. Every op runs "now, on the
// calling thread." Removed/displaced cores are RETURNED to the caller (who deletes them, or routes
// them back across a thread) — the Engine never frees on a hot path. No atomics: transport + ppq
// are passed per block by whoever drives it (the synchronous pull path or the audio loop).
class Engine {
public:
    explicit Engine(double sampleRate = 44100.0);

    double sampleRate() const { return sampleRate_; }

    // --- structure (alloc-free swaps vs the pre-reserved Project; `sys` was built off-thread) ---
    // (ids are TS-owned now — the store allocates and passes them in; native never mints one.)
    void adoptSystem(std::unique_ptr<SystemBase> sys);                                        // + rebuild
    std::unique_ptr<SystemBase> removeSystem(SystemId id);                                    // returns removed
    std::unique_ptr<SystemBase> replaceSystem(SystemId id, std::unique_ptr<SystemBase> sys);  // returns displaced
    std::size_t systemCount() const;
    SystemBase* findSystem(SystemId id);   // borrowed (non-owning)

    // --- DSP kernel ---
    bool loadKernel(const std::vector<std::uint8_t>& bytecode);   // sets the per-block DSP stage active
    bool setSystems(const std::vector<std::uint8_t>& json);
    void stageMidi(std::uint32_t frame, std::vector<std::uint8_t> bytes);  // delivered on the next processBlock, at intra-block `frame`
    void stageMidi(std::vector<std::uint8_t> bytes) { stageMidi(0, std::move(bytes)); }  // frame-0 convenience (RPC/harness path)

    // --- transport (plain members; mutated only by the Engine's owning thread) ---
    void setBpm(double bpm);
    void setTransport(bool playing);
    // Move the host playhead. A DAW does this on every locate: stop-and-rewind, a loop wrap,
    // or a click in the timeline. Roles see it as a ppqStart discontinuity in the next block.
    void setPpq(double ppq);
    // Which output pairs each system routes to (Stereo = all → pair 0). Plain member, like transport.
    void setAudioRouting(AudioRouting mode);

    // Host sample rate. Used at construct for new cores (findable via sampleRate()) AND pushed to every
    // already-live core so a host sample-rate change re-rates them in place (SameBoy GB_set_sample_rate,
    // Mesen resampler). Safe to call with cores live only when the audio thread isn't draining — the plugin
    // calls it from DPF's sampleRateChanged, which fires only while deactivated; the CLI, before any build.
    void setSampleRate(double sr) { sampleRate_ = sr; project_.onSampleRateChanged(sr); }

    // The kernel's host-MIDI-out sink for this block (filled by processBlock's DSP stage, cleared at
    // the top of the next). The plugin drains this to the DAW after processBlock; nothing else reads it.
    const std::vector<DspRuntime::MidiOut>& midiOut() const { return dsp_.midiOut_; }
    void clearMidiOut() { dsp_.midiOut_.clear(); }

    // An optional observer of the raw core-bytes sink (a tracker's host-sync protocol), invoked once
    // per message on the audio thread at the point those bytes fan to the addressed core. The standalone
    // sets it to MIRROR the generated risa sync stream to a physical Everdrive N8 (via N8Link), so the
    // real cart stays in lock-step with the emulated core. Null everywhere else (plugin / CLI / tests),
    // where it is a no-op. `frame` is the intra-block sample offset; `flush` is the arm-barrier flag.
    void setCoreByteSink(std::function<void(std::uint32_t frame, const std::uint8_t* data,
                                            std::size_t size, bool flush)> sink) {
        coreByteSink_ = std::move(sink);
    }

    // --- DSP-runtime allocation/GC profiling (spec/08-profiling.md) ---
    // Forward to the bare JS runtime's counters. Valid only on the Engine's owning thread (the
    // renderAudio pull path); no-op / enabled=false in a non-RETROPLUG_PROFILE build.
    DspAllocStats dspAllocStats() const { return dsp_.allocStats(); }
    void          resetDspAllocStats(bool disableAutoGc) { dsp_.resetAllocStats(disableAutoGc); }
    DspGcResult   dspRunGc() { return dsp_.runGc(); }

    // --- per-role runtime tracing (spec/08-profiling.md Tier B) ---
    void                      dspTraceReset(bool arm) { dsp_.traceReset(arm); }
    std::vector<DspTraceSpan> dspTrace() const { return dsp_.traceSpans(); }
    std::vector<std::string>  dspTraceNames() const { return dsp_.traceNames(); }

    // --- per block: run kernel → fan sinks to cores → route to the outputs → advance the ppq clock ---
    // Multi-out core: `outputs` is a flat array of `numOutputs` planar channels; each system routes
    // into its pair per audioRouting_ (via MultiOutRouter). The plugin passes its 8 channels; the
    // stereo overload below passes 2 (any routing mode collapses to one pair with 2 channels).
    void processBlock(std::uint32_t frames, float* const* outputs, std::size_t numOutputs);
    void processBlock(std::uint32_t frames, float* outL, float* outR);
    // Per-system isolation: each system renders into its OWN L/R pair (ls[i]/rs[i] for slot i in
    // Project::systems() order) via PerSystemRouter, ignoring audioRouting_. Proves LSDj link-cable
    // sync — a follower sounds only when it actually synced (a healthy 2-system mix can't show that).
    void processBlockPerSystem(std::uint32_t frames, float* const* ls, float* const* rs, std::size_t nSystems);
    // Per-channel isolation for ONE system: its channelLayout() streams each render into their own L/R
    // pair (ls[k]/rs[k] for stream k) via PerChannelRouter. Since that router keys off streamIndex (not
    // slot), this is only correct for a single-system project — the RPC gates on systemCount() == 1. The
    // full kernel/serial/snapshot pipeline still runs, so MIDI-driven audio (e.g. mGB) sounds.
    void processBlockPerChannel(std::uint32_t frames, float* const* ls, float* const* rs, std::size_t nStreams);

    // --- live-state reads / direct mutation (valid only on the Engine's owning thread) ---
    std::optional<std::vector<std::uint8_t>> readState(SystemId id);
    std::optional<std::vector<std::uint8_t>> readSram(SystemId id);
    // Work RAM (WRAM), from the registry's per-block published copy — safe while the audio thread runs,
    // and fresh EVERY block (unlike readState/readSram), so a runtime overlay tracks per-frame state.
    std::optional<std::vector<std::uint8_t>> readRam(SystemId id);
    /** The published WRAM length, for a bounds check that costs no copy. 0 = no writable RAM. */
    std::size_t ramSize(SystemId id);
    bool screenshot(SystemId id, const std::string& path);
    // The system's latest video frame (raw XRGB8888). Reads the concurrent FrameBufferTriple, so it is
    // safe while the audio thread writes; width/height are 0 (published false) for an unknown system.
    EngineFrame getFrame(SystemId id);
    bool pressButton(SystemId id, std::uint8_t button, bool down);

    // Audio-thread: poke bytes into a system's work RAM. `offset` indexes the SAME region readRam
    // returns, so read-modify-write round-trips without a coordinate conversion. Applied between
    // blocks via the invoker, never from the calling thread - which is what separates it from the
    // debug facet's writeCpu (a live-core write, valid only when the audio thread is stopped).
    //
    // Deliberately unguarded beyond bounds: writing a running core's RAM CAN confuse it, because the
    // emulated program has its own invariants over those bytes. That is the point of the feature.
    bool writeRam(SystemId id, std::uint32_t offset, const std::vector<std::uint8_t>& bytes);
    // Apply a live config edit to a system (SameBoy-only cast today). Value-guarded per field so a
    // whole-config re-send only acts on what changed (no spurious model restart).
    void applyConfigField(SystemId id, std::uint8_t field, double value);

    // The owned snapshot store the control plane reads through. Claim a slot at build, release it
    // when the core is deleted; processBlock publishes into it.
    SnapshotRegistry& registry() { return registry_; }

private:
    // Shared per-block core: run the kernel (if active) + fan its sinks to the cores, render every
    // system through `router`, publish snapshots, advance the ppq clock. Both processBlock overloads
    // and processBlockPerSystem funnel through here; the caller zeroes the router's destination buffers
    // first (they differ per router — the flat multi-out array vs the per-system pairs).
    void runBlockWithRouter(std::uint32_t frames, const AudioRouter& router);

    Project          project_;
    SnapshotRegistry registry_;
    DspRuntime dsp_;
    double     sampleRate_;
    bool       dspActive_ = false;                 // a kernel is loaded → run the per-block DSP stage
    std::vector<DspRuntime::MidiIn> pendingMidi_;  // staged host MIDI, consumed on the next block
    // Raw serial-out bytes each core emitted LAST block (LSDj MI.OUT), gathered after runBlock and fed
    // to the kernel on the next block (one-block latency — the kernel runs before runBlock, and each
    // SameBoy clears its serialOutLog_ in prepareForBlock).
    std::vector<DspRuntime::SerialOut> pendingSerialOut_;

    // Simulated host transport, driven per block. Continuous ppq clock (the pull path and the audio
    // loop share it; they never run concurrently). Plain — mutated only by the owning thread.
    double bpm_       = 120.0;
    bool   transport_ = false;
    double ppq_       = 0.0;
    HostSyncTrace syncTrace_;  // inert unless RETROPLUG_SYNC_TRACE is set
    // Optional external mirror of the core-bytes stream (set by the standalone to reach a physical N8);
    // null in every other host. Called on the audio thread, so the target must be RT-safe (N8Link is).
    std::function<void(std::uint32_t, const std::uint8_t*, std::size_t, bool)> coreByteSink_;

    AudioRouting audioRouting_ = AudioRouting::Stereo;  // output-pair placement; Stereo = all → pair 0
};
