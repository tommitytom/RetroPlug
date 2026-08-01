// Guards the intra-block timing of SameBoy serial input (SameBoySystem::pushSerialIn +
// nextSerialInBit + the finishBlock offset rebase). Host MIDI reaches mGB/LSDj over the GB
// serial port; DPF delivers each MidiEvent with an intra-block sample offset, and that offset
// must survive to delivery so a burst of fast MIDI keeps its timing instead of collapsing to the
// block start. A byte scheduled at offset N must NOT begin shifting before audioFrameCount_
// reaches N, and a byte scheduled past the block end must be rebased (offset -= frames), mirroring
// the pending-button queue this path is modelled on.
//
// The gate lives on `audioFrameCount_` (public), so these checks drive it directly rather than
// booting a ROM into serial-ready state. Run via `pnpm test:plugin`.

#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "system/SystemTypes.hpp"
#include "system/sameboy/SameBoyConfig.hpp"
#include "system/sameboy/SameBoySystem.hpp"

#ifndef RP_MGB_ROM_PATH
#error "RP_MGB_ROM_PATH must be defined (path to resources/roms/mGB.gb)"
#endif

namespace {

std::vector<std::uint8_t> readRom() {
    std::FILE* f = std::fopen(RP_MGB_ROM_PATH, "rb");
    REQUIRE(f != nullptr);
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    REQUIRE(n > 0);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(n));
    REQUIRE(std::fread(bytes.data(), 1, bytes.size(), f) == bytes.size());
    std::fclose(f);
    return bytes;
}

std::unique_ptr<SameBoySystem> buildMgb() {
    SameBoyConfig cfg;
    cfg.model = SameBoyModel::DmgB;
    auto sys = std::make_unique<SameBoySystem>(1, cfg, readRom());
    sys->onActivate(48000.0);
    return sys;
}

// Shift a full byte out MSB-first through nextSerialInBit(), reassembling it.
std::uint8_t drainByte(SameBoySystem& sys) {
    std::uint8_t out = 0;
    for (int i = 0; i < 8; ++i) {
        out = static_cast<std::uint8_t>((out << 1) | (sys.nextSerialInBit() ? 1u : 0u));
    }
    return out;
}

} // namespace

TEST_CASE("SameBoy serial byte does not shift before its scheduled offset", "[audio][sameboy][serial]") {
    auto sys = buildMgb();
    sys->pushSerialIn(/*frame=*/500, /*byte=*/0xAB);

    // Before the offset is reached, nextSerialInBit() is idle-high and consumes nothing — the byte
    // stays queued and no bit of it has started shifting.
    sys->audioFrameCount_ = 0;
    for (int i = 0; i < 16; ++i) {
        CHECK(sys->nextSerialInBit() == true); // idle high
    }
    REQUIRE(sys->serialIn_.size() == 1);
    CHECK(sys->serialBitsRemaining_ == 0); // never started the byte

    // Just short of the offset: still gated.
    sys->audioFrameCount_ = 499;
    CHECK(sys->nextSerialInBit() == true);
    REQUIRE(sys->serialIn_.size() == 1);

    // At the offset the byte shifts out MSB-first and the queue drains.
    sys->audioFrameCount_ = 500;
    CHECK(drainByte(*sys) == 0xAB);
    CHECK(sys->serialIn_.empty());
}

TEST_CASE("SameBoy serial byte at offset 0 delivers immediately", "[audio][sameboy][serial]") {
    // The default (frame-0) path — every existing MIDI-in test relies on it — must be unaffected:
    // a byte scheduled at the block start shifts on the very first bit request.
    auto sys = buildMgb();
    sys->pushSerialIn(/*frame=*/0, /*byte=*/0xCD);
    sys->audioFrameCount_ = 0;
    CHECK(drainByte(*sys) == 0xCD);
    CHECK(sys->serialIn_.empty());
}

TEST_CASE("SameBoy rebases serial offsets past the block end", "[audio][sameboy][serial]") {
    // A byte scheduled beyond this block carries into the next one with its offset shifted back by
    // `frames`, so a MIDI event landing late in a small block keeps its relative timing.
    auto sys = buildMgb();
    constexpr std::uint32_t kFrames = 400;
    sys->pushSerialIn(/*frame=*/kFrames + 100, /*byte=*/0x42);

    std::vector<float> l(kFrames, 0.0f), r(kFrames, 0.0f);
    float* outs[2] = {l.data(), r.data()};
    AudioBlockInfo info{};
    info.frames     = kFrames;
    info.sampleRate = 48000.0;
    sys->onProcess(info, outs);

    // Untouched this block (offset was past the end), rebased to 100 for the next.
    REQUIRE(sys->serialIn_.size() == 1);
    CHECK(sys->serialIn_.front().offset == 100);
    CHECK(sys->serialIn_.front().byte == 0x42);
}
