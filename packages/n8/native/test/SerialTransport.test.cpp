// Guards SerialTransport's timeouts: a read sets the READ timeout only. serial::Timeout::simpleTimeout(t) sets the
// write timeout to t as well, and the port keeps one Timeout for both, so every write inherited the last read's -
// after N8Link's 0 ms back-channel poll, a CMD_MEM_WR to a real N8 timed out part-written, the short write went
// unchecked, and the N8's command parser was left mid-command: frames that never landed, then a device that
// answered nothing (found on hardware 2026-10-22; `retroplug-n8-hwtest linkdbg` is the on-device check).
//
// Here a pseudo-terminal stands in for the N8: a 0 ms read, then a write bigger than the pty's buffer while a reader
// drains it only after a pause. The write must wait for room, not give up at the read's 0 ms. Linux only (ptys).
// A second case holds the port exclusive: a POSIX port RetroPlug opens refuses every other open (another instance, a
// CLI session, minicom) with EBUSY, as only a Windows COM port does without it. Not for root, which TIOCEXCL does not
// bind. (The window between deps/serial's open and the flag cannot be provoked on a pty.)
// Run via `pnpm test:n8` (retroplug-serial-test).
#include <catch2/catch_test_macros.hpp>

#if defined(__linux__)
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "n8/desktop/SerialTransport.hpp"

namespace {

// A pseudo-terminal master, and the name of its slave (the "port").
struct Pty {
    int         master = -1;
    std::string slave;

    Pty() {
        master = posix_openpt(O_RDWR | O_NOCTTY);
        REQUIRE(master >= 0);
        REQUIRE(grantpt(master) == 0);
        REQUIRE(unlockpt(master) == 0);
        const char* name = ptsname(master);
        REQUIRE(name != nullptr);
        slave = name;
    }
    ~Pty() { ::close(master); }
};

}  // namespace

TEST_CASE("SerialTransport: a write is not cut short by the previous read's timeout", "[n8][serial]") {
    const int master = posix_openpt(O_RDWR | O_NOCTTY);
    REQUIRE(master >= 0);
    REQUIRE(grantpt(master) == 0);
    REQUIRE(unlockpt(master) == 0);
    REQUIRE(fcntl(master, F_SETFL, fcntl(master, F_GETFL) | O_NONBLOCK) == 0);   // a short write must not hang the reader
    const char* slave = ptsname(master);
    REQUIRE(slave != nullptr);

    retroplug::SerialTransport port(slave);
    std::uint8_t b[16];
    CHECK(port.read(b, sizeof b, 0) == 0);   // a 0 ms poll with nothing to read - what N8Link does constantly

    std::atomic<std::size_t> drained{0};
    std::thread reader([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));   // the cart is busy for a moment
        std::uint8_t buf[4096];
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (drained < 65536 && std::chrono::steady_clock::now() < until) {
            const ssize_t n = ::read(master, buf, sizeof buf);
            if (n > 0) drained += static_cast<std::size_t>(n);
            else std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    const std::vector<std::uint8_t> big(65536, 0x55);
    const std::size_t written = port.write(big.data(), big.size());
    reader.join();
    ::close(master);
    CHECK(written == big.size());
}

TEST_CASE("SerialTransport: an open port is exclusive, a second open fails busy", "[n8][serial]") {
    if (geteuid() == 0) SKIP("running as root: TIOCEXCL does not bind a process with CAP_SYS_ADMIN");
    Pty pty;

    {
        retroplug::SerialTransport port(pty.slave);
        REQUIRE(port.isOpen());

        // Any second open, by this process or another: the kernel's EBUSY.
        const int fd  = ::open(pty.slave.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
        const int err = errno;
        if (fd >= 0) ::close(fd);
        CHECK(fd == -1);
        CHECK(err == EBUSY);

        // A second transport throws, and says the port is busy rather than a bare errno.
        try {
            retroplug::SerialTransport second(pty.slave);
            FAIL("a second SerialTransport opened the same port");
        } catch (const std::exception& e) {
            const std::string said = e.what();
            CHECK(said.find("busy") != std::string::npos);
            CHECK(said.find(pty.slave) != std::string::npos);
        }
    }

    // The exclusivity lives with the open: once the port is closed, it opens again.
    retroplug::SerialTransport again(pty.slave);
    CHECK(again.isOpen());
}
#else
TEST_CASE("SerialTransport: timeout guard is Linux-only (pty)", "[n8][serial]") { SUCCEED(); }
#endif
