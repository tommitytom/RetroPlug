// The pack module loader (host/js/PackModules, docs/downloadable-packs-m2c-plan.md): a pack's half is a module named
// pack:<id>/<half>, compiled from its file the first time something imports it, and a pack module may import only a
// contract module of its context's registered set.
#include <cstdint>
#include <filesystem>
#include <string>

#include "dpfjs/host/TjsHostRuntime.hpp"
#include "host/js/PackModules.hpp"
#include "host/js/SharedModules.hpp"
#include "host/packs/PackTable.hpp"
#include "quickjs.h"

// txiki's utils.h defines a CHECK macro, which Catch2's would redefine (ClassIdReserve.test.cpp does the same).
#ifdef CHECK
#undef CHECK
#endif
#include <catch2/catch_test_macros.hpp>

extern "C" {
extern const rp_js_module rp_fx_modules[];
extern const std::uint32_t rp_fx_modules_count;
extern const std::uint8_t rp_fx_host_a[];
extern const std::uint32_t rp_fx_host_a_size;
}

namespace fs = std::filesystem;
using rp::host::packs::PackTable;

namespace {

fs::path fixture(const char* id) { return fs::path(RP_PACK_MODULES_FIXTURE) / id / "1.0.0"; }

struct Runtime {
    rp::host::PackHostRuntime host;
    Runtime() { REQUIRE(host.init()); }
    JSContext* ctx() { return host.context(); }
};

std::string quoted(const std::string& s) { return "\"" + s + "\""; }

void evalModule(Runtime& rt, const std::string& code) {
    REQUIRE(rt.host.evalModuleBuffer(code.c_str(), code.size(), "<pm-test>") == 0);
}

/** `expr` evaluated as global code, converted to a string. */
std::string readString(Runtime& rt, const std::string& expr) {
    const std::string code = "String(" + expr + ")";
    JSValue v = JS_Eval(rt.ctx(), code.c_str(), code.size(), "<probe>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(v)) {
        JS_FreeValue(rt.ctx(), JS_GetException(rt.ctx()));
        return "<threw>";
    }
    const char* s = JS_ToCString(rt.ctx(), v);
    std::string out = s ? s : "<null>";
    JS_FreeCString(rt.ctx(), s);
    JS_FreeValue(rt.ctx(), v);
    return out;
}

void pumpUntil(Runtime& rt, const std::string& expr) {
    for (int i = 0; i < 1000 && readString(rt, expr) != "true"; ++i) rt.host.pump();
}

void evalHostA(Runtime& rt) { REQUIRE(rt.host.evalModuleBytecode(rp_fx_host_a, rp_fx_host_a_size) == 0); }

// Import `name` from a module evaluated in the context, pump until it settles, and return "ok <JSON of the default
// export>" or "error <message>".
std::string importResult(Runtime& rt, const std::string& name) {
    const std::string code = "import(" + quoted(name) + ").then((m) => { globalThis.__pm = 'ok ' + "
                             "JSON.stringify(m.default); }, (e) => { globalThis.__pm = 'error ' + e.message; });";
    evalModule(rt, code);
    pumpUntil(rt, "globalThis.__pm !== undefined");
    const std::string out = readString(rt, "globalThis.__pm");
    evalModule(rt, "globalThis.__pm = undefined;");
    return out;
}

}  // namespace

TEST_CASE("a pack's half loads by name and links to the context's contract modules", "[pack-modules]") {
    PackTable table;
    for (const char* id : {"pmgood", "pmforeign", "pminternal", "pmrelative", "pmthrows"})
        REQUIRE(table.load(fixture(id)));
    Runtime rt;
    REQUIRE(rp::host::registerSharedModules(rt.ctx(), rp_fx_modules, rp_fx_modules_count).empty());
    // The contracts the loader is given: the fixture table's two modules and an rp:-named one, as a real set has (its
    // hosts and chunks), so the guard's rp: filter is what refuses pminternal, not the name's absence from the table.
    const rp_js_module contracts[] = {rp_fx_modules[0], rp_fx_modules[1], {"rp:fx-a", nullptr, 0}};
    rp::host::installPackLoader(rt.ctx(), table, contracts, 3);

    SECTION("the halves, each once") {
        const std::string app = importResult(rt, "pack:pmgood/app");
        CHECK(app.rfind("ok {\"half\":\"app\"", 0) == 0);
        CHECK(importResult(rt, "pack:pmgood/ui") == "ok {\"half\":\"ui\"}");
        CHECK(importResult(rt, "pack:pmgood/app") == app);  // the same module, not compiled again
    }
    SECTION("a half the pack does not have, or a pack the table does not hold") {
        CHECK(importResult(rt, "pack:pmgood/cli").rfind("error could not load 'pack:pmgood/cli'", 0) == 0);
        CHECK(importResult(rt, "pack:nope/app").rfind("error could not load 'pack:nope/app'", 0) == 0);
        // pmgood declares no native half, so this fails at the half lookup; the loader's own native refusal is untested.
        CHECK(importResult(rt, "pack:pmgood/native").rfind("error could not load", 0) == 0);
    }
    SECTION("the guard") {
        CHECK(importResult(rt, "pack:pmforeign/app") ==
              "error pack:pmforeign/app imports 'fx:nope', which is not a RetroPlug contract module");
        evalHostA(rt);  // rp:fx-a is now a loaded module of the context, and still not a pack's to import
        CHECK(importResult(rt, "pack:pminternal/app") ==
              "error pack:pminternal/app imports 'rp:fx-a', which is not a RetroPlug contract module");
        CHECK(importResult(rt, "pack:pmrelative/app") ==
              "error pack:pmrelative/app imports './other.js', which is not a RetroPlug contract module");
    }
    SECTION("a half that throws") {
        CHECK(importResult(rt, "pack:pmthrows/app") == "error the fixture half throws");
    }
    SECTION("the host's own imports are not judged") {
        // A module that is not a pack's still reaches anything txiki's loader serves.
        CHECK(importResult(rt, "fx:dep").rfind("ok", 0) == 0);
    }
}

TEST_CASE("the pack list and the refusal report", "[pack-modules]") {
    PackTable table;
    REQUIRE(table.load(fixture("pmgood")));
    Runtime rt;
    REQUIRE(rp::host::registerSharedModules(rt.ctx(), rp_fx_modules, rp_fx_modules_count).empty());
    rp::host::installPackLoader(rt.ctx(), table, rp_fx_modules, rp_fx_modules_count);
    CHECK(readString(rt, "globalThis.__rp_packList()") ==
          R"json([{"id":"pmgood","name":"Pack modules fixture (good)","version":"1.0.0","halves":["app","ui"],)json"
          R"json("provides":{"platforms":[],"nodeTypes":[],"roles":[]},"requires":{"packs":[],"platforms":[]}}])json");
    evalModule(rt, "globalThis.__rp_packRefused('pmgood', 'its app half failed');");
    REQUIRE(table.refusals().size() == 1);
    CHECK(table.refusals()[0].id == "pmgood");
    CHECK(table.refusals()[0].reason == "pack pmgood is refused: its app half failed");
    // The contract set is the context's, frozen, and not enumerable.
    CHECK(readString(rt, "Object.isFrozen(globalThis.__rp_packContracts)") == "true");
    CHECK(readString(rt, "Object.keys(globalThis).includes('__rp_packContracts')") == "false");
}

TEST_CASE("the refusal list carries each refusal's reason, its folder's version and its API-level flag",
          "[pack-modules]") {
    PackTable table;
    table.recordRefusal({"pmold", fs::path("/packs/pmold/1.2.0"), "pack pmold is built for API level 1", true});
    table.recordRefusal({"pmplain", fs::path("/dev/pmplain"), "pack pmplain has no library", false});
    Runtime rt;
    REQUIRE(rp::host::registerSharedModules(rt.ctx(), rp_fx_modules, rp_fx_modules_count).empty());
    rp::host::installPackLoader(rt.ctx(), table, rp_fx_modules, rp_fx_modules_count);
    CHECK(readString(rt, "globalThis.__rp_packRefusals()") ==
          R"([{"id":"pmold","reason":"pack pmold is built for API level 1","version":"1.2.0","needsUpdate":true},)"
          R"({"id":"pmplain","reason":"pack pmplain has no library","needsUpdate":false}])");
}

TEST_CASE("the refusal list escapes a control character in a reason", "[pack-modules]") {
    PackTable table;
    table.recordRefusal({"pmctl", fs::path("/dev/pmctl"), std::string("a\nb\t\"c\"\\d"), false});
    Runtime rt;
    REQUIRE(rp::host::registerSharedModules(rt.ctx(), rp_fx_modules, rp_fx_modules_count).empty());
    rp::host::installPackLoader(rt.ctx(), table, rp_fx_modules, rp_fx_modules_count);
    CHECK(readString(rt, "globalThis.__rp_packRefusals()") ==
          R"json([{"id":"pmctl","reason":"a\u000ab\u0009\"c\"\\d","needsUpdate":false}])json");
    CHECK(readString(rt, "JSON.parse(globalThis.__rp_packRefusals())[0].reason") == "a\nb\t\"c\"\\d");
}

TEST_CASE("a live load answers the pack it loaded, or why it refused", "[pack-modules]") {
    PackTable table;
    Runtime rt;
    REQUIRE(rp::host::registerSharedModules(rt.ctx(), rp_fx_modules, rp_fx_modules_count).empty());
    rp::host::installPackLoader(rt.ctx(), table, rp_fx_modules, rp_fx_modules_count);
    const std::string good = fixture("pmgood").generic_string();
    CHECK(readString(rt, "globalThis.__rp_packLoadNow(" + quoted(good) + ")") == R"({"loaded":"pmgood"})");
    CHECK(table.find("pmgood") != nullptr);
    // The same folder again answers the same.
    CHECK(readString(rt, "globalThis.__rp_packLoadNow(" + quoted(good) + ")") == R"({"loaded":"pmgood"})");
    const std::string refused = readString(rt, "globalThis.__rp_packLoadNow('/no/such/pack/1.0.0')");
    CHECK(refused.rfind(R"({"refused":")", 0) == 0);
    CHECK(readString(rt, "globalThis.__rp_packRefusals()").find("\"needsUpdate\":false") != std::string::npos);
}

TEST_CASE("a runtime freed with a pack half still pending frees cleanly", "[pack-modules]") {
    PackTable table;
    REQUIRE(table.load(fixture("pmpending")));
    // Several runtimes: the unsettled free reads a freed module, which a single run can survive by luck.
    for (int n = 0; n < 8; ++n) {
        Runtime rt;
        REQUIRE(rp::host::registerSharedModules(rt.ctx(), rp_fx_modules, rp_fx_modules_count).empty());
        rp::host::installPackLoader(rt.ctx(), table, rp_fx_modules, rp_fx_modules_count);
        // The half awaits a timer that never fires in this test: its async module stays pending until the runtime goes.
        evalModule(rt, "import('pack:pmpending/app').catch(() => {});");
        for (int i = 0; i < 5; ++i) rt.host.pump();
        CHECK(readString(rt, "typeof globalThis.__rp_packList") == "function");
    }  // each free here used to touch a freed module (an abort in a normal build, an ASan report under it)
    SUCCEED();
}

TEST_CASE("only a pack that ran past the load budget is left out of the pack list", "[pack-modules]") {
    PackTable table;
    REQUIRE(table.load(fixture("pmgood")));
    REQUIRE(table.load(fixture("pmforeign")));
    REQUIRE(table.load(fixture("pmdependent")));
    Runtime rt;
    REQUIRE(rp::host::registerSharedModules(rt.ctx(), rp_fx_modules, rp_fx_modules_count).empty());
    rp::host::installPackLoader(rt.ctx(), table, rp_fx_modules, rp_fx_modules_count);
    const auto lists = [&](const char* id) { return readString(rt, "globalThis.__rp_packList()").find(std::string("\"id\":\"") + id + "\"") != std::string::npos; };
    CHECK(lists("pmgood"));
    CHECK(lists("pmdependent"));
    // The table's own refusals also name a pack that did load (a second folder of the same id): it still lists.
    table.recordRefusal({"pmgood", fixture("pmforeign"), "pack pmgood is refused: given twice"});
    CHECK(lists("pmgood"));
    // A refusal the TS loader reports without the flag is deterministic: free to repeat in the next runtime.
    evalModule(rt, "globalThis.__rp_packRefused('pmgood', 'its app half failed');");
    CHECK(lists("pmgood"));
    // With the flag (a pack that ran past the budget) it is left out, and only that pack.
    evalModule(rt, "globalThis.__rp_packRefused('pmforeign', 'ran past the budget', true);");
    CHECK_FALSE(lists("pmforeign"));
    CHECK(lists("pmgood"));
    // A pack that requires it is left out with it: the next runtime's TS loader has no refusal to propagate, so listing
    // the dependent would commit it without its dependency.
    CHECK_FALSE(lists("pmdependent"));
}

namespace {

// A bare QuickJS runtime, as a render job's (host/render/RenderHost): no txiki, no timers, a job queue run here.
struct BareRuntime {
    JSRuntime* rt = JS_NewRuntime();
    JSContext* ctx = JS_NewContext(rt);
    ~BareRuntime() {
        JS_RunGC(rt);  // as ~RenderHost: a pending half's callbacks go before the modules they hold (bellard/quickjs#280)
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
    }
    void drain() {
        JSContext* jc = nullptr;
        while (JS_ExecutePendingJob(rt, &jc) > 0) {}
    }
    std::string importResult(const std::string& name) {
        const std::string code = "import(" + quoted(name) + ").then((m) => { globalThis.__pm = 'ok ' + "
                                 "JSON.stringify(m.default); }, (e) => { globalThis.__pm = 'error ' + e.message; });";
        JSValue v = JS_Eval(ctx, code.c_str(), code.size(), "<bare-test>", JS_EVAL_TYPE_MODULE);
        REQUIRE_FALSE(JS_IsException(v));
        JS_FreeValue(ctx, v);
        drain();
        JSValue g = JS_GetGlobalObject(ctx);
        JSValue s = JS_GetPropertyStr(ctx, g, "__pm");
        const char* c = JS_ToCString(ctx, s);
        std::string out = c ? c : "<none>";
        JS_FreeCString(ctx, c);
        JS_FreeValue(ctx, s);
        JS_SetPropertyStr(ctx, g, "__pm", JS_UNDEFINED);
        JS_FreeValue(ctx, g);
        return out;
    }
};

}  // namespace

TEST_CASE("on a bare runtime a pack's half loads by name, held to the same guard", "[pack-modules]") {
    PackTable table;
    for (const char* id : {"pmgood", "pmforeign", "pmrelative", "pmthrows", "pmpending"})
        REQUIRE(table.load(fixture(id)));
    BareRuntime rt;
    REQUIRE(rp::host::registerSharedModules(rt.ctx, rp_fx_modules, rp_fx_modules_count).empty());
    const rp_js_module contracts[] = {rp_fx_modules[0], rp_fx_modules[1], {"rp:fx-a", nullptr, 0}};
    rp::host::installBarePackLoader(rt.ctx, table, contracts, 3);

    CHECK(rt.importResult("pack:pmgood/app").rfind("ok {\"half\":\"app\"", 0) == 0);
    CHECK(rt.importResult("pack:pmgood/cli").rfind("error could not load 'pack:pmgood/cli'", 0) == 0);
    CHECK(rt.importResult("pack:pmforeign/app") ==
          "error pack:pmforeign/app imports 'fx:nope', which is not a RetroPlug contract module");
    CHECK(rt.importResult("pack:pmrelative/app") ==
          "error pack:pmrelative/app imports './other.js', which is not a RetroPlug contract module");
    CHECK(rt.importResult("pack:pmthrows/app") == "error the fixture half throws");
    // A name that is neither a pack's nor registered is not found: there is no file loader on a bare runtime.
    CHECK(rt.importResult("fx:absent").rfind("error could not load 'fx:absent'", 0) == 0);
    // A registered module is found by its name.
    CHECK(rt.importResult("fx:dep").rfind("ok", 0) == 0);
    // The pack list is installed, the refusal hook is not: a render's refusals are its console's.
    JSValue g = JS_GetGlobalObject(rt.ctx);
    JSValue list = JS_GetPropertyStr(rt.ctx, g, "__rp_packList");
    JSValue refused = JS_GetPropertyStr(rt.ctx, g, "__rp_packRefused");
    CHECK(JS_IsFunction(rt.ctx, list));
    CHECK(JS_IsUndefined(refused));
    JS_FreeValue(rt.ctx, list);
    JS_FreeValue(rt.ctx, refused);
    JS_FreeValue(rt.ctx, g);
    // A half left pending when the runtime goes: freed without a fault (the destructor collects first).
    rt.importResult("pack:pmpending/app");
}
