#pragma once

// Desktop mouse wheel -> scroll the hit-tested scrollable ancestor under the cursor. Shared by every UI
// host so wheel behaviour can't drift between them: the DPF editor (PluginUI::onScroll), the SDL2
// standalone (SDL_MOUSEWHEEL) and the headless UI-test harness (RenderCore::scrollAt).
//
// LVGL has no wheel concept of its own - dpf-widgets feeds the wheel to an ENCODER indev, which shuffles
// keypad-group FOCUS rather than scrolling whatever is under the pointer. So the hosts hit-test the point
// themselves and scroll the first ancestor that actually overflows. Programmatic scrolling ignores an
// object's scroll-dir (that only gates drag/indev scrolling), which is what makes this work on the menu's
// inner container - it sets "scroll-dir": "none" to suppress desktop drag-scroll.

#include <cstdint>

extern "C" {
    #include "lvgl.h"
}

namespace retroplug::ui {

// Pixels scrolled per wheel notch.
inline constexpr std::int32_t kWheelStep = 24;

// Scroll by (notchesX, notchesY) wheel notches at display point (x, y). Positive notchesY = wheel away from
// the user (content moves down, revealing earlier rows); positive notchesX = wheel right. Fractional notches
// (high-resolution / trackpad wheels) are honoured. Returns true if an ancestor scrolled.
inline bool scrollAtPoint(std::int32_t x, std::int32_t y, float notchesX, float notchesY) {
    lv_obj_t* const scr = lv_screen_active();
    if (!scr) return false;

    lv_point_t p = {x, y};
    lv_obj_t* const hit = lv_indev_search_obj(scr, &p);
    const auto dx = static_cast<std::int32_t>(notchesX * kWheelStep);
    const auto dy = static_cast<std::int32_t>(notchesY * kWheelStep);

    for (lv_obj_t* it = hit; it; it = lv_obj_get_parent(it)) {
        if (!lv_obj_has_flag(it, LV_OBJ_FLAG_SCROLLABLE)) continue;
        const bool vScroll = (dy != 0) && (lv_obj_get_scroll_top(it) + lv_obj_get_scroll_bottom(it) > 0);
        const bool hScroll = (dx != 0) && (lv_obj_get_scroll_left(it) + lv_obj_get_scroll_right(it) > 0);
        if (vScroll || hScroll) {
            lv_obj_scroll_by_bounded(it, hScroll ? -dx : 0, vScroll ? dy : 0, LV_ANIM_OFF);
            return true;
        }
    }
    return false;
}

} // namespace retroplug::ui
