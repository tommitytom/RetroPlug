#pragma once

// An audio system a test scripts, filling the same interface PortAudio and SDL do (docs/modular-graph-phase-f5b-
// design.md section 5.7). Header-only test support under native/test-support, an include root the tests add and no
// product does: what a case needs is a stream it opens by hand, a rate it may adopt, an open it may refuse, a start it
// may refuse, a device it may lose, a pull it drives itself at whatever ragged sizes it likes, and - for the re-apply
// cases - a stream that pulls on a thread of its own the moment start() returns, as a real device does.

#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "audio/AudioBackend.hpp"

namespace rp::audio {

class FakeAudioBackend final : public AudioBackend {
public:
    /** One stream the fake opened. `pull` copies the callback's buffer so a case can read the sample stream a host
     *  produced, whatever the pull sizes were. */
    struct Stream final : AudioStream {
        StreamInfo description;
        Pull       pull = nullptr;
        void*      ctx  = nullptr;
        bool       started = false;
        bool       stopped = false;
        bool        startFails = false;
        std::string startError = "the device would not start";
        bool        lostFlag   = false;
        // A real device's callback can run the moment start() returns. With this set, the stream pulls `pullFrames`
        // (or its own block) on a thread of its own until stop(), which is how a re-apply is exercised under a pull.
        bool        pullOnStart = false;
        int         pullFrames  = 0;
        std::thread worker;
        std::atomic<bool> running{false};
        std::vector<std::string>* log = nullptr;  // the backend's event log, so a case reads start/stop with the rest
        std::vector<float> captured;              // what the last pull wrote

        bool start(std::string& error) override {
            started = !startFails;
            if (!started && error.empty()) error = startError;
            if (log) log->push_back(started ? "start" : "start-failed");
            if (started && pullOnStart) {
                running.store(true);
                worker = std::thread([this] {
                    const int frames = pullFrames > 0 ? pullFrames : description.blockFrames;
                    while (running.load()) {
                        drive(frames);
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                });
            }
            return started;
        }
        void stop() override {
            running.store(false);
            if (worker.joinable()) worker.join();  // no pull is running when this returns, as the contract says
            stopped = true;
            started = false;
            if (log) log->push_back("stop");
        }
        StreamInfo info() const override { return description; }
        bool       lost() const override { return lostFlag; }

        /** One device callback of `frames`: what the backend's real-time thread would do. */
        void drive(int frames) {
            captured.assign(static_cast<std::size_t>(frames) * static_cast<std::size_t>(description.channels), 0.0f);
            if (pull && frames > 0) pull(ctx, captured.data(), frames, false);
        }
    };

    // --- what a case scripts -------------------------------------------------------------------------------------

    std::string              idValue     = "fake";
    std::string              labelValue  = "Fake";
    std::string              autoDriverValue = "fake-driver";
    bool                     openFails   = false;
    std::string              openError   = "the fake refused the device";
    std::string              startError  = "the device would not start";  // what a stream it opens reports
    bool                     startFails  = false;                         // every stream this backend opens refuses
    bool                     pullOnStart = false;  // ...and every stream pulls on its own thread from start(), as a
                                                   // real device does (the re-apply cases and the TSan leg)
    int                      adoptRate   = 0;      // 0: the requested rate; else the rate the device reports
    int                      actualBlock = 0;      // 0: the requested block; else the device's own quantum
    std::vector<std::string>* events = nullptr;     // "open <driver>/<device>", "start", "stop": shared with a client
    // How often the backend was asked to enumerate: a description must not be a use of a library a run never chose.
    int autoDriverCalls = 0;
    int driverCalls     = 0;
    int deviceCalls     = 0;
    Stream*                  lastStream  = nullptr;  // the last stream opened (the HOST owns it; read before stop)

    std::string id() const override { return idValue; }
    std::string label() const override { return labelValue; }
    std::vector<Driver> drivers() override {
        ++driverCalls;
        return {{"fake-driver", "Fake Driver"}, {"other", "Other Driver"}};
    }
    std::string autoDriver() override {
        ++autoDriverCalls;
        return autoDriverValue;
    }
    Devices devices(const std::string& driver) override {
        ++deviceCalls;
        if (driver != "fake-driver") return {};
        return {{"Fake Out", "Fake Out 2"}, "Fake Out"};
    }

    std::unique_ptr<AudioStream> open(const Request& request, Pull pull, void* ctx, std::string& error) override {
        if (openFails) {
            error = openError;
            return nullptr;
        }
        auto stream         = std::make_unique<Stream>();
        stream->description = StreamInfo{request.driver, request.device.empty() ? "Fake Out" : request.device,
                                        adoptRate > 0 ? adoptRate : request.sampleRate,
                                        actualBlock > 0 ? actualBlock
                                                        : (request.blockFrames > 0 ? request.blockFrames : 512),
                                        request.channels};
        stream->pull = pull;
        stream->ctx  = ctx;
        stream->log  = events;
        if (events) events->push_back("open " + request.driver + "/" + stream->description.device);
        stream->startFails  = startFails;
        stream->startError  = startError;
        stream->pullOnStart = pullOnStart;
        lastStream         = stream.get();
        return stream;
    }
};

}  // namespace rp::audio
