#pragma once

#include <atomic>
#include <cstdint>
#include <thread>

#include "host/rpc/BackendTypes.hpp"

class Engine;
class QueuedInvoker;

// Test/dev harness control: spins the background audio thread that OWNS the Engine while it runs,
// draining the invoker into it each block and publishing observation (energy/frames/count) for the
// control thread. Start/stop just flip the invoker's ownership bit (audioThreadOwns) — while owned,
// mutation RPCs push-only; when quiescent, the invoker flushes each push inline. Only the threaded
// host mounts this (the plugin drives run()/pluginProcessBlock directly).
class AudioDriverRpcService {
public:
    AudioDriverRpcService(Engine& engine, QueuedInvoker& invoker);
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

    Engine&        engine_;
    QueuedInvoker& invoker_;   // the one mutation path; its audioThreadOwns bit is the run condition

    static constexpr std::uint32_t kBlockSize = 1024;
    std::thread                audioThread_;
    std::atomic<double>        capturedEnergy_{0.0};
    std::atomic<std::uint64_t> capturedFrames_{0};
    std::atomic<std::uint32_t> liveSystemCount_{0};
};
