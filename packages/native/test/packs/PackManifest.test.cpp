// pack.json (docs/downloadable-packs-loading-design.md section 1): a manifest parses whole, the staged fixture's
// included, and each malformed member is refused with a reason that names it.
//
// Run via `pnpm test:plugin packs-test` or `build/bin/retroplug-packs-test "[packs-folders]"`.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <fstream>
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

TEST_CASE("a half's path is printable ASCII and a name Windows can hold", "[packs-folders]") {
    const auto parses = [](const std::string& halves) {
        std::string why;
        return parsePackManifest(manifestWith("halves", halves), &why, std::nullopt).has_value();
    };
    // views is a half of its own (docs/downloadable-packs-m2c-plan.md).
    CHECK(parses(R"({"native":"rp-demo","views":"js/views.js"})"));
    for (const char* bad : {"js/con.js", "js/CON", "js/aux.txt", "js/nul", "js/com1.js", "js/LPT9", "js/app.js.",
                            "js/app.js ", R"(js/app\u0001.js)", R"(js/é.js)", "js/a?.js", "js/a<b.js",
                            "js/a|b.js", "js/a*.js", "js/com0.js", "js/LPT0", "js/con .txt", "js/conin$.js",
                            "js/CONOUT$"}) {
        INFO(bad);
        CHECK_FALSE(parses(std::string(R"({"native":"rp-demo","app":")") + bad + "\"}"));
    }
    CHECK(parses(R"({"native":"rp-demo","app":"js/console.js"})"));  // a prefix of a reserved name is a name
    CHECK(parses(R"({"native":"rp-demo","app":"js/com10.js"})"));
}

TEST_CASE("pack.json parses whole, and each malformed member or file is refused by name", "[packs-folders]") {
    std::string why;
    const auto good = parsePackManifest(manifestWith("", ""), &why, std::nullopt);
    INFO(why);
    REQUIRE(good);
    CHECK(good->id == "demo");
    CHECK(good->version == "1.2.0");
    CHECK(good->apiLevel == RP_API_LEVEL);
    CHECK(good->nativeHalf() == "rp-demo");
    CHECK(good->providesNodeTypes == std::vector<std::string>{"rp.demo.tone"});

    // The staged fixture's manifest, stamped by cmake/packs.cmake, is one too.
    const auto staged = readPackManifest(rp::host::packs::test::fixture("packfixture"), &why, std::nullopt);
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
        {manifestWith("apiLevel", "-1"), "apiLevel is -1, outside 0..4294967295"},
        {manifestWith("apiLevel", "4294967296"), "apiLevel is 4294967296, outside 0..4294967295"},
        {manifestWith("apiLevel", R"("4")"), "not a manifest"},
        {manifestWith("name", ""), "name is required"},
        {manifestWith("source", ""), "source is required"},
        {manifestWith("name", R"("a\u0000b")"), "name holds a control character"},
        {manifestWith("license", R"("a\u001bb")"), "license holds a control character"},
        {manifestWith("source", "\"" + std::string(257, 'x') + "\""), "source is longer than 256 characters"},
        {manifestWith("requires", R"({"packs":["ok"],"platforms":["a b"]})"), "requires.platforms holds"},
        {manifestWith("requires", R"({"pack":["other"]})"), "requires holds \"pack\""},
        {manifestWith("provides", R"({"nodeTypes":["a"],"nodeType":["b"]})"), "provides holds \"nodeType\""},
        {manifestWith("provides", R"({"nodeTypes":["a"],"platforms":["a\"b"]})"), "provides.platforms holds"},
        {manifestWith("provides", R"({"nodeTypes":["a"],"roles":[""]})"), "provides.roles holds"},
        {manifestWith("provides", R"({"nodeTypes":"a"})"), "not a manifest"},
        {manifestWith("id", R"("zzz","id":"demo")"), "the member \"id\" appears twice"},
        {manifestWith("requires", R"({"packs":[],"packs":["x"]})"), "the member \"packs\" appears twice"},
        {manifestWith("name", R"("x","\u006eame":"y")"), "the member \"name\" appears twice"},
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
        CHECK_FALSE(parsePackManifest(text, &why, std::nullopt));
        CHECK_THAT(why, ContainsSubstring("pack.json: ") && ContainsSubstring(reason));
    }

    // Nesting is refused before any parse recurses, wherever it sits and however deep: a 100k-level array under an
    // unknown member of requires, or at the top level, must come back as a refusal, not a stack overflow.
    const std::string deep = std::string(100000, '[') + std::string(100000, ']');
    const std::vector<std::string> deepTexts = {manifestWith("requires", R"({"zzz":)" + deep + "}"),
                                                manifestWith("name", deep), "{\"x\":" + deep + "}", deep};
    for (const std::string& text : deepTexts) {
        why.clear();
        CHECK_FALSE(parsePackManifest(text, &why, std::nullopt));
        CHECK_THAT(why, ContainsSubstring("pack.json: nested deeper than 32 levels"));
    }

    // Deep nesting does not switch the duplicate check off: past the limit it is refused whole, within it the repeat
    // after the nesting is still found.
    for (const auto& [levels, reason] : std::vector<std::pair<int, std::string>>{
             {70, "nested deeper than 32 levels"}, {20, "the member \"id\" appears twice"}}) {
        why.clear();
        CHECK_FALSE(parsePackManifest(R"({"x":)" + std::string(levels, '[') + std::string(levels, ']') +
                                          R"(,"id":"zzz","id":"demo"})",
                                      &why, std::nullopt));
        CHECK_THAT(why, ContainsSubstring(reason));
    }

    // A 1 MiB object of distinct keys is refused or parsed, without a quadratic key check.
    {
        std::string wide = "{";
        for (int i = 0; wide.size() < 1000000; ++i) wide += "\"k" + std::to_string(i) + "\":1,";
        wide += "\"id\":\"demo\"}";
        why.clear();
        CHECK_FALSE(parsePackManifest(wide, &why, std::nullopt));
    }

    // A cause from the parser is echoed longer than an input value, and neither is cut inside a UTF-8 sequence.
    why.clear();
    CHECK_FALSE(parsePackManifest(manifestWith("version", "\"" + std::string(63, 'a') + "\xc3\xa9\xc3\xa9\""), &why,
                                  std::nullopt));
    CHECK_THAT(why, ContainsSubstring(std::string(63, 'a') + "..."));

    // Hostile input is echoed escaped and bounded, never raw.
    why.clear();
    CHECK_FALSE(parsePackManifest(manifestWith("version", "\"1\\u001b[31m" + std::string(200, 'z') + "\""), &why,
                                  std::nullopt));
    CHECK(why.find('\x1b') == std::string::npos);
    CHECK(why.size() < 200);
    CHECK_THAT(why, ContainsSubstring("\\x1b"));

    // And it is read only as a bounded regular file.
    rp::host::packs::test::TempDir dir;

    // A directory named pack.json is unreadable, not an empty manifest.
    std::filesystem::create_directory(dir.path() / "pack.json");
    CHECK_FALSE(readPackManifest(dir.path(), &why, std::nullopt));
    CHECK_THAT(why, ContainsSubstring("cannot read"));

    std::filesystem::remove(dir.path() / "pack.json");
    {
        std::ofstream out(dir.path() / "pack.json", std::ios::binary);
        out << manifestWith("", "") << std::string(1024 * 1024 + 1, ' ');
    }
    why.clear();
    CHECK_FALSE(readPackManifest(dir.path(), &why, std::nullopt));
    CHECK_THAT(why, ContainsSubstring("too large"));
}
