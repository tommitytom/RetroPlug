// The DPF plugin (DSP-first, UI-less). It hosts the SAME control-plane runtime the test
// host does — TjsHostRuntime + the backend service graph + the __rpcSend bridge on Symbol.for("plugin") — evals
// the embedded control-plane bundle (which composes the stores + DSP kernel and defines the __rp_*
// globals), and drives the Engine per audio block from DPF's run(). No editor: get/setState and the
// RETROPLUG_AUTOLOAD_PROJECT hook go through the JS project globals (base64 done in JS).
#include "DistrhoPlugin.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include "dpfjs/host/TjsHostRuntime.hpp"  // shared txiki/QuickJS host (+ tjs.h/quickjs.h)

#include "PluginShared.hpp"     // SharedDSP handoff to the editor

#include "host/engine/Engine.hpp"
#include "host/engine/EngineInvoker.hpp"
#include "Version.hpp"                    // single source of truth for the plugin version
#include "host/rpc/BackendRpcRegistration.hpp"
#include "system/CoreBackends.hpp"
#include "system/SystemFactory.hpp"
#include "TypedRpcServer.h"
#include "codecs/QuickJSCodec.h"
#include "transports/QuickJSTransport.h"

// The embedded control-plane bundle (bytecode) — build/native/cp-bundle_data.c, rp_ prefix.
extern "C" {
extern const std::uint8_t  rp_cp_bundle[];
extern const std::uint32_t rp_cp_bundle_size;
}

START_NAMESPACE_DISTRHO

using PluginRpcServer = rpcpp::TypedRpcServer<rpcpp::Empty, rpcpp::QuickJSCodec>;

class PluginDSP : public Plugin {
    // Control-plane runtime (main-thread only): the txiki context + the backend service graph it drives
    // over __rpcSend. The plugin owns the Engine and drives it per audio block from run() directly (it
    // never routes audio through RPC), so it holds engine_ + the ONE invoker plus the host + emulator
    // services it mounts. Declaration order is load-bearing.
    TjsHostRuntime host_;
    Engine           engine_;
    SystemFactory    factory_;
    QueuedInvoker    invoker_{engine_, engine_.registry()};
    HostRpcService   hostSvc_;
    EngineRpcService engineSvc_{engine_, factory_, invoker_};
    std::unique_ptr<rpcpp::QuickJSTransport> transport_;
    std::unique_ptr<PluginRpcServer>     server_;
    bool jsReady_ = false;

    float gainDb_ = 0.0f;

public:
    // In-process handoff to the editor: exposes host_ so a DPF UI can attach its LVGL display to the
    // control-plane context (where __rpcSend is already bound). Public so getSharedDSP reaches it.
    SharedDSP shared_{};

    PluginDSP() : Plugin(1 /*params*/, 0 /*programs*/, 1 /*states*/) {
        bootControlPlane();
    }

protected:
    // --- information ---
    const char* getLabel()       const noexcept override { return "RetroPlug"; }
    const char* getDescription() const          override { return "Multi-system retro emulator host"; }
    const char* getMaker()       const noexcept override { return "tommitytom"; }
    const char* getLicense()     const noexcept override { return "MIT"; }
    uint32_t    getVersion()     const noexcept override { return d_version(RETROPLUG_VERSION_MAJOR, RETROPLUG_VERSION_MINOR, RETROPLUG_VERSION_MICRO); }
    int64_t     getUniqueId()    const noexcept override { return d_cconst('R', 'P', 'g', 'f'); }

    // --- audio ports: name the 8 outputs as four stereo pairs (out_1..4) + tag each with a port
    // group so DAWs show Out 1..4 stereo pairs. Systems route to a pair per audioRouting. (Mirrors
    // the legacy plugin so tooling that links `out_N_l/r` works the same.) ---
    void initAudioPort(bool input, uint32_t index, AudioPort& port) override {
        if (input) { Plugin::initAudioPort(input, index, port); return; }
        const uint32_t pair = index / 2;
        const bool     left = (index % 2) == 0;
        char nameBuf[16];
        char symBuf [16];
        std::snprintf(nameBuf, sizeof(nameBuf), "Out %u%c", pair + 1, left ? 'L' : 'R');
        std::snprintf(symBuf,  sizeof(symBuf),  "out_%u_%c", pair + 1, left ? 'l' : 'r');
        port.hints   = 0;
        port.name    = nameBuf;
        port.symbol  = symBuf;
        port.groupId = pair;
    }

    void initPortGroup(uint32_t groupId, PortGroup& portGroup) override {
        constexpr uint32_t kPairCount = DISTRHO_PLUGIN_NUM_OUTPUTS / 2;
        if (groupId >= kPairCount) return;
        char nameBuf[16];
        char symBuf [16];
        std::snprintf(nameBuf, sizeof(nameBuf), "Out %u", groupId + 1);
        std::snprintf(symBuf,  sizeof(symBuf),  "out_%u", groupId + 1);
        portGroup.name   = nameBuf;
        portGroup.symbol = symBuf;
    }

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
        return String(const_cast<PluginDSP*>(this)->callGlobal("__rp_saveProjectB64", nullptr).c_str());
    }
    void setState(const char* key, const char* value) override {
        if (std::strcmp(key, "project") != 0) return;
        callGlobal("__rp_loadProjectB64", value ? value : "");
        updateLatency();  // the loaded project's sync mode determines the compensable latency
    }

    // --- audio lifecycle: DPF owns the audio thread and calls run(), so the plugin drives the Engine
    // directly here (mirrors AudioDriverRpcService's loop, minus the spawned thread). ---
    void activate() override {
        // Hand the Engine to the DPF audio thread: from here every control-plane edit is push-only,
        // drained by run() each block. (Set BEFORE the host starts calling run(); the quiescent path
        // flushed every push, so the command ring is empty at this handoff.) Then report PDC latency.
        invoker_.setAudioThreadOwns(true);
        updateLatency();
    }
    void deactivate() override {
        // DPF guarantees no run() during/after deactivate, so the ring has a single accessor again. Take
        // the Engine back, apply any commands the last block didn't drain (no lost mutation), and reclaim
        // cores the audio thread released just before stopping (freeing each snapshot slot).
        invoker_.setAudioThreadOwns(false);
        invoker_.drainInto(engine_);
        invoker_.reclaimReleased();
    }
    void sampleRateChanged(double newSampleRate) override { engine_.setSampleRate(newSampleRate); }

    // --- the per-block loop (replaces AudioDriverRpcService::audioLoop) ---
    void run(const float**, float** outputs, uint32_t frames,
             const MidiEvent* midiEvents, uint32_t midiEventCount) override {
        // Transport from the host. The Engine advances its own PPQ from bpm/transport, so pass those
        // two only (pushing host ppq too would double-count).
        double bpm = 120.0;
        const TimePosition& tp = getTimePosition();
        if (tp.bbt.valid) bpm = tp.bbt.beatsPerMinute;
        const bool playing = tp.playing;

        // Host MIDI in → stage directly on the audio thread (bypasses the ring + its 4-byte cap; safe
        // because run() owns the Engine while active). Short messages only; SysEx deferred.
        for (uint32_t i = 0; i < midiEventCount; ++i) {
            const MidiEvent& e = midiEvents[i];
            if (e.size >= 1 && e.size <= 4)
                engine_.stageMidi(std::vector<std::uint8_t>(e.data, e.data + e.size));
        }

        // One audio block: drain control-thread structural edits on the audio thread → set transport
        // (direct — we're the audio thread) → render into the output channels (the plugin's 4 stereo
        // pairs; routed per audioRouting by the Engine's MultiOutRouter).
        invoker_.drainInto(engine_);
        engine_.setBpm(bpm);
        engine_.setTransport(playing);
        engine_.processBlock(frames, outputs, DISTRHO_PLUGIN_NUM_OUTPUTS);

        // Kernel MIDI-out → the DAW (drain the block just rendered, then clear).
        for (const auto& mo : engine_.midiOut()) {
            if (mo.data.empty() || mo.data.size() > MidiEvent::kDataSize) continue;
            MidiEvent ev{};
            ev.frame = mo.frame;
            ev.size  = static_cast<uint32_t>(mo.data.size());
            for (std::size_t j = 0; j < mo.data.size(); ++j) ev.data[j] = mo.data[j];
            writeMidiEvent(ev);
        }
        engine_.clearMidiOut();

        // Master gain across every output channel (post-render; the Engine has no master-gain stage).
        const float lin = std::pow(10.0f, gainDb_ / 20.0f);
        if (lin != 1.0f)
            for (uint32_t c = 0; c < DISTRHO_PLUGIN_NUM_OUTPUTS; ++c)
                for (uint32_t i = 0; i < frames; ++i) outputs[c][i] *= lin;
    }

private:
    // Bring up the control-plane runtime: bind __rpcSend, eval the bundle, autoload.
    void bootControlPlane() {
        if (!host_.init()) { d_stderr("[retroplug] TjsHostRuntime init failed"); return; }
        JSContext* ctx = host_.context();

        registerCoreBackends(factory_);  // sameboy + mesen — before the bundle autoloads any system

        transport_ = std::make_unique<rpcpp::QuickJSTransport>(ctx, [](JSContext*, JSValue) {});
        server_    = std::make_unique<PluginRpcServer>(*transport_, rpcpp::QuickJSCodec{ctx});
        // The plugin control plane composes the stores (fs + emulator) and loads the DSP kernel. It
        // drives audio per block in C++ (never the renderAudio harness), never debugs the live core,
        // and never spawns the background audio-driver thread — so those facets are NOT on this channel.
        // The editor reuses this same context/channel, so it inherits exactly this surface.
        registerHostRpc(*server_, hostSvc_);
        registerEmulatorRpc(*server_, engineSvc_);
        registerDspKernelRpc(*server_, engineSvc_);
        server_->addDiscoveryMethod();

        // globalThis[Symbol.for("plugin")] = { __rpcSend } — the namespace realBackend.ts targets.
        JSValue global = JS_GetGlobalObject(ctx);
        JSValue sym    = JS_NewSymbol(ctx, "plugin", /*is_global*/ 1);
        JSAtom atom    = JS_ValueToAtom(ctx, sym);
        JSValue ns     = JS_NewObjectProto(ctx, JS_NULL);
        PluginRpcServer* srv = server_.get();
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
        engine_.setSampleRate(getSampleRate());

        // Eval the control-plane bundle (composes stores + kernel, defines the __rp_* globals), then
        // pump until it signals ready (the composition is synchronous; a bounded pump covers module eval).
        host_.evalModuleBytecode(rp_cp_bundle, rp_cp_bundle_size);
        for (int i = 0; i < 1000 && !readReady(); ++i) host_.pump();
        jsReady_ = readReady();
        if (!jsReady_) { d_stderr("[retroplug] control plane not ready"); return; }

        // Publish the host so an editor can attach its LVGL display to this same context.
        shared_.host = &host_;

        // Headless seed: reaper -renderproject sets RETROPLUG_AUTOLOAD_PROJECT to a .rplg path.
        if (const char* autoload = std::getenv("RETROPLUG_AUTOLOAD_PROJECT")) {
            const std::string ok = callGlobal("__rp_loadProjectPath", autoload);
            d_stderr("[retroplug] autoload %s -> %s", autoload, ok.empty() ? "false/void" : ok.c_str());
            updateLatency();  // report the autoloaded project's PDC latency before the host reads it
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
                JSValue stk  = JS_GetPropertyStr(ctx, exc, "stack");
                const char* st = JS_IsUndefined(stk) ? nullptr : JS_ToCString(ctx, stk);
                d_stderr("[retroplug] %s threw: %s%s%s", name, s ? s : "?",
                         st ? "\n" : "", st ? st : "");
                if (st) JS_FreeCString(ctx, st);
                JS_FreeValue(ctx, stk);
                if (s) JS_FreeCString(ctx, s);
                JS_FreeValue(ctx, exc);
            } else if (JS_IsString(ret)) {
                const char* s = JS_ToCString(ctx, ret);
                if (s) { out = s; JS_FreeCString(ctx, s); }
            } else if (JS_IsBool(ret)) {
                // Stringify booleans so callers that return a bool (e.g. __rp_loadProjectPath) get an
                // unambiguous "true"/"false" in diagnostics instead of the empty "false/void" default.
                out = JS_ToBool(ctx, ret) > 0 ? "true" : "false";
            }
            JS_FreeValue(ctx, ret);
        }
        JS_FreeValue(ctx, fn);
        JS_FreeValue(ctx, global);
        return out;
    }

    // Report the loaded project's compensable latency to the host (PDC). LSDj in a host-clocked sync mode
    // locks a fixed distance behind the DAW clock; the control plane returns that latency in ms, which we
    // convert to frames at the live sample rate. Called after each load + on activate (hosts read latency
    // around activation); a change re-runs setLatency, which DPF forwards to the host where supported.
    void updateLatency() {
        const std::string ms = callGlobal("__rp_syncLatencyMs", nullptr);
        const double latMs = ms.empty() ? 0.0 : std::atof(ms.c_str());
        setLatency(static_cast<uint32_t>(latMs * getSampleRate() / 1000.0 + 0.5));
    }
};

Plugin* createPlugin() { return new PluginDSP(); }

// In-process handoff: the editor calls this with getPluginInstancePointer() to reach the shared host.
SharedDSP* getSharedDSP(void* pluginPtr) {
    return &static_cast<PluginDSP*>(pluginPtr)->shared_;
}

END_NAMESPACE_DISTRHO
