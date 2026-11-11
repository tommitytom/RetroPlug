#pragma once

// The audio backend contract (docs/modular-graph-phase-f5b-design.md section 5.1): the one interface the SDL
// standalone's audio goes through, so what the device, driver and channel count are called stops being PortAudio's
// vocabulary and a saved choice can carry it by name. PortAudio, SDL2 and the null device fill it; nothing above this
// header names an audio library, so the host - and a test - see names and a pull callback.

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace rp::audio {

/** One driver of a backend, by name: "pipewire" / "PipeWire". A backend's own vocabulary, never a library's numeric
 *  id: a saved choice carries this name, and a fork's renumbering must not move it. */
struct Driver {
    std::string id;
    std::string label;
};

/** The devices a driver has, and the name of the one it opens by default. An empty `names` list is a driver that cannot
 *  be enumerated right now (SDL: a driver that is not the one initialised), which the UI shows as "Default". */
struct Devices {
    std::vector<std::string> names;
    std::string              defaultName;
};

/** Whether a backend that runs ONE driver at a time may list `requested`'s devices while `active` is the driver
 *  initialised now: only the driver in force (or a request for "whatever is in force"), because listing another would
 *  mean quitting the one in force - and SDL's quit closes every device the process has open. That is SDL's rule (design
 *  5.1, R8): every other driver answers an empty list, which the row shows as "Default", and picking one opens it after
 *  the host has stopped the old stream. Pure, and in the contract header, so the rule is held without SDL. */
inline bool devicesListableFor(const std::string& active, const std::string& requested) {
    return requested.empty() ? !active.empty() : requested == active;
}

/** What to open. `driver` and `device` empty mean the backend's auto driver and that driver's default device. A
 *  `blockFrames` of 0 asks for the backend's own quantum (a fixed-quantum server's, or render-ahead's). */
struct Request {
    std::string driver;
    std::string device;
    int         sampleRate  = 48000;
    int         blockFrames = 512;
    int         channels    = 2;
};

/** What a stream came up as. The rate may differ from the request (a device that adopts its own), and is what the
 *  engine is re-rated to. */
struct StreamInfo {
    std::string driver;
    std::string device;
    int         sampleRate  = 0;
    int         blockFrames = 0;
    int         channels    = 0;
};

/** The device's pull: fill `interleaved` with `frames` frames of `channels` samples (the stream's channel count, which
 *  is what the request asked for). `underflow` is the device's own report that it ran dry before this call - SDL has
 *  no such flag and never sets it. Called on the backend's real-time thread, so it takes no lock and allocates
 *  nothing. */
using Pull = void (*)(void* ctx, float* interleaved, int frames, bool underflow);

/** One open device. `open` returns it not started: start/stop is the host's, so it can shape the engine between the
 *  two. */
class AudioStream {
public:
    virtual ~AudioStream() = default;
    /** True when the device started. A backend that has no start (the null device's thread is already running) says
     *  true. A refusal puts its own words in `error`, which is what the status row shows: a stream that opens and will
     *  not start is an open failure to the host, not a live-but-silent device. */
    virtual bool start(std::string& error) = 0;
    /** Returns once no pull can run. */
    virtual void stop() = 0;
    virtual StreamInfo info() const = 0;
    /** The device went away, or the stream stopped by itself: the host's cue to fall back (a finished PortAudio stream,
     *  an SDL device whose status is no longer PLAYING). */
    virtual bool lost() const = 0;
};

/** An audio system: the drivers and devices it names, and the streams it opens. Every open that fails reports through
 *  `error` and a null stream, never a throw: a failure is a status the Settings row shows, not an exception the host
 *  has to catch around a device.
 *
 *  Three rules a backend keeps, because the host calls it at these moments and no other:
 *
 *  - the FACTORY runs at startup for every backend a product names (a product's backends are built before anything is
 *    open), so a constructor must not touch its library: `drivers`, `devices` and `autoDriver` are the first use, and
 *    the host calls them only for the backend in force (or on an explicit "More drivers..." step);
 *  - `drivers` / `devices` must never disturb a running stream. SDL is the case in point: switching its driver means
 *    SDL_AudioQuit, which closes every open device, so a listing answers for the driver in force only
 *    (devicesListableFor above);
 *  - `open` returns a stream that is NOT started. The host shapes the engine between open and start, so a backend that
 *    started inside `open` would pull into an engine that is not ready for it. */
class AudioBackend {
public:
    virtual ~AudioBackend() = default;
    /** "portaudio", "sdl", "null": what a saved choice and --audio-backend name it by. */
    virtual std::string id() const = 0;
    /** "PortAudio", "SDL", "Null": what the log line and the Driver row call it. */
    virtual std::string label() const = 0;
    virtual std::vector<Driver> drivers() = 0;
    /** The driver an empty Request::driver resolves to, or "" when none can open (no server, no device). A backend that
     *  enumerates lazily initialises its library here; that is the first use a product makes of it. */
    virtual std::string autoDriver() = 0;
    virtual Devices     devices(const std::string& driver) = 0;
    /** Open a stream, not started. Null (with `error` set) when the device will not open. */
    virtual std::unique_ptr<AudioStream> open(const Request& request, Pull pull, void* ctx,
                                              std::string& error) = 0;
    /** The driver's LABEL, for the log line and the Driver row ("PipeWire", not "pipewire"); the id itself when the
     *  backend does not know the driver. */
    virtual std::string driverLabel(const std::string& driver) {
        for (const Driver& d : drivers())
            if (d.id == driver) return d.label;
        return driver;
    }
};

/** The null device: always opens, and advances the engine at the requested block's real-time cadence on a thread of
 *  its own, so a machine with no usable audio still runs the emulator (and the smoke's transport and multi-out checks).
 *  What the host falls back to; a stream of this backend never reports itself lost. */
std::unique_ptr<AudioBackend> nullAudioBackend();

/** Every backend a product names, in its AUDIO_BACKENDS order (rp_product_audio_backends, the generated header). The
 *  host appends the null one itself. */
using BackendList = std::vector<std::unique_ptr<AudioBackend>>;

}  // namespace rp::audio
