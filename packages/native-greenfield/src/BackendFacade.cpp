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
    // Order is load-bearing: raise audioRunning_ (so main-thread reads fail-safe) BEFORE routing
    // mutations through the ring. From here every control-plane edit is applied on the audio thread.
    audioRunning_.store(true, std::memory_order_release);
    active_ = &queued_;
}

void BackendFacade::pluginDeactivate() {
    // DPF guarantees no run() during/after deactivate, so the ring has a single accessor again.
    audioRunning_.store(false, std::memory_order_release);
    active_ = &direct_;
    queued_.freePending();            // free un-applied command payloads (+ their snapshot slots)
    // delete cores the audio thread released just before stop, freeing each slot first
    while (std::unique_ptr<SystemBase> sys = queued_.popReleased()) engine_.registry().release(sys->id());
}

void BackendFacade::pluginProcessBlock(double bpm, bool playing, std::uint32_t frames, float* outL, float* outR) {
    queued_.drainInto(engine_);       // apply control-thread structural edits on the audio thread
    engine_.setBpm(bpm);              // transport from DPF TimePosition (direct — we're the audio thread)
    engine_.setTransport(playing);
    engine_.processBlock(frames, outL, outR);  // consumes MIDI staged this block + the drained edits
}
