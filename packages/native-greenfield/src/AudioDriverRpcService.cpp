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

AudioDriverRpcService::AudioDriverRpcService(Engine& engine, QueuedInvoker& queued, DirectInvoker& direct,
                                             EngineInvoker*& active, std::atomic<bool>& audioRunning)
    : engine_(engine), queued_(queued), direct_(direct), active_(active), audioRunning_(audioRunning) {}

AudioDriverRpcService::~AudioDriverRpcService() {
    if (audioRunning_.load(std::memory_order_acquire)) {
        audioRunning_.store(false, std::memory_order_release);
        if (audioThread_.joinable()) audioThread_.join();
    }
    queued_.freePending();  // un-applied command payloads (built-but-unadopted systems etc.)
    drainReleased();        // cores the audio thread released but nobody drained
}

void AudioDriverRpcService::audioLoop() {
    // Audio-thread-local: the control thread reaches the loop only through the QueuedInvoker's
    // command ring and audioRunning_. drainInto applies every queued edit (structure + transport)
    // INTO the Engine before the block; the live count is republished each iteration.
    std::vector<float> l(kBlockSize), r(kBlockSize);
    double energy = 0.0;
    std::uint64_t frames = 0;

    while (audioRunning_.load(std::memory_order_acquire)) {
        queued_.drainInto(engine_);
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
    if (audioRunning_.load(std::memory_order_acquire)) return false;  // already running
    capturedEnergy_.store(0.0, std::memory_order_relaxed);
    capturedFrames_.store(0, std::memory_order_relaxed);
    liveSystemCount_.store(static_cast<std::uint32_t>(engine_.systemCount()), std::memory_order_relaxed);
    active_ = &queued_;  // subsequent mutations enqueue for the audio thread (control-thread state)
    audioRunning_.store(true, std::memory_order_release);
    audioThread_ = std::thread([this] { audioLoop(); });
    return true;
}

bool AudioDriverRpcService::stopAudio() {
    if (!audioRunning_.load(std::memory_order_acquire)) return false;
    audioRunning_.store(false, std::memory_order_release);
    if (audioThread_.joinable()) audioThread_.join();
    active_ = &direct_;  // back to synchronous application
    // The audio thread is joined, so both rings are single-threaded again: free any un-applied
    // command payloads, and delete any cores the audio thread released just before stopping.
    queued_.freePending();
    drainReleased();
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
    return liveSystemCount_.load(std::memory_order_acquire);
}

std::uint32_t AudioDriverRpcService::drainReleased() {
    // Control-thread consumer of the release ring: delete each core the audio thread handed back.
    // popReleased is SPSC-safe against the audio-thread producer; deleting here is safe because the
    // audio thread already dropped the core from the Project (no longer rendered) before pushing it.
    std::uint32_t freed = 0;
    while (std::unique_ptr<SystemBase> sys = queued_.popReleased()) ++freed;  // deleted at scope end
    return freed;
}
