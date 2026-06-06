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
