#include "host/dsp/DspRuntime.hpp"

#include <chrono>

#ifdef RETROPLUG_PROFILE
#include <cstdlib>
#include <malloc.h>  // malloc_usable_size — Linux/glibc; RETROPLUG_PROFILE is a dev-only build
#endif

extern "C" {
#include "quickjs.h"
}

namespace {

// The DspRuntime is the context opaque (one runtime per context), so every bound sink thunk reaches
// its collector through it.
DspRuntime* self(JSContext* ctx) { return static_cast<DspRuntime*>(JS_GetContextOpaque(ctx)); }

#ifdef RETROPLUG_PROFILE
// Counting allocator (spec/08-profiling.md). The JS_NewRuntime2 opaque is a DspAllocCounters*; each
// thunk bumps it and delegates to libc. `js_malloc_usable_size` receives only the ptr, so it calls
// the real platform usable-size — which QuickJS ALSO uses for its own byte accounting + GC threshold,
// so it must return the true value.
std::size_t rpUsable(const void* p) { return p ? malloc_usable_size(const_cast<void*>(p)) : 0; }
void rpBumpAlloc(DspAllocCounters* c, std::size_t usable) {
    c->allocCalls++;
    c->allocBytes += usable;
    c->liveBytes += static_cast<std::int64_t>(usable);
    if (c->liveBytes > static_cast<std::int64_t>(c->peakBytes)) c->peakBytes = static_cast<std::uint64_t>(c->liveBytes);
}
void* rpCalloc(void* op, std::size_t n, std::size_t sz) {
    void* p = std::calloc(n, sz);
    if (p) rpBumpAlloc(static_cast<DspAllocCounters*>(op), rpUsable(p));
    return p;
}
void* rpMalloc(void* op, std::size_t sz) {
    void* p = std::malloc(sz);
    if (p) rpBumpAlloc(static_cast<DspAllocCounters*>(op), rpUsable(p));
    return p;
}
void rpFree(void* op, void* p) {
    if (p) {
        auto* c = static_cast<DspAllocCounters*>(op);
        c->freeCalls++;
        c->liveBytes -= static_cast<std::int64_t>(rpUsable(p));
    }
    std::free(p);
}
void* rpRealloc(void* op, void* p, std::size_t sz) {
    auto* c = static_cast<DspAllocCounters*>(op);
    const std::int64_t before = p ? static_cast<std::int64_t>(rpUsable(p)) : 0;
    void* q = std::realloc(p, sz);
    if (q || sz == 0) {
        c->reallocCalls++;
        const std::int64_t after = q ? static_cast<std::int64_t>(rpUsable(q)) : 0;
        if (after > before) c->allocBytes += static_cast<std::uint64_t>(after - before);
        c->liveBytes += (after - before);
        if (c->liveBytes > static_cast<std::int64_t>(c->peakBytes)) c->peakBytes = static_cast<std::uint64_t>(c->liveBytes);
    }
    return q;
}
std::size_t rpUsableCb(const void* p) { return rpUsable(p); }
#endif

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

// emitCoreMidi(system, frame, [b0, b1, …]) — the MIDI-IN-to-core sink; appends {system, frame, bytes}.
// The caller fans these to the addressed core's onMidi (opposite direction to emitMidiOut → the DAW).
JSValue emitCoreMidi(JSContext* ctx, JSValueConst /*thisVal*/, int argc, JSValueConst* argv) {
    DspRuntime* rt = self(ctx);
    if (!rt || argc < 3) return JS_UNDEFINED;

    std::int32_t system = 0, frame = 0;
    JS_ToInt32(ctx, &system, argv[0]);
    JS_ToInt32(ctx, &frame, argv[1]);

    std::int64_t len = 0;
    if (JS_GetLength(ctx, argv[2], &len) < 0) return JS_UNDEFINED;

    DspRuntime::CoreMidi ev;
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
    rt->coreMidi_.push_back(std::move(ev));
    return JS_UNDEFINED;
}

// pushCoreBytes(system, frame, [b0, b1, …]) — the RAW-bytes-to-core sink; appends {system, frame, bytes}.
// The un-framed twin of emitCoreMidi: no length cap (the caller fans these straight to the core's byte
// device — the NES N8 FIFO — with no MidiEvent frame), for a byte protocol carried over the transport.
JSValue pushCoreBytes(JSContext* ctx, JSValueConst /*thisVal*/, int argc, JSValueConst* argv) {
    DspRuntime* rt = self(ctx);
    if (!rt || argc < 3) return JS_UNDEFINED;

    std::int32_t system = 0, frame = 0;
    JS_ToInt32(ctx, &system, argv[0]);
    JS_ToInt32(ctx, &frame, argv[1]);

    std::int64_t len = 0;
    if (JS_GetLength(ctx, argv[2], &len) < 0) return JS_UNDEFINED;

    DspRuntime::CoreBytes ev;
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
    rt->coreBytes_.push_back(std::move(ev));
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

#ifdef RETROPLUG_PROFILE
// Monotonic wall-clock in ns (spec/08-profiling.md Tier B). Spans store µs relative to a per-window base.
std::uint64_t nowNs() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

// Per-role runtime-tracing thunks, bound onto the DSP global ONLY in a profile build. The JS kernel
// brackets each role's dsp() call with spanBegin(label)/spanEnd() and registers kind→label via
// traceName(); everything reaches its DspRuntime through the context opaque, like the sinks.
JSValue spanBeginThunk(JSContext* ctx, JSValueConst /*thisVal*/, int argc, JSValueConst* argv) {
    DspRuntime* rt = self(ctx);
    std::int32_t label = 0;
    if (rt && argc >= 1) {
        JS_ToInt32(ctx, &label, argv[0]);
        rt->spanBegin(static_cast<std::uint32_t>(label));
    }
    return JS_UNDEFINED;
}
JSValue spanEndThunk(JSContext* ctx, JSValueConst /*thisVal*/, int /*argc*/, JSValueConst* /*argv*/) {
    if (DspRuntime* rt = self(ctx)) rt->spanEnd();
    return JS_UNDEFINED;
}
JSValue traceNameThunk(JSContext* ctx, JSValueConst /*thisVal*/, int argc, JSValueConst* argv) {
    DspRuntime* rt = self(ctx);
    if (rt && argc >= 2) {
        std::int32_t label = 0;
        JS_ToInt32(ctx, &label, argv[0]);
        if (const char* s = JS_ToCString(ctx, argv[1])) {
            rt->traceName(static_cast<std::uint32_t>(label), s);
            JS_FreeCString(ctx, s);
        }
    }
    return JS_UNDEFINED;
}
#endif

} // namespace

DspRuntime::DspRuntime() {
#ifdef RETROPLUG_PROFILE
    // Route every JS allocation through the counting thunks. QuickJS COPIES `mf` but RETAINS the
    // opaque, so `counters_` (a member) is the live counter; `mf` can be a ctor local.
    JSMallocFunctions mf{ rpCalloc, rpMalloc, rpFree, rpRealloc, rpUsableCb };
    rt_ = JS_NewRuntime2(&mf, &counters_);
#else
    rt_ = JS_NewRuntime();
#endif
    ctx_ = JS_NewContext(rt_);
    // The DspRuntime is the context opaque, so the bound sink thunks reach its collectors.
    JS_SetContextOpaque(ctx_, this);

    JSValue global = JS_GetGlobalObject(ctx_);
    JS_SetPropertyStr(ctx_, global, "pushSerialIn", JS_NewCFunction(ctx_, pushSerialIn, "pushSerialIn", 3));
    JS_SetPropertyStr(ctx_, global, "emitMidiOut", JS_NewCFunction(ctx_, emitMidiOut, "emitMidiOut", 3));
    JS_SetPropertyStr(ctx_, global, "emitCoreMidi", JS_NewCFunction(ctx_, emitCoreMidi, "emitCoreMidi", 3));
    JS_SetPropertyStr(ctx_, global, "pushCoreBytes", JS_NewCFunction(ctx_, pushCoreBytes, "pushCoreBytes", 3));
    JS_SetPropertyStr(ctx_, global, "pressButton", JS_NewCFunction(ctx_, pressButton, "pressButton", 4));
#ifdef RETROPLUG_PROFILE
    // Per-role runtime tracing (spec/08-profiling.md Tier B): bind the span thunks + name the fixed
    // native pipeline stages. Only present in a profile build → the kernel detects tracing via
    // `typeof spanBegin === "function"` and stays inert in production.
    JS_SetPropertyStr(ctx_, global, "spanBegin", JS_NewCFunction(ctx_, spanBeginThunk, "spanBegin", 1));
    JS_SetPropertyStr(ctx_, global, "spanEnd", JS_NewCFunction(ctx_, spanEndThunk, "spanEnd", 0));
    JS_SetPropertyStr(ctx_, global, "traceName", JS_NewCFunction(ctx_, traceNameThunk, "traceName", 2));
    traceName(DSP_SPAN_KERNEL, "dsp-kernel");
    traceName(DSP_SPAN_MARSHAL, "marshal");
    traceName(DSP_SPAN_JSCALL, "js-call");
    traceName(DSP_SPAN_APU, "apu-render");
    traceName(DSP_SPAN_PUBLISH, "state-publish");
#endif
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
                              const std::vector<SerialOut>& serialOut,
                              const BlockInfo& block) {
    serialIn_.clear();
    midiOut_.clear();
    coreMidi_.clear();
    coreBytes_.clear();
    buttonOut_.clear();
    if (!loaded_) return;
    JS_UpdateStackTop(rt_);  // re-anchor for the calling thread (see loadKernel)
    JSContext* ctx = ctx_;

#ifdef RETROPLUG_PROFILE
    // Bracket this block's allocation traffic natively (marshalling + JS_Call + frees) so the window
    // aggregates track the per-block max with no per-block RPC.
    const std::uint64_t blockCalls0 = counters_.allocCalls;
    const std::uint64_t blockBytes0 = counters_.allocBytes;
#endif

    // input = { frames, sampleRate, tempo, ppqStart, transport,
    //           midiIn:    [{frame, data:[…]}],
    //           buttons:   [{system, frame, button, down}],
    //           keys:      [{system, frame, key, down}],
    //           serialOut: [{system, byte}] }
    // The event arrays are always present (empty is fine) — the kernel filters keys/buttons per
    // system and would fault on `undefined`.
#ifdef RETROPLUG_PROFILE
    spanBegin(DSP_SPAN_MARSHAL);
#endif
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

    JSValue serialArr = JS_NewArray(ctx);
    for (std::size_t i = 0; i < serialOut.size(); ++i) {
        JSValue ev = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, ev, "system", JS_NewInt32(ctx, static_cast<std::int32_t>(serialOut[i].system)));
        JS_SetPropertyStr(ctx, ev, "byte", JS_NewInt32(ctx, static_cast<std::int32_t>(serialOut[i].byte)));
        JS_SetPropertyUint32(ctx, serialArr, static_cast<std::uint32_t>(i), ev);
    }
    JS_SetPropertyStr(ctx, input, "serialOut", serialArr);
#ifdef RETROPLUG_PROFILE
    spanEnd();                    // marshal
    spanBegin(DSP_SPAN_JSCALL);
#endif

    JSValue global = JS_GetGlobalObject(ctx);
    JSValue fn = JS_GetPropertyStr(ctx, global, "processBlock");
    if (JS_IsFunction(ctx, fn)) {
        JSValue res = JS_Call(ctx, fn, global, 1, &input);  // fills serialIn_/midiOut_/buttonOut_ via the sinks
        JS_FreeValue(ctx, res);
    }
    JS_FreeValue(ctx, fn);
    JS_FreeValue(ctx, global);
    JS_FreeValue(ctx, input);

#ifdef RETROPLUG_PROFILE
    spanEnd();  // js-call (paired with the spanBegin before JS_GetGlobalObject)
    ++blockCount_;
    const std::uint64_t dCalls = counters_.allocCalls - blockCalls0;
    const std::uint64_t dBytes = counters_.allocBytes - blockBytes0;
    if (dCalls > maxBlockAllocCalls_) maxBlockAllocCalls_ = dCalls;
    if (dBytes > maxBlockAllocBytes_) maxBlockAllocBytes_ = dBytes;
#endif
}

// --- allocation / GC profiling (spec/08-profiling.md) --------------------------------------------
#ifdef RETROPLUG_PROFILE
DspAllocStats DspRuntime::allocStats() const {
    DspAllocStats s;
    s.enabled            = true;
    s.allocCalls         = counters_.allocCalls   - base_.allocCalls;
    s.reallocCalls       = counters_.reallocCalls - base_.reallocCalls;
    s.freeCalls          = counters_.freeCalls    - base_.freeCalls;
    s.allocBytes         = counters_.allocBytes   - base_.allocBytes;
    s.liveBytesDelta     = counters_.liveBytes    - base_.liveBytes;
    s.peakBytes          = counters_.peakBytes;
    s.blockCount         = blockCount_;
    s.maxBlockAllocCalls = maxBlockAllocCalls_;
    s.maxBlockAllocBytes = maxBlockAllocBytes_;
    return s;
}

void DspRuntime::resetAllocStats(bool disableAutoGc) {
    base_ = counters_;  // window deltas are measured against this snapshot
    blockCount_ = 0;
    maxBlockAllocCalls_ = 0;
    maxBlockAllocBytes_ = 0;
    // Pin auto-GC off so the window has zero collections by construction (deterministic). QuickJS
    // only checks the threshold on object creation, so SIZE_MAX means it never trips.
    if (disableAutoGc) JS_SetGCThreshold(rt_, static_cast<std::size_t>(-1));
}

DspGcResult DspRuntime::runGc() {
    JS_UpdateStackTop(rt_);  // re-anchor for the calling thread (see loadKernel)
    const std::int64_t before = counters_.liveBytes;
    const auto t0 = std::chrono::steady_clock::now();
    JS_RunGC(rt_);
    const auto t1 = std::chrono::steady_clock::now();
    DspGcResult r;
    r.enabled    = true;
    r.ms         = std::chrono::duration<double, std::milli>(t1 - t0).count();
    r.freedBytes = before - counters_.liveBytes;  // cyclic garbage reclaimed (~0 = no cycles)
    return r;
}

// --- per-role runtime tracing (spec/08-profiling.md Tier B) ---
void DspRuntime::spanBegin(std::uint32_t label) {
    if (!traceArmed_) return;
    traceStack_.emplace_back(label, (nowNs() - traceBaseNs_) / 1000.0);  // µs relative to the window base
}

void DspRuntime::spanEnd() {
    if (!traceArmed_ || traceStack_.empty()) return;
    const auto top = traceStack_.back();
    traceStack_.pop_back();
    traceSpans_.push_back({ top.first, top.second, (nowNs() - traceBaseNs_) / 1000.0 });
}

void DspRuntime::traceName(std::uint32_t label, const std::string& name) {
    if (traceNames_.size() <= label) traceNames_.resize(label + 1);
    traceNames_[label] = name;
}

void DspRuntime::traceReset(bool arm) {
    traceSpans_.clear();
    traceStack_.clear();
    traceArmed_  = arm;
    traceBaseNs_ = nowNs();
    // Flip the kernel's in-JS trace flag (like setSystems calls a global) so JS emits per-role spans
    // ONLY while armed — the non-traced path then makes zero span-thunk crossings (alloc counts pristine).
    if (!loaded_) return;
    JS_UpdateStackTop(rt_);
    JSValue global = JS_GetGlobalObject(ctx_);
    JSValue fn = JS_GetPropertyStr(ctx_, global, "__setTrace");
    if (JS_IsFunction(ctx_, fn)) {
        JSValue arg = JS_NewBool(ctx_, arm);
        JSValue res = JS_Call(ctx_, fn, global, 1, &arg);
        JS_FreeValue(ctx_, res);
        JS_FreeValue(ctx_, arg);
    }
    JS_FreeValue(ctx_, fn);
    JS_FreeValue(ctx_, global);
}

std::vector<DspTraceSpan> DspRuntime::traceSpans() const { return traceSpans_; }
std::vector<std::string>  DspRuntime::traceNames() const { return traceNames_; }
#else
DspAllocStats DspRuntime::allocStats() const { return DspAllocStats{}; }  // enabled=false
void          DspRuntime::resetAllocStats(bool /*disableAutoGc*/) {}
DspGcResult   DspRuntime::runGc() { return DspGcResult{}; }               // enabled=false
void          DspRuntime::spanBegin(std::uint32_t /*label*/) {}
void          DspRuntime::spanEnd() {}
void          DspRuntime::traceName(std::uint32_t /*label*/, const std::string& /*name*/) {}
void          DspRuntime::traceReset(bool /*arm*/) {}
std::vector<DspTraceSpan> DspRuntime::traceSpans() const { return {}; }
std::vector<std::string>  DspRuntime::traceNames() const { return {}; }
#endif
