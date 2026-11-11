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
// external master (so the two sources can't disagree about what a legal tempo is). Persisted to audio.json.
JSValue jsSetClockBpm(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    double bpm = 0.0;
    if (g_app && argc >= 1 && JS_ToFloat64(ctx, &bpm, argv[0]) == 0) {
        bpm = std::clamp(bpm, MidiClockSync::kMinBpm, MidiClockSync::kMaxBpm);
        g_app->clockBpmRequest.store(static_cast<float>(bpm), std::memory_order_relaxed);
        saveAudioConfig(*g_app);
    }
    return JS_UNDEFINED;
}

// __rp_getMidiConfig(): { inputs: string[], outputs: string[], selectedInput, selectedOutput } - the live hardware MIDI
// ports + the current device selection, for the Settings > MIDI pickers. One name each, as midi.json still spells the
// selection until the project carries it ("" = None, "*" = every device). Standalone-only; absent in the plugin (DAW
// routes MIDI).
JSValue jsGetMidiConfig(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    JSValue o = JS_NewObject(ctx);
    if (g_app) {
        auto toArray = [&](const std::vector<std::string>& names) {
            JSValue a = JS_NewArray(ctx);
            for (std::uint32_t i = 0; i < names.size(); ++i)
                JS_SetPropertyUint32(ctx, a, i, JS_NewString(ctx, names[i].c_str()));
            return a;
        };
        JS_SetPropertyStr(ctx, o, "inputs",  toArray(g_app->midi.listInputs()));
        JS_SetPropertyStr(ctx, o, "outputs", toArray(g_app->midi.listOutputs()));
        JS_SetPropertyStr(ctx, o, "selectedInput",  JS_NewString(ctx, g_app->midiInputJson.c_str()));
        JS_SetPropertyStr(ctx, o, "selectedOutput", JS_NewString(ctx, g_app->midiOutputJson.c_str()));
    }
    return o;
}

// __rp_setMidiInput(name) / __rp_setMidiOutput(name): choose a hardware device by port name ("" = None, "*" = every
// device on the input side). Applies immediately, without stopping audio, and persists to midi.json.
JSValue jsSetMidiInput(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (g_app && argc >= 1) {
        const char* s = JS_ToCString(ctx, argv[0]);
        applyMidiSelection(*g_app, true, s ? s : "");
        if (s) JS_FreeCString(ctx, s);
    }
    return JS_UNDEFINED;
}
JSValue jsSetMidiOutput(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (g_app && argc >= 1) {
        const char* s = JS_ToCString(ctx, argv[0]);
        applyMidiSelection(*g_app, false, s ? s : "");
        if (s) JS_FreeCString(ctx, s);
    }
    return JS_UNDEFINED;
}

// __rp_getAudioConfig(): the shape the Settings > Audio rows read while this is the shim (audioDraft.ts,
// nativeHooks.ts): { sampleRate, blockSize, outChannels, driver, drivers, devicesByDriver, defaultByDriver, autoDriver,
// device, muted, error }.
//
//  - `drivers` is "Auto" plus one LABEL per driver of the backend IN FORCE ("PortAudio: PipeWire", "SDL: pipewire"):
//    the backend names itself because two backends may call a driver alike, and only the backend in force is enumerated
//    (asking another is its library's first use - the Zoom L6 rule).
//  - `driver` is the SELECTED label: "Auto" when the choice is automatic, else the label of the chosen pair.
//  - `devicesByDriver` / `defaultByDriver` are keyed by those labels (plus "Auto" for the automatic driver), and
//    `defaultByDriver` is what an empty device selection resolves to, which the row shows beside "Default".
//  - `autoDriver` is the driver label "Auto" currently resolves to, so the row can read "Auto (PipeWire)".
//  - `device` is the CHOSEN name, "" for the driver's default: the RESOLVED device is the status's, and feeding it back
//    as the choice would pin the concrete sink the session default happens to be today.
JSValue jsGetAudioConfig(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    JSValue o = JS_NewObject(ctx);
    if (g_app) {
        auto toArray = [&](const std::vector<std::string>& names) {
            JSValue a = JS_NewArray(ctx);
            for (std::uint32_t i = 0; i < names.size(); ++i)
                JS_SetPropertyUint32(ctx, a, i, JS_NewString(ctx, names[i].c_str()));
            return a;
        };
        JS_SetPropertyStr(ctx, o, "sampleRate",  JS_NewInt32(ctx, g_app->reqSampleRate));
        JS_SetPropertyStr(ctx, o, "blockSize",   JS_NewInt32(ctx, g_app->reqBlockSize));
        JS_SetPropertyStr(ctx, o, "outChannels", JS_NewInt32(ctx, g_app->reqOutChannels));
        const rp::audio::AudioDescription described =
            g_app->audio ? g_app->audio->describe() : rp::audio::AudioDescription{};

        JSValue     drivers   = JS_NewArray(ctx);
        JSValue     byDriver  = JS_NewObject(ctx);
        JSValue     defaults  = JS_NewObject(ctx);
        std::string selected  = rp::audio::kAutoDriverLabel;
        std::string autoLabel;
        std::uint32_t at = 0;
        JS_SetPropertyUint32(ctx, drivers, at++, JS_NewString(ctx, rp::audio::kAutoDriverLabel));
        for (const auto& backend : described.backends) {
            if (backend.id == "null") continue;
            const std::string autoKey =
                backend.autoDriver.empty()
                    ? std::string()
                    : rp::audio::driverChoiceLabel(backend.label,
                                                   [&] {
                                                       for (const auto& d : backend.drivers)
                                                           if (d.id == backend.autoDriver) return d.label;
                                                       return backend.autoDriver;
                                                   }());
            for (const auto& driver : backend.drivers) {
                const std::string label = rp::audio::driverChoiceLabel(backend.label, driver.label);
                JS_SetPropertyUint32(ctx, drivers, at++, JS_NewString(ctx, label.c_str()));
                JS_SetPropertyStr(ctx, byDriver, label.c_str(), toArray(driver.devices.names));
                JS_SetPropertyStr(ctx, defaults, label.c_str(), JS_NewString(ctx, driver.devices.defaultName.c_str()));
                if (g_app->reqBackend == backend.id && g_app->reqDriver == driver.id) selected = label;
                if (driver.id == backend.autoDriver) {
                    // "Auto" resolves to this driver: the same devices and default, under the row the user picks.
                    JS_SetPropertyStr(ctx, byDriver, rp::audio::kAutoDriverLabel, toArray(driver.devices.names));
                    JS_SetPropertyStr(ctx, defaults, rp::audio::kAutoDriverLabel,
                                      JS_NewString(ctx, driver.devices.defaultName.c_str()));
                    autoLabel = driver.label;
                }
            }
            if (!autoKey.empty()) autoLabel = autoLabel.empty() ? autoKey : autoLabel;
        }
        if (g_app->reqBackend == "auto") selected = rp::audio::kAutoDriverLabel;
        JS_SetPropertyStr(ctx, o, "drivers", drivers);
        JS_SetPropertyStr(ctx, o, "driver", JS_NewString(ctx, selected.c_str()));
        JS_SetPropertyStr(ctx, o, "devicesByDriver", byDriver);
        JS_SetPropertyStr(ctx, o, "defaultByDriver", defaults);
        // The DRIVER's label, as the row shows it beside "Auto" ("Auto (PipeWire)"): what the row's bracket reads.
        JS_SetPropertyStr(ctx, o, "autoDriver", JS_NewString(ctx, autoLabel.c_str()));
        JS_SetPropertyStr(ctx, o, "device",  JS_NewString(ctx, g_app->reqOutputDevice.c_str()));
        JS_SetPropertyStr(ctx, o, "muted",   JS_NewBool(ctx, !described.status.error.empty()));
        JS_SetPropertyStr(ctx, o, "error",   JS_NewString(ctx, described.status.error.c_str()));
    }
    return o;
}

// __rp_setAudioConfig(sampleRate, blockSize[, outChannels[, driverLabel[, device]]]): apply a live device change through
// AudioHost, which stops the stream, takes the engine back, opens the new request, re-rates to what the DEVICE obtained
// and starts it - one pass, never a partial reconfigure. The chosen values are persisted (audio.json is the shim until
// the TypeScript half owns the section).
//
// `driverLabel` is a label from `drivers` above: "Auto"/"" is the automatic choice, and "<Backend>: <driver>" is that
// pair (rp::audio::choiceForDriverLabel). An unknown label keeps the choice in force, so an Apply that changed only the
// rate cannot mute the run by naming a backend that does not exist. outChannels is optional (an older bundle omits
// it), and 2/4/6/8 - anything else keeps the current count.
JSValue jsSetAudioConfig(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (g_app && argc >= 2) {
        int sr = 0, bs = 0, ch = g_app->reqOutChannels;
        JS_ToInt32(ctx, &sr, argv[0]);
        JS_ToInt32(ctx, &bs, argv[1]);
        if (argc >= 3) {
            int c = 0;
            JS_ToInt32(ctx, &c, argv[2]);
            if (c >= 2 && c <= 8 && (c % 2) == 0) ch = c;
        }
        std::string device = g_app->reqOutputDevice;
        if (argc >= 5) {
            const char* s = JS_ToCString(ctx, argv[4]);
            device        = s ? s : "";
            if (s) JS_FreeCString(ctx, s);
        }
        if (argc >= 4) {
            const char* s = JS_ToCString(ctx, argv[3]);
            const std::string label = s ? s : "";
            if (s) JS_FreeCString(ctx, s);
            // The row's label, mapped to the (backend, driver) an apply takes. A backend's library comes up here, which
            // is a real use: the row offered it because the user asked for it.
            const rp::audio::AudioDescription described =
                g_app->audio ? g_app->audio->describe() : rp::audio::AudioDescription{};
            const rp::audio::AudioChoice current{g_app->reqBackend, g_app->reqDriver};
            const rp::audio::AudioChoice choice = rp::audio::choiceForDriverLabel(label, described, current);
            g_app->reqBackend                   = choice.backend;
            g_app->reqDriver                    = choice.driver;
            // The row's choice is the SAVED one; a run's --audio-backend still decides what this run opens.
        }
        if (sr > 0 && bs > 0) reconfigureAudio(*g_app, sr, bs, ch, device);
    }
    return JS_UNDEFINED;
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
    bind("__rp_getAudioConfig", jsGetAudioConfig, 0);
    bind("__rp_setAudioConfig", jsSetAudioConfig, 2);
    // Local transport (Instance menu > Transport) — standalone-only, like the audio/MIDI device seams.
    bind("__rp_getTransport", jsGetTransport, 0);
    bind("__rp_setTransport", jsSetTransport, 1);
    bind("__rp_setClockBpm", jsSetClockBpm, 1);
    // MIDI device selection (Settings > MIDI). RtMidi is compiled into retroplug-sdl, so the seam always
    // exists here; the plugin never binds these (hasMidiConfig() is false there → the submenu is hidden).
    bind("__rp_getMidiConfig", jsGetMidiConfig, 0);
    bind("__rp_setMidiInput", jsSetMidiInput, 1);
    bind("__rp_setMidiOutput", jsSetMidiOutput, 1);
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
