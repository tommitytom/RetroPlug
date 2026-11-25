// A pack's protocol half (docs/downloadable-packs-m2e-plan.md): its global-code node bundle compiled and trial-loaded
// once by the table, beside the built-in script nodes, and refused for a type it does not declare, one it declares and
// does not define, a clash, a bundle that does not compile or throws; its data in the pack list the TS loader reads.
// Every Engine's own load of it is the next step's.
//
// Run via `pnpm test:plugin packs-test` or `build/bin/retroplug-packs-test "[packs-protocol]"`.
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "PackTestSupport.hpp"
#include "host/js/PackLoaderCore.hpp"
#include "host/packs/PackTable.hpp"
#include "host/packs/ProtocolTrial.hpp"
#include "quickjs.h"

namespace fs = std::filesystem;
using rp::host::packs::PackTable;
using rp::host::packs::test::fixture;
using rp::host::packs::test::TempDir;

namespace {

// The table with the backend's trial, as the process's has.
void withTrial(PackTable& table) { table.setProtocolTrial(rp::host::packs::protocolTrial()); }

// A bare QuickJS runtime, as a render job's (host/render/RenderHost): no txiki, a job queue run here.
struct BareRuntime {
    JSRuntime* rt = JS_NewRuntime();
    JSContext* ctx = JS_NewContext(rt);
    ~BareRuntime() {
        JS_RunGC(rt);
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
    }
    void drain() {
        JSContext* jc = nullptr;
        while (JS_ExecutePendingJob(rt, &jc) > 0) {}
    }
    std::string eval(const std::string& code, int flags) {
        JSValue v = JS_Eval(ctx, code.c_str(), code.size(), "<protocol-test>", flags);
        REQUIRE_FALSE(JS_IsException(v));
        JS_FreeValue(ctx, v);
        drain();
        JSValue g = JS_GetGlobalObject(ctx);
        JSValue s = JS_GetPropertyStr(ctx, g, "__pt");
        const char* c = JS_ToCString(ctx, s);
        std::string out = c ? c : "<none>";
        JS_FreeCString(ctx, c);
        JS_FreeValue(ctx, s);
        JS_SetPropertyStr(ctx, g, "__pt", JS_UNDEFINED);
        JS_FreeValue(ctx, g);
        return out;
    }
    std::string importResult(const std::string& name) {
        return eval("import('" + name + "').then(() => { globalThis.__pt = 'ok'; }, (e) => { globalThis.__pt = 'error ' + "
                    "e.message; });",
                    JS_EVAL_TYPE_MODULE);
    }
    std::string packList() { return eval("globalThis.__pt = globalThis.__rp_packList();", JS_EVAL_TYPE_GLOBAL); }
};

}  // namespace

TEST_CASE("a protocol half is compiled and trial-loaded once, its types and descriptors kept", "[packs-protocol]") {
    PackTable table;
    withTrial(table);
    const auto load = table.load(fixture("protofix"));
    INFO(load.error);
    REQUIRE(load);
    REQUIRE(load.pack->protocol);
    const auto& half = *load.pack->protocol;
    CHECK(half.scriptId == "rp.protofix.protocol");
    CHECK_FALSE(half.bytecode.empty());
    CHECK(half.typeIds == std::vector<std::string>{"rp.protofix.up", "rp.protofix.idle"});
    REQUIRE(half.descriptors.size() == 2);
    CHECK(half.descriptors[0].find(R"("id":"rp.protofix.up")") != std::string::npos);
    CHECK(half.descriptors[0].find(R"("id":"midiOut")") != std::string::npos);
    CHECK(load.pack->packages.empty());  // no native half and no script half
}

TEST_CASE("a protocol half is refused for what it defines, what it misses, a clash, a compile and a throw",
          "[packs-protocol]") {
    PackTable table;
    withTrial(table);
    const auto refused = [&](const std::string& id, const std::string& part) {
        const auto load = table.load(fixture(id));
        CHECK_FALSE(load);
        INFO(id << ": " << load.error);
        CHECK(load.error.find(part) != std::string::npos);
        CHECK(table.find(id) == nullptr);
    };
    refused("protofix-undeclared", "pack protofix-undeclared's protocol half defines node type "
                                   "rp.protofix-undeclared.extra, which its pack.json's protocol.nodes does not declare");
    refused("protofix-missing", "pack protofix-missing's pack.json's protocol.nodes declares rp.protofix-missing.gone, "
                                "which its protocol half does not define");
    refused("protofix-clash", "rp.midi.route");
    refused("protofix-syntax", "pack protofix-syntax: its protocol half does not compile");
    refused("protofix-throws", "the protocol fixture throws");
}

TEST_CASE("a table with no protocol trial refuses a protocol half", "[packs-protocol]") {
    PackTable table;
    const auto load = table.load(fixture("protofix"));
    CHECK_FALSE(load);
    CHECK(load.error == "pack protofix has a protocol half, and this host loads none");
}

TEST_CASE("a pack whose type an earlier pack's protocol half holds is refused", "[packs-protocol]") {
    PackTable table;
    withTrial(table);
    REQUIRE(table.load(fixture("protofix")));

    // A second pack naming protofix's type under another id, with protofix's bundle copied in.
    TempDir tmp;
    const fs::path dir = tmp.path() / "protofix-again";
    fs::create_directories(dir / "js");
    fs::copy_file(fixture("protofix") / "js" / "protocol.js", dir / "js" / "protocol.js");
    std::ofstream(dir / "pack.json")
        << "{\"id\": \"protofix-again\", \"name\": \"n\", \"version\": \"1.0.0\", \"apiLevel\": " << RP_API_LEVEL_CMAKE
        << ", \"license\": \"MIT\", \"source\": \"s\", \"requires\": {\"packs\": [], \"platforms\": []}, "
           "\"provides\": {\"platforms\": [], \"nodeTypes\": [\"rp.protofix.up\", \"rp.protofix.idle\"], \"roles\": []}, "
           "\"halves\": {\"protocol\": \"js/protocol.js\"}, \"protocol\": {\"nodes\": [{\"type\": \"rp.protofix.up\", "
           "\"midiIn\": \"in\", \"writes\": [], \"midiOut\": true}, {\"type\": \"rp.protofix.idle\"}], \"roles\": {}}}";
    const auto load = table.load(dir);
    CHECK_FALSE(load);
    CHECK(load.error == "pack protofix-again registers node type rp.protofix.up, which package rp.protofix.protocol has "
                        "registered already");
    CHECK(table.find("protofix-again") == nullptr);
}

TEST_CASE("the pack list carries a protocol half's data, and pack:<id>/protocol is no module", "[packs-protocol]") {
    PackTable table;
    withTrial(table);
    REQUIRE(table.load(fixture("protofix")));
    BareRuntime rt;
    rp::host::installBarePackLoader(rt.ctx, table, nullptr, 0);

    const std::string list = rt.packList();
    INFO(list);
    const std::size_t halves = list.find(R"("halves":["protocol"])");
    REQUIRE(halves != std::string::npos);
    const std::string expected =
        R"("protocol":{"nodes":[{"midiIn":"in","midiOut":true,"type":"rp.protofix.up","writes":[]},{"type":"rp.protofix.idle"}],"roles":{},"descriptors":{"rp.protofix.up":{)";
    CHECK(list.find(expected, halves) != std::string::npos);

    CHECK(rt.importResult("pack:protofix/protocol").rfind("error could not load 'pack:protofix/protocol'", 0) == 0);
}
