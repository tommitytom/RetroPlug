// The DPF plugin's DSP half. It hosts the SAME control-plane runtime the test host does —
// TjsHostRuntime + the backend service graph + the __rpcSend bridge on Symbol.for("plugin") — evals
// the embedded control-plane bundle (which composes the stores and defines the __rp_* globals),
// and drives the Engine per audio block from DPF's run(). The editor is PluginUI.cpp
// (DISTRHO_PLUGIN_HAS_UI 1) and shares this graph rather than composing its own; get/setState and the
// RETROPLUG_AUTOLOAD_PROJECT hook go through the JS project globals (base64 done in JS).
#include "DistrhoPlugin.hpp"

#include <array>
#include <chrono>
#include <thread>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <rfl/json.hpp>       // the __rp_parameterMapJson payload
#include <rfl/DefaultIfMissing.hpp>

#include "host/js/SharedModules.hpp"   // registerSharedModules (the app's shared module table)
#include "host/packs/ProcessPacks.hpp"
#include "host/js/PackModules.hpp"
#include "dpfjs/host/TjsHostRuntime.hpp"  // shared txiki/QuickJS host (+ tjs.h/quickjs.h)

#include "PluginShared.hpp"     // SharedDSP handoff to the editor
#include "LastGoodState.hpp"   // keeps a failed project serialization from reporting "empty"
#include "LifecycleHandoff.hpp" // the lifecycle policy: a foreign size/rate change parks instead of racing here

#include "host/engine/Engine.hpp"
#include "host/engine/EngineInvoker.hpp"
#include "Version.hpp"                    // single source of truth for the plugin version
#include "host/rpc/BackendRpcRegistration.hpp"
#include "host/rpc/NativeFileWatcher.hpp"  // the FileWatcher the host hands HostRpcService (retroplug-file-watcher)
#include "host/devices/ProviderHost.hpp"  // the rp_device_host this product starts its providers with
#include "rp_product_providers.hpp"       // generated, in this product's own include dir: its providers (the N8's)
#include "TypedRpcServer.h"
#include "codecs/QuickJSCodec.h"
#include "transports/QuickJSTransport.h"
#include "host/QuickJsGlobals.hpp"
#include "host/HostServices.hpp"
#include "host/IdleReclaimProbe.hpp"      // the editor idle's smoke hook (RETROPLUG_IDLE_RECLAIM_TEST)

// The embedded control-plane bundle (bytecode) — build/native/cp-bundle_data.c, rp_ prefix.
extern "C" {
extern const std::uint8_t  rp_cp_bundle[];
extern const std::uint32_t rp_cp_bundle_size;
extern const rp_js_module rp_app_modules[];
extern const std::uint32_t rp_app_modules_count;
}

START_NAMESPACE_DISTRHO

using PluginRpcServer = rpcpp::TypedRpcServer<rpcpp::Empty, rpcpp::QuickJSCodec>;

// --- the DAW parameter pool (spec/12-dynamic-parameters.md) ---
// One master gain plus a fixed grid of MIDI-CC slots. The COUNT, ORDER, SYMBOLS, RANGES and hints are
// frozen for the instance's life: DPF refuses to move them on a live plugin (CLAP would need a
// deactivate, VST3 a component reload), a DAW project's automation is bound to the slot, and DPF's own
// state save/restore is keyed by symbol. Only the names and the hidden flag change per loaded ROM.
// kMaxSystems matches the four stereo output pairs, since a system routes to one.
// kCcSlotsPerSystem twins CC_SLOTS_PER_SYSTEM in packages/retroplug/src/parameterMap.ts and is sized
// for the largest ROM in the set (BlipToaster's VRC7 build: 12 channels, ~130 lanes) with headroom.
// CHANGING IT SHIFTS HOST AUTOMATION on systems 2-4, whose pool indices are `system * this`, so it is
// deliberately generous rather than tight - unclaimed slots are hidden and cost a float each.
static constexpr std::uint32_t kMaxSystems       = 4;
static constexpr std::uint32_t kCcSlotsPerSystem = 160;
static constexpr std::uint32_t kCcSlotCount      = kMaxSystems * kCcSlotsPerSystem;
static constexpr std::uint32_t kParamGain        = 0;
static constexpr std::uint32_t kParamCcBase      = 1;
static constexpr std::uint32_t kParameterCount   = kParamCcBase + kCcSlotCount;

// One entry of __rp_parameterMapJson. Tolerant-read (DefaultIfMissing) like the other JSON that
// crosses into native: an older/newer control plane must not fail the whole map.
struct CcSlotDto {
    std::uint32_t slot      = 0;   // pool index, 0 .. kCcSlotCount-1
    std::uint32_t cc        = 0;   // MIDI CC number
    std::uint32_t channel   = 0;   // PRE-routing MIDI channel (the project's routing then delivers it)
    std::string   name;
    std::string   shortName;
};

class PluginDSP : public Plugin, private rp::plugin::HandoffHost {
    // Control-plane runtime (main-thread only): the txiki context + the backend service graph it drives
    // over __rpcSend. The plugin owns the Engine and drives it per audio block from run() directly (it
    // never routes audio through RPC), so it holds svc_.engine + the ONE invoker plus the host + emulator
    // services it mounts. Declaration order is load-bearing.
    // The launch packs, loaded before the Engine below is built: a pack's kind items' runtimes join an Engine only at its
    // construction (Engine's constructor, docs/downloadable-packs-m2f-plan.md). First, so it is constructed first.
    struct LaunchPacksFirst {
        LaunchPacksFirst() { rp::host::packs::ensureLaunchPacks(); }
    } launchPacks_;
    rp::host::PackHostRuntime host_;
    // The three facets below are ALL this channel mounts — no debug, no audio-driver. See host/
    // HostServices.hpp for why those two are not members here rather than merely unmounted.
    HostServices     svc_;
    // RETROPLUG_IDLE_RECLAIM_TEST: inert unless `tools/run-standalone.sh` arms it. Holds the editor's idle (the only
    // per-frame hook the plugin has) to the idle tick it owes the engine, which no in-process test can reach.
    retroplug::IdleReclaimProbe idleProbe_;
    // What a host's buffer-size / sample-rate callback may do, and what run() does while one is pending: see
    // LifecycleHandoff.hpp. Built on the thread DPF builds the plugin on, which is the thread it also runs the first
    // activate() on. mutable because getState (DPF's signature is const) is one of the seams that applies a pending
    // park, for a host that never calls the editor's idle.
    mutable rp::plugin::LifecycleHandoff handoff_{*this, std::this_thread::get_id(), Engine::kDefaultMaxBlockFrames};
    // The device providers of the packages this product lists (rp_product_providers.hpp, generated): the N8 pack's,
    // the same one the SDL standalone starts, which owns the physical Everdrive N8 Pro's streaming link, its n8.cfg in
    // svc_.host's config dir and the cart link the engine hands rp.n8.nes by name. Nothing in run() pushes at it: the
    // graph's node does, on the audio thread. Settings > N8 drives it through the pack's facet, mounted on this
    // channel. The plugin owns no control surface, so the surface name answers null and the surface nodes run on a
    // stub that is never connected: every message is SysEx, which the DAW MIDI seam caps at 4 bytes (the SDL
    // standalone's comment says the same where it binds its Launchpad).
    rp::host::ProviderHost deviceHost_{svc_.engine, svc_.host.configDir()};
    rp::Providers          providers_;
    std::unique_ptr<rpcpp::QuickJSTransport> transport_;
    std::unique_ptr<PluginRpcServer>     server_;
    // Did the control plane come up AND signal __rp_ready? Gates callGlobal, so a runtime that failed
    // to init, or a bundle that threw during eval, degrades to "" everywhere instead of being called
    // into half-built. NOT consulted by readReady, which is the pump loop that decides this value.
    bool jsReady_ = false;

    // Last project chunk that serialized successfully. getState is const (DPF's signature), hence
    // mutable rather than the const_cast this used to need at the call.
    mutable retroplug::LastGoodState lastGoodProject_;

    float gainDb_ = 0.0f;

    // --- CC slot state ---
    // Names are main-thread only (initParameter runs from the DPF ctor and from reinitParameters, both
    // main thread). The (claimed, channel, cc) triple is what the audio thread needs, so it lives apart
    // as one relaxed atomic per slot: 0 = unclaimed, else 0x8000 | (channel << 8) | cc. The audio thread
    // only reads it, and a map landing a block late just means one block of stale routing.
    struct CcSlotName { std::string name, shortName; bool claimed = false; };
    std::array<CcSlotName, kCcSlotCount>          ccName_{};
    std::array<std::atomic<std::uint16_t>, kCcSlotCount> ccBinding_{};
    std::array<std::atomic<float>, kCcSlotCount>  ccValue_{};
    std::array<float, kCcSlotCount>               ccSent_{};   // audio thread only
    // Set when the map moves, consumed by the next block. A re-labelled slot now drives a DIFFERENT CC,
    // so the value the DAW is showing has never been sent to the newly-loaded ROM; without this the
    // lane and the ROM disagree until the user happens to touch it.
    std::atomic<bool>                             ccResendAll_{false};
    std::string                                   ccMapJson_;  // last map seen, so a poll is a no-op

public:
    // In-process handoff to the editor: exposes host_ so a DPF UI can attach its LVGL display to the
    // control-plane context (where __rpcSend is already bound). Public so getSharedDSP reaches it.
    SharedDSP shared_{};

    PluginDSP() : Plugin(kParameterCount, 0 /*programs*/, 1 /*states*/) {
        for (auto& b : ccBinding_) b.store(0, std::memory_order_relaxed);
        for (auto& v : ccValue_)   v.store(0.0f, std::memory_order_relaxed);
        ccSent_.fill(0.0f);
        // The providers before the first graph the control plane can publish, so a node of that graph binds the links
        // they install (a refusal is said by the provider, and leaves rp.n8.nes on its stub).
        providers_ = rp_start_providers(deviceHost_.get());
        bootControlPlane();
        // The autoload in bootControlPlane may already have put a ROM in place, so name the pool from
        // it before DPF reads the parameters. (This is the construct-time pass; the host has no
        // parameter info yet, so it only fills ccName_ - there is nothing to notify.)
        updateParameterMap();
    }

    // svc_ (the engine) is declared above the providers, so it is destroyed last, after them. Reclaim the plans first,
    // in this body, while the providers are still running: a node may use its handle in destroy (a farewell, a flush,
    // a read), and it must reach a live link. Then stop them, which takes their links back. The engine has no plan
    // left to destroy by the time the members go.
    ~PluginDSP() override {
        svc_.engine.clearGraph();
        rp_stop_providers(providers_);
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
        // Sized for any uint32_t pair number: GCC's -Wformat-truncation counts that, not the four pairs.
        char nameBuf[24];
        char symBuf [24];
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

    // --- parameters: master gain + the per-system MIDI-CC slot pool ---
    // Re-run for every slot by DPF whenever the loaded ROM changes what the slots mean, so everything
    // here except the name / short name / hidden bit MUST be a constant of the index (DPF refuses a
    // structural change and warns).
    void initParameter(uint32_t index, Parameter& p) override {
        if (index == kParamGain) {
            p.symbol = "gain"; p.name = "Master Gain"; p.shortName = "Gain"; p.unit = "dB";
            p.ranges.min = -90.0f; p.ranges.max = 12.0f; p.ranges.def = 0.0f;
            p.hints = kParameterIsAutomatable;
            return;
        }
        if (index < kParamCcBase || index >= kParameterCount) return;

        const std::uint32_t slot = index - kParamCcBase;
        const std::uint32_t sys  = slot / kCcSlotsPerSystem;
        const std::uint32_t n    = slot % kCcSlotsPerSystem;
        char buf[80];   // fits the longest lane name, e.g. "1: MMC5 Pulse 1 Sustain / Length Flag"

        std::snprintf(buf, sizeof(buf), "sys%u_cc%u", sys + 1, n + 1);
        p.symbol = buf;

        const CcSlotName& nm = ccName_[slot];
        if (nm.claimed) {
            // "1: PU1 Pulse Width" - the system number keeps four systems' lanes apart in the host list
            std::snprintf(buf, sizeof(buf), "%u: %s", sys + 1, nm.name.c_str());
            p.name      = buf;
            p.shortName = nm.shortName.c_str();
        } else {
            std::snprintf(buf, sizeof(buf), "%u: CC %u", sys + 1, n + 1);
            p.name = buf;
            std::snprintf(buf, sizeof(buf), "CC %u", n + 1);
            p.shortName = buf;
        }

        p.ranges.min = 0.0f; p.ranges.max = 127.0f; p.ranges.def = 0.0f;
        p.hints = kParameterIsAutomatable | kParameterIsInteger | (nm.claimed ? 0x0 : kParameterIsHidden);
    }

    float getParameterValue(uint32_t index) const override {
        if (index == kParamGain) return gainDb_;
        if (index >= kParamCcBase && index < kParameterCount)
            return ccValue_[index - kParamCcBase].load(std::memory_order_relaxed);
        return 0.0f;
    }

    // Called from the host's main thread outside processing and from the audio thread during it, so it
    // only ever stores. run() turns a moved value into the actual CC (the block's parameter changes have
    // all landed by then).
    void setParameterValue(uint32_t index, float value) override {
        if (index == kParamGain) { gainDb_ = value; return; }
        if (index >= kParamCcBase && index < kParameterCount)
            ccValue_[index - kParamCcBase].store(value, std::memory_order_relaxed);
    }

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
        // A host that changed the rate off its main thread and never called the editor's idle left a park pending, and
        // everything below reads the script context: apply it here, on this (the control) thread. This is the tick that
        // never SUSPENDS (LifecycleHandoff::stateTick): a save must not leave the plugin parked, because the only thing
        // that could hand the engine back afterwards is the next save. What it does apply calls updateControlPlane - a
        // latency report, which DPF forwards to the host - from inside the host's own getState. A rate change moves
        // the latency, so this is where it is reported; the map rarely moves with it and a no-op costs one compare.
        handoff_.stateTick();
        const std::string& out =
            lastGoodProject_.offer(const_cast<PluginDSP*>(this)->callGlobal("__rp_saveProjectB64", nullptr));
        // Log the first substitution only: the host calls this on every save AND on editor connect, so
        // a persistent failure would otherwise flood stderr with the same line.
        if (lastGoodProject_.fallbacks() == 1)
            d_stderr("[retroplug] project serialization failed; reporting the last good state to the host");
        return String(out.c_str());
    }
    void setState(const char* key, const char* value) override {
        if (std::strcmp(key, "project") != 0) return;
        handoff_.stateTick();    // the same seam getState is, before a load reaches the script context
        callGlobal("__rp_loadProjectB64", value ? value : "");
        updateLatency();        // the loaded project's sync mode determines the compensable latency
        updateParameterMap();   // ...and its ROMs determine what the DAW's CC slots are called
    }

    // --- audio lifecycle: DPF owns the audio thread and calls run(), so the plugin drives the Engine
    // directly here (mirrors AudioDriverRpcService's loop, minus the spawned thread). ---
    //
    // The four overrides below are the FORMAT's callbacks, and only some formats run them on the control thread. CLAP,
    // VST3 and AU do. JACK does not: a buffer-size or sample-rate change runs deactivate(), the change and activate()
    // from its notification callback (DistrhoPluginJACK.cpp's jackBufferSizeCallback / jackSampleRateCallback ->
    // PluginExporter::setBufferSize(n, true)), and VST2 runs the same sequence from whichever thread its host
    // dispatches the block-size and sample-rate opcodes on. Doing the engine work there races the editor's per-frame
    // reclaim and its edits, and updateLatency / updateParameterMap would call into the QuickJS context this thread
    // pumps every frame (spec/02-native-host.md's plugin lifecycle thread rule; PipeWire changes its quantum whenever
    // any client asks for a different latency, so it is routine). So all four only RECORD what the format asked for and
    // hand it to the policy: a size the plan already holds costs nothing, and a rate change or an over-plan size parks
    // run() until this thread's idle tick (the editor's per-frame reclaimReleased) applies it quiescently - unless this
    // thread makes it with no engine given (a control-thread deactivate before it), when there is nothing to wait for
    // and it applies here.
    // LifecycleHandoff.hpp is the whole argument, including what a format's activate and deactivate mean for who owns
    // the engine.
    void activate() override { handoff_.activate(getBufferSize()); }
    void deactivate() override { handoff_.deactivate(); }
    void bufferSizeChanged(uint32_t newBufferSize) override { handoff_.bufferSizeChanged(newBufferSize); }
    void sampleRateChanged(double newSampleRate) override { handoff_.sampleRateChanged(newSampleRate); }

    // --- the per-block loop (replaces AudioDriverRpcService::audioLoop) ---
    void run(const float**, float** outputs, uint32_t frames,
             const MidiEvent* midiEvents, uint32_t midiEventCount) override {
        // A park comes before anything that reaches the engine or the script context: this block is silence, its MIDI
        // is held for the first resumed one, and nothing here allocates or takes a lock the audio thread would.
        if (!handoff_.beginBlock()) {
            holdMidi(midiEvents, midiEventCount);
            for (uint32_t c = 0; c < DISTRHO_PLUGIN_NUM_OUTPUTS; ++c)
                std::memset(outputs[c], 0, frames * sizeof(float));
            return;
        }

        // What a park held, at frame 0 of this first resumed block, before the block's own messages: a note-off the
        // host sent while parked must reach the cart before anything struck after it.
        handoff_.replayHeld([this](const std::uint8_t* bytes, std::uint32_t size) {
            svc_.engine.stageMidi(0, std::span<const std::uint8_t>(bytes, size));
        });

        // Transport from the host. The Engine advances its own PPQ from bpm/transport, so pass those
        // two only (pushing host ppq too would double-count).
        double bpm = 120.0;
        const TimePosition& tp = getTimePosition();
        if (tp.bbt.valid) bpm = tp.bbt.beatsPerMinute;
        const bool playing = tp.playing;

        // Host MIDI in → stage directly on the audio thread (bypasses the ring; safe because run() owns
        // the Engine while active), copied into the Engine's staging with no allocation. Every size: DPF
        // keeps a message longer than the inline data[4] (a SysEx) in dataExt, and a system node hands
        // one to the core's raw byte path (the NES N8 FIFO), so a DAW can upload a wave to the N163 build
        // the same way the CLI does.
        //
        // A connected physical N8 is fed from the graph, not from here: an rp.n8.nes node, placed by a system's
        // "drive the real cart" setting, takes the same protocol-node MIDI and bytes its emulated NES's core does
        // (buildGraph fans them out), so the cart hears what the emulated FIFO hears - after the project's routing,
        // which may send it none of a message or rewrite its channel. A raw copy here as well would land twice, the
        // second one unrouted.
        for (uint32_t i = 0; i < midiEventCount; ++i) {
            const MidiEvent& e = midiEvents[i];
            if (e.size < 1) continue;
            const uint8_t* bytes = e.size <= MidiEvent::kDataSize ? e.data : e.dataExt;
            if (bytes == nullptr) continue;
            svc_.engine.stageMidi(e.frame, std::span<const std::uint8_t>(bytes, e.size));
        }

        // DAW automation → MIDI CC. A claimed slot emits at the head of the block whenever its value
        // moved: DPF has already applied every parameter change for this block by now, so frame 0 is
        // where they all landed. The bytes take the path a host CC does - the emulated core, routed by the project's
        // MIDI routing (which is why the map carries a PRE-routing channel) - and a physical N8 hears the same
        // through its node's fan-out, so automation behaves the same on hardware.
        const bool resendAll = ccResendAll_.exchange(false, std::memory_order_relaxed);
        for (std::uint32_t slot = 0; slot < kCcSlotCount; ++slot) {
            const std::uint16_t binding = ccBinding_[slot].load(std::memory_order_relaxed);
            if (binding == 0) continue;
            const float v = ccValue_[slot].load(std::memory_order_relaxed);
            if (v == ccSent_[slot] && !resendAll) continue;
            ccSent_[slot] = v;
            int iv = static_cast<int>(v + 0.5f);
            iv = iv < 0 ? 0 : (iv > 127 ? 127 : iv);
            const std::uint8_t cc[3] = {
                static_cast<std::uint8_t>(0xB0 | ((binding >> 8) & 0x0F)),
                static_cast<std::uint8_t>(binding & 0x7F),
                static_cast<std::uint8_t>(iv),
            };
            svc_.engine.stageMidi(0, std::span<const std::uint8_t>(cc));
        }

        // One audio block: drain control-thread edits on the audio thread, set transport (direct: we're
        // the audio thread), render into the output channels (the plugin's 4 stereo pairs, laid out by the
        // graph TS builds for 8 outputs and the project's routing).
        svc_.invoker.drainInto(svc_.engine);
        svc_.engine.setBpm(bpm);
        svc_.engine.setTransport(playing);
        svc_.engine.processBlock(frames, outputs, DISTRHO_PLUGIN_NUM_OUTPUTS);

        // Host MIDI out (LSDj MI.OUT / master sync, the controller's launches to a real Game Boy) → the DAW
        // (drain the block just rendered, then clear).
        for (const auto& mo : svc_.engine.midiOut()) {
            if (mo.data.empty() || mo.data.size() > MidiEvent::kDataSize) continue;
            MidiEvent ev{};
            ev.frame = mo.frame;
            ev.size  = static_cast<uint32_t>(mo.data.size());
            for (std::size_t j = 0; j < mo.data.size(); ++j) ev.data[j] = mo.data[j];
            writeMidiEvent(ev);
        }
        svc_.engine.clearMidiOut();

        // Master gain across every output channel (post-render; the Engine has no master-gain stage).
        const float lin = std::pow(10.0f, gainDb_ / 20.0f);
        if (lin != 1.0f)
            for (uint32_t c = 0; c < DISTRHO_PLUGIN_NUM_OUTPUTS; ++c)
                for (uint32_t i = 0; i < frames; ++i) outputs[c][i] *= lin;
    }

private:
    // --- what the lifecycle policy may do to the engine (rp::plugin::HandoffHost), on the control thread only -------
    void takeEngine() override {
        // DPF guarantees no run() while the engine is taken: flip the ownership, then apply any commands the last block
        // did not drain (no lost mutation).
        svc_.invoker.setAudioThreadOwns(false);
        svc_.invoker.drainInto(svc_.engine);
    }
    std::size_t reclaimReleases() override { return svc_.reclaimOnIdle(); }
    void giveEngine() override { svc_.invoker.setAudioThreadOwns(true); }
    void setMaxBlockFrames(std::uint32_t frames) override { svc_.engine.setMaxBlockFrames(frames); }
    void setSampleRate(double rate) override { svc_.engine.setSampleRate(rate); }
    void updateControlPlane() override {
        updateLatency();
        updateParameterMap();
    }
    void parkOverdue(double seconds) override {
        d_stderr("[retroplug] a lifecycle handoff stayed parked for %.1f s: no idle tick and no state save applied it",
                 seconds);
    }
    void midiDropped(std::uint64_t count) override {
        d_stderr("[retroplug] a parked block dropped %llu MIDI message(s): they did not fit the hold",
                 static_cast<unsigned long long>(count));
    }

    // A message longer than DPF's inline data is not held: DPF keeps it in dataExt, and a SysEx has no place in the
    // policy's fixed buffer. The two sizes must agree, or a message that fits the engine's arena would be dropped by a
    // parked block for no reason (HeldMidi is DPF-free, so nothing there can include MidiEvent).
    static_assert(rp::plugin::HeldMidi::kMaxBytes >= MidiEvent::kDataSize,
                  "a parked block holds what DPF puts inline, or it drops a message it could have kept");

    // Copy a parked block's MIDI into the policy's fixed buffer (LifecycleHandoff.hpp): a message longer than DPF's
    // inline data is not held, and past the buffer a message is dropped and counted rather than allocated for.
    void holdMidi(const MidiEvent* events, uint32_t count) {
        for (uint32_t i = 0; i < count; ++i) {
            const MidiEvent& e = events[i];
            if (e.size < 1) continue;
            const std::uint8_t* bytes = e.size <= MidiEvent::kDataSize ? e.data : e.dataExt;
            if (bytes != nullptr) handoff_.holdMidi(bytes, e.size);
        }
    }

    // Bring up the control-plane runtime: bind __rpcSend, eval the bundle, autoload.
    void bootControlPlane() {
        rp::host::packs::ensureLaunchPacks();  // before any context: a pack's TS halves are read from what it loaded
        if (!host_.init()) { d_stderr("[retroplug] TjsHostRuntime init failed"); return; }
        JSContext* ctx = host_.context();

        transport_ = std::make_unique<rpcpp::QuickJSTransport>(ctx, [](JSContext*, JSValue) {});
        server_    = std::make_unique<PluginRpcServer>(*transport_, rpcpp::QuickJSCodec{ctx});
        // The plugin control plane composes the stores (fs + emulator); the script nodes the graph places
        // are embedded in every Engine. It drives audio per block in C++ (never the renderAudio harness), never debugs
        // the live core, and never spawns the background audio-driver thread, so those facets are NOT on
        // this channel.
        // The editor reuses this same context/channel, so it inherits exactly this surface.
        registerHostRpc(*server_, svc_.host);
        registerEmulatorRpc(*server_, svc_.engineSvc);
        // The facets of the providers started in the constructor: the N8's link half, which Settings > N8 drives.
        rp_register_package_rpc(*server_, providers_);
        server_->addDiscoveryMethod();

        // globalThis[Symbol.for("plugin")] = { __rpcSend } — the namespace realBackend.ts targets.
        PluginRpcServer* srv = server_.get();
        installPluginNamespace(ctx, [&](JSValue ns) {
            host_.bindRpcSend(ns, [srv](JSContext* sctx, JSValueConst req) -> JSValue {
                auto out = srv->processMessage(req);
                if (!out) return JS_NULL;
                return out->materialize(sctx);
            });
        });

        // Systems bake the sample rate at construct — set it BEFORE the bundle composes / autoloads.
        svc_.engine.setSampleRate(getSampleRate());

        // The app's shared modules first (docs/downloadable-packs-m2b-plan.md): the control plane's imports, and later the
        // editor's, resolve to them by name, so the two share one copy of every contract module. A failure is fatal: a
        // retry would append duplicate modules.
        if (const std::string why = rp::host::registerSharedModules(ctx, rp_app_modules, rp_app_modules_count);
            !why.empty()) {
            d_stderr("[retroplug] %s", why.c_str());
            return;
        }
        // The pack loader serves the packs' TS halves by name; it must be in place before the entry evaluates.
        rp::host::installPackLoader(ctx, rp::host::packs::processPacks(), rp_app_modules, rp_app_modules_count);
        // Eval the control-plane bundle (composes the stores, defines the __rp_* globals), then pump until it signals
        // ready: that covers module eval and the packs' app halves (a slow one is waited for, up to the deadline).
        const auto readyDeadline = std::chrono::steady_clock::now() + rp::host::kHostReadyDeadline;
        const bool evaluated = host_.evalModuleBytecode(rp_cp_bundle, rp_cp_bundle_size) == 0;
        // A failed eval (a throw while composing) will never become ready: no deadline to wait out.
        for (int i = 0; evaluated && !readReady(); ++i) {
            if (i >= 1000) {
                if (std::chrono::steady_clock::now() >= readyDeadline) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            host_.pump();
        }
        jsReady_ = readReady();
        if (!jsReady_) { d_stderr("[retroplug] control plane not ready"); return; }
        // The graph's output wiring is built for the plugin's fixed outputs (the control plane owns it).
        if (callGlobal("__rp_setHostOutputs", std::to_string(DISTRHO_PLUGIN_NUM_OUTPUTS).c_str()) != "true")
            d_stderr("[retroplug] the %u-output host shape did not plan", static_cast<unsigned>(DISTRHO_PLUGIN_NUM_OUTPUTS));

        // Publish the host so an editor can attach its LVGL display to this same context.
        shared_.host = &host_;
        shared_.pollParameterMap = [this] { updateParameterMap(); };
        // The editor's per-frame hand-back, through the one idle seam both hosts call (the SDL standalone's loop is the
        // other). The probe rides it so a host that stops calling it goes red in its own smoke.
        shared_.reclaimReleased  = [this] {
            // The idle tick also applies a lifecycle handoff a foreign callback parked for, which is why the policy's
            // tick is the one seam (see LifecycleHandoff.hpp).
            const std::size_t freed = handoff_.idleTick();
            idleProbe_.tick(freed);
            return freed;
        };
        idleProbe_.armFromEnv(svc_.engine);

        // Turn on the native file watcher (config.json + bindings/ recursively; ROMs registered by TS via
        // setWatchedRoms). The TS FileWatcher.pump() drains it from the UI idle loop (__rp_pumpWatcher). The service
        // takes the interface, so this is the one place in the tree that builds an efsw watcher, and only the plugin
        // watches: the test host / CLI hand over none, and drainChangedPaths stays inert.
        svc_.host.enableWatching(std::make_unique<NativeFileWatcher>(svc_.host.configDir()));

        // The providers' persisted state (the physical Everdrive N8 Pro, Settings > N8: n8.cfg, reconnecting a link
        // left enabled). A system whose "drive the real cart" setting is on places an rp.n8.nes node, and what its
        // protocol nodes send the emulated NES's core reaches the cart through the graph's fan-out (buildGraph wires
        // both from the same sources), on the audio thread, in the block it was played - so the plugin drives a real
        // NES from the DAW transport, in lock-step with the emulated core.
        rp_restore_providers(providers_);

        // Headless seed: reaper -renderproject sets RETROPLUG_AUTOLOAD_PROJECT to a .rplg path.
        if (const char* autoload = std::getenv("RETROPLUG_AUTOLOAD_PROJECT")) {
            const std::string ok = callGlobal("__rp_loadProjectPath", autoload);
            d_stderr("[retroplug] autoload %s -> %s", autoload, ok.empty() ? "false/void" : ok.c_str());
            updateLatency();  // report the autoloaded project's PDC latency before the host reads it
        }
    }

    bool readReady() { return host_.isInitialized() && jsReadReady(host_.context()); }

    // Call globalThis[name](arg?) on the control-plane context (main thread only). Returns the string
    // result, or "" for a non-string / missing fn / exception — or for a runtime that never came up.
    //
    // That last case is why the guard is HERE rather than at each caller: bootControlPlane returns
    // early when host_.init() fails, leaving a null context, and the constructor goes straight on to
    // updateParameterMap(). Every JS-touching method (getState/setState/activate/updateLatency/
    // updateParameterMap) funnels through this function and readReady, so guarding the two of them
    // covers the whole surface — and a DAW scanning plugins gets a silent no-op instead of a crash
    // inside our constructor. "" is already the answer every caller handles.
    std::string callGlobal(const char* name, const char* arg) {
        // `jsReady_` is the half SDL has no analogue for: a DAW scan must degrade rather than exit, so
        // the plugin refuses to call into a control plane that never signalled ready. See 48c8a895.
        JsGlobals g{ host_.isInitialized() ? host_.context() : nullptr, jsReady_,
                     [](const char* n, const char* detail) { d_stderr("[retroplug] %s threw: %s", n, detail); } };
        return jsCallGlobal(g, name, arg);
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

public:
    // Re-label the CC slot pool from whatever the control plane says the loaded ROMs expose, and ask the
    // host to re-read the parameter info if anything moved (spec/12-dynamic-parameters.md). The JSON is
    // compared as a string first, so the common "nothing changed" poll costs one JS call and a compare.
    // Main thread only — it writes the name strings initParameter reads, and DPF notifies the host inline.
    // Public so the editor can drive it from uiIdle: a ROM loaded through the UI never reaches setState.
    void updateParameterMap() {
        const std::string json = callGlobal("__rp_parameterMapJson", nullptr);
        if (json == ccMapJson_) return;
        ccMapJson_ = json;

        for (auto& n : ccName_) n = CcSlotName{};
        for (auto& b : ccBinding_) b.store(0, std::memory_order_relaxed);

        if (!json.empty()) {
            const auto parsed = rfl::json::read<std::vector<CcSlotDto>, rfl::DefaultIfMissing>(json);
            if (parsed) {
                for (const CcSlotDto& s : *parsed) {
                    if (s.slot >= kCcSlotCount || s.cc > 127 || s.channel > 15) continue;
                    ccName_[s.slot] = CcSlotName{s.name, s.shortName, true};
                    ccBinding_[s.slot].store(
                        static_cast<std::uint16_t>(0x8000u | (s.channel << 8) | s.cc),
                        std::memory_order_relaxed);
                }
            } else {
                d_stderr("[retroplug] __rp_parameterMapJson unreadable: %s",
                         parsed.error().what().c_str());
            }
        }

        // Every claimed slot now means something new, so push the DAW's current values at the ROM that
        // just arrived rather than waiting for the user to move a lane.
        ccResendAll_.store(true, std::memory_order_relaxed);

        if (canUpdateParameterInfo()) requestParameterInfoUpdate();
    }
};

Plugin* createPlugin() { return new PluginDSP(); }

// In-process handoff: the editor calls this with getPluginInstancePointer() to reach the shared host.
SharedDSP* getSharedDSP(void* pluginPtr) {
    return &static_cast<PluginDSP*>(pluginPtr)->shared_;
}

END_NAMESPACE_DISTRHO
