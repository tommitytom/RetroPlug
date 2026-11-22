// pack.json (docs/downloadable-packs-loading-design.md section 1): a manifest parses whole, the staged fixture's
// included, and each malformed member is refused with a reason that names it.
//
// Run via `pnpm test:plugin packs-test` or `build/bin/retroplug-packs-test "[packs-folders]"`.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <string>
#include <utility>
#include <vector>

#include "PackTestSupport.hpp"
#include "graph/rp_pack.h"
#include "host/packs/PackManifest.hpp"

using Catch::Matchers::ContainsSubstring;
using namespace rp::host::packs;

namespace {

// A manifest's text with one member's value replaced (or the member dropped, for an empty value).
std::string manifestWith(const std::string& member, const std::string& value) {
    std::string out = "{";
    const std::vector<std::pair<std::string, std::string>> base = {
        {"id", R"("demo")"},
        {"name", R"("Demo")"},
        {"version", R"("1.2.0")"},
        {"apiLevel", std::to_string(RP_API_LEVEL)},
        {"license", R"("MIT")"},
        {"source", R"("https://example.invalid/demo")"},
        {"requires", R"({"packs":[],"platforms":[]})"},
        {"provides", R"({"platforms":[],"nodeTypes":["rp.demo.tone"],"roles":[]})"},
        {"halves", R"({"native":"rp-demo"})"},
    };
    bool first = true;
    for (const auto& [key, v] : base) {
        const std::string& text = key == member ? value : v;
        if (text.empty()) continue;
        out += (first ? "\"" : ",\"") + key + "\":" + text;
        first = false;
    }
    return out + "}";
}

}  // namespace

TEST_CASE("pack.json parses whole, and each malformed member is refused by name", "[packs-folders]") {
    std::string why;
    const auto good = parsePackManifest(manifestWith("", ""), &why);
    INFO(why);
    REQUIRE(good);
    CHECK(good->id == "demo");
    CHECK(good->version == "1.2.0");
    CHECK(good->apiLevel == RP_API_LEVEL);
    CHECK(good->nativeHalf() == "rp-demo");
    CHECK(good->providesNodeTypes == std::vector<std::string>{"rp.demo.tone"});

    // The staged fixture's manifest, stamped by cmake/packs.cmake, is one too.
    const auto staged = readPackManifest(rp::host::packs::test::fixture("packfixture"), &why);
    INFO(why);
    REQUIRE(staged);
    CHECK(staged->apiLevel == RP_API_LEVEL);
    CHECK(staged->nativeHalf() == "rp-packfixture");

    const std::vector<std::pair<std::string, std::string>> refused = {
        {manifestWith("id", ""), "id is required"},
        {manifestWith("id", R"("Demo")"), "is not a pack id"},
        {manifestWith("version", R"("1.2")"), "is not MAJOR.MINOR.PATCH"},
        {manifestWith("version", R"("01.2.0")"), "is not MAJOR.MINOR.PATCH"},
        {manifestWith("apiLevel", ""), "apiLevel is required"},
        {manifestWith("apiLevel", "-1"), "apiLevel is required"},
        {manifestWith("license", ""), "license is required"},
        {manifestWith("requires", R"({"packs":["demo"]})"), "names the pack itself"},
        {manifestWith("requires", R"({"packs":["Not An Id"]})"), "which is not a pack id"},
        {manifestWith("provides", R"({"nodeTypes":["a","a"]})"), "names \"a\" twice"},
        {manifestWith("provides", R"({"nodeTypes":[]})"), "provides.nodeTypes is empty"},
        {manifestWith("halves", R"({"native":"rp-demo","wasm":"x.wasm"})"), "halves.wasm is no half"},
        {manifestWith("halves", R"({"native":"../rp-demo"})"), "named after its id: rp-demo"},
        {manifestWith("halves", R"({"native":"rp-other"})"), "named after its id: rp-demo"},
        {manifestWith("halves", R"({"native":"rp-demo","app":"../app.js"})"), "not a path inside the pack"},
        {manifestWith("halves", R"({})"), "names no half"},
        {"[1, 2]", "not a manifest"},
    };
    for (const auto& [text, reason] : refused) {
        INFO(text);
        why.clear();
        CHECK_FALSE(parsePackManifest(text, &why));
        CHECK_THAT(why, ContainsSubstring("pack.json: ") && ContainsSubstring(reason));
    }
}
