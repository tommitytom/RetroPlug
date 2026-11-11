#include "PortAudioBackend.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <alsa/asoundlib.h>
#include <dlfcn.h>
#endif

#include <portaudio.h>

namespace rp::audio {
namespace {

// RETROPLUG_DEBUG_AUDIO: the backends' own chatter and the host API / device enumeration. Read once (it cannot change
// under us), and by the log handlers below as well as open().
bool debugEnabled() {
    static const bool on = std::getenv("RETROPLUG_DEBUG_AUDIO") != nullptr;
    return on;
}

// The driver ids this backend's host APIs are named by. A FIXED table keyed by the host API TYPE, not the display
// name: a saved choice carries this id (config.json's `audio.driver`), and a fork that renames a host API's display
// string ("Core Audio" -> "CoreAudio") must not move every saved choice. A library's numeric PaHostApiTypeId is exactly
// what a saved file may not carry; this table is the stable NAME for it. A type no table names (a fork's own host API)
// falls back to its display name, lower-cased, so it still has an id rather than none.
std::string driverIdFor(PaHostApiTypeId type, const std::string& hostApiName) {
    switch (type) {
        case paALSA:            return "alsa";
        case paJACK:            return "jack";
        case paOSS:             return "oss";
        case paSndio:           return "sndio";
        case paPipeWire:        return "pipewire";
        case paCoreAudio:       return "coreaudio";
        case paSoundManager:    return "soundmanager";
        case paWASAPI:          return "wasapi";
        case paWDMKS:           return "wdmks";
        case paMME:             return "mme";
        case paDirectSound:     return "directsound";
        case paASIO:            return "asio";
        case paAudioScienceHPI: return "audioscience";
        case paInDevelopment:   break;
    }
    std::string id;
    for (char c : hostApiName) {
        if (c == ' ') id.push_back('-');
        else id.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return id;
}

std::string driverId(const PaHostApiInfo& h) { return driverIdFor(h.type, h.name); }

PaHostApiIndex hostApiForDriver(const std::string& driver) {
    for (PaHostApiIndex i = 0; i < Pa_GetHostApiCount(); ++i) {
        const PaHostApiInfo* h = Pa_GetHostApiInfo(i);
        if (h && driverId(*h) == driver) return i;
    }
    return paHostApiNotFound;
}

// The output-capable device names in a host API (Settings > Audio > Output Device). Empty for an invalid index.
std::vector<std::string> outputDevicesForHostApi(PaHostApiIndex idx) {
    std::vector<std::string> out;
    const PaHostApiInfo*     h = (idx >= 0) ? Pa_GetHostApiInfo(idx) : nullptr;
    if (!h) return out;
    for (int i = 0; i < h->deviceCount; ++i) {
        const PaDeviceIndex dev = Pa_HostApiDeviceIndexToDeviceIndex(idx, i);
        if (dev < 0) continue;
        const PaDeviceInfo* di = Pa_GetDeviceInfo(dev);
        if (di && di->maxOutputChannels > 0) out.push_back(di->name);
    }
    return out;
}

std::string defaultOutputName(PaHostApiIndex idx) {
    const PaHostApiInfo* h = (idx >= 0) ? Pa_GetHostApiInfo(idx) : nullptr;
    if (!h || h->defaultOutputDevice == paNoDevice) return {};
    const PaDeviceInfo* di = Pa_GetDeviceInfo(h->defaultOutputDevice);
    return (di && di->name) ? std::string(di->name) : std::string();
}

PaDeviceIndex findOutputDeviceByName(PaHostApiIndex idx, const std::string& name) {
    const PaHostApiInfo* h = (idx >= 0 && !name.empty()) ? Pa_GetHostApiInfo(idx) : nullptr;
    if (!h) return paNoDevice;
    for (int i = 0; i < h->deviceCount; ++i) {
        const PaDeviceIndex dev = Pa_HostApiDeviceIndexToDeviceIndex(idx, i);
        if (dev < 0) continue;
        const PaDeviceInfo* di = Pa_GetDeviceInfo(dev);
        if (di && di->maxOutputChannels > 0 && name == di->name) return dev;
    }
    return paNoDevice;
}

// The Auto output host API: PipeWire when its host API has an output device (today's rule, which the harness greps),
// then WASAPI on Windows (report item 10: MME is the first host there, and the release-readiness list names it), else
// the host of the platform default output.
//
// WINDOWS, for the agent that runs it (E14, R2): a shared-mode WASAPI device refuses a rate other than its mix format
// unless PaWasapiStreamInfo::flags has paWinWasapiAutoConvert set, and this open passes no host API specific flags. So
// a saved rate WASAPI will not take fails the OPEN, and the host moves to the next BACKEND (SDL), not to MME: if the
// Windows run shows that, the fix is the flag or a fallback to the platform default's host inside this backend.
PaHostApiIndex autoHostApiIndex() {
    auto hasOutput = [](PaHostApiTypeId type) {
        const PaHostApiIndex idx = Pa_HostApiTypeIdToHostApiIndex(type);
        const PaHostApiInfo* h   = (idx >= 0) ? Pa_GetHostApiInfo(idx) : nullptr;
        return (h && h->defaultOutputDevice != paNoDevice) ? idx : static_cast<PaHostApiIndex>(paHostApiNotFound);
    };
    for (PaHostApiTypeId preferred : {paPipeWire, paWASAPI}) {
        const PaHostApiIndex idx = hasOutput(preferred);
        if (idx >= 0) return idx;
    }
    const PaDeviceIndex def = Pa_GetDefaultOutputDevice();
    if (def != paNoDevice) {
        const PaDeviceInfo* di = Pa_GetDeviceInfo(def);
        if (di) return di->hostApi;
    }
    return paHostApiNotFound;
}

// ---- the log quietening ---------------------------------------------------------------------------------------------
// Pa_Initialize brings up EVERY compiled-in host API, and enumerating them is loud on a normal desktop: PortAudio's
// ALSA backend builds its device list by opening every PCM named in the system alsa.conf (rear, center_lfe, side,
// hdmi, modem, phoneline, dsp, a52, usb_stream, dsnoop...), and libasound's default error handler prints each
// failure to stderr - roughly forty lines of "error" about devices we are never going to open. The probe then adds
// its own "GropeDevice: ... set_rate_near failed!" lines through PaUtil_DebugPrint, and libjack prints "jack server
// is not running or cannot be started" wherever no JACK server happens to be up. None of it indicates a problem,
// but it buries the one line that says which device opened. So route all three here: silent by default, verbatim under
// RETROPLUG_DEBUG_AUDIO. Nothing is lost; it takes an env var to see it.
#if defined(__linux__)
void alsaLogHandler(const char* file, int line, const char* function, int err, const char* fmt, ...) {
    if (!debugEnabled()) return;
    std::fprintf(stderr, "[retroplug-sdl] ALSA %s:%d:(%s) ", file ? file : "?", line, function ? function : "?");
    va_list args;
    va_start(args, fmt);
    std::vfprintf(stderr, fmt, args);
    va_end(args);
    if (err) std::fprintf(stderr, ": %s", snd_strerror(err));
    std::fputc('\n', stderr);
}

void jackLogHandler(const char* msg) {
    if (debugEnabled() && msg) std::fprintf(stderr, "[retroplug-sdl] JACK: %s\n", msg);
}
#endif

void paLogHandler(const char* log) {
    if (debugEnabled() && log) std::fputs(log, stderr);
}

// PortAudio's own diagnostics. Its callback hands us a preformatted line, newline included; the symbol is declared here
// rather than by including pa_debugprint.h, a PRIVATE PortAudio header, for two symbols.
extern "C" {
typedef void (*PaUtilLogCallback)(const char* log);
void PaUtil_SetDebugPrintFunction(PaUtilLogCallback cb);
}

// Install the handlers. Must run BEFORE Pa_Initialize: that is where the enumeration (and the noise) happens.
void quietenLogs() {
    PaUtil_SetDebugPrintFunction(paLogHandler);
#if defined(__linux__)
    snd_lib_error_set_handler(alsaLogHandler);
    // libjack is NOT linked: PortAudio dlopen's it (PA_JACK_DYNAMIC), and only once Pa_Initialize runs, which is
    // already too late to catch its first complaint. So load it ourselves first and keep the handle: PortAudio's dlopen
    // then reuses the same library with our handlers in place. Absent libjack simply means no JACK host API.
    if (void* jack = dlopen("libjack.so.0", RTLD_LAZY)) {
        using JackLogSetter = void (*)(void (*)(const char*));
        if (auto setError = reinterpret_cast<JackLogSetter>(dlsym(jack, "jack_set_error_function")))
            setError(jackLogHandler);
        if (auto setInfo = reinterpret_cast<JackLogSetter>(dlsym(jack, "jack_set_info_function")))
            setInfo(jackLogHandler);
        // deliberately not dlclose'd: unloading would drop the handlers we just installed
    }
#endif
}

// The library is initialised on the backend's FIRST USE (finding the auto driver, listing, or opening), never by a
// product that merely links it: the Zoom L6 rule, and what makes a `null` run prove no other backend loaded. Guarded so
// a second call is free.
std::atomic<int> g_paState{0};  // 0 = never, 1 = initialising/ok, 2 = failed (file scope: the destructor reads it)

bool ensureInitialised() {
    std::atomic<int>& state = g_paState;
    int               was   = 0;
    if (!state.compare_exchange_strong(was, 1)) return state.load() == 1;
    quietenLogs();
    const PaError err = Pa_Initialize();
    if (err != paNoError) {
        std::fprintf(stderr, "[retroplug-sdl] PortAudio: Pa_Initialize failed: %s\n", Pa_GetErrorText(err));
        state.store(2);
        return false;
    }
    return true;
}

/** What a callback reads: the host's pull and context. It is allocated before the stream opens (PortAudio needs the
 *  pointer at Pa_OpenStream time) and owned by the stream, so it outlives every callback the stream runs. */
struct Call {
    Pull  pull = nullptr;
    void* ctx  = nullptr;
};

class PortAudioStream final : public AudioStream {
public:
    PortAudioStream(PaStream* stream, StreamInfo info, std::unique_ptr<Call> call)
        : stream_(stream), info_(info), call_(std::move(call)) {}
    ~PortAudioStream() override {
        stop();
        if (stream_) {
            Pa_CloseStream(stream_);
            stream_ = nullptr;
        }
    }

    bool start(std::string& error) override {
        if (!stream_) {
            error = "PortAudio: the stream is not open";
            return false;
        }
        const PaError err = Pa_StartStream(stream_);
        if (err != paNoError) {
            // Into the status as well as the log: a device that opens and will not start is why a run is muted.
            error = std::string("Pa_StartStream failed: ") + Pa_GetErrorText(err);
            return false;
        }
        started_ = true;
        return true;
    }

    void stop() override {
        if (!stream_ || !started_) return;
        Pa_StopStream(stream_);  // blocks until the callback returns (joins it)
        started_ = false;
    }

    StreamInfo info() const override { return info_; }

    bool lost() const override {
        if (!stream_ || !started_) return false;
        // 1 = active, 0 = stopped, negative = the query itself failed: both of the last two mean nothing is pulling.
        return Pa_IsStreamActive(stream_) <= 0;
    }

    static int callback(const void*, void* output, unsigned long frameCount, const PaStreamCallbackTimeInfo*,
                        PaStreamCallbackFlags statusFlags, void* userData) {
        auto* call = static_cast<Call*>(userData);
        if (call && call->pull)
            call->pull(call->ctx, static_cast<float*>(output), static_cast<int>(frameCount),
                       (statusFlags & paOutputUnderflow) != 0);
        return paContinue;
    }

private:
    PaStream*                stream_ = nullptr;
    StreamInfo               info_;
    std::unique_ptr<Call>    call_;
    bool                     started_ = false;
};

class PortAudioBackend final : public AudioBackend {
public:
    ~PortAudioBackend() override {
        // Terminate exactly what ensureInitialised started, so a host that opens nothing never initialises the library
        // and a host that did gives it back. The latch goes back to NEVER, because a backend built after this one must
        // initialise the library again rather than believe the state a destroyed instance left behind.
        if (g_paState.load() == 1) Pa_Terminate();
        g_paState.store(0);
    }

    std::string id() const override { return "portaudio"; }
    std::string label() const override { return "PortAudio"; }

    std::vector<Driver> drivers() override {
        std::vector<Driver> out;
        if (!ensureInitialised()) return out;
        for (PaHostApiIndex i = 0; i < Pa_GetHostApiCount(); ++i) {
            const PaHostApiInfo* h = Pa_GetHostApiInfo(i);
            // A host API with no output device is no driver a user can pick: it would fail at the open.
            if (h && h->defaultOutputDevice != paNoDevice) out.push_back({driverId(*h), h->name});
        }
        return out;
    }

    std::string autoDriver() override {
        if (!ensureInitialised()) return {};
        const PaHostApiIndex idx = autoHostApiIndex();
        const PaHostApiInfo* h   = (idx >= 0) ? Pa_GetHostApiInfo(idx) : nullptr;
        if (!h || h->defaultOutputDevice == paNoDevice) return {};
        return driverId(*h);
    }

    Devices devices(const std::string& driver) override {
        Devices out;
        if (!ensureInitialised()) return out;
        const PaHostApiIndex idx = driver.empty() ? autoHostApiIndex() : hostApiForDriver(driver);
        out.names                = outputDevicesForHostApi(idx);
        out.defaultName          = defaultOutputName(idx);
        return out;
    }

    std::unique_ptr<AudioStream> open(const Request& request, Pull pull, void* ctx, std::string& error) override {
        if (!ensureInitialised()) {
            error = "PortAudio would not initialise";
            return nullptr;
        }
        if (debugEnabled()) {
            for (PaHostApiIndex i = 0; i < Pa_GetHostApiCount(); ++i) {
                const PaHostApiInfo* h = Pa_GetHostApiInfo(i);
                if (h)
                    std::fprintf(stderr, "[retroplug-sdl] host API %d: %s (type=%d, devices=%d, defaultOut=%d)\n", i,
                                 h->name, static_cast<int>(h->type), h->deviceCount, h->defaultOutputDevice);
            }
            for (PaDeviceIndex d = 0; d < Pa_GetDeviceCount(); ++d) {
                const PaDeviceInfo* di = Pa_GetDeviceInfo(d);
                if (di && di->maxOutputChannels > 0)
                    std::fprintf(stderr, "[retroplug-sdl] output device %d: [%s] '%s' (maxOut=%d)\n", d,
                                 Pa_GetHostApiInfo(di->hostApi)->name, di->name, di->maxOutputChannels);
            }
        }

        const PaHostApiIndex hostIdx = request.driver.empty() ? autoHostApiIndex() : hostApiForDriver(request.driver);
        if (hostIdx < 0) {
            error = "PortAudio: no driver '" + request.driver + "' with an output device";
            return nullptr;
        }
        PaDeviceIndex dev = paNoDevice;
        if (!request.device.empty()) {
            dev = findOutputDeviceByName(hostIdx, request.device);
            if (dev == paNoDevice)
                std::fprintf(stderr,
                             "[retroplug-sdl] audio: output device '%s' not found in the chosen driver, using its "
                             "default\n",
                             request.device.c_str());
        }
        if (dev == paNoDevice) {
            const PaHostApiInfo* hi = Pa_GetHostApiInfo(hostIdx);
            if (hi && hi->defaultOutputDevice != paNoDevice) dev = hi->defaultOutputDevice;
        }
        if (dev == paNoDevice) dev = Pa_GetDefaultOutputDevice();  // last resort
        if (dev == paNoDevice) {
            error = "PortAudio: no default output device";
            return nullptr;
        }

        PaStreamParameters out{};
        out.device                    = dev;
        out.channelCount              = request.channels;
        out.sampleFormat              = paFloat32;  // interleaved float, frame-major (what the pull writes)
        out.suggestedLatency          = Pa_GetDeviceInfo(dev)->defaultLowOutputLatency;
        out.hostApiSpecificStreamInfo = nullptr;
        // A block of 0 asks for the server's own quantum: what render-ahead wants, so a fixed-quantum server schedules
        // the callback at its cycle rather than at ours.
        const unsigned long callbackFrames =
            request.blockFrames > 0 ? static_cast<unsigned long>(request.blockFrames) : paFramesPerBufferUnspecified;
        auto      call    = std::make_unique<Call>();
        call->pull        = pull;
        call->ctx         = ctx;
        PaStream* stream  = nullptr;
        const PaError err = Pa_OpenStream(&stream, nullptr, &out, request.sampleRate, callbackFrames, paNoFlag,
                                          &PortAudioStream::callback, call.get());
        if (err != paNoError) {
            error = std::string("Pa_OpenStream(") + std::to_string(request.sampleRate) + " Hz, " +
                    std::to_string(request.blockFrames) + " frames, " + std::to_string(request.channels) +
                    " ch) failed: " + Pa_GetErrorText(err);
            return nullptr;
        }

        const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(dev);
        StreamInfo          info;
        info.driver       = driverId(*Pa_GetHostApiInfo(hostIdx));
        info.device       = deviceInfo->name;
        info.channels     = request.channels;
        info.blockFrames  = request.blockFrames;
        info.sampleRate   = request.sampleRate;
        if (const PaStreamInfo* si = Pa_GetStreamInfo(stream)) info.sampleRate = static_cast<int>(si->sampleRate);
        // Wider than the device takes: legal on PipeWire (the stream carries its own layout), but only the pairs the
        // device has ports for are connected, and the rest wait for a patchbay. Say so, or "Out Channels: 8 and I only
        // hear the first pair" looks like a bug rather than a routing step.
        if (request.channels > deviceInfo->maxOutputChannels)
            std::fprintf(stderr,
                         // The em dash is the bytes today's line carries (U+2014), spelled as bytes so no compiler's
                         // execution character set decides what the output says.
                         "[retroplug-sdl] audio: '%s' takes %d ch \xE2\x80\x94 pairs beyond the first %d channel(s) "
                         "are "
                         "unconnected until you patch them (qpwgraph / your DAW)\n",
                         deviceInfo->name, deviceInfo->maxOutputChannels, deviceInfo->maxOutputChannels);

        return std::make_unique<PortAudioStream>(stream, info, std::move(call));
    }
};

}  // namespace

std::unique_ptr<AudioBackend> rp_portaudio_backend() { return std::make_unique<PortAudioBackend>(); }

}  // namespace rp::audio
