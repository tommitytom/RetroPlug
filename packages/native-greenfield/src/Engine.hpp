#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "project/Project.hpp"

#include "DspRuntime.hpp"
#include "SnapshotRegistry.hpp"

// A per-system config field a control-plane edit applies to the live core (SameBoy today). Carried
// as a double across the command ring: gain in dB; a bool as 0/1; an enum/int as its integer value.
enum class ConfigField : std::uint8_t {
    Gain = 0, ReloadOnRomChange = 1, Model = 2, Highpass = 3, LinkGroup = 4, FastBoot = 5,
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

// The single-threaded core of the greenfield host: owns the Project of live systems and the DSP
// role kernel, and knows nothing about audio threads, queues, or RPC. Every op runs "now, on the
// calling thread." Removed/displaced cores are RETURNED to the caller (who deletes them, or routes
// them back across a thread) — the Engine never frees on a hot path. No atomics: transport + ppq
// are passed per block by whoever drives it (the synchronous pull path or the audio loop).
class Engine {
public:
    explicit Engine(double sampleRate = 44100.0);

    double sampleRate() const { return sampleRate_; }

    // --- structure (alloc-free swaps vs the pre-reserved Project; `sys` was built off-thread) ---
    SystemId nextSystemId();
    void adoptSystem(std::unique_ptr<SystemBase> sys);                                        // + rebuild
    std::unique_ptr<SystemBase> removeSystem(SystemId id);                                    // returns removed
    std::unique_ptr<SystemBase> replaceSystem(SystemId id, std::unique_ptr<SystemBase> sys);  // returns displaced
    std::size_t systemCount() const;
    SystemBase* findSystem(SystemId id);   // borrowed (non-owning)

    // --- DSP kernel ---
    bool loadKernel(const std::vector<std::uint8_t>& bytecode);   // sets the per-block DSP stage active
    bool setSystems(const std::vector<std::uint8_t>& json);
    void stageMidi(std::vector<std::uint8_t> bytes);              // delivered on the next processBlock

    // --- transport (plain members; mutated only by the Engine's owning thread) ---
    void setBpm(double bpm);
    void setTransport(bool playing);
    // Which output pairs each system routes to (Stereo = all → pair 0). Plain member, like transport.
    void setAudioRouting(AudioRouting mode);

    // Host sample rate. Baked into each core at construct (findable via sampleRate()); set it BEFORE
    // constructing systems — there is no per-system resample-on-change today.
    void setSampleRate(double sr) { sampleRate_ = sr; }

    // The kernel's host-MIDI-out sink for this block (filled by processBlock's DSP stage, cleared at
    // the top of the next). The plugin drains this to the DAW after processBlock; nothing else reads it.
    const std::vector<DspRuntime::MidiOut>& midiOut() const { return dsp_.midiOut_; }
    void clearMidiOut() { dsp_.midiOut_.clear(); }

    // --- per block: run kernel → fan sinks to cores → route to the outputs → advance the ppq clock ---
    // Multi-out core: `outputs` is a flat array of `numOutputs` planar channels; each system routes
    // into its pair per audioRouting_ (via MultiOutRouter). The plugin passes its 8 channels; the
    // stereo overload below passes 2 (any routing mode collapses to one pair with 2 channels).
    void processBlock(std::uint32_t frames, float* const* outputs, std::size_t numOutputs);
    void processBlock(std::uint32_t frames, float* outL, float* outR);

    // --- live-state reads / direct mutation (valid only on the Engine's owning thread) ---
    std::optional<std::vector<std::uint8_t>> readState(SystemId id);
    std::optional<std::vector<std::uint8_t>> readSram(SystemId id);
    bool screenshot(SystemId id, const std::string& path);
    // The system's latest video frame (raw XRGB8888). Reads the concurrent FrameBufferTriple, so it is
    // safe while the audio thread writes; width/height are 0 (published false) for an unknown system.
    EngineFrame getFrame(SystemId id);
    bool pressButton(SystemId id, std::uint8_t button, bool down);
    // Apply a live config edit to a system (SameBoy-only cast today). Value-guarded per field so a
    // whole-config re-send only acts on what changed (no spurious model restart).
    void applyConfigField(SystemId id, std::uint8_t field, double value);

    // The owned snapshot store the control plane reads through. Claim a slot at build, release it
    // when the core is deleted; processBlock publishes into it.
    SnapshotRegistry& registry() { return registry_; }

private:
    Project          project_;
    SnapshotRegistry registry_;
    DspRuntime dsp_;
    double     sampleRate_;
    bool       dspActive_ = false;                 // a kernel is loaded → run the per-block DSP stage
    std::vector<DspRuntime::MidiIn> pendingMidi_;  // staged host MIDI, consumed on the next block

    // Simulated host transport, driven per block. Continuous ppq clock (the pull path and the audio
    // loop share it; they never run concurrently). Plain — mutated only by the owning thread.
    double bpm_       = 120.0;
    bool   transport_ = false;
    double ppq_       = 0.0;

    AudioRouting audioRouting_ = AudioRouting::Stereo;  // output-pair placement; Stereo = all → pair 0
};
