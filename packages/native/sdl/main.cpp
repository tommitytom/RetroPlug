#include "app.hpp"

// The generated product tables: the backends THIS product names (AUDIO_BACKENDS) and the providers it starts. Only the
// unit that constructs them includes them, so a unit that just uses AppState reads no generated declaration.
#include "rp_product_audio_backends.hpp"
#include "rp_product_providers.hpp"

namespace retroplug::sdl {


// retroplug-sdl — the standalone RetroPlug for framebuffer handhelds (Anbernic / muOS) and any plain
// Linux desktop, hosted on SDL2 instead of DPF/pugl. muOS has no X11 (fbdev + Mali EGL only), so the
// DPF JACK standalone — whose window comes from pugl's X11-only Linux backend — can't run there. This
// host reuses the ENTIRE core (retroplug-backend Engine + cores + txiki runtime, and the SAME embedded
// control-plane + React/LVGL UI bytecode the plugin ships) and swaps only the thin shell:
//
//   * window + present : SDL2 software renderer (LVGL renders into a CPU ARGB8888 buffer; we upload it
//                        with SDL_UpdateTexture + SDL_RenderCopy — SDL drives Mali GLES2 internally, so
//                        no manual GL, no desktop-GL-over-Mali fuss).
//   * audio            : an SDL2 audio callback that drives Engine::processBlock per block (mirrors
//                        PluginDSP::run / AudioDriverRpcService::audioLoop, minus DPF).
//   * input            : SDL keyboard → the LVGL keypad indev + the "key" JS bus; GamepadManager →
//                        the gamepad-* JS bus (emulator input / TS bindings) AND the keypad indev
//                        (menu navigation, since a handheld has no arrow keys).
//
// It composes PluginDSP's control-plane boot (the backend service graph + __rpcSend on
// Symbol.for("plugin") + the cp bundle) and PluginUI's mount (the UI bundle + LVGL display) onto ONE
// TjsHostRuntime, exactly as the DPF plugin splits them across its DSP and UI halves. The non-GL LVGL
// setup mirrors packages/native/test/ui/RenderCore.cpp; the boot mirrors plugin/PluginDSP.cpp.

// ---- boot ------------------------------------------------------------------------------------------
// The boot, the arguments, the test hooks and the frame loop (docs/modular-graph-phase-f5b-design.md 5.5). The control
// plane's runtime, services and RPC facets come up on the one host here (PluginDSP::bootControlPlane, DPF-free).

AppState* g_app = nullptr;  // single instance

namespace {

bool readReady(TjsHostRuntime& host) { return jsReadReady(host.context()); }

}  // namespace

bool bootControlPlane(AppState& a) {
    if (!a.host.init()) { std::fprintf(stderr, "[retroplug-sdl] TjsHostRuntime init failed\n"); return false; }
    JSContext* ctx = a.host.context();

    a.transport = std::make_unique<rpcpp::QuickJSTransport>(ctx, [](JSContext*, JSValue) {});
    a.server    = std::make_unique<BackendRpcServer>(*a.transport, rpcpp::QuickJSCodec{ctx});
    registerHostRpc(*a.server, a.svc.host);
    registerEmulatorRpc(*a.server, a.svc.engineSvc);
    // The facets of the providers this product started (the N8's link half, which Settings > N8 drives). The UI
    // bundle runs on this same context, so it reaches them through the same __rpcSend.
    rp_register_package_rpc(*a.server, a.providers);
    a.server->addDiscoveryMethod();

    // globalThis[Symbol.for("plugin")] = { __rpcSend } — the namespace realBackend.ts targets.
    BackendRpcServer* srv = a.server.get();
    installPluginNamespace(ctx, [&](JSValue ns) {
        a.host.bindRpcSend(ns, [srv](JSContext* sctx, JSValueConst req) -> JSValue {
            auto out = srv->processMessage(req);
            if (!out) return JS_NULL;
            return out->materialize(sctx);
        });
    });

    // Systems bake the sample rate at construct — set it BEFORE the bundle composes / autoloads.
    a.svc.engine.setSampleRate(a.sampleRate);

    a.host.evalModuleBytecode(rp_cp_bundle, rp_cp_bundle_size);
    for (int i = 0; i < 1000 && !readReady(a.host); ++i) a.host.pump();
    if (!readReady(a.host)) { std::fprintf(stderr, "[retroplug-sdl] control plane not ready\n"); return false; }
    return true;
}


void emitUiFrame(AppState& a) {
    JSContext* ctx = a.ui.getContext();
    if (!ctx) return;
    // arg0 = LVGL tick (ms): the UI's monotonic clock (menu gamepad hold-to-repeat timing).
    JSValue tickMs = JS_NewUint32(ctx, lv_tick_get());
    a.ui.emit("frame", 1, &tickMs); // drives the views and the shell's UI services
    JS_FreeValue(ctx, tickMs);
}

// The teardown the product's providers owe (the N8 link, the Launchpad's port claim), in the unit that holds the
// generated table. The graph is cleared first: a node may use its provider's handle in destroy.
AppState::~AppState() {
    svc.engine.clearGraph();
    rp_stop_providers(providers);
}

}  // namespace retroplug::sdl

using namespace retroplug::sdl;

int main(int argc, char** argv) {
    AppState app;
    g_app = &app;

    // The host's audio: the backends THIS product names (rp_product_audio_backends, from AUDIO_BACKENDS), in preference
    // order, and the product's client behind them. Built before any hook can fire, so __rp_audioApply and the
    // reconfig test hooks always have a host to talk to.
    ProductAudioClient audioClient;
    audioClient.app = &app;
    // The stream's consumer buffer: poll() push_backs into it on the audio thread, so it is reserved to the ring's size
    // once, before any device can pull (at full ring size it never has to grow there).
    app.midiScratch.reserve(rp::midi::MidiStream::kCapacity);
    rp::audio::AudioHost audioHost(rp_product_audio_backends(), audioClient);
    app.audio = &audioHost;
    // The graph's hardware nodes bind these by name (rp.host.devices/2), so they go in before any document can place
    // one: the control plane below is the first thing that can publish a graph. The providers install theirs (the N8's
    // cart link, the Launchpad pack's surface link) as they start, and mount their facets in bootControlPlane.
    // A refusal (a full table, a name taken) would leave the nodes on their stubs, so it is said, not swallowed.
    app.providers = rp_start_providers(app.deviceHost.get());

    // --- args / env ---
    std::string autoloadPath;
    if (const char* env = std::getenv("RETROPLUG_AUTOLOAD_PROJECT")) autoloadPath = env;
    long exitAfterFrames = 0;
    if (const char* env = std::getenv("RETROPLUG_SDL_EXIT_AFTER_FRAMES")) exitAfterFrames = std::atol(env);
    std::string screenshotPath;
    if (const char* env = std::getenv("RETROPLUG_SDL_SCREENSHOT")) screenshotPath = env;
    // --audio-backend <id> / RETROPLUG_SDL_AUDIO_BACKEND: the backend a run uses, overriding the saved choice for that
    // run (a saved choice carries backend and driver ids, never a library's numeric id).
    if (const char* env = std::getenv("RETROPLUG_SDL_AUDIO_BACKEND")) app.backendOverride = env;
    // --render-ahead <frames> / RETROPLUG_SDL_RENDER_AHEAD: render on a thread into a ring, and let the device callback
    // only copy out of it (for an audio server with a small fixed quantum, where one emulator block cannot fit a
    // callback).
    if (const char* env = std::getenv("RETROPLUG_SDL_RENDER_AHEAD")) {
        const int f = std::atoi(env);
        if (f > 0) app.renderAhead = f;
    }
    // RETROPLUG_SDL_TEST_SCAN makes a SHIPPED binary sweep every output the system lists with a Device Inquiry, so it is
    // refused without the test-only port filter (RP_MIDI_PORT_ALLOW): the filter is what keeps that sweep off a real
    // mixer, cable or Launchpad, and a run without it would probe somebody's gear before anything could stop it.
    if (std::getenv("RETROPLUG_SDL_TEST_SCAN")) {
        const char* allow = std::getenv("RP_MIDI_PORT_ALLOW");
        if (!allow || !*allow) {
            std::fprintf(stderr, "[retroplug-sdl] RETROPLUG_SDL_TEST_SCAN writes a Device Inquiry to every MIDI output: "
                                 "refusing to run without RP_MIDI_PORT_ALLOW set\n");
            return 1;
        }
    }
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--autoload" && i + 1 < argc) autoloadPath = argv[++i];
        else if (arg == "--width" && i + 1 < argc) app.width = static_cast<std::uint32_t>(std::atoi(argv[++i]));
        else if (arg == "--height" && i + 1 < argc) app.height = static_cast<std::uint32_t>(std::atoi(argv[++i]));
        // Default audio block size (frames). The compiled-in default is 512 (low latency); a low-power device passes a
        // bigger buffer here (the muOS launcher uses 4096) so the callback keeps its deadline. It overrides the `audio`
        // section's block for this run (design 6.2, report item 8), which is why it is kept beside the backend override
        // and applied wherever the device is opened.
        else if (arg == "--block-size" && i + 1 < argc) {
            const int b = std::atoi(argv[++i]);
            if (b > 0) app.blockSizeOverride = b;
        }
        else if (arg == "--audio-backend" && i + 1 < argc) app.backendOverride = argv[++i];
        else if (arg == "--render-ahead" && i + 1 < argc) { const int f = std::atoi(argv[++i]); if (f > 0) app.renderAhead = f; }
        else if (arg == "--screenshot" && i + 1 < argc) screenshotPath = argv[++i];
    }

    if (!setupSdl(app)) return 1;
    if (!bootControlPlane(app)) return 1;

    // Autoload a .rplg project before the audio thread takes the Engine (structural build runs direct).
    if (!autoloadPath.empty()) {
        const std::string ok = callGlobal(app.host, "__rp_loadProjectPath", autoloadPath.c_str());
        std::fprintf(stderr, "[retroplug-sdl] autoload %s -> %s\n", autoloadPath.c_str(), ok.empty() ? "false/void" : ok.c_str());
    }

    if (!setupUi(app)) return 1;

    // The startup rendezvous (docs/modular-graph-phase-f3-design.md section 3): the control plane restores the control
    // surface it persisted (config.json) before the shared stream opens a port of its own, and the project's MIDI
    // selection and the `audio` section are applied by their services on the first poll, which the pumped frame runs -
    // so a claimed pair is reserved from the first frame, never heard as a second, unquantised launch, and never costs
    // an audio restart. Bounded, so a missing or broken config.json (or a hung port) cannot keep the app silent. The
    // audio section's apply only RECORDS a device request (hooks.cpp's jsAudioApply); the device itself opens below,
    // after the MIDI stream and the providers' restore.
    waitForUiStartup(app);

    // Open MIDI (virtual in/out ports). Non-fatal: the host runs without MIDI if none is available. The audio callback
    // drains app.midi into the Engine per block (see render); RETROPLUG_MIDI_LOG logs input. The hardware ports are a
    // PROJECT's choice now, applied by the midi package's service once a project's section is known - so nothing is
    // opened here beyond the virtual pair, and a fresh project opens none.
    app.midi.open();

    // Restore the providers' persisted state (the N8's n8.cfg: reconnect the physical cart if it was left enabled).
    rp_restore_providers(app.providers);

    // Test/diagnostic hook: dump the enumerated hardware MIDI devices then continue.
    if (std::getenv("RETROPLUG_SDL_MIDI_LIST")) {
        for (const auto& n : app.midi.listInputs())  std::fprintf(stderr, "[retroplug-sdl] MIDI in device:  '%s'\n", n.c_str());
        for (const auto& n : app.midi.listOutputs()) std::fprintf(stderr, "[retroplug-sdl] MIDI out device: '%s'\n", n.c_str());
    }

    // Hand the Engine to the audio thread (control-plane edits become push-only, drained per block) and open the
    // device. Mirrors PluginDSP::activate: AudioHost runs stop, release, open, prepare (which re-rates the engine to
    // what the DEVICE came up at) and start, so the engine is shaped before the first pull renders. This is the app's
    // one point of opening: the `audio` section the UI applied during the rendezvous is AppState's request by now, and a
    // UI that never applied leaves the defaults in it (the rule the rendezvous states). It comes AFTER the MIDI stream
    // and the providers' restore because that is the order the app has always opened things in - the control surface's
    // claim, then MIDI, then audio last - and because a running audio thread would render into an engine the restore has
    // not shaped yet.
    app.svc.invoker.setAudioThreadOwns(true);
#if defined(__linux__)
    pinUiThreadAwayFromAudioCore(); // keep the 60 fps UI/emulator work off the audio thread's core
#endif
    applyAudioConfig(app);
    app.audioDeviceReady = true; // from here a UI apply is live (hooks.cpp's jsAudioApply)
    if (!app.audio->live()) {
        // A run that pinned the null backend is not a fallback, and the line says which switch asked for it: the same
        // field carries --audio-backend and RETROPLUG_SDL_AUDIO_BACKEND.
        const std::string why = app.audio->status().error.empty()
                                    ? (app.backendOverride == "null" ? "pinned by --audio-backend null"
                                                                    : "no device chose it")
                                    : app.audio->status().error;
        std::fprintf(stderr, "[retroplug-sdl] audio: on the null device (%s)\n", why.c_str());
    }

    app.idleProbe.armFromEnv(app.svc.engine);
    std::fprintf(stderr, "[retroplug-sdl] running %ux%u @ %.0f Hz\n", app.width, app.height, app.sampleRate);

    // Test hook: inject a MIDI message into the input ring (no hardware needed) to prove the audio-thread
    // drain path (ring -> the audio callback's poll -> engine.stageMidi). The staged count is printed on exit.
    if (std::getenv("RETROPLUG_SDL_MIDI_SELFTEST")) {
        const std::uint8_t noteOn[3] = { 0x90, 60, 100 };  // ch1 NoteOn C4
        app.midi.injectForTest(noteOn, 3);
    }

    // Test hook: drive the transport seam the way the menu does — through the JS global, not the atomic —
    // so a missing or renamed bind shows up as playing=0 in the report below rather than as a row that
    // silently disappears from the menu. Any non-empty string is truthy to JS_ToBool.
    // Test hook: start the transport through the same global the MIDI Clock row uses. The TEMPO is deliberately not set
    // here: it comes from the autoloaded project's `midi.clockBpm`, applied by the midi package's service, which is what
    // E7 (the tempo is the project's) means - a hook that also set it would let E7 regress with this line still green.
    if (std::getenv("RETROPLUG_SDL_TEST_TRANSPORT")) callGlobal(app.host, "__rp_setTransport", "1");

    // Test hook: drive the Settings > Audio seam the way the package's Apply row does (RETROPLUG_SDL_TEST_AUDIO_HOOK) -
    // as JS, through the hooks themselves, with an `audio` section's fields. A hook whose mapping is wrong MUTES the run
    // instead of quietly keeping the old device, and a bind that went missing reads back as the null fallback, so the
    // report below is what the rows would see.
    if (std::getenv("RETROPLUG_SDL_TEST_AUDIO_HOOK")) {
        JSContext*  ctx = app.host.context();
        const char* apply =
            "(__rp_audioApply({ backend: 'auto', driver: '', device: '', sampleRate: 44100, blockSize: 256, "
            "outChannels: 2 }), 1)";
        JS_FreeValue(ctx, JS_Eval(ctx, apply, std::strlen(apply), "<audio-hook>", JS_EVAL_TYPE_GLOBAL));
        const char* readCode = "JSON.stringify(__rp_audioDescribe())";
        JSValue     read = JS_Eval(ctx, readCode, std::strlen(readCode), "<audio-hook>", JS_EVAL_TYPE_GLOBAL);
        const char* json = JS_ToCString(ctx, read);
        std::fprintf(stderr, "[retroplug-sdl] post-audio-hook: %s\n", json ? json : "(none)");
        if (json) JS_FreeCString(ctx, json);
        JS_FreeValue(ctx, read);
    }

    // Test hook: exercise a live audio reconfigure (device close/reopen + engine re-rate) headlessly.
    if (std::getenv("RETROPLUG_SDL_TEST_RECONFIG")) {
        reconfigureAudio(app, 44100, 512, app.reqOutChannels, app.reqOutputDevice);
        std::fprintf(stderr, "[retroplug-sdl] post-reconfigure: %d Hz, %d frames, stream=%s\n",
                     app.audio->status().sampleRate, app.audio->status().blockFrames,
                     app.audio->live() ? "open" : "muted");
    }

    // Test hook: reconfigure to a wide output (RETROPLUG_SDL_TEST_MULTIOUT=<N ch>) and report the obtained
    // channel count — proves the N-channel device open + planar multi-out render + interleave stride.
    if (const char* env = std::getenv("RETROPLUG_SDL_TEST_MULTIOUT")) {
        const int ch = std::atoi(env);
        if (ch > 0) {
            reconfigureAudio(app, app.reqSampleRate, app.reqBlockSize, ch, app.reqOutputDevice);
            std::fprintf(stderr, "[retroplug-sdl] post-multiout: numOutputs=%d planarBufs=%zu stream=%s\n",
                         app.numOutputs, app.audioPlanar.size(), app.audio->live() ? "open" : "muted");
        } else {
            std::fprintf(stderr, "[retroplug-sdl] RETROPLUG_SDL_TEST_MULTIOUT=%s ignored: not a channel count\n", env);
        }
    }

    // Test hook: exercise the window resize path (SDL window + LVGL display/buffer + texture) headlessly,
    // the same call the __rp_setWindowSize seam makes. RETROPLUG_SDL_TEST_RESIZE=WxH.
    if (const char* env = std::getenv("RETROPLUG_SDL_TEST_RESIZE")) {
        int w = 0, h = 0;
        if (std::sscanf(env, "%dx%d", &w, &h) == 2 && w > 0 && h > 0) {
            requestWindowSize(app, static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h));
            int ww = 0, wh = 0;
            SDL_GetWindowSize(app.window, &ww, &wh);
            // wmControlled is printed too: a request the WM clamps or refuses must not be mistaken for a
            // tiling takeover (see requestWindowSize), and that's only observable through this latch.
            std::fprintf(stderr, "[retroplug-sdl] post-resize: state=%ux%u window=%dx%d buf=%zu wmControlled=%d\n",
                         app.width, app.height, ww, wh, app.drawBuf.size(), (int)app.wmControlled);
        }
    }

    // Test hook: push an OS-close (SDL_QUIT) so the close-guard path runs headlessly. With no unsaved
    // changes the guard doesn't veto, so the app exits on the first handleEvents (well before exitAfterFrames).
    if (std::getenv("RETROPLUG_SDL_TEST_QUIT")) {
        SDL_Event q; q.type = SDL_QUIT; SDL_PushEvent(&q);
    }

    // Test hook: drive the MIDI-clock → transport estimator with a synthetic clock at RETROPLUG_SDL_TEST_CLOCK
    // BPM and report the derived tempo/transport (deterministic — no wall-clock dependence).
    if (const char* env = std::getenv("RETROPLUG_SDL_TEST_CLOCK")) {
        double target = std::atof(env);
        if (target <= 0.0) target = 140.0;
        MidiClockSync cs;
        cs.sampleRate = app.sampleRate;
        const long long framesPerPulse = static_cast<long long>(app.sampleRate * 60.0 / (target * 24.0));
        for (int i = 0; i < 48; ++i) { cs.onByte(0xF8); cs.advance(framesPerPulse); } // 2 quarter-notes of clock
        const double derived = cs.outBpm();
        const bool playing = cs.outPlaying();
        cs.onByte(0xFC); cs.advance(framesPerPulse);          // explicit stop
        const bool afterStop = cs.outPlaying();
        // A master that CHANGES tempo mid-stream (the DAW-tempo-fader case): the estimate has to follow it,
        // not latch the first reading.
        MidiClockSync sweep;
        sweep.sampleRate = app.sampleRate;
        for (int i = 0; i < 48; ++i) { sweep.onByte(0xF8); sweep.advance(framesPerPulse); }
        const double sweepFirst = sweep.outBpm();
        const long long framesPerPulse2 = static_cast<long long>(app.sampleRate * 60.0 / ((target * 0.5) * 24.0));
        for (int i = 0; i < 48; ++i) { sweep.onByte(0xF8); sweep.advance(framesPerPulse2); }
        const double sweepAfter = sweep.outBpm();

        for (int i = 0; i < 64; ++i) cs.advance(framesPerPulse); // long clock silence → free-run revert
        // `live` is the RUNNING host's transport, not this scratch estimator's: it proves the standalone
        // boots stopped (the local transport's default), which is the thing a cart's sync role keys off.
        std::fprintf(stderr, "[retroplug-sdl] clock self-test: target=%.1f derived=%.1f playing=%d afterStop=%d "
                     "afterTimeout(bpm=%.1f playing=%d) live=%d sweep(%.1f -> %.1f)\n",
                     target, derived, (int)playing, (int)afterStop, cs.outBpm(), (int)cs.outPlaying(),
                     (int)app.transportPlaying.load(std::memory_order_relaxed), sweepFirst, sweepAfter);
    }

    // Test hook: drive the desktop wheel headlessly. RETROPLUG_SDL_TEST_WHEEL=<notches> pushes a real
    // SDL_MOUSEMOTION + SDL_MOUSEWHEEL pair through the SDL queue (the exact path a physical wheel takes)
    // once the React menu has settled, and the handler logs the translated notches + cursor. It reports
    // scrolled=0 on the start menu, which fits the window — nothing here can open an overflowing menu, so
    // the scroll itself is asserted end-to-end by the shared `pnpm test:ui wheel-scroll` instead.
    const char* wheelTest = std::getenv("RETROPLUG_SDL_TEST_WHEEL");
    app.logWheel = wheelTest != nullptr;

    // Test hook: drive the real menu from the SDL queue to Settings > Audio, one key every second frame (the keypad
    // needs a frame to rebuild between presses), and report the audio device's status with the rows open. A leg uses it
    // to show a RUNNING stream survives a menu build - the one thing a device listing must never disturb.
    const char* menuTestEnv = std::getenv("RETROPLUG_SDL_TEST_MENU");
    const long  menuStart   = menuTestEnv ? std::atol(menuTestEnv) : -1;
    const SDL_Keycode menuKeys[] = {SDLK_ESCAPE, SDLK_DOWN, SDLK_DOWN, SDLK_DOWN, SDLK_DOWN, SDLK_RETURN, SDLK_RETURN};
    const long  menuKeyCount     = static_cast<long>(sizeof(menuKeys) / sizeof(menuKeys[0]));
    bool        menuReported     = false;

    // --- the 60 fps loop: input → JS frame → LVGL render → present ---
    const bool requireNonBlank = std::getenv("RETROPLUG_SDL_REQUIRE_NONBLANK") != nullptr;
    bool nonBlankFail = false;
    // Test hook: start a control-surface scan the way Settings > MIDI's row does - through the UI's own global, so the
    // whole TS scan (the inquiry sweep, the model confirmation, persisting the find and claiming it) runs over the real
    // facet and RtMidi. tools/run-sdl-controller.sh drives it against a fake device on the ALSA sequencer; main() refused
    // to run at all unless the port filter was set, because this sweep writes to every output the system lists.
    const bool scanTest = std::getenv("RETROPLUG_SDL_TEST_SCAN") != nullptr;
    long frame = 0;
    while (app.running) {
        const Uint32 t0 = SDL_GetTicks();
        handleEvents(app);
        // The audio frame: a device that went away (or that the no-pull watchdog has not heard from) falls back to the
        // null device here, with the reason in the status Settings shows.
        if (app.audio->poll())
            std::fprintf(stderr, "[retroplug-sdl] audio: %s\n",
                         app.audio->status().error.empty() ? "device changed" : app.audio->status().error.c_str());
        pollFileDialog(app);    // deliver a finished OS file-dialog pick (async; no-op when none is open)
        rp_wake_providers(app.providers);  // write the LED traffic the audio thread queued (no-op when idle)
        // Free the plans the audio thread has handed back, on this thread (the one setGraph runs on), rather than at
        // the next graph publish: a node an edit removed while audio runs (a cart node and the notes it holds on the
        // cart, a surface node, a core, the resources they pin) goes within a frame. Nothing to free costs nothing.
        app.idleProbe.tick(app.svc.reclaimOnIdle());
        if (JSContext* ctx = app.ui.getContext()) {
            emitUiFrame(app);
            pumpGamepad(app);
            (void)ctx;
        }
        app.ui.tick();          // pump the shared host's JS/libuv loop
        lv_timer_handler();     // LVGL indev read + layout + render into drawBuf
        present(app);

        if (scanTest && frame == 10) // the UI has mounted and run its startup restore by now
            std::fprintf(stderr, "[retroplug-sdl] scan test: started=%s\n",
                         callGlobal(app.host, "__rp_scanControlSurface", "").c_str());

        if (menuStart >= 0 && frame >= menuStart) {
            const long step = (frame - menuStart) / 2;
            if ((frame - menuStart) % 2 == 0 && step < menuKeyCount) {
                SDL_Event down{};
                down.type            = SDL_KEYDOWN;
                down.key.state       = SDL_PRESSED;
                down.key.keysym.sym  = menuKeys[step];
                SDL_PushEvent(&down);
                SDL_Event up = down;
                up.type       = SDL_KEYUP;
                up.key.state  = SDL_RELEASED;
                SDL_PushEvent(&up);
            }
            if (!menuReported && step >= menuKeyCount) {
                menuReported = true;
                const bool live = app.audio && app.audio->live();
                std::fprintf(stderr, "[retroplug-sdl] menu test: settings open, audio live=%d backend=%s\n", (int)live,
                             app.audio ? app.audio->status().backend.c_str() : "");
            }
        }

        if (wheelTest && frame == 45) { // the menu has mounted + laid out by now
            SDL_Event m{};
            m.type = SDL_MOUSEMOTION;
            m.motion.x = static_cast<Sint32>(app.width / 2);
            m.motion.y = static_cast<Sint32>(app.height / 2);
            SDL_PushEvent(&m);
            SDL_Event w{};
            w.type = SDL_MOUSEWHEEL;
            w.wheel.y = std::atoi(wheelTest);
            w.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
            SDL_PushEvent(&w);  // handled by the next handleEvents, after the motion sets the cursor
        }

        if (exitAfterFrames > 0 && frame + 1 == exitAfterFrames) { // last frame: screenshot + non-blank gate
            if (!screenshotPath.empty()) writeScreenshot(app, screenshotPath);
            if (requireNonBlank && frameIsBlank(app)) {
                nonBlankFail = true;
                std::fprintf(stderr, "[retroplug-sdl] non-blank check FAILED: frame is flat (UI did not render)\n");
            }
        }
        if (exitAfterFrames > 0 && ++frame >= exitAfterFrames) app.running = false;

        const Uint32 dt = SDL_GetTicks() - t0;
        if (dt < 16) SDL_Delay(16 - dt); // ~60 fps
    }
    if (std::getenv("RETROPLUG_SDL_TEST_QUIT"))
        std::fprintf(stderr, "[retroplug-sdl] close-guard test: exited at frame %ld\n", frame);

    // --- teardown: reclaim the Engine, unmount, drop SDL ---
    // Stop the device and take the Engine back before anything else the control plane might still tick: a pull that
    // restarted the engine here would resurrect what the next lines are closing.
    app.audio->stop();
    if (std::getenv("RETROPLUG_SDL_MIDI_SELFTEST"))
        std::fprintf(stderr, "[retroplug-sdl] MIDI self-test: staged %llu message(s) into the Engine\n",
                     static_cast<unsigned long long>(app.midiStaged.load(std::memory_order_relaxed)));
    // playing=1 proves the whole path: the JS bind exists, the request crossed to the audio thread, and the
    // transport it published back came out running.
    if (std::getenv("RETROPLUG_SDL_TEST_TRANSPORT"))
        std::fprintf(stderr, "[retroplug-sdl] transport self-test: playing=%d bpm=%.0f\n",
                     (int)app.transportPlaying.load(std::memory_order_relaxed),
                     (double)app.transportBpm.load(std::memory_order_relaxed));
    // Test hook: the status the audio service's apply adopted, as the Muted row and the file's section read it. Its use
    // is a run whose `--block-size` overrides the section's block (E12): the status carries the flag's block, not the
    // file's.
    if (std::getenv("RETROPLUG_SDL_TEST_AUDIO_STATUS")) {
        JSContext* ctx = app.host.context();
        const char* code = "JSON.stringify(__rp_audioStatus())";
        JSValue read = JS_Eval(ctx, code, std::strlen(code), "<audio-status>", JS_EVAL_TYPE_GLOBAL);
        const char* json = JS_ToCString(ctx, read);
        std::fprintf(stderr, "[retroplug-sdl] post-audio-status: %s\n", json ? json : "(none)");
        if (json) JS_FreeCString(ctx, json);
        JS_FreeValue(ctx, read);
    }
    app.midi.close();
    // Reclaim the plans while every device link is still open, as the plugin's destructor does: a node may use its
    // handle in destroy, and a surface node reaching a port the next line has already closed reads disconnected and
    // sends its last frame nowhere. The destructor clears again, for the paths that return before this point.
    app.svc.engine.clearGraph();
    app.svc.invoker.setAudioThreadOwns(false);
    app.svc.invoker.drainInto(app.svc.engine);
    app.svc.invoker.reclaimReleased();

    // Tear the UI down in the order the LVGL lifecycle expects. Disable invalidation FIRST so the widget
    // deletes below don't walk styles into freed memory (lv_obj_invalidate early-outs). Then detachDisplay()
    // unmounts React + async-deletes every widget (and the window root); a single lv_timer_handler() flushes
    // that async-delete queue. Do NOT lv_obj_clean(screen) beforehand — that deletes the same widgets
    // synchronously, so the async deletes detachDisplay queued then run over freed objects (a UAF that
    // survives on Linux's allocator but is a hard EXC_BAD_ACCESS on macOS / under MALLOC_PERTURB_).
    if (app.display) lv_display_enable_invalidation(app.display, false);
    app.ui.detachDisplay();
    lv_timer_handler();

    // Once more, because the unmount above is code that can publish: a React cleanup that edited the project leaves a plan
    // the first clear never saw, and a plan still holding the surface link when the providers stop is exactly what the
    // provider's own rule forbids ("a product reclaims its plans before it stops its providers"). Nothing published by the
    // unmount costs nothing here.
    app.svc.engine.clearGraph();
    app.svc.invoker.drainInto(app.svc.engine);
    app.svc.invoker.reclaimReleased();

    // Stop the providers LAST, after React has unmounted: their facets stay mounted on rpc pointers into them, so a
    // cleanup that called one during the unmount (a release on the way out, a scan cancelled) would read freed memory if
    // the providers had gone first. The Launchpad pack's link flushes what its last block queued and replays the farewell
    // bytes TS handed it, which releases a device left in Programmer mode (its own front panel is locked until they
    // arrive); neither needs a window. Idempotent, so ~AppState's stop is a no-op.
    rp_stop_providers(app.providers);

    if (app.texture) SDL_DestroyTexture(app.texture);
    if (app.renderer) SDL_DestroyRenderer(app.renderer);
    if (app.window) SDL_DestroyWindow(app.window);
    SDL_Quit();
    g_app = nullptr;
    return nonBlankFail ? 2 : 0; // exit 2 = the RETROPLUG_SDL_REQUIRE_NONBLANK gate failed (blank UI)
}
