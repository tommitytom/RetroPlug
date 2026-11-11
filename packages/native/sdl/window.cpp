#include "app.hpp"

namespace retroplug::sdl {


// The SDL window: the LVGL glue LVGL calls back into, setupSdl's video and game controller, the UI mount, the frame's
// present, and the window-size latches a tiling WM makes necessary (docs/modular-graph-phase-f5b-design.md 5.5).

// LVGL is built LV_GLOBAL_CUSTOM: the application must supply lv_global_default(). In the plugin DPF's
// LVGLWidget provides a per-instance one; standalone (single window, single runtime) we provide one
// process-wide instance, zero-initialized like DPF's calloc — lv_init() populates it. (Same pattern as
// RenderCore.cpp.)
extern "C" lv_global_t* lv_global_default(void) {
    static lv_global_t* g = static_cast<lv_global_t*>(std::calloc(1, sizeof(lv_global_t)));
    return g;
}

// ---- LVGL glue (mirrors RenderCore's non-GL subset) -----------------------------------------------

// Flush: in LV_DISPLAY_RENDER_MODE_DIRECT the draw buffer already holds the full rendered frame; we just
// record which region changed (union across the refresh's flush calls) so the loop uploads + presents
// only that rect, and skips presenting entirely when nothing changed.
namespace {
// This unit's own glue: LVGL's display flush callback and its tick. Nothing outside window.cpp names them.
void flushCb(lv_display_t* disp, const lv_area_t* area, uint8_t*) {
    if (g_app && area) {
        if (!g_app->dirty) { g_app->dirtyRect = *area; g_app->dirty = true; }
        else {
            g_app->dirtyRect.x1 = std::min(g_app->dirtyRect.x1, area->x1);
            g_app->dirtyRect.y1 = std::min(g_app->dirtyRect.y1, area->y1);
            g_app->dirtyRect.x2 = std::max(g_app->dirtyRect.x2, area->x2);
            g_app->dirtyRect.y2 = std::max(g_app->dirtyRect.y2, area->y2);
        }
    }
    lv_display_flush_ready(disp);
}

// Keypad + pointer indev read callbacks live in host/ui/SoftwareLvglDisplay.hpp (shared with the test
// harness). AppState embeds retroplug::ui::LvInputState and hands &input to both indevs as driver_data.

// LVGL's tick source: real wall-clock ms so timers/animations run in real time.
std::uint32_t sdlTicksCb(void) { return SDL_GetTicks(); }
}  // namespace

bool setupUi(AppState& a) {
    lv_init();
    lv_tick_set_cb(sdlTicksCb);

    // Display + indevs: the shared software-LVGL scaffold (host/ui/SoftwareLvglDisplay.hpp), also used by the
    // headless test harness. SDL supplies its own dirty-tracking flush (present() uploads only the changed rect).
    auto sd = retroplug::ui::createSoftwareDisplay(a.width, a.height, a.drawBuf, flushCb, &a.input);
    if (!sd.display) return false;
    a.display = sd.display;
    a.group   = sd.group;
    a.keypad  = sd.keypad;
    a.pointer = sd.pointer;

    if (!a.ui.init()) { std::fprintf(stderr, "[retroplug-sdl] LvglJsEngine init failed\n"); return false; }

    JSContext* ctx = a.ui.getContext();
    if (ctx && !hasGlobalFunction(ctx, "__rp_mountUI")) {
        if (a.ui.evalModuleBytecode(rp_ui_bundle, rp_ui_bundle_size) != 0) {
            std::fprintf(stderr, "[retroplug-sdl] failed to eval UI bundle\n");
            return false;
        }
    }
    a.ui.attachDisplay(); // → __rp_mountUI

    // Black screen, stripped chrome, root pinned to 100% — same as PluginUI (shared with the test harness).
    retroplug::ui::applyChromelessScreen();
    if (ctx) installWindowHooks(ctx);

    // Headless proof of the OS-dialog seam. Reports whether __rp_openFileBrowser is bound + whether a desktop
    // helper is available; if one is (a real or a fake zenity/kdialog in PATH), it drives a real pfd request
    // whose pick pollFileDialog then delivers + logs — exercising the whole request→helper→parse→deliver path.
    if (std::getenv("RETROPLUG_SDL_DIALOG_SELFTEST")) {
        const bool bound = ctx && hasGlobalFunction(ctx, "__rp_openFileBrowser");
        const bool avail = retroplug::ui::NativeFileDialog::available();
        std::fprintf(stderr, "[retroplug-sdl] dialog self-test: __rp_openFileBrowser %s, helper %s\n",
                     bound ? "BOUND" : "MISSING", avail ? "available" : "absent");
        if (avail) a.nativeDialog.request("Self-test", "*.gb", false, "", "/tmp", false);
    }
    return true;
}

bool setupSdl(AppState& a) {
    SDL_SetMainReady(); // required when SDL_MAIN_HANDLED is set (see the <SDL.h> include note)
    // A stable application name → the Wayland app_id / X11 WM_CLASS, so a tiling WM's window rules can target
    // RetroPlug (e.g. Hyprland `windowrulev2 = tile, class:(RetroPlug)`). Set before SDL_Init.
    SDL_SetHint(SDL_HINT_APP_NAME, "RetroPlug");
    // Audio is PortAudio now (not SDL) — init only video + gamepad here.
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        std::fprintf(stderr, "[retroplug-sdl] SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    a.fullscreen = std::getenv("RETROPLUG_SDL_FULLSCREEN") != nullptr;
    // Resizable (not a fixed-size toplevel) so a tiling WM tiles the window instead of auto-floating it, and a
    // floating window can be dragged to any size — the DPF standalone is resizable for exactly this reason. A
    // fullscreen handheld ignores the flag. The min-size floor mirrors the plugin's setGeometryConstraints.
    Uint32 winFlags = a.fullscreen ? SDL_WINDOW_FULLSCREEN : SDL_WINDOW_RESIZABLE;
    a.window = SDL_CreateWindow("RetroPlug", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                a.width, a.height, winFlags);
    if (!a.window) { std::fprintf(stderr, "[retroplug-sdl] SDL_CreateWindow failed: %s\n", SDL_GetError()); return false; }
    SDL_SetWindowMinimumSize(a.window, static_cast<int>(kMinWindowW), static_cast<int>(kMinWindowH));
    a.debugResize = std::getenv("RETROPLUG_DEBUG_RESIZE") != nullptr;

    // Seed the geometry-ownership state: fullscreen is WM-controlled from the start; arm the clamp baseline with
    // the initial size so the FIRST compositor resize that differs (a tile) is recognised as a WM takeover.
    //
    // Wayland is WM-controlled too, always. On X11 a resize request is VERIFIABLE: SDL_SetWindowSize round-trips
    // it and reports back the geometry the WM actually granted (X11_SetWindowSize waits on the server, then
    // sends RESIZED with the real size), so a WM that refuses is detected and adopted by onWindowSizeChanged.
    // Wayland has no such feedback - xdg-shell has no client resize request at all: we simply commit the size
    // we want, and the compositor either goes along with it (mutter/kwin) or keeps its own box and SCALES our
    // now-mismatched surface into it, sending nothing either way. Under a compositor that owns geometry
    // (Hyprland, sway, every tiling compositor) that scaling is what stretched a freshly loaded tile across the
    // whole window until the next real configure - a user resize - put surface and window back in step. Since
    // "honored" and "ignored" are indistinguishable from here, we don't guess: the compositor owns the window
    // and the UI fits its grid via zoom. RETROPLUG_SDL_FIT_WINDOW=1 opts back into driving the window size on a
    // compositor known to honor it.
    const char* videoDriver = SDL_GetCurrentVideoDriver();
    const bool  wayland     = videoDriver && SDL_strcmp(videoDriver, "wayland") == 0;
    const bool  forceFit    = std::getenv("RETROPLUG_SDL_FIT_WINDOW") != nullptr;
    a.wmControlled = a.fullscreen || (wayland && !forceFit);
    a.requestedW = a.width;
    a.requestedH = a.height;
    if (a.debugResize)
        std::fprintf(stderr, "[retroplug-sdl] video driver '%s' - window geometry %s\n",
                     videoDriver ? videoDriver : "?", a.wmControlled ? "owned by the WM" : "ours to size");
    a.renderer = SDL_CreateRenderer(a.window, -1, SDL_RENDERER_SOFTWARE);
    if (!a.renderer) { std::fprintf(stderr, "[retroplug-sdl] SDL_CreateRenderer failed: %s\n", SDL_GetError()); return false; }
    a.texture = SDL_CreateTexture(a.renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                  a.width, a.height);
    if (!a.texture) { std::fprintf(stderr, "[retroplug-sdl] SDL_CreateTexture failed: %s\n", SDL_GetError()); return false; }

    // No audio device is opened here: setupSdl initialises video and the game controller, and AudioHost opens the
    // device after the persisted settings are read (main's audio step), so a run that names the null backend touches no
    // audio library at all. The backends initialise their own library at first use.
    return true;
}

void present(AppState& a) {
    if (!a.dirty) return; // nothing redrew this frame — leave the screen as-is (huge idle CPU saving)
    const int stride = static_cast<int>(lv_draw_buf_width_to_stride(a.width, lv_display_get_color_format(a.display)));
    // Upload only the changed region (clamped) from the draw buffer; the streaming texture keeps the rest.
    int x1 = std::max(0, (int)a.dirtyRect.x1), y1 = std::max(0, (int)a.dirtyRect.y1);
    int x2 = std::min((int)a.width - 1, (int)a.dirtyRect.x2), y2 = std::min((int)a.height - 1, (int)a.dirtyRect.y2);
    if (x2 >= x1 && y2 >= y1) {
        SDL_Rect rect{x1, y1, x2 - x1 + 1, y2 - y1 + 1};
        const std::uint8_t* src = a.drawBuf.data() + static_cast<std::size_t>(y1) * stride + static_cast<std::size_t>(x1) * 4;
        SDL_UpdateTexture(a.texture, &rect, src, stride);
    }
    SDL_RenderCopy(a.renderer, a.texture, nullptr, nullptr);
    SDL_RenderPresent(a.renderer);
    a.dirty = false;
}

// Adopt an actual window size — resize every dimension-tracking resource in lockstep: the SDL window (only
// when WE initiated it; a WM-driven change already sized the window), the LVGL display resolution + its
// DIRECT-mode draw buffer, and the streaming SDL texture (a STREAMING texture can't resize in place —
// recreate it). Then force a full-window redraw and tell the UI so it re-lays-out. This inlines what DPF's
// platform resize callback does for the plugin (LVGL::recreateTextureData + emit "resize").
void applyWindowSize(AppState& a, std::uint32_t w, std::uint32_t h, bool fromWm) {
    if (!a.window || !a.display || !a.renderer) return;
    if (w == a.width && h == a.height && a.texture) return; // already at this size — nothing to realloc

    if (!fromWm) SDL_SetWindowSize(a.window, static_cast<int>(w), static_cast<int>(h)); // ask the WM

    // LVGL display: set the new resolution, then re-point it at a freshly sized DIRECT buffer.
    const lv_color_format_t cf = lv_display_get_color_format(a.display);
    const std::uint32_t stride = lv_draw_buf_width_to_stride(w, cf);
    a.drawBuf.assign(static_cast<std::size_t>(stride) * h, 0);
    lv_display_set_resolution(a.display, w, h);
    lv_display_set_buffers(a.display, a.drawBuf.data(), nullptr, static_cast<std::uint32_t>(a.drawBuf.size()), LV_DISPLAY_RENDER_MODE_DIRECT);

    // SDL streaming texture: recreate at the new size (can't be resized in place).
    if (a.texture) SDL_DestroyTexture(a.texture);
    a.texture = SDL_CreateTexture(a.renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                  static_cast<int>(w), static_cast<int>(h));

    a.width = w;
    a.height = h;
    a.dirty = true; // present the whole new surface next frame (lv_timer_handler redraws into the new buffer)
    a.dirtyRect = {0, 0, static_cast<std::int32_t>(w) - 1, static_cast<std::int32_t>(h) - 1};

    if (JSContext* ctx = a.ui.getContext()) { // mirror PluginUI::onResize → the UI re-reads Dimensions.window
        JSValue args[2] = {JS_NewUint32(ctx, w), JS_NewUint32(ctx, h)};
        a.ui.emit("resize", 2, args);
        JS_FreeValue(ctx, args[0]);
        JS_FreeValue(ctx, args[1]);
    }
}

// The __rp_setWindowSize seam: the UI asks the WM for a w×h window (grow/shrink to the grid). Mirrors
// PluginUI::requestWindowSize — record the request as the clamp baseline, but don't fight a WM that owns
// geometry (a tiling compositor or a fullscreen handheld); there the UI fits its grid via zoom instead.
void requestWindowSize(AppState& a, std::uint32_t w, std::uint32_t h) {
    // Clamp to the window minimum FIRST, because SDL_SetWindowSize does: a 1x/2x grid (160x144 / 320x288) is
    // below the floor, so the window can only ever become 480x432. Applying the raw request would size the
    // LVGL surface to something the window never takes - and a surface that doesn't match the window is
    // exactly what a compositor scales to fit, which is how a freshly loaded tile ended up stretched across
    // the whole window. The clamped size also has to be what we RECORD, or it comes back through
    // onWindowSizeChanged as "a size we never asked for", i.e. a tiling takeover, wrongly latching
    // wmControlled and killing fit-to-grid for the rest of the session.
    w = std::max(w, kMinWindowW);
    h = std::max(h, kMinWindowH);
    a.requestedW = w; // recorded even when WM-controlled, so onWindowSizeChanged can still latch sizeHonored
    a.requestedH = h;
    if (a.wmControlled) {
        if (a.debugResize)
            std::fprintf(stderr, "[retroplug-sdl] resize request %ux%u ignored (WM owns geometry)\n", w, h);
        return;
    }
    applyWindowSize(a, w, h, /*fromWm=*/false);
}

// SDL_WINDOWEVENT_SIZE_CHANGED: the actual window size changed (our SDL_SetWindowSize, a user drag, or a
// tiling-WM tile). Mirrors PluginUI::onResize — latch sizeHonored when the WM grants a size we asked for
// (proof the window is floating, ours to size), and detect a tiling takeover: a size we never requested,
// before any request was ever honored, means the compositor owns geometry, so latch wmControlled and stop
// driving SDL_SetWindowSize (fullscreen is already wmControlled). Then adopt the new size.
void onWindowSizeChanged(AppState& a, std::uint32_t w, std::uint32_t h) {
    if (a.requestedW != 0 && a.requestedH != 0 && w == a.requestedW && h == a.requestedH) a.sizeHonored = true;
    if (!a.wmControlled && !a.sizeHonored && a.requestedW != 0 && a.requestedH != 0 &&
        (w != a.requestedW || h != a.requestedH)) {
        a.wmControlled = true;
        if (a.debugResize)
            std::fprintf(stderr, "[retroplug-sdl] WM owns geometry (tiled): got %ux%u, never asked for it\n", w, h);
    }
    if (a.debugResize)
        std::fprintf(stderr, "[retroplug-sdl] onResize %ux%u (req %ux%u wmC=%d honored=%d)\n", w, h, a.requestedW,
                     a.requestedH, (int)a.wmControlled, (int)a.sizeHonored);
    applyWindowSize(a, w, h, /*fromWm=*/true);
}

bool writeScreenshot(AppState& a, const std::string& path) {
    if (!a.display || a.drawBuf.empty()) return false;
    const std::uint32_t w = a.width, h = a.height;
    const std::uint32_t stride = lv_draw_buf_width_to_stride(w, lv_display_get_color_format(a.display));
    std::vector<unsigned char> rgb(static_cast<std::size_t>(w) * h * 3);
    for (std::uint32_t y = 0; y < h; ++y)
        for (std::uint32_t x = 0; x < w; ++x) {
            const std::uint8_t* px = a.drawBuf.data() + static_cast<std::size_t>(y) * stride + x * 4;
            unsigned char* out = rgb.data() + (static_cast<std::size_t>(y) * w + x) * 3;
            out[0] = px[2]; out[1] = px[1]; out[2] = px[0]; // ARGB8888 (B,G,R,A) -> R,G,B
        }
    const unsigned err = lodepng_encode24_file(path.c_str(), rgb.data(), w, h);
    if (err) { std::fprintf(stderr, "[retroplug-sdl] screenshot error %u\n", err); return false; }
    std::fprintf(stderr, "[retroplug-sdl] screenshot -> %s (%ux%u)\n", path.c_str(), w, h);
    return true;
}

// ---- screenshot (headless proof) ------------------------------------------------------------------
// Dump the display's composited draw buffer (the exact frame presented, including top-layer overlays
// like the file browser) so a headless run (SDL_VIDEODRIVER=offscreen) can screenshot for a test.
bool writeScreenshot(AppState& a, const std::string& path);

// True if the rendered frame is a single flat colour (nothing drew) — the CI smoke's "the UI actually
// rendered, not just booted" gate. Compares the draw buffer as 32-bit pixels against the first.
bool frameIsBlank(const AppState& a) {
    if (a.drawBuf.size() < 8) return true;
    const auto* px = reinterpret_cast<const std::uint32_t*>(a.drawBuf.data());
    const std::size_t n = a.drawBuf.size() / 4;
    for (std::size_t i = 1; i < n; ++i) if (px[i] != px[0]) return false;
    return true;
}

}  // namespace retroplug::sdl
