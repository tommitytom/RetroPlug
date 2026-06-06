#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <vector>

#include "lsdj/codec/Regions.hpp"
#include "lsdj/codec/SongCodec.hpp"

namespace fs = std::filesystem;
using namespace rp::lsdj;

namespace {
std::vector<std::uint8_t> slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}
constexpr std::size_t kSongBytes = 0x8000;
const fs::path kSavDir{RETROPLUG_LSDJ_SAV_DIR};
} // namespace

TEST_CASE("decode fmt22 working song", "[lsdj-sav]") {
    const fs::path sav = kSavDir / "lsdj9_4_2.sav";
    if (!fs::exists(sav)) {
        WARN("corpus sav missing: " << sav.string());
        return;
    }
    const auto bytes = slurp(sav);
    REQUIRE(bytes.size() >= kSongBytes);

    auto res = codec::decodeSong(std::span<const std::uint8_t>(bytes.data(), kSongBytes));
    if (!res) FAIL("decode failed: " << res.error().what());

    const model::Song& song = res.value();
    CHECK(song.formatVersion == 22);
    CHECK(song.settings.tempo >= 40);
    CHECK(song.settings.tempo <= 295);

    // Any decoded instruments must be one of the four valid variants (the
    // TaggedUnion guarantees this structurally; this also exercises the
    // type-first decode path on whatever the fresh sav has allocated).
    std::size_t allocated = 0;
    for (const auto& inst : song.instruments)
        if (inst) ++allocated;
    INFO("allocated instruments: " << allocated);
    SUCCEED();
}

TEST_CASE("decode every corpus sav without error", "[lsdj-sav]") {
    if (!fs::exists(kSavDir)) {
        WARN("corpus dir missing: " << kSavDir.string());
        return;
    }
    std::size_t total = 0, ok = 0;
    for (const auto& entry : fs::directory_iterator(kSavDir)) {
        if (entry.path().extension() != ".sav") continue;
        const auto bytes = slurp(entry.path());
        if (bytes.size() < kSongBytes) continue; // early/32KB savs handled later
        ++total;
        auto res = codec::decodeSong(std::span<const std::uint8_t>(bytes.data(), kSongBytes));
        if (res) {
            ++ok;
        } else {
            UNSCOPED_INFO("decode failed: " << entry.path().filename().string()
                                            << " — " << res.error().what());
        }
    }
    INFO("decoded " << ok << "/" << total << " corpus savs");
    CHECK(total > 100); // sanity: corpus is present
    CHECK(ok == total);
}

namespace {
// First offset where two buffers differ, or npos if identical.
std::size_t firstDiff(std::span<const std::uint8_t> a, std::span<const std::uint8_t> b) {
    const std::size_t n = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < n; ++i)
        if (a[i] != b[i]) return i;
    return std::string::npos;
}
} // namespace

TEST_CASE("working-song round-trip is byte-identical (fmt22)", "[lsdj-sav]") {
    const fs::path sav = kSavDir / "lsdj9_4_2.sav";
    if (!fs::exists(sav)) { WARN("corpus sav missing"); return; }
    const auto bytes = slurp(sav);
    REQUIRE(bytes.size() >= kSongBytes);
    std::span<const std::uint8_t> orig(bytes.data(), kSongBytes);

    auto res = codec::decodeSong(orig);
    if (!res) FAIL("decode failed: " << res.error().what());
    const auto out = codec::encodeSong(res.value(), orig);

    REQUIRE(out.size() == kSongBytes);
    const std::size_t d = firstDiff(orig, out);
    if (d != std::string::npos)
        UNSCOPED_INFO("first diff at song offset 0x" << std::hex << d
                      << " (orig=0x" << int(orig[d]) << " enc=0x" << int(out[d]) << ")");
    CHECK(d == std::string::npos);
}

TEST_CASE("every corpus working-song round-trips byte-identical", "[lsdj-sav]") {
    if (!fs::exists(kSavDir)) { WARN("corpus dir missing"); return; }
    std::size_t total = 0, identical = 0;
    std::string firstFail;
    for (const auto& entry : fs::directory_iterator(kSavDir)) {
        if (entry.path().extension() != ".sav") continue;
        const auto bytes = slurp(entry.path());
        if (bytes.size() < kSongBytes) continue;
        ++total;
        std::span<const std::uint8_t> orig(bytes.data(), kSongBytes);
        auto res = codec::decodeSong(orig);
        if (!res) continue;
        const auto out = codec::encodeSong(res.value(), orig);
        const std::size_t d = firstDiff(orig, out);
        if (d == std::string::npos) {
            ++identical;
        } else if (firstFail.empty()) {
            firstFail = entry.path().filename().string() + " @0x" +
                        [d] { char b[8]; std::snprintf(b, sizeof b, "%zx", d); return std::string(b); }();
        }
    }
    UNSCOPED_INFO("byte-identical round-trips: " << identical << "/" << total
                  << (firstFail.empty() ? "" : ("; first fail " + firstFail)));
    CHECK(identical == total);
}

// The fresh corpus allocates no instruments, so exercise the instrument codec
// with a synthetic model: encode -> decode -> re-encode must be byte-stable
// (decode and encode are mutual inverses), and key fields must survive.
TEST_CASE("instrument encode/decode are mutual inverses", "[lsdj-sav]") {
    using namespace rp::lsdj::model;
    Song song;

    PulseInstrument pulse;
    pulse.common.get().panning = Panning::LeftRight;
    pulse.common.get().table = Nibble{0x5};
    pulse.adsr.initialLevel = 15;
    pulse.adsr.attackSpeed = 9;
    pulse.vibrato.shape = VibratoShape::Square;
    pulse.vibrato.plvSpeed = PlvSpeed::Tick;
    pulse.transpose = false;
    pulse.pulseWidth = PulseWidth::W50;
    pulse.finetune = 0xB;
    pulse.pulse2Tune = 0x42;
    song.instruments[0] = pulse;

    WaveInstrument wave;
    wave.volume = WaveVolume::V2;
    wave.synth = 0x3;
    wave.wave = 0x07;             // note: low nibble shares with synth's high nibble
    wave.playMode = WavePlayMode::Loop;
    wave.length = 0xC;
    wave.speed = 8;
    song.instruments[1] = wave;

    KitInstrument kit;
    kit.volume = WaveVolume::V3;
    kit.kit1 = 5; kit.kit2 = 10;
    kit.halfSpeed = true;
    kit.loop1 = KitLoopMode::Attack;
    kit.loop2 = KitLoopMode::On;
    kit.distortion = KitDistortion::Wrap;
    kit.pitch = 0x20; kit.length1 = 0x30; kit.offset1 = 0x11; kit.offset2 = 0x42;
    song.instruments[2] = kit;

    NoiseInstrument noise;
    noise.adsr.sustainLevel = 12;
    noise.vibrato.direction = VibratoDirection::Up;
    noise.stability = NoiseStability::Stable;
    noise.shape = 0x33;
    song.instruments[3] = noise;

    const auto b1 = codec::encodeSong(song);            // default template
    auto res = codec::decodeSong(b1);
    if (!res) FAIL("decode failed: " << res.error().what());
    const auto b2 = codec::encodeSong(res.value(), b1);
    CHECK(b1 == b2);                                     // mutual-inverse stability

    const Song& m = res.value();
    REQUIRE(m.instruments[0]); REQUIRE(m.instruments[1]);
    REQUIRE(m.instruments[2]); REQUIRE(m.instruments[3]);

    m.instruments[2]->visit([](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, KitInstrument>) {
            CHECK(v.kit1.value() == 5);
            CHECK(v.kit2.value() == 10);
            CHECK(v.loop1 == KitLoopMode::Attack);
            CHECK(v.loop2 == KitLoopMode::On);
            CHECK(v.distortion == KitDistortion::Wrap);
            CHECK(v.offset2 == 0x42);
            CHECK(v.halfSpeed == true);
        } else {
            FAIL("instrument 2 decoded as the wrong variant");
        }
    });
    m.instruments[0]->visit([](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, PulseInstrument>) {
            CHECK(v.pulseWidth == PulseWidth::W50);
            CHECK(v.finetune.value() == 0xB);
            CHECK(v.transpose == false);
            CHECK(v.vibrato.shape == VibratoShape::Square);
            CHECK(v.vibrato.plvSpeed == PlvSpeed::Tick);
        } else {
            FAIL("instrument 0 decoded as the wrong variant");
        }
    });
}
