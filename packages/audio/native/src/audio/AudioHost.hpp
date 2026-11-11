#pragma once

// The host's audio: the whole open / start / stop / reconfigure sequence, the fallback to the null device with a status
// the UI shows, a lost device as the same fallback, render-ahead and the stats (docs/modular-graph-phase-f5b-design.md
// section 5.2). It never names the Engine: the product's render loop is behind AudioClient, which is where today's
// renderAudioBlock lives.
//
// The order a reconfigure runs in is stop, release, open, [render-ahead's ring and renderer], prepare(info()), start.
// Opening before prepare is what lets prepare see the rate the device actually runs at: a device may adopt another
// one, and an engine re-rated with the REQUESTED rate would render at the wrong pitch. The ring and its counters are
// set up BEFORE start too: a device's callback can run the moment start() returns, and a pull that found the previous
// run's counters would read a ring nothing had sized yet.
//
// One sender, one renderer: pull() is the audio path and runs on a backend's real-time thread; the render-ahead
// renderer is the same client's render on a thread of its own, and never at the same time as pull's (the ring decides
// which of the two renders a block). The stats are the renderer's alone under render-ahead (the pull only counts
// underruns into an atomic), so the accumulator is one thread's. apply/stop/poll are the control thread's.
//
// Real-time treatment is here too, because only the host sees both threads: on Linux the device callback runs
// SCHED_FIFO 85 above the renderer's 80 under render-ahead (80 otherwise), both pinned to the audio core, applied per
// THREAD (a callback thread is new with each stream, so a per-process latch left a re-opened stream untuned). Best
// effort: without RT permission the default scheduling stands, as it always has.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "audio/AudioBackend.hpp"

namespace rp::audio {

/** The product's side of the host: the engine, behind an interface AudioHost never names. */
class AudioClient {
public:
    virtual ~AudioClient() = default;
    /** Re-rate, reshape and size the render for the obtained stream. Called between release and start, so the engine is
     *  quiescent, and it HANDS THE ENGINE BACK to the audio thread before it returns: the host's next call is start(),
     *  and the first pull must find an engine it may render. */
    virtual void prepare(double sampleRate, int maxBlockFrames, int channels) = 0;
    /** Render one interleaved block: today's renderAudioBlock, with the buffer sized by the caller. */
    virtual void render(float* interleaved, int frames) = 0;
    /** Take the engine back from the audio thread (the invoker drain), so prepare can re-rate it. */
    virtual void release() = 0;
};

/** One run's audio choice: the `audio` config section's fields plus render-ahead, which the section does not carry (it
 *  is a flag). */
struct AudioConfig {
    std::string backend         = "auto";  // "auto" takes the first backend whose auto driver opens
    std::string driver          = "";
    std::string device          = "";
    int         sampleRate      = 48000;
    int         blockSize       = 512;
    int         channels        = 2;
    int         renderAheadFrames = 0;  // 0 = render inside the pull (the default)
};

/** One run's request with its flags applied. `--audio-backend` / RETROPLUG_SDL_AUDIO_BACKEND replaces only the
 *  BACKEND: the saved driver and device are that backend's own vocabulary, so they survive when the saved section names
 *  the backend the flag pins, and are cleared only when it names another one (whose names would mean nothing to it). A
 *  block-size override wins over the saved block, because a launcher's quantum has to reach the device (E12). */
AudioConfig applyRunOverrides(AudioConfig config, const std::string& backendOverride, int blockSizeOverride);

/** What is running, for the Settings row: empty `error` is a real device; otherwise it is the bare reason the null
 *  fallback is playing, and the row renders it as "Muted: <reason>". */
struct AudioStatus {
    std::string backend;
    std::string driver;
    std::string device;
    int         sampleRate  = 0;
    int         blockFrames = 0;
    int         channels    = 0;
    std::string error;
};

/** One driver of one backend, as the Settings > Audio rows read them. */
struct AudioDriverDescription {
    std::string id;
    std::string label;
    Devices     devices;
};

/** One backend, its drivers and the driver "auto" resolves to right now ("" when none can open). */
struct AudioBackendDescription {
    std::string                    id;
    std::string                    label;
    std::string                    autoDriver;
    std::vector<AudioDriverDescription> drivers;
};

/** What a RUN's flags pin, regardless of what the section saved (--audio-backend, --block-size): the rows name it so a
 *  user can see why the saved choice is not the one running. Empty/0 when no flag is set. */
struct RunOverrides {
    std::string backend;
    int         blockSize = 0;
};

/** Everything the Settings > Audio rows show: each backend with its drivers and devices, the status, and the run's
 *  overrides. */
struct AudioDescription {
    std::vector<AudioBackendDescription> backends;
    AudioStatus                          status;
    RunOverrides                         overrides;
};

class AudioHost {
public:
    /** `preference` is the product's AUDIO_BACKENDS order (rp_product_audio_backends()); the null device is appended
     *  here, last, so a product that names no backend still runs. */
    AudioHost(BackendList preference, AudioClient& client);
    ~AudioHost();
    AudioHost(const AudioHost&)            = delete;
    AudioHost& operator=(const AudioHost&) = delete;

    /** The whole sequence, once: stop, release, open, prepare(info()), start. `backend` empty or "auto" takes the first
     *  backend whose auto driver opens. A failed open falls back to the null device with the reason in the status
     *  (`muted`); true when a real device is running. */
    bool apply(const AudioConfig& config);

    /** The teardown path: stop the device and take the engine back. */
    void stop();

    /** The UI frame: a lost stream (or one the watchdog has not seen a pull from) falls back to the null device, and
     *  the status moves. True when the status changed. It never blocks on a callback that is inside a pull: a pull in
     *  progress is a live device, not a stall, and a stop that will not come back mutes the run instead of freezing the
     *  window. */
    bool poll();

    AudioStatus      status() const;
    /** Each backend's id and label, and what the backend IN FORCE has: its drivers with their devices. Enumerating a
     *  backend initialises its library, so no other one is touched here (the Zoom L6 rule, and why a null run's startup
     *  opens nothing). */
    AudioDescription describe() const;
    /** One backend's drivers and devices, on demand: the Driver row that offers another backend asks for it here, and
     *  that call is a real use of its library. */
    std::vector<AudioDriverDescription> backendDrivers(const std::string& backendId) const;
    /** True when a real device (not the null fallback) is open and started. */
    bool live() const;

    /** The audio path: one device callback's worth. Public for the null device and the fake's own thread, which call it
     *  through the Pull it was handed; a product never calls it. */
    void pull(float* interleaved, int frames, bool underflow);
    static void pullTrampoline(void* ctx, float* interleaved, int frames, bool underflow);

    /** The watchdog's bound: a started device with no pull IN PROGRESS and none finished for this long is treated as
     *  lost - the bound is the GAP between pulls, never one pull's own duration. `apply` sets it
     *  to max(250 ms, 4 x the duration of the LONGER of the block the run renders in and the device's own quantum: under
     *  render-ahead the stream is opened with a block of 0 and pulls in its own quantum, which can outlast the render
     *  block. A test may shorten it. */
    void setWatchdogBound(std::chrono::milliseconds bound) { watchdogBound_ = bound; }
    std::chrono::milliseconds watchdogBound() const { return watchdogBound_; }

    /** Render-ahead's underrun count since the stream started: the pull pads a short ring with silence and counts it
     *  once the ring has primed. A render-ahead run that never underruns is what "the ring is deep enough" means. */
    std::uint64_t underruns() const { return underruns_.load(std::memory_order_relaxed); }

    /** The log sink: what would be written to stderr, one line at a time, prefix included. A test installs one to hold
     *  the bytes the harnesses grep and the stats line; the default writes them to stderr. */
    using LogSink = std::function<void(const std::string&)>;
    void setLogSink(LogSink sink) { sink_ = std::move(sink); }

    /** The stats' reporting interval (RETROPLUG_SDL_AUDIO_STATS), and its switch. For a test. */
    void setStatsForTest(bool enabled, std::chrono::milliseconds interval);

private:
    struct Resolved {
        AudioBackend* backend = nullptr;
        std::string   driver;   // the resolved driver's id
        std::string   device;   // the resolved device's name
    };

    /** Resolve the config's backend and driver, then open. Sets `error` and leaves `stream` null on failure. */
    std::unique_ptr<AudioStream> openFor(const AudioConfig& config, Resolved& resolved, StreamInfo& info,
                                         std::string& error);
    /** Open AND start: the ring and the renderer before the device can pull, `prepare` with what the device obtained,
     *  `start`, and the watchdog's clock. False on either failure, with nothing left open or started and `error` set:
     *  a stream that opens and will not start is an open failure, not a live-but-silent host. */
    bool openAndStart(const AudioConfig& config, Resolved& resolved, StreamInfo& info, std::string& error);
    /** The null device's open-and-start, with `reason` as the status: every failure lands here, because a machine with
     *  no usable device still runs the emulator (the sleep-paced pump). False when even that failed. */
    bool fallbackToNull(const std::string& reason);
    void setWatchdogFor(int blockFrames);  // the bound a block's duration allows
    /** Stops and frees the stream and ends the renderer if one runs, waiting at most `bound` for the callback. False
     *  when the callback was still inside a pull: then nothing below the stream (the ring it reads, the engine it owns)
     *  is touched, and the stream stays alive. */
    bool closeCurrent(std::chrono::milliseconds bound);
    void startRenderer(int blockFrames, int channels);
    void stopRenderer();
    void renderLoop(int blockFrames, int channels);
    void reportStats(double renderSec, double renderCpuSec, double blockSec, int blockFrames);
    void logLine(const std::string& text) const;

    BackendList                                       preference_;  // the product's, null last
    AudioClient*                                      client_;
    std::unique_ptr<AudioStream>                      stream_;
    AudioBackend*                                     backend_ = nullptr;  // the open stream's
    AudioConfig                                       config_;
    StreamInfo                                        info_;
    std::string                                       error_;
    bool                                              started_ = false;
    // The thread that built the host (the control thread): a pull on it is a test's hand-cranked one, not a device's.
    std::thread::id                                   controlThread_;

    // Render-ahead: a render thread keeps interleaved frames in a ring and the pull only copies.
    bool                       renderAhead_ = false;
    std::vector<float>         ring_;
    std::size_t                ringFrames_ = 0;
    std::atomic<std::uint64_t> ringWritten_{0};
    std::atomic<std::uint64_t> ringRead_{0};
    std::atomic<bool>          ringPrimed_{false};
    std::atomic<std::uint64_t> underruns_{0};
    std::vector<float>         renderScratch_;
    std::thread                renderThread_;
    std::atomic<bool>          renderRun_{false};
    int                        blockFrames_ = 0;  // the block the engine was shaped for (the watchdog's, the stats')

    // The watchdog's witness: whether a pull is running, when one last finished, when the UI last looked, and the bound.
    // A stream that starts and never pulls is caught by the start stamp; a pull that is still inside the engine is a
    // live device (a long block is one block's work, not a stall); and the UI's own absence - a debugger pause, a laptop
    // asleep - restamps instead of blaming the device for a gap the device had no chance to fill.
    std::atomic<std::uint64_t>    pulls_{0};
    std::atomic<bool>             inPull_{false};
    std::atomic<std::int64_t>     lastPullMs_{0};
    std::atomic<std::int64_t>     lastPollMs_{0};
    std::chrono::milliseconds     watchdogBound_{250};

    // The stats (RETROPLUG_SDL_AUDIO_STATS): load, worst block, real frames/s and underruns, every few seconds. One
    // thread's accumulator: under render-ahead the renderer alone reports, and the pull only counts its underruns.
    bool                                                     statsEnabled_ = false;
    std::chrono::milliseconds                                statsInterval_{5000};
    double                                                   statsBusySec_ = 0, statsCpuSec_ = 0, statsWallSec_ = 0;
    double                                                   statsWorst_ = 0;
    std::uint64_t                                            statsBlocks_ = 0, statsUnderflows_ = 0, statsFrames_ = 0;
    std::uint64_t                                            statsTotalUnderflows_ = 0;
    std::atomic<std::uint64_t>                               pendingUnderflows_{0};
    std::chrono::steady_clock::time_point                    statsSince_ = std::chrono::steady_clock::now();

    LogSink                                            sink_;  // empty: stderr
};

}  // namespace rp::audio
