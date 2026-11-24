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
          R"([{"id":"pmgood","version":"1.0.0","halves":["app","ui"],)"
          R"("provides":{"platforms":[],"nodeTypes":[],"roles":[]},"requires":{"packs":[],"platforms":[]}}])");
    evalModule(rt, "globalThis.__rp_packRefused('pmgood', 'its app half failed');");
    REQUIRE(table.refusals().size() == 1);
    CHECK(table.refusals()[0].id == "pmgood");
    CHECK(table.refusals()[0].reason == "pack pmgood is refused: its app half failed");
    // The contract set is the context's, frozen, and not enumerable.
    CHECK(readString(rt, "Object.isFrozen(globalThis.__rp_packContracts)") == "true");
    CHECK(readString(rt, "Object.keys(globalThis).includes('__rp_packContracts')") == "false");
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
