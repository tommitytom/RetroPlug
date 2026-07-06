#include "DspRuntime.hpp"

extern "C" {
#include "quickjs.h"
}

#include "system/SystemTypes.hpp"  // AudioBlockInfo
#include "util/PpqUtil.hpp"        // PpqUtil::eachTick (drift-exact PPQ iterator)

namespace {

// The DspRuntime is the context opaque (one runtime per context), so every bound thunk reaches
// its collector + block state through it.
DspRuntime* self(JSContext* ctx) { return static_cast<DspRuntime*>(JS_GetContextOpaque(ctx)); }

// emitMidiOut(frame, [b0, b1, …]) — the MIDI output sink; appends {frame, bytes} to out_.
JSValue emitMidiOut(JSContext* ctx, JSValueConst /*thisVal*/, int argc, JSValueConst* argv) {
    DspRuntime* rt = self(ctx);
    if (!rt || argc < 2) return JS_UNDEFINED;

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
    rt->out_.push_back(std::move(ev));
    return JS_UNDEFINED;
}

// pushSerialIn(frame, byte) — the serial-in sink; appends {frame, byte} to serialOut_. The host
// drains these into the attached system's serial input after the block. `frame` is carried for
// ABI symmetry with emitMidiOut but the GB serial pump is a plain FIFO (intra-block frame is not
// yet honoured — same as MgbPassthroughRole).
JSValue pushSerialIn(JSContext* ctx, JSValueConst /*thisVal*/, int argc, JSValueConst* argv) {
    DspRuntime* rt = self(ctx);
    if (!rt || argc < 2) return JS_UNDEFINED;

    std::int32_t frame = 0;
    JS_ToInt32(ctx, &frame, argv[0]);
    std::int32_t byte = 0;
    JS_ToInt32(ctx, &byte, argv[1]);

    rt->serialOut_.push_back({ static_cast<std::uint32_t>(frame), static_cast<std::uint8_t>(byte & 0xff) });
    return JS_UNDEFINED;
}

// eachTick(resolution, callback) — walks the `resolution`-PPQN ticks in the current block via the
// shipped drift-exact PpqUtil::eachTick (nextTick_ persists across blocks), calling
// callback(tickIndex, sampleOffset) for each. A script emits a sample-accurate clock from here.
JSValue eachTick(JSContext* ctx, JSValueConst /*thisVal*/, int argc, JSValueConst* argv) {
    DspRuntime* rt = self(ctx);
    if (!rt || argc < 2) return JS_UNDEFINED;

    std::int32_t resolution = 0;
    JS_ToInt32(ctx, &resolution, argv[0]);
    JSValueConst cb = argv[1];
    if (!JS_IsFunction(ctx, cb)) return JS_UNDEFINED;

    const AudioBlockInfo info{ rt->curBlock_.frames, rt->curBlock_.sampleRate, rt->curBlock_.tempo,
                               rt->curBlock_.ppqPosBlockStart, rt->curBlock_.transportPlaying };
    PpqUtil::eachTick(info, static_cast<std::uint32_t>(resolution), rt->nextTick_,
                      [&](std::uint32_t tick, std::uint32_t off) {
        JSValue args[2] = { JS_NewInt32(ctx, static_cast<std::int32_t>(tick)),
                            JS_NewInt32(ctx, static_cast<std::int32_t>(off)) };
        JSValue r = JS_Call(ctx, cb, JS_UNDEFINED, 2, args);  // re-entrant JS call is fine
        JS_FreeValue(ctx, r);
        JS_FreeValue(ctx, args[0]);
        JS_FreeValue(ctx, args[1]);
    });
    return JS_UNDEFINED;
}

} // namespace

DspRuntime::DspRuntime() {
    rt_ = JS_NewRuntime();
    ctx_ = JS_NewContext(rt_);
    // The DspRuntime is the context opaque, so the bound thunks reach out_ / curBlock_ / nextTick_.
    JS_SetContextOpaque(ctx_, this);

    JSValue global = JS_GetGlobalObject(ctx_);
    JS_SetPropertyStr(ctx_, global, "emitMidiOut", JS_NewCFunction(ctx_, emitMidiOut, "emitMidiOut", 2));
    JS_SetPropertyStr(ctx_, global, "pushSerialIn", JS_NewCFunction(ctx_, pushSerialIn, "pushSerialIn", 2));
    JS_SetPropertyStr(ctx_, global, "eachTick", JS_NewCFunction(ctx_, eachTick, "eachTick", 2));
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
    if (ok) nextTick_ = 0;  // a fresh script = a fresh clock
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
    serialOut_.clear();
    curBlock_ = block;  // so the eachTick thunk sees this block's info
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
