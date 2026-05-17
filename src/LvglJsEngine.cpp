#include "LvglJsEngine.hpp"

extern "C" {
    #include <uv.h>
    #include <quickjs.h>
}

#include "native/bootstrap/render_bootstrap.hpp"
#include "native/components/component.hpp"
#include "native/core/group/group.hpp"

// Called by the native component event system (declared in engine.hpp).
// Retrieves the TJSRuntime* for the current plugin instance via the
// LVGL display's user_data — each plugin instance has its own display.
TJSRuntime* GetRuntime() {
    DpfJsDisplayData* data = DpfJsDisplayData::get();
    return data ? data->runtime : nullptr;
}

LvglJsEngine::LvglJsEngine() : lvgljsObj(JS_UNDEFINED) {}

LvglJsEngine::~LvglJsEngine() {
    shutdown();
}

JSValue LvglJsEngine::js_lvgljs_on(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 2)
        return JS_UNDEFINED;
    DpfJsDisplayData* data = DpfJsDisplayData::get();
    if (!data || !data->engine)
        return JS_UNDEFINED;
    const char* channel = JS_ToCString(ctx, argv[0]);
    if (!channel)
        return JS_EXCEPTION;
    if (!JS_IsFunction(ctx, argv[1])) {
        JS_FreeCString(ctx, channel);
        return JS_ThrowTypeError(ctx, "lvgljs.on: handler must be a function");
    }
    JSValue dup = JS_DupValue(ctx, argv[1]);
    data->engine->handlers[channel].push_back(dup);
    JS_FreeCString(ctx, channel);
    return JS_UNDEFINED;
}

JSValue LvglJsEngine::js_lvgljs_setParameter(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 2)
        return JS_UNDEFINED;
    DpfJsDisplayData* data = DpfJsDisplayData::get();
    if (!data || !data->engine)
        return JS_UNDEFINED;
    int32_t index;
    double value;
    if (JS_ToInt32(ctx, &index, argv[0]))
        return JS_EXCEPTION;
    if (JS_ToFloat64(ctx, &value, argv[1]))
        return JS_EXCEPTION;
    LvglJsEngine* engine = data->engine;
    if (!engine->paramIndexToName.empty()
        && engine->paramIndexToName.find(static_cast<uint32_t>(index)) == engine->paramIndexToName.end()) {
        return JS_ThrowRangeError(ctx, "lvgljs.setParameter: parameter index %d is not registered", index);
    }
    if (engine->paramWrite)
        engine->paramWrite(static_cast<uint32_t>(index), static_cast<float>(value));
    return JS_UNDEFINED;
}

JSValue LvglJsEngine::js_lvgljs_getParameterIndex(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1)
        return JS_NewInt32(ctx, -1);
    DpfJsDisplayData* data = DpfJsDisplayData::get();
    if (!data || !data->engine)
        return JS_NewInt32(ctx, -1);
    const char* name = JS_ToCString(ctx, argv[0]);
    if (!name)
        return JS_EXCEPTION;
    auto& m = data->engine->paramNameToIndex;
    auto it = m.find(name);
    int32_t result = (it == m.end()) ? -1 : static_cast<int32_t>(it->second);
    JS_FreeCString(ctx, name);
    return JS_NewInt32(ctx, result);
}

JSValue LvglJsEngine::js_lvgljs_getParameterCount(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    DpfJsDisplayData* data = DpfJsDisplayData::get();
    if (!data || !data->engine)
        return JS_NewUint32(ctx, 0);
    return JS_NewUint32(ctx, static_cast<uint32_t>(data->engine->paramNameToIndex.size()));
}

JSValue LvglJsEngine::js_lvgljs_off(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1)
        return JS_UNDEFINED;
    DpfJsDisplayData* data = DpfJsDisplayData::get();
    if (!data || !data->engine)
        return JS_UNDEFINED;
    const char* channel = JS_ToCString(ctx, argv[0]);
    if (!channel)
        return JS_EXCEPTION;
    auto it = data->engine->handlers.find(channel);
    JS_FreeCString(ctx, channel);
    if (it == data->engine->handlers.end())
        return JS_UNDEFINED;

    if (argc < 2 || JS_IsUndefined(argv[1])) {
        for (JSValue v : it->second)
            JS_FreeValue(ctx, v);
        data->engine->handlers.erase(it);
        return JS_UNDEFINED;
    }

    void* target = JS_VALUE_GET_PTR(argv[1]);
    auto& vec = it->second;
    for (auto vit = vec.begin(); vit != vec.end(); ++vit) {
        if (JS_VALUE_GET_PTR(*vit) == target) {
            JS_FreeValue(ctx, *vit);
            vec.erase(vit);
            break;
        }
    }
    if (vec.empty())
        data->engine->handlers.erase(it);
    return JS_UNDEFINED;
}

bool LvglJsEngine::init() {
    if (initialized)
        return true;

    // Initialize txiki.js globals (idempotent if called multiple times)
    static bool tjs_initialized = false;
    if (!tjs_initialized) {
        static char arg0[] = "lvgl-plugin";
        static char* argv[] = { arg0, nullptr };
        TJS_Initialize(1, argv);
        tjs_initialized = true;
    }

    // Create a new JS runtime with default options
    qrt = TJS_NewRuntime();
    if (!qrt)
        return false;

    ctx = TJS_GetJSContext(qrt);

    // Store per-instance data on the LVGL display. This extends
    // LvBindingJsDisplayData (used by lv_binding_js internally for
    // GetWindowInstance()) with the TJS runtime pointer.
    displayData.runtime = qrt;
    displayData.engine = this;
    lv_display_set_user_data(lv_display_get_default(), &displayData);

    // Set up the lvgljs global symbol and register native LVGL components
    JSValue global_obj = JS_GetGlobalObject(ctx);
    JSValue render_sym = JS_NewSymbol(ctx, "lvgljs", true);
    JSAtom render_atom = JS_ValueToAtom(ctx, render_sym);
    JSValue render = JS_NewObjectProto(ctx, JS_NULL);

    JS_DefinePropertyValue(ctx, global_obj, render_atom, render, JS_PROP_C_W_E);

    NativeRenderInit(ctx, render);

    // Generic event mechanism — plugins (or anything else) can hook native
    // events without modifying the engine.
    JS_SetPropertyStr(ctx, render, "on",
                      JS_NewCFunction(ctx, js_lvgljs_on, "on", 2));
    JS_SetPropertyStr(ctx, render, "off",
                      JS_NewCFunction(ctx, js_lvgljs_off, "off", 2));

    // Parameter machinery (generic across plugins): JS -> DSP write and
    // name<->index lookup so JS code can address parameters by name.
    JS_SetPropertyStr(ctx, render, "setParameter",
                      JS_NewCFunction(ctx, js_lvgljs_setParameter, "setParameter", 2));
    JS_SetPropertyStr(ctx, render, "getParameterIndex",
                      JS_NewCFunction(ctx, js_lvgljs_getParameterIndex, "getParameterIndex", 1));
    JS_SetPropertyStr(ctx, render, "getParameterCount",
                      JS_NewCFunction(ctx, js_lvgljs_getParameterCount, "getParameterCount", 0));

    // Redirect the keypad input device(s) to a JS-side lv_group, or restore
    // the default group when called with null/undefined. Used to confine
    // keyboard navigation while modal overlays are visible.
    JS_SetPropertyStr(ctx, render, "setKeyboardGroup",
                      JS_NewCFunction(ctx, NativeSetKeyboardGroup, "setKeyboardGroup", 1));

    // Hold a reference to the lvgljs namespace so external code can attach
    // further native methods after init.
    lvgljsObj = JS_DupValue(ctx, render);

    JS_FreeAtom(ctx, render_atom);
    JS_FreeValue(ctx, render_sym);
    JS_FreeValue(ctx, global_obj);

    // Initialize the LVGL window object (root container for JS components)
    WindowInit();

    // Start the prepare/check handles so libuv processes JS jobs
    uv_prepare_start(&qrt->jobs.prepare, [](uv_prepare_t* handle) {
        TJSRuntime* qrt = static_cast<TJSRuntime*>(handle->data);
        if (JS_IsJobPending(qrt->rt)) {
            uv_idle_start(&qrt->jobs.idle, [](uv_idle_t*) {});
        }
    });
    uv_unref(reinterpret_cast<uv_handle_t*>(&qrt->jobs.prepare));

    uv_check_start(&qrt->jobs.check, [](uv_check_t* handle) {
        TJSRuntime* qrt = static_cast<TJSRuntime*>(handle->data);
        tjs__execute_jobs(qrt->ctx);
        if (!JS_IsJobPending(qrt->rt)) {
            uv_idle_stop(&qrt->jobs.idle);
        }
    });
    uv_unref(reinterpret_cast<uv_handle_t*>(&qrt->jobs.check));

    initialized = true;
    return true;
}

void LvglJsEngine::tick() {
    if (!initialized)
        return;

    uv_run(TJS_GetLoop(qrt), UV_RUN_NOWAIT);
    tjs__execute_jobs(ctx);
}

void LvglJsEngine::shutdown() {
    if (!initialized)
        return;

    initialized = false;

    // Free all registered handlers before tearing down the runtime.
    for (auto& kv : handlers) {
        for (JSValue v : kv.second)
            JS_FreeValue(ctx, v);
    }
    handlers.clear();

    paramWrite = nullptr;
    paramNameToIndex.clear();
    paramIndexToName.clear();

    JS_FreeValue(ctx, lvgljsObj);
    lvgljsObj = JS_UNDEFINED;

    lv_display_t* disp = lv_display_get_default();
    if (disp && lv_display_get_user_data(disp) == &displayData)
        lv_display_set_user_data(disp, nullptr);

    displayData.runtime = nullptr;
    displayData.engine = nullptr;
    displayData.bridge = nullptr;
    displayData.windowInstance = nullptr;

    TJS_FreeRuntime(qrt);
    qrt = nullptr;
    ctx = nullptr;
}

int LvglJsEngine::evalModule(const char* filename) {
    if (!initialized)
        return -1;

    JSValue result = TJS_EvalModule(ctx, filename, true);
    if (JS_IsException(result)) {
        tjs_dump_error(ctx);
        JS_FreeValue(ctx, result);
        return -1;
    }
    JS_FreeValue(ctx, result);
    return 0;
}

int LvglJsEngine::evalModuleBuffer(const char* code, size_t len, const char* name) {
    if (!initialized)
        return -1;

    // Same path as TJS_EvalModule, but fed from a buffer rather than a file.
    // Sets up import.meta.url and fires the synthetic 'load' event.
    JSValue result = TJS_EvalModuleContent(ctx, name, true, false, code, len);
    if (JS_IsException(result)) {
        tjs_dump_error(ctx);
        JS_FreeValue(ctx, result);
        return -1;
    }
    JS_FreeValue(ctx, result);
    return 0;
}

int LvglJsEngine::evalModuleBytecode(const uint8_t* bytecode, size_t len) {
    if (!initialized)
        return -1;

    // tjs__eval_bytecode does JS_ReadObject(JS_READ_OBJ_BYTECODE) →
    // JS_ResolveModule → js_module_set_import_meta(false, false) →
    // JS_EvalFunction. It does NOT fire the 'load' event that
    // TJS_EvalModuleContent fires when is_main=true, so do it here for
    // parity with evalModuleBuffer.
    if (tjs__eval_bytecode(ctx, bytecode, len, true) != 0)
        return -1;

    static const char kLoadEvent[] = "window.dispatchEvent(new Event('load'));";
    JSValue result = JS_Eval(ctx, kLoadEvent, sizeof(kLoadEvent) - 1,
                             "<global>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(result)) {
        tjs_dump_error(ctx);
        JS_FreeValue(ctx, result);
        return -1;
    }
    JS_FreeValue(ctx, result);
    return 0;
}

int LvglJsEngine::evalString(const char* code) {
    if (!initialized)
        return -1;

    JSValue result = JS_Eval(ctx, code, strlen(code), "<eval>",
                             JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_STRICT);
    if (JS_IsException(result)) {
        tjs_dump_error(ctx);
        JS_FreeValue(ctx, result);
        return -1;
    }
    JS_FreeValue(ctx, result);
    return 0;
}

JSContext* LvglJsEngine::getContext() const {
    return ctx;
}

JSValue LvglJsEngine::lvglJsNamespace() const {
    if (!initialized)
        return JS_UNDEFINED;
    return JS_DupValue(ctx, lvgljsObj);
}

void LvglJsEngine::setParamWriteCallback(ParamWriteCallback cb) {
    paramWrite = std::move(cb);
}

void LvglJsEngine::registerParameter(uint32_t index, const std::string& name) {
    paramNameToIndex[name] = index;
    paramIndexToName[index] = name;
}

void LvglJsEngine::pushParameter(uint32_t index, float value) {
    if (!initialized)
        return;
    JSValue args[2] = {
        JS_NewUint32(ctx, index),
        JS_NewFloat64(ctx, value),
    };
    emit("parameter", 2, args);
    JS_FreeValue(ctx, args[0]);
    JS_FreeValue(ctx, args[1]);
}

void LvglJsEngine::emit(const char* channel, int argc, JSValueConst* argv) {
    if (!initialized)
        return;
    auto it = handlers.find(channel);
    if (it == handlers.end())
        return;

    // Snapshot before iterating: a handler may call lvgljs.off() and mutate the vector.
    std::vector<JSValue> snapshot;
    snapshot.reserve(it->second.size());
    for (JSValue v : it->second)
        snapshot.push_back(JS_DupValue(ctx, v));

    JSValue global = JS_GetGlobalObject(ctx);
    for (JSValue fn : snapshot) {
        JSValue r = JS_Call(ctx, fn, global, argc, argv);
        if (JS_IsException(r))
            tjs_dump_error(ctx);
        JS_FreeValue(ctx, r);
        JS_FreeValue(ctx, fn);
    }
    JS_FreeValue(ctx, global);
}
