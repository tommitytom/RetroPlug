// The greenfield DPF plugin (DSP-first, UI-less). It hosts the SAME control-plane runtime the test
// host does — TjsHostRuntime + BackendFacade + the __rpcSend bridge on Symbol.for("plugin") — evals
// the embedded control-plane bundle (which composes the stores + DSP kernel and defines the __rp_*
// globals), and drives the Engine per audio block from DPF's run(). No editor: get/setState and the
// RETROPLUG_AUTOLOAD_PROJECT hook go through the JS project globals (base64 done in JS).
#include "DistrhoPlugin.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include "dpfjs/host/TjsHostRuntime.hpp"  // shared txiki/QuickJS host (+ tjs.h/quickjs.h)

#include "BackendFacade.hpp"
#include "BackendRpcRegistration.hpp"
#include "TypedRpcServer.h"
#include "codecs/QuickJSCodec.h"
#include "transports/QuickJSTransport.h"

// The embedded control-plane bundle (bytecode) — build/native-greenfield/cp-bundle_data.c, gfcp_ prefix.
extern "C" {
extern const std::uint8_t  gfcp_cp_bundle[];
extern const std::uint32_t gfcp_cp_bundle_size;
}

START_NAMESPACE_DISTRHO

using GreenfieldRpcServer = rpcpp::TypedRpcServer<BackendFacade, rpcpp::QuickJSCodec>;

class PluginGreenfieldDSP : public Plugin {
    // Control-plane runtime (main-thread only): the txiki context + the facade it drives over __rpcSend.
    TjsHostRuntime host_;
    BackendFacade  service_;
    std::unique_ptr<rpcpp::QuickJSTransport> transport_;
    std::unique_ptr<GreenfieldRpcServer>     server_;
    bool jsReady_ = false;

    float gainDb_ = 0.0f;

public:
    PluginGreenfieldDSP() : Plugin(1 /*params*/, 0 /*programs*/, 1 /*states*/) {
        bootControlPlane();
    }

protected:
    // --- information ---
    const char* getLabel()       const noexcept override { return "RetroPlugGreenfield"; }
    const char* getDescription() const          override { return "Greenfield multi-system retro emulator host"; }
    const char* getMaker()       const noexcept override { return "tommitytom"; }
    const char* getLicense()     const noexcept override { return "MIT"; }
    uint32_t    getVersion()     const noexcept override { return d_version(0, 1, 0); }
    int64_t     getUniqueId()    const noexcept override { return d_cconst('R', 'P', 'g', 'f'); }

    // --- parameters (one: master gain, applied post-render) ---
    void initParameter(uint32_t index, Parameter& p) override {
        if (index != 0) return;
        p.symbol = "gain"; p.name = "Master Gain"; p.shortName = "Gain"; p.unit = "dB";
        p.ranges.min = -90.0f; p.ranges.max = 12.0f; p.ranges.def = 0.0f;
        p.hints = kParameterIsAutomatable;
    }
    float getParameterValue(uint32_t index) const override { return index == 0 ? gainDb_ : 0.0f; }
    void  setParameterValue(uint32_t index, float value) override { if (index == 0) gainDb_ = value; }

    // --- state: one "project" key = base64(.rplg) via the JS control plane ---
    void initState(uint32_t index, State& state) override {
        if (index != 0) return;
        state.hints        = kStateIsHostReadable | kStateIsHostWritable;
        state.key          = "project";
        state.label        = "Project";
        state.description  = "Serialized RetroPlug project (.rplg, base64).";
        state.defaultValue = "";
    }
    String getState(const char* key) const override {
        if (std::strcmp(key, "project") != 0) return String();
        return String(const_cast<PluginGreenfieldDSP*>(this)->callGlobal("__rp_saveProjectB64", nullptr).c_str());
    }
    void setState(const char* key, const char* value) override {
        if (std::strcmp(key, "project") != 0) return;
        callGlobal("__rp_loadProjectB64", value ? value : "");
    }

    // --- audio lifecycle ---
    void activate()   override { service_.pluginActivate(); }    // audioRunning_ + active_=&queued_
    void deactivate() override { service_.pluginDeactivate(); }  // + freePending + reclaim released
    void sampleRateChanged(double newSampleRate) override { service_.setSampleRate(newSampleRate); }

    // --- the per-block loop (replaces AudioDriverRpcService::audioLoop) ---
    void run(const float**, float** outputs, uint32_t frames,
             const MidiEvent* midiEvents, uint32_t midiEventCount) override {
        // Transport from the host. The Engine advances its own PPQ from bpm/transport, so pass those
        // two only (pushing host ppq too would double-count).
        double bpm = 120.0;
        const TimePosition& tp = getTimePosition();
        if (tp.bbt.valid) bpm = tp.bbt.beatsPerMinute;
        const bool playing = tp.playing;

        // Host MIDI in → stage directly on the audio thread (short messages only; SysEx deferred).
        for (uint32_t i = 0; i < midiEventCount; ++i) {
            const MidiEvent& e = midiEvents[i];
            if (e.size >= 1 && e.size <= 4) service_.stageMidiRaw(e.data, e.size);
        }

        float* outL = outputs[0];
        float* outR = outputs[1];
        service_.pluginProcessBlock(bpm, playing, frames, outL, outR);

        // Kernel MIDI-out → the DAW.
        for (const auto& mo : service_.pluginMidiOut()) {
            if (mo.data.empty() || mo.data.size() > MidiEvent::kDataSize) continue;
            MidiEvent ev{};
            ev.frame = mo.frame;
            ev.size  = static_cast<uint32_t>(mo.data.size());
            for (std::size_t j = 0; j < mo.data.size(); ++j) ev.data[j] = mo.data[j];
            writeMidiEvent(ev);
        }
        service_.pluginClearMidiOut();

        // Master gain (post-render; the Engine has no master-gain stage).
        const float lin = std::pow(10.0f, gainDb_ / 20.0f);
        if (lin != 1.0f)
            for (uint32_t i = 0; i < frames; ++i) { outL[i] *= lin; outR[i] *= lin; }
    }

private:
    // Bring up the control-plane runtime: bind __rpcSend, eval the bundle, autoload.
    void bootControlPlane() {
        if (!host_.init()) { d_stderr("[greenfield] TjsHostRuntime init failed"); return; }
        JSContext* ctx = host_.context();

        transport_ = std::make_unique<rpcpp::QuickJSTransport>(ctx, [](JSContext*, JSValue) {});
        server_    = std::make_unique<GreenfieldRpcServer>(service_, *transport_, rpcpp::QuickJSCodec{ctx});
        registerBackendRpcMethods(*server_);

        // globalThis[Symbol.for("plugin")] = { __rpcSend } — the namespace realBackend.ts targets.
        JSValue global = JS_GetGlobalObject(ctx);
        JSValue sym    = JS_NewSymbol(ctx, "plugin", /*is_global*/ 1);
        JSAtom atom    = JS_ValueToAtom(ctx, sym);
        JSValue ns     = JS_NewObjectProto(ctx, JS_NULL);
        GreenfieldRpcServer* srv = server_.get();
        host_.bindRpcSend(ns, [srv](JSContext* sctx, JSValueConst req) -> JSValue {
            auto out = srv->processMessage(req);
            if (!out) return JS_NULL;
            return out->materialize(sctx);
        });
        JS_DefinePropertyValue(ctx, global, atom, ns, JS_PROP_C_W_E);
        JS_FreeAtom(ctx, atom);
        JS_FreeValue(ctx, sym);
        JS_FreeValue(ctx, global);

        // Systems bake the sample rate at construct — set it BEFORE the bundle composes / autoloads.
        service_.setSampleRate(getSampleRate());

        // Eval the control-plane bundle (composes stores + kernel, defines the __rp_* globals), then
        // pump until it signals ready (the composition is synchronous; a bounded pump covers module eval).
        host_.evalModuleBytecode(gfcp_cp_bundle, gfcp_cp_bundle_size);
        for (int i = 0; i < 1000 && !readReady(); ++i) host_.pump();
        jsReady_ = readReady();
        if (!jsReady_) { d_stderr("[greenfield] control plane not ready"); return; }

        // Headless seed: reaper -renderproject sets RETROPLUG_AUTOLOAD_PROJECT to a .rplg path.
        if (const char* autoload = std::getenv("RETROPLUG_AUTOLOAD_PROJECT")) {
            const std::string ok = callGlobal("__rp_loadProjectPath", autoload);
            d_stderr("[greenfield] autoload %s -> %s", autoload, ok.empty() ? "false/void" : ok.c_str());
        }
    }

    bool readReady() {
        JSContext* ctx = host_.context();
        JSValue global = JS_GetGlobalObject(ctx);
        JSValue v      = JS_GetPropertyStr(ctx, global, "__rp_ready");
        const bool r   = JS_ToBool(ctx, v) > 0;
        JS_FreeValue(ctx, v);
        JS_FreeValue(ctx, global);
        return r;
    }

    // Call globalThis[name](arg?) on the control-plane context (main thread only). Returns the string
    // result, or "" for a non-string / missing fn / exception.
    std::string callGlobal(const char* name, const char* arg) {
        JSContext* ctx = host_.context();
        JSValue global = JS_GetGlobalObject(ctx);
        JSValue fn     = JS_GetPropertyStr(ctx, global, name);
        std::string out;
        if (JS_IsFunction(ctx, fn)) {
            JSValue argv[1];
            int argc = 0;
            if (arg != nullptr) { argv[0] = JS_NewString(ctx, arg); argc = 1; }
            JSValue ret = JS_Call(ctx, fn, global, argc, argc ? argv : nullptr);
            if (argc) JS_FreeValue(ctx, argv[0]);
            if (JS_IsException(ret)) {
                JSValue exc  = JS_GetException(ctx);
                const char* s = JS_ToCString(ctx, exc);
                d_stderr("[greenfield] %s threw: %s", name, s ? s : "?");
                if (s) JS_FreeCString(ctx, s);
                JS_FreeValue(ctx, exc);
            } else if (JS_IsString(ret)) {
                const char* s = JS_ToCString(ctx, ret);
                if (s) { out = s; JS_FreeCString(ctx, s); }
            }
            JS_FreeValue(ctx, ret);
        }
        JS_FreeValue(ctx, fn);
        JS_FreeValue(ctx, global);
        return out;
    }
};

Plugin* createPlugin() { return new PluginGreenfieldDSP(); }

END_NAMESPACE_DISTRHO
