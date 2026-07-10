#include "BackendFacade.hpp"

#include <memory>

#include "MesenBackend.hpp"
#include "SameBoyBackend.hpp"

BackendFacade::BackendFacade() {
    // The one build path: a backend per core, keyed by the `core` value. Mesen serves both NES and
    // GBA (it dispatches internally on the platform). (The Engine pre-reserves its Project so the
    // audio thread's adopt/swap never reallocates.)
    factory_.registerBackend("sameboy", std::make_unique<SameBoyBackend>());
    factory_.registerBackend("mesen", std::make_unique<MesenBackend>());
}

// --- DPF plugin driving (mirrors AudioDriverRpcService::startAudio/stopAudio/audioLoop, minus the
// spawned thread — DPF owns the audio thread and calls run()). ---

void BackendFacade::pluginActivate() {
    // Hand the Engine to the DPF audio thread: from here every control-plane edit is push-only, drained
    // by pluginProcessBlock each block. (Set BEFORE the host starts calling run(); the quiescent path
    // flushed every push, so the command ring is empty at this handoff.)
    invoker_.setAudioThreadOwns(true);
}

void BackendFacade::pluginDeactivate() {
    // DPF guarantees no run() during/after deactivate, so the ring has a single accessor again. Take the
    // Engine back, apply any commands the last block didn't drain (no lost mutation), and reclaim cores
    // the audio thread released just before stopping (freeing each snapshot slot).
    invoker_.setAudioThreadOwns(false);
    invoker_.drainInto(engine_);
    invoker_.reclaimReleased();
}

void BackendFacade::pluginProcessBlock(double bpm, bool playing, std::uint32_t frames,
                                       float* const* outputs, std::uint32_t numOutputs) {
    invoker_.drainInto(engine_);      // apply control-thread structural edits on the audio thread
    engine_.setBpm(bpm);              // transport from DPF TimePosition (direct — we're the audio thread)
    engine_.setTransport(playing);
    engine_.processBlock(frames, outputs, numOutputs);  // consumes MIDI staged this block + the drained edits
}
