// A pack's kinds half (docs/downloadable-packs-m2f-plan.md): its contribution-kind items' runtimes, global code compiled
// and trial-loaded once by the table, beside the built-in script nodes, and refused for a chunk that throws, an id a
// built-in package holds or a kind no package declares; its items in the pack list the TS loader reads.
//
// Run via `pnpm test:plugin packs-test` or `build/bin/retroplug-packs-test "[packs-kinds]"`.
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "PackTestSupport.hpp"
#include "host/js/PackLoaderCore.hpp"
#include "host/packs/KindsTrial.hpp"
#include "host/packs/PackTable.hpp"
#include "host/packs/ProtocolTrial.hpp"
#include "quickjs.h"

using rp::host::packs::PackTable;
using rp::host::packs::test::fixture;

namespace {

// The table with the backend's trials, as the process's has.
void withTrials(PackTable& table) {
    table.setProtocolTrial(rp::host::packs::protocolTrial());
    table.setKindsTrial(rp::host::packs::kindsTrial());
}

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
        JSValue v = JS_Eval(ctx, code.c_str(), code.size(), "<kinds-test>", flags);
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

TEST_CASE("a kinds half is compiled and trial-loaded beside the built-in script nodes, its bytecode kept",
          "[packs-kinds]") {
    PackTable table;
    withTrials(table);
    const auto load = table.load(fixture("kindfix"));
    INFO(load.error);
    REQUIRE(load);
    REQUIRE(load.pack->kinds);
    CHECK_FALSE(load.pack->kinds->bytecode.empty());
}

TEST_CASE("a kinds half is refused when it throws, takes a built-in id or names an undeclared kind", "[packs-kinds]") {
    PackTable table;
    withTrials(table);
    const auto refused = [&](const std::string& id, const std::string& part) {
        const auto load = table.load(fixture(id));
        CHECK_FALSE(load);
        INFO(id << ": " << load.error);
        CHECK(load.error.find(part) != std::string::npos);
        CHECK(table.find(id) == nullptr);
    };
    refused("kindfix-throws", "the kinds fixture throws");
    refused("kindfix-taken", "the controllerApps item lsdj-midimap, which this product's lsdj-launchpad holds");
    refused("kindfix-unknown", "a contribution kind this product does not declare");
}

TEST_CASE("a table with no kinds trial refuses a kinds half", "[packs-kinds]") {
    PackTable table;
    const auto load = table.load(fixture("kindfix"));
    CHECK_FALSE(load);
    CHECK(load.error == "pack kindfix has a kinds half, and this host loads none");
}

TEST_CASE("the pack list carries the kinds member, and pack:<id>/kinds is no module", "[packs-kinds]") {
    PackTable table;
    withTrials(table);
    REQUIRE(table.load(fixture("kindfix")));
    BareRuntime rt;
    rp::host::installBarePackLoader(rt.ctx, table, nullptr, 0);

    const std::string list = rt.packList();
    INFO(list);
    const std::size_t halves = list.find(R"("halves":["kinds"])");
    REQUIRE(halves != std::string::npos);
    CHECK(list.find(R"("kinds":{"controllerApps":[{"id":"kindfix-app"}]})", halves) != std::string::npos);

    CHECK(rt.importResult("pack:kindfix/kinds").rfind("error could not load 'pack:kindfix/kinds'", 0) == 0);
}
