// The plugin lifecycle's handoff policy (plugin/LifecycleHandoff.hpp): a buffer-size or sample-rate change that a host
// runs on a thread of its own must not touch the engine there.
//
// The format's callbacks are what the policy is driven with - deactivate(), the change, activate() - so every case
// calls them exactly as DPF's JACK host does: from a std::thread that is not the one the plugin was built on. The fake
// host records the thread of every engine call, which is the whole assertion: whatever happens must happen on the
// control thread, or not at all.
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
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

    void takeEngine() override { record("take"); }
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
