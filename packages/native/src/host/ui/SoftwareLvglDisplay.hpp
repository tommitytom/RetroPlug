#pragma once

// Shared headless/software LVGL scaffold for the non-DPF UI hosts: the SDL2 standalone
// (packages/native/sdl/main.cpp) and the headless UI-test harness (packages/native/test/ui/RenderCore.cpp).
// Both boot the SAME React/LVGL bundle on a CPU draw buffer with keypad + pointer indevs; this header is the
// one copy of that setup so the two can't drift. It replicates only the cheap, non-GL subset of DPF's
// LVGLWidget (deps/dpf-widgets/generic/LVGL.cpp).
//
// The caller still owns the parts that legitimately differ: lv_init() / lv_tick_set_cb (global — a simulated
// clock for tests vs SDL_GetTicks for the app), the draw buffer's storage + lifetime (SDL resizes it), the
// flush callback (a no-op snapshot sink for tests vs dirty-rect tracking for SDL present), and teardown.

#include <cstdint>
#include <deque>
#include <vector>

extern "C" {
    #include "lvgl.h"
}

#include "native/components/component.hpp"  // GetWindowInstance() — the lv_binding_js React-tree root

namespace retroplug::ui {

// Synthetic input the keypad/pointer indev read callbacks pull from. Owned by whatever owns the display
// (the SDL host's AppState, RenderCore) and handed to both indevs as driver_data.
struct LvInputState {
    std::deque<std::uint32_t> keyQueue;   // pending LVGL keypad codes (nav keys)
    lv_point_t mousePos{0, 0};
    bool       mouseDown = false;
};

// Keypad indev: pop one queued LVGL key per read (PRESSED while queued, then RELEASED), like dpf-widgets'
// keyBuffer. driver_data == LvInputState*.
inline void keypadReadCb(lv_indev_t* indev, lv_indev_data_t* data) {
    auto* in = static_cast<LvInputState*>(lv_indev_get_driver_data(indev));
    if (in && !in->keyQueue.empty()) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->key   = in->keyQueue.front();
        in->keyQueue.pop_front();
        data->continue_reading = !in->keyQueue.empty();
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// Pointer indev: report the synthetic cursor position + button state. driver_data == LvInputState*.
inline void pointerReadCb(lv_indev_t* indev, lv_indev_data_t* data) {
    auto* in = static_cast<LvInputState*>(lv_indev_get_driver_data(indev));
    if (!in) { data->state = LV_INDEV_STATE_RELEASED; return; }
    data->point = in->mousePos;
    data->state = in->mouseDown ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

struct SoftwareDisplay {
    lv_display_t* display = nullptr;
    lv_group_t*   group = nullptr;
    lv_indev_t*   keypad = nullptr;
    lv_indev_t*   pointer = nullptr;
};

// Create a CPU DIRECT-mode lv_display (drawing into `drawBuf`, which the caller owns + sizes) plus the
// default group and keypad/pointer indevs wired to `input`. The caller supplies `flushCb` (no-op snapshot
// sink vs dirty-rect tracking) and must have called lv_init() / lv_tick_set_cb first. `display` is null on
// failure.
inline SoftwareDisplay createSoftwareDisplay(std::uint32_t width, std::uint32_t height,
                                             std::vector<std::uint8_t>& drawBuf,
                                             lv_display_flush_cb_t flushCb, LvInputState* input) {
    SoftwareDisplay d;
    d.display = lv_display_create(width, height);
    if (!d.display) return d;
    const lv_color_format_t cf = lv_display_get_color_format(d.display);
    const std::uint32_t stride = lv_draw_buf_width_to_stride(width, cf);
    drawBuf.assign(static_cast<std::size_t>(stride) * height, 0);
    lv_display_set_buffers(d.display, drawBuf.data(), nullptr, drawBuf.size(), LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(d.display, flushCb);

    d.group = lv_group_create();
    lv_group_set_default(d.group);
    d.keypad = lv_indev_create();
    lv_indev_set_type(d.keypad, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(d.keypad, keypadReadCb);
    lv_indev_set_driver_data(d.keypad, input);
    lv_indev_set_group(d.keypad, d.group);
    d.pointer = lv_indev_create();
    lv_indev_set_type(d.pointer, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(d.pointer, pointerReadCb);
    lv_indev_set_driver_data(d.pointer, input);
    return d;
}

// Match PluginUI: black screen, strip the theme's rounded border / padding + the scrollable flag, and pin
// the React-tree root to 100% so it tracks the display. Call after the engine mounts the UI (GetWindowInstance
// is guarded, so it's a no-op for the root until the tree exists).
inline void applyChromelessScreen() {
    if (lv_obj_t* scr = lv_screen_active()) {
        lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(scr, 0, 0);
        lv_obj_set_style_radius(scr, 0, 0);
        lv_obj_set_style_pad_all(scr, 0, 0);
        lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    }
    if (lv_obj_t* win = GetWindowInstance()) {
        lv_obj_set_style_width(win, lv_pct(100), 0);
        lv_obj_set_style_height(win, lv_pct(100), 0);
    }
}

} // namespace retroplug::ui
