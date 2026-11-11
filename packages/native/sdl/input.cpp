#include "app.hpp"

namespace retroplug::sdl {


// Input: SDL keyboard and gamepad to the LVGL keypad indev and the JS buses, and the SDL event loop (docs/modular-
// graph-phase-f5b-design.md 5.5).

// ---- input translation ----------------------------------------------------------------------------

// Push an LVGL key onto the keypad queue AND emit the "key" JS bus (press+release), mirroring
// RenderCore::tapKey / PluginUI::onKeyboard. dpf is the DPF keycode the TS input layer expects.
// SDL's modifier state -> DPF's Modifier mask (dgl/Base.hpp), which is what the "key" bus carries.
// The bus is shared with the plugin host, so the mask has to mean the same thing on both sides.
namespace {
// Key translation: this unit's alone, and the only place an SDL keycode becomes an LVGL one.
std::uint32_t dpfModsFromSdl(std::uint16_t sdlMod) {
    std::uint32_t mods = 0;
    if (sdlMod & KMOD_SHIFT) mods |= 1U << 0U; // kModifierShift
    if (sdlMod & KMOD_CTRL)  mods |= 1U << 1U; // kModifierControl
    if (sdlMod & KMOD_ALT)   mods |= 1U << 2U; // kModifierAlt
    if (sdlMod & KMOD_GUI)   mods |= 1U << 3U; // kModifierSuper
    return mods;
}

void feedKey(AppState& a, std::uint32_t lvKey, std::uint32_t dpf, bool press, std::uint32_t mods) {
    JSContext* ctx = a.ui.getContext();
    if (press && lvKey != 0) a.input.keyQueue.push_back(lvKey);  // only real LVGL nav keys reach the keypad indev
    if (ctx) {
        // THREE args, matching PluginUI. DPF gives the unshifted code point ('a' for the A key whether or
        // not Shift is down), so the modifier mask is the only way the prompt text-input can apply
        // Shift -> uppercase - Menu.tsx's key handler takes it as `mod = 0` (NativeEvents in
        // useNativeEvent.ts types the channel). This emitted two args for a long time, so that default
        // silently made Shift a no-op in the standalone while it worked in the DAW.
        JSValue args[3] = {JS_NewUint32(ctx, dpf), JS_NewBool(ctx, press), JS_NewUint32(ctx, mods)};
        a.ui.emit("key", 3, args);
        for (JSValue& v : args) JS_FreeValue(ctx, v);
    }
}

// Map an SDL keysym to (LVGL key, DPF keycode). The DPF code is what the TS input layer (keyCodes.ts)
// expects on the "key" bus: arrows in the 0xE03x band, ShiftL/R at 0xE051/2, and — crucially — printable
// keys carry their ASCII code verbatim (dpfCodeToKeyName does String.fromCharCode). Only the navigation
// keys get a real LVGL keypad code (`lv`); everything else is bus-only (lv = 0) — game buttons, capture,
// and app actions all live on the "key" bus, not the keypad. Returns false for keys we don't translate.
//
// This must cover every key the default bindings use (bindingMap.ts): dpad = arrows, A = Z, B = X,
// Start = Enter, Select = ShiftL/ShiftR/Backspace — plus anything the user might rebind to. Previously only
// arrows/Enter/Esc were mapped, so A/B/Select (and any letter/modifier binding) never reached the emulator
// OR the key-capture editor.
bool mapSdlKey(SDL_Keycode k, std::uint32_t& lv, std::uint32_t& dpf) {
    lv = 0;
    switch (k) {
        case SDLK_UP:       lv = LV_KEY_UP;    dpf = 0xE036; return true;
        case SDLK_DOWN:     lv = LV_KEY_DOWN;  dpf = 0xE038; return true;
        case SDLK_RIGHT:    lv = LV_KEY_RIGHT; dpf = 0xE037; return true;
        case SDLK_LEFT:     lv = LV_KEY_LEFT;  dpf = 0xE035; return true;
        case SDLK_RETURN:   lv = LV_KEY_ENTER; dpf = 0x0D;   return true;
        case SDLK_KP_ENTER: lv = LV_KEY_ENTER; dpf = 0x0D;   return true;
        case SDLK_ESCAPE:   lv = LV_KEY_ESC;   dpf = 0x1B;   return true;
        // Bus-only keys (no keypad nav): the GB Select defaults + editor keys.
        case SDLK_LSHIFT:   dpf = 0xE051; return true;   // ShiftL
        case SDLK_RSHIFT:   dpf = 0xE052; return true;   // ShiftR
        case SDLK_BACKSPACE:dpf = 0x08;   return true;   // Backspace (Select default; clears a capture row)
        case SDLK_TAB:      dpf = 0x09;   return true;   // Tab (default Cycle-Instances app action)
        case SDLK_PAGEUP:   dpf = 0xE031; return true;   // menu coarse-step
        case SDLK_PAGEDOWN: dpf = 0xE032; return true;
        default:
            // Printable ASCII (letters/digits/punctuation/space): SDL's keycode IS the ASCII code, which is
            // exactly the DPF/DGL "key" bus convention. Covers A=Z, B=X and anything the user rebinds to.
            if (k >= 0x20 && k <= 0x7e) { dpf = static_cast<std::uint32_t>(k); return true; }
            return false;
    }
}

// Poll controllers once per frame → re-emit each transition onto the gamepad-* JS bus (exactly as
// PluginUI::pumpGamepad). The gamepad drives BOTH menu navigation and in-game input purely through this
// bus: the React Menu has its own gamepad handler, and the design contract is that the gamepad bus never
// reaches the LVGL keypad indev (Menu.tsx). So we must NOT also translate gamepad → keypad here — doing
// so double-drives the menu (every move counted twice) while leaving in-game input single (the keypad
// keys hit the idle focus sink). Keyboard still feeds the keypad (mapSdlKey/feedKey) for desktop use.
}  // namespace

void pumpGamepad(AppState& a) { pumpGamepadInto(a.gamepad, a.ui, a.ui.getContext()); }

void handleEvents(AppState& a) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
            // OS window-close (window button / WM / Ctrl-C) → route through the unsaved-changes guard, exactly
            // as the DPF standalone's PluginUI::onClose does (and the Exit menu row already does via JS). The
            // JS guard returns true to veto (it raises the save prompt and later calls __rp_quitWindow itself
            // once the user confirms Save/Discard); false = clean project, quit now. Without this, closing a
            // dirty project by the window button would skip the prompt and lose work.
            case SDL_QUIT: {
                // No allowClose_ latch here: jsQuitWindow just clears `running`, so this never re-enters.
                if (!jsCloseVetoed(a.ui.getContext())) a.running = false;
                break;
            }
            // Desktop drag-and-drop → the "file-drop" JS bus (mirrors PluginUI::uiFileDropped). SDL2's
            // SDL_DropEvent carries no coordinates (the x/y fields are SDL3-only) and one path per event, so
            // we batch across begin→complete and read the cursor ourselves for the tile hit-test.
            case SDL_DROPBEGIN: a.dropBatch.clear(); break;
            case SDL_DROPFILE:
                if (ev.drop.file) { a.dropBatch.emplace_back(ev.drop.file); SDL_free(ev.drop.file); }
                break;
            case SDL_DROPCOMPLETE:
                if (JSContext* ctx = a.ui.getContext(); ctx && !a.dropBatch.empty()) {
                    std::string joined;
                    for (const std::string& p : a.dropBatch) { if (!joined.empty()) joined += '\n'; joined += p; }
                    int mx = 0, my = 0;
                    SDL_GetMouseState(&mx, &my); // window-pixel space — the coords hitTestTile expects (best-effort)
                    JSValue args[3] = {JS_NewString(ctx, joined.c_str()),
                                       JS_NewFloat64(ctx, mx), JS_NewFloat64(ctx, my)};
                    a.ui.emit("file-drop", 3, args);
                    for (JSValue& v : args) JS_FreeValue(ctx, v);
                }
                a.dropBatch.clear();
                break;
            case SDL_KEYDOWN:
            case SDL_KEYUP: {
                std::uint32_t lv, dpf;
                if (mapSdlKey(ev.key.keysym.sym, lv, dpf))
                    feedKey(a, lv, dpf, ev.type == SDL_KEYDOWN, dpfModsFromSdl(ev.key.keysym.mod));
                break;
            }
            // The WM/user resized the window (a drag-grip resize, or a tiling compositor tiling us) → adopt the
            // new size + run the tiling-clamp detection. SIZE_CHANGED fires for both API- and WM-driven changes.
            case SDL_WINDOWEVENT:
                if (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
                    onWindowSizeChanged(a, static_cast<std::uint32_t>(ev.window.data1),
                                        static_cast<std::uint32_t>(ev.window.data2));
                break;
            case SDL_MOUSEMOTION: a.input.mousePos = {ev.motion.x, ev.motion.y}; break;
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
                a.input.mousePos = {ev.button.x, ev.button.y};
                a.input.mouseDown = ev.type == SDL_MOUSEBUTTONDOWN;
                break;
            // Wheel -> scroll the scrollable ancestor under the cursor (the overflowing menu), the same
            // shared hit-test scroll PluginUI::onScroll runs for the DPF editor. Without this the wheel was
            // simply dead in the standalone: LVGL has no wheel handling of its own. We're on the UI thread
            // here, which is also the one running lv_timer_handler, so scrolling from here is safe.
            case SDL_MOUSEWHEEL: {
                float wx = static_cast<float>(ev.wheel.x), wy = static_cast<float>(ev.wheel.y);
#if SDL_VERSION_ATLEAST(2, 0, 18)
                // Trackpads / high-resolution wheels report sub-notch deltas here (the integer x/y are the
                // accumulated whole notches, and stay 0 until a full one builds up).
                if (ev.wheel.preciseX != 0.0f || ev.wheel.preciseY != 0.0f) {
                    wx = ev.wheel.preciseX;
                    wy = ev.wheel.preciseY;
                }
#endif
                if (ev.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) { wx = -wx; wy = -wy; }
                // SDL2's wheel event carries no position (mouseX/mouseY are SDL3-only). Use the
                // motion-tracked cursor rather than SDL_GetMouseState: it's the same point the pointer indev
                // hovers with, so the wheel scrolls exactly what's highlighted under it (and a synthetic
                // motion event can drive it headlessly). Window-pixel space == LVGL display space.
                const lv_point_t& p = a.input.mousePos;
                const bool scrolled = retroplug::ui::scrollAtPoint(p.x, p.y, wx, wy);
                if (a.logWheel)
                    std::fprintf(stderr, "[retroplug-sdl] wheel: notches=(%.2f,%.2f) at (%d,%d) scrolled=%d\n",
                                 wx, wy, p.x, p.y, static_cast<int>(scrolled));
                break;
            }
            default: break;
        }
    }
}

}  // namespace retroplug::sdl
