#include "host/js/PackModules.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "tjs.h"

#include "host/packs/PackTable.hpp"

namespace rp::host {

namespace {

constexpr const char* kPackPrefix = "pack:";
// The context's contract names, as a frozen object's keys: written once by installPackLoader from the table the context
// registered, read by the guard. Non-enumerable, non-writable and frozen, so a pack can neither widen nor replace it.
constexpr const char* kContracts = "__rp_packContracts";

// The table every context's loader reads: the process's (processPacks) in every host, a test's own in a test. A
// pointer, not an owner: the table outlives every runtime.
std::atomic<packs::PackTable*> g_table{nullptr};

bool startsWith(const char* s, const char* prefix) { return std::strncmp(s, prefix, std::strlen(prefix)) == 0; }

bool isContract(JSContext* ctx, const char* name) {
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue set = JS_GetPropertyStr(ctx, global, kContracts);
    bool found = false;
    if (JS_IsObject(set)) {
        JSAtom atom = JS_NewAtom(ctx, name);
        found = JS_HasProperty(ctx, set, atom) > 0;
        JS_FreeAtom(ctx, atom);
    }
    JS_FreeValue(ctx, set);
    JS_FreeValue(ctx, global);
    return found;
}

char* normalize(JSContext* ctx, const char* base, const char* name, void* opaque) {
    if (base && startsWith(base, kPackPrefix) && !isContract(ctx, name)) {
        JS_ThrowReferenceError(ctx, "%s imports '%s', which is not a RetroPlug contract module", base, name);
        return nullptr;
    }
    return tjs_module_normalizer(ctx, base, name, opaque);
}

JSModuleDef* notFound(JSContext* ctx, const char* name) {
    JS_ThrowReferenceError(ctx, "could not load '%s': no loaded pack has that half", name);
    return nullptr;
}

JSModuleDef* load(JSContext* ctx, const char* name, void* opaque, JSValueConst attributes) {
    if (!startsWith(name, kPackPrefix)) return tjs_module_loader(ctx, name, opaque, attributes);
    const std::string_view rest(name + std::strlen(kPackPrefix));
    const std::size_t slash = rest.find('/');
    packs::PackTable* table = g_table.load();
    if (slash == std::string_view::npos || !table) return notFound(ctx, name);
    const packs::LoadedPack* pack = table->find(rest.substr(0, slash));
    const std::string half(rest.substr(slash + 1));
    if (!pack || half == "native" || half == "script") return notFound(ctx, name);
    const auto file = pack->manifest.halves.find(half);
    if (file == pack->manifest.halves.end()) return notFound(ctx, name);

    std::ifstream in(pack->dir / std::filesystem::path(file->second), std::ios::binary);
    if (!in) {
        JS_ThrowReferenceError(ctx, "could not load '%s': %s cannot be read", name, file->second.c_str());
        return nullptr;
    }
    const std::string source((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    // Compiled, not evaluated or resolved: QuickJS resolves its imports once this returns, through the guard above
    // with this module's name as their base; a refused one fails the import, and QuickJS frees every module of the
    // context not yet resolved (this one: the shared sets are resolved at registration, SharedModules.cpp).
    JSValue val = JS_Eval(ctx, source.c_str(), source.size(), name, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    if (JS_IsException(val)) return nullptr;
    // The name is no path, so import.meta takes it as written.
    if (js_module_set_import_meta(ctx, val, false, false) != 0) {
        JS_FreeValue(ctx, val);
        return nullptr;
    }
    auto* m = static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(val));
    JS_FreeValue(ctx, val);  // the module is referenced by the context's module list
    return m;
}

std::string jsonString(const std::string& s) {
    std::string out = "\"";
    for (const char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}

std::string jsonList(const std::vector<std::string>& items) {
    std::string out = "[";
    for (std::size_t i = 0; i < items.size(); ++i) out += (i ? "," : "") + jsonString(items[i]);
    return out + "]";
}

// The packs a runtime refused for running past the load budget, for the whole process (see packList).
std::mutex            g_skipMutex;
std::set<std::string> g_skipLater;

JSValue packList(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    std::string out = "[";
    bool first = true;
    if (packs::PackTable* table = g_table.load()) {
        // A pack that ran past the load budget in some runtime is left out: refusals are per runtime in the TS loader,
        // so without this every plugin instance, DPF's info instance and the editor would pay a stuck pack's budget
        // again. Only that refusal is kept (the table's own refusals also name packs that did load, and a
        // deterministic refusal costs nothing to repeat).
        // A pack requiring a skipped pack is left out with it (in dependency order, so transitively): the new runtime's
        // TS loader has no refusal to propagate, and would commit it without its dependency.
        std::set<std::string> skipped = [] {
            std::lock_guard<std::mutex> lock(g_skipMutex);
            return g_skipLater;
        }();
        for (const packs::LoadedPack* pack : table->loadedFrom(0)) {
            const auto& req = pack->manifest.requiresPacks;
            if (skipped.count(pack->manifest.id) ||
                std::any_of(req.begin(), req.end(), [&](const std::string& r) { return skipped.count(r) > 0; })) {
                skipped.insert(pack->manifest.id);
                continue;
            }
            std::vector<std::string> halves;
            for (const auto& [kind, file] : pack->manifest.halves)
                if (kind != "native") halves.push_back(kind);
            if (halves.empty()) continue;
            const packs::PackManifest& m = pack->manifest;
            out += std::string(first ? "" : ",") + "{\"id\":" + jsonString(m.id) + ",\"version\":" +
                   jsonString(m.version) + ",\"halves\":" + jsonList(halves) + ",\"provides\":{\"platforms\":" +
                   jsonList(m.providesPlatforms) + ",\"nodeTypes\":" + jsonList(m.providesNodeTypes) +
                   ",\"roles\":" + jsonList(m.providesRoles) + "},\"requires\":{\"packs\":" +
                   jsonList(m.requiresPacks) + ",\"platforms\":" + jsonList(m.requiresPlatforms) + "}}";
            first = false;
        }
    }
    out += "]";
    return JS_NewStringLen(ctx, out.data(), out.size());
}

JSValue packRefused(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    packs::PackTable* table = g_table.load();
    if (!table || argc < 2) return JS_UNDEFINED;
    const char* id = JS_ToCString(ctx, argv[0]);
    const char* reason = JS_ToCString(ctx, argv[1]);
    if (id && reason) {
        if (argc > 2 && JS_ToBool(ctx, argv[2]) > 0) {
            std::lock_guard<std::mutex> lock(g_skipMutex);
            g_skipLater.insert(id);
        }
        const packs::LoadedPack* pack = table->find(id);
        table->recordRefusal({id, pack ? pack->dir : std::filesystem::path(),
                              std::string("pack ") + id + " is refused: " + reason});
    }
    JS_FreeCString(ctx, id);
    JS_FreeCString(ctx, reason);
    return (id && reason) ? JS_UNDEFINED : JS_EXCEPTION;
}

}  // namespace

void installPackLoader(JSContext* ctx, packs::PackTable& table, const rp_js_module* contracts, std::size_t count) {
    g_table.store(&table);
    // txiki's opaque is its TJSRuntime; the wrappers hand it on unchanged.
    JS_SetModuleLoaderFunc2(JS_GetRuntime(ctx), normalize, load, tjs_module_attr_checker, TJS_GetRuntime(ctx));

    JSValue global = JS_GetGlobalObject(ctx);
    JSValue mark = JS_GetPropertyStr(ctx, global, kContracts);
    const bool installed = JS_IsObject(mark);
    JS_FreeValue(ctx, mark);
    if (!installed) {
        JSValue set = JS_NewObjectProto(ctx, JS_NULL);
        for (std::size_t i = 0; i < count; ++i)
            if (!startsWith(contracts[i].name, "rp:"))
                JS_DefinePropertyValueStr(ctx, set, contracts[i].name, JS_TRUE, 0);
        JS_FreezeObject(ctx, set);
        JS_DefinePropertyValueStr(ctx, global, kContracts, set, 0);
        JS_DefinePropertyValueStr(ctx, global, "__rp_packList", JS_NewCFunction(ctx, packList, "__rp_packList", 0), 0);
        JS_DefinePropertyValueStr(ctx, global, "__rp_packRefused",
                                  JS_NewCFunction(ctx, packRefused, "__rp_packRefused", 2), 0);
    }
    JS_FreeValue(ctx, global);
}

void settleForFree(TjsHostRuntime& host) {
    TJSRuntime* qrt = host.runtime();
    if (!qrt || !host.context()) return;
    // QuickJS-ng frees a context's module structs while a callback of a pending async module (a pack half parked on a
    // timer) still holds a counted reference to one: the callback is garbage only once its timer is destroyed, and no
    // collection runs before the context is freed (the same defect as bellard/quickjs#280). Destroying the timers
    // here, then collecting, frees the callback while the modules are alive. txiki destroys timers again when it
    // frees the runtime; that walk finds none left.
    tjs__destroy_timers(qrt);
    JS_RunGC(JS_GetRuntime(host.context()));
}

}  // namespace rp::host
