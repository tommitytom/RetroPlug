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
    // Single-reader / single-writer race. Mirrors the FrameBufferTriple
    // stress pattern: count torn vs good reads with atomics and assert
    // ONLY from the main thread after both worker threads have joined —
    // Catch2 REQUIRE from a non-main thread isn't reliable.
    //
    // Each slot encodes its frame value v in byte 0, v+1 in byte 1, etc.
    // A torn snapshot would mix bytes from two different v values.
    MemorySnapshotTriple m(kSize);

    // Prime first publish so the reader's "before == 0" guard doesn't bail.
    fillSlot(m.writeSlot(), kSize, 0);
    m.publish();

    std::atomic<bool> stop{false};
    std::atomic<std::uint64_t> tornReads{0};
    std::atomic<std::uint64_t> goodReads{0};

    std::thread writer([&] {
        for (std::uint32_t v = 1; !stop.load(std::memory_order_acquire); ++v) {
            const std::uint8_t base = static_cast<std::uint8_t>(v & 0xFF);
            fillSlot(m.writeSlot(), kSize, base);
            m.publish();
        }
    });

    std::thread reader([&] {
        std::vector<std::uint8_t> dst(kSize, 0);
        for (std::uint64_t r = 0; r < 100'000; ++r) {
            if (!m.readInto(dst)) continue;
            const std::uint8_t base = dst[0];
            bool torn = false;
            for (std::size_t i = 1; i < kSize; ++i) {
                if (dst[i] != static_cast<std::uint8_t>(base + i)) { torn = true; break; }
            }
            if (torn) tornReads.fetch_add(1, std::memory_order_relaxed);
            else      goodReads.fetch_add(1, std::memory_order_relaxed);
        }
        stop.store(true, std::memory_order_release);
    });

    reader.join();
    writer.join();

    REQUIRE(goodReads.load() > 0);
    REQUIRE(tornReads.load() == 0);
}
