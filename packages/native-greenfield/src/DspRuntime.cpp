#include "DspRuntime.hpp"

extern "C" {
#include "quickjs.h"
}

namespace {

// The DspRuntime is the context opaque (one runtime per context), so every bound sink thunk reaches
// its collector through it.
DspRuntime* self(JSContext* ctx) { return static_cast<DspRuntime*>(JS_GetContextOpaque(ctx)); }

// pushSerialIn(system, frame, byte) — the serial-in sink; appends {system, frame, byte} to
// serialIn_. The caller delivers each byte to that system's serial input after the block. `frame`
// is carried for ABI symmetry but the GB serial pump is a plain FIFO (intra-block frame not yet
// honoured — the LSDj MidiSync clock and mGB passthrough both just need the bytes).
JSValue pushSerialIn(JSContext* ctx, JSValueConst /*thisVal*/, int argc, JSValueConst* argv) {
    DspRuntime* rt = self(ctx);
    if (!rt || argc < 3) return JS_UNDEFINED;

    std::int32_t system = 0, frame = 0, byte = 0;
    JS_ToInt32(ctx, &system, argv[0]);
    JS_ToInt32(ctx, &frame, argv[1]);
    JS_ToInt32(ctx, &byte, argv[2]);

    rt->serialIn_.push_back({ static_cast<std::uint32_t>(system), static_cast<std::uint32_t>(frame),
                              static_cast<std::uint8_t>(byte & 0xff) });
    return JS_UNDEFINED;
}

// emitMidiOut(system, frame, [b0, b1, …]) — the host MIDI-out sink; appends {system, frame, bytes}.
JSValue emitMidiOut(JSContext* ctx, JSValueConst /*thisVal*/, int argc, JSValueConst* argv) {
    DspRuntime* rt = self(ctx);
    if (!rt || argc < 3) return JS_UNDEFINED;

    std::int32_t system = 0, frame = 0;
    JS_ToInt32(ctx, &system, argv[0]);
    JS_ToInt32(ctx, &frame, argv[1]);

    std::int64_t len = 0;
    if (JS_GetLength(ctx, argv[2], &len) < 0) return JS_UNDEFINED;

    DspRuntime::MidiOut ev;
    ev.system = static_cast<std::uint32_t>(system);
    ev.frame = static_cast<std::uint32_t>(frame);
    ev.data.reserve(static_cast<std::size_t>(len));
    for (std::int64_t i = 0; i < len; ++i) {
        JSValue e = JS_GetPropertyUint32(ctx, argv[2], static_cast<std::uint32_t>(i));
        std::int32_t b = 0;
        JS_ToInt32(ctx, &b, e);
        JS_FreeValue(ctx, e);
        ev.data.push_back(static_cast<std::uint8_t>(b & 0xff));
    }
    rt->midiOut_.push_back(std::move(ev));
    return JS_UNDEFINED;
}

// pressButton(system, frame, button, down) — the role-generated button sink; appends the transition
// to buttonOut_. (Distinct from a host UI tap, which the render loop delivers to a core directly.)
JSValue pressButton(JSContext* ctx, JSValueConst /*thisVal*/, int argc, JSValueConst* argv) {
    DspRuntime* rt = self(ctx);
    if (!rt || argc < 4) return JS_UNDEFINED;

    std::int32_t system = 0, frame = 0, button = 0;
    JS_ToInt32(ctx, &system, argv[0]);
    JS_ToInt32(ctx, &frame, argv[1]);
    JS_ToInt32(ctx, &button, argv[2]);
    const bool down = JS_ToBool(ctx, argv[3]) > 0;

    rt->buttonOut_.push_back({ static_cast<std::uint32_t>(system), static_cast<std::uint32_t>(frame),
                               static_cast<std::uint32_t>(button), down });
    return JS_UNDEFINED;
}

} // namespace

DspRuntime::DspRuntime() {
    rt_ = JS_NewRuntime();
    ctx_ = JS_NewContext(rt_);
    // The DspRuntime is the context opaque, so the bound sink thunks reach its collectors.
    JS_SetContextOpaque(ctx_, this);

    JSValue global = JS_GetGlobalObject(ctx_);
    JS_SetPropertyStr(ctx_, global, "pushSerialIn", JS_NewCFunction(ctx_, pushSerialIn, "pushSerialIn", 3));
    JS_SetPropertyStr(ctx_, global, "emitMidiOut", JS_NewCFunction(ctx_, emitMidiOut, "emitMidiOut", 3));
    JS_SetPropertyStr(ctx_, global, "pressButton", JS_NewCFunction(ctx_, pressButton, "pressButton", 4));
    JS_FreeValue(ctx_, global);
}

DspRuntime::~DspRuntime() {
    if (rt_) {
        JS_FreeContext(ctx_);
        JS_FreeRuntime(rt_);
    }
}

bool DspRuntime::loadKernel(const std::vector<std::uint8_t>& bytecode) {
    // QuickJS's stack-overflow guard is calibrated against the stack top captured when the runtime
    // was created. This context is driven from whichever thread owns it (the control thread for the
    // pull path, the audio thread once running) — so re-anchor the stack top to the CURRENT thread
    // before entering JS, or a call from a different stack throws a spurious stack overflow.
    JS_UpdateStackTop(rt_);
    JSValue obj = JS_ReadObject(ctx_, bytecode.data(), bytecode.size(), JS_READ_OBJ_BYTECODE);
    if (JS_IsException(obj)) {
        JS_FreeValue(ctx_, obj);
        return false;
    }
    // Global-code bytecode: JS_EvalFunction consumes `obj` and runs the top-level code, which
    // constructs the kernel and defines the `setSystems` / `processBlock` globals. Not a module.
    JSValue res = JS_EvalFunction(ctx_, obj);
    const bool ok = !JS_IsException(res);
    JS_FreeValue(ctx_, res);
    loaded_ = ok;
    return ok;
}

bool DspRuntime::setSystems(const std::vector<std::uint8_t>& json) {
    if (!loaded_) return false;
    JS_UpdateStackTop(rt_);  // re-anchor for the calling thread (see loadKernel)
    JSValue global = JS_GetGlobalObject(ctx_);
    JSValue fn = JS_GetPropertyStr(ctx_, global, "setSystems");
    bool ok = false;
    if (JS_IsFunction(ctx_, fn)) {
        JSValue arg = JS_NewStringLen(ctx_, reinterpret_cast<const char*>(json.data()), json.size());
        JSValue res = JS_Call(ctx_, fn, global, 1, &arg);
        ok = !JS_IsException(res);
        JS_FreeValue(ctx_, res);
        JS_FreeValue(ctx_, arg);
    }
    JS_FreeValue(ctx_, fn);
    JS_FreeValue(ctx_, global);
    return ok;
}

void DspRuntime::processBlock(const std::vector<MidiIn>& midi,
                              const std::vector<ButtonIn>& buttons,
                              const std::vector<KeyIn>& keys,
                              const BlockInfo& block) {
    serialIn_.clear();
    midiOut_.clear();
    buttonOut_.clear();
    if (!loaded_) return;
    JS_UpdateStackTop(rt_);  // re-anchor for the calling thread (see loadKernel)
    JSContext* ctx = ctx_;

    // input = { frames, sampleRate, tempo, ppqStart, transport,
    //           midiIn:  [{frame, data:[…]}],
    //           buttons: [{system, frame, button, down}],
    //           keys:    [{system, frame, key, down}] }
    // The event arrays are always present (empty is fine) — the kernel filters keys/buttons per
    // system and would fault on `undefined`.
    JSValue input = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, input, "frames", JS_NewInt32(ctx, static_cast<std::int32_t>(block.frames)));
    JS_SetPropertyStr(ctx, input, "sampleRate", JS_NewFloat64(ctx, block.sampleRate));
    JS_SetPropertyStr(ctx, input, "tempo", JS_NewFloat64(ctx, block.tempo));
    JS_SetPropertyStr(ctx, input, "ppqStart", JS_NewFloat64(ctx, block.ppqStart));
    JS_SetPropertyStr(ctx, input, "transport", JS_NewBool(ctx, block.transport));

    JSValue midiArr = JS_NewArray(ctx);
    for (std::size_t i = 0; i < midi.size(); ++i) {
        JSValue ev = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, ev, "frame", JS_NewInt32(ctx, static_cast<std::int32_t>(midi[i].frame)));
        JSValue data = JS_NewArray(ctx);
        for (std::size_t j = 0; j < midi[i].data.size(); ++j)
            JS_SetPropertyUint32(ctx, data, static_cast<std::uint32_t>(j), JS_NewInt32(ctx, midi[i].data[j]));
        JS_SetPropertyStr(ctx, ev, "data", data);
        JS_SetPropertyUint32(ctx, midiArr, static_cast<std::uint32_t>(i), ev);
    }
    JS_SetPropertyStr(ctx, input, "midiIn", midiArr);

    JSValue btnArr = JS_NewArray(ctx);
    for (std::size_t i = 0; i < buttons.size(); ++i) {
        JSValue ev = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, ev, "system", JS_NewInt32(ctx, static_cast<std::int32_t>(buttons[i].system)));
        JS_SetPropertyStr(ctx, ev, "frame", JS_NewInt32(ctx, static_cast<std::int32_t>(buttons[i].frame)));
        JS_SetPropertyStr(ctx, ev, "button", JS_NewInt32(ctx, static_cast<std::int32_t>(buttons[i].button)));
        JS_SetPropertyStr(ctx, ev, "down", JS_NewBool(ctx, buttons[i].down));
        JS_SetPropertyUint32(ctx, btnArr, static_cast<std::uint32_t>(i), ev);
    }
    JS_SetPropertyStr(ctx, input, "buttons", btnArr);

    JSValue keyArr = JS_NewArray(ctx);
    for (std::size_t i = 0; i < keys.size(); ++i) {
        JSValue ev = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, ev, "system", JS_NewInt32(ctx, static_cast<std::int32_t>(keys[i].system)));
        JS_SetPropertyStr(ctx, ev, "frame", JS_NewInt32(ctx, static_cast<std::int32_t>(keys[i].frame)));
        JS_SetPropertyStr(ctx, ev, "key", JS_NewInt32(ctx, static_cast<std::int32_t>(keys[i].key)));
        JS_SetPropertyStr(ctx, ev, "down", JS_NewBool(ctx, keys[i].down));
        JS_SetPropertyUint32(ctx, keyArr, static_cast<std::uint32_t>(i), ev);
    }
    JS_SetPropertyStr(ctx, input, "keys", keyArr);

    JSValue global = JS_GetGlobalObject(ctx);
    JSValue fn = JS_GetPropertyStr(ctx, global, "processBlock");
    if (JS_IsFunction(ctx, fn)) {
        JSValue res = JS_Call(ctx, fn, global, 1, &input);  // fills serialIn_/midiOut_/buttonOut_ via the sinks
        JS_FreeValue(ctx, res);
    }
    JS_FreeValue(ctx, fn);
    JS_FreeValue(ctx, global);
    JS_FreeValue(ctx, input);
}
