#include "AudioDriverRpcService.hpp"

#include <chrono>
#include <memory>
#include <vector>

#include "Engine.hpp"
#include "EngineInvoker.hpp"

#include "system/SystemBase.hpp"  // complete type for unique_ptr<SystemBase> deletion

namespace {

// Per-block sleep on the background audio thread: runs faster than real time (so a short sleepMs
// window yields plenty of audio) while yielding the core rather than busy-spinning.
constexpr auto kAudioBlockPace = std::chrono::microseconds(200);

} // namespace

AudioDriverRpcService::AudioDriverRpcService(Engine& engine, QueuedInvoker& invoker)
    : engine_(engine), invoker_(invoker) {}

AudioDriverRpcService::~AudioDriverRpcService() {
    if (invoker_.audioThreadOwns()) {
        invoker_.setAudioThreadOwns(false);
        if (audioThread_.joinable()) audioThread_.join();
    }
    invoker_.freePending();     // teardown: DISCARD un-applied command payloads (built-but-unadopted systems etc.)
    invoker_.reclaimReleased(); // cores the audio thread released but nobody drained
}

void AudioDriverRpcService::audioLoop() {
    // Audio-thread-local: the control thread reaches the loop only through the invoker's command ring
    // and its audioThreadOwns bit. drainInto applies every queued edit (structure + transport) INTO the
    // Engine before the block; the live count is republished each iteration.
    std::vector<float> l(kBlockSize), r(kBlockSize);
    double energy = 0.0;
    std::uint64_t frames = 0;

    while (invoker_.audioThreadOwns()) {
        invoker_.drainInto(engine_);
        liveSystemCount_.store(static_cast<std::uint32_t>(engine_.systemCount()), std::memory_order_release);
        engine_.processBlock(kBlockSize, l.data(), r.data());

        for (std::uint32_t f = 0; f < kBlockSize; ++f)
            energy += static_cast<double>(l[f]) * l[f] + static_cast<double>(r[f]) * r[f];
        frames += kBlockSize;
        capturedEnergy_.store(energy, std::memory_order_release);
        capturedFrames_.store(frames, std::memory_order_release);

        std::this_thread::sleep_for(kAudioBlockPace);
    }
}

bool AudioDriverRpcService::startAudio() {
    if (invoker_.audioThreadOwns()) return false;  // already running
    capturedEnergy_.store(0.0, std::memory_order_relaxed);
    capturedFrames_.store(0, std::memory_order_relaxed);
    // The quiescent path flushed every push, so the ring is empty + Project current: this live read is
    // accurate, and it's the last one before the audio thread takes over the count.
    liveSystemCount_.store(static_cast<std::uint32_t>(engine_.systemCount()), std::memory_order_relaxed);
    invoker_.setAudioThreadOwns(true);  // pushes become push-only BEFORE the thread spawns
    audioThread_ = std::thread([this] { audioLoop(); });
    return true;
}

bool AudioDriverRpcService::stopAudio() {
    if (!invoker_.audioThreadOwns()) return false;
    invoker_.setAudioThreadOwns(false);  // the loop exits after its current iteration
    if (audioThread_.joinable()) audioThread_.join();
    // Single accessor again: APPLY any commands the last block didn't drain (no lost mutation), then
    // reclaim released cores. From here every push flushes inline again.
    invoker_.drainInto(engine_);
    invoker_.reclaimReleased();
    return true;
}

AudioCaptured AudioDriverRpcService::audioCaptured() {
    const std::uint64_t f = capturedFrames_.load(std::memory_order_acquire);
    const double        e = capturedEnergy_.load(std::memory_order_acquire);
    return { e, f };
}

bool AudioDriverRpcService::sleepMs(double ms) {
    if (ms > 0.0) std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(ms));
    return true;
}

std::uint32_t AudioDriverRpcService::systemCount() {
    // While the audio thread owns the Engine, read its per-block republished count (reading Project
    // directly would race it). Quiescent, the control thread owns the Engine + the ring is flushed, so
    // read it live.
    return invoker_.audioThreadOwns()
        ? liveSystemCount_.load(std::memory_order_acquire)
        : static_cast<std::uint32_t>(engine_.systemCount());
}

std::uint32_t AudioDriverRpcService::drainReleased() {
    // Control-thread consumer of the release ring: delete each core the audio thread handed back,
    // freeing its snapshot slot. Safe because the audio thread already dropped the core from the
    // Project (no longer rendered) before pushing it.
    return invoker_.reclaimReleased();
}
