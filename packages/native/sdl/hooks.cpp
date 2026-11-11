#include "app.hpp"

namespace retroplug::sdl {


// The __rp_* window hooks: the seam the UI optional-chains for the window, the file dialog, the transport, the audio and
// MIDI device rows, and the install that binds them (docs/modular-graph-phase-f5b-design.md 5.5).

// ---- backend boot (PluginDSP::bootControlPlane, DPF-free) ------------------------------------------

// Call globalThis[name](arg?) on the control-plane context (main thread). Returns the string result, or
// "" for non-string / missing / exception. Shared with PluginDSP::callGlobal.
//
// `ready` is true unconditionally here, and that is not laziness: bootControlPlane returns false if the
// host fails to initialise OR the bundle never signals ready, and main exits 1 on either. An unready SDL
// host is a dead process, so by the time anything calls this there is nothing to guard against. The
// plugin cannot take that route - a DAW scan must degrade rather than exit - which is why the shared
// body carries the guard and this caller opts out of it explicitly.
std::string callGlobal(TjsHostRuntime& host, const char* name, const char* arg) {
    JsGlobals g{ host.context(), /*ready=*/true,
                 [](const char* n, const char* detail) {
                     std::fprintf(stderr, "[retroplug-sdl] %s threw: %s\n", n, detail);
                 } };
    return jsCallGlobal(g, name, arg);
}

bool hasGlobalFunction(JSContext* ctx, const char* name) { return jsHasGlobalFunction(ctx, name); }

// Tell the control plane how many output channels the device has: it builds the graph's output wiring for
// that many. Before the audio thread takes the Engine, so the plan it publishes applies at once.
void setHostOutputs(AppState& a) {
    if (callGlobal(a.host, "__rp_setHostOutputs", std::to_string(a.numOutputs).c_str()) != "true")
        std::fprintf(stderr, "[retroplug-sdl] the %d-output host shape did not plan; the outputs keep the shape before it\n",
                     a.numOutputs);
}

// ---- __rp_* window hooks (PluginUI's window-op seam, single-instance) ------------------------------
// The UI optional-chains these, so anything we omit is simply inert. We provide the handful a handheld
// standalone needs; renders need no hook here, they are engine jobs through the shared backend's RPCs.

namespace {
// The hook bodies themselves: installWindowHooks binds them, and nothing outside this unit names one.
JSValue jsQuitWindow(JSContext*, JSValueConst, int, JSValueConst*) {
    if (g_app) g_app->running = false;
    return JS_UNDEFINED;
}
// A WM-controlled window (a fullscreen handheld, or a window a tiling compositor tiled) → the UI fits its grid
// via zoom rather than resizing. A floating desktop window is ours to size, so the App's fit-to-grid effect
// drives resizes. Mirrors PluginUI::isWindowSizeControlled — the latch that keeps us from fighting the WM.
JSValue jsIsWindowSizeControlled(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    return JS_NewBool(ctx, g_app ? g_app->wmControlled : 1);
}
// __rp_setWindowSize(w, h): grow/shrink the window to the grid. Mirrors the DPF standalone, where setSize() →
// the platform resize callback reallocs the LVGL buffer + emits "resize"; SDL gives us no such callback, so
// requestWindowSize asks the WM and the SDL_WINDOWEVENT_SIZE_CHANGED handler does the realloc. Inert once the
// WM owns geometry (a fullscreen handheld, or a tiling compositor) — requestWindowSize handles that.
JSValue jsSetWindowSize(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (!g_app || argc < 2) return JS_UNDEFINED;
    std::uint32_t w = 0, h = 0;
    JS_ToUint32(ctx, &w, argv[0]);
    JS_ToUint32(ctx, &h, argv[1]);
    if (w != 0 && h != 0) requestWindowSize(*g_app, w, h);
    return JS_UNDEFINED;
}
JSValue jsSetWindowTitle(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (g_app && g_app->window && argc >= 1) {
        const char* t = JS_ToCString(ctx, argv[0]);
        if (t && *t) SDL_SetWindowTitle(g_app->window, t);
        if (t) JS_FreeCString(ctx, t);
    }
    return JS_UNDEFINED;
}
JSValue jsOpenPath(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc >= 1) {
        const char* p = JS_ToCString(ctx, argv[0]);
        if (p && *p) openPathInFileManager(p);
        if (p) JS_FreeCString(ctx, p);
    }
    return JS_UNDEFINED;
}

// Deliver a native file dialog's pick into JS by calling the __rp_onFileBrowserResult resolver the real
// Backend registered (empty path = cancel → null). Same shared context / UI thread that pumps the JS loop,
// so a direct call is safe — the awaiting Promise settles on the next tick. Copy of PluginUI::uiFileBrowserSelected.
void deliverFileBrowserResult(JSContext* ctx, const std::string& path) {
    jsDeliverFileBrowserResult(ctx, path.c_str());
}

// __rp_openFileBrowser(title, patterns, saving, defaultName, startDir, directory): open the OS-native file
// dialog (bound only where NativeFileDialog::available()). Async — the pick is delivered later by
// pollFileDialog. `patterns` is a whitespace-separated glob list; `directory` picks a FOLDER (the render
// "Output Dir"); `saving` seeds the save name with defaultName.
JSValue jsOpenFileBrowser(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (!g_app || argc < 6) return JS_UNDEFINED;
    auto arg = [&](int i) -> std::string {
        const char* s = JS_ToCString(ctx, argv[i]);
        std::string out = (s && *s) ? s : "";
        if (s) JS_FreeCString(ctx, s);
        return out;
    };
    g_app->nativeDialog.request(arg(0), arg(1), JS_ToBool(ctx, argv[2]) == 1, arg(3), arg(4),
                                JS_ToBool(ctx, argv[5]) == 1);
    return JS_UNDEFINED;
}

// Poll the in-flight OS file dialog once per frame; when the helper finishes, deliver the pick (cancel = empty)
// into JS. Cheap no-op when nothing is open.
}  // namespace

void pollFileDialog(AppState& a) {
    JSContext* ctx = a.ui.getContext();
    if (!ctx) return;
    a.nativeDialog.poll([ctx](const std::string& pick) {
        if (std::getenv("RETROPLUG_SDL_DIALOG_SELFTEST")) // headless E2E: prove the full pfd round-trip delivered
            std::fprintf(stderr, "[retroplug-sdl] dialog self-test: picked '%s'\n", pick.c_str());
        deliverFileBrowserResult(ctx, pick);
    });
}

// __rp_getTransport(): { playing, external, bpm, localBpm } — the state behind the Instance menu's MIDI Clock
// submenu. `bpm` is the tempo in force (an external master's when `external`); `localBpm` is our own setting,
// which is what comes back when the master goes away. Standalone-only: a DAW / the DPF standalone take the
// transport from their host, so those builds never bind this and the submenu is absent.
namespace {
JSValue jsGetTransport(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    JSValue o = JS_NewObject(ctx);
    if (g_app) {
        JS_SetPropertyStr(ctx, o, "playing",  JS_NewBool(ctx, g_app->transportPlaying.load(std::memory_order_relaxed)));
        JS_SetPropertyStr(ctx, o, "external", JS_NewBool(ctx, g_app->transportExternal.load(std::memory_order_relaxed)));
        JS_SetPropertyStr(ctx, o, "bpm",      JS_NewFloat64(ctx, g_app->transportBpm.load(std::memory_order_relaxed)));
        JS_SetPropertyStr(ctx, o, "localBpm", JS_NewFloat64(ctx, g_app->clockBpmRequest.load(std::memory_order_relaxed)));
    }
    return o;
}

// __rp_setTransport(playing): start/stop the local transport. Takes effect on the next audio block; with an
// external clock master present it is recorded but overridden (see jsGetTransport).
JSValue jsSetTransport(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (g_app && argc >= 1)
        g_app->transportRequest.store(JS_ToBool(ctx, argv[0]) != 0, std::memory_order_relaxed);
    return JS_UNDEFINED;
}

// __rp_setClockBpm(bpm): the local tempo, clamped to the same range the clock estimator will accept from an
// external master (so the two sources can't disagree about what a legal tempo is). Nothing is persisted here: the tempo
// is the project's `midi.clockBpm`, written by the row that changes it.
JSValue jsSetClockBpm(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    double bpm = 0.0;
    if (g_app && argc >= 1 && JS_ToFloat64(ctx, &bpm, argv[0]) == 0) {
        bpm = std::clamp(bpm, MidiClockSync::kMinBpm, MidiClockSync::kMaxBpm);
        g_app->clockBpmRequest.store(static_cast<float>(bpm), std::memory_order_relaxed);
    }
    return JS_UNDEFINED;
}

// ---- the MIDI stream (docs/modular-graph-phase-f5b-design.md section 6.4) -------------------------
//
// Two hooks: what the machine lists and what actually OPENED (the rows read the second rather than inferring it from a
// listing taken at another moment), and the selection a project's `midi` section resolved to. The selection is
// EXACT port names: matching a stored name to a present one is TypeScript's (midi/ts/portMatch.ts), so native never
// guesses. Both disappear in the plugin, which takes MIDI from its DAW.

void setStringArray(JSContext* ctx, JSValueConst obj, const char* name, const std::vector<std::string>& values) {
    JSValue a = JS_NewArray(ctx);
    for (std::uint32_t i = 0; i < values.size(); ++i)
        JS_SetPropertyUint32(ctx, a, i, JS_NewString(ctx, values[i].c_str()));
    JS_SetPropertyStr(ctx, obj, name, a);
}

// __rp_midiDescribe(): { inputs, outputs, openedInputs, openedOutputs, supportsVirtual, reserved: { input, output } }.
JSValue jsMidiDescribe(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    JSValue o = JS_NewObject(ctx);
    if (!g_app) return o;
    setStringArray(ctx, o, "inputs", g_app->midi.listInputs());
    setStringArray(ctx, o, "outputs", g_app->midi.listOutputs());
    setStringArray(ctx, o, "openedInputs", g_app->midi.openedInputs());
    setStringArray(ctx, o, "openedOutputs", g_app->midi.openedOutputs());
    JS_SetPropertyStr(ctx, o, "supportsVirtual", JS_NewBool(ctx, g_app->midi.supportsVirtual()));
    JSValue reserved = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, reserved, "input", JS_NewString(ctx, g_app->midi.reservedInput().c_str()));
    JS_SetPropertyStr(ctx, reserved, "output", JS_NewString(ctx, g_app->midi.reservedOutput().c_str()));
    JS_SetPropertyStr(ctx, o, "reserved", reserved);
    return o;
}

/** The selection object's `names` list, as the strings a row wrote. */
std::vector<std::string> stringListOf(JSContext* ctx, JSValueConst value) {
    std::vector<std::string> out;
    if (!JS_IsArray(value)) return out;
    JSValue lengthValue = JS_GetPropertyStr(ctx, value, "length");
    std::int64_t n = 0;
    JS_ToInt64(ctx, &n, lengthValue);
    JS_FreeValue(ctx, lengthValue);
    for (std::int64_t i = 0; i < n; ++i) {
        JSValue entry = JS_GetPropertyUint32(ctx, value, static_cast<std::uint32_t>(i));
        const char* s = JS_ToCString(ctx, entry);
        if (s && *s) out.emplace_back(s);
        if (s) JS_FreeCString(ctx, s);
        JS_FreeValue(ctx, entry);
    }
    return out;
}

// __rp_midiApply({ inputs, allInputs, outputs }, mode): apply a project's selection to the stream. `mode` is the
// stream's (a project load DIFFS, so it costs no connection it already had; the Rescan row and a re-pick REOPEN, which
// is the user's one remedy for a port that was unplugged). No audio stop: the stream closes and opens inputs, each port
// cancelling its own callback, and replaces its output set through its epoch handshake.
JSValue jsMidiApply(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (!g_app || argc < 1 || !JS_IsObject(argv[0])) return JS_UNDEFINED;
    JSValue inputsValue = JS_GetPropertyStr(ctx, argv[0], "inputs");
    JSValue outputsValue = JS_GetPropertyStr(ctx, argv[0], "outputs");
    const std::vector<std::string> inputs = stringListOf(ctx, inputsValue);
    const std::vector<std::string> outputs = stringListOf(ctx, outputsValue);
    JS_FreeValue(ctx, inputsValue);
    JS_FreeValue(ctx, outputsValue);
    JSValue allValue = JS_GetPropertyStr(ctx, argv[0], "allInputs");
    const bool allInputs = JS_ToBool(ctx, allValue) != 0;
    JS_FreeValue(ctx, allValue);
    rp::midi::Apply mode = rp::midi::Apply::Diff;
    if (argc >= 2) {
        const char* m = JS_ToCString(ctx, argv[1]);
        if (m && std::strcmp(m, "reopen") == 0) mode = rp::midi::Apply::Reopen;
        if (m) JS_FreeCString(ctx, m);
    }
    g_app->midi.setInputs(inputs, allInputs, mode);
    g_app->midi.setOutputs(outputs, mode);
    return JS_UNDEFINED;
}

// ---- the audio device (docs/modular-graph-phase-f5b-design.md section 6.4) -------------------------
//
// Four hooks: what the device can do, one sibling backend's drivers on demand, the whole reconfigure, and a cheap
// status read. The first is also the Settings > Audio gate: a host that binds none draws no row.

JSValue audioStatusObject(JSContext* ctx, const rp::audio::AudioStatus& status, bool live) {
    JSValue o = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, o, "backend", JS_NewString(ctx, status.backend.c_str()));
    JS_SetPropertyStr(ctx, o, "driver", JS_NewString(ctx, status.driver.c_str()));
    JS_SetPropertyStr(ctx, o, "device", JS_NewString(ctx, status.device.c_str()));
    JS_SetPropertyStr(ctx, o, "sampleRate", JS_NewInt32(ctx, status.sampleRate));
    JS_SetPropertyStr(ctx, o, "blockFrames", JS_NewInt32(ctx, status.blockFrames));
    JS_SetPropertyStr(ctx, o, "channels", JS_NewInt32(ctx, status.channels));
    JS_SetPropertyStr(ctx, o, "error", JS_NewString(ctx, status.error.c_str()));
    JS_SetPropertyStr(ctx, o, "live", JS_NewBool(ctx, live));
    return o;
}

JSValue driverObject(JSContext* ctx, const rp::audio::AudioDriverDescription& driver) {
    JSValue o = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, o, "id", JS_NewString(ctx, driver.id.c_str()));
    JS_SetPropertyStr(ctx, o, "label", JS_NewString(ctx, driver.label.c_str()));
    JSValue devices = JS_NewObject(ctx);
    setStringArray(ctx, devices, "names", driver.devices.names);
    JS_SetPropertyStr(ctx, devices, "defaultName", JS_NewString(ctx, driver.devices.defaultName.c_str()));
    JS_SetPropertyStr(ctx, o, "devices", devices);
    return o;
}

// __rp_audioDescribe(): { backends: [{ id, label, autoDriver, drivers: [{ id, label, devices }] }], status }. Only the
// backend IN FORCE carries its drivers (enumerating another is its library's first use, the Zoom L6 rule); a row that
// offers a sibling asks for it through __rp_audioBackendDrivers.
JSValue jsAudioDescribe(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    JSValue o = JS_NewObject(ctx);
    if (!g_app) return o;
    const rp::audio::AudioDescription described =
        g_app->audio ? g_app->audio->describe() : rp::audio::AudioDescription{};
    JSValue backends = JS_NewArray(ctx);
    std::uint32_t at = 0;
    for (const auto& backend : described.backends) {
        JSValue b = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, b, "id", JS_NewString(ctx, backend.id.c_str()));
        JS_SetPropertyStr(ctx, b, "label", JS_NewString(ctx, backend.label.c_str()));
        JS_SetPropertyStr(ctx, b, "autoDriver", JS_NewString(ctx, backend.autoDriver.c_str()));
        JSValue drivers = JS_NewArray(ctx);
        std::uint32_t d = 0;
        for (const auto& driver : backend.drivers) JS_SetPropertyUint32(ctx, drivers, d++, driverObject(ctx, driver));
        JS_SetPropertyStr(ctx, b, "drivers", drivers);
        JS_SetPropertyUint32(ctx, backends, at++, b);
    }
    JS_SetPropertyStr(ctx, o, "backends", backends);
    JS_SetPropertyStr(ctx, o, "status", audioStatusObject(ctx, described.status, g_app->audio && g_app->audio->live()));
    // What this RUN's flags pin over the saved section (E12): the rows say so rather than showing a choice that is not
    // the one running.
    JSValue overrides = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, overrides, "backend", JS_NewString(ctx, g_app->backendOverride.c_str()));
    JS_SetPropertyStr(ctx, overrides, "blockSize", JS_NewInt32(ctx, g_app->blockSizeOverride));
    JS_SetPropertyStr(ctx, o, "overrides", overrides);
    return o;
}

// __rp_audioBackendDrivers(id): that backend's drivers and devices, on demand. THIS call is the sibling library's first
// use, which is why the Driver row asks for it when the user opens it and nothing asks at startup.
JSValue jsAudioBackendDrivers(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    JSValue out = JS_NewArray(ctx);
    // A run that pinned a backend has nothing to enumerate: asking would initialise a library the pin exists to keep
    // closed (the Zoom L6 rule), and no choice of its could ever take effect this run.
    if (!g_app || !g_app->audio || argc < 1 || !g_app->backendOverride.empty()) return out;
    const char* id = JS_ToCString(ctx, argv[0]);
    const std::string backend = id ? id : "";
    if (id) JS_FreeCString(ctx, id);
    std::uint32_t at = 0;
    for (const auto& driver : g_app->audio->backendDrivers(backend))
        JS_SetPropertyUint32(ctx, out, at++, driverObject(ctx, driver));
    return out;
}

// __rp_audioApply({ backend, driver, device, sampleRate, blockSize, outChannels }): the `audio` section's fields, which
// become AppState's request. While the startup rendezvous still holds the host (before main reaches its own point), this
// RECORDS the request and tells the rendezvous its word arrived: the device opens there, after the MIDI stream and the
// providers' restore, which is the order the app has always opened things in. A later call (the Apply row) is live.
JSValue jsAudioApply(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (!g_app || !g_app->audio || argc < 1 || !JS_IsObject(argv[0])) return JS_NULL;
    auto readString = [&](const char* key) {
        JSValue v = JS_GetPropertyStr(ctx, argv[0], key);
        // A missing key is not the string "undefined": JS_ToCString spells an undefined property that way, and a driver
        // of that name would mute the run.
        const char* s = (JS_IsUndefined(v) || JS_IsNull(v)) ? nullptr : JS_ToCString(ctx, v);
        std::string out = s ? s : "";
        if (s) JS_FreeCString(ctx, s);
        JS_FreeValue(ctx, v);
        return out;
    };
    // A missing key is NOT zero: JS_ToInt32 succeeds on undefined, so a section without a rate would otherwise request
    // 0 Hz. The fallback stands for a missing, non-numeric or non-positive value (design 6.4's validation).
    auto readInt = [&](const char* key, int fallback) {
        JSValue v = JS_GetPropertyStr(ctx, argv[0], key);
        std::int32_t n = 0;
        const bool ok = JS_IsUndefined(v) || JS_IsNull(v) ? false : JS_ToInt32(ctx, &n, v) == 0;
        JS_FreeValue(ctx, v);
        return ok && n > 0 ? static_cast<int>(n) : fallback;
    };
    AppState& a = *g_app;
    const std::string backend = readString("backend");
    a.reqBackend             = backend.empty() ? "auto" : backend;
    a.reqDriver              = readString("driver");
    a.reqOutputDevice        = readString("device");
    a.reqSampleRate          = readInt("sampleRate", a.reqSampleRate);
    a.reqBlockSize           = readInt("blockSize", a.reqBlockSize);
    const int channels       = readInt("outChannels", a.reqOutChannels);
    if (channels >= 2 && channels <= 8 && (channels % 2) == 0) a.reqOutChannels = channels;
    if (a.audioDeviceReady) applyAudioConfig(a); // a live change: the host is open, so it happens now
    a.audioApplied = true;
    return audioStatusObject(ctx, a.audio->status(), a.audio->live());
}

// __rp_audioStatus(): the status alone, for the UI frame and the muted row.
JSValue jsAudioStatus(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    if (!g_app || !g_app->audio) return JS_NULL;
    return audioStatusObject(ctx, g_app->audio->status(), g_app->audio->live());
}

}  // namespace

void installWindowHooks(JSContext* ctx) {
    JSValue g = JS_GetGlobalObject(ctx);
    auto bind = [&](const char* name, JSCFunction* fn, int len) {
        JS_SetPropertyStr(ctx, g, name, JS_NewCFunction(ctx, fn, name, len));
    };
    bind("__rp_quitWindow", jsQuitWindow, 0);
    bind("__rp_isWindowSizeControlled", jsIsWindowSizeControlled, 0);
    bind("__rp_setWindowSize", jsSetWindowSize, 2);
    bind("__rp_setWindowTitle", jsSetWindowTitle, 1);
    bind("__rp_openPath", jsOpenPath, 1);
    bind("__rp_audioDescribe", jsAudioDescribe, 0);
    bind("__rp_audioBackendDrivers", jsAudioBackendDrivers, 1);
    bind("__rp_audioApply", jsAudioApply, 1);
    bind("__rp_audioStatus", jsAudioStatus, 0);
    // Local transport (Instance menu > Transport) — standalone-only, like the audio/MIDI device seams.
    bind("__rp_getTransport", jsGetTransport, 0);
    bind("__rp_setTransport", jsSetTransport, 1);
    bind("__rp_setClockBpm", jsSetClockBpm, 1);
    // The MIDI stream (Settings > MIDI and the control surface's claim). RtMidi is compiled into retroplug-sdl, so the
    // seam always exists here; the plugin never binds these, so its rows and its transport are absent.
    bind("__rp_midiDescribe", jsMidiDescribe, 0);
    bind("__rp_midiApply", jsMidiApply, 2);
    // The OS-native file dialog — only where a desktop helper exists (zenity/kdialog/…). Where none does
    // (the muOS handheld, a bare container) we DON'T bind it: useFileBrowser sees no nativeHook and the
    // "File Dialogs: OS Native" toggle transparently stays in-app. The self-test forces the bind to prove it.
    if (retroplug::ui::NativeFileDialog::available() || std::getenv("RETROPLUG_SDL_DIALOG_SELFTEST"))
        bind("__rp_openFileBrowser", jsOpenFileBrowser, 6);
    // Mark this as a standalone host so the UI shows the "Exit" menu row (a DAW hides it).
    JS_SetPropertyStr(ctx, g, "__rp_isStandalone", JS_NewBool(ctx, 1));
    JS_FreeValue(ctx, g);
}

}  // namespace retroplug::sdl
