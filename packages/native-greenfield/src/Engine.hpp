#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "project/Project.hpp"

#include "DspRuntime.hpp"

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

    // --- per block: run kernel → fan sinks to cores → onProcess → advance ppq ---
    void processBlock(std::uint32_t frames, double bpm, bool transport, double& ppq,
                      float* outL, float* outR);

    // --- live-state reads / direct mutation (valid only on the Engine's owning thread) ---
    std::optional<std::vector<std::uint8_t>> readState(SystemId id);
    std::optional<std::vector<std::uint8_t>> readSram(SystemId id);
    bool screenshot(SystemId id, const std::string& path);
    bool sendMidi(SystemId id, const std::vector<std::uint8_t>& bytes);
    bool pressButton(SystemId id, std::uint8_t button, bool down);

private:
    Project    project_;
    DspRuntime dsp_;
    double     sampleRate_;
    bool       dspActive_ = false;                 // a kernel is loaded → run the per-block DSP stage
    std::vector<DspRuntime::MidiIn> pendingMidi_;  // staged host MIDI, consumed on the next block
};
