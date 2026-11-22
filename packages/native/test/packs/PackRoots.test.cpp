// Where packs live (docs/downloadable-packs-loading-design.md section 1): each platform's data and cache roots and
// their overrides, the platform folder a pack's libraries sit in, and the development pack path. Nothing here reads
// or writes a folder: the roots are computed from the environment, which each case sets for itself.
//
// Run via `pnpm test:plugin packs-test` or `build/bin/retroplug-packs-test "[packs-folders]"`.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

#include "PackTestSupport.hpp"
#include "graph/rp_pack.h"
#include "host/packs/PackDirs.hpp"

namespace fs = std::filesystem;
using namespace rp::host::packs;
using rp::host::packs::test::ScopedEnv;

TEST_CASE("the pack roots follow the platform's convention, and the overrides win", "[packs-folders]") {
    {
        const ScopedEnv data("RETROPLUG_PACKS_DIR", "/scratch/data");
        const ScopedEnv cache("RETROPLUG_PACKS_CACHE_DIR", "/scratch/cache");
        const PackRoots roots = defaultPackRoots();
        CHECK(roots.data == fs::path("/scratch/data"));
        CHECK(roots.cache == fs::path("/scratch/cache"));
    }
    const ScopedEnv data("RETROPLUG_PACKS_DIR", std::nullopt);
    const ScopedEnv cache("RETROPLUG_PACKS_CACHE_DIR", std::nullopt);
#if defined(_WIN32)
    const ScopedEnv local("LOCALAPPDATA", "C:\\Users\\me\\AppData\\Local");
    const PackRoots roots = defaultPackRoots();
    CHECK(roots.data == fs::path("C:\\Users\\me\\AppData\\Local") / "RetroPlug" / "packs");
    CHECK(roots.cache == fs::path("C:\\Users\\me\\AppData\\Local") / "RetroPlug" / "cache" / "packs");
#elif defined(__APPLE__)
    const ScopedEnv home("HOME", "/Users/me");
    const PackRoots roots = defaultPackRoots();
    CHECK(roots.data == fs::path("/Users/me/Library/Application Support/RetroPlug/packs"));
    CHECK(roots.cache == fs::path("/Users/me/Library/Caches/RetroPlug/packs"));
#else
    const ScopedEnv home("HOME", "/home/me");
    {
        const ScopedEnv xdgData("XDG_DATA_HOME", std::nullopt);
        const ScopedEnv xdgCache("XDG_CACHE_HOME", std::nullopt);
        const PackRoots roots = defaultPackRoots();
        CHECK(roots.data == fs::path("/home/me/.local/share/retroplug/packs"));
        CHECK(roots.cache == fs::path("/home/me/.cache/retroplug/packs"));
    }
    {
        // A relative base directory is invalid under the XDG spec and ignored.
        const ScopedEnv xdgData("XDG_DATA_HOME", "relative/data");
        const ScopedEnv xdgCache("XDG_CACHE_HOME", "relative/cache");
        const PackRoots roots = defaultPackRoots();
        CHECK(roots.data == fs::path("/home/me/.local/share/retroplug/packs"));
        CHECK(roots.cache == fs::path("/home/me/.cache/retroplug/packs"));
    }
    const ScopedEnv xdgData("XDG_DATA_HOME", "/xdg/data");
    const ScopedEnv xdgCache("XDG_CACHE_HOME", "/xdg/cache");
    const PackRoots roots = defaultPackRoots();
    CHECK(roots.data == fs::path("/xdg/data/retroplug/packs"));
    CHECK(roots.cache == fs::path("/xdg/cache/retroplug/packs"));
#endif
}

TEST_CASE("the platform folder and the API level are the build's", "[packs-folders]") {
    // cmake/packs.cmake stages a module under the folder it names and stamps the level it reads from rp_pack.h; the
    // loader looks where currentPlatform() says and compares with RP_API_LEVEL. The two sides must agree.
    CHECK(std::string(currentPlatform()) == RP_PACK_PLATFORM_CMAKE);
    CHECK(RP_API_LEVEL == RP_API_LEVEL_CMAKE);
    CHECK(libraryFileName("rp-demo").rfind("rp-demo.", 0) == 0);
}

TEST_CASE("RETROPLUG_PACK_PATH splits as PATH does", "[packs-folders]") {
#if defined(_WIN32)
    const auto dirs = splitPackPath("C:\\a;;C:\\b c;");
    REQUIRE(dirs.size() == 2);
    CHECK(dirs[0] == fs::path("C:\\a"));
    CHECK(dirs[1] == fs::path("C:\\b c"));
#else
    const auto dirs = splitPackPath("/a::/b c:");
    REQUIRE(dirs.size() == 2);
    CHECK(dirs[0] == fs::path("/a"));
    CHECK(dirs[1] == fs::path("/b c"));
#endif
    // The environment's value goes through the same splitter, wide on Windows.
#if defined(_WIN32)
    const ScopedEnv fromEnv("RETROPLUG_PACK_PATH", "C:\\a;;C:\\b c;");
    const fs::path first = "C:\\a", second = "C:\\b c";
#else
    const ScopedEnv fromEnv("RETROPLUG_PACK_PATH", "/a::/b c:");
    const fs::path first = "/a", second = "/b c";
#endif
    const auto fromVar = packPathFromEnv();
    REQUIRE(fromVar.size() == 2);
    CHECK(fromVar[0] == first);
    CHECK(fromVar[1] == second);
    const ScopedEnv unset("RETROPLUG_PACK_PATH", std::nullopt);
    CHECK(packPathFromEnv().empty());
}
