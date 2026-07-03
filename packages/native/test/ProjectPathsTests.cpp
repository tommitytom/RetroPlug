// Unit tests for the .rplg path-rebasing visitor (project-relative on disk,
// absolute in memory). See project/ProjectPaths.hpp.
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

#include "project/ProjectConfig.hpp"
#include "project/ProjectPaths.hpp"
#include "system/SystemConfig.hpp"
#include "system/mesen/MesenGbaConfig.hpp"
#include "system/sameboy/SameBoyConfig.hpp"
#include "system/sameboy/roles/LsdjKitPatchRole.hpp"

namespace fs = std::filesystem;

namespace {

// A base dir under the system temp root. It needn't exist on disk —
// weakly_canonical normalises the existing prefix (the temp root) and appends
// the rest lexically, which is all these tests rely on.
fs::path baseDir() { return fs::temp_directory_path() / "rp_paths_base"; }

const SameBoyConfig& sameboy(const ProjectConfig& cfg, std::size_t i = 0) {
    const auto* sb = rfl::get_if<SameBoyConfig>(&cfg.systems.at(i).variant());
    REQUIRE(sb != nullptr);
    return *sb;
}

const MesenGbaConfig& gba(const ProjectConfig& cfg, std::size_t i) {
    const auto* gb = rfl::get_if<MesenGbaConfig>(&cfg.systems.at(i).variant());
    REQUIRE(gb != nullptr);
    return *gb;
}

// SameBoy(rom+sav+one kit sample) + GBA(rom+biosPath), all under `base`.
ProjectConfig makeConfig(const fs::path& base) {
    SameBoyConfig sb;
    sb.romPath = (base / "game.gb").string();
    sb.savPath = (base / "saves" / "game.sav").string();
    rp::lsdj::LsdjKitConfig kit; kit.slot = 0;
    rp::lsdj::LsdjSampleConfig sample; sample.path = (base / "wav" / "kick.wav").string();
    kit.samples.push_back(sample);
    rp::lsdj::LsdjKitPatchConfig patch; patch.kits.push_back(kit);
    sb.roles.emplace_back(std::move(patch));

    MesenGbaConfig gb;
    gb.romPath  = (base / "tune.gba").string();
    gb.biosPath = (base / "gba_bios.bin").string();   // under base — but must NOT rebase

    ProjectConfig cfg;
    cfg.systems.push_back(sb);
    cfg.systems.push_back(gb);
    return cfg;
}

} // namespace

TEST_CASE("project-paths: toRelative rebases assets under the project dir", "[paths]") {
    const fs::path base = baseDir();
    ProjectConfig cfg = makeConfig(base);
    rp::project_paths::toRelative(cfg, base.string());

    const auto& sb = sameboy(cfg);
    CHECK(sb.romPath == "game.gb");
    CHECK(sb.savPath == "saves/game.sav");            // forward slashes, cross-platform
    const auto* patch = rfl::get_if<rp::lsdj::LsdjKitPatchConfig>(&sb.roles.front().variant());
    REQUIRE(patch != nullptr);
    CHECK(patch->kits.at(0).samples.at(0).path == "wav/kick.wav");

    CHECK(gba(cfg, 1).romPath == "tune.gba");
    // biosPath is a fixed firmware path, deliberately excluded from rebasing.
    CHECK(gba(cfg, 1).biosPath == (base / "gba_bios.bin").string());
}

TEST_CASE("project-paths: toRelative leaves out-of-dir and empty paths absolute", "[paths]") {
    const fs::path base = baseDir();
    const std::string outside = (fs::temp_directory_path() / "rp_paths_other" / "x.gb").string();

    ProjectConfig cfg;
    SameBoyConfig sb;
    sb.romPath = outside;   // not under base
    sb.savPath = "";        // unset override
    cfg.systems.push_back(sb);

    rp::project_paths::toRelative(cfg, base.string());
    CHECK(sameboy(cfg).romPath == outside);   // stays absolute (no ../ chains)
    CHECK(sameboy(cfg).savPath.empty());       // untouched
}

TEST_CASE("project-paths: toAbsolute round-trips and resolves relatives", "[paths]") {
    const fs::path base = baseDir();

    SECTION("round-trip abs -> rel -> abs") {
        ProjectConfig cfg = makeConfig(base);
        rp::project_paths::toRelative(cfg, base.string());
        rp::project_paths::toAbsolute(cfg, base.string());

        const auto& sb = sameboy(cfg);
        CHECK(sb.romPath == fs::weakly_canonical(base / "game.gb").string());
        CHECK(sb.savPath == fs::weakly_canonical(base / "saves" / "game.sav").string());
        const auto* patch = rfl::get_if<rp::lsdj::LsdjKitPatchConfig>(&sb.roles.front().variant());
        REQUIRE(patch != nullptr);
        CHECK(patch->kits.at(0).samples.at(0).path ==
              fs::weakly_canonical(base / "wav" / "kick.wav").string());
    }

    SECTION("a hand-authored forward-slash relative resolves against base") {
        ProjectConfig cfg;
        SameBoyConfig sb;
        sb.romPath = "roms/game.gb";   // relative, forward slashes
        cfg.systems.push_back(sb);

        rp::project_paths::toAbsolute(cfg, base.string());
        const std::string out = sameboy(cfg).romPath;
        CHECK(fs::path(out).is_absolute());
        CHECK(out == fs::weakly_canonical(base / "roms" / "game.gb").string());
    }

    SECTION("absolute paths pass through toAbsolute unchanged (old saves)") {
        const std::string abs = (base / "already.gb").string();
        ProjectConfig cfg;
        SameBoyConfig sb;
        sb.romPath = abs;
        cfg.systems.push_back(sb);

        rp::project_paths::toAbsolute(cfg, base.string());
        CHECK(sameboy(cfg).romPath == abs);
    }
}
