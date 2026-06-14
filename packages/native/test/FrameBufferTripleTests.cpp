#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include "transport/FrameBufferTriple.hpp"

namespace {

constexpr std::uint32_t kW = 4;
constexpr std::uint32_t kH = 3;
constexpr std::uint32_t kPixels = kW * kH;

void fillSlot(std::uint32_t* slot, std::uint32_t pixels, std::uint32_t value) {
    for (std::uint32_t i = 0; i < pixels; ++i) slot[i] = value + i;
}

bool verifyFrame(const std::uint32_t* dst, std::uint32_t pixels, std::uint32_t expectedValue) {
    for (std::uint32_t i = 0; i < pixels; ++i) {
        if (dst[i] != expectedValue + i) return false;
    }
    return true;
}

} // namespace

TEST_CASE("FrameBufferTriple readInto fails before first publish", "[FrameBufferTriple]") {
    FrameBufferTriple fb(kW, kH);
    REQUIRE(fb.width() == kW);
    REQUIRE(fb.height() == kH);

    std::vector<std::uint32_t> dst(kPixels, 0xDEADBEEF);
    REQUIRE_FALSE(fb.readInto(dst.data(), kPixels));
}

TEST_CASE("FrameBufferTriple round-trips a single frame", "[FrameBufferTriple]") {
    FrameBufferTriple fb(kW, kH);

    fillSlot(fb.writeSlot(), kPixels, 0x100);
    fb.publish();

    std::vector<std::uint32_t> dst(kPixels, 0);
    REQUIRE(fb.readInto(dst.data(), kPixels));
    REQUIRE(verifyFrame(dst.data(), kPixels, 0x100));
}

TEST_CASE("FrameBufferTriple readInto returns the most recently published frame", "[FrameBufferTriple]") {
    FrameBufferTriple fb(kW, kH);

    fillSlot(fb.writeSlot(), kPixels, 0x100);
    fb.publish();
    fillSlot(fb.writeSlot(), kPixels, 0x200);
    fb.publish();
    fillSlot(fb.writeSlot(), kPixels, 0x300);
    fb.publish();

    std::vector<std::uint32_t> dst(kPixels, 0);
    REQUIRE(fb.readInto(dst.data(), kPixels));
    REQUIRE(verifyFrame(dst.data(), kPixels, 0x300));
}

TEST_CASE("FrameBufferTriple rejects undersized read buffer", "[FrameBufferTriple]") {
    FrameBufferTriple fb(kW, kH);
    fillSlot(fb.writeSlot(), kPixels, 0x100);
    fb.publish();

    std::vector<std::uint32_t> dst(kPixels - 1, 0);
    REQUIRE_FALSE(fb.readInto(dst.data(), kPixels - 1));
}

TEST_CASE("FrameBufferTriple writer never picks the slot the reader is on", "[FrameBufferTriple]") {
    // Single-reader / single-writer race. We can't deterministically assert
    // "no torn read" with stdlib-only tooling, but we CAN check the
    // structural invariant: after a long run, every read returns a
    // frame whose pixels are internally consistent (i.e. the data wasn't
    // half-overwritten between the latest.load and the memcpy).
    //
    // The slot encodes its frame number in every pixel, so a torn read
    // would mix two frame numbers in one snapshot.
    FrameBufferTriple fb(kW, kH);

    std::atomic<bool> stop{false};
    std::atomic<std::uint64_t> tornReads{0};
    std::atomic<std::uint64_t> goodReads{0};

    std::thread writer([&]() {
        for (std::uint32_t frame = 1; !stop.load(std::memory_order_acquire); ++frame) {
            std::uint32_t* slot = fb.writeSlot();
            for (std::uint32_t i = 0; i < kPixels; ++i)
                slot[i] = frame; // every pixel == frame number
            fb.publish();
        }
    });

    std::thread reader([&]() {
        std::vector<std::uint32_t> dst(kPixels, 0);
        // Spin until first publish has happened.
        while (!fb.readInto(dst.data(), kPixels)) {}
        // Run for a fixed number of reads so the test terminates.
        for (std::uint64_t r = 0; r < 100'000; ++r) {
            if (!fb.readInto(dst.data(), kPixels)) continue;
            const std::uint32_t expected = dst[0];
            bool torn = false;
            for (std::uint32_t i = 1; i < kPixels; ++i) {
                if (dst[i] != expected) { torn = true; break; }
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

TEST_CASE("FrameBufferTriple respects different dimensions", "[FrameBufferTriple]") {
    // SameBoy: 160×144. Mesen NES (later): 256×240. Confirm construction
    // sizing works at arbitrary dimensions without UB.
    FrameBufferTriple fbGB(160, 144);
    FrameBufferTriple fbNES(256, 240);

    REQUIRE(fbGB.width() == 160);
    REQUIRE(fbGB.height() == 144);
    REQUIRE(fbNES.width() == 256);
    REQUIRE(fbNES.height() == 240);

    fbNES.writeSlot()[0] = 0x12345678;
    fbNES.writeSlot()[256 * 240 - 1] = 0x9ABCDEF0;
    fbNES.publish();

    std::vector<std::uint32_t> dst(256 * 240, 0);
    REQUIRE(fbNES.readInto(dst.data(), 256 * 240));
    REQUIRE(dst[0] == 0x12345678);
    REQUIRE(dst[256 * 240 - 1] == 0x9ABCDEF0);
}
