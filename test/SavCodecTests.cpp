#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <vector>

#include "lsdj/SavSerialization.hpp"
#include "lsdj/codec/Compression.hpp"
#include "lsdj/codec/Regions.hpp"
#include "lsdj/codec/SavCodec.hpp"
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

// ---- SavCodec: full 128 KiB image -------------------------------------------

TEST_CASE("full sav round-trip is byte-identical (corpus)", "[lsdj-sav]") {
    if (!fs::exists(kSavDir)) { WARN("corpus dir missing"); return; }
    std::size_t total = 0, decoded = 0, identical = 0, unreadable = 0;
    std::string firstFail;
    for (const auto& entry : fs::directory_iterator(kSavDir)) {
        if (entry.path().extension() != ".sav") continue;
        const auto bytes = slurp(entry.path());
        if (bytes.size() != codec::kSavSize) continue; // 128 KiB images only
        ++total;
        std::span<const std::uint8_t> orig(bytes.data(), bytes.size());
        auto res = codec::decodeSav(orig);
        if (!res) { ++unreadable; continue; } // non-standard early savs lacking 'jk'
        ++decoded;
        const auto out = codec::encodeSav(res.value(), orig);
        const std::size_t d = firstDiff(orig, out);
        if (d == std::string::npos) {
            ++identical;
        } else if (firstFail.empty()) {
            char buf[16]; std::snprintf(buf, sizeof buf, "0x%zx", d);
            firstFail = entry.path().filename().string() + " @" + buf;
        }
    }
    UNSCOPED_INFO("full-sav: " << identical << "/" << decoded << " decodable round-trip byte-identical; "
                  << unreadable << " unreadable (no 'jk')"
                  << (firstFail.empty() ? "" : ("; first fail " + firstFail)));
    CHECK(total > 100);
    CHECK(identical == decoded);   // everything we can decode round-trips exactly
    CHECK(unreadable <= 5);        // only a couple of non-standard early develop savs
}

TEST_CASE("compression is a mutual inverse", "[lsdj-sav]") {
    // Use a real working song as representative content.
    const fs::path sav = kSavDir / "lsdj9_4_2.sav";
    if (!fs::exists(sav)) { WARN("corpus sav missing"); return; }
    const auto bytes = slurp(sav);
    REQUIRE(bytes.size() >= kSongBytes);
    std::vector<std::uint8_t> song(bytes.begin(), bytes.begin() + kSongBytes);

    auto comp = codec::compressProject(song, /*startBlock 1-based*/ 1);
    if (!comp) FAIL("compress failed: " << comp.error().what());
    REQUIRE(comp.value().bytes.size() % codec::kBlockSize == 0);
    INFO("compressed into " << comp.value().blockCount << " blocks");

    // Lay the compressed stream into a block area and decompress from block 0.
    std::vector<std::uint8_t> blockArea(codec::kBlockCount * codec::kBlockSize, 0);
    std::memcpy(blockArea.data(), comp.value().bytes.data(), comp.value().bytes.size());
    auto round = codec::decompressProject(blockArea, /*0-based*/ 0);
    if (!round) FAIL("decompress failed: " << round.error().what());
    CHECK(round.value() == song);
}

TEST_CASE("sav with a stored project round-trips at the model level", "[lsdj-sav]") {
    using namespace rp::lsdj::model;
    Sav sav;
    sav.activeProjectIndex = 0;
    StoredProject proj;
    proj.name = "TEST";
    proj.version = 3;
    proj.song.settings.tempo = 150;
    proj.song.instruments[0] = KitInstrument{}; // a non-default allocated instrument
    sav.projects[0] = proj;

    const auto img = codec::encodeSav(sav);
    auto res = codec::decodeSav(img);
    if (!res) FAIL("decodeSav failed: " << res.error().what());
    const Sav& m = res.value();

    REQUIRE(m.projects[0]);
    CHECK(m.projects[0]->name == "TEST");
    CHECK(m.projects[0]->version == 3);
    CHECK(m.projects[0]->song.settings.tempo == 150);
    REQUIRE(m.projects[0]->song.instruments[0]);
    CHECK(m.activeProjectIndex == 0);
    for (std::size_t i = 1; i < 32; ++i) CHECK_FALSE(m.projects[i]);
}

TEST_CASE("32 KiB early-SRAM sav decodes as working-song-only", "[lsdj-sav]") {
    const fs::path sav = kSavDir / "lsdj2_6_3-develop.sav";
    if (!fs::exists(sav)) { WARN("32KiB corpus sav missing"); return; }
    const auto bytes = slurp(sav);
    REQUIRE(bytes.size() == 0x8000); // 32 KiB
    auto res = codec::decodeSav(std::span<const std::uint8_t>(bytes.data(), bytes.size()));
    if (!res) FAIL("decodeSav(32KiB) failed: " << res.error().what());
    CHECK(res.value().activeProjectIndex == 0xFF); // no header
    for (const auto& p : res.value().projects) CHECK_FALSE(p); // no archive
}

// ---- JSON (the headline) ----------------------------------------------------

TEST_CASE("sav <-> JSON is lossless (re-encodes identically)", "[lsdj-sav]") {
    const fs::path sav = kSavDir / "lsdj9_4_2.sav";
    if (!fs::exists(sav)) { WARN("corpus sav missing"); return; }
    const auto bytes = slurp(sav);
    REQUIRE(bytes.size() == codec::kSavSize);
    std::span<const std::uint8_t> orig(bytes.data(), bytes.size());

    auto sav1 = codec::decodeSav(orig);
    if (!sav1) FAIL("decodeSav failed: " << sav1.error().what());

    const std::string json = savToJson(sav1.value());
    CHECK(json.size() > 0);
    auto sav2 = savFromJson(json);
    if (!sav2) FAIL("savFromJson failed: " << sav2.error().what());

    // model -> JSON -> model is lossless iff both re-encode to the same bytes.
    const auto b1 = codec::encodeSav(sav1.value(), orig);
    const auto b2 = codec::encodeSav(sav2.value(), orig);
    CHECK(b1 == b2);
}

TEST_CASE("JSON fixture authoring round-trips (no template)", "[lsdj-sav]") {
    using namespace rp::lsdj::model;
    // Author a song the way a test fixture would, purely in the model.
    Song song;
    song.settings.tempo = 175;
    song.settings.syncMode = SyncMode::Midi;
    WaveInstrument w; w.synth = 2; w.playMode = WavePlayMode::PingPong; w.speed = 6;
    song.instruments[1] = w;

    const std::string json = songToJson(song);
    auto song2 = songFromJson(json);
    if (!song2) FAIL("songFromJson failed: " << song2.error().what());
    // Authoring path: encode with no template, decode, re-serialize -> stable.
    const auto bytes = codec::encodeSong(song2.value());
    auto song3 = codec::decodeSong(bytes);
    if (!song3) FAIL("decode failed: " << song3.error().what());
    CHECK(song3.value().settings.tempo == 175);
    CHECK(song3.value().settings.syncMode == SyncMode::Midi);
    REQUIRE(song3.value().instruments[1]);
}

TEST_CASE("DefaultIfMissing enables partial fixtures", "[lsdj-sav]") {
    // A fixture specifies only what it cares about; everything else defaults.
    auto sav = savFromJsonFixture(R"({"workingSong":{"settings":{"tempo":150}}})");
    if (!sav) FAIL("savFromJsonFixture failed: " << sav.error().what());
    CHECK(sav.value().workingSong.settings.tempo == 150);    // specified
    CHECK(sav.value().workingSong.formatVersion == 22);      // model default
    CHECK(sav.value().activeProjectIndex == 0xFF);           // model default

    auto song = songFromJsonFixture(R"({"settings":{"syncMode":"Lsdj"}})");
    if (!song) FAIL("songFromJsonFixture failed: " << song.error().what());
    CHECK(song.value().settings.syncMode == model::SyncMode::Lsdj); // specified
    CHECK(song.value().settings.tempo == 128);                      // default
}
