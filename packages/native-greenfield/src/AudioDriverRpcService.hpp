#pragma once

#include <atomic>
#include <cstdint>
#include <thread>

#include "BackendTypes.hpp"

class Engine;
class DirectInvoker;
class QueuedInvoker;
class EngineInvoker;

// Test/dev harness control: spins the background audio thread that OWNS the Engine while it runs,
// draining the QueuedInvoker into it each block and publishing observation (energy/frames/count) for
// the control thread. It also flips the shared invoker pointer — direct_ ⇄ queued_ — so mutation
// RPCs enqueue during a run and apply synchronously when quiescent, with no per-method threading
// branch. Only the threaded host mounts this.
class AudioDriverRpcService {
public:
    AudioDriverRpcService(Engine& engine, QueuedInvoker& queued, DirectInvoker& direct,
                          EngineInvoker*& active, std::atomic<bool>& audioRunning);
    ~AudioDriverRpcService();
    AudioDriverRpcService(const AudioDriverRpcService&)            = delete;
    AudioDriverRpcService& operator=(const AudioDriverRpcService&) = delete;

    bool          startAudio();
    bool          stopAudio();
    AudioCaptured audioCaptured();
    bool          sleepMs(double ms);
    std::uint32_t systemCount();
    std::uint32_t drainReleased();

private:
    void audioLoop();  // the background audio thread body

    Engine&            engine_;
    QueuedInvoker&     queued_;
    DirectInvoker&     direct_;
    EngineInvoker*&    active_;        // the facade's invoker pointer, swapped on start/stop
    std::atomic<bool>& audioRunning_;  // shared: selects invoker mode + gates the engine reads

    static constexpr std::uint32_t kBlockSize = 1024;
    std::thread                audioThread_;
    std::atomic<double>        capturedEnergy_{0.0};
    std::atomic<std::uint64_t> capturedFrames_{0};
    std::atomic<std::uint32_t> liveSystemCount_{0};
};
