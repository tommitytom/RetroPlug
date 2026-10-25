#include "host/rpc/AudioDriverRpcService.hpp"

#include <chrono>
#include <cstdio>
#include <memory>
#include <vector>

#include "host/engine/Engine.hpp"
#include "host/engine/EngineInvoker.hpp"

#include "system/SystemBase.hpp"  // complete type for unique_ptr<SystemBase> deletion

namespace {

// Per-block sleep on the background audio thread: runs faster than real time (so a short sleepMs
// window yields plenty of audio) while yielding the core rather than busy-spinning.
constexpr auto kAudioBlockPace = std::chrono::microseconds(200);

} // namespace

AudioDriverRpcService::AudioDriverRpcService(Engine& engine, QueuedInvoker& invoker)
    : engine_(engine), invoker_(invoker) {}

AudioDriverRpcService::~AudioDriverRpcService() {
    running_.store(false, std::memory_order_release);
    if (audioThread_.joinable()) audioThread_.join();
    if (invoker_.audioThreadOwns()) {
        invoker_.freePending();             // teardown: adopt the queued plans, DISCARD the queued edits
        invoker_.setAudioThreadOwns(false);
    }
    invoker_.reclaimReleased(); // plans (and the cores only they held) the audio thread handed back
}

void AudioDriverRpcService::audioLoop() {
    // Audio-thread-local: the control thread reaches the loop only through the one command ring and
    // running_. drainInto adopts every queued plan and applies every queued edit before the block; the
    // live count is republished each iteration.
    std::vector<float> l(kBlockSize), r(kBlockSize);
    double energy = 0.0;
    std::uint64_t frames = 0;

    while (running_.load(std::memory_order_acquire)) {
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
    // The quiescent path applied every push, so the ring is empty and the plan current: this live read is
    // accurate, and it's the last one before the audio thread takes over the count.
    liveSystemCount_.store(static_cast<std::uint32_t>(engine_.systemCount()), std::memory_order_relaxed);
    invoker_.setAudioThreadOwns(true);  // pushes become push-only BEFORE the thread spawns
    running_.store(true, std::memory_order_release);
    audioThread_ = std::thread([this] { audioLoop(); });
    return true;
}

bool AudioDriverRpcService::stopAudio() {
    if (!invoker_.audioThreadOwns()) return false;
    running_.store(false, std::memory_order_release);  // the loop exits after its current iteration
    if (audioThread_.joinable()) audioThread_.join();
    // Single accessor again: clearing the bit hands the engine back and APPLIES, on this thread, whatever
    // the last block didn't drain (no lost mutation); then reclaim the plans handed back. From here every
    // push applies inline again.
    invoker_.setAudioThreadOwns(false);
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
    // While the audio thread owns the Engine, read its per-block republished count (reading the running
    // plan's table directly would race the swap). Quiescent, the control thread owns the Engine and the
    // ring is flushed, so read it live.
    return invoker_.audioThreadOwns()
        ? liveSystemCount_.load(std::memory_order_acquire)
        : static_cast<std::uint32_t>(engine_.systemCount());
}

std::uint32_t AudioDriverRpcService::drainReleased() {
    // Control-thread consumer of the GraphHost's release ring: free each plan the audio thread handed
    // back, and with it every core only that plan held (each freeing its snapshot slot), returning how
    // many PLANS. Safe because the audio thread runs a newer plan by then.
    return invoker_.reclaimReleased();
}
