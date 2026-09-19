// Guards the control-surface device link (the instance menu's Launchpad submenu) without hardware. A fake
// IMidiPort records everything written and hands back a receiver the test can fire, so the whole lifecycle -
// connect, both rings, the farewell, launchpad.cfg - is exercised with no rtmidi and no MIDI system.
//
// The farewell cases are the ones that matter most. Programmer mode locks the device's own Settings menu, so
// a host that closes its port without replaying the exit message strands the user's hardware in a state the
// front panel cannot escape. It has to go out on BOTH paths: an explicit disconnect, and destruction.
//
// The scan cases at the bottom guard LaunchpadScanner, which is what decides whether that submenu appears at
// all. Both layers here are deliberately ignorant of the Launchpad protocol: TS hands down the probe bytes
// and reads the answers, native only carries them, so these tests assert on ROUTING (which port was written
// to, which port answered) and never on what any byte means.

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "host/launchpad/LaunchpadHost.hpp"
#include "host/launchpad/LaunchpadLink.hpp"
#include "host/launchpad/LaunchpadScanner.hpp"

using retroplug::IMidiPort;
using retroplug::LaunchpadConfigDto;
using retroplug::LaunchpadHost;
using retroplug::LaunchpadLink;
using retroplug::LaunchpadScanner;
using retroplug::LaunchpadScanStatusDto;

namespace {

// What the fake port saw, kept OUTSIDE the port so it survives the port's destruction (the farewell is
// written moments before the port is dropped, and a test has to be able to read it afterwards).
struct PortLog {
    std::vector<std::vector<std::uint8_t>> written;
    IMidiPort::Receiver                    receiver;  // fire it to simulate the device sending us something
    std::string                            inName, outName;
    int                                    opens = 0, closes = 0;
};

struct FakePort final : IMidiPort {
    explicit FakePort(PortLog& log) : log_(log) { log_.opens++; }
    ~FakePort() override { log_.closes++; log_.receiver = nullptr; }
    void send(const std::uint8_t* data, std::size_t n) override { log_.written.emplace_back(data, data + n); }
    PortLog& log_;
};

LaunchpadLink::PortFactory factoryFor(PortLog& log) {
    return [&log](const std::string& in, const std::string& out,
                  IMidiPort::Receiver receiver) -> std::unique_ptr<IMidiPort> {
        log.inName   = in;
        log.outName  = out;
        log.receiver = std::move(receiver);
        return std::make_unique<FakePort>(log);
    };
}

/** A factory that refuses, the way RtMidi does when a named port is absent. */
LaunchpadLink::PortFactory failingFactory() {
    return [](const std::string&, const std::string&, IMidiPort::Receiver) -> std::unique_ptr<IMidiPort> {
        throw std::runtime_error("MIDI input port not found: Nope");
    };
}

LaunchpadHost::PortLister listerWith(std::vector<std::string> inputs, std::vector<std::string> outputs) {
    return [inputs, outputs](bool input) { return input ? inputs : outputs; };
}

/** For the cases that are not about scanning: a host whose scanner can never open anything. */
LaunchpadScanner::OpenFn noOpener() {
    return [](bool, const std::string& name, IMidiPort::Receiver) -> std::unique_ptr<IMidiPort> {
        throw std::runtime_error("no MIDI system: " + name);
    };
}

// A scriptable rig of MIDI ports for the scanner: which output answers, which INPUT its answer comes back
// on (deliberately a different port - that pairing is the thing a scan exists to discover), and which ports
// refuse to open at all. An answer is delivered synchronously from the probing send, which is where a real
// device's would arrive from too, just on another thread.
struct ScanRig {
    struct Wire {
        std::string               input;          // the port the answer comes back on
        std::vector<std::uint8_t> reply;
        bool                      onOpen = false;  // answer when the port OPENS, i.e. before any probe window
    };

    std::map<std::string, Wire>                wires;      // output port -> what answers on it
    std::set<std::string>                      refuse;     // ports that will not open
    std::vector<std::string>                   probed;     // outputs written to, in order
    std::vector<std::vector<std::uint8_t>>     sent;       // the bytes as they went out
    std::map<std::string, IMidiPort::Receiver> listening;  // currently-open inputs, by name
    std::mutex                                 m;

    void deliver(const Wire& w) {
        IMidiPort::Receiver r;
        {
            std::lock_guard<std::mutex> lk(m);
            auto it = listening.find(w.input);
            if (it == listening.end()) return;  // nobody is listening on that port
            r = it->second;
        }
        if (r && !w.reply.empty()) r(w.reply.data(), w.reply.size());
    }
};

struct ScanInPort final : IMidiPort {
    ScanInPort(ScanRig& rig, std::string name, Receiver r) : rig_(rig), name_(std::move(name)) {
        std::lock_guard<std::mutex> lk(rig_.m);
        rig_.listening[name_] = std::move(r);
    }
    ~ScanInPort() override {
        std::lock_guard<std::mutex> lk(rig_.m);
        rig_.listening.erase(name_);
    }
    void send(const std::uint8_t*, std::size_t) override {}
    ScanRig&    rig_;
    std::string name_;
};

struct ScanOutPort final : IMidiPort {
    ScanOutPort(ScanRig& rig, std::string name) : rig_(rig), name_(std::move(name)) {
        const ScanRig::Wire* w = nullptr;
        {
            std::lock_guard<std::mutex> lk(rig_.m);
            auto it = rig_.wires.find(name_);
            if (it != rig_.wires.end() && it->second.onOpen) w = &it->second;
        }
        if (w) rig_.deliver(*w);  // before the probe window opens - the scanner must not attribute this
    }
    void send(const std::uint8_t* data, std::size_t n) override {
        const ScanRig::Wire* w = nullptr;
        {
            std::lock_guard<std::mutex> lk(rig_.m);
            rig_.probed.push_back(name_);
            rig_.sent.emplace_back(data, data + n);
            auto it = rig_.wires.find(name_);
            if (it != rig_.wires.end() && !it->second.onOpen) w = &it->second;
        }
        if (w) rig_.deliver(*w);
    }
    ScanRig&    rig_;
    std::string name_;
};

LaunchpadScanner::OpenFn openerFor(ScanRig& rig) {
    return [&rig](bool input, const std::string& name, IMidiPort::Receiver r) -> std::unique_ptr<IMidiPort> {
        {
            std::lock_guard<std::mutex> lk(rig.m);
            if (rig.refuse.count(name)) throw std::runtime_error("port in use: " + name);
        }
        if (input) return std::make_unique<ScanInPort>(rig, name, std::move(r));
        return std::make_unique<ScanOutPort>(rig, name);
    };
}

/** Poll the way the UI does until the scan settles. Returns the final status; fails the test if it hangs. */
LaunchpadScanStatusDto waitForScan(LaunchpadHost& host) {
    for (int i = 0; i < 2000; ++i) {  // 10 s ceiling; a scan of a handful of ports is well under a second
        LaunchpadScanStatusDto s = host.scanStatus();
        if (!s.busy && s.done) return s;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    FAIL("scan never finished");
    return {};
}

// A Pro MK3's answer: F0 7E <dev> 06 02 <Novation 00 20 29> <family 13 01> 00 00 <4-byte version> F7.
const std::vector<std::uint8_t> kInquiryReply{0xF0, 0x7E, 0x00, 0x06, 0x02, 0x00, 0x20, 0x29, 0x13,
                                              0x01, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0xF7};
const std::vector<std::uint8_t> kInquiry{0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7};  // deviceInquiry()

std::string tempCfgDir() {
    const auto dir = std::filesystem::temp_directory_path() / "rp-launchpad-test";
    std::filesystem::create_directories(dir);
    std::filesystem::remove(dir / "launchpad.cfg");  // start clean
    return dir.string();
}

const std::vector<std::uint8_t> kFarewell{0xF0, 0x00, 0x20, 0x29, 0x02, 0x0E, 0x0E, 0x00, 0xF7};  // exitToLiveMode

}  // namespace

TEST_CASE("LaunchpadLink.connect claims the named pair, disconnect gives it back", "[launchpad]") {
    PortLog       log;
    LaunchpadLink link(factoryFor(log));

    REQUIRE_FALSE(link.isConnected());
    REQUIRE(link.connect("LPProMK3 MIDI in", "LPProMK3 MIDI out"));
    REQUIRE(link.isConnected());
    REQUIRE(log.opens == 1);
    REQUIRE(log.inName == "LPProMK3 MIDI in");
    REQUIRE(log.outName == "LPProMK3 MIDI out");

    link.disconnect();
    REQUIRE_FALSE(link.isConnected());
    REQUIRE(log.closes == 1);
}

TEST_CASE("LaunchpadLink.connect surfaces a refused port instead of pretending", "[launchpad]") {
    LaunchpadLink link(failingFactory());
    REQUIRE_FALSE(link.connect("Nope", "Nope"));
    REQUIRE_FALSE(link.isConnected());
    REQUIRE(link.lastError() == "MIDI input port not found: Nope");
}

TEST_CASE("LaunchpadLink replays the farewell on disconnect", "[launchpad]") {
    PortLog       log;
    LaunchpadLink link(factoryFor(log));
    link.setFarewell(kFarewell);
    REQUIRE(link.connect("in", "out"));
    REQUIRE(log.written.empty());

    link.disconnect();
    REQUIRE(log.written.size() == 1);
    REQUIRE(log.written[0] == kFarewell);  // the exact bytes TS handed down, unparsed
}

TEST_CASE("LaunchpadLink replays the farewell on DESTRUCT too", "[launchpad]") {
    PortLog log;
    {
        LaunchpadLink link(factoryFor(log));
        link.setFarewell(kFarewell);
        REQUIRE(link.connect("in", "out"));
    }  // no disconnect() call: a host that quits without one must not strand the device
    REQUIRE(log.written.size() == 1);
    REQUIRE(log.written[0] == kFarewell);
    REQUIRE(log.closes == 1);
}

TEST_CASE("LaunchpadLink keeps the farewell across a reconnect", "[launchpad]") {
    PortLog       log;
    LaunchpadLink link(factoryFor(log));
    link.setFarewell(kFarewell);
    REQUIRE(link.connect("in", "out"));
    link.disconnect();
    REQUIRE(link.connect("in", "out"));  // set once, honoured every time
    link.disconnect();
    REQUIRE(log.written.size() == 2);
    REQUIRE(log.written[1] == kFarewell);
}

TEST_CASE("LaunchpadLink: a message from the device reaches the audio-thread drain", "[launchpad]") {
    PortLog       log;
    LaunchpadLink link(factoryFor(log));
    REQUIRE(link.connect("in", "out"));
    REQUIRE(log.receiver);

    const std::uint8_t pad[3] = {0x90, 0x51, 0x7F};  // NoteOn, grid index 81 = top-left pad
    log.receiver(pad, 3);
    const std::uint8_t release[3] = {0x90, 0x51, 0x00};
    log.receiver(release, 3);

    std::vector<LaunchpadLink::Message> drained;
    link.drainInput(drained);
    REQUIRE(drained.size() == 2);
    REQUIRE(drained[0] == std::vector<std::uint8_t>{0x90, 0x51, 0x7F});
    REQUIRE(drained[1] == std::vector<std::uint8_t>{0x90, 0x51, 0x00});

    link.drainInput(drained);  // drained once, delivered once
    REQUIRE(drained.empty());
}

TEST_CASE("LaunchpadLink: queued LED traffic goes out on pump, not on push", "[launchpad]") {
    PortLog       log;
    LaunchpadLink link(factoryFor(log));
    REQUIRE(link.connect("in", "out"));

    const std::uint8_t led[3] = {0x90, 0x51, 0x15};
    link.pushOutput(led, 3);
    REQUIRE(log.written.empty());  // the audio thread only queues; the host's main loop writes

    link.pump();
    REQUIRE(log.written.size() == 1);
    REQUIRE(log.written[0] == std::vector<std::uint8_t>{0x90, 0x51, 0x15});
    REQUIRE(link.messagesSent() == 1);
}

TEST_CASE("LaunchpadLink drops rather than overruns an oversized message", "[launchpad]") {
    PortLog       log;
    LaunchpadLink link(factoryFor(log));
    REQUIRE(link.connect("in", "out"));

    // One byte past the worst case the protocol can produce (a 106-spec RGB bulk SysEx is 538).
    const std::vector<std::uint8_t> huge(LaunchpadLink::kMaxOutMessage + 1, 0x7F);
    link.pushOutput(huge.data(), huge.size());
    link.pump();
    REQUIRE(log.written.empty());
    REQUIRE(link.messagesDropped() == 1);

    // The exact maximum still fits.
    const std::vector<std::uint8_t> big(LaunchpadLink::kMaxOutMessage, 0x7F);
    link.pushOutput(big.data(), big.size());
    link.pump();
    REQUIRE(log.written.size() == 1);
    REQUIRE(log.written[0].size() == LaunchpadLink::kMaxOutMessage);
}

TEST_CASE("LaunchpadLink ignores output while disconnected", "[launchpad]") {
    PortLog       log;
    LaunchpadLink link(factoryFor(log));
    const std::uint8_t led[3] = {0x90, 0x51, 0x15};
    link.pushOutput(led, 3);
    link.pump();
    REQUIRE(log.written.empty());
}

TEST_CASE("LaunchpadHost connects only with BOTH ports chosen", "[launchpad]") {
    PortLog       log;
    LaunchpadHost host(factoryFor(log), noOpener(), listerWith({"LPProMK3 MIDI"}, {"LPProMK3 MIDI"}), tempCfgDir());

    host.connect(true);
    REQUIRE(host.getConfig().enabled);
    REQUIRE_FALSE(host.getConfig().connected);  // enabled but no ports: down, rather than guessing one

    host.setPorts("LPProMK3 MIDI", "LPProMK3 MIDI");
    REQUIRE(host.getConfig().connected);
    REQUIRE(host.reservedInputPort() == "LPProMK3 MIDI");

    host.connect(false);
    REQUIRE_FALSE(host.getConfig().connected);
    REQUIRE(host.reservedInputPort().empty());  // nothing claimed -> the shared MIDI stream gets it back
}

TEST_CASE("LaunchpadHost lists EVERY port, not just ones that look like a Launchpad", "[launchpad]") {
    PortLog log;
    // A Launchpad on TRS arrives through a MIDI interface, on a port named after the INTERFACE. Filtering
    // the list to a device-name hint would make exactly that setup unconfigurable.
    LaunchpadHost host(factoryFor(log), noOpener(), listerWith({"MIDISPORT 2x2 Port A", "LPProMK3 MIDI"}, {"MIDISPORT 2x2 Port A"}),
                       tempCfgDir());
    const LaunchpadConfigDto c = host.getConfig();
    REQUIRE(c.inputs.size() == 2);
    REQUIRE(c.inputs[0] == "MIDISPORT 2x2 Port A");
    REQUIRE(c.outputs.size() == 1);

    host.setPorts("MIDISPORT 2x2 Port A", "MIDISPORT 2x2 Port A");
    host.connect(true);
    REQUIRE(host.getConfig().connected);
}

TEST_CASE("LaunchpadHost round-trips launchpad.cfg and reclaims the pair on restore", "[launchpad]") {
    const std::string dir = tempCfgDir();
    PortLog           log;
    {
        LaunchpadHost host(factoryFor(log), noOpener(), listerWith({"in"}, {"out"}), dir);
        host.setPorts("in", "out");
        host.connect(true);
        REQUIRE(host.getConfig().connected);
    }

    PortLog       log2;
    LaunchpadHost restored(factoryFor(log2), noOpener(), listerWith({"in"}, {"out"}), dir);
    REQUIRE_FALSE(restored.getConfig().enabled);  // nothing read yet
    restored.restore();
    const LaunchpadConfigDto c = restored.getConfig();
    REQUIRE(c.enabled);
    REQUIRE(c.connected);
    REQUIRE(c.selectedInput == "in");
    REQUIRE(c.selectedOutput == "out");
}

TEST_CASE("LaunchpadHost fires onLinkChanged so the host can re-reserve the port", "[launchpad]") {
    PortLog       log;
    LaunchpadHost host(factoryFor(log), noOpener(), listerWith({"in"}, {"out"}), tempCfgDir());
    std::vector<std::string> reserved;
    host.setOnLinkChanged([&] { reserved.push_back(host.reservedInputPort()); });

    host.setPorts("in", "out");
    host.connect(true);
    host.connect(false);
    REQUIRE(reserved.size() == 3);
    REQUIRE(reserved[0].empty());  // ports set while disabled: still nothing claimed
    REQUIRE(reserved[1] == "in");  // connected: keep it out of the shared musical stream
    REQUIRE(reserved[2].empty());  // released
}

TEST_CASE("LaunchpadHost switches ports live, saying goodbye to the old device first", "[launchpad]") {
    PortLog       log;
    LaunchpadHost host(factoryFor(log), noOpener(), listerWith({"in", "in2"}, {"out", "out2"}), tempCfgDir());
    host.setFarewell(kFarewell);
    host.setPorts("in", "out");
    host.connect(true);
    REQUIRE(log.opens == 1);

    host.setPorts("in2", "out2");
    REQUIRE(log.opens == 2);
    REQUIRE(log.closes == 1);
    REQUIRE(log.written.size() == 1);
    REQUIRE(log.written[0] == kFarewell);  // the device we walked away from was released, not abandoned
    REQUIRE(host.reservedInputPort() == "in2");
}

// --- the scan (LaunchpadScanner) ---------------------------------------------------------------------
//
// What gates the Launchpad submenu. A surface on TRS/DIN arrives through somebody's MIDI interface, on a port
// named after the INTERFACE, and DIN offers no enumeration and no idle heartbeat - so the only way to know it
// is there is to ask, and the only way to learn WHICH INPUT belongs to the output you asked on is to see
// where the answer came back. Both halves are pinned here.

TEST_CASE("LaunchpadScanner probes every output and reports the PAIR that answered", "[launchpad]") {
    PortLog log;
    ScanRig rig;
    // The answer to a probe on "IF Port B" comes back on "IF Port A" - different port, same device. A scan
    // that only reported "something answered" would leave the user to guess this.
    rig.wires["IF Port B"] = { "IF Port A", kInquiryReply, false };
    LaunchpadHost host(factoryFor(log), openerFor(rig),
                       listerWith({"Keystep", "IF Port A"}, {"Keystep", "IF Port B"}), tempCfgDir());

    REQUIRE(host.scan(kInquiry, 5));
    const LaunchpadScanStatusDto s = waitForScan(host);

    REQUIRE(rig.probed == std::vector<std::string>{"Keystep", "IF Port B"});  // every output, in order
    REQUIRE(rig.sent.size() == 2);
    REQUIRE(rig.sent[0] == kInquiry);  // verbatim: native never builds or parses this
    REQUIRE(rig.sent[1] == kInquiry);
    REQUIRE(s.error.empty());
    REQUIRE(s.replies.size() == 1);
    REQUIRE(s.replies[0].output == "IF Port B");
    REQUIRE(s.replies[0].input == "IF Port A");
    REQUIRE(s.replies[0].bytes == kInquiryReply);
}

TEST_CASE("LaunchpadScanner steps over a port it cannot open, and counts it", "[launchpad]") {
    PortLog log;
    ScanRig rig;
    rig.refuse = { "Busy In", "Busy Out" };  // another application holds them (the normal Windows case)
    rig.wires["Good Out"] = { "Good In", kInquiryReply, false };
    LaunchpadHost host(factoryFor(log), openerFor(rig),
                       listerWith({"Busy In", "Good In"}, {"Busy Out", "Good Out"}), tempCfgDir());

    REQUIRE(host.scan(kInquiry, 5));
    const LaunchpadScanStatusDto s = waitForScan(host);

    REQUIRE(s.skipped == 2);
    REQUIRE(rig.probed == std::vector<std::string>{"Good Out"});
    REQUIRE(s.replies.size() == 1);  // a port in use costs that port, not the scan
    REQUIRE(s.error.empty());
}

TEST_CASE("LaunchpadScanner drops an arrival with no probe window open", "[launchpad]") {
    PortLog log;
    ScanRig rig;
    rig.wires["Out"] = { "In", kInquiryReply, true };  // fires as the port OPENS, before the probe goes out
    LaunchpadHost host(factoryFor(log), openerFor(rig), listerWith({"In"}, {"Out"}), tempCfgDir());

    REQUIRE(host.scan(kInquiry, 5));
    const LaunchpadScanStatusDto s = waitForScan(host);

    // Unattributable, so dropped. Tagging it with whichever output happened to be next would record a pair
    // whose LED traffic goes somewhere else entirely.
    REQUIRE(s.replies.empty());
}

TEST_CASE("LaunchpadScanner ignores System Real-Time noise", "[launchpad]") {
    PortLog log;
    ScanRig rig;
    rig.wires["Out"] = { "In", { 0xF8 }, false };  // a clock pulse from something with a sequencer running
    LaunchpadHost host(factoryFor(log), openerFor(rig), listerWith({"In"}, {"Out"}), tempCfgDir());

    REQUIRE(host.scan(kInquiry, 5));
    REQUIRE(waitForScan(host).replies.empty());
}

TEST_CASE("LaunchpadHost refuses a scan while the link holds the ports", "[launchpad]") {
    PortLog log;
    ScanRig rig;
    LaunchpadHost host(factoryFor(log), openerFor(rig), listerWith({"in"}, {"out"}), tempCfgDir());
    host.setPorts("in", "out");
    host.connect(true);
    REQUIRE(host.link().isConnected());

    // Those ports cannot be reopened while they are claimed, and a connected device needs no finding.
    REQUIRE_FALSE(host.scan(kInquiry, 5));
    REQUIRE(rig.probed.empty());
}

TEST_CASE("LaunchpadHost runs one scan at a time", "[launchpad]") {
    PortLog log;
    ScanRig rig;
    LaunchpadHost host(factoryFor(log), openerFor(rig), listerWith({"in"}, {"out"}), tempCfgDir());

    REQUIRE(host.scan(kInquiry, 200));   // still inside its answer window when the second asks
    REQUIRE_FALSE(host.scan(kInquiry, 200));
    waitForScan(host);
}

TEST_CASE("LaunchpadHost brackets a scan with onScanBusy so the host can free its MIDI inputs", "[launchpad]") {
    PortLog           log;
    ScanRig           rig;
    std::vector<bool> edges;
    LaunchpadHost     host(factoryFor(log), openerFor(rig), listerWith({"in"}, {"out"}), tempCfgDir());
    host.setOnScanBusy([&edges](bool busy) { edges.push_back(busy); });

    REQUIRE(host.scan(kInquiry, 5));
    REQUIRE(edges == std::vector<bool>{true});  // raised before the first port is opened
    waitForScan(host);
    // The false edge comes from the UI-thread poll, not the worker: the host stops audio to re-apply its
    // input selection, which is not something to do from a background thread.
    REQUIRE(edges == std::vector<bool>{true, false});
}

TEST_CASE("LaunchpadHost round-trips the scanned device label, and reads a file written without one",
          "[launchpad]") {
    PortLog           log;
    const std::string dir = tempCfgDir();
    {
        LaunchpadHost host(factoryFor(log), noOpener(), listerWith({"in"}, {"out"}), dir);
        host.setPorts("in", "out");
        host.setDeviceLabel("Launchpad Pro [MK3]");
    }
    PortLog       log2;
    LaunchpadHost restored(factoryFor(log2), noOpener(), listerWith({"in"}, {"out"}), dir);
    restored.restore();
    REQUIRE(restored.getConfig().deviceLabel == "Launchpad Pro [MK3]");

    // A cfg from a build that predates the label is three lines; the fields before it must still land.
    if (std::FILE* f = std::fopen((dir + "/launchpad.cfg").c_str(), "w")) {
        std::fprintf(f, "old in\nold out\n1\n");
        std::fclose(f);
    }
    PortLog       log3;
    LaunchpadHost old(factoryFor(log3), noOpener(), listerWith({"old in"}, {"old out"}), dir);
    old.restore();
    const LaunchpadConfigDto c = old.getConfig();
    REQUIRE(c.selectedInput == "old in");
    REQUIRE(c.selectedOutput == "old out");
    REQUIRE(c.enabled);
    REQUIRE(c.deviceLabel.empty());
}
