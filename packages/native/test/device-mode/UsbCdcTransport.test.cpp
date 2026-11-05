// The box's Edio link (pico/n8-host/UsbCdcTransport.cpp, docs/modular-graph-phase-e2-design.md section 8) over a fake
// TinyUSB host (FakeTinyUsb.hpp): the rules E2 proved on the rig and by reading, held on the desktop.
//
// - A write gives the FIFO every byte, flushing as it goes, then waits for the bytes to leave, flushing until the
//   FIFO's free space is back; that wait is best effort (still queued at kDrainMs is a success), and a FIFO that takes
//   nothing for kWriteMs is a short write.
// - An unmount (the CDC unmount callback's unbind, which runs inside a pump) fails the call in flight at once: a write
//   answers what it handed over (0 once its bytes were all handed over and still queued), a read and a flush return.
//   From then every call fails after one pump, and a remount carries on nothing: the new N8 hears its first byte only
//   after the main loop binds it.
// - A read answers what has arrived once nothing more is waiting, and 0 at its timeout (one pump for a timeout of 0);
//   an input flush drops what arrives until kFlushQuietMs pass with nothing.
// - Every wait pumps the USB host and the idle hook, never a bare sleep.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

#include "FakeTinyUsb.hpp"
#include "UsbCdcTransport.hpp"

namespace {

int gIdles = 0;
void idle() { ++gIdles; }

std::vector<std::uint8_t> bytes(std::size_t n, std::uint8_t first = 0) {
    std::vector<std::uint8_t> v(n);
    for (std::size_t i = 0; i < n; ++i) v[i] = static_cast<std::uint8_t>(first + i);
    return v;
}

struct Rig {
    UsbCdcTransport usb{&idle};
    Rig() {
        fakeusb::reset();
        gIdles = 0;
        usb.bind(0);
    }
};

}  // namespace

TEST_CASE("a CDC write hands the FIFO every byte, then waits, flushing, for them to leave", "[device-mode][usb-cdc]") {
    Rig  rig;
    auto& u = fakeusb::usb();
    const auto data = bytes(300);
    CHECK(rig.usb.write(data.data(), data.size()) == 300);
    CHECK(u.taken == data);
    CHECK(u.sent == data);  // it returned once the FIFO was empty again
    CHECK(u.fifoFree == u.fifoSize);
    CHECK(u.flushes > 0);
    CHECK(gIdles == static_cast<int>(u.pumps));  // every pump ran the idle hook
}

TEST_CASE("a CDC write's drain is best effort, and a FIFO that takes nothing is a short write",
          "[device-mode][usb-cdc]") {
    SECTION("still queued at kDrainMs: a success") {
        Rig  rig;
        auto& u = fakeusb::usb();
        u.sendPerPump = 0;  // the bytes never leave
        const auto data = bytes(10);
        CHECK(rig.usb.write(data.data(), data.size()) == 10);
        CHECK(u.taken == data);
        CHECK(u.sent.empty());
        CHECK(u.ms >= UsbCdcTransport::kDrainMs);
        CHECK(u.ms <= UsbCdcTransport::kDrainMs + 5);
    }
    SECTION("nothing taken for kWriteMs: short") {
        Rig  rig;
        auto& u = fakeusb::usb();
        u.fifoFree = 0;
        u.sendPerPump = 0;
        const auto data = bytes(10);
        CHECK(rig.usb.write(data.data(), data.size()) == 0);
        CHECK(u.ms > UsbCdcTransport::kWriteMs);
        CHECK(u.ms <= UsbCdcTransport::kWriteMs + 5);
    }
    SECTION("a FIFO that takes a little at a time: each take restarts the wait") {
        Rig  rig;
        auto& u = fakeusb::usb();
        u.takeLimit = 8;
        u.sendPerPump = 8;
        const auto data = bytes(100);
        CHECK(rig.usb.write(data.data(), data.size()) == 100);
        CHECK(u.sent == data);
    }
}

TEST_CASE("an unmount fails the CDC call in flight at once, and every call after it", "[device-mode][usb-cdc]") {
    SECTION("while the FIFO takes the bytes: what it handed over") {
        Rig  rig;
        auto& u = fakeusb::usb();
        u.takeLimit = 16;
        u.sendPerPump = 0;
        u.onPump = [&] {
            if (u.pumps == 3) rig.usb.unbind();
        };
        const auto data = bytes(100);
        const std::size_t sent = rig.usb.write(data.data(), data.size());
        CHECK(sent == 32);  // two pumps' takes, then the unmount in the third
        CHECK(u.ms == 3);
        CHECK_FALSE(rig.usb.linkUp());
    }
    SECTION("while the bytes leave: 0") {
        Rig  rig;
        auto& u = fakeusb::usb();
        u.sendPerPump = 0;
        u.onPump = [&] {
            if (u.pumps == 4) rig.usb.unbind();
        };
        const auto data = bytes(10);
        CHECK(rig.usb.write(data.data(), data.size()) == 0);
        CHECK(u.ms == 4);
    }
    SECTION("a read and a flush return") {
        Rig  rig;
        auto& u = fakeusb::usb();
        u.onPump = [&] {
            if (u.pumps == 2) rig.usb.unbind();
        };
        std::uint8_t buffer[8];
        CHECK(rig.usb.read(buffer, sizeof buffer, 1000) == 0);
        CHECK(u.ms == 2);
        fakeusb::reset();
        rig.usb.bind(0);
        u.arriving.push_back({1, 0x11});  // never quiet for long: only the unmount can end the flush early
        u.onPump = [&] {
            if (u.pumps == 5) rig.usb.unbind();
        };
        rig.usb.flushInput();
        CHECK(u.ms == 5);
    }
    SECTION("after it, each call fails after one pump") {
        Rig  rig;
        auto& u = fakeusb::usb();
        rig.usb.unbind();
        const auto   data = bytes(4);
        std::uint8_t buffer[4];
        CHECK(rig.usb.write(data.data(), data.size()) == 0);
        CHECK(rig.usb.read(buffer, sizeof buffer, 1000) == 0);
        rig.usb.flushInput();
        CHECK(u.pumps == 3);
        CHECK(u.taken.empty());
    }
}

TEST_CASE("a remount carries on nothing: the new N8 hears its first byte once the main loop binds it",
          "[device-mode][usb-cdc]") {
    Rig          rig;
    auto&        u = fakeusb::usb();
    std::uint8_t mountedIdx = 0xFF;
    // Inside one pump, as TinyUSB can deliver them: the old N8's unmount (unbind) and the new one's CDC mount, whose
    // callback only records the index (pico/n8-host/main.cpp's tuh_cdc_mount_cb).
    u.onPump = [&] {
        if (u.pumps == 2) {
            rig.usb.unbind();
            mountedIdx = 1;
        }
    };
    std::uint8_t reply[4];
    CHECK(rig.usb.read(reply, sizeof reply, 1000) == 0);  // a reply the old N8 never gave: the call ends here
    CHECK(mountedIdx == 1);
    CHECK_FALSE(rig.usb.linkUp());
    const auto rest = bytes(6, 0x40);
    CHECK(rig.usb.write(rest.data(), rest.size()) == 0);  // what a call would carry on with goes nowhere
    CHECK(u.taken.empty());

    u.onPump = nullptr;
    rig.usb.bind(mountedIdx);  // the main loop, at its top level, before the new N8's probe
    CHECK(rig.usb.linkUp());
    const auto probe = bytes(4, 0x2A);
    CHECK(rig.usb.write(probe.data(), probe.size()) == 4);
    CHECK(u.taken == probe);
    CHECK(u.writeIdx == std::vector<std::uint8_t>(4, 1));
}

TEST_CASE("a CDC read answers what has arrived, and an input flush waits for the line to go quiet",
          "[device-mode][usb-cdc]") {
    SECTION("a read") {
        Rig  rig;
        auto& u = fakeusb::usb();
        u.arriving = {{3, 0xA1}, {4, 0xA2}, {40, 0xA3}};
        std::uint8_t buffer[8] = {};
        CHECK(rig.usb.read(buffer, sizeof buffer, 100) == 2);  // the two together, once nothing more is waiting
        CHECK(buffer[0] == 0xA1);
        CHECK(buffer[1] == 0xA2);
        CHECK(u.ms == 5);
        const std::uint32_t before = u.pumps;
        CHECK(rig.usb.read(buffer, sizeof buffer, 0) == 0);  // a timeout of 0: one pump
        CHECK(u.pumps == before + 1);
        CHECK(rig.usb.read(buffer, 1, 100) == 1);  // the third, when it comes
        CHECK(buffer[0] == 0xA3);
        CHECK(u.ms == 40);
        CHECK(rig.usb.read(buffer, sizeof buffer, 50) == 0);
        CHECK(u.ms > 40 + 50);
    }
    SECTION("a flush") {
        Rig  rig;
        auto& u = fakeusb::usb();
        u.arriving = {{5, 0x01}, {15, 0x02}, {30, 0x03}, {80, 0x04}};
        rig.usb.flushInput();
        CHECK(u.ms >= 30 + UsbCdcTransport::kFlushQuietMs);  // 20 ms after the last byte it dropped
        CHECK(u.ms < 80);
        std::uint8_t buffer[4] = {};
        CHECK(rig.usb.read(buffer, sizeof buffer, 100) == 1);  // what arrives after the flush is kept
        CHECK(buffer[0] == 0x04);
    }
}
