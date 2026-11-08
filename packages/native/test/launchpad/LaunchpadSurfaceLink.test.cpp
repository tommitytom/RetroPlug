// Guards the surface device link rp.host.devices/2 hands rp.launchpad.surface-in / -out
// (host/launchpad/LaunchpadSurfaceLink.hpp), over a real LaunchpadLink and its fake IMidiPort: the device's messages
// drain into the caller's own slots, in order, once each; the ring's bound drops and counts rather than truncating (a
// message wider than a slot, and a full ring); a push while the device is gone is dropped and counted too, which is
// what a node reads off dropped(); and the LED traffic a node pushes is queued for the host's main loop exactly as
// LaunchpadLink queues it.
//
// The slots are the point: the block thread copies out of the link's fixed slots and frees nothing, so a drain costs
// the audio thread no allocation at all. No rtmidi, no MIDI system, no hardware.
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../graph/CountingAllocator.hpp"

#include "launchpad/SurfaceLink.hpp"
#include "host/launchpad/LaunchpadLink.hpp"
#include "host/launchpad/LaunchpadSurfaceLink.hpp"

using retroplug::IMidiPort;
using retroplug::LaunchpadLink;
using retroplug::LaunchpadSurfaceLink;
using rp::launchpad::DeviceMessage;
using rp::launchpad::kMaxDeviceMessage;

namespace {

// What the fake port saw and the receiver the test fires, kept outside the port so it survives its destruction.
struct PortLog {
    std::vector<std::vector<std::uint8_t>> written;
    IMidiPort::Receiver                    receiver;
};

struct FakePort final : IMidiPort {
    explicit FakePort(PortLog& log) : log_(log) {}
    ~FakePort() override { log_.receiver = nullptr; }
    void send(const std::uint8_t* data, std::size_t n) override { log_.written.emplace_back(data, data + n); }
    PortLog& log_;
};

LaunchpadLink::PortFactory factoryFor(PortLog& log) {
    return [&log](const std::string&, const std::string&,
                  IMidiPort::Receiver receiver) -> std::unique_ptr<IMidiPort> {
        log.receiver = std::move(receiver);
        return std::make_unique<FakePort>(log);
    };
}

std::vector<std::uint8_t> bytesOf(const DeviceMessage& m) { return {m.data, m.data + m.size}; }

}  // namespace

TEST_CASE("LaunchpadSurfaceLink: the device's messages drain into the caller's slots, in order, once each",
          "[launchpad]") {
    PortLog       log;
    LaunchpadLink link(factoryFor(log));
    REQUIRE(link.connect("in", "out"));
    LaunchpadSurfaceLink surface(link);
    REQUIRE(surface.generation() != 0);  // 1: the legacy link reports the connected edge as its generation
    REQUIRE(log.receiver);

    const std::uint8_t pad[3]     = {0x90, 0x51, 0x7F};
    const std::uint8_t release[3] = {0x90, 0x51, 0x00};
    log.receiver(pad, sizeof pad);
    log.receiver(release, sizeof release);

    std::vector<DeviceMessage> out(4);
    REQUIRE(surface.drainInput(out.data(), out.size()) == 2);
    CHECK(bytesOf(out[0]) == std::vector<std::uint8_t>{0x90, 0x51, 0x7F});
    CHECK(bytesOf(out[1]) == std::vector<std::uint8_t>{0x90, 0x51, 0x00});
    CHECK(surface.drainInput(out.data(), out.size()) == 0);  // drained once, delivered once
}

TEST_CASE("LaunchpadSurfaceLink: a drain with room for fewer messages leaves the rest queued", "[launchpad]") {
    PortLog       log;
    LaunchpadLink link(factoryFor(log));
    REQUIRE(link.connect("in", "out"));
    LaunchpadSurfaceLink surface(link);

    for (std::uint8_t i = 0; i < 3; ++i) {
        const std::uint8_t m[2] = {0x90, static_cast<std::uint8_t>(0x10 + i)};
        log.receiver(m, sizeof m);
    }
    DeviceMessage out[2];
    REQUIRE(surface.drainInput(out, 2) == 2);
    CHECK(bytesOf(out[0]) == std::vector<std::uint8_t>{0x90, 0x10});
    CHECK(bytesOf(out[1]) == std::vector<std::uint8_t>{0x90, 0x11});
    REQUIRE(surface.drainInput(out, 2) == 1);
    CHECK(bytesOf(out[0]) == std::vector<std::uint8_t>{0x90, 0x12});
}

TEST_CASE("LaunchpadSurfaceLink: a message wider than a slot is dropped and counted, never truncated", "[launchpad]") {
    PortLog       log;
    LaunchpadLink link(factoryFor(log));
    REQUIRE(link.connect("in", "out"));
    LaunchpadSurfaceLink surface(link);

    // One byte past the bound: a device cannot produce one (its own replies are short), so it is a fault upstream, and
    // a truncated message would be worse than a lost one.
    const std::vector<std::uint8_t> huge(kMaxDeviceMessage + 1, 0x7F);
    log.receiver(huge.data(), huge.size());
    DeviceMessage out[1];
    CHECK(surface.drainInput(out, 1) == 0);
    CHECK(link.messagesInboundDropped() == 1);
    CHECK(link.messagesDropped() == 0);  // a press the audio side missed is not a frame the device is missing

    // The exact bound still fits, in a slot of its own.
    const std::vector<std::uint8_t> big(kMaxDeviceMessage, 0x7F);
    log.receiver(big.data(), big.size());
    REQUIRE(surface.drainInput(out, 1) == 1);
    CHECK(out[0].size == kMaxDeviceMessage);
    CHECK(link.messagesInboundDropped() == 1);
}

TEST_CASE("LaunchpadSurfaceLink: a full ring drops and counts what the device sent", "[launchpad]") {
    PortLog       log;
    LaunchpadLink link(factoryFor(log));
    REQUIRE(link.connect("in", "out"));
    LaunchpadSurfaceLink surface(link);

    // The ring holds LaunchpadLink::kInCapacity slots less its one free slot: every message past that is dropped, not
    // queued.
    constexpr std::size_t kSlots = LaunchpadLink::kInCapacity;
    constexpr std::size_t kHeld  = kSlots - 1;
    const std::uint8_t    m[2]   = {0x90, 0x51};
    for (std::size_t i = 0; i < kSlots + 8; ++i) log.receiver(m, sizeof m);
    CHECK(link.messagesInboundDropped() == kSlots + 8 - kHeld);
    CHECK(link.messagesDropped() == 0);

    // And what did fit comes out whole, oldest first.
    std::vector<DeviceMessage> out(kSlots);
    CHECK(surface.drainInput(out.data(), out.size()) == kHeld);
    CHECK(surface.drainInput(out.data(), out.size()) == 0);
}

TEST_CASE("LaunchpadSurfaceLink: a block thread's drain allocates and frees nothing", "[launchpad]") {
    PortLog       log;
    LaunchpadLink link(factoryFor(log));
    REQUIRE(link.connect("in", "out"));
    LaunchpadSurfaceLink surface(link);
    REQUIRE(log.receiver);

    const std::uint8_t pad[3] = {0x90, 0x51, 0x7F};
    // One warm drain first, so a counted one is the steady state: the messages stay in the link's slots until copied.
    log.receiver(pad, sizeof pad);
    std::array<DeviceMessage, 4> out{};
    REQUIRE(surface.drainInput(out.data(), out.size()) == 1);

    log.receiver(pad, sizeof pad);
    log.receiver(pad, sizeof pad);
    rp::test::beginCountingAllocations();
    const std::size_t n = surface.drainInput(out.data(), out.size());
    const auto        counted = rp::test::endCountingAllocations();
    CHECK(n == 2);
    CHECK(counted.allocations == 0);
    CHECK(counted.frees == 0);
}

TEST_CASE("LaunchpadSurfaceLink: the output a node pushes is queued for the host's main loop", "[launchpad]") {
    PortLog       log;
    LaunchpadLink link(factoryFor(log));
    REQUIRE(link.connect("in", "out"));
    LaunchpadSurfaceLink surface(link);

    const std::uint8_t led[3] = {0x90, 0x51, 0x15};
    surface.pushOutput(led, sizeof led);
    CHECK(log.written.empty());  // the block thread only queues; the host's pump writes

    link.pump();
    REQUIRE(log.written.size() == 1);
    CHECK(log.written[0] == std::vector<std::uint8_t>{0x90, 0x51, 0x15});
    CHECK(link.messagesSent() == 1);
}

TEST_CASE("LaunchpadSurfaceLink: a push while the device is gone goes nowhere and is counted", "[launchpad]") {
    PortLog       log;
    LaunchpadLink link(factoryFor(log));
    REQUIRE(link.connect("in", "out"));
    LaunchpadSurfaceLink surface(link);
    REQUIRE(surface.dropped() == 0);

    // The device goes away: the push the block thread makes reaches nothing, and the link counts it, which is what a
    // node reads off dropped(). The link keeps that count apart from the frames it lost while attached
    // (messagesDetached), and the handle's dropped() is the sum: one number for what the device never got.
    link.disconnect();
    const std::uint8_t led[3] = {0x90, 0x51, 0x15};
    surface.pushOutput(led, sizeof led);
    CHECK(surface.dropped() == 1);
    link.pump();
    CHECK(log.written.empty());

    // A message with no bytes is not a push that went nowhere.
    surface.pushOutput(nullptr, 0);
    CHECK(surface.dropped() == 1);
}
