#include "DspRuntime.hpp"

extern "C" {
#include "quickjs.h"
}

namespace {

// The bound emitMidiOut(frame, [b0, b1, …]) sink. Routed to the owning DspRuntime's per-block
// collector via the context opaque pointer (one DspRuntime per context). Appends {frame,
// bytes}; the script calls it from inside onBlock.
JSValue emitMidiOut(JSContext* ctx, JSValueConst /*thisVal*/, int argc, JSValueConst* argv) {
    auto* out = static_cast<std::vector<DspRuntime::MidiOut>*>(JS_GetContextOpaque(ctx));
    if (!out || argc < 2) return JS_UNDEFINED;

    std::int32_t frame = 0;
    JS_ToInt32(ctx, &frame, argv[0]);

    std::int64_t len = 0;
    if (JS_GetLength(ctx, argv[1], &len) < 0) return JS_UNDEFINED;

    DspRuntime::MidiOut ev;
    ev.frame = static_cast<std::uint32_t>(frame);
    ev.data.reserve(static_cast<std::size_t>(len));
    for (std::int64_t i = 0; i < len; ++i) {
        JSValue e = JS_GetPropertyUint32(ctx, argv[1], static_cast<std::uint32_t>(i));
        std::int32_t b = 0;
        JS_ToInt32(ctx, &b, e);
        JS_FreeValue(ctx, e);
        ev.data.push_back(static_cast<std::uint8_t>(b & 0xff));
    }
    out->push_back(std::move(ev));
    return JS_UNDEFINED;
}

} // namespace

DspRuntime::DspRuntime() {
    rt_ = JS_NewRuntime();
    ctx_ = JS_NewContext(rt_);
    // The collector is reachable from the sink via the context opaque (one runtime, one
    // collector). out_'s address is stable across clear()s.
    JS_SetContextOpaque(ctx_, &out_);

    JSValue global = JS_GetGlobalObject(ctx_);
    JS_SetPropertyStr(ctx_, global, "emitMidiOut", JS_NewCFunction(ctx_, emitMidiOut, "emitMidiOut", 2));
    JS_FreeValue(ctx_, global);
}

DspRuntime::~DspRuntime() {
    if (rt_) {
        JS_FreeContext(ctx_);
        JS_FreeRuntime(rt_);
    }
}

bool DspRuntime::loadScript(const std::vector<std::uint8_t>& bytecode) {
    JSValue obj = JS_ReadObject(ctx_, bytecode.data(), bytecode.size(), JS_READ_OBJ_BYTECODE);
    if (JS_IsException(obj)) {
        JS_FreeValue(ctx_, obj);
        return false;
    }
    // Global-code bytecode: JS_EvalFunction consumes `obj` and runs the top-level code, which
    // defines the `setConfig` / `onBlock` globals. No JS_ResolveModule — it isn't a module.
    JSValue res = JS_EvalFunction(ctx_, obj);
    const bool ok = !JS_IsException(res);
    JS_FreeValue(ctx_, res);
    loaded_ = ok;
    return ok;
}

bool DspRuntime::setConfig(const std::vector<std::uint8_t>& bytes) {
    if (!loaded_) return false;
    JSValue global = JS_GetGlobalObject(ctx_);
    JSValue fn = JS_GetPropertyStr(ctx_, global, "setConfig");
    bool ok = false;
    if (JS_IsFunction(ctx_, fn)) {
        JSValue arg = JS_NewStringLen(ctx_, reinterpret_cast<const char*>(bytes.data()), bytes.size());
        JSValue res = JS_Call(ctx_, fn, global, 1, &arg);
        ok = !JS_IsException(res);
        JS_FreeValue(ctx_, res);
        JS_FreeValue(ctx_, arg);
    }
    JS_FreeValue(ctx_, fn);
    JS_FreeValue(ctx_, global);
    return ok;
}

std::vector<DspRuntime::MidiOut> DspRuntime::runBlock(const std::vector<MidiIn>& midi,
                                                      const BlockInfo& block) {
    out_.clear();
    if (!loaded_) return {};
    JSContext* ctx = ctx_;

    // input = { midi: [{frame, data:[…]}], frames, sampleRate, tempo, ppqPosBlockStart,
    //           transportPlaying }
    JSValue input = JS_NewObject(ctx);
    JSValue arr = JS_NewArray(ctx);
    for (std::size_t i = 0; i < midi.size(); ++i) {
        JSValue ev = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, ev, "frame", JS_NewInt32(ctx, static_cast<std::int32_t>(midi[i].frame)));
        JSValue data = JS_NewArray(ctx);
        for (std::size_t j = 0; j < midi[i].data.size(); ++j)
            JS_SetPropertyUint32(ctx, data, static_cast<std::uint32_t>(j), JS_NewInt32(ctx, midi[i].data[j]));
        JS_SetPropertyStr(ctx, ev, "data", data);
        JS_SetPropertyUint32(ctx, arr, static_cast<std::uint32_t>(i), ev);
    }
    JS_SetPropertyStr(ctx, input, "midi", arr);
    JS_SetPropertyStr(ctx, input, "frames", JS_NewInt32(ctx, static_cast<std::int32_t>(block.frames)));
    JS_SetPropertyStr(ctx, input, "sampleRate", JS_NewFloat64(ctx, block.sampleRate));
    JS_SetPropertyStr(ctx, input, "tempo", JS_NewFloat64(ctx, block.tempo));
    JS_SetPropertyStr(ctx, input, "ppqPosBlockStart", JS_NewFloat64(ctx, block.ppqPosBlockStart));
    JS_SetPropertyStr(ctx, input, "transportPlaying", JS_NewBool(ctx, block.transportPlaying));

    JSValue global = JS_GetGlobalObject(ctx);
    JSValue fn = JS_GetPropertyStr(ctx, global, "onBlock");
    if (JS_IsFunction(ctx, fn)) {
        JSValue res = JS_Call(ctx, fn, global, 1, &input);  // fills out_ via emitMidiOut
        JS_FreeValue(ctx, res);
    }
    JS_FreeValue(ctx, fn);
    JS_FreeValue(ctx, global);
    JS_FreeValue(ctx, input);

    return out_;
}
