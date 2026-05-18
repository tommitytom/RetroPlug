#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include "transport/MemorySnapshotTriple.hpp"

namespace {

constexpr std::size_t kSize = 64;

void fillSlot(std::uint8_t* slot, std::size_t size, std::uint8_t base) {
    for (std::size_t i = 0; i < size; ++i) slot[i] = static_cast<std::uint8_t>(base + i);
}

bool verify(const std::uint8_t* dst, std::size_t size, std::uint8_t base) {
    for (std::size_t i = 0; i < size; ++i) {
        if (dst[i] != static_cast<std::uint8_t>(base + i)) return false;
    }
    return true;
}

} // namespace

TEST_CASE("MemorySnapshotTriple readInto fails before first publish", "[MemorySnapshotTriple]") {
    MemorySnapshotTriple m(kSize);
    REQUIRE(m.size() == kSize);

    std::vector<std::uint8_t> dst(kSize, 0xEE);
    REQUIRE_FALSE(m.readInto(dst));
}

TEST_CASE("MemorySnapshotTriple round-trips a single snapshot", "[MemorySnapshotTriple]") {
    MemorySnapshotTriple m(kSize);
    fillSlot(m.writeSlot(), kSize, 0x10);
    m.publish();

    std::vector<std::uint8_t> dst(kSize, 0);
    REQUIRE(m.readInto(dst));
    REQUIRE(verify(dst.data(), kSize, 0x10));
}

TEST_CASE("MemorySnapshotTriple returns the most recent publish", "[MemorySnapshotTriple]") {
    MemorySnapshotTriple m(kSize);
    for (std::uint8_t v = 0; v < 4; ++v) {
        fillSlot(m.writeSlot(), kSize, v);
        m.publish();
    }
    std::vector<std::uint8_t> dst(kSize, 0);
    REQUIRE(m.readInto(dst));
    REQUIRE(verify(dst.data(), kSize, 3));
}

TEST_CASE("MemorySnapshotTriple grows dst on read into smaller vector", "[MemorySnapshotTriple]") {
    MemorySnapshotTriple m(kSize);
    fillSlot(m.writeSlot(), kSize, 0x20);
    m.publish();

    std::vector<std::uint8_t> dst;  // empty
    REQUIRE(m.readInto(dst));
    REQUIRE(dst.size() == kSize);
    REQUIRE(verify(dst.data(), kSize, 0x20));
}

TEST_CASE("MemorySnapshotTriple raw readInto rejects too-small destination", "[MemorySnapshotTriple]") {
    MemorySnapshotTriple m(kSize);
    fillSlot(m.writeSlot(), kSize, 0x30);
    m.publish();

    std::vector<std::uint8_t> dst(kSize - 1, 0);
    REQUIRE_FALSE(m.readInto(dst.data(), dst.size()));
}

TEST_CASE("MemorySnapshotTriple stress: concurrent writes and reads stay tear-free",
          "[MemorySnapshotTriple]") {
    constexpr std::size_t kStressIterations = 10000;
    MemorySnapshotTriple m(kSize);

    // Prime first publish so the reader's "before == 0" guard doesn't bail.
    fillSlot(m.writeSlot(), kSize, 0);
    m.publish();

    std::atomic<bool> stop{false};
    std::atomic<std::uint64_t> readsOk{0};

    std::thread writer([&] {
        for (std::size_t i = 0; i < kStressIterations; ++i) {
            const std::uint8_t v = static_cast<std::uint8_t>(i & 0xFF);
            fillSlot(m.writeSlot(), kSize, v);
            m.publish();
        }
        stop.store(true);
    });

    std::thread reader([&] {
        std::vector<std::uint8_t> dst(kSize, 0);
        while (!stop.load()) {
            if (!m.readInto(dst)) continue;
            // Every byte must be (base + offset) for SOME consistent base.
            const std::uint8_t base = dst[0];
            bool ok = true;
            for (std::size_t i = 0; i < kSize; ++i) {
                if (dst[i] != static_cast<std::uint8_t>(base + i)) { ok = false; break; }
            }
            REQUIRE(ok);
            if (ok) ++readsOk;
        }
    });

    writer.join();
    reader.join();
    REQUIRE(readsOk.load() > 0);
}
