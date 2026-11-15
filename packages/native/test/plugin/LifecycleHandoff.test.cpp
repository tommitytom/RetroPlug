// The plugin lifecycle's handoff policy (plugin/LifecycleHandoff.hpp): a buffer-size or sample-rate change that a host
// runs on a thread of its own must not touch the engine there.
//
// The format's callbacks are what the policy is driven with - deactivate(), the change, activate() - so every case
// calls them exactly as DPF's JACK host does: from a std::thread that is not the one the plugin was built on. The fake
// host records the thread of every engine call, which is the whole assertion: whatever happens must happen on the
// control thread, or not at all.
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "LifecycleHandoff.hpp"

namespace {

using rp::plugin::HeldMidi;
using rp::plugin::LifecycleHandoff;

// The engine work the policy may ask for, recorded with the thread it ran on.
class FakeHost : public rp::plugin::HandoffHost {
public:
    struct Call {
        std::string    what;
        std::thread::id thread;
        double         value = 0.0;
    };

    // takeEngine, behind a gate a case can shut: the quiescent handoff holds the policy's mutex across it, so a case
    // that shuts the gate holds the control thread inside the engine work - which is what the audio thread must never
    // wait for. Open unless a case shut it.
    void takeEngine() override {
        record("take");
        std::unique_lock<std::mutex> lock(gateMutex_);
        inTake_ = true;
        gateCv_.notify_all();
        gateCv_.wait(lock, [this] { return !gateShut_; });
        inTake_ = false;
    }

    void shutGate() {
        std::lock_guard<std::mutex> guard(gateMutex_);
        gateShut_ = true;
    }

    void openGate() {
        std::lock_guard<std::mutex> guard(gateMutex_);
        gateShut_ = false;
        gateCv_.notify_all();
    }

    // True once a takeEngine call is inside the gate (or once the timeout has passed without one).
    bool waitInTake(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(gateMutex_);
        return gateCv_.wait_for(lock, timeout, [this] { return inTake_; });
    }
    std::size_t reclaimReleases() override { record("reclaim"); return 0; }
    void giveEngine() override { record("give"); }
    void setMaxBlockFrames(std::uint32_t frames) override { record("maxFrames", static_cast<double>(frames)); }
    void setSampleRate(double rate) override { record("rate", rate); }
    void updateControlPlane() override { record("control"); }
    void parkOverdue(double seconds) override { record("overdue", seconds); }
    void midiDropped(std::uint64_t count) override { record("midiDropped", static_cast<double>(count)); }

    std::vector<Call> calls() {
        std::lock_guard<std::mutex> guard(mutex_);
        return calls_;
    }

    void reset() {
        std::lock_guard<std::mutex> guard(mutex_);
        calls_.clear();
    }

    bool ranOn(std::thread::id thread) {
        for (const Call& c : calls())
            if (c.thread == thread) return true;
        return false;
    }

    std::vector<std::string> names() {
        std::vector<std::string> out;
        for (const Call& c : calls()) out.push_back(c.what);
        return out;
    }

private:
    void record(const char* what, double value = 0.0) {
        std::lock_guard<std::mutex> guard(mutex_);
        calls_.push_back({what, std::this_thread::get_id(), value});
    }

    std::vector<Call> calls_;
    std::mutex        mutex_;

    std::mutex              gateMutex_;
    std::condition_variable gateCv_;
    bool                    gateShut_ = false;
    bool                    inTake_   = false;
};

// Run `body` on a thread of its own, as JACK's notification thread is, and report the id it had.
std::thread::id onForeignThread(const std::function<void()>& body) {
    std::thread::id id{};
    std::thread thread([&] {
        id = std::this_thread::get_id();
        body();
    });
    thread.join();
    return id;
}

// One lifecycle call made on the AUDIO thread while the control thread's tick holds the policy's mutex inside the take:
// whether the call waited for that (it must not), and whether it ran engine work itself (it must not either). The
// thread's own fields are read after join().
class AudioCall {
public:
    AudioCall(FakeHost& host, std::function<void(LifecycleHandoff&)> call, LifecycleHandoff& handoff)
        : host_(host), handoff_(handoff), call_(std::move(call)), thread_([this] {
              id_     = std::this_thread::get_id();
              inTake_ = host_.waitInTake(std::chrono::seconds(5));  // only once the stopwatch's subject is held
              const auto start = std::chrono::steady_clock::now();
              call_(handoff_);
              waitedUs_ = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() -
                                                                               start)
                              .count();
              returned_ = true;
          }) {}

    ~AudioCall() { if (thread_.joinable()) thread_.join(); }
    void join() { if (thread_.joinable()) thread_.join(); }

    std::thread::id id() const { return id_; }
    bool            inTake() const { return inTake_; }
    bool            returned() const { return returned_; }
    long long       waitedUs() const { return waitedUs_; }

private:
    FakeHost&                              host_;
    LifecycleHandoff&                      handoff_;
    std::function<void(LifecycleHandoff&)> call_;
    std::thread::id                        id_{};
    bool                                   inTake_   = false;
    bool                                   returned_ = false;
    long long                              waitedUs_ = 0;
    std::thread                            thread_;
};

} // namespace

TEST_CASE("a foreign buffer-size change within the plan touches nothing at all", "[lifecycle]") {
    FakeHost host;
    LifecycleHandoff handoff(host, std::this_thread::get_id(), 8192);
    handoff.activate(1024);  // as DPF's first activate, on the thread it built the plugin on: the engine is given
    host.reset();
    const std::thread::id foreign = onForeignThread([&] {
        handoff.deactivate();
        handoff.bufferSizeChanged(2048);
        handoff.activate(2048);
    });
    REQUIRE(foreign != std::this_thread::get_id());
    // No ownership flip, no drain, no reclaim, no JS: the engine never left the audio thread, and the next block drains
    // the ring as usual.
    REQUIRE(host.calls().empty());
    REQUIRE_FALSE(handoff.parked());
    REQUIRE(handoff.beginBlock());
    REQUIRE(host.calls().empty());
}

TEST_CASE("a foreign rate change parks the audio thread, and the idle tick applies it there", "[lifecycle]") {
    FakeHost host;
    LifecycleHandoff handoff(host, std::this_thread::get_id(), 8192);
    handoff.activate(1024);  // as DPF's first activate, on the thread it built the plugin on: the engine is given
    host.reset();
    const std::thread::id foreign = onForeignThread([&] { handoff.sampleRateChanged(96000.0); });
    REQUIRE(foreign != std::this_thread::get_id());
    REQUIRE_FALSE(host.ranOn(foreign));
    REQUIRE(host.calls().empty());
    REQUIRE(handoff.parked());

    // The next block parks: it writes silence and holds its MIDI, and touches neither the engine nor the host.
    REQUIRE_FALSE(handoff.beginBlock());
    REQUIRE(handoff.state() == LifecycleHandoff::State::Parked);
    const std::uint8_t noteOff[3] = {0x80, 0x3C, 0x00};
    handoff.holdMidi(noteOff, 3);
    REQUIRE(handoff.held().count() == 1);
    REQUIRE(host.calls().empty());

    // The control thread's idle does the whole quiescent handoff, and nothing of it ran on JACK's thread.
    REQUIRE(handoff.idleTick() == 0);
    const std::vector<std::string> names = host.names();
    const std::vector<std::string> want = {"take", "reclaim", "rate", "give", "control"};
    REQUIRE(names == want);
    REQUIRE_FALSE(host.ranOn(foreign));
    const std::vector<FakeHost::Call> calls = host.calls();
    REQUIRE(calls[2].value == 96000.0);

    // The first resumed block runs the engine again, with the held message replayed at frame 0 first.
    REQUIRE_FALSE(handoff.parked());
    REQUIRE(handoff.beginBlock());
    std::vector<std::uint8_t> replayed;
    handoff.replayHeld([&](const std::uint8_t* bytes, std::uint32_t size) {
        replayed.insert(replayed.end(), bytes, bytes + size);
    });
    const std::vector<std::uint8_t> wantBytes = {0x80, 0x3C, 0x00};
    REQUIRE(replayed == wantBytes);
    REQUIRE(handoff.held().count() == 0);
}

TEST_CASE("a foreign size above the plan parks, and the tick grows the plan for it", "[lifecycle]") {
    FakeHost host;
    LifecycleHandoff handoff(host, std::this_thread::get_id(), 8192);
    handoff.activate(1024);  // as DPF's first activate, on the thread it built the plugin on: the engine is given
    host.reset();
    const std::thread::id foreign = onForeignThread([&] {
        handoff.deactivate();
        handoff.bufferSizeChanged(16384);
        handoff.activate(16384);
    });
    REQUIRE(host.calls().empty());
    REQUIRE(handoff.parked());
    REQUIRE_FALSE(handoff.beginBlock());
    REQUIRE(handoff.idleTick() == 0);
    const std::vector<std::string> names = host.names();
    const std::vector<std::string> want = {"take", "reclaim", "maxFrames", "give", "control"};
    REQUIRE(names == want);
    const std::vector<FakeHost::Call> calls = host.calls();
    REQUIRE(calls[2].value == 16384.0);
    REQUIRE_FALSE(host.ranOn(foreign));
    // The plan now holds a block that long, so the next one is a no-op again.
    onForeignThread([&] { handoff.bufferSizeChanged(16384); });
    REQUIRE_FALSE(handoff.parked());
    REQUIRE(handoff.beginBlock());
}

TEST_CASE("a control-thread deactivate leaves the engine control-owned, and a foreign activate parks until the tick "
          "gives it",
          "[lifecycle]") {
    FakeHost host;
    LifecycleHandoff handoff(host, std::this_thread::get_id(), 8192);
    handoff.activate(1024);  // as DPF's first activate, on the thread it built the plugin on
    REQUIRE(host.names() == std::vector<std::string>{"maxFrames", "give", "control"});
    REQUIRE(handoff.beginBlock());
    handoff.deactivate();  // and a control-thread deactivate, as CLAP, VST3 and AU do
    REQUIRE(host.names() == std::vector<std::string>{"maxFrames", "give", "control", "take", "reclaim"});
    // The engine is the CONTROL thread's again: an idle tick must not hand it to a thread nothing is running on, and
    // the next block must not drive it either.
    REQUIRE(handoff.idleTick() == 0);
    REQUIRE(host.names().size() == 5);
    REQUIRE_FALSE(handoff.beginBlock());

    // A host that activates on another thread (VST3's setProcessing on the processing thread, DPF's run() auto-activate
    // when fIsActive is false) gets a park: the give is the control thread's to make, from a quiescent moment.
    const std::thread::id foreign = onForeignThread([&] { handoff.activate(2048); });
    REQUIRE_FALSE(host.ranOn(foreign));
    REQUIRE(handoff.parked());
    REQUIRE_FALSE(handoff.beginBlock());  // the block parks, so the tick knows no block is in the engine
    REQUIRE(handoff.idleTick() == 0);
    const std::vector<std::string> want = {"maxFrames", "give", "control", "take", "reclaim", "give", "control"};
    REQUIRE(host.names() == want);
    REQUIRE_FALSE(host.ranOn(foreign));
    REQUIRE(handoff.beginBlock());
}

TEST_CASE("the very first activate on a foreign thread parks, and the tick gives it", "[lifecycle]") {
    FakeHost host;
    LifecycleHandoff handoff(host, std::this_thread::get_id(), 8192);
    // Nothing has given the engine to an audio thread yet, so this activate must not run the give, the latency report
    // or the CC map on JACK's thread: it asks for a park, and the tick gives from the control thread.
    const std::thread::id foreign = onForeignThread([&] { handoff.activate(1024); });
    REQUIRE_FALSE(host.ranOn(foreign));
    REQUIRE(host.calls().empty());
    REQUIRE(handoff.parked());
    REQUIRE_FALSE(handoff.beginBlock());
    REQUIRE(handoff.idleTick() == 0);
    REQUIRE(host.names() == std::vector<std::string>{"give", "control"});
    REQUIRE_FALSE(host.ranOn(foreign));
    REQUIRE(handoff.beginBlock());
}

TEST_CASE("a foreign deactivate nobody follows is taken back and kept; one that pairs costs nothing", "[lifecycle]") {
    SECTION("deactivate then activate, as VST2's SUSPEND(1) does: nothing moved") {
        FakeHost host;
        LifecycleHandoff handoff(host, std::this_thread::get_id(), 8192);
        handoff.activate(1024);  // as DPF's first activate, on the thread it built the plugin on: the engine is given
        host.reset();
        onForeignThread([&] {
            handoff.deactivate();
            handoff.activate(1024);
        });
        REQUIRE(host.calls().empty());
        REQUIRE_FALSE(handoff.parked());
    }
    SECTION("deactivate alone, as VST2's SUSPEND(0) does off the main thread") {
        FakeHost host;
        LifecycleHandoff handoff(host, std::this_thread::get_id(), 8192);
        handoff.activate(1024);  // as DPF's first activate, on the thread it built the plugin on: the engine is given
        host.reset();
        const std::thread::id foreign = onForeignThread([&] { handoff.deactivate(); });
        REQUIRE(host.calls().empty());
        REQUIRE(handoff.parked());
        // Not at once: the activate half of a format's pair can arrive a moment later, and cancels this for nothing.
        REQUIRE(handoff.idleTick() == 0);
        REQUIRE(host.calls().empty());
        std::this_thread::sleep_for(std::chrono::milliseconds(300));  // past the grace period
        // DPF promises no run() after a deactivate, so the tick may take the engine back without waiting for a block -
        // and KEEPS it: giving it to a thread that will not run it would fill the ring with nobody draining.
        REQUIRE(handoff.idleTick() == 0);
        REQUIRE(host.names() == std::vector<std::string>{"take", "reclaim"});
        REQUIRE_FALSE(host.ranOn(foreign));
        REQUIRE(handoff.parked());
        REQUIRE_FALSE(handoff.beginBlock());

        // The activate that follows asks for the give, and the tick makes it.
        onForeignThread([&] { handoff.activate(1024); });
        REQUIRE(host.names() == std::vector<std::string>{"take", "reclaim"});
        REQUIRE_FALSE(handoff.beginBlock());
        REQUIRE(handoff.idleTick() == 0);
        REQUIRE(host.names() == std::vector<std::string>{"take", "reclaim", "give", "control"});
        REQUIRE(handoff.beginBlock());
    }
}

TEST_CASE("the idle tick is the control thread's: off it, it applies nothing", "[lifecycle]") {
    FakeHost host;
    LifecycleHandoff handoff(host, std::this_thread::get_id(), 8192);
    std::size_t ticked = 1;
    // A host that calls getState off its main thread (VST2's effGetChunk can be) reaches the tick from there: the
    // reclaim, the handoff and the JS its report runs are all the control thread's, so it is a no-op here.
    handoff.activate(1024);  // as DPF's first activate, on the thread it built the plugin on: the engine is given
    host.reset();
    const std::thread::id foreign = onForeignThread([&] {
        handoff.sampleRateChanged(48000.0);
        ticked = handoff.idleTick();
    });
    REQUIRE(ticked == 0);
    REQUIRE(host.calls().empty());
    REQUIRE(handoff.parked());
    REQUIRE_FALSE(handoff.beginBlock());
    REQUIRE(handoff.idleTick() == 0);  // and where it belongs it does the whole handoff
    REQUIRE(host.names() == std::vector<std::string>{"take", "reclaim", "rate", "give", "control"});
    REQUIRE_FALSE(host.ranOn(foreign));
}

TEST_CASE("a message a parked block could not hold is counted, and said on the control thread", "[lifecycle]") {
    FakeHost host;
    LifecycleHandoff handoff(host, std::this_thread::get_id(), 8192);
    handoff.activate(1024);  // as DPF's first activate, on the thread it built the plugin on: the engine is given
    host.reset();
    onForeignThread([&] { handoff.sampleRateChanged(48000.0); });
    REQUIRE_FALSE(handoff.beginBlock());
    const std::uint8_t sysex[6] = {0xF0, 0x7E, 0x00, 0x06, 0x01, 0xF7};
    handoff.holdMidi(sysex, 6);  // longer than DPF's inline data, so not held: counted, and said where a log belongs
    REQUIRE(handoff.held().count() == 0);
    REQUIRE(handoff.idleTick() == 0);
    REQUIRE(host.names() == std::vector<std::string>{"take", "reclaim", "rate", "give", "midiDropped", "control"});
    REQUIRE(host.calls().back().value == 0.0);  // the control plane's report is the last call, the drop the one before
    REQUIRE(host.calls()[4].value == 1.0);
}

TEST_CASE("held MIDI is a fixed buffer: an over-long or over-full message is dropped and counted", "[lifecycle]") {
    HeldMidi held;
    const std::uint8_t sysex[6] = {0xF0, 0x7E, 0x00, 0x06, 0x01, 0xF7};
    REQUIRE_FALSE(held.push(sysex, 6));
    REQUIRE(held.dropped() == 1);
    const std::uint8_t note[3] = {0x90, 0x3C, 0x7F};
    for (int i = 0; i < HeldMidi::kCapacity; ++i) REQUIRE(held.push(note, 3));
    REQUIRE_FALSE(held.push(note, 3));
    REQUIRE(held.dropped() == 2);
    REQUIRE(held.count() == HeldMidi::kCapacity);
    held.clear();
    REQUIRE(held.count() == 0);
}

TEST_CASE("a control-thread change while a foreign deactivate stands waits for the take that applies it",
          "[lifecycle]") {
    // DPF calls bufferSizeChanged and sampleRateChanged on the thread they came from, with NO deactivate/activate
    // bracket, whenever the plugin is inactive (DistrhoPluginInternal.hpp's setBufferSize / setSampleRate). After a
    // FOREIGN deactivate the engine is still the audio thread's until a tick has waited the grace out, so a change
    // that arrives on the control thread in that window is a change to an engine it does not own: re-rating it there
    // would race a block, and GraphHost::reactivate refuses.
    SECTION("a rate change, as VST3's setupProcessing reaches it after setProcessing(false) on the processing thread") {
        FakeHost host;
        LifecycleHandoff handoff(host, std::this_thread::get_id(), 8192);
        handoff.activate(1024);  // as DPF's first activate, on the thread it built the plugin on: the engine is given
        host.reset();
        onForeignThread([&] { handoff.deactivate(); });
        REQUIRE(host.calls().empty());

        handoff.sampleRateChanged(96000.0);
        // Recorded, not applied: the engine is still the audio thread's, and the park is what buys the quiescent take.
        REQUIRE(host.calls().empty());
        REQUIRE(handoff.parked());

        // The activate that follows applies it under its own take, and the engine comes back at the new rate.
        handoff.activate(1024);
        REQUIRE(host.names() == std::vector<std::string>{"take", "reclaim", "maxFrames", "rate", "give", "control"});
        REQUIRE(host.calls()[3].value == 96000.0);
        REQUIRE(handoff.beginBlock());
    }
    SECTION("a size above the plan, which the activate's own frames do not repair") {
        FakeHost host;
        LifecycleHandoff handoff(host, std::this_thread::get_id(), 8192);
        handoff.activate(1024);
        host.reset();
        onForeignThread([&] { handoff.deactivate(); });
        handoff.bufferSizeChanged(16384);
        REQUIRE(host.calls().empty());
        REQUIRE(handoff.parked());
        handoff.activate(1024);
        REQUIRE(host.names() ==
                std::vector<std::string>{"take", "reclaim", "maxFrames", "maxFrames", "give", "control"});
        REQUIRE(host.calls()[3].value == 16384.0);
        REQUIRE(handoff.beginBlock());
    }
}

TEST_CASE("the state seams' tick applies a waiting park but never suspends the engine", "[lifecycle]") {
    FakeHost host;
    LifecycleHandoff handoff(host, std::this_thread::get_id(), 8192);
    handoff.activate(1024);  // as DPF's first activate, on the thread it built the plugin on: the engine is given
    host.reset();
    // A VST3 host stops processing on its processing thread (a FOREIGN deactivate) and then saves the project on its
    // main thread, more than the grace later. The save is a control-thread entry, but not a tick that keeps coming: a
    // suspend made here would be undone by nothing, because DPF reports a rate or a size only while inactive.
    onForeignThread([&] { handoff.deactivate(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    REQUIRE(handoff.stateTick() == 0);  // getState's own tick, which may apply a park and may not make one
    REQUIRE(host.calls().empty());

    // So the engine never moved, and the host's setProcessing(true) - a foreign activate - costs nothing and resumes.
    onForeignThread([&] { handoff.activate(1024); });
    REQUIRE(host.calls().empty());
    REQUIRE(handoff.beginBlock());
}

TEST_CASE("the state seams' tick applies a park a block has already seen", "[lifecycle]") {
    FakeHost host;
    LifecycleHandoff handoff(host, std::this_thread::get_id(), 8192);
    handoff.activate(1024);  // as DPF's first activate, on the thread it built the plugin on: the engine is given
    host.reset();
    onForeignThread([&] { handoff.sampleRateChanged(96000.0); });
    REQUIRE_FALSE(handoff.beginBlock());
    // A host with no idle tick at all: the save is the only control-thread entry there is, and it applies the park.
    REQUIRE(handoff.stateTick() == 0);
    REQUIRE(host.names() == std::vector<std::string>{"take", "reclaim", "rate", "give", "control"});
    REQUIRE(handoff.beginBlock());
}

TEST_CASE("a park no tick applies is said once, a second later, from whatever control-thread entry comes next",
          "[lifecycle]") {
    FakeHost host;
    LifecycleHandoff handoff(host, std::this_thread::get_id(), 8192);
    handoff.activate(1024);  // as DPF's first activate, on the thread it built the plugin on: the engine is given
    host.reset();
    onForeignThread([&] { handoff.sampleRateChanged(96000.0); });
    REQUIRE_FALSE(handoff.beginBlock());
    // A host that moved its main thread never ticks on the one that owns the engine, so the park is never applied: the
    // tick it does call says so, once, and only from a later entry.
    onForeignThread([&] { handoff.idleTick(); });
    REQUIRE(host.calls().empty());
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    onForeignThread([&] { handoff.idleTick(); });
    REQUIRE(host.names() == std::vector<std::string>{"overdue"});
    REQUIRE(host.calls().front().value >= 1.0);
    onForeignThread([&] { handoff.idleTick(); });
    REQUIRE(host.names().size() == 1);
}

TEST_CASE("a message a parked block dropped is said by the next tick, even one that only reclaims", "[lifecycle]") {
    FakeHost host;
    LifecycleHandoff handoff(host, std::this_thread::get_id(), 8192);
    handoff.activate(1024);  // as DPF's first activate, on the thread it built the plugin on: the engine is given
    host.reset();
    // A foreign deactivate's pair: a block parks inside the grace and drops a SysEx it cannot hold, and the pair's
    // activate resumes the engine with no handoff in between, so no handoff would ever say the drop.
    onForeignThread([&] { handoff.deactivate(); });
    REQUIRE_FALSE(handoff.beginBlock());
    const std::uint8_t sysex[6] = {0xF0, 0x7E, 0x00, 0x06, 0x01, 0xF7};
    handoff.holdMidi(sysex, 6);
    onForeignThread([&] { handoff.activate(1024); });
    REQUIRE(handoff.idleTick() == 0);
    REQUIRE(host.names() == std::vector<std::string>{"reclaim", "midiDropped"});
    REQUIRE(host.calls().back().value == 1.0);
}

TEST_CASE("a lifecycle call on the audio thread does not wait for the engine work the tick holds the mutex across",
          "[lifecycle]") {
    FakeHost host;
    LifecycleHandoff handoff(host, std::this_thread::get_id(), 8192);
    handoff.activate(1024);  // as DPF's first activate, on the thread it built the plugin on: the engine is given
    host.reset();
    onForeignThread([&] { handoff.sampleRateChanged(96000.0); });
    REQUIRE_FALSE(handoff.beginBlock());  // the park the tick may then take the engine for

    // The tick takes the engine with the gate shut, so the policy's mutex is held across the take until the releaser
    // opens it. This thread is the control thread, so it is the one that must block here: the audio thread must not.
    host.shutGate();
    std::thread releaser([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        host.openGate();
    });
    // DPF's run() auto-activate, or VST3's setProcessing(true) from the processing thread: the activate reaches the
    // policy on the AUDIO thread. Its frames are over the plan, so a request that is applied leaves a trace.
    AudioCall audio(host, [](LifecycleHandoff& policy) { policy.activate(16384); }, handoff);
    REQUIRE(handoff.idleTick() == 0);  // blocks in the take until the releaser opens the gate
    audio.join();
    releaser.join();
    REQUIRE(audio.inTake());
    REQUIRE(audio.returned());
    REQUIRE(audio.waitedUs() < 150000);
    REQUIRE_FALSE(host.ranOn(audio.id()));

    // And the request was not lost: the next tick applies the activate, which finds a size over the plan and parks
    // again, and the handoff after the block has seen that park grows the plan for it.
    REQUIRE(handoff.idleTick() == 0);
    REQUIRE(handoff.parked());
    REQUIRE_FALSE(handoff.beginBlock());
    REQUIRE(handoff.idleTick() == 0);
    const std::vector<std::string> want = {"take", "reclaim", "rate", "give", "control",
                                          "take", "reclaim", "maxFrames", "give", "control"};
    REQUIRE(host.names() == want);
    REQUIRE(host.calls()[7].value == 16384.0);  // the audio thread's own activate frames
    REQUIRE(handoff.beginBlock());
}

TEST_CASE("a deactivate the audio thread could not apply is applied by the tick, and starts its grace there",
          "[lifecycle]") {
    FakeHost host;
    LifecycleHandoff handoff(host, std::this_thread::get_id(), 8192);
    handoff.activate(1024);  // as DPF's first activate, on the thread it built the plugin on: the engine is given
    host.reset();
    // An AU reset on the render thread deactivates and activates again. This case drives the DEACTIVATE half of the
    // pair: the deactivate arrives while the control thread's tick is inside the take, so it may not wait for it (the
    // activate that follows, and the ORDER the two keep, is the next case).
    host.shutGate();
    std::thread releaser([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        host.openGate();
    });
    AudioCall audio(host, [](LifecycleHandoff& policy) { policy.deactivate(); }, handoff);
    // The tick has to be a handoff for the gate to hold it: a rate change parks the engine first.
    onForeignThread([&] { handoff.sampleRateChanged(96000.0); });
    REQUIRE_FALSE(handoff.beginBlock());
    REQUIRE(handoff.idleTick() == 0);
    audio.join();
    releaser.join();
    REQUIRE(audio.inTake());
    REQUIRE(audio.returned());
    REQUIRE(audio.waitedUs() < 150000);
    REQUIRE_FALSE(host.ranOn(audio.id()));
    // The request was not lost, and it keeps its own meaning: the tick that applies it suspends nothing yet, because
    // the grace a foreign deactivate gets starts when it is applied - the activate half of a pair may be on its way.
    REQUIRE(handoff.idleTick() == 0);
    REQUIRE(host.names() ==
            std::vector<std::string>{"take", "reclaim", "rate", "give", "control"});
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    REQUIRE(handoff.idleTick() == 0);
    REQUIRE(host.names().back() == "reclaim");
    REQUIRE(handoff.parked());
    REQUIRE_FALSE(handoff.beginBlock());
}

TEST_CASE("a deferred deactivate is applied before the call that got the lock, not after it", "[lifecycle]") {
    SECTION("the pair's activate a moment later gets the lock the deactivate missed") {
        FakeHost host;
        LifecycleHandoff handoff(host, std::this_thread::get_id(), 8192);
        handoff.activate(1024);  // as DPF's first activate, on the thread it built the plugin on: the engine is given
        // A park the tick may take the engine for, so the policy's mutex is held across engine work: the two halves of
        // a format's pair arrive around it, and the deactivate is the half that finds it held.
        onForeignThread([&] { handoff.sampleRateChanged(96000.0); });
        REQUIRE_FALSE(handoff.beginBlock());
        host.shutGate();
        std::thread releaser([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
            host.openGate();
        });
        AudioCall audio(host, [](LifecycleHandoff& policy) { policy.deactivate(); }, handoff);
        REQUIRE(handoff.idleTick() == 0);  // blocks inside the take until the releaser opens the gate
        audio.join();
        releaser.join();
        REQUIRE(audio.inTake());
        REQUIRE(audio.returned());
        REQUIRE(audio.waitedUs() < 150000);
        REQUIRE_FALSE(host.ranOn(audio.id()));
        host.reset();

        // The activate half gets the lock. The deferred deactivate is applied FIRST, so the pair nets out to nothing:
        // without that order the next tick replays the stale deactivate, the engine is suspended once the grace is
        // past, and no activate is coming to hand it back.
        const std::thread::id foreign = onForeignThread([&] { handoff.activate(1024); });
        REQUIRE(host.calls().empty());
        REQUIRE_FALSE(host.ranOn(foreign));
        REQUIRE(handoff.idleTick() == 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));  // past the grace a deactivate would get
        REQUIRE(handoff.idleTick() == 0);
        REQUIRE(host.names() == std::vector<std::string>{"reclaim", "reclaim"});  // a Running tick's own reclaim
        REQUIRE(handoff.state() == LifecycleHandoff::State::Running);
        REQUIRE(handoff.beginBlock());
    }
    SECTION("a control-thread rate change, then the activate, with the deferred deactivate in between") {
        FakeHost host;
        LifecycleHandoff handoff(host, std::this_thread::get_id(), 8192);
        handoff.activate(1024);
        // The same contended deactivate, held up by a size the tick must grow the plan for.
        onForeignThread([&] { handoff.bufferSizeChanged(16384); });
        REQUIRE_FALSE(handoff.beginBlock());
        host.shutGate();
        std::thread releaser([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
            host.openGate();
        });
        AudioCall audio(host, [](LifecycleHandoff& policy) { policy.deactivate(); }, handoff);
        REQUIRE(handoff.idleTick() == 0);
        audio.join();
        releaser.join();
        REQUIRE(audio.inTake());
        REQUIRE(audio.returned());
        REQUIRE(audio.waitedUs() < 150000);
        REQUIRE_FALSE(host.ranOn(audio.id()));
        host.reset();

        // VST3's setupProcessing on the main thread reports the rate while the plugin is inactive, and the activate
        // that follows applies it. The deferred deactivate still comes first, so the rate is applied by an activate
        // the plugin then stays Running after.
        handoff.sampleRateChanged(96000.0);
        REQUIRE(host.calls().empty());
        handoff.activate(1024);
        REQUIRE(host.names() == std::vector<std::string>{"take", "reclaim", "maxFrames", "rate", "give", "control"});
        REQUIRE(host.calls()[3].value == 96000.0);
        REQUIRE(handoff.idleTick() == 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        REQUIRE(handoff.idleTick() == 0);
        REQUIRE(host.names() ==
                std::vector<std::string>{"take", "reclaim", "maxFrames", "rate", "give", "control", "reclaim",
                                        "reclaim"});  // the activate's handoff, then two Running ticks
        REQUIRE(handoff.state() == LifecycleHandoff::State::Running);
        REQUIRE(handoff.beginBlock());
    }
}
