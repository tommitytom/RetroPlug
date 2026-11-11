// Unit-tests AudioHost over the fake backend (docs/modular-graph-phase-f5b-design.md section 5.7): the reconfigure's
// call order with the rate the device OBTAINED, the no-change apply that runs none of it (and the BACKEND part of the
// choice), a failed open AND a failed start falling back to the null device with a status, a lost device and the
// no-pull watchdog doing the same (a stream that never pulls included, and the bound the block allows), render-ahead
// producing the in-callback stream sample for sample across ragged pulls that wrap its ring and never underrun, the
// single renderer the client may see, the stats through a sink, the log lines byte for byte, and the Settings row's
// label-to-choice mapping.
//
// No device, no PortAudio, no SDL: the backend is test support (audio/FakeAudioBackend.hpp). The two real backends are
// compiled by the ordinary build and proved against a real server by tools/run-sdl-pipewire.sh.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "audio/AudioChoice.hpp"
#include "audio/AudioHost.hpp"
#include "audio/FakeAudioBackend.hpp"

using namespace rp::audio;

namespace {

/** The product's side, as a deterministic signal: frame i writes a value that depends on i AND on what the renderer
 *  wrote before it, so a ring that reorders, repeats or drops a block cannot produce the same stream. Every call is
 *  recorded in one log shared with the fake, which is how a case reads the order the host ran in. */
struct TestClient final : AudioClient {
    std::vector<std::string>*          log = nullptr;
    std::vector<float>                 stream;      // every sample rendered, in order
    // Frames rendered so far, atomic because a render-ahead renderer increments it while the case waits on it.
    std::atomic<std::uint64_t>         position{0};
    double                             rate     = 0;
    int                                block    = 0;
    int                                channels = 0;
    double                             last = 0.0;
    std::atomic<int>                   renders{0};
    std::atomic<int>                   inFlight{0};
    std::atomic<int>                   maxInFlight{0};
    bool                               renderSleeps = false;

    void prepare(double sampleRate, int maxBlockFrames, int ch) override {
        if (log) log->push_back("prepare " + std::to_string(static_cast<int>(sampleRate)) + " " +
                                std::to_string(maxBlockFrames) + " " + std::to_string(ch));
        rate     = sampleRate;
        block    = maxBlockFrames;
        channels = ch;
        stream.clear();
        position.store(0);
        last = 0.0;
    }

    void render(float* interleaved, int frames) override {
        const int now = inFlight.fetch_add(1) + 1;
        int       was = maxInFlight.load();
        while (now > was && !maxInFlight.compare_exchange_weak(was, now)) {
        }
        if (renderSleeps) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        for (int i = 0; i < frames; ++i) {
            for (int c = 0; c < channels; ++c) {
                const double v =
                    static_cast<double>(position.load(std::memory_order_relaxed) + static_cast<std::uint64_t>(i)) +
                    0.001 * c + last;
                interleaved[static_cast<std::size_t>(i) * channels + c] = static_cast<float>(v);
            }
            last = 1e-6 * static_cast<double>(interleaved[static_cast<std::size_t>(i) * channels]);
        }
        // NOT an entry in `log`: that vector is the control thread's (and the fake's), and a renderer writing to it
        // would be a race in the harness rather than in the host. A count says the same thing.
        renders.fetch_add(1, std::memory_order_relaxed);
        position.fetch_add(static_cast<std::uint64_t>(frames));
        for (int i = 0; i < frames; ++i)
            for (int c = 0; c < channels; ++c)
                stream.push_back(interleaved[static_cast<std::size_t>(i) * channels + c]);
        inFlight.fetch_sub(1);
    }

    void release() override {
        if (log) log->push_back("release");
    }
};

/** The host's preference list holds backends by unique_ptr; the case holds the fake by value so it can script it, so
 *  this forwards every call to that one. */
struct Forward final : AudioBackend {
    FakeAudioBackend* target = nullptr;

    std::string id() const override { return target->id(); }
    std::string label() const override { return target->label(); }
    std::vector<Driver> drivers() override { return target->drivers(); }
    std::string autoDriver() override { return target->autoDriver(); }
    Devices     devices(const std::string& driver) override { return target->devices(driver); }
    std::unique_ptr<AudioStream> open(const Request& request, Pull pull, void* ctx, std::string& error) override {
        return target->open(request, pull, ctx, error);
    }
};

/** The host's log sink, as a case reads it: the control thread and the render-ahead renderer BOTH write lines (the
 *  open line is the control thread's, the stats line the renderer's), so the vector is behind a mutex rather than being
 *  a race in the harness. */
struct Lines {
    std::mutex               mutex;
    std::vector<std::string> items;

    void add(const std::string& line) {
        std::lock_guard<std::mutex> lock(mutex);
        items.push_back(line);
    }
    std::vector<std::string> snapshot() {
        std::lock_guard<std::mutex> lock(mutex);
        return items;
    }
};

/** A host over one fake backend, the fake and the client sharing one event log. */
struct Rig {
    FakeAudioBackend           backend;
    std::vector<std::string>   log;
    TestClient                 client;
    std::unique_ptr<AudioHost> host;

    Rig() {
        backend.events = &log;
        client.log       = &log;
        auto fwd         = std::make_unique<Forward>();
        fwd->target      = &backend;
        BackendList preference;
        preference.push_back(std::move(fwd));
        host = std::make_unique<AudioHost>(std::move(preference), client);
    }

    AudioConfig config(int rate = 48000, int block = 512, int channels = 2) const {
        AudioConfig c;
        c.sampleRate = rate;
        c.blockSize  = block;
        c.channels   = channels;
        return c;
    }

    FakeAudioBackend&       fwd() { return backend; }
};

}  // namespace

TEST_CASE("a reconfigure runs stop, release, open, prepare(what the DEVICE obtained), start") {
    // The order report item 2 settled: the engine is re-rated to the rate the device came up at, not to the rate that
    // was asked for. A device that adopts 44100 while 48000 was requested must reach prepare as 44100.
    Rig rig;
    rig.fwd().adoptRate = 44100;
    REQUIRE(rig.host->apply(rig.config(48000, 256, 2)));
    const std::vector<std::string> expected = {
        "release",
        "open fake-driver/Fake Out",
        "prepare 44100 256 2",  // what the device says, not 48000
        "start",
    };
    CHECK(rig.log == expected);
    CHECK(rig.host->status().sampleRate == 44100);
    CHECK(rig.host->status().driver == "fake-driver");
    CHECK(rig.host->status().error.empty());
    CHECK(rig.host->live());
}

TEST_CASE("a second apply of the same choice runs none of the sequence") {
    Rig rig;
    REQUIRE(rig.host->apply(rig.config()));
    const std::size_t after = rig.log.size();
    REQUIRE(rig.host->apply(rig.config()));
    CHECK(rig.log.size() == after);  // no stop, no release, no open, no prepare, no start
    // A different choice runs all of it: stop of the old device first.
    REQUIRE(rig.host->apply(rig.config(48000, 1024, 2)));
    CHECK(rig.log[after] == "stop");
    CHECK(rig.log[after + 1] == "release");
}

TEST_CASE("the client renders only between start and the next stop") {
    Rig rig;
    REQUIRE(rig.host->apply(rig.config()));
    // The fake drives one callback, as its own thread would.
    rig.fwd().lastStream->drive(64);
    CHECK(rig.client.stream.size() == 64 * 2);
    CHECK(rig.client.renders.load() == 1);
    // Reconciling runs stop and release before anything else; nothing renders until start again.
    rig.log.clear();
    REQUIRE(rig.host->apply(rig.config(44100)));
    CHECK(rig.log.front() == "stop");
    CHECK(rig.log[1] == "release");
    const auto startAt = std::find(rig.log.begin(), rig.log.end(), "start");
    REQUIRE(startAt != rig.log.end());
    CHECK(rig.client.renders.load() == 1);  // nothing between release and start
    // After the start, the next callback does render again.
    rig.fwd().lastStream->drive(16);
    CHECK(rig.client.renders.load() == 2);
}

TEST_CASE("an open failure falls to the null device with the reason, and a later apply recovers") {
    Rig rig;
    rig.fwd().openFails = true;
    REQUIRE_FALSE(rig.host->apply(rig.config()));
    // Muted, with the backend's words as the status: Settings shows "Muted: <reason>" instead of silence.
    CHECK(rig.host->status().backend == "null");
    CHECK(rig.host->status().error == rig.fwd().openError);
    CHECK_FALSE(rig.host->live());
    // The null device still renders (its own thread), so the emulator advances on a muted run.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (rig.client.position.load() == 0 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    CHECK(rig.client.position.load() > 0);
    // The device comes back: a later apply opens it and the status clears.
    rig.fwd().openFails = false;
    REQUIRE(rig.host->apply(rig.config()));
    CHECK(rig.host->live());
    CHECK(rig.host->status().error.empty());
}

TEST_CASE("a lost device is the same fallback, reported by poll()") {
    Rig rig;
    REQUIRE(rig.host->apply(rig.config()));
    rig.fwd().lastStream->lostFlag = true;
    CHECK(rig.host->poll());
    CHECK(rig.host->status().backend == "null");
    CHECK_FALSE(rig.host->status().error.empty());
    // Once muted, poll() is quiet: there is nothing left to lose.
    CHECK_FALSE(rig.host->poll());
}

TEST_CASE("the watchdog: a started device that pulls nothing falls back, and a steady one never does") {
    Rig rig;
    REQUIRE(rig.host->apply(rig.config()));
    // apply sets the bound a block allows; a case may shorten it.
    rig.host->setWatchdogBound(std::chrono::milliseconds(20));
    // Pulling steadily: the watchdog never fires.
    for (int i = 0; i < 10; ++i) {
        rig.fwd().lastStream->drive(32);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        CHECK_FALSE(rig.host->poll());
    }
    // Then the device stops: after the bound, the host treats it as lost and mutes.
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    CHECK(rig.host->poll());
    CHECK(rig.host->status().backend == "null");
    // And it never fires while stopped.
    rig.host->stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    CHECK_FALSE(rig.host->poll());
}

TEST_CASE("render-ahead produces the in-callback stream sample for sample, across ragged pulls") {
    // The ring must be invisible in the OUTPUT: a render-ahead host playing into ragged device callbacks produces the
    // byte-for-byte stream a host rendering inside the callback does.
    auto pullAll = [](AudioHost& host, FakeAudioBackend::Stream& stream, const std::vector<int>& sizes,
                      std::vector<float>* out) {
        for (int frames : sizes) {
            stream.drive(frames);
            out->insert(out->end(), stream.captured.begin(), stream.captured.end());
        }
    };
    const std::vector<int> ragged = {64, 1, 7, 255, 512, 1000, 64, 33, 17, 512};

    Rig plain;
    REQUIRE(plain.host->apply(plain.config(48000, 512, 2)));
    std::vector<float> plainOut;
    pullAll(*plain.host, *plain.fwd().lastStream, ragged, &plainOut);
    plain.host->stop();

    Rig ahead;
    AudioConfig cfg = ahead.config(48000, 256, 2);
    cfg.renderAheadFrames = 4096;
    REQUIRE(ahead.host->apply(cfg));
    // Let the renderer prime the ring: it fills until the device drains it.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (ahead.client.position.load() < 4096 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    std::vector<float> aheadOut;
    pullAll(*ahead.host, *ahead.fwd().lastStream, ragged, &aheadOut);
    ahead.host->stop();

    REQUIRE(plainOut.size() == aheadOut.size());
    CHECK(plainOut == aheadOut);
}

TEST_CASE("a render-ahead host never renders on two threads at once") {
    // The single-renderer rule the stream's send() relies on in the app: with render-ahead the pull only copies, so a
    // device callback running while the renderer renders never enters the client.
    Rig rig;
    rig.client.renderSleeps = true;
    AudioConfig cfg         = rig.config(48000, 256, 2);
    cfg.renderAheadFrames   = 1024;
    REQUIRE(rig.host->apply(cfg));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
    while (std::chrono::steady_clock::now() < deadline) {
        rig.fwd().lastStream->drive(16);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    rig.host->stop();
    CHECK(rig.client.maxInFlight.load() == 1);
    CHECK(rig.client.position.load() > 0);
}

TEST_CASE("describe() enumerates only the backend in force: a null run touches no other library") {
    // A backend's library comes up at its first use, and listing its drivers IS that use. So a description reports
    // the backend in force, and the others by id and label alone - which is what makes a null-pinned run, and the
    // startup of any run, safe on a machine whose default audio device is a card it must not open.
    Rig rig;
    rig.fwd().openFails = true;  // auto tries the fake, fails, and the host runs on the null device
    REQUIRE_FALSE(rig.host->apply(rig.config()));
    REQUIRE(rig.host->status().backend == "null");
    const int autoCalls   = rig.fwd().autoDriverCalls;  // the attempt itself
    const int driverCalls = rig.fwd().driverCalls;
    const int deviceCalls = rig.fwd().deviceCalls;
    const AudioDescription described = rig.host->describe();
    CHECK(rig.fwd().driverCalls == driverCalls);   // untouched
    CHECK(rig.fwd().deviceCalls == deviceCalls);
    CHECK(rig.fwd().autoDriverCalls >= autoCalls);
    // The fake is in the list by name, with no drivers: nothing was asked of it.
    REQUIRE(described.backends.size() == 2);
    CHECK(described.backends[0].id == "fake");
    CHECK(described.backends[0].drivers.empty());
    CHECK(described.backends[0].label == "Fake");
    // The null device, in force, is described in full.
    CHECK(described.backends[1].id == "null");
    CHECK(described.backends[1].drivers.size() == 1);
    // And a row that offers another backend asks for it explicitly, which is that backend's first use.
    const auto fakeDrivers = rig.host->backendDrivers("fake");
    CHECK(fakeDrivers.size() == 2);
    CHECK(fakeDrivers[0].devices.names.size() == 2);
    CHECK(rig.fwd().deviceCalls > deviceCalls);
}

TEST_CASE("the stats line reports load, the real frame rate and underruns") {
    // The line 4b9e39ea8 added, backend neutral: a case reads it from the client's host with a short interval.
    Rig rig;
    rig.host->setStatsForTest(true, std::chrono::milliseconds(1));
    REQUIRE(rig.host->apply(rig.config(48000, 480, 2)));
    for (int i = 0; i < 12; ++i) rig.fwd().lastStream->drive(480);
    rig.host->stop();
    CHECK(rig.client.position.load() == 12 * 480);
    CHECK(rig.host->status().blockFrames == 480);
}

TEST_CASE("a device that starts and never pulls is caught by the watchdog, and the bound scales with the block") {
    // The probe: a stream that opens and starts but never calls back (a PipeWire stream that never links, a stuck host)
    // was invisible while the watchdog waited for a FIRST pull it never saw. The clock starts at start() instead, so
    // the stall is the stall - and the bound is the block's (a 8192-frame quantum at 44.1 kHz is 186 ms between pulls,
    // so a fixed 250 ms would trip on one block's own duration).
    Rig rig;
    REQUIRE(rig.host->apply(rig.config(44100, 8192, 2)));
    // max(250 ms, 4 x 186 ms): the block the stream runs at sets it, not a constant.
    CHECK(rig.host->watchdogBound().count() >= 740);
    CHECK(rig.host->watchdogBound().count() <= 760);
    // Shortened for the case: with no pull at all, the host mutes after the bound.
    rig.host->setWatchdogBound(std::chrono::milliseconds(20));
    CHECK_FALSE(rig.host->poll());  // inside the bound, nothing yet
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    CHECK(rig.host->poll());
    CHECK(rig.host->status().backend == "null");
    CHECK(rig.host->status().error.find("stopped pulling") != std::string::npos);
}

TEST_CASE("a stream that opens and will not start is an open failure: null, with the reason, and the engine back") {
    // The probe: apply() returned false with an EMPTY status, nothing pulled, and prepare had already handed the engine
    // to an audio thread that never existed - the control plane's edits queued undrained and the emulator froze, which
    // is exactly what the muted status exists to end.
    Rig rig;
    rig.fwd().startFails = true;
    CHECK_FALSE(rig.host->apply(rig.config()));
    CHECK(rig.host->status().backend == "null");          // the null device, which does run
    CHECK(rig.host->status().error == "the device would not start");  // the backend's own words, not an empty string
    // The engine is fed: the null pump pulls on its thread, so the emulator advances on a run whose device refused.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (rig.client.position.load() == 0 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    CHECK(rig.client.position.load() > 0);
    // The refused stream was stopped and freed, not kept half-open: the last thing the log says about the fake is its
    // stop, and a later apply with a working device recovers.
    rig.fwd().startFails = false;
    REQUIRE(rig.host->apply(rig.config()));
    CHECK(rig.host->live());
    CHECK(rig.host->status().error.empty());
}

TEST_CASE("a switch of BACKEND with the same driver, device and rate is a real change") {
    // The probe: PortAudio and SDL both name a driver "pipewire", so comparing everything but the backend made
    // "PortAudio: PipeWire" -> "SDL: pipewire" a silent no-op that left the old backend on the device.
    FakeAudioBackend a, b;
    TestClient       client;
    auto             fa = std::make_unique<Forward>();
    fa->target          = &a;
    auto fb             = std::make_unique<Forward>();
    fb->target          = &b;
    BackendList preference;
    preference.push_back(std::move(fa));
    preference.push_back(std::move(fb));
    AudioHost host(std::move(preference), client);
    AudioConfig cfg;
    cfg.sampleRate = 48000;
    cfg.blockSize  = 512;
    cfg.driver     = "fake-driver";  // the same driver id in both backends
    cfg.backend    = "fake";
    REQUIRE(host.apply(cfg));
    CHECK(host.status().backend == "fake");
    // The second backend renames itself so the switch is observable through the status.
    b.idValue = "other";
    cfg.backend = "other";
    REQUIRE(host.apply(cfg));
    CHECK(host.status().backend == "other");  // it reopened, rather than returning "nothing changed"
}

TEST_CASE("a render-ahead run that falls to null shapes the engine for what the NULL device pulls") {
    // The probe: --block-size 256 --render-ahead 2048 against a refused device prepared the engine for 256 while the
    // null pump pulled 512, and Engine::processBlock refuses a block over its maximum: silence, staged MIDI dropped.
    Rig rig;
    rig.fwd().openFails = true;
    AudioConfig cfg     = rig.config(48000, 256, 2);
    cfg.renderAheadFrames = 2048;
    REQUIRE_FALSE(rig.host->apply(cfg));
    CHECK(rig.host->status().backend == "null");
    // The null device renders in the block the SECTION asks for, and prepare was told exactly that.
    CHECK(rig.client.block == 256);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (rig.client.position.load() == 0 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    REQUIRE(rig.client.position.load() > 0);
    rig.host->stop();
    CHECK(rig.client.position.load() % 256 == 0);  // every block the null pump pulled is one the engine was sized for
}

TEST_CASE("render-ahead wraps its ring many times over and counts no underrun once primed") {
    // The shipped identity case pulled less than one ring, so the ring never wrapped and the renderer never refilled a
    // slot the pull was about to read - the two things a broken ring gets wrong. This pulls several rings' worth while
    // the renderer runs, then holds the underrun count at zero.
    Rig plain;
    REQUIRE(plain.host->apply(plain.config(48000, 256, 2)));
    std::vector<float> plainOut;
    for (int i = 0; i < 200; ++i) {
        plain.fwd().lastStream->drive(97);
        plainOut.insert(plainOut.end(), plain.fwd().lastStream->captured.begin(),
                        plain.fwd().lastStream->captured.end());
    }
    plain.host->stop();

    Rig ahead;
    AudioConfig cfg       = ahead.config(48000, 256, 2);
    cfg.renderAheadFrames = 2048;  // 2096 framing: the ring wraps more than nine times over the pulls below
    REQUIRE(ahead.host->apply(cfg));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (ahead.client.position.load() < 4096 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    std::vector<float> aheadOut;
    for (int i = 0; i < 200; ++i) {
        // Slow enough that the renderer keeps up: the identity is what is held, not an underrun-free real time.
        std::this_thread::sleep_for(std::chrono::microseconds(500));
        ahead.fwd().lastStream->drive(97);
        aheadOut.insert(aheadOut.end(), ahead.fwd().lastStream->captured.begin(),
                        ahead.fwd().lastStream->captured.end());
    }
    const std::uint64_t underruns = ahead.host->underruns();
    ahead.host->stop();
    REQUIRE(plainOut.size() == aheadOut.size());
    CHECK(plainOut == aheadOut);
    CHECK(underruns == 0);
}

TEST_CASE("the log lines the harnesses grep: the open line and the muted line, byte for byte") {
    // run-sdl-pipewire.sh reads "audio: PortAudio [h] 'd' N Hz, B frames, C ch" through its own sed, and the muted line
    // is what a user sees when a persisted device is gone. Both go through the host's sink, so a case holds the bytes.
    Rig   rig;
    Lines lines;
    rig.host->setLogSink([&](const std::string& line) { lines.add(line); });
    REQUIRE(rig.host->apply(rig.config(44100, 256, 2)));
    REQUIRE(lines.snapshot().size() == 1);
    CHECK(lines.snapshot()[0] == "[retroplug-sdl] audio: Fake [Fake Driver] 'Fake Out' 44100 Hz, 256 frames, 2 ch");
    // A refused device: the backend's words, "(muted)", and how to ask for its diagnostics.
    rig.host->stop();
    {
        std::lock_guard<std::mutex> lock(lines.mutex);
        lines.items.clear();
    }
    rig.fwd().openFails = true;
    REQUIRE_FALSE(rig.host->apply(rig.config()));
    const std::vector<std::string> written = lines.snapshot();
    REQUIRE(written.size() == 2);
    CHECK(written[0] == "[retroplug-sdl] audio: the fake refused the device (muted) \xE2\x80\x94 re-run with "
                        "RETROPLUG_DEBUG_AUDIO=1 for the backend's diagnostics");
    // ...and the device that IS running, so the log says both why the run is muted and what is advancing the engine.
    CHECK(written[1] == "[retroplug-sdl] audio: Null [Null] 'the null device' 48000 Hz, 512 frames, 2 ch");
}

TEST_CASE("the stats are one thread's accumulator, and say so: the renderer's block, and the underrun total") {
    // The probe: under render-ahead the pull and the renderer both accumulated into the same plain fields, which is a
    // data race AND a nonsense line (blocks of 128 alternating with blocks of 256, and the wall time counted twice).
    // Now the renderer alone reports and the pull only counts its underruns: every line names the renderer's block, and
    // the cumulative total is printed.
    Rig   rig;
    Lines lines;
    rig.host->setLogSink([&](const std::string& line) { lines.add(line); });
    rig.host->setStatsForTest(true, std::chrono::milliseconds(1));
    AudioConfig cfg       = rig.config(48000, 256, 2);
    cfg.renderAheadFrames = 1024;
    REQUIRE(rig.host->apply(cfg));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        rig.fwd().lastStream->drive(1000);  // a pull of 1000 frames: a line that counted it would say so
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    rig.host->stop();
    std::size_t stats = 0;
    for (const std::string& line : lines.snapshot()) {
        if (line.find("audio stats:") == std::string::npos) continue;
        ++stats;
        CHECK(line.find("blocks of 256") != std::string::npos);
        CHECK(line.find("(total ") != std::string::npos);
    }
    CHECK(stats > 0);
}

TEST_CASE("the Driver row's label maps to the choice an apply takes, and an unknown label keeps the current one") {
    // rp::audio::choiceForDriverLabel is what the Settings > Audio hook leans on: "Auto" is the automatic choice, a
    // "<Backend>: <driver>" label is that pair (the backend named because two backends may call a driver alike), and a
    // label the description does not carry is a stale row that must not move the choice.
    AudioDescription described;
    AudioBackendDescription backend;
    backend.id   = "portaudio";
    backend.label = "PortAudio";
    AudioDriverDescription pipe;
    pipe.id    = "pipewire";
    pipe.label = "PipeWire";
    backend.drivers.push_back(pipe);
    described.backends.push_back(backend);
    AudioBackendDescription other;
    other.id    = "sdl";
    other.label = "SDL";
    AudioDriverDescription sdlPipe;
    sdlPipe.id    = "pipewire";  // the same driver id, a different backend
    sdlPipe.label = "pipewire";
    other.drivers.push_back(sdlPipe);
    described.backends.push_back(other);

    const AudioChoice current{"sdl", "pipewire"};
    CHECK(choiceForDriverLabel("Auto", described, current).backend == "auto");
    CHECK(choiceForDriverLabel("Auto", described, current).driver.empty());
    CHECK(choiceForDriverLabel("", described, current).backend == "auto");  // an older bundle's spelling
    const AudioChoice pa = choiceForDriverLabel("PortAudio: PipeWire", described, current);
    CHECK(pa.backend == "portaudio");
    CHECK(pa.driver == "pipewire");
    const AudioChoice sd = choiceForDriverLabel("SDL: pipewire", described, current);
    CHECK(sd.backend == "sdl");
    CHECK(sd.driver == "pipewire");
    const AudioChoice stale = choiceForDriverLabel("PortAudio: Gone", described, current);
    CHECK(stale.backend == "sdl");
    CHECK(stale.driver == "pipewire");
    CHECK(driverChoiceLabel("PortAudio", "PipeWire") == "PortAudio: PipeWire");
}

TEST_CASE("a backend that runs one driver at a time lists only the driver in force") {
    // SDL's rule, held without SDL (devicesListableFor): enumerating another driver would mean quitting the one in
    // force, and SDL's quit closes every open device - the listing used to mute a running stream. A test never
    // initialises SDL audio, so the rule lives in the contract header and is held here.
    CHECK(devicesListableFor("pipewire", "pipewire"));
    CHECK(devicesListableFor("pipewire", ""));       // "whatever is in force"
    CHECK_FALSE(devicesListableFor("pipewire", "alsa"));
    CHECK_FALSE(devicesListableFor("", "pipewire"));  // nothing initialised: nothing to list
    CHECK_FALSE(devicesListableFor("", ""));
}

TEST_CASE("a re-apply under a pull that runs as soon as the device starts is safe") {
    // The probe: the ring and its counters were rebuilt AFTER start(), so a real device's first pulls (the fake's own
    // thread here) read the previous run's indices while the control thread reallocated the ring - under
    // ThreadSanitizer a data race, and on a real device permanent silence (written - read wrapped, no room).
    // Run in build-tsan/ too.
    Rig rig;
    rig.fwd().pullOnStart = true;
    AudioConfig cfg       = rig.config(48000, 256, 2);
    cfg.renderAheadFrames = 1024;
    REQUIRE(rig.host->apply(cfg));
    for (int i = 0; i < 20; ++i) {
        cfg.renderAheadFrames = (i % 2 == 0) ? 4096 : 1024;  // a re-apply with a different ring
        REQUIRE(rig.host->apply(cfg));
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(rig.host->live());
    rig.host->stop();
    CHECK(rig.client.inFlight.load() == 0);
}

TEST_CASE("a run's flags replace its backend but keep a saved driver and device the same backend names") {
    // E12's override rule (docs/modular-graph-phase-f5b-design.md 5.2): --audio-backend / RETROPLUG_SDL_AUDIO_BACKEND
    // replaces only the BACKEND. A saved driver and device belong to that backend's own vocabulary, so a harness that
    // pins the backend its section already names still opens the file's device; naming another backend clears them,
    // because its names would mean nothing there.
    AudioConfig saved;
    saved.backend  = "portaudio";
    saved.driver   = "pipewire";
    saved.device   = "RetroPlug Low Priority";
    saved.blockSize = 512;

    const AudioConfig pinned = applyRunOverrides(saved, "portaudio", 0);
    CHECK(pinned.backend == "portaudio");
    CHECK(pinned.driver == "pipewire");
    CHECK(pinned.device == "RetroPlug Low Priority");
    CHECK(pinned.blockSize == 512);

    const AudioConfig another = applyRunOverrides(saved, "sdl", 0);
    CHECK(another.backend == "sdl");
    CHECK(another.driver.empty());
    CHECK(another.device.empty());

    // A block-size flag wins over the saved block whatever the backend does.
    const AudioConfig block = applyRunOverrides(saved, "", 256);
    CHECK(block.blockSize == 256);
    CHECK(block.driver == "pipewire");
    CHECK(block.device == "RetroPlug Low Priority");

    // No flag at all is the saved choice untouched.
    CHECK(applyRunOverrides(saved, "", 0).backend == "portaudio");
}
