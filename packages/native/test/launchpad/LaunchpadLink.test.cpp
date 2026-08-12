// Guards the control-surface device link (the instance menu's Launchpad submenu) without hardware. A fake
// IMidiPort records everything written and hands back a receiver the test can fire, so the whole lifecycle -
// connect, both rings, the farewell, launchpad.cfg - is exercised with no rtmidi and no MIDI system.
//
// The farewell cases are the ones that matter most. Programmer mode locks the device's own Settings menu, so
// a host that closes its port without replaying the exit message strands the user's hardware in a state the
// front panel cannot escape. It has to go out on BOTH paths: an explicit disconnect, and destruction.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "host/launchpad/LaunchpadHost.hpp"
#include "host/launchpad/LaunchpadLink.hpp"

using retroplug::IMidiPort;
using retroplug::LaunchpadConfigDto;
using retroplug::LaunchpadHost;
using retroplug::LaunchpadLink;

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
    LaunchpadHost host(factoryFor(log), listerWith({"LPProMK3 MIDI"}, {"LPProMK3 MIDI"}), tempCfgDir());

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
    LaunchpadHost host(factoryFor(log), listerWith({"MIDISPORT 2x2 Port A", "LPProMK3 MIDI"}, {"MIDISPORT 2x2 Port A"}),
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
        LaunchpadHost host(factoryFor(log), listerWith({"in"}, {"out"}), dir);
        host.setPorts("in", "out");
        host.connect(true);
        REQUIRE(host.getConfig().connected);
    }

    PortLog       log2;
    LaunchpadHost restored(factoryFor(log2), listerWith({"in"}, {"out"}), dir);
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
    LaunchpadHost host(factoryFor(log), listerWith({"in"}, {"out"}), tempCfgDir());
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
    LaunchpadHost host(factoryFor(log), listerWith({"in", "in2"}, {"out", "out2"}), tempCfgDir());
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
