// Unit-tests MidiStream, the host's one MIDI stream, over FakeMidiBackend: which ports a selection opens (the lists and
// "all"), a re-pick reopening what it names, the reservation and the scan's suspended edge, the receive ring under
// concurrent producers and under a delivery thread, an output swap and a close under a send in flight, the log lines
// the harnesses grep, a backend with no virtual ports, a refused open, and the test-only allow list reaching the
// stream's own list and opens. No MIDI system and no hardware: the backend is the fake in midi/FakeMidiBackend.hpp.
//
// The output-swap and close cases are the ones that must be run in build-tsan/ as well (section 4.7 of
// docs/modular-graph-phase-f5b-design.md): the ports a send() may be using are freed only once the send in flight has
// been waited out.

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "AllocationCounter.hpp"  // the global operator new PortWriter.test.cpp defines, and its per-thread count
#include "midi/FakeMidiBackend.hpp"
#include "midi/MidiStream.hpp"

using rp::midi::FakeMidiBackend;
using rp::midi::MidiStream;

namespace {

/** RP_MIDI_PORT_ALLOW is a developer's environment variable (tools/run-sdl-controller.sh exports it for the
 *  standalone's harness), and a hand run of that harness leaves it in the shell. The cases here open the fake's own
 *  ports, so the file clears it on load and puts back whatever it found at exit; the one case that wants a list sets it
 *  for its own scope. */
struct UnfilteredFileScope {
    UnfilteredFileScope() {
        const char* was = std::getenv("RP_MIDI_PORT_ALLOW");
        had_            = was != nullptr;
        if (had_) was_ = was;
#if defined(_WIN32)
        _putenv_s("RP_MIDI_PORT_ALLOW", "");
#else
        unsetenv("RP_MIDI_PORT_ALLOW");
#endif
    }
    ~UnfilteredFileScope() {
#if defined(_WIN32)
        _putenv_s("RP_MIDI_PORT_ALLOW", had_ ? was_.c_str() : "");
#else
        if (had_) setenv("RP_MIDI_PORT_ALLOW", was_.c_str(), 1);
        else unsetenv("RP_MIDI_PORT_ALLOW");
#endif
    }
    bool        had_ = false;
    std::string was_;
};
const UnfilteredFileScope kUnfiltered;

/** RP_MIDI_PORT_ALLOW for one case, put back as it was when it goes (the filter reads it on every call). */
struct PortAllowList {
    explicit PortAllowList(const char* spec) {
        const char* was = std::getenv("RP_MIDI_PORT_ALLOW");
        had_            = was != nullptr;
        if (had_) was_ = was;
#if defined(_WIN32)
        _putenv_s("RP_MIDI_PORT_ALLOW", spec);
#else
        setenv("RP_MIDI_PORT_ALLOW", spec, 1);
#endif
    }
    ~PortAllowList() {
#if defined(_WIN32)
        _putenv_s("RP_MIDI_PORT_ALLOW", had_ ? was_.c_str() : "");
#else
        if (had_) setenv("RP_MIDI_PORT_ALLOW", was_.c_str(), 1);
        else unsetenv("RP_MIDI_PORT_ALLOW");
#endif
    }
    PortAllowList(const PortAllowList&)            = delete;
    PortAllowList& operator=(const PortAllowList&) = delete;

    bool        had_ = false;
    std::string was_;
};

/** A machine with two controllers and a synth's output, and nothing open: what the stream's cases start from. The fake
 *  holds a mutex, so it is neither copyable nor movable and the rig holds it by value rather than returning one. */
struct Machine {
    FakeMidiBackend backend;
    Machine() {
        backend.inputs  = {"Keys in", "Pads in", "Midi Through:Midi Through Port-0 14:0",
                           "RetroPlug:RetroPlug In 128:0"};
        backend.outputs = {"Synth out", "Interface out", "RetroPlug:RetroPlug Out 128:1"};
    }
};

/** The "open ..." events a rig recorded, so a case can hold WHICH ports the stream opened and in what order. */
std::vector<std::string> opensOf(const FakeMidiBackend& backend) {
    std::vector<std::string> out;
    for (const std::string& e : backend.events)
        if (e.rfind("open ", 0) == 0 || e.rfind("virtual ", 0) == 0) out.push_back(e);
    return out;
}

std::vector<std::uint8_t> bytes(int a, int b, int c = -1) {
    std::vector<std::uint8_t> out{static_cast<std::uint8_t>(a), static_cast<std::uint8_t>(b)};
    if (c >= 0) out.push_back(static_cast<std::uint8_t>(c));
    return out;
}

/** Wait until `count` sends have FINISHED: Inline has done them by the time the send returns, and Threaded's writers
 *  do them after it. Waiting for "as many started as finished" is not enough - that reads 0 == 0 before the writer has
 *  begun - so the count is the caller's, which is what the case knows it sent. */
bool settled(const FakeMidiBackend& backend, const int count) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(5000);
    while (std::chrono::steady_clock::now() < deadline) {
        if (backend.sendsFinished.load() == count) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return backend.sendsFinished.load() == count;
}

}  // namespace

// ---- what a selection opens ------------------------------------------------------------------------------------

TEST_CASE("an empty selection opens no hardware port: the virtual pair and nothing else") {
    Machine             machine;
    FakeMidiBackend&    backend = machine.backend;
    MidiStream          stream(machine.backend);
    REQUIRE(stream.open());
    // The stream opened the virtual pair ("RetroPlug In" / "RetroPlug Out") and no hardware port.
    CHECK(opensOf(backend) == std::vector<std::string>{"virtual in:RetroPlug", "virtual out:RetroPlug"});
    CHECK(backend.listening.count("Keys in") == 0);
    CHECK(stream.listInputs() == std::vector<std::string>{"Keys in", "Pads in"});
    CHECK(stream.listOutputs() == std::vector<std::string>{"Synth out", "Interface out"});
}

TEST_CASE("a list of exact names opens exactly those ports") {
    Machine             machine;
    FakeMidiBackend&    backend = machine.backend;
    MidiStream          stream(machine.backend);
    stream.setInputs({"Pads in"}, false);
    stream.setOutputs({"Interface out"});
    REQUIRE(stream.open());
    CHECK(backend.listening.count("Pads in") == 1);
    CHECK(backend.listening.count("Keys in") == 0);
    // The output: a send lands on the selected port and the virtual one, nowhere else.
    const std::uint8_t note[3] = {0x90, 0x3C, 0x7F};
    stream.send(note, 3);
    CHECK(backend.sentTo("Interface out").size() == 1);
    CHECK(backend.sentTo("Synth out").empty());
    CHECK(backend.sentTo("RetroPlug Out").size() == 1);  // the virtual output mirrors, as it always did
}

TEST_CASE("all devices opens every hardware input, once each, and no virtual one twice") {
    Machine          machine;
    FakeMidiBackend& backend = machine.backend;
    MidiStream       stream(backend);
    stream.setInputs({}, true);
    REQUIRE(stream.open());
    CHECK(backend.listening.count("Keys in") == 1);
    CHECK(backend.listening.count("Pads in") == 1);
    // The rig's own virtual port and ALSA's Through are not hardware: neither is opened, and the virtual pair the
    // stream published is still exactly one port each.
    CHECK(backend.listening.count("RetroPlug:RetroPlug In 128:0") == 0);
    CHECK(backend.listening.count("Midi Through:Midi Through Port-0 14:0") == 0);
    CHECK(opensOf(backend) == std::vector<std::string>{"virtual in:RetroPlug", "virtual out:RetroPlug",
                                                       "open in:Keys in", "open in:Pads in"});
    // Every open input feeds the one ring: a message on either is drained with its bytes intact.
    backend.deliver("Keys in", bytes(0x90, 0x3C, 0x7F));
    backend.deliver("Pads in", bytes(0xB0, 0x07, 0x64));
    std::vector<MidiStream::Message> got;
    stream.poll(got);
    REQUIRE(got.size() == 2);
    CHECK(got[0].bytes == bytes(0x90, 0x3C, 0x7F));
    CHECK(got[1].bytes == bytes(0xB0, 0x07, 0x64));
    CHECK(got[0].seq < got[1].seq);
}

TEST_CASE("a session's name is not the family: a renamed stream still refuses every RetroPlug port as hardware") {
    // The CLI's facet renames a stream for its session ("RetroPlug N8", "RetroPlug Loopback") - a name a DAW routes to -
    // and no input means every hardware input, which is the bridge's default. The machine then lists this app's OWN
    // ports: the standalone's virtual output is a readable ALSA output so it lists as an input, and so is a claimed
    // Launchpad's link. Judged by the session's name they are hardware, and "all" would forward RetroPlug's own MIDI
    // into the session's graph (E11); judged by the FAMILY they are the app's, as they are to the stream that published
    // them. No allow list here on purpose: the filter, not a test-only list, is what keeps them out.
    FakeMidiBackend backend;
    backend.inputs  = {"RetroPlug:RetroPlug Out 128:1", "RetroPlug Launchpad:LPProMK3 MIDI 129:1", "Keys in"};
    backend.outputs = {"RetroPlug:RetroPlug In 128:0", "RetroPlug Launchpad:LPProMK3 MIDI 129:0", "Synth out"};
    std::unique_ptr<rp::midi::MidiBackend> renamed = backend.withClientName("RetroPlug N8");
    auto&                                  session = static_cast<FakeMidiBackend&>(*renamed);
    MidiStream                             stream(session);
    stream.setInputs({}, true);
    stream.setOutputs({"Synth out"});
    REQUIRE(stream.open());
    CHECK(stream.listInputs() == std::vector<std::string>{"Keys in"});
    CHECK(stream.listOutputs() == std::vector<std::string>{"Synth out"});
    CHECK(session.listening.count("Keys in") == 1);
    CHECK(session.listening.count("RetroPlug:RetroPlug Out 128:1") == 0);
    CHECK(session.listening.count("RetroPlug Launchpad:LPProMK3 MIDI 129:1") == 0);
    // The virtual pair this stream publishes is its SESSION's, still: the family decides what is hardware, the client
    // name decides what is published.
    CHECK(session.events.front() == "virtual in:RetroPlug N8");
}

TEST_CASE("a name that is not present opens nothing, and a later apply picks it up when it appears") {
    Machine             machine;
    FakeMidiBackend&    backend = machine.backend;
    MidiStream          stream(machine.backend);
    stream.setInputs({"Unplugged keys"}, false);
    stream.setOutputs({"Unplugged synth"});
    REQUIRE(stream.open());
    CHECK(backend.listening.empty());
    CHECK(backend.totalReceived() == 0);
    // The name stayed in the selection: the device appears (a machine's list is read when it is asked), and the next
    // apply opens it. Nothing was dropped from the selection by the failed attempt.
    backend.inputs.push_back("Unplugged keys");
    backend.outputs.push_back("Unplugged synth");
    stream.setInputs({"Unplugged keys"}, false);  // the next apply
    CHECK(backend.listening.count("Unplugged keys") == 1);
}

TEST_CASE("a re-pick of the same device closes and reopens it: how an unplugged port heals") {
    // There is no hotplug watch, so picking the device again in Settings > MIDI is the user's one remedy after an
    // unplug and a replug - and it has to reopen, not diff to the name it already held. That is what Apply::Reopen
    // asks for: a diff (a project load) would leave the dead port open.
    Machine          machine;
    FakeMidiBackend& backend = machine.backend;
    MidiStream       stream(backend);
    stream.setInputs({"Keys in"}, false);
    stream.setOutputs({"Synth out"});
    REQUIRE(stream.open());
    const std::size_t afterFirstPick = backend.records();
    // The same selection again: the ports it names are let go and opened afresh (one record each, and the fake's event
    // log shows the closes in between).
    stream.setInputs({"Keys in"}, false, rp::midi::Apply::Reopen);
    stream.setOutputs({"Synth out"}, rp::midi::Apply::Reopen);
    CHECK(backend.records() == afterFirstPick + 2);
    // Two opens of that output in the log: the first pick's and the re-pick's.
    CHECK(std::count(backend.events.begin(), backend.events.end(), "open out:Synth out") == 2);
    CHECK(std::count(backend.events.begin(), backend.events.end(), "close out:Synth out") == 1);
    // And what is open is what was asked for, which is what the rows read.
    CHECK(stream.openedInputs() == std::vector<std::string>{"Keys in"});
    CHECK(stream.openedOutputs() == std::vector<std::string>{"Synth out"});
    REQUIRE(backend.listening.count("Keys in") == 1);
    const std::uint8_t note[3] = {0x90, 0x3C, 0x7F};
    stream.send(note, 3);
    CHECK(backend.sentTo("Synth out").size() == 1);
}

TEST_CASE("a diff re-applies only what moved: a port the selection keeps is never reopened") {
    // A project load applies the selection a project's section names, and almost every port in it may already be open
    // (the same project reloaded, a second section that agrees). Reopening those would cost the connection: a clock
    // arriving on a hardware input loses what arrives in the gap, and on Windows an exclusive port can be lost
    // outright. Only the names the change moved are touched.
    Machine          machine;
    FakeMidiBackend& backend = machine.backend;
    MidiStream       stream(backend);
    stream.setInputs({"Keys in"}, false);
    stream.setOutputs({"Synth out"});
    REQUIRE(stream.open());
    const std::size_t afterFirstPick = backend.records();
    // Adding an input and an output opens exactly the two that were added, and leaves the two that were kept alone.
    stream.setInputs({"Keys in", "Pads in"}, false);
    stream.setOutputs({"Synth out", "Interface out"});
    CHECK(backend.records() == afterFirstPick + 2);
    CHECK(std::count(backend.events.begin(), backend.events.end(), "open in:Keys in") == 1);
    CHECK(std::count(backend.events.begin(), backend.events.end(), "close in:Keys in") == 0);
    CHECK(std::count(backend.events.begin(), backend.events.end(), "open out:Synth out") == 1);
    CHECK(std::count(backend.events.begin(), backend.events.end(), "close out:Synth out") == 0);
    CHECK(stream.openedInputs() == std::vector<std::string>{"Keys in", "Pads in"});
    CHECK(stream.openedOutputs() == std::vector<std::string>{"Synth out", "Interface out"});
    // Dropping one closes it and nothing else: the kept input's receiver still delivers.
    stream.setInputs({"Keys in"}, false);
    CHECK(std::count(backend.events.begin(), backend.events.end(), "close in:Pads in") == 1);
    CHECK(std::count(backend.events.begin(), backend.events.end(), "close in:Keys in") == 0);
    backend.deliver("Keys in", bytes(0x90, 0x3C, 0x7F));
    std::vector<MidiStream::Message> got;
    stream.poll(got);
    REQUIRE(got.size() == 1);
    CHECK(got[0].bytes == bytes(0x90, 0x3C, 0x7F));
    // Restating the unchanged selection costs nothing at all.
    const std::size_t before = backend.records();
    stream.setInputs({"Keys in"}, false);
    stream.setOutputs({"Synth out", "Interface out"});
    CHECK(backend.records() == before);
}

TEST_CASE("an exclusive port: a re-pick must let the port go before it is opened again, or it is lost") {
    // A WinMM port's model, over the fake: a second open of a name this stream still holds is
    // refused. A re-pick that opened before it closed therefore lost the device it meant to heal - the output went
    // silent for good, and the event log says so (a refused open, then a close, then nothing).
    const rp::midi::OutputMode mode = GENERATE(rp::midi::OutputMode::Inline, rp::midi::OutputMode::Threaded);
    Machine          machine;
    FakeMidiBackend& backend = machine.backend;
    backend.exclusivePorts   = true;
    // The virtual output is inline in both modes and the fake counts its sends too: off, so a send is one hardware
    // write, which is what this case waits for.
    backend.virtualPorts = false;
    MidiStream stream(backend, mode);
    stream.setInputs({"Keys in"}, false);
    stream.setOutputs({"Synth out"});
    REQUIRE(stream.open());
    REQUIRE(stream.openedOutputs() == std::vector<std::string>{"Synth out"});
    stream.setInputs({"Keys in"}, false, rp::midi::Apply::Reopen);
    stream.setOutputs({"Synth out"}, rp::midi::Apply::Reopen);
    // The port is still the stream's: the close came first, so the second open was accepted.
    CHECK(stream.openedInputs() == std::vector<std::string>{"Keys in"});
    CHECK(stream.openedOutputs() == std::vector<std::string>{"Synth out"});
    CHECK(backend.listening.count("Keys in") == 1);
    const std::uint8_t note[3] = {0x90, 0x3C, 0x7F};
    stream.send(note, 3);
    REQUIRE(settled(backend, 1));  // Threaded: the port is read once the writer has written what the push queued
    CHECK(backend.sentTo("Synth out").size() == 1);
    // A diff keeps the port and never asks for it again, so it is not refused either.
    stream.setOutputs({"Synth out", "Interface out"});
    CHECK(stream.openedOutputs() == std::vector<std::string>{"Synth out", "Interface out"});
    stream.send(note, 3);  // this one fans out to both ports
    REQUIRE(settled(backend, 3));
    CHECK(backend.sentTo("Interface out").size() == 1);
}

TEST_CASE("what opened is not what was selected: a missing name and a held port are not in the opened sets") {
    Machine          machine;
    FakeMidiBackend& backend = machine.backend;
    MidiStream       stream(backend);
    stream.setInputs({"Keys in", "Not here"}, false);
    stream.setOutputs({"Synth out", "Not here either"});
    REQUIRE(stream.open());
    CHECK(stream.openedInputs() == std::vector<std::string>{"Keys in"});
    CHECK(stream.openedOutputs() == std::vector<std::string>{"Synth out"});
    // A claimed port is selected and not opened, which is what the checklist's held mark says.
    stream.setReserved("Keys in", "Synth out");
    CHECK(stream.openedInputs().empty());
    CHECK(stream.openedOutputs().empty());
}

TEST_CASE("closing the stream gives every port back, and re-opening re-applies the selection") {
    Machine             machine;
    FakeMidiBackend&    backend = machine.backend;
    MidiStream          stream(machine.backend);
    stream.setInputs({"Keys in"}, false);
    stream.setOutputs({"Synth out"});
    REQUIRE(stream.open());
    REQUIRE(backend.listening.count("Keys in") == 1);
    stream.close();
    CHECK(backend.listening.empty());
    CHECK_FALSE(stream.isOpen());
    // A send after the close goes nowhere (not to a port the close freed).
    const std::size_t before = backend.totalReceived();
    const std::uint8_t note[3] = {0x90, 0x3C, 0x7F};
    stream.send(note, 3);
    CHECK(backend.totalReceived() == before);
    // Re-opened, the selection is applied again.
    REQUIRE(stream.open());
    CHECK(backend.listening.count("Keys in") == 1);
}

TEST_CASE("a close under a send in flight frees no port the send is using, nor the virtual output") {
    // A device loss is one reason a product closes the stream while audio still renders; the close publishes the end of
    // the set and waits the send out, so the ports (and the virtual output, whose sends the same bracket covers) are
    // never freed under a caller still inside one.
    const rp::midi::OutputMode mode = GENERATE(rp::midi::OutputMode::Inline, rp::midi::OutputMode::Threaded);
    Machine          machine;
    FakeMidiBackend& backend = machine.backend;
    // INLINE: a slow port, so a send is still inside the fake when the close lands, and the virtual output on, because
    // this case is about the epoch bracket covering its send too. THREADED: no delay, because the pushes only have to
    // reach the ring (the subject is push against retire and flush, not a slow port) and a millisecond per send is the
    // platform's sleep granularity off Linux (Windows' is about 15.6 ms), not a millisecond; and the virtual output off,
    // because its send stays inline there and the fake's delay would pace this caller, leaving the ring holding only a
    // handful of messages - what this mode has to say is the hardware port's flush.
    backend.sendDelayMs  = mode == rp::midi::OutputMode::Inline ? 1 : 0;
    backend.virtualPorts = mode == rp::midi::OutputMode::Inline;
    MidiStream stream(backend, mode);
    stream.setOutputs({"Synth out"});
    REQUIRE(stream.open());
    constexpr int     kSends = 200;
    std::atomic<bool> started{false};
    std::atomic<bool> pushed{false};
    std::thread       audio([&] {
        const std::uint8_t note[3] = {0x90, 0x3C, 0x7F};
        started.store(true);
        for (int i = 0; i < kSends; ++i) stream.send(note, 3);
        pushed.store(true);
    });
    while (!started.load()) std::this_thread::yield();
    if (mode == rp::midi::OutputMode::Inline) {
        // A few sends in, so one is in flight when the close lands.
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    } else {
        // Every push made before the close, so the count below does not rest on the pushes beating a timer: on a loaded
        // box (or under the thread sanitizer) 200 pushes, each of which may wake the writer, can outlast any fixed
        // sleep, and a push after the close goes nowhere. The writer may still be draining the ring when the close
        // lands.
        while (!pushed.load()) std::this_thread::yield();
    }
    stream.close();
    audio.join();
    CHECK(backend.sendsAfterClose() == 0);
    // Inline: whatever had not been sent by the close went nowhere at all, because no port was left published to receive
    // it. Threaded: the close FLUSHES the ring before the port goes instead (a dropped Note Off is a stuck note), so
    // every push, all of which had returned when it landed, reaches the port.
    if (mode == rp::midi::OutputMode::Inline) {
        CHECK(backend.sendsStarted.load() < kSends);
    } else {
        CHECK(backend.sendsStarted.load() == kSends);
    }
}

// ---- the reservation and the scan's held edge ------------------------------------------------------------------

TEST_CASE("a reserved port is skipped under all devices and under a list naming it, and the claim itself is a no-op "
          "when nothing moved") {
    Machine             machine;
    FakeMidiBackend&    backend = machine.backend;
    MidiStream          stream(machine.backend);
    stream.setInputs({"Keys in", "Pads in"}, false);
    stream.setOutputs({"Synth out"});
    REQUIRE(stream.open());
    REQUIRE(backend.listening.size() == 2);
    const std::size_t recordsBefore = backend.records();
    // Claiming an open port makes the stream let go of it, and of nothing else.
    stream.setReserved("Keys in", "Synth out");
    CHECK(backend.listening.count("Keys in") == 0);
    CHECK(backend.listening.count("Pads in") == 1);
    // The output did not move? It did: the claim named it, so it is held and closed.
    const std::uint8_t note[3] = {0x90, 0x3C, 0x7F};
    stream.send(note, 3);
    CHECK(backend.sentTo("Synth out").empty());
    // The claim and the scan's edge apply what is open after a diff, so restating one costs no port churn (the claimed
    // port stays out, and the one output is still held). A SELECTION setter is diffed too: a re-pick is what asks for a
    // reopen, and that is the case above.
    const std::size_t recordsAfterClaim = backend.records();
    stream.setReserved("Keys in", "Synth out");
    stream.setHardwareInputsSuspended(false);  // already resumed: no apply at all
    CHECK(backend.records() == recordsAfterClaim);
    // A claim only ever CLOSES: the ports it takes away are let go and nothing is opened in their place.
    CHECK(recordsAfterClaim == recordsBefore);
    // Releasing gives both back.
    stream.setReserved("", "");
    CHECK(backend.listening.count("Keys in") == 1);
    stream.send(note, 3);
    CHECK(backend.sentTo("Synth out").size() == 1);
}

TEST_CASE("a reserved input under all devices still leaves every OTHER input open") {
    Machine             machine;
    FakeMidiBackend&    backend = machine.backend;
    MidiStream          stream(machine.backend);
    stream.setInputs({}, true);
    stream.setReserved("Pads in", "");
    REQUIRE(stream.open());
    CHECK(backend.listening.count("Keys in") == 1);
    CHECK(backend.listening.count("Pads in") == 0);
}

TEST_CASE("a scan's held edge closes every input and resuming re-applies the selection") {
    Machine             machine;
    FakeMidiBackend&    backend = machine.backend;
    MidiStream          stream(machine.backend);
    stream.setInputs({"Keys in", "Pads in"}, false);
    REQUIRE(stream.open());
    REQUIRE(backend.listening.size() == 2);
    stream.setHardwareInputsSuspended(true);
    CHECK(stream.hardwareInputsSuspended());
    CHECK(backend.listening.empty());
    // The selection was not disturbed: resuming opens exactly what it held.
    stream.setHardwareInputsSuspended(false);
    CHECK(backend.listening.count("Keys in") == 1);
    CHECK(backend.listening.count("Pads in") == 1);
}

// ---- the receive ring ----------------------------------------------------------------------------------------

namespace {

constexpr int kProducers   = 4;
constexpr int kPerProducer = 5000;
// Producers yield this often. Without it a few unpaced producers fill the ring and nearly everything takes the drop
// path, which writes no slot - so on a 1-2 core runner the racy write path runs only a couple of hundred times out of
// twenty thousand. Paced, most messages land and the slot writes actually contend.
constexpr int kYieldEvery = 16;
constexpr std::size_t kRing = MidiStream::kCapacity;

// Message i from producer p: a status byte naming the producer, i split over two data bytes, then a tail whose length
// (0-4 bytes) and contents both depend on i - so a slot assembled from two writers fails the decode rather than passing
// as some other valid message.
std::vector<std::uint8_t> encode(int p, int i) {
    std::vector<std::uint8_t> m = {static_cast<std::uint8_t>(0x90 | p), static_cast<std::uint8_t>(i & 0x7F),
                                   static_cast<std::uint8_t>((i >> 7) & 0x7F)};
    for (int k = 0; k < i % 5; ++k) m.push_back(static_cast<std::uint8_t>((i + k) & 0x7F));
    return m;
}

// The (producer, i) a message encodes, or {-1, -1} if it isn't a well-formed message from encode().
std::pair<int, int> decode(const std::vector<std::uint8_t>& m) {
    if (m.size() < 3 || (m[0] & 0xF0) != 0x90) return {-1, -1};
    const int p = m[0] & 0x0F;
    const int i = m[1] | (m[2] << 7);
    if (p >= kProducers || i >= kPerProducer || m != encode(p, i)) return {-1, -1};
    return {p, i};
}

}  // namespace

TEST_CASE("the input ring: concurrent producers deliver every message intact, once, in order") {
    // Every open input delivers on its own callback thread - the virtual one plus one per selected hardware port - so a
    // selection of several puts two or more threads in pushRing at once while the audio thread drains with poll(). The
    // ring has one producer at a time (pushMutex_); without that, these threads race the tail index, the sequence
    // counter and a slot's vector, which build-tsan/ reports every run.
    Machine          machine;
    FakeMidiBackend& backend = machine.backend;
    MidiStream       stream(backend);
    stream.setInputs({"Keys in", "Pads in"}, false);
    REQUIRE(stream.open());
    // The stream's ring has three producers here: the virtual input and the two hardware ones. The case drives the two
    // hardware ports directly, on more threads than there are ports, which is the shape the race was in.
    std::atomic<int> producersLeft{kProducers};
    std::atomic<int> ready{0};

    std::vector<std::thread> producers;
    for (int p = 0; p < kProducers; ++p)
        producers.emplace_back([&backend, &producersLeft, &ready, p] {
            ready.fetch_add(1, std::memory_order_acq_rel);
            while (ready.load(std::memory_order_acquire) < kProducers) std::this_thread::yield();
            // Half the producers speak on each port: deliver() calls that port's receiver on THIS thread, which is the
            // shape an RtMidi callback has - one thread per open input, all pushing into the one ring.
            const std::string port = p % 2 == 0 ? "Keys in" : "Pads in";
            for (int i = 0; i < kPerProducer; ++i) {
                backend.deliver(port, encode(p, i));
                if (i % kYieldEvery == 0) std::this_thread::yield();
            }
            producersLeft.fetch_sub(1, std::memory_order_release);
        });

    // The consumer drains while the producers run (the audio thread's shape), then once more after.
    std::vector<MidiStream::Message> received;
    std::thread consumer([&] {
        std::vector<MidiStream::Message> batch;
        bool last = false;
        while (!last) {
            last = producersLeft.load(std::memory_order_acquire) == 0;
            stream.poll(batch);
            for (auto& m : batch) received.push_back(std::move(m));
        }
    });

    for (auto& t : producers) t.join();
    consumer.join();

    std::set<std::pair<int, int>> seen;
    int lastIndex[kProducers];
    for (int& v : lastIndex) v = -1;
    std::uint64_t lastSeq = 0;
    int malformed = 0, duplicates = 0, reordered = 0, seqGaps = 0;
    for (const auto& m : received) {
        const auto [p, i] = decode(m.bytes);
        if (p < 0) { ++malformed; continue; }
        if (!seen.insert({p, i}).second) ++duplicates;
        if (i <= lastIndex[p]) ++reordered;
        lastIndex[p] = i;
        // A drop takes no sequence number, so arrivals are contiguous: a gap is a lost slot write.
        if (m.seq != lastSeq + 1) ++seqGaps;
        lastSeq = m.seq;
    }

    CHECK(malformed == 0);
    CHECK(duplicates == 0);
    CHECK(reordered == 0);
    CHECK(seqGaps == 0);
    // Overflow is allowed (a full ring drops and counts), but nothing may vanish uncounted.
    CHECK(received.size() + stream.droppedCount() == static_cast<std::size_t>(kProducers * kPerProducer));
    // Nothing landing would make every check above vacuous. Deliberately no tighter threshold: the drop rate depends on
    // the scheduler, and a rate bound would be a flaky test.
    CHECK(received.size() > kRing);
}

TEST_CASE("a full ring drops the incoming message and counts it, and what did land is intact") {
    Machine             machine;
    MidiStream          stream(machine.backend);
    REQUIRE(stream.open());
    const std::size_t overflow = 40;
    for (std::size_t i = 0; i < kRing + overflow; ++i) {
        const std::vector<std::uint8_t> m = bytes(0x90, static_cast<int>(i & 0x7F), 0x40);
        stream.injectForTest(m.data(), m.size());
    }
    // The ring keeps one slot free for its full/empty distinction, so kRing - 1 land and the rest are dropped and
    // counted.
    CHECK(stream.droppedCount() == kRing + overflow - (kRing - 1));
    std::vector<MidiStream::Message> got;
    stream.poll(got);
    CHECK(got.size() == kRing - 1);
    CHECK(got.front().bytes == bytes(0x90, 0x00, 0x40));
    CHECK(got.back().bytes == bytes(0x90, static_cast<int>((kRing - 2) & 0x7F), 0x40));
    // And it keeps working afterwards.
    stream.injectForTest(bytes(0x90, 0x40, 0x7F).data(), 3);
    stream.poll(got);
    REQUIRE(got.size() == 1);
    CHECK(got[0].bytes == bytes(0x90, 0x40, 0x7F));
    CHECK(got[0].seq == kRing);  // a drop takes no sequence number: arrivals are contiguous
}

// ---- the output set's swap ------------------------------------------------------------------------------------

TEST_CASE("an output swap under a send in flight: every send lands on one live port, and none reaches a freed one") {
    // The hazard the epoch handshake exists for (design 4.3): send() runs on the audio thread and calls a port the
    // control thread is replacing. The ports are freed only once the send in flight has been waited out, so no send
    // reaches a port the stream has let go, and none is lost. The fake counts both, so the failure is a number rather
    // than only a crash - and this case runs in build-tsan/ as well.
    Machine          machine;
    FakeMidiBackend& backend = machine.backend;
    const rp::midi::OutputMode mode = GENERATE(rp::midi::OutputMode::Inline, rp::midi::OutputMode::Threaded);
    // Inline: a write that takes a moment, as a WinMM bulk SysEx does, so the sender is still inside one under most of
    // the swaps. Threaded: no delay, because the pushes only have to reach the ring (the subject is push against retire
    // and flush, not a slow port) and a millisecond per send is the platform's sleep granularity off Linux (Windows' is
    // about 15.6 ms), not a millisecond there.
    backend.sendDelayMs  = mode == rp::midi::OutputMode::Inline ? 1 : 0;
    backend.virtualPorts = false;  // the swap is the hardware set's: no virtual output to count as well
    MidiStream stream(backend, mode);
    stream.setOutputs({"Synth out"});  // an output is selected before the audio thread starts, as it is in a run
    REQUIRE(stream.open());
    constexpr int kSends = 300;
    std::atomic<bool> done{false};
    std::thread audio([&] {
        const std::uint8_t note[3] = {0x90, 0x3C, 0x7F};
        for (int i = 0; i < kSends; ++i) {
            stream.send(note, 3);
            // Threaded: the pushes are otherwise microseconds and would all be over before the control thread's second
            // swap, so the count below would rest on a thread-start race. Pacing the sender makes the swaps land under
            // it, which is what the case is about; inline, the fake's own write already paces it.
            if (mode == rp::midi::OutputMode::Threaded)
                std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
        done.store(true);
    });
    int swaps = 0;
    while (!done.load(std::memory_order_acquire)) {
        stream.setOutputs(swaps % 2 == 0 ? std::vector<std::string>{"Synth out"}
                                         : std::vector<std::string>{"Interface out"});
        ++swaps;
    }
    audio.join();
    // Threaded: every push returned before its writer wrote, so the counts are read once the last writer has drained.
    REQUIRE(settled(backend, kSends));
    // Both modes: many swaps landed while the sender was sending (inline it is inside a fake write, threaded the pushes
    // are paced), and every send still reached exactly one live port - no send was lost and none reached a freed one.
    CHECK(swaps > 2);
    CHECK(backend.sendsStarted.load() == kSends);   // every send reached a port: none went to no output at all
    CHECK(backend.sendsFinished.load() == kSends);
    CHECK(backend.sendsAfterClose() == 0);
    CHECK(backend.totalReceived() == static_cast<std::size_t>(kSends));
}

TEST_CASE("a diff closes what it drops before it opens what it adds: a renamed device is not refused") {
    // WinMM exclusivity is per DEVICE and a re-resolve lands on the same device under a new index ("Synth 1" to
    // "Synth 2"): opening the new name while the old handle is still held is refused, and the device stays lost until a
    // tap. The add is tried once with the old handle held (opening first is what keeps every send on a live port), and
    // on that refusal the drop is let go and the add retried - so the close precedes the second, successful open.
    Machine          machine;
    FakeMidiBackend& backend = machine.backend;
    backend.outputs          = {"Synth 1", "Synth 2"};
    backend.exclusivePorts   = true;
    backend.deviceOf         = {{"Synth 1", "synth"}, {"Synth 2", "synth"}};  // one device, two enumerations
    const rp::midi::OutputMode mode = GENERATE(rp::midi::OutputMode::Inline, rp::midi::OutputMode::Threaded);
    MidiStream stream(backend, mode);
    stream.setOutputs({"Synth 1"});
    REQUIRE(stream.open());
    backend.events.clear();
    stream.setOutputs({"Synth 2"});
    const auto at = [&](const std::string& event) {
        const auto it = std::find(backend.events.begin(), backend.events.end(), event);
        REQUIRE(it != backend.events.end());
        return static_cast<std::size_t>(it - backend.events.begin());
    };
    CHECK(at("close out:Synth 1") < at("open out:Synth 2"));
    CHECK(stream.openedOutputs() == std::vector<std::string>{"Synth 2"});
}

TEST_CASE("a swap behind back-to-back long sends waits a bounded time: one send inline, the flush's deadline threaded") {
    // A WinMM bulk SysEx blocks about 170 ms inside send(), and a block may send several. INLINE: the wait must be
    // bounded by the ONE send in flight - the set the swap published is what the next send reads - so a reconfigure
    // waits one send, not the stream of them. Waiting "while the epoch is odd" instead chases every send that follows:
    // this case measured hundreds of milliseconds on a 32-thread box and never returned under `taskset -c 0` (the swap
    // spins out the whole run). The bound is a quarter of the run, loose enough for a loaded runner and tight enough to
    // fail that wait. THREADED: the wait is the writer's stop, which flushes what the ring holds up to kFlushDeadline and
    // discards the rest - bounded the same way, by a constant rather than by the backlog's length.
    const rp::midi::OutputMode mode = GENERATE(rp::midi::OutputMode::Inline, rp::midi::OutputMode::Threaded);
    Machine          machine;
    FakeMidiBackend& backend = machine.backend;
    backend.sendDelayMs      = 20;
    backend.virtualPorts     = false;  // the swap is the hardware set's
    MidiStream stream(backend, mode);
    stream.setOutputs({"Synth out"});
    REQUIRE(stream.open());
    constexpr int kSends = 40;  // 800 ms of unbroken sends, which is the backlog a flush has to abandon
    std::thread   audio([&] {
        const std::uint8_t note[3] = {0x90, 0x3C, 0x7F};
        for (int i = 0; i < kSends; ++i) stream.send(note, 3);
    });
    while (backend.sendsStarted.load() == 0) std::this_thread::yield();  // a send is in flight as the swap lands
    const auto start = std::chrono::steady_clock::now();
    stream.setOutputs({"Interface out"});
    const auto waited =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    audio.join();
    // Inline: one in-flight send plus slack, a quarter of the run (ten sends' time), because a ThreadSanitizer run inside
    // the parallel kit, or a loaded macOS runner, stretches a 20 ms send and the scheduling around it (a CI run measured
    // 92 ms against the four sends this was). Chasing the run would wait out most of its 800 ms. Threaded: the writer's
    // flush deadline plus the send it was already inside, the same invariant with a constant bound.
    if (mode == rp::midi::OutputMode::Inline) {
        CHECK(waited < kSends * backend.sendDelayMs / 4);
        CHECK(backend.totalReceived() == static_cast<std::size_t>(kSends));
    } else {
        // The bound is the flush's deadline plus the sends it can still be inside, and it has to be TIGHTER than the
        // backlog to mean anything: a writer with no deadline at all sends all 800 ms of it and fails this.
        CHECK(waited < rp::midi::PortWriter::kFlushDeadline.count() + 500);
        CHECK(backend.totalReceived() >= 1);  // the flush sent what the deadline allowed, and discarded the rest
        CHECK(backend.totalReceived() < static_cast<std::size_t>(kSends));
    }
    CHECK(backend.sendsAfterClose() == 0);
}

TEST_CASE("every message a delivery thread queued is delivered before its port's destructor returns") {
    // The MidiPort contract (midi/MidiBackend.hpp) is "no receiver call after the destructor returns", and it is what
    // lets a selection change close inputs under a running stream: the fake delivers on a thread of its own here, as
    // RtMidi does, and a swap must lose none of what was queued for a port it lets go. Run in build-tsan/ too.
    Machine          machine;
    FakeMidiBackend& backend = machine.backend;
    backend.threadedDelivery = true;
    MidiStream stream(backend);
    stream.setInputs({"Keys in", "Pads in"}, false);
    REQUIRE(stream.open());
    constexpr int kEach = 50;
    for (int i = 0; i < kEach; ++i) {
        backend.deliver("Keys in", bytes(0x90, i & 0x7F, 0x40));
        backend.deliver("Pads in", bytes(0xB0, i & 0x7F, 0x10));
    }
    // The swap closes "Pads in", joining its delivery thread before it is gone; "Keys in" STAYS (a diff), so its own
    // thread is still delivering. Poll until the kept port's queue has caught up: the port that was let go owes every
    // message its destructor's join drained, and the one that stayed simply delivers asynchronously, as RtMidi does.
    stream.setInputs({"Keys in"}, false);
    std::vector<MidiStream::Message> got;
    std::vector<MidiStream::Message> batch;
    for (int tries = 0; tries < 1000 && got.size() < 2 * kEach; ++tries) {
        if (tries > 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        batch.clear();
        stream.poll(batch);
        got.insert(got.end(), batch.begin(), batch.end());
    }
    CHECK(got.size() == 2 * kEach);
    CHECK(stream.droppedCount() == 0);
    // Same-thread delivery, the other mode, on the port that stayed: unchanged.
    backend.deliver("Keys in", bytes(0x90, 0x3C, 0x7F));
    got.clear();
    for (int tries = 0; tries < 1000 && got.empty(); ++tries) {  // the port that stayed delivers on its own thread too
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        stream.poll(got);
    }
    REQUIRE(got.size() == 1);
    CHECK(got[0].bytes == bytes(0x90, 0x3C, 0x7F));
}

TEST_CASE("the log lines the standalone's harnesses grep, byte for byte") {
    // tools/run-sdl-controller.sh greps the reservation line and the virtual-ports line; the smoke greps the rest of
    // the shape. The stream writes them through one sink, so a case can hold the very bytes (and the "[retroplug-sdl] "
    // prefix the app has always used, which the CLI prints too).
    Machine          machine;
    FakeMidiBackend& backend = machine.backend;
    MidiStream       stream(backend);
    std::vector<std::string> lines;
    stream.setLogSink([&](const std::string& line) { lines.push_back(line); });
    stream.setOutputs({"Synth out", "Nope"});  // "Nope" is not on this machine: the picker's stale name
    REQUIRE(stream.open());
    stream.setInputs({"Keys in"}, false);
    stream.setReserved("Keys in", "Synth out");
    stream.setHardwareInputsSuspended(true);
    stream.setHardwareInputsSuspended(false);
    const std::vector<std::string> expected = {
        "[retroplug-sdl] MIDI: virtual ports 'RetroPlug In' / 'RetroPlug Out' open",
        "[retroplug-sdl] MIDI in: no hardware device selected (Settings > MIDI)",
        "[retroplug-sdl] MIDI out: selected port 'Nope' not present",
        "[retroplug-sdl] MIDI out: connected hardware port 'Synth out'",
        "[retroplug-sdl] MIDI in: connected hardware port 'Keys in'",
        "[retroplug-sdl] MIDI out: 'Synth out' is held by the control surface",
        "[retroplug-sdl] MIDI out: selected port 'Nope' not present",
        "[retroplug-sdl] MIDI in: hardware inputs suspended for a control-surface scan",
        // Resumed with its one port still claimed by the surface: the selection is re-applied, and nothing opens.
        "[retroplug-sdl] MIDI in: hardware inputs resumed",
    };
    CHECK(lines == expected);
}

TEST_CASE("send() has ONE caller, and what two would break") {
    // send() marks a send in flight by making the epoch odd on entry and even on exit, and a swap waits that one send
    // out. A SECOND caller breaks the mark: A's entry makes it odd, B's entry makes it EVEN again while A is still
    // inside a port, and a swap that lands between them publishes and retires at once - the port A is writing is freed
    // under it (two senders, 200 swaps, 2 sends reaching a closed port in 20 runs, the probe that found it), which is why
    // send() asserts the entry found the counter even (an assert, so a Debug build fails loudly;
    // the release build carries none). A block sends sequentially, which is the caller this stream is written for.
    Machine          machine;
    FakeMidiBackend& backend = machine.backend;
    MidiStream       stream(backend);
    stream.setOutputs({"Synth out"});
    REQUIRE(stream.open());
    const std::uint8_t note[3] = {0x90, 0x3C, 0x7F};
    for (int i = 0; i < 3; ++i) stream.send(note, 3);  // one thread, one send at a time: the odd/even parity holds
    CHECK(backend.sentTo("Synth out").size() == 3);
    CHECK(backend.sendsAfterClose() == 0);
}

// ---- the backend's own answers, and the filter ---------------------------------------------------------------

TEST_CASE("a backend with no virtual ports opens none, and announces none") {
    // WinMM's openVirtualPort prints a warning and returns, so the old stream announced two ports that did not exist
    // and left the user without the "RetroPlug In/Out" a DAW connects to, silently. supportsVirtual() says so instead.
    Machine          machine;
    FakeMidiBackend& backend = machine.backend;
    backend.virtualPorts     = false;
    MidiStream       stream(backend);
    REQUIRE(stream.open());
    CHECK_FALSE(stream.supportsVirtual());
    CHECK(opensOf(backend).empty());  // no virtual pair, no hardware input, no output
    // The stream still opens the hardware the selection names.
    stream.setInputs({"Keys in"}, false);
    CHECK(opensOf(backend) == std::vector<std::string>{"open in:Keys in"});
}

TEST_CASE("a refused open is one exception, skipped, and the rest of the list opens") {
    Machine          machine;
    FakeMidiBackend& backend = machine.backend;
    backend.refuse.insert("Interface out");
    backend.refuse.insert("Pads in");
    MidiStream stream(backend);
    stream.setInputs({"Keys in", "Pads in"}, false);
    stream.setOutputs({"Interface out", "Synth out"});
    REQUIRE(stream.open());  // a refused PORT is not a refused stream
    CHECK(backend.listening.count("Keys in") == 1);
    CHECK(backend.listening.count("Pads in") == 0);
    const std::uint8_t note[3] = {0x90, 0x3C, 0x7F};
    stream.send(note, 3);
    CHECK(backend.sentTo("Synth out").size() == 1);   // the port that did open still gets the stream
    CHECK(backend.sentTo("Interface out").empty());
}

TEST_CASE("a backend that cannot be used at all answers false and opens nothing") {
    Machine          machine;
    FakeMidiBackend& backend = machine.backend;
    backend.usableFlag       = false;
    MidiStream stream(backend);
    stream.setInputs({}, true);
    CHECK_FALSE(stream.open());
    CHECK_FALSE(stream.isOpen());
    CHECK(backend.records() == 0);
}

TEST_CASE("the allow list hides a port from the stream's list and from its opens") {
    PortAllowList allow("RPFake");
    FakeMidiBackend backend;
    backend.inputs  = {"RPFake keys", "Zoom L6:1 20:0"};
    backend.outputs = {"RPFake synth", "Zoom L6:1 20:1"};
    MidiStream stream(backend);
    stream.setInputs({}, true);  // all devices, which is the case that would otherwise open everything the machine has
    stream.setOutputs({"Zoom L6:1 20:1"});
    REQUIRE(stream.open());
    CHECK(stream.listInputs() == std::vector<std::string>{"RPFake keys"});
    CHECK(stream.listOutputs() == std::vector<std::string>{"RPFake synth"});
    CHECK(backend.listening.size() == 1);
    CHECK(backend.listening.count("RPFake keys") == 1);
    CHECK(backend.listening.count("Zoom L6:1 20:0") == 0);
    CHECK(backend.totalReceived() == 0);  // the filtered output never even opened
}

// ---- the stream in Threaded mode ------------------------------------------------------------------------------
//
// What the audio callback sees when the stream writes from a thread of its own: a send that returns before the port has
// written, the per-message dump written by the writer rather than the callback, a swap that FLUSHES a backlog before the
// port goes, and drops counted per port. The cases that pin a contract the two modes share run in BOTH (a GENERATE at
// the top of each, reading what a port received only once its writer has drained), so the concurrency cases the design
// asks to hold in both modes - the exclusive port's re-pick, a close under a send in flight, an output swap under a
// send, the diff that closes before it opens, and a swap behind back-to-back sends - are what the ThreadSanitizer leg
// runs in Threaded mode too.

TEST_CASE("a threaded send returns while the port is still writing, and every byte arrives in order") {
    // The SDL standalone's audio callback: it drains the Engine's MIDI out once per block, and a WinMM SysEx write waits
    // for the bytes to leave the wire (about 170 ms for a 538-byte repaint at DIN rate). The callback queues and moves
    // on; the writer thread does the waiting.
    Machine          machine;
    FakeMidiBackend& backend = machine.backend;
    backend.sendDelayMs      = 200;
    // The virtual output is INLINE in both modes, so a delay on it is the caller's: it is not what this case is about.
    backend.virtualPorts = false;
    MidiStream stream(backend, rp::midi::OutputMode::Threaded);
    stream.setOutputs({"Synth out"});
    REQUIRE(stream.open());
    const std::vector<std::uint8_t> sysex(538, 0xF0);
    const std::uint8_t              note[3] = {0x90, 0x3C, 0x7F};
    const auto                      t0      = std::chrono::steady_clock::now();
    stream.send(sysex.data(), sysex.size());
    for (int i = 0; i < 3; ++i) stream.send(note, 3);
    CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(150));  // it waited for no write at all
    // And the port receives them whole, in the order the block sent them.
    REQUIRE(settled(backend, 4));
    const std::vector<std::vector<std::uint8_t>> sent = backend.sentTo("Synth out");
    REQUIRE(sent.size() == 4);
    CHECK(sent[0] == sysex);
    CHECK(sent[1] == std::vector<std::uint8_t>(note, note + 3));
    CHECK(stream.droppedOutputCount() == 0);
    stream.close();
}

TEST_CASE("a threaded stream's dump is written by the writer, not the audio callback") {
    // RETROPLUG_MIDI_LOG's per-message line moved with the send: building it is an allocation, and send() runs on the
    // audio thread. The line still carries the prefix and the bytes a harness greps.
    Machine          machine;
    FakeMidiBackend& backend = machine.backend;
    backend.sendDelayMs      = 0;
    backend.virtualPorts     = false;
    // The sink runs on the WRITER's thread (that is the point: send() is the audio callback), so the case's own
    // collection is guarded: ThreadSanitizer runs this file too.
    std::mutex               linesMutex;
    std::vector<std::string> lines;
    auto                     linesCopy = [&] {
        std::lock_guard<std::mutex> lk(linesMutex);
        return lines;
    };
    const char*              was = std::getenv("RETROPLUG_MIDI_LOG");
    std::string              kept;
    const bool               had = was != nullptr;
    if (had) kept = was;
#if defined(_WIN32)
    _putenv_s("RETROPLUG_MIDI_LOG", "1");
#else
    setenv("RETROPLUG_MIDI_LOG", "1", 1);
#endif
    {
        MidiStream stream(backend, rp::midi::OutputMode::Threaded);
        stream.setLogSink([&](const std::string& line) {
            if (line.find("MIDI out (Synth out): 90") == std::string::npos) return;
            std::lock_guard<std::mutex> lk(linesMutex);
            lines.push_back(line);
        });
        stream.setOutputs({"Synth out"});
        REQUIRE(stream.open());
        const std::uint8_t note[3] = {0x90, 0x3C, 0x7F};
        stream.send(note, 3);
        REQUIRE(settled(backend, 1));
        // The writer's line, which names the port: a Threaded stream has one writer per output, and two ports sending
        // the same bytes is not something a dump should hide. The prefix and the bytes are what a harness greps.
        const std::vector<std::string> got = linesCopy();
        REQUIRE(got.size() == 1);
        CHECK(got[0] == "[retroplug-sdl] MIDI out (Synth out): 90 3C 7F ");
        stream.close();
    }
#if defined(_WIN32)
    _putenv_s("RETROPLUG_MIDI_LOG", had ? kept.c_str() : "");
#else
    if (had) setenv("RETROPLUG_MIDI_LOG", kept.c_str(), 1);
    else unsetenv("RETROPLUG_MIDI_LOG");
#endif
}

TEST_CASE("a threaded stream's swap with a backlog flushes it before the port closes, and sends nothing after") {
    // A port leaving the selection (a project load, a pick, the stream closing) stops its writer with a flush: a dropped
    // Note Off is a stuck note. Nothing reaches the port after it closes, which is what the stream's epoch handshake and
    // the writer's stop are ordered to guarantee.
    Machine          machine;
    FakeMidiBackend& backend = machine.backend;
    backend.sendDelayMs      = 20;
    backend.virtualPorts     = false;  // the hardware set's flush is the subject; the virtual send stays inline
    MidiStream stream(backend, rp::midi::OutputMode::Threaded);
    stream.setOutputs({"Synth out"});
    REQUIRE(stream.open());
    const std::uint8_t note[3]  = {0x90, 0x3C, 0x7F};
    constexpr int      kQueued  = 8;
    for (int i = 0; i < kQueued; ++i) stream.send(note, 3);
    stream.setOutputs({});  // the port leaves: published away, waited out, flushed, joined, closed
    CHECK(backend.sentTo("Synth out").size() == kQueued);
    CHECK(backend.sendsAfterClose() == 0);
    // The stream is still usable, and no hardware port is left to receive: the next message goes nowhere on the wire.
    CHECK(stream.openedOutputs().empty());
    stream.send(note, 3);
    CHECK(settled(backend, kQueued));  // nothing more is written: the count stands
    CHECK(backend.sentTo("Synth out").size() == kQueued);
    stream.close();
}

TEST_CASE("a Threaded stream's send allocates nothing on the audio callback") {
    // The design's case is send()'s, not push()'s: the audio callback calls MidiStream::send, so that is where an
    // allocation would matter (PortWriter.test.cpp counts push's own, and the count is the same global operator new,
    // armed for this thread alone, so the writer's own allocations are not measured).
    Machine          machine;
    FakeMidiBackend& backend = machine.backend;
    backend.sendDelayMs      = 50;  // the ring is being drained while the block's messages land
    backend.virtualPorts     = false;
    MidiStream stream(backend, rp::midi::OutputMode::Threaded);
    stream.setOutputs({"Synth out"});
    REQUIRE(stream.open());
    const std::vector<std::uint8_t> sysex(538, 0xF0);
    const std::uint8_t              note[3] = {0x90, 0x3C, 0x7F};
    {
        rp::midi::test::AllocationCount count;
        for (int i = 0; i < 64; ++i) {
            stream.send(sysex.data(), sysex.size());
            stream.send(note, 3);
        }
        const bool quiet = count.value() == 0;
        CHECK(quiet);
    }
    stream.close();
}

TEST_CASE("a threaded stream counts the messages a full ring drops, and says so when the writer goes") {
    // The count is per port and read on the control thread, as droppedCount() is for the receive ring: a port that is
    // not draining must not make a lost message invisible. The stream itself is a reader - it reports what a writer
    // dropped just before letting it go, because the count dies with the writer - so the case holds that line too.
    Machine          machine;
    FakeMidiBackend& backend = machine.backend;
    backend.sendDelayMs      = 300;
    backend.virtualPorts     = false;
    MidiStream               stream(backend, rp::midi::OutputMode::Threaded);
    std::mutex               linesMutex;
    std::vector<std::string> lines;
    stream.setLogSink([&](const std::string& line) {
        std::lock_guard<std::mutex> lk(linesMutex);
        lines.push_back(line);
    });
    stream.setOutputs({"Synth out"});
    REQUIRE(stream.open());
    const std::vector<std::uint8_t> repaint(1024, 0xF0);  // a big message, so a few fills the ring
    for (int i = 0; i < 400; ++i) stream.send(repaint.data(), repaint.size());
    CHECK(stream.droppedOutputCount() > 0);
    stream.close();
    bool said = false;
    {
        std::lock_guard<std::mutex> lk(linesMutex);
        for (const std::string& line : lines)
            if (line.find("MIDI out (Synth out): ") != std::string::npos &&
                line.find(" message(s) dropped") != std::string::npos)
                said = true;
    }
    CHECK(said);
}

TEST_CASE("a threaded stream's close reports the messages its flush abandoned on a wedged port") {
    // The writer counts what a flush's deadline leaves in the ring only as it stops, so the stream has to stop it
    // BEFORE it reads the count: read first, the line was missing exactly the Note Offs the close gave up on. Nothing
    // here is refused by the ring (a handful of small messages), so every message the line counts is one the flush
    // abandoned, and the number is exact.
    Machine          machine;
    FakeMidiBackend& backend = machine.backend;
    backend.sendDelayMs      = 200;  // a wedged port: eight writes would take 1.6 s, the flush gives up after 0.5 s
    backend.virtualPorts     = false;
    MidiStream               stream(backend, rp::midi::OutputMode::Threaded);
    std::mutex               linesMutex;
    std::vector<std::string> lines;
    stream.setLogSink([&](const std::string& line) {
        std::lock_guard<std::mutex> lk(linesMutex);
        lines.push_back(line);
    });
    stream.setOutputs({"Synth out"});
    REQUIRE(stream.open());
    const std::uint8_t note[3] = {0x80, 0x3C, 0x00};
    constexpr int      kSent   = 8;
    for (int i = 0; i < kSent; ++i) stream.send(note, 3);
    CHECK(stream.droppedOutputCount() == 0);  // the ring took every one
    stream.close();
    const std::size_t written = backend.sentTo("Synth out").size();
    REQUIRE(written < static_cast<std::size_t>(kSent));  // the deadline did run out
    const std::string want =
        "[retroplug-sdl] MIDI out (Synth out): " + std::to_string(kSent - written) + " message(s) dropped";
    std::vector<std::string> got;
    {
        std::lock_guard<std::mutex> lk(linesMutex);
        got = lines;
    }
    INFO("the line: " << want << "; lines: " << got.size());
    CHECK(std::find(got.begin(), got.end(), want) != got.end());
}
