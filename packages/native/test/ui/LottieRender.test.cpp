// Smoke test for the genuinely new capability behind the <Lottie> React component:
// that lv_lottie actually rasterizes animated vector frames via ThorVG on a headless
// software display. The lv_binding_js wrapper around it (Lottie native component + the
// React comp) is mechanical — copied from GIF/Canvas — so it isn't what needs proving.
// If ThorVG weren't compiled in, or LV_USE_LOTTIE were off, this translation unit
// wouldn't build (the #error below) and lv_lottie_* wouldn't link.
//
// It drives the LVGL API directly (no JS engine) via a bare display + a caller-owned
// ARGB8888 draw buffer, then asserts the buffer is (a) non-blank after a few ticks
// and (b) different 20 frames later — i.e. ThorVG drew, and the animation advanced.

#include <catch2/catch_test_macros.hpp>

extern "C" {
    #include "lvgl.h"
}

#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#if !LV_USE_LOTTIE
#error "LottieRender.test.cpp requires LV_USE_LOTTIE — check the active lv_conf.h"
#endif

// LVGL here is built with LV_GLOBAL_CUSTOM (LV_GLOBAL_CUSTOM() → lv_global_default()),
// so the application must supply the global-state accessor. In the real plugin DPF owns
// it per-widget (dpf-widgets/generic/LVGL.cpp); this headless single-context test provides
// a plain singleton instead of linking the whole DPF/GL widget layer.
lv_global_t* lv_global_default() {
    static lv_global_t global{};
    return &global;
}

namespace {

void nullFlush(lv_display_t* d, const lv_area_t*, uint8_t*) { lv_display_flush_ready(d); }

std::string slurp(const char* path) {
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.good());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

size_t countNonZero(const lv_draw_buf_t* buf, int w, int h) {
    const uint8_t* p = buf->data;
    size_t n = 0;
    for (size_t i = 0; i < static_cast<size_t>(w) * h * 4; i++) {
        if (p[i]) n++;
    }
    return n;
}

} // namespace

TEST_CASE("lv_lottie rasterizes animated frames via ThorVG") {
    lv_init();

    // A bare software display: caller-owned framebuffer + a no-op flush. lv_lottie
    // needs an active screen to parent under and the timer/anim loop to tick.
    static std::vector<uint8_t> fb(static_cast<size_t>(64) * 64 * 4);
    lv_display_t* disp = lv_display_create(64, 64);
    REQUIRE(disp != nullptr);
    lv_display_set_buffers(disp, fb.data(), nullptr, static_cast<uint32_t>(fb.size()),
                           LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(disp, nullFlush);

    const int W = 48, H = 48;
    lv_draw_buf_t* buf = lv_draw_buf_create(W, H, LV_COLOR_FORMAT_ARGB8888, LV_STRIDE_AUTO);
    REQUIRE(buf != nullptr);

    const std::string json = slurp(RP_LOTTIE_JSON_PATH);
    REQUIRE(json.size() > 0);

    lv_obj_t* lottie = lv_lottie_create(lv_screen_active());
    REQUIRE(lottie != nullptr);
    lv_lottie_set_draw_buf(lottie, buf);
    lv_lottie_set_src_data(lottie, json.c_str(), json.size());

    auto pump = [&](int frames) {
        for (int i = 0; i < frames; i++) {
            lv_tick_inc(16);
            lv_timer_handler();
        }
    };

    pump(3);
    REQUIRE(countNonZero(buf, W, H) > 0); // ThorVG rasterized something

    std::vector<uint8_t> early(buf->data, buf->data + static_cast<size_t>(W) * H * 4);
    pump(20);
    std::vector<uint8_t> later(buf->data, buf->data + static_cast<size_t>(W) * H * 4);

    // The animation advanced — a static rasterizer or a stuck timer would fail this.
    REQUIRE(std::memcmp(early.data(), later.data(), early.size()) != 0);

    lv_obj_delete(lottie);
    lv_draw_buf_destroy(buf);
}
