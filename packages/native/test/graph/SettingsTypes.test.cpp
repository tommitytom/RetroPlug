// The fixed-capacity settings types (docs/modular-graph-phase-e1-design.md section 5): rp::text and rp::list read and
// write the JSON the std::string and std::vector they replace did, an over-long value is refused naming its field, and
// an rp::resource_ref keeps the document's "res:N" while storing the number.
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>

#include <rfl/DefaultIfMissing.hpp>
#include <rfl/json.hpp>

#include "GraphRig.hpp"
#include "graph/node.hpp"

using rp::graph::test::GraphRig;

namespace settings_types_test {

// The same fields twice: as std::string and std::vector hold them, and as rp::text and rp::list do. Named namespace, as
// every Settings struct is.
struct Before {
    std::vector<std::uint32_t> counts = {1, 1};
    std::string                layout = "stereo";
    std::string                empty;
};
struct After {
    rp::list<std::uint32_t, 16> counts = {1, 1};
    rp::text<16>                layout = "stereo";
    rp::text<8>                 empty;
};

struct Refs {
    rp::resource_ref<"rom">  rom;
    rp::resource_ref<"sram"> sram;
};

template <class S>
rfl::Result<S> readJson(const std::string& json) {
    return rfl::json::read<S, rfl::DefaultIfMissing>(json);
}

}  // namespace settings_types_test

using namespace settings_types_test;

TEST_CASE("text and list write the JSON a string and a vector did, defaults included", "[settings-types]") {
    CHECK(rfl::json::write(After{}) == rfl::json::write(Before{}));

    for (const std::string json : {R"({})", R"({"counts":[],"layout":""})", R"({"counts":[7],"layout":"5.1"})",
                                   R"x({"counts":[1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16],"layout":"discrete(64)"})x",
                                   R"({"layout":"héllo \"q\" \\ \/","empty":"abcdefgh"})"}) {
        const auto before = readJson<Before>(json);
        const auto after = readJson<After>(json);
        REQUIRE(before.has_value());
        REQUIRE(after.has_value());
        CHECK(after->layout.view() == before->layout);
        CHECK(std::vector<std::uint32_t>(after->counts.begin(), after->counts.end()) == before->counts);
        CHECK(rfl::json::write(*after) == rfl::json::write(*before));
    }
}

TEST_CASE("text and list have the JSON Schema of the types they replace", "[settings-types]") {
    CHECK(rfl::json::to_schema<rp::text<16>>() == rfl::json::to_schema<std::string>());
    CHECK(rfl::json::to_schema<rp::list<std::uint32_t, 16>>() == rfl::json::to_schema<std::vector<std::uint32_t>>());
    CHECK(rfl::json::to_schema<rp::list<double, 4>>() == rfl::json::to_schema<std::vector<double>>());
}

TEST_CASE("a text and a list hold their bound and refuse one past it, naming the field", "[settings-types]") {
    const auto atText = readJson<After>(R"({"layout":"0123456789abcdef"})");  // 16 bytes
    REQUIRE(atText.has_value());
    CHECK(atText->layout.size() == 16);
    const auto overText = readJson<After>(R"({"layout":"0123456789abcdefg"})");  // 17
    REQUIRE_FALSE(overText.has_value());
    CHECK(overText.error().what().find("'layout'") != std::string::npos);

    // The bound is bytes: five two-byte code points fit eight, a fifth three-byte one does not.
    CHECK(readJson<After>(R"({"empty":"éééé"})").has_value());
    CHECK_FALSE(readJson<After>(R"({"empty":"ééééé"})").has_value());

    const auto atList = readJson<After>(R"({"counts":[1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1]})");  // 16
    REQUIRE(atList.has_value());
    CHECK(atList->counts.size() == 16);
    const auto overList = readJson<After>(R"({"counts":[1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1]})");  // 17
    REQUIRE_FALSE(overList.has_value());
    CHECK(overList.error().what().find("'counts'") != std::string::npos);

    // An element that is not an element still reports as it did.
    CHECK_FALSE(readJson<After>(R"({"counts":[1,-1]})").has_value());
    CHECK_FALSE(readJson<After>(R"({"layout":3})").has_value());
}

TEST_CASE("text and list as values: compare, view, assign and fill", "[settings-types]") {
    rp::text<4> t = "ab";
    CHECK(t == "ab");
    CHECK(t == std::string_view("ab"));
    CHECK(t.view() == "ab");
    CHECK(t.str() == "ab");
    CHECK_FALSE(t.empty());
    CHECK(t.assign("abcd"));
    CHECK_FALSE(t.assign("abcde"));
    CHECK(t == "abcd");  // a refused assign changes nothing
    CHECK(rp::text<4>().empty());
    CHECK(rp::text<4>::capacity() == 4);

    rp::list<int, 3> l = {1, 2};
    CHECK(l.size() == 2);
    CHECK(l[1] == 2);
    CHECK(l.push_back(3));
    CHECK_FALSE(l.push_back(4));
    CHECK(l.size() == 3);
    CHECK(l == rp::list<int, 3>{1, 2, 3});
    CHECK_FALSE(l == rp::list<int, 3>{1, 2});
    int sum = 0;
    for (int v : l) sum += v;
    CHECK(sum == 6);
    CHECK(l.view().size() == 3);
}

TEST_CASE("a resource_ref round-trips res:N as a number, and empty as none", "[settings-types]") {
    const auto r = readJson<Refs>(R"({"rom":"res:7"})");
    REQUIRE(r.has_value());
    CHECK(r->rom.id == 7);
    CHECK_FALSE(r->rom.empty());
    CHECK(r->rom.ref() == "res:7");
    CHECK(r->sram.empty());
    CHECK(rfl::json::write(*r) == R"({"rom":"res:7","sram":""})");

    const auto none = readJson<Refs>(R"({"rom":"","sram":"res:4294967295"})");
    REQUIRE(none.has_value());
    CHECK(none->rom.empty());
    CHECK(none->sram.id == 4294967295u);
    CHECK(rfl::json::write(*none) == R"({"rom":"","sram":"res:4294967295"})");
    CHECK(rfl::json::write(Refs{}) == R"({"rom":"","sram":""})");

    for (const char* bad : {"res:0", "res:007", "res:", "res:1x", "7", "rom"}) {
        const auto refused = readJson<Refs>(std::string(R"({"rom":")") + bad + R"("})");
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().what().find("'rom'") != std::string::npos);
        CHECK(refused.error().what().find("is not a resource reference") != std::string::npos);
    }
    CHECK(rfl::json::to_schema<rp::resource_ref<"rom">>() == rfl::json::to_schema<std::string>());
}

// The converted nodes plan and run as they did through the cases that already place them: "split, join and downmix move
// channels explicitly, in order", "5.1 downmixes to stereo ..." and "join fixes a mono-into-stereo mismatch; relabel
// asserts a layout" ([core], CoreNodes.test.cpp), the buffer-player cases there, the [dsp-resample] cases (a resample's
// layout), and the engine-graph case that plans and runs every buildGraph golden document (buildGraph emits the joins and splits),
// whose audio the parity groups hold to the bit.
TEST_CASE("the core nodes refuse an over-long list or layout naming the field", "[settings-types][core]") {
    GraphRig rig;
    const auto refusedWith = [&](const std::string& settings, const std::string& type, const std::string& field) {
        const auto r = rig.set(R"({"nodes":[{"id":"n","type":")" + type + R"(","settings":)" + settings + R"(}],"wires":[]})");
        REQUIRE_FALSE(r.ok);
        REQUIRE(r.errors.size() == 1);
        CHECK(r.errors[0].code == "settings-invalid");
        CHECK(r.errors[0].message.find("'" + field + "'") != std::string::npos);
    };
    // 16 ports plans, 17 does not: the bound the validator holds is also the storage's.
    CHECK(rig.set(R"x({"nodes":[{"id":"j","type":"rp.core.join","settings":{"inputs":[1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1],"layout":"discrete(16)"}}],"wires":[]})x").ok);
    refusedWith(R"x({"inputs":[1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1],"layout":"discrete(17)"})x", "rp.core.join", "inputs");
    refusedWith(R"x({"outputs":[1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1],"layout":"discrete(17)"})x", "rp.core.split", "outputs");
    // A layout name is 16 bytes at most; the longest any table accepts, "discrete(64)", is 12.
    CHECK(rig.set(R"x({"nodes":[{"id":"p","type":"rp.core.relabel","settings":{"channels":64,"layout":"discrete(64)"}}],"wires":[]})x").ok);
    refusedWith(R"x({"channels":1,"layout":"discrete(0000000001)"})x", "rp.core.relabel", "layout");
    refusedWith(R"({"from":"stereo","to":"0123456789abcdefg"})", "rp.core.downmix", "to");
    refusedWith(R"({"from":"0123456789abcdefg","to":"mono"})", "rp.core.downmix", "from");
}
