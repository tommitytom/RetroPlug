#include "audio/AudioHost.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <utility>

#if defined(__linux__)
#include <ctime>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#endif

namespace rp::audio {
namespace {

// Today's log voice, kept byte for byte: the harnesses grep "[retroplug-sdl] audio: PortAudio [h] 'd' N Hz, B frames,
// C ch" (tools/run-sdl-pipewire.sh's sed and grep), and the CLI prints the same prefix through the midi package.
constexpr const char* kLogPrefix = "[retroplug-sdl] ";

std::int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

double threadCpuSeconds() {
#if defined(__linux__)
    timespec ts{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) * 1e-9;
#else
    return 0.0;
#endif
}

std::string normalizedBackend(const AudioConfig& c) { return c.backend.empty() ? std::string("auto") : c.backend; }

/** Whether two requests ask for the same run. The BACKEND is part of the choice: PortAudio and SDL both name a driver
 *  "pipewire", so a switch between them with the same driver, device and rate would otherwise be a no-op that left the
 *  old backend driving the device. */
bool sameChoice(const AudioConfig& a, const AudioConfig& b) {
    return normalizedBackend(a) == normalizedBackend(b) && a.driver == b.driver && a.device == b.device &&
           a.sampleRate == b.sampleRate && a.blockSize == b.blockSize && a.channels == b.channels &&
           a.renderAheadFrames == b.renderAheadFrames;
}

// The real-time priorities the two threads ask for: the device callback above the renderer under render-ahead, so a
// block that the renderer is still filling loses to the pull that copies it.
constexpr int kCallbackPriority        = 80;
constexpr int kCallbackPriorityAhead   = 85;
constexpr int kRendererPriority        = 80;

/** Best effort, ONCE PER THREAD: real-time scheduling and the audio core for the calling thread. Per thread, not per
 *  process: a re-opened stream's callback is a new thread, and each one asks once for itself. Without RT permission,
 *  the default scheduling stands, and the UI's own pin away from the audio core is this product's (main.cpp). */
void tuneAudioThread(int priority) {
#if defined(__linux__)
    static thread_local bool tuned = false;
    if (tuned) return;
    tuned = true;
    const long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    if (ncpu > 1) {
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(ncpu - 1, &set);  // the last core, which the UI is pinned away from
        pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
    }
    sched_param sp{};
    sp.sched_priority = priority;
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
#else
    (void)priority;
#endif
}

/** The hint an open failure carries: the backend's own diagnostics are silent unless asked for, so a muted run says how
 *  to ask. */
const char* debugHint() {
    static const bool debug = std::getenv("RETROPLUG_DEBUG_AUDIO") != nullptr;
    // The em dash is the one today's line carried (U+2014), spelled as bytes so no execution character set decides it.
    return debug ? "" : " \xE2\x80\x94 re-run with RETROPLUG_DEBUG_AUDIO=1 for the backend's diagnostics";
}

}  // namespace

AudioHost::AudioHost(BackendList preference, AudioClient& client)
    : preference_(std::move(preference)), client_(&client), controlThread_(std::this_thread::get_id()) {
    preference_.push_back(nullAudioBackend());  // last, always: a product that names no backend still runs
    statsEnabled_ = std::getenv("RETROPLUG_SDL_AUDIO_STATS") != nullptr;
}

AudioHost::~AudioHost() {
    closeCurrent();
}

void AudioHost::logLine(const std::string& text) const {
    const std::string line = std::string(kLogPrefix) + text;
    if (sink_) {
        sink_(line);
        return;
    }
    std::fprintf(stderr, "%s\n", line.c_str());
}

bool AudioHost::live() const {
    return started_ && stream_ != nullptr && backend_ != nullptr && backend_->id() != "null";
}

AudioStatus AudioHost::status() const {
    AudioStatus out;
    if (backend_) {
        out.backend = backend_->id();
        out.driver  = info_.driver;
        out.device  = info_.device;
    }
    out.sampleRate  = info_.sampleRate;
    out.blockFrames = info_.blockFrames;
    out.channels    = info_.channels;
    out.error       = error_;
    return out;
}

AudioDescription AudioHost::describe() const {
    // ONLY the backend in force is enumerated, and only while a real one is: a backend initialises its library at its
    // first use, so asking a PortAudio or SDL backend to list its drivers is exactly that use. A `null` run - and the
    // startup of any run before its device is open - therefore touches no other audio library, which is what lets a
    // machine whose default ALSA device is somebody else's card run this app without opening it. A row that needs
    // another backend's drivers asks for it, once, through backendDrivers().
    AudioDescription out;
    const std::string inForce = backend_ ? backend_->id() : std::string();
    for (const auto& backend : preference_) {
        AudioBackendDescription described;
        described.id    = backend->id();
        described.label = backend->label();
        if (described.id != inForce) {
            out.backends.push_back(std::move(described));
            continue;
        }
        described.autoDriver = backend->autoDriver();
        for (const Driver& driver : backend->drivers()) {
            AudioDriverDescription d;
            d.id      = driver.id;
            d.label   = driver.label;
            d.devices = backend->devices(driver.id);
            described.drivers.push_back(std::move(d));
        }
        out.backends.push_back(std::move(described));
    }
    out.status = status();
    return out;
}

AudioConfig applyRunOverrides(AudioConfig config, const std::string& backendOverride, int blockSizeOverride) {
    if (blockSizeOverride > 0) config.blockSize = blockSizeOverride;
    if (backendOverride.empty()) return config;
    if (config.backend != backendOverride) {
        config.driver.clear();
        config.device.clear();
    }
    config.backend = backendOverride;
    return config;
}

std::vector<AudioDriverDescription> AudioHost::backendDrivers(const std::string& backendId) const {
    std::vector<AudioDriverDescription> out;
    for (const auto& backend : preference_) {
        if (backend->id() != backendId) continue;
        for (const Driver& driver : backend->drivers()) {
            AudioDriverDescription d;
            d.id      = driver.id;
            d.label   = driver.label;
            d.devices = backend->devices(driver.id);
            out.push_back(std::move(d));
        }
    }
    return out;
}

std::unique_ptr<AudioStream> AudioHost::openFor(const AudioConfig& config, Resolved& resolved, StreamInfo& info,
                                                std::string& error) {
    Request request;
    request.device      = config.device;
    request.sampleRate  = config.sampleRate;
    request.channels    = config.channels;
    request.blockFrames = config.renderAheadFrames > 0 ? 0 : config.blockSize;

    const bool explicitBackend = !config.backend.empty() && config.backend != "auto";
    for (const auto& backend : preference_) {
        if (explicitBackend && backend->id() != config.backend) continue;
        if (!explicitBackend && backend->id() == "null") continue;  // auto never picks the fallback
        request.driver = config.driver.empty() ? backend->autoDriver() : config.driver;
        if (request.driver.empty()) {
            if (!explicitBackend) continue;  // this backend cannot open anything right now: the next one may
            error = backend->label() + ": no audio driver is usable";
            return nullptr;
        }
        std::string attempt;
        std::unique_ptr<AudioStream> stream = backend->open(request, &AudioHost::pullTrampoline, this, attempt);
        if (stream) {
            resolved.backend = backend.get();
            resolved.driver  = request.driver;
            resolved.device  = stream->info().device;
            info             = stream->info();
            return stream;
        }
        if (!config.driver.empty() || explicitBackend) {
            error = attempt;  // an explicitly named backend or driver: its failure is the answer
            return nullptr;
        }
        error = attempt;  // remembered for the fallback's status; the next backend is tried
    }
    if (error.empty()) error = explicitBackend ? "no audio backend '" + config.backend + "'" : "no audio device";
    return nullptr;
}

bool AudioHost::openAndStart(const AudioConfig& config, Resolved& resolved, StreamInfo& info, std::string& error) {
    std::unique_ptr<AudioStream> stream = openFor(config, resolved, info, error);
    if (!stream) return false;
    // Render-ahead is decided BEFORE the request is made (a backend asked for its own quantum is asked because the
    // renderer fills whole blocks); the null device never renders ahead, so a fallback asks for its block and renders
    // in the pull.
    renderAhead_ = config.renderAheadFrames > 0 && resolved.backend->id() != "null";
    const int blockFrames = renderAhead_ ? std::max(1, config.blockSize) : std::max(1, info.blockFrames);
    // The run's state goes in FIRST: the renderer (below) and the pull (after start) read it, so it must be complete
    // before either thread exists.
    stream_      = std::move(stream);
    backend_     = resolved.backend;
    info_        = info;
    blockFrames_ = blockFrames;
    if (renderAhead_) {
        // The ring and the scratch exist before the device can pull, and the renderer runs before start(): a real
        // device's callback can run the moment start() returns, and a pull that found the previous run's counters would
        // read a ring the control thread was reallocating.
        ringFrames_ = static_cast<std::size_t>(std::max(config.renderAheadFrames, 2 * blockFrames));
        ring_.assign(ringFrames_ * static_cast<std::size_t>(std::max(1, info.channels)), 0.0f);
        renderScratch_.assign(
            static_cast<std::size_t>(blockFrames) * static_cast<std::size_t>(std::max(1, info.channels)), 0.0f);
    }
    // The engine is shaped by what the DEVICE came up as and by the block the RUN renders in, never by what was asked
    // for: a null fallback pulls in the block the section names, so the engine is sized for that. Before the renderer
    // starts, which is why the ring is sized above and the renderer below.
    client_->prepare(static_cast<double>(std::max(1, info_.sampleRate)), blockFrames, std::max(1, info_.channels));
    if (renderAhead_) startRenderer(blockFrames, info_.channels);  // and before the DEVICE can pull, ring sized
    std::string startError;
    if (!stream_->start(startError)) {
        // A stream that opens and never starts is an OPEN failure: nothing pulls, so the emulator would sit frozen and
        // the status would claim a device. The backend's own words go into the status, and the caller falls to null.
        error = startError.empty() ? resolved.backend->label() + ": the audio device would not start" : startError;
        closeCurrent();
        return false;
    }
    started_ = true;
    pulls_.store(0, std::memory_order_relaxed);
    lastPullMs_.store(nowMs(), std::memory_order_relaxed);  // a stream that starts and never pulls trips the watchdog
    setWatchdogFor(blockFrames);
    return true;
}

bool AudioHost::apply(const AudioConfig& config) {
    // No change and a REAL device running: none of the sequence runs (a Settings Apply with nothing edited is free).
    // A muted host never takes this path: the same request is how a user retries the device that refused.
    if (started_ && stream_ && sameChoice(config, config_) && backend_ && backend_->id() != "null") return live();

    closeCurrent();
    client_->release();
    config_ = config;

    Resolved    resolved;
    StreamInfo  info;
    std::string error;
    if (!openAndStart(config, resolved, info, error)) {
        // The null device, with the reason in the status: the emulator still advances, and Settings says why it is
        // silent. Today's muted line, with a reason the UI can show as well as the log.
        const std::string reason = error.empty() ? "no audio device" : error;
        logLine("audio: " + reason + " (muted)" + debugHint());
        if (!fallbackToNull(reason)) {
            error_   = reason;
            info_    = StreamInfo{};
            backend_ = nullptr;
            started_ = false;
            return false;
        }
        error_ = reason;
    } else {
        error_.clear();
    }

    // The line openAudio printed, backend neutral: the backend's label and the driver's LABEL, so PortAudio's is
    // byte-identical to today's.
    std::string line = "audio: " + backend_->label() + " [" + backend_->driverLabel(info_.driver) + "] '" +
                       info_.device + "' " + std::to_string(info_.sampleRate) + " Hz, " +
                       std::to_string(blockFrames_) + " frames, " + std::to_string(info_.channels) + " ch";
    if (renderAhead_) line += ", render-ahead ring " + std::to_string(ringFrames_) + " frames";
    logLine(line);
    return live();
}

void AudioHost::stop() {
    closeCurrent();
    client_->release();
}

bool AudioHost::poll() {
    if (!started_ || !stream_ || !backend_) return false;
    if (backend_->id() == "null") return false;  // the fallback is already the answer
    std::string reason;
    if (stream_->lost()) {
        reason = "the audio device went away";
    } else {
        // A backend that never reports still falls back: a pull that has not finished for a few block durations while
        // the stream is started is a stall, and today's app freezes on one (the emulator advances only inside a pull).
        // The clock starts at start(), so a stream that starts and NEVER pulls is caught too.
        const std::int64_t last = lastPullMs_.load(std::memory_order_relaxed);
        if (last != 0 && nowMs() - last > watchdogBound_.count()) reason = "the audio device stopped pulling";
    }
    if (reason.empty()) return false;

    const AudioStatus before = status();
    closeCurrent();
    client_->release();
    if (!fallbackToNull(reason)) {
        // Nothing to run on: the engine went back to the control plane, and the status says what happened. Recovery is
        // the user's apply (a device that is back shows up as one).
        error_   = reason;
        started_ = false;
    } else {
        logLine("audio: " + reason + " (muted)");
    }
    const AudioStatus after = status();
    return before.backend != after.backend || before.device != after.device || before.error != after.error;
}

bool AudioHost::fallbackToNull(const std::string& reason) {
    // The last resort every failure lands on: the sleep-paced null pump, which always opens and always starts. Its
    // request is the block the section asks for and NOT render-ahead: it renders inside the pull, so the engine is
    // shaped for exactly what it runs - rendering ahead of a pump that pulls in whole blocks would only make the engine
    // refuse blocks over its maximum.
    AudioConfig fallback   = config_;
    fallback.backend       = "null";
    fallback.driver.clear();
    fallback.device.clear();
    fallback.renderAheadFrames = 0;
    Resolved    resolved;
    StreamInfo  info;
    std::string error;
    if (!openAndStart(fallback, resolved, info, error)) {
        if (!error.empty()) error_ = error;
        return false;
    }
    // What the status row shows, and what a later apply clears by opening a real device. The muted line itself is the
    // caller's (it knows whether the failure was an open or a poll), so a muted run logs it once.
    if (!reason.empty()) error_ = reason;
    return true;
}

void AudioHost::closeCurrent() {
    stopRenderer();
    if (stream_) {
        stream_->stop();
        stream_.reset();
    }
    started_     = false;
    ringFrames_  = 0;  // the next run sizes its own, so a pull can never read this one's
    blockFrames_ = 0;
    pulls_.store(0, std::memory_order_relaxed);
    lastPullMs_.store(0, std::memory_order_relaxed);
}

void AudioHost::setWatchdogFor(int blockFrames) {
    // A few block durations, never under 250 ms: an 8192-frame quantum at 44.1 kHz is 186 ms between pulls, so a fixed
    // quarter of a second would trip on one block's own duration (and a WinMM SysEx inside the block adds its 170 ms).
    // The bound follows the LONGER of the block the run renders in and the DEVICE's own quantum: under render-ahead the
    // stream is opened with blockFrames 0, so the device pulls in its quantum, which can outlast the render block.
    const int pullFrames = std::max(blockFrames, info_.blockFrames);
    const double blockMs =
        info_.sampleRate > 0 ? 1000.0 * static_cast<double>(pullFrames) / info_.sampleRate : 0.0;
    watchdogBound_ = std::chrono::milliseconds(std::max<std::int64_t>(250, static_cast<std::int64_t>(4.0 * blockMs)));
}

void AudioHost::pull(float* interleaved, int frames, bool underflow) {
    // The audio path's first call on this thread: real-time priority and the audio core, once (per thread) - but only
    // on a REAL device's thread. The null device's pump is a sleep-paced emulator loop with nothing to keep ahead of,
    // and a test that pulls by hand would leave the control process's own thread (and every thread it later creates)
    // at SCHED_FIFO on the audio core.
    if (backend_ && backend_->id() != "null" && std::this_thread::get_id() != controlThread_)
        tuneAudioThread(renderAhead_ ? kCallbackPriorityAhead : kCallbackPriority);
    pulls_.fetch_add(1, std::memory_order_relaxed);
    if (frames <= 0) {
        lastPullMs_.store(nowMs(), std::memory_order_relaxed);
        return;
    }
    if (renderAhead_) {
        // Copy what the renderer has queued and pad a short ring with silence. The underrun is counted only once the
        // ring has primed: the first pulls of a fresh stream are meant to find it empty.
        const std::uint64_t read  = ringRead_.load(std::memory_order_relaxed);
        const std::uint64_t write = ringWritten_.load(std::memory_order_acquire);
        const std::uint64_t avail = write - read;
        const std::uint64_t n     = std::min<std::uint64_t>(avail, static_cast<std::uint64_t>(frames));
        const std::size_t   channels = static_cast<std::size_t>(info_.channels);
        for (std::uint64_t i = 0; i < n; ++i) {
            const std::size_t at = static_cast<std::size_t>((read + i) % ringFrames_) * channels;
            for (std::size_t c = 0; c < channels; ++c) interleaved[i * channels + c] = ring_[at + c];
        }
        if (n < static_cast<std::uint64_t>(frames)) {
            std::fill(interleaved + n * channels, interleaved + static_cast<std::size_t>(frames) * channels, 0.0f);
            if (ringPrimed_.load(std::memory_order_relaxed)) {
                underruns_.fetch_add(1, std::memory_order_relaxed);
                pendingUnderflows_.fetch_add(1, std::memory_order_relaxed);  // the renderer's count to report
            }
        }
        ringRead_.store(read + n, std::memory_order_release);
        lastPullMs_.store(nowMs(), std::memory_order_relaxed);  // at EXIT: the copy is the whole call
        return;
    }
    const auto   start = std::chrono::steady_clock::now();
    const double cpu0  = statsEnabled_ ? threadCpuSeconds() : 0.0;
    client_->render(interleaved, frames);
    // At EXIT: a render longer than the bound is one block's work, not a stall, so the stamp is what the pull FINISHED
    // at. The stats are this thread's alone here - under render-ahead only the renderer reports.
    lastPullMs_.store(nowMs(), std::memory_order_relaxed);
    if (statsEnabled_) {
        const double    renderSec = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        const double    blockSec  = info_.sampleRate > 0 ? static_cast<double>(frames) / info_.sampleRate : 0.0;
        if (underflow) pendingUnderflows_.fetch_add(1, std::memory_order_relaxed);
        reportStats(renderSec, threadCpuSeconds() - cpu0, blockSec, frames);
    }
}

void AudioHost::pullTrampoline(void* ctx, float* interleaved, int frames, bool underflow) {
    static_cast<AudioHost*>(ctx)->pull(interleaved, frames, underflow);
}

void AudioHost::startRenderer(int blockFrames, int channels) {
    ringWritten_.store(0, std::memory_order_relaxed);
    ringRead_.store(0, std::memory_order_relaxed);
    ringPrimed_.store(false, std::memory_order_relaxed);
    underruns_.store(0, std::memory_order_relaxed);
    pendingUnderflows_.store(0, std::memory_order_relaxed);
    renderRun_.store(true, std::memory_order_release);
    renderThread_ = std::thread([this, blockFrames, channels] { renderLoop(blockFrames, channels); });
}

void AudioHost::stopRenderer() {
    if (!renderThread_.joinable()) return;
    renderRun_.store(false, std::memory_order_release);
    renderThread_.join();
}

void AudioHost::renderLoop(int blockFrames, int channels) {
    // The render-ahead producer (ported from feature/handheld-perf's 4b9e39ea8): keep the ring topped up a block at a
    // time, below the device callback's own priority on the same core, so a block that overruns the server's quantum
    // is survivable. This thread alone reports the stats while it runs.
    tuneAudioThread(kRendererPriority);
    const std::size_t   frameSamples = static_cast<std::size_t>(channels);
    const std::uint64_t roomFor      = static_cast<std::uint64_t>(blockFrames);
    while (renderRun_.load(std::memory_order_acquire)) {
        const std::uint64_t written = ringWritten_.load(std::memory_order_relaxed);
        const std::uint64_t read    = ringRead_.load(std::memory_order_acquire);
        if (ringFrames_ - (written - read) < roomFor) {
            if (written - read >= ringFrames_ / 2) ringPrimed_.store(true, std::memory_order_relaxed);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        const auto   start = std::chrono::steady_clock::now();
        const double cpu0  = statsEnabled_ ? threadCpuSeconds() : 0.0;
        client_->render(renderScratch_.data(), blockFrames);
        for (int i = 0; i < blockFrames; ++i) {
            const std::size_t at = static_cast<std::size_t>((written + i) % ringFrames_) * frameSamples;
            for (std::size_t c = 0; c < frameSamples; ++c)
                ring_[at + c] = renderScratch_[static_cast<std::size_t>(i) * frameSamples + c];
        }
        ringWritten_.store(written + blockFrames, std::memory_order_release);
        if (statsEnabled_) {
            const double renderSec = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            const double blockSec  = info_.sampleRate > 0 ? static_cast<double>(blockFrames) / info_.sampleRate : 0.0;
            reportStats(renderSec, threadCpuSeconds() - cpu0, blockSec, blockFrames);
        }
    }
}

void AudioHost::setStatsForTest(bool enabled, std::chrono::milliseconds interval) {
    statsEnabled_  = enabled;
    statsInterval_ = interval;
    statsSince_    = std::chrono::steady_clock::now();
    statsBusySec_ = statsCpuSec_ = statsWallSec_ = statsWorst_ = 0;
    statsBlocks_ = statsUnderflows_ = statsFrames_ = 0;
}

void AudioHost::reportStats(double renderSec, double renderCpuSec, double blockSec, int blockFrames) {
    statsFrames_ += static_cast<std::uint64_t>(blockFrames);
    statsBusySec_ += renderSec;
    statsCpuSec_ += renderCpuSec;
    statsWallSec_ += blockSec;
    if (blockSec > 0) statsWorst_ = std::max(statsWorst_, renderSec / blockSec);
    ++statsBlocks_;
    // The pull's underruns arrive through an atomic, because under render-ahead the two threads must not share a plain
    // field: the renderer alone prints, and the callback only counts.
    statsUnderflows_ += pendingUnderflows_.exchange(0, std::memory_order_relaxed);
    if (statsWallSec_ < static_cast<double>(statsInterval_.count()) / 1000.0) return;
    const auto   now     = std::chrono::steady_clock::now();
    const double realSec = std::chrono::duration<double>(now - statsSince_).count();
    statsSince_          = now;
    statsTotalUnderflows_ += statsUnderflows_;
    char line[512];
    std::snprintf(line, sizeof(line),
                  "audio stats: load avg %.0f%% (cpu %.0f%%) max %.0f%% over %llu blocks of %llu, %.0f frames/s real, "
                  "underflows %llu (total %llu)",
                  statsWallSec_ > 0 ? 100.0 * statsBusySec_ / statsWallSec_ : 0.0,
                  statsWallSec_ > 0 ? 100.0 * statsCpuSec_ / statsWallSec_ : 0.0, 100.0 * statsWorst_,
                  static_cast<unsigned long long>(statsBlocks_),
                  static_cast<unsigned long long>(statsBlocks_ ? statsFrames_ / statsBlocks_ : 0),
                  realSec > 0 ? static_cast<double>(statsFrames_) / realSec : 0.0,
                  static_cast<unsigned long long>(statsUnderflows_),
                  static_cast<unsigned long long>(statsTotalUnderflows_));
    logLine(line);
    statsBusySec_ = statsCpuSec_ = statsWallSec_ = statsWorst_ = 0;
    statsBlocks_ = statsUnderflows_ = statsFrames_ = 0;
}

}  // namespace rp::audio
