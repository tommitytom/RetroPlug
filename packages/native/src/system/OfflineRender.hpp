#pragma once

#include <cstdint>
#include <vector>

class Project;

// Parameters for an offline render: a fixed-transport, scripted-input-free audio
// render of `totalSamples` samples. (Mid-render MIDI / serial capture and
// scripted input stay on the single-threaded harness path; this is pure audio.)
struct OfflineRenderParams {
    std::uint64_t totalSamples     = 0;
    std::uint32_t blockSize        = 1024;
    double        sampleRate       = 44100.0;
    double        bpm              = 120.0;
    bool          transportPlaying = false;
    double        startPpq         = 0.0;
};

// Render the project's independent units in parallel — one enkiTS task per unit
// (a singleton system, or a SameBoy link group). Returns one interleaved L/R
// buffer per system slot (out[slot] == L,R,L,R…, length totalSamples*2), byte-
// identical to a single-threaded runBlock + PerSystemRouter sequence over the
// same starting state. The caller sums the per-system buffers for the mix.
//
// Units render concurrently into DISJOINT per-slot buffers (no two threads touch
// the same buffer) and step only their own members' state, so there is no shared
// mutable state to guard. Each unit runs entirely on one worker thread (so a
// Mesen unit's emulation thread stays fixed). The call blocks until all units
// finish.
std::vector<std::vector<float>> renderUnitsParallel(Project& project,
                                                    const OfflineRenderParams& params);
