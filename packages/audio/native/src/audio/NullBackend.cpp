#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "audio/AudioBackend.hpp"

namespace rp::audio {
namespace {

/** The null device's stream: nothing is opened, and a thread of its own advances the engine at the requested block's
 *  real-time cadence, so a machine with no usable audio still runs (the emulator, the transport, the multi-out
 *  self-test and sdl:smoke's checks all live inside a pull). It is the sleep-paced fallback pump, as a backend. */
class NullStream final : public AudioStream {
public:
    NullStream(const Request& request, Pull pull, void* ctx)
        : pull_(pull), ctx_(ctx), info_{request.driver, "the null device", request.sampleRate,
                                       request.blockFrames > 0 ? request.blockFrames : 512, request.channels} {}

    ~NullStream() override { stop(); }

    bool start(std::string& /*error*/) override {
        if (running_.exchange(true)) return true;
        thread_ = std::thread([this] { loop(); });
        return true;
    }

    void stop() override {
        if (!running_.exchange(false)) return;
        if (thread_.joinable()) thread_.join();
    }

    StreamInfo info() const override { return info_; }
    bool       lost() const override { return false; }

private:
    void loop() {
        std::vector<float> block(static_cast<std::size_t>(info_.blockFrames) * static_cast<std::size_t>(info_.channels),
                                 0.0f);
        while (running_.load(std::memory_order_acquire)) {
            pull_(ctx_, block.data(), info_.blockFrames, false);
            const double secs = info_.sampleRate > 0 ? static_cast<double>(info_.blockFrames) / info_.sampleRate
                                                    : static_cast<double>(info_.blockFrames) / 48000.0;
            std::this_thread::sleep_for(std::chrono::duration<double>(secs));  // approximate realtime
        }
    }

    Pull                   pull_;
    void*                  ctx_;
    StreamInfo             info_;
    std::atomic<bool>      running_{false};
    std::thread            thread_;
};

class NullBackend final : public AudioBackend {
public:
    std::string id() const override { return "null"; }
    std::string label() const override { return "Null"; }
    std::vector<Driver> drivers() override { return {{"null", "Null"}}; }
    std::string autoDriver() override { return "null"; }
    Devices     devices(const std::string&) override { return {{"the null device"}, "the null device"}; }

    std::unique_ptr<AudioStream> open(const Request& request, Pull pull, void* ctx, std::string& error) override {
        if (request.channels <= 0 || request.sampleRate <= 0) {
            error = "the null device needs a positive rate and channel count";
            return nullptr;
        }
        return std::make_unique<NullStream>(request, pull, ctx);
    }
};

}  // namespace

std::unique_ptr<AudioBackend> nullAudioBackend() { return std::make_unique<NullBackend>(); }

}  // namespace rp::audio
