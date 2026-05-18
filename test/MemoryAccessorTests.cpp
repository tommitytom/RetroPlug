#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <vector>

#include "system/MemoryAccessor.hpp"
#include "system/MemoryType.hpp"

TEST_CASE("MemoryAccessor default is invalid", "[MemoryAccessor]") {
    rp::MemoryAccessor a;
    REQUIRE_FALSE(a.valid());
    REQUIRE(a.size() == 0);
    REQUIRE(a.data() == nullptr);
    REQUIRE(a[0] == 0u);  // out-of-range read returns 0 sentinel
}

TEST_CASE("MemoryAccessor wraps a buffer for read", "[MemoryAccessor]") {
    std::array<std::uint8_t, 8> buf{0,1,2,3,4,5,6,7};
    rp::MemoryAccessor a(rp::MemoryType::Ram, rp::AccessType::Read,
                         buf.data(), buf.size());
    REQUIRE(a.valid());
    REQUIRE(a.size() == 8);
    for (std::size_t i = 0; i < buf.size(); ++i) REQUIRE(a[i] == i);
}

TEST_CASE("MemoryAccessor write fails when read-only", "[MemoryAccessor]") {
    std::array<std::uint8_t, 4> buf{};
    rp::MemoryAccessor a(rp::MemoryType::Ram, rp::AccessType::Read,
                         buf.data(), buf.size());
    REQUIRE_FALSE(a.write(0, 0xAB));
    REQUIRE(buf[0] == 0);
}

TEST_CASE("MemoryAccessor single-byte write lands and records patch", "[MemoryAccessor]") {
    std::array<std::uint8_t, 4> buf{};
    std::vector<rp::MemoryPatch> patches;
    rp::MemoryAccessor a(rp::MemoryType::Rom, rp::AccessType::ReadWrite,
                         buf.data(), buf.size(), &patches);
    REQUIRE(a.write(1, 0x42));
    REQUIRE(buf[1] == 0x42);
    REQUIRE(patches.size() == 1);
    REQUIRE(patches[0].offset == 1);
    REQUIRE(patches[0].bytes == std::vector<std::uint8_t>{0x42});
}

TEST_CASE("MemoryAccessor bulk write enforces bounds", "[MemoryAccessor]") {
    std::array<std::uint8_t, 4> buf{};
    rp::MemoryAccessor a(rp::MemoryType::Rom, rp::AccessType::ReadWrite,
                         buf.data(), buf.size());
    const std::uint8_t payload[] = {1,2,3};

    REQUIRE(a.write(0, payload, 3));
    REQUIRE(buf == std::array<std::uint8_t,4>{1,2,3,0});

    REQUIRE_FALSE(a.write(2, payload, 3)); // would overflow
    REQUIRE(buf == std::array<std::uint8_t,4>{1,2,3,0});

    REQUIRE_FALSE(a.write(5, payload, 1)); // offset past end
}

TEST_CASE("MemoryAccessor bulk write tracks patches with bytes copy", "[MemoryAccessor]") {
    std::array<std::uint8_t, 16> buf{};
    std::vector<rp::MemoryPatch> patches;
    rp::MemoryAccessor a(rp::MemoryType::Rom, rp::AccessType::ReadWrite,
                         buf.data(), buf.size(), &patches);
    const std::uint8_t p1[] = {0xAA, 0xBB};
    const std::uint8_t p2[] = {0xCC};
    REQUIRE(a.write(4, p1, sizeof(p1)));
    REQUIRE(a.write(8, p2, sizeof(p2)));
    REQUIRE(patches.size() == 2);
    REQUIRE(patches[0].offset == 4);
    REQUIRE(patches[0].bytes == std::vector<std::uint8_t>{0xAA, 0xBB});
    REQUIRE(patches[1].offset == 8);
    REQUIRE(patches[1].bytes == std::vector<std::uint8_t>{0xCC});
}

TEST_CASE("MemoryAccessor clear fills the region", "[MemoryAccessor]") {
    std::array<std::uint8_t, 4> buf{1,2,3,4};
    rp::MemoryAccessor a(rp::MemoryType::Rom, rp::AccessType::ReadWrite,
                         buf.data(), buf.size());
    REQUIRE(a.clear(0x55));
    REQUIRE(buf == std::array<std::uint8_t,4>{0x55,0x55,0x55,0x55});
}
