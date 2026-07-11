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
// liblsdj's real content savs (named songs, fmt3..16) — ground truth for the
// content-bearing byte-identity tests below.
const fs::path kContentSavDir{RETROPLUG_LSDJ_DIFF_SAV_DIR};

// One representative default sav per distinct on-disk format version (the newest
// LSDj release carrying that format byte). `zeroArchive` = the unused project
// archive is all-zero (so a no-template full-sav encode reproduces it); the
// three oldest formats leave 0xFF power-on SRAM fill there, so only their
// working song is asserted. fmt bytes 1 and 6 have no corpus sav.
struct FmtSav { int fmt; const char* file; bool zeroArchive; };
constexpr FmtSav kFmtSavs[] = {
    {22, "lsdj9_4_2",          true},  {21, "lsdj9_2_0-develop", true},
    {20, "lsdj9_1_A-develop",  true},  {19, "lsdj9_1_4-develop", true},
    {18, "lsdj9_0_1-develop",  true},  {17, "lsdj8_9_6-develop", true},
    {16, "lsdj8_9_2-develop",  true},  {15, "lsdj8_8_7-develop", true},
    {14, "lsdj8_8_5-develop",  true},  {13, "lsdj8_8_0-develop", true},
    {12, "lsdj8_7_7-develop",  true},  {11, "lsdj8_5_1",         true},
    {10, "lsdj8_0_1-develop",  true},  { 9, "lsdj7_9_7-develop", true},
    { 8, "lsdj7_4_4-develop",  true},  { 7, "lsdj7_0_8-develop", true},
    { 5, "lsdj6_6_8-develop",  true},  { 4, "lsdj6_2_0-develop", true},
    { 3, "lsdj5_6_5-develop",  false}, { 2, "lsdj4_3_9-develop", false},
    { 0, "lsdj3_5_1_full",     false},
};
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

// Transient editor/runtime state the model doesn't (and shouldn't) reproduce, so
// these song-relative offsets are allowed to differ in a no-template encode:
//   - work/total-time clock (0x3FB2/3 + 0x3FB6..9): LSDj keeps ticking it.
//   - fileChanged (0x3FC1): the "unsaved edits" dirty flag (0x20 once a song has
//     content, 0 on a fresh sav) — UI state, not musical data.
bool isVolatileByte(std::size_t off) {
    return off == 0x3FB2 || off == 0x3FB3 || (off >= 0x3FB6 && off <= 0x3FB9)
        || off == 0x3FC1;
}
// First non-volatile offset where two buffers differ, or npos if identical apart
// from the volatile editor/runtime bytes.
std::size_t firstDiffExceptClock(std::span<const std::uint8_t> a, std::span<const std::uint8_t> b) {
    const std::size_t n = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < n; ++i)
        if (a[i] != b[i] && !isVolatileByte(i)) return i;
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

// No-template byte-identity: re-encode the decoded song from the MODEL ALONE
// (no source bytes passed as template), so every static byte must come from the
// model rather than being copied through. This is the stronger guarantee — it
// proves the model fully covers the fmt22 working song. Only the volatile
// work/total clock is allowed to differ.
TEST_CASE("working-song encodes byte-identical with no template (fmt22)", "[lsdj-sav]") {
    const fs::path sav = kSavDir / "lsdj9_4_2.sav";
    if (!fs::exists(sav)) { WARN("corpus sav missing"); return; }
    const auto bytes = slurp(sav);
    REQUIRE(bytes.size() >= kSongBytes);
    std::span<const std::uint8_t> orig(bytes.data(), kSongBytes);

    auto res = codec::decodeSong(orig);
    if (!res) FAIL("decode failed: " << res.error().what());
    REQUIRE(res.value().formatVersion == 22);

    const auto out = codec::encodeSong(res.value()); // NO template
    REQUIRE(out.size() == kSongBytes);
    const std::size_t d = firstDiffExceptClock(orig, out);
    if (d != std::string::npos)
        UNSCOPED_INFO("first non-clock diff at song offset 0x" << std::hex << d
                      << " (orig=0x" << int(orig[d]) << " enc=0x" << int(out[d]) << ")");
    CHECK(d == std::string::npos);
}

// The same no-template guarantee across EVERY distinct format version (fmt0..22):
// each representative default sav must re-encode byte-identical from the model
// alone, save the volatile clock. This is the headline coverage claim — the
// model reproduces a fresh LSDj song for every on-disk format.
TEST_CASE("every format version's working song encodes byte-identical (no template)", "[lsdj-sav]") {
    if (!fs::exists(kSavDir)) { WARN("corpus dir missing"); return; }
    std::size_t total = 0, identical = 0;
    std::string fails;
    for (const auto& fv : kFmtSavs) {
        const fs::path sav = kSavDir / (std::string(fv.file) + ".sav");
        if (!fs::exists(sav)) { WARN("missing fmt" << fv.fmt << ": " << fv.file); continue; }
        const auto bytes = slurp(sav);
        if (bytes.size() < kSongBytes) continue;
        ++total;
        std::span<const std::uint8_t> orig(bytes.data(), kSongBytes);
        auto res = codec::decodeSong(orig);
        if (!res) { fails += " fmt" + std::to_string(fv.fmt) + "(decode)"; continue; }
        if (int(res.value().formatVersion) != fv.fmt)
            fails += " fmt" + std::to_string(fv.fmt) + "(ver!=" + std::to_string(res.value().formatVersion) + ")";
        const auto out = codec::encodeSong(res.value()); // no template
        const std::size_t d = firstDiffExceptClock(orig, out);
        if (d == std::string::npos) {
            ++identical;
        } else {
            char b[40]; std::snprintf(b, sizeof b, " fmt%d@0x%zx(0x%02x!=0x%02x)",
                                      fv.fmt, d, int(orig[d]), int(out[d]));
            fails += b;
        }
    }
    UNSCOPED_INFO("no-template working-song byte-identical: " << identical << "/" << total
                  << (fails.empty() ? "" : ("; fails:" + fails)));
    CHECK(total >= 20);
    CHECK(identical == total);
}

// ---- Content-bearing savs (liblsdj fixtures) --------------------------------
// The corpus tests above use EMPTY default savs, which leave the content codec
// paths (instrument encoders, fmt-specific command/field remaps, table FX, synth
// data, names) at zero. liblsdj ships real songs (fmt3..16); these two tests run
// the same byte round-trips over actual content.

// Template round-trip: decode -> encode WITH the original as template -> exact.
// Isolates content-path codec bugs in MODELED regions from model-coverage gaps —
// if a modeled instrument/command/synth field doesn't round-trip, it fails here
// regardless of the unmodeled regions (which the template carries through).
TEST_CASE("content fixtures round-trip byte-identical with template", "[lsdj-sav]") {
    if (!fs::exists(kContentSavDir)) { WARN("content dir missing: " << kContentSavDir.string()); return; }
    std::size_t total = 0, identical = 0;
    std::string fails;
    for (const auto& entry : fs::directory_iterator(kContentSavDir)) {
        if (entry.path().extension() != ".sav") continue;
        const auto bytes = slurp(entry.path());
        if (bytes.size() < kSongBytes) continue;
        ++total;
        std::span<const std::uint8_t> orig(bytes.data(), kSongBytes);
        auto res = codec::decodeSong(orig);
        if (!res) { fails += " " + entry.path().filename().string() + "(decode)"; continue; }
        const auto out = codec::encodeSong(res.value(), orig); // template
        const std::size_t d = firstDiff(orig, out);
        if (d == std::string::npos) {
            ++identical;
        } else {
            char b[24]; std::snprintf(b, sizeof b, "@0x%zx", d);
            fails += " " + entry.path().filename().string() + b;
        }
    }
    UNSCOPED_INFO("content template round-trip: " << identical << "/" << total
                  << (fails.empty() ? "" : ("; fails:" + fails)));
    CHECK(total >= 8);
    CHECK(identical == total);
}

// NOTE: a no-template byte-identity test over content savs is intentionally NOT
// here. Reaching it surfaced (and this suite drove fixes for) several real codec
// gaps — kit volume was a lossy enum, chain 0x7F was dropped, the ADSR envelope
// direction bit and instrument names weren't modeled. What remains is purely
// non-semantic LSDj leftover: an unallocated instrument's reserved bytes and the
// remembered length bits of an infinite-length note (per-instrument {0,0x3F}),
// which LSDj writes but never reads. Reproducing those from the model would mean
// storing raw leftover bytes; the template round-trip above already proves the
// codec preserves all *meaningful* content. (No-template byte-identity is fully
// covered for empty savs across all 21 format versions.)

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
    wave.volume = 0x40;
    wave.synth = 0x3;
    wave.wave = 0x07;             // note: low nibble shares with synth's high nibble
    wave.playMode = WavePlayMode::Loop;
    wave.length = 0xC;
    wave.speed = 8;
    song.instruments[1] = wave;

    KitInstrument kit;
    kit.volume = 0xA8;
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

// A content-bearing no-template round-trip: author a populated song touching
// every newly-modeled raw region (bookmarks / words / wordNames / reserved) plus
// core musical content, encode from the MODEL ALONE (no template), and prove
// encode/decode are mutual inverses (b1 == b2) with the values surviving the
// trip. The fmt22 default test proves the EMPTY song reproduces; this proves a
// FULL one does too, without the template crutch.
TEST_CASE("populated song round-trips byte-stable with no template", "[lsdj-sav]") {
    using namespace rp::lsdj::model;
    Song song;
    song.settings.tempo    = 175;
    song.settings.syncMode = SyncMode::Midi;

    // Raw byte regions: non-default content at both ends of each blob.
    song.bookmarks[0] = 0x0A; song.bookmarks[0x3F] = 0x2B;
    for (std::size_t i = 0; i < song.wordNames.size(); ++i)
        song.wordNames[i] = static_cast<Byte>('A' + (i % 26));
    song.words[0] = 0x10; song.words[song.words.size() - 1] = 0x9C;
    song.reserved3FC6[0] = 0xFF; song.reserved3FC6[3] = 0xFF;
    song.instrumentNames[0] = 'L'; song.instrumentNames[1] = 'D';   // instrument name
    song.synthOverwrites[0] = 0x05; song.synthOverwrites[1] = 0x01; // synth bitset

    // Musical content: a chain -> phrase -> note with an FX, an instrument, a
    // table, a non-default groove, a synth, and a wave frame.
    song.rows[0].chains[0] = 0;
    Chain ch; ch.phrases[0] = 0; ch.transpositions[0] = 12; song.chains[0] = ch;
    Phrase ph; ph.notes[0] = 40; ph.instruments[0] = 0;
    ph.commands[0] = Command::G; ph.commandValues[0] = 0x34; song.phrases[0] = ph;
    // ADSR speeds with bit 3 set exercise the envelope-direction bit the model
    // used to drop (a 3-bit field would truncate 0xC/0xA/0x9 to 4/2/1).
    PulseInstrument pulse;
    pulse.adsr.initialLevel = 15;
    pulse.adsr.attackSpeed = 0xC; pulse.adsr.decaySpeed = 0x9; pulse.adsr.releaseSpeed = 0xA;
    song.instruments[0] = pulse;
    Table tbl; tbl.volumes[0] = 8; song.tables[0] = tbl;
    song.grooves[5].steps[0] = 4;
    song.synths[0].waveform = SynthWaveform::Square;
    song.waves[10].frames[0] = 0xAB;

    const auto b1 = codec::encodeSong(song);          // no template
    auto dec = codec::decodeSong(b1);
    if (!dec) FAIL("decode failed: " << dec.error().what());
    const auto b2 = codec::encodeSong(dec.value());   // no template
    CHECK(b1 == b2);                                   // mutual inverse, no crutch

    const Song& m = dec.value();
    CHECK(m.settings.tempo == 175);
    CHECK(m.settings.syncMode == SyncMode::Midi);
    CHECK(m.bookmarks[0] == 0x0A);
    CHECK(m.bookmarks[0x3F] == 0x2B);
    CHECK(m.wordNames[5] == static_cast<Byte>('A' + 5));
    CHECK(m.words[0] == 0x10);
    CHECK(m.words[m.words.size() - 1] == 0x9C);
    CHECK(m.reserved3FC6[0] == 0xFF);
    CHECK(m.reserved3FC6[3] == 0xFF);
    CHECK(m.instrumentNames[0] == 'L');
    CHECK(m.synthOverwrites[0] == 0x05);
    REQUIRE(m.chains[0]);  CHECK(m.chains[0]->transpositions[0] == 12);
    REQUIRE(m.phrases[0]); CHECK(m.phrases[0]->notes[0] == 40);
    CHECK(m.phrases[0]->commands[0] == Command::G);
    CHECK(m.phrases[0]->commandValues[0] == 0x34);
    REQUIRE(m.instruments[0]);
    m.instruments[0]->visit([](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, PulseInstrument>) {
            CHECK(v.adsr.attackSpeed.value() == 0xC);   // envelope-direction bit survives
            CHECK(v.adsr.decaySpeed.value() == 0x9);
            CHECK(v.adsr.releaseSpeed.value() == 0xA);
        }
    });
    REQUIRE(m.tables[0]);  CHECK(m.tables[0]->volumes[0] == 8);
    CHECK(m.grooves[5].steps[0] == 4);
    CHECK(m.synths[0].waveform == SynthWaveform::Square);
    CHECK(m.waves[10].frames[0] == 0xAB);
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

// The full 128 KiB no-template guarantee for the zero-archive formats (fmt4..22):
// the whole image — working song, header magic, alloc table, and the empty
// project archive — reproduces from the model alone. (The 3 oldest formats leave
// 0xFF power-on SRAM fill in the unused archive, which is environment state, not
// codec content, so they're covered at the working-song level above.)
TEST_CASE("zero-archive formats encode the full sav byte-identical (no template)", "[lsdj-sav]") {
    if (!fs::exists(kSavDir)) { WARN("corpus dir missing"); return; }
    std::size_t total = 0, identical = 0;
    std::string fails;
    for (const auto& fv : kFmtSavs) {
        if (!fv.zeroArchive) continue;
        const fs::path sav = kSavDir / (std::string(fv.file) + ".sav");
        if (!fs::exists(sav)) { WARN("missing fmt" << fv.fmt); continue; }
        const auto bytes = slurp(sav);
        if (bytes.size() != codec::kSavSize) continue;
        ++total;
        std::span<const std::uint8_t> orig(bytes.data(), bytes.size());
        auto res = codec::decodeSav(orig);
        if (!res) { fails += " fmt" + std::to_string(fv.fmt) + "(decode)"; continue; }
        const auto out = codec::encodeSav(res.value()); // no template
        const std::size_t d = firstDiffExceptClock(orig, out);
        if (d == std::string::npos) {
            ++identical;
        } else {
            char b[40]; std::snprintf(b, sizeof b, " fmt%d@0x%zx(0x%02x!=0x%02x)",
                                      fv.fmt, d, int(orig[d]), int(out[d]));
            fails += b;
        }
    }
    UNSCOPED_INFO("no-template full-sav byte-identical: " << identical << "/" << total
                  << (fails.empty() ? "" : ("; fails:" + fails)));
    CHECK(total >= 15);
    CHECK(identical == total);
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
    sav.projects[0] = std::make_shared<StoredProject>(proj);

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

TEST_CASE("short fixed arrays pad to full length with defaults", "[lsdj-sav]") {
    using namespace rp::lsdj::model;
    // A fixture supplies only the cells it cares about; every fixed array is
    // padded to its on-disk length with default elements (and inner arrays too).
    auto sav = savFromJsonFixture(R"({"workingSong":{
        "rows":[{"chains":[0]}],
        "chains":[{"phrases":[0]}],
        "phrases":[{"notes":[1],"instruments":[0]}],
        "instruments":[{"type":"pulse"}]
    }})");
    if (!sav) FAIL("savFromJsonFixture failed: " << sav.error().what());
    const Song& s = sav.value().workingSong;

    // Outer arrays padded to full count.
    REQUIRE(s.rows.size() == 256);
    REQUIRE(s.chains.size() == 128);
    REQUIRE(s.phrases.size() == 256);
    REQUIRE(s.instruments.size() == 64);
    // Authored cells survive; padded siblings/tails take defaults.
    CHECK(s.rows[0].chains[0] == 0);          // authored
    CHECK(!s.rows[0].chains[1]);              // inner pad -> nullopt
    CHECK(!s.rows[255].chains[0]);           // outer pad -> default SongRow
    REQUIRE(s.chains[0]);
    CHECK(s.chains[0]->phrases[0] == 0);
    CHECK(!s.chains[1]);                      // unallocated
    REQUIRE(s.phrases[0]);
    CHECK(s.phrases[0]->notes[0] == 1);
    CHECK(s.phrases[0]->notes[1] == 0);                          // inner pad
    CHECK(s.phrases[0]->commands[0] == Command::None);           // omitted -> default
    REQUIRE(s.instruments[0]);

    // The padded model is byte-identical to authoring every array in full.
    Song full;
    full.rows[0].chains[0] = 0;
    full.chains[0] = Chain{};   full.chains[0]->phrases[0] = 0;
    full.phrases[0] = Phrase{}; full.phrases[0]->notes[0] = 1; full.phrases[0]->instruments[0] = 0;
    full.instruments[0] = PulseInstrument{};
    CHECK(codec::encodeSong(s) == codec::encodeSong(full));

    // More than the fixed length is an error, not a silent truncation.
    auto overflow = savFromJsonFixture(R"({"workingSong":{"rows":[{"chains":[0,1,2,3,4]}]}})");
    CHECK(!overflow);
}
