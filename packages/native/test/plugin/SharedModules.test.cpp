// The shared module sets' host loader (docs/downloadable-packs-m2b-plan.md, Task 4): a table registered in a context
// gives every host evaluated there one instance of each module, registering twice is a no-op, a table read out of order
// is refused naming the module, and a host whose import no table provides does not evaluate. The fixture is four tiny
// modules compiled by retroplug-modc at build time (test/plugin/shared-modules-fixture/).
#include <cstdint>
#include <string>

#include "dpfjs/host/TjsHostRuntime.hpp"
#include "host/js/SharedModules.hpp"
#include "quickjs.h"

// txiki's utils.h defines a CHECK macro, which Catch2's would redefine (ClassIdReserve.test.cpp does the same).
#ifdef CHECK
#undef CHECK
#endif
#include <catch2/catch_test_macros.hpp>

extern "C" {
extern const rp_js_module rp_fx_modules[];
extern const std::uint32_t rp_fx_modules_count;
extern const rp_js_module rp_fx_misordered[];
extern const std::uint32_t rp_fx_misordered_count;
extern const std::uint8_t rp_fx_host_a[];
extern const std::uint32_t rp_fx_host_a_size;
extern const std::uint8_t rp_fx_host_b[];
extern const std::uint32_t rp_fx_host_b_size;
}

namespace {

/** `expr` evaluated as global code in `ctx`, as JSON. */
std::string json(JSContext* ctx, const char* expr) {
    const std::string code = std::string("JSON.stringify(") + expr + ")";
    JSValue v = JS_Eval(ctx, code.c_str(), code.size(), "<probe>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(v)) {
        JS_FreeValue(ctx, JS_GetException(ctx));
        return "<threw>";
    }
    const char* s = JS_ToCString(ctx, v);
    std::string out = s ? s : "<null>";
    JS_FreeCString(ctx, s);
    JS_FreeValue(ctx, v);
    return out;
}

}  // namespace

TEST_CASE("two hosts over one table share one instance of each module", "[shared-modules]") {
    TjsHostRuntime host;
    REQUIRE(host.init());
    JSContext* ctx = host.context();
    REQUIRE(rp::host::registerSharedModules(ctx, rp_fx_modules, rp_fx_modules_count).empty());
    CHECK(json(ctx, "typeof globalThis.__fxStateRuns") == "\"undefined\"");
    REQUIRE(host.evalModuleBytecode(rp_fx_host_a, rp_fx_host_a_size) == 0);
    REQUIRE(host.evalModuleBytecode(rp_fx_host_b, rp_fx_host_b_size) == 0);
    CHECK(json(ctx, "globalThis.__fxA === globalThis.__fxB") == "true");
    CHECK(json(ctx, "globalThis.__fxA.count") == "2");
    CHECK(json(ctx, "globalThis.__fxStateRuns") == "1");
}

TEST_CASE("registering a table twice in one context does nothing the second time", "[shared-modules]") {
    TjsHostRuntime host;
    REQUIRE(host.init());
    JSContext* ctx = host.context();
    REQUIRE(rp::host::registerSharedModules(ctx, rp_fx_modules, rp_fx_modules_count).empty());
    // The second table would fail if it were read, so an empty result proves the mark short-circuited the call.
    static const std::uint8_t junk[3] = {1, 2, 3};
    const rp_js_module bad[1] = {{"fx:bad", junk, 3}};
    REQUIRE(rp::host::registerSharedModules(ctx, bad, 1).empty());
    CHECK(json(ctx, "Object.getOwnPropertyDescriptor(globalThis,'__rp_sharedModulesRegistered').enumerable") == "false");
    REQUIRE(host.evalModuleBytecode(rp_fx_host_a, rp_fx_host_a_size) == 0);
    CHECK(json(ctx, "globalThis.__fxStateRuns") == "1");
}

TEST_CASE("a table read out of order is refused naming the module", "[shared-modules]") {
    TjsHostRuntime host;
    REQUIRE(host.init());
    const std::string why = rp::host::registerSharedModules(host.context(), rp_fx_misordered, rp_fx_misordered_count);
    CHECK(why.find("fx:dep") != std::string::npos);
}

TEST_CASE("a host whose import no table provides does not evaluate", "[shared-modules]") {
    TjsHostRuntime host;
    REQUIRE(host.init());
    CHECK(host.evalModuleBytecode(rp_fx_host_a, rp_fx_host_a_size) != 0);
}
