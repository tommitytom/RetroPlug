// Unit-tests PortWriter, one hardware output and the thread that writes to it (docs/known-bugs-batch-3-design.md
// section 6), over FakeMidiBackend: a send that blocks in the port does not block the caller, a slow port holds nothing
// else, a full ring drops whole messages and counts them, a stop flushes what is queued before the port goes and
// abandons the rest at the deadline, a push allocates nothing, and the Inline mode is the old write on the caller's
// thread. No MIDI system and no hardware: the backend is the fake in midi/FakeMidiBackend.hpp, and the ports are the
// fake's own.
//
// The ring and the flush are what ThreadSanitizer is for: build this target in build-tsan/ too (tools/run-sanitizer.sh's
// midi leg), as MidiStream.test.cpp says of its own swap and close cases.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <vector>

#include "AllocationCounter.hpp"  // the global operator new this file defines, and the count MidiStream.test.cpp reads
#include "midi/FakeMidiBackend.hpp"
#include "midi/PortWriter.hpp"

using rp::midi::FakeMidiBackend;
using rp::midi::OutputMode;
using rp::midi::PortWriter;

namespace {

using Bytes = std::vector<std::uint8_t>;
using Clock = std::chrono::steady_clock;

std::vector<std::uint8_t> bytes(std::size_t n, std::uint8_t first = 0x90) {
    std::vector<std::uint8_t> out(n);
    for (std::size_t i = 0; i < n; ++i) out[i] = static_cast<std::uint8_t>(first + i);
    return out;
}

/** Wait until `done()` or the deadline passes; false on the deadline. A case never sleeps a fixed time for something a
 *  thread does, so a slow runner costs a longer wait rather than a flake. */
bool eventually(const std::function<bool()>& done, std::chrono::milliseconds within = std::chrono::milliseconds(5000)) {
    const auto deadline = Clock::now() + within;
    while (Clock::now() < deadline) {
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return done();
}

/** A backend with two output ports, and the writer a case opens over the first. */
struct Rig {
    FakeMidiBackend backend;
    Rig() { backend.outputs = {"Synth out", "Interface out"}; }
    std::unique_ptr<PortWriter> open(const PortWriter::Options& options = {}) {
        return std::make_unique<PortWriter>(backend.openOutput("Synth out"), "Synth out", options,
                                            [](const std::string&) {});
    }
};

}  // namespace

// ---- the caller does not wait ----------------------------------------------------------------------------------

TEST_CASE("a threaded push returns at once while the port takes 200 ms a message, and the bytes arrive whole, in order") {
    Rig     rig;
    auto&   backend     = rig.backend;
    backend.sendDelayMs = 200;
    auto writer         = rig.open();
    // A control surface's repaint: 538 bytes, the size the WinMM SysEx path blocks on for about 170 ms at DIN rate.
    const Bytes sysex = bytes(538, 0xF0);
    const Bytes note  = {0x90, 0x3C, 0x7F};

    const auto t0 = Clock::now();
    REQUIRE(writer->push(sysex.data(), sysex.size()));
    REQUIRE(writer->push(note.data(), note.size()));
    REQUIRE(writer->push(note.data(), note.size()));
    REQUIRE(writer->push(note.data(), note.size()));
    const auto elapsed = Clock::now() - t0;
    // Three 200 ms writes are queued behind the first: the caller waited for none of them. The bound is loose on
    // purpose (a macOS or Windows CI runner stalls a thread for tens of milliseconds), and the count says the property
    // outright: not all four writes had finished when the pushes returned.
    CHECK(elapsed < std::chrono::milliseconds(150));
    CHECK(backend.sendsFinished.load() < 4);
    REQUIRE(eventually([&] { return backend.sendsFinished.load() == 4; }));
    // Every byte, in the order it was pushed, each message whole.
    const std::vector<Bytes> sent = backend.sentTo("Synth out");
    REQUIRE(sent.size() == 4);
    CHECK(sent[0] == sysex);
    CHECK(sent[1] == note);
    CHECK(sent[2] == note);
    CHECK(sent[3] == note);
    CHECK(writer->dropped() == 0);
    writer->stop();
}

TEST_CASE("a slow port does not delay a fast one: each output has its own writer") {
    Rig   rig;
    auto& backend = rig.backend;
    // One port answers slowly (a device that has to wake), the other at once: the delay is the fake's per-send hook, so
    // it holds only the writer whose port it is.
    backend.onSend = [](FakeMidiBackend&, const std::string& name, const std::uint8_t*, std::size_t) {
        if (name == "Synth out") std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    };
    // MidiStream picks the ports it opens; here two writers are built directly, which is what the stream does per
    // output, so the isolation is this class's and not the stream's.
    auto slow = std::make_unique<PortWriter>(backend.openOutput("Synth out"), "Synth out", PortWriter::Options{},
                                             [](const std::string&) {});
    auto fast = std::make_unique<PortWriter>(backend.openOutput("Interface out"), "Interface out", PortWriter::Options{},
                                             [](const std::string&) {});
    const Bytes note = {0x90, 0x3C, 0x7F};
    const auto  t0   = Clock::now();
    REQUIRE(slow->push(note.data(), note.size()));
    REQUIRE(fast->push(note.data(), note.size()));
    // The fast port's write FINISHED while the slow one is still inside its own (one send of the two has completed).
    REQUIRE(eventually(
        [&] { return !backend.sentTo("Interface out").empty() && backend.sendsFinished.load() < 2; },
        std::chrono::milliseconds(800)));
    CHECK(Clock::now() - t0 < std::chrono::milliseconds(800));
    slow->stop();
    fast->stop();
    CHECK(backend.sentTo("Synth out").size() == 1);
}

// ---- the idle wake --------------------------------------------------------------------------------------------

TEST_CASE("a push that lands as the writer goes idle is not lost: 50k single messages, one at a time") {
    Rig         rig;
    auto&       backend = rig.backend;
    auto        writer  = rig.open();
    const Bytes note    = {0x90, 0x3C, 0x7F};
    // Every round waits for the PREVIOUS send to finish and then pushes, so the push lands as the writer leaves
    // sendOne() for the idle path - the window between its empty-ring check and its sleep that a counter snapshot taken
    // AFTER the check can miss. Nothing calls stop() in between: a stop's own notify would wake the writer and hide the
    // lost one, and a lost wake is exactly the Note Off a burst ends with, held until the next message arrives.
    //
    // It is a probabilistic guard, which is what a soak is: against the pre-fix wait (a spinning producer, a woken
    // writer that had not reached the kernel yet) it failed about one run in six on the development machine without
    // TSan, about once in 300k rounds, and found the lost wake before the wait moved onto the flag word. So CI holds a
    // reintroduction only with that kind of chance per run (more on a small, oversubscribed runner, whose preemption
    // is the window). Under `tools/run-sanitizer.sh thread` the window is wider still, so that leg runs it too.
    constexpr int kRounds = 50000;
    int           lost    = -1;
    int           refused = -1;
    for (int i = 1; i <= kRounds && lost < 0 && refused < 0; ++i) {
        while (backend.sendsFinished.load(std::memory_order_acquire) < i - 1) std::this_thread::yield();
        if (!writer->push(note.data(), note.size())) {
            refused = i;  // a ring refusal (the writer drains it), not the case being tested here
            break;
        }
        // A writer that noticed the push delivers it at once; one that went to sleep with it queued delivers it only
        // on the NEXT push or on stop(), so the round's own bound is what tells them apart. It is the file's 5 s
        // policy, not the round's healthy microseconds: a lost record is held for ever (nothing stops the writer in the
        // loop), so the bound catches it just as surely, while a runner that deschedules this thread mid-round must not
        // be a lost wake.
        const auto deadline = Clock::now() + std::chrono::milliseconds(5000);
        int        spins    = 0;
        while (backend.sendsFinished.load(std::memory_order_acquire) < i) {
            if ((++spins & 0x3FF) == 0 && Clock::now() >= deadline) break;
            std::this_thread::yield();
        }
        if (backend.sendsFinished.load(std::memory_order_acquire) < i) lost = i;
    }
    writer->stop();
    CHECK(refused == -1);  // the round whose push the ring refused, -1 when it refused none
    CHECK(lost == -1);  // the round whose message the writer held, -1 when it held none
    // Nothing was dropped and nothing was abandoned: every message reached the port, in order, whatever the scheduling.
    if (lost < 0 && refused < 0) CHECK(backend.sentTo("Synth out").size() == kRounds);
}

// ---- Inline, the mode the CLI's facet and the test host use ---------------------------------------------------

TEST_CASE("an inline push is the write, on the caller's thread") {
    Rig     rig;
    auto&   backend     = rig.backend;
    backend.sendDelayMs = 100;
    auto               writer = rig.open({OutputMode::Inline, false, PortWriter::kDefaultRingBytes});
    const Bytes        note   = {0x90, 0x3C, 0x7F};
    const auto         t0     = Clock::now();
    REQUIRE(writer->push(note.data(), note.size()));
    // "The push returned" is "it is on the wire": that is what a script relying on midiSend before tjs.exit wants.
    CHECK(Clock::now() - t0 >= std::chrono::milliseconds(100));
    CHECK(backend.sentTo("Synth out") == std::vector<Bytes>{note});
    // No thread was started, so none is left to stop with: the close is the port's.
    writer->stop();
    CHECK(backend.sendsAfterClose() == 0);
    CHECK(!writer->push(note.data(), note.size()));  // a stopped writer takes nothing, whichever mode it is
}

// ---- overflow -------------------------------------------------------------------------------------------------

TEST_CASE("a full ring drops the newest WHOLE message, counts it, and never tears one") {
    Rig   rig;
    auto& backend       = rig.backend;
    backend.sendDelayMs = 300;  // the first message sits in the port while the ring fills
    // A ring of four 40-byte records: the fifth cannot fit while the first is in flight.
    auto          writer = rig.open(PortWriter::Options{OutputMode::Threaded, false, 4 * (sizeof(std::uint32_t) + 40)});
    const Bytes   first  = bytes(40);
    REQUIRE(writer->push(first.data(), first.size()));
    REQUIRE(eventually([&] { return backend.sendsStarted.load() == 1; }, std::chrono::milliseconds(2000)));
    // The port is inside the first send, so the ring fills: fill it until it refuses, and count the refusals - what it
    // takes is exact, what it does not is dropped WHOLE.
    int refused = 0;
    for (int i = 0; i < 12; ++i)
        if (!writer->push(first.data(), first.size())) ++refused;
    CHECK(refused > 0);
    CHECK(writer->dropped() == static_cast<std::uint64_t>(refused));
    writer->stop();  // flushes what it took, up to the deadline
    // Every message that arrived is whole: a fragment is what an all-or-nothing push exists to prevent.
    const std::vector<Bytes> sent = backend.sentTo("Synth out");
    for (const Bytes& message : sent) CHECK(message == first);
    CHECK(sent.size() >= 1);
}

// ---- stopping -------------------------------------------------------------------------------------------------

TEST_CASE("a threaded writer whose port never opened starts no thread, and stop is the no-op it promises") {
    // The stream opens a port OUTSIDE the writer, so a refused exclusive open never builds one with no port; a null
    // one still has to be safe, and stop()'s doc says it is. It was not: the thread started whatever `port_` was, and
    // stop() returned at its `!port_` check without joining it, so ~thread ended the process.
    PortWriter         writer(nullptr, "no port", PortWriter::Options{}, [](const std::string&) {});
    const std::uint8_t note[3] = {0x90, 0x3C, 0x7F};
    CHECK(!writer.push(note, sizeof(note)));
    writer.stop();
    writer.stop();  // idempotent
    CHECK(writer.dropped() == 0);
}

TEST_CASE("a stop flushes what queued before the port goes, and nothing is sent after it") {
    Rig   rig;
    auto& backend = rig.backend;
    // The first write waits inside the port with the rest queued behind it, and the port lets it go once the stop is
    // under way, so the backlog is the flush's to send. Held, not slowed: a backlog of 20 ms writes spent the flush's
    // whole deadline (500 ms) on five of them on a macOS runner whose sleeps overshot, and the flush rightly gave the
    // rest up. A release that comes before the stop on a stalled machine leaves the writer nothing to flush, which
    // still passes; one that comes inside the deadline, as it does by a wide margin, has no time to run out.
    backend.holdSends();
    auto          writer  = rig.open();
    const Bytes   note    = {0x90, 0x3C, 0x7F};
    constexpr int kQueued = 8;
    for (int i = 0; i < kQueued; ++i) REQUIRE(writer->push(note.data(), note.size()));
    REQUIRE(eventually([&] { return backend.sendsStarted.load() == 1; }, std::chrono::milliseconds(2000)));
    std::thread release([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        backend.releaseSends();
    });
    writer->stop();
    release.join();
    // Every queued message went out before the close (a dropped Note Off is a stuck note), and none after it.
    CHECK(backend.sentTo("Synth out").size() == kQueued);
    CHECK(backend.sendsAfterClose() == 0);
    // A stopped writer takes nothing more: a push is refused rather than reaching a closed port.
    CHECK(!writer->push(note.data(), note.size()));
}

TEST_CASE("a stop that lands in any phase of the writer's pass joins it, and flushes what was queued") {
    // stop() is a push with no record: it bumps the counter and clears the flag, so a writer that is sending, deciding,
    // publishing its sleep or already asleep is joined. The hang this holds against (a writer that publishes after the
    // stop's clear and reads `stopping_` stale) is a weak-memory reordering no x86 machine makes and today's AArch64 code
    // does not either, so no run here can show it red; what the case does is put the stop in every phase of a pass,
    // twenty thousand times, so the path the header's proof is about is run. A hang is a watchdog's abort naming the
    // round, not a suite that never returns.
    Rig                     rig;
    auto&                   backend = rig.backend;
    const Bytes             note    = {0x90, 0x3C, 0x7F};
    constexpr int           kRounds = 20000;
    std::atomic<int>        round{0};
    std::mutex              doneMutex;
    std::condition_variable doneWake;
    bool                    done = false;
    std::thread             watchdog([&] {
        std::unique_lock<std::mutex> lk(doneMutex);
        int                          last = -1;
        while (!done) {
            // 5 s with no round finished is a stop that never returned (a healthy round is microseconds).
            if (doneWake.wait_for(lk, std::chrono::seconds(5), [&] { return done; })) break;
            const int now = round.load();
            if (now == last) {
                std::fprintf(stderr, "PortWriter stop hung in round %d\n", now);
                std::abort();
            }
            last = now;
        }
    });
    int sent = 0;
    for (int i = 0; i < kRounds; ++i) {
        auto writer = rig.open();
        // The phase: a stop before the writer's first pass, one while it sends, one as it leaves the send for the idle
        // path, and one a varying while after it went idle (publishing its sleep, re-reading, or asleep).
        switch (i % 4) {
            case 0: break;
            case 1:
                REQUIRE(writer->push(note.data(), note.size()));
                ++sent;
                break;
            case 2:
            case 3:
                REQUIRE(writer->push(note.data(), note.size()));
                ++sent;
                while (backend.sendsFinished.load(std::memory_order_acquire) < sent) std::this_thread::yield();
                for (int spin = 0; spin < (i % 4 == 2 ? 0 : (i / 4) % 64); ++spin) std::this_thread::yield();
                break;
        }
        writer->stop();
        round.store(i + 1);
    }
    {
        std::lock_guard<std::mutex> lk(doneMutex);
        done = true;
    }
    doneWake.notify_all();
    watchdog.join();
    // Every pushed message was flushed by its stop (the port is instant, so no deadline ran out), and none after.
    CHECK(backend.totalReceived() == static_cast<std::size_t>(sent));
    CHECK(backend.sendsAfterClose() == 0);
}

TEST_CASE("a wedged port's stop returns by its deadline, and abandons the rest of the backlog") {
    Rig   rig;
    auto& backend       = rig.backend;
    backend.sendDelayMs = 200;  // each write takes 200 ms: the whole backlog would be 4 seconds
    auto          writer  = rig.open();
    const Bytes   note    = {0x90, 0x3C, 0x7F};
    constexpr int kQueued = 20;
    for (int i = 0; i < kQueued; ++i) REQUIRE(writer->push(note.data(), note.size()));
    const auto t0 = Clock::now();
    writer->stop();
    const auto elapsed = Clock::now() - t0;
    // The deadline (counted from the stop), plus the one write that was already inside the port when the stop landed,
    // plus the one that started just before the deadline, plus slack.
    CHECK(elapsed < PortWriter::kFlushDeadline + std::chrono::milliseconds(2 * 200 + 500));
    CHECK(backend.sendsAfterClose() == 0);
    // It gave up on the rest: fewer than the twenty, and more than none (the flush sent what it could).
    const std::size_t sent = backend.sentTo("Synth out").size();
    CHECK(sent >= 1);
    CHECK(sent < kQueued);
    // And what it gave up on is counted, as a full ring's drops are: a Note Off the flush abandoned is not invisible.
    CHECK(writer->dropped() == kQueued - sent);
}

// ---- the audio thread allocates nothing -----------------------------------------------------------------------

namespace rp::midi::test {

// The header declares these; this TU owns them, and the operator new/delete replacements below cover the whole test
// binary (MidiStream.test.cpp's Threaded-send case reads the same count).
thread_local bool        countAllocations = false;
thread_local std::size_t allocations      = 0;

}  // namespace rp::midi::test

void* operator new(std::size_t size) {
    if (rp::midi::test::countAllocations) ++rp::midi::test::allocations;
    if (void* p = std::malloc(size == 0 ? 1 : size)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void  operator delete(void* p) noexcept { std::free(p); }
void  operator delete[](void* p) noexcept { std::free(p); }
void  operator delete(void* p, std::size_t) noexcept { std::free(p); }
void  operator delete[](void* p, std::size_t) noexcept { std::free(p); }

TEST_CASE("a threaded push allocates nothing on the caller's thread") {
    Rig   rig;
    auto& backend       = rig.backend;
    backend.sendDelayMs = 50;  // the ring is being drained while the pushes land
    auto        writer  = rig.open();
    const Bytes sysex   = bytes(538, 0xF0);
    const Bytes note    = {0x90, 0x3C, 0x7F};
    const auto  t0      = Clock::now();
    rp::midi::test::AllocationCount count;
    for (int i = 0; i < 64; ++i) {
        writer->push(sysex.data(), sysex.size());
        writer->push(note.data(), note.size());
    }
    const bool quiet = count.value() == 0;
    CHECK(quiet);
    CHECK(Clock::now() - t0 < std::chrono::milliseconds(150));  // and it did not wait for the port either
    writer->stop();
}
