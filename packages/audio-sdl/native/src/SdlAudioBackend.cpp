#include "SdlAudioBackend.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <SDL.h>

namespace rp::audio {
namespace {

/** What a callback reads: the host's pull and its context. SDL hands the pointer back unchanged, and the device is
 *  closed before the stream goes, so it outlives every call. */
struct SdlCall {
    Pull  pull     = nullptr;
    void* ctx      = nullptr;
    int   channels = 2;
};

bool audioSubsystemReady() {
    // Once, and the failure is said once: a run whose audio subsystem will not come up would otherwise print the same
    // line on every listing and every open.
    static std::once_flag once;
    static bool           ok = false;
    std::call_once(once, [] {
        ok = SDL_InitSubSystem(SDL_INIT_AUDIO) == 0;
        if (!ok) std::fprintf(stderr, "[retroplug-sdl] SDL: audio subsystem init failed: %s\n", SDL_GetError());
    });
    return ok;
}

/** The drivers a user may pick: SDL's own list, less the `dummy` and `disk` test drivers, which exist for CI and for
 *  writing a file rather than for playing anything. */
std::vector<std::string> usableDrivers() {
    std::vector<std::string> out;
    if (!audioSubsystemReady()) return out;
    const int count = SDL_GetNumAudioDrivers();
    for (int i = 0; i < count; ++i) {
        const char* name = SDL_GetAudioDriver(i);
        if (!name) continue;
        if (std::strcmp(name, "dummy") == 0 || std::strcmp(name, "disk") == 0) continue;
        out.push_back(name);
    }
    return out;
}

/** The driver initialised right now, or "" when none is. SDL runs ONE audio driver at a time (SDL_AudioInit /
 *  SDL_AudioQuit), which is why a device list is real only for the active one. */
std::string activeDriver() {
    const char* name = SDL_GetCurrentAudioDriver();
    return name ? std::string(name) : std::string();
}

/** How many of this backend's devices are open right now. SDL runs ONE audio driver at a time and SDL_AudioQuit closes
 *  every device the process has open, so a driver may only be switched while this is 0: the host stops and closes the
 *  stream before it opens another. */
std::atomic<int>& openDevices() {
    static std::atomic<int> count{0};
    return count;
}

/** Initialise `driver` ("" asks for SDL's choice, which it makes once). Switching drivers means quitting the current
 *  one first, which is only legal with nothing of ours open (see openDevices): a listing never switches. */
bool ensureDriver(const std::string& driver, std::string& error) {
    if (!audioSubsystemReady()) {
        error = "SDL: audio subsystem initialisation failed";
        return false;
    }
    const std::string current = activeDriver();
    if (driver.empty()) {
        if (!current.empty()) return true;
        if (SDL_AudioInit(nullptr) != 0) {
            error = std::string("SDL_AudioInit failed: ") + SDL_GetError();
            return false;
        }
        return true;
    }
    if (current == driver) return true;
    if (openDevices().load() > 0) {
        error = "SDL: cannot switch audio driver to '" + driver + "' while a device is open";
        return false;
    }
    if (!current.empty()) SDL_AudioQuit();
    if (SDL_AudioInit(driver.c_str()) != 0) {
        error = std::string("SDL_AudioInit('") + driver + "') failed: " + SDL_GetError();
        return false;
    }
    return true;
}

class SdlStream final : public AudioStream {
public:
    SdlStream(SDL_AudioDeviceID device, StreamInfo info, std::unique_ptr<SdlCall> call)
        : device_(device), info_(info), call_(std::move(call)) {
        openDevices().fetch_add(1);
    }
    ~SdlStream() override {
        stop();
        if (device_ != 0) {
            SDL_CloseAudioDevice(device_);
            openDevices().fetch_sub(1);
        }
    }

    bool start(std::string& error) override {
        if (device_ == 0) {
            error = "SDL: the audio device is not open";
            return false;
        }
        SDL_PauseAudioDevice(device_, 0);
        started_ = true;
        return true;
    }

    void stop() override {
        if (device_ == 0 || !started_) return;
        SDL_PauseAudioDevice(device_, 1);  // returns once the callback is paused
        started_ = false;
    }

    StreamInfo info() const override { return info_; }

    bool lost() const override {
        if (device_ == 0 || !started_) return false;
        // SDL 2.0.4 and later report a removed device as no longer PLAYING.
        return SDL_GetAudioDeviceStatus(device_) != SDL_AUDIO_PLAYING;
    }

    static void SDLCALL callback(void* userData, Uint8* stream, int len) {
        auto* call = static_cast<SdlCall*>(userData);
        if (!call || !call->pull || len <= 0) return;
        const int frames =
            len / static_cast<int>(sizeof(float) * static_cast<std::size_t>(std::max(1, call->channels)));
        // SDL has no "the device ran dry" flag: it hands a buffer to fill and never says whether it underran.
        call->pull(call->ctx, reinterpret_cast<float*>(stream), frames, false);
    }

private:
    SDL_AudioDeviceID     device_ = 0;
    StreamInfo            info_;
    std::unique_ptr<SdlCall> call_;
    bool                  started_ = false;
};

/** SDL's audio backend: one driver at a time, a float32 pull callback, the frequency allowed to change and adopted,
 *  the channel count exact (the engine renders for that width). */
class SdlBackend final : public AudioBackend {
public:
    std::string id() const override { return "sdl"; }
    std::string label() const override { return "SDL"; }

    std::vector<Driver> drivers() override {
        std::vector<Driver> out;
        for (const std::string& id : usableDrivers()) {
            // SDL's driver names are lowercase ids with no separate label ("pipewire", "pulseaudio", "wasapi"), so the
            // label is the id: the Driver row reads "SDL: pipewire", as the design's example does.
            out.push_back({id, id});
        }
        return out;
    }

    std::string autoDriver() override {
        if (usableDrivers().empty()) return {};
        std::string error;
        if (!ensureDriver("", error)) return {};
        return activeDriver();  // what SDL chose
    }

    Devices devices(const std::string& driver) override {
        // The list is real ONLY for the driver initialised now: SDL runs one at a time, and enumerating another would
        // mean SDL_AudioQuit + SDL_AudioInit, which closes every open device - the one thing a listing must never do
        // (the UI calls this several times per menu render). Every other driver answers empty, which the row shows as
        // "Default"; picking that driver opens it, and the host has stopped the old stream by then (design 5.1, R8).
        Devices out;
        if (!audioSubsystemReady()) return out;
        if (!devicesListableFor(activeDriver(), driver)) return out;
        const int count = SDL_GetNumAudioDevices(0);
        for (int i = 0; i < count; ++i) {
            const char* name = SDL_GetAudioDeviceName(i, 0);
            if (name) out.names.push_back(name);
        }
        // SDL 2.24 and later name the default device; the same shape as PortAudio's defaultOutputName, which is what an
        // empty device selection resolves to.
        char*         defaultName = nullptr;
        SDL_AudioSpec spec{};
        if (SDL_GetDefaultAudioInfo(&defaultName, &spec, 0) == 0 && defaultName) {
            out.defaultName = defaultName;
            SDL_free(defaultName);
        }
        return out;
    }

    std::unique_ptr<AudioStream> open(const Request& request, Pull pull, void* ctx, std::string& error) override {
        // The one call that may switch drivers: the host has stopped and closed the previous stream (a switch with
        // anything open is refused by ensureDriver), so nothing runs under the quit.
        if (!ensureDriver(request.driver, error)) return nullptr;

        SDL_AudioSpec want{};
        want.freq     = request.sampleRate;
        want.format   = AUDIO_F32SYS;
        want.channels = static_cast<Uint8>(request.channels > 0 ? request.channels : 2);
        // SDL has no "unspecified" block size: 0 means a value of SDL's. A request of 0 (render-ahead's) gets a small
        // power of two, and the obtained size is reported in StreamInfo either way.
        want.samples  = static_cast<Uint16>(request.blockFrames > 0 ? request.blockFrames : 512);
        auto call     = std::make_unique<SdlCall>();
        call->pull    = pull;
        call->ctx     = ctx;
        call->channels = want.channels;
        want.callback = &SdlStream::callback;
        want.userdata = call.get();

        SDL_AudioSpec     have{};
        bool              usedDefault = request.device.empty();
        const char*       deviceName  = request.device.empty() ? nullptr : request.device.c_str();
        SDL_AudioDeviceID id = SDL_OpenAudioDevice(deviceName, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
        if (id == 0 && deviceName) {
            // A saved name the driver no longer lists (a device unplugged, or a pick from another machine): say so and
            // use the driver's default, the same fallback PortAudio's backend makes. Muting for it would leave a user
            // with no sound and no obvious cause.
            std::fprintf(stderr, "[retroplug-sdl] audio: output device '%s' not found in the chosen driver, using its "
                                 "default\n",
                         request.device.c_str());
            deviceName  = nullptr;
            usedDefault = true;
            id = SDL_OpenAudioDevice(nullptr, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
        }
        if (id == 0) {
            error = std::string("SDL_OpenAudioDevice('") + (request.device.empty() ? "default" : request.device) +
                    "') failed: " + SDL_GetError();
            return nullptr;
        }
        StreamInfo info;
        info.driver = activeDriver();
        // The name a choice can be saved as and reopened by: SDL's default device name where it has one, "" (the
        // driver's default) where it does not. "Default" was neither - open() cannot reopen it.
        info.device = usedDefault ? "" : request.device;
        if (info.device.empty()) {
            char*         defaultName = nullptr;
            SDL_AudioSpec spec{};
            if (SDL_GetDefaultAudioInfo(&defaultName, &spec, 0) == 0 && defaultName) {
                info.device = defaultName;
                SDL_free(defaultName);
            }
        }
        info.sampleRate  = have.freq;
        info.blockFrames = have.samples;
        // The channel count is exact, never allowed to change: the engine renders for this width.
        info.channels = have.channels;
        // A wide request against SDL's PipeWire driver is accepted at the API (the host renders this width, and it is
        // what StreamInfo reports) but the node SDL registers has 2 output ports, so only the first pair reaches the
        // sink - whether SDL mixes it or the server's stream adapter does is not established (design R11). PortAudio's
        // declared layout is the opposite: one port per channel, with the pairs beyond the device's own left
        // unconnected until they are patched, which is a routing step a user can see. This one is silent, so it says so.
        if (info.channels > 2 && info.driver == "pipewire")
            std::fprintf(stderr,
                         "[retroplug-sdl] audio: this %d-channel stream's PipeWire node has 2 output ports, so only the "
                         "first pair reaches the sink (Settings > Audio > Driver, PortAudio, is one port per channel)\n",
                         info.channels);
        return std::make_unique<SdlStream>(id, info, std::move(call));
    }
};

}  // namespace

std::unique_ptr<AudioBackend> rp_sdl_backend() { return std::make_unique<SdlBackend>(); }

}  // namespace rp::audio
