#include "BackendFacade.hpp"

#include <memory>

#include "GbaBackend.hpp"
#include "NesBackend.hpp"
#include "SameBoyBackend.hpp"

BackendFacade::BackendFacade() {
    // The one build path: a backend per emulator kind, keyed by the factory key toBuildSpec maps the
    // TS SystemKind onto. (The Engine pre-reserves its Project so the audio thread's adopt/swap never
    // reallocates.)
    factory_.registerBackend("sameboy", std::make_unique<SameBoyBackend>());
    factory_.registerBackend("mesen-nes", std::make_unique<NesBackend>());
    factory_.registerBackend("mesen-gba", std::make_unique<GbaBackend>());
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
    queued_.freePending();            // free un-applied command payloads
    while (queued_.popReleased()) {}  // delete cores the audio thread released just before stop
}

void BackendFacade::pluginProcessBlock(double bpm, bool playing, std::uint32_t frames, float* outL, float* outR) {
    queued_.drainInto(engine_);       // apply control-thread structural edits on the audio thread
    engine_.setBpm(bpm);              // transport from DPF TimePosition (direct — we're the audio thread)
    engine_.setTransport(playing);
    engine_.processBlock(frames, outL, outR);  // consumes MIDI staged this block + the drained edits
}
