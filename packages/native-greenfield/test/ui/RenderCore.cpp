#include "RenderCore.hpp"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <unordered_set>

// LVGL is built in custom-global mode (LV_GLOBAL_CUSTOM): the application must provide
// lv_global_default(). In the plugin DPF's LVGLWidget supplies a per-instance one
// (deps/dpf-widgets/generic/LVGL.cpp); headless we need a single process-wide instance.
// Zero-initialized like DPF's calloc; lv_init() populates it. Leaked intentionally
// (process-lifetime singleton).
extern "C" lv_global_t* lv_global_default(void) {
    static lv_global_t* g = static_cast<lv_global_t*>(std::calloc(1, sizeof(lv_global_t)));
    return g;
}

extern "C" {
    #include <quickjs.h>
}

// lv_binding_js internals (global widget registry + root window + PNG encoder).
#include "native/core/basic/comp.hpp"          // comp_map, BasicComponent, ECOMP_TYPE
#include "native/components/component.hpp"      // GetWindowInstance()
#include "native/core/img/png/lodepng.h"

namespace rpuigf {

namespace {

// The active render core, for the static JS testId trampoline (single runtime, single-threaded, one
// core at a time — same recovery pattern as the legacy harness's g_active).
RenderCore* g_active = nullptr;

// Simulated millisecond clock. Advanced by pump() so LVGL timers fire deterministically without
// wall-clock dependence.
std::uint32_t g_tickMs = 0;
std::uint32_t tickCb() { return g_tickMs; }

// No-op flush: the draw buffer already holds the rendered pixels; there's no GL texture / window to
// push to. Just acknowledge so LVGL continues.
void flushCb(lv_display_t* disp, const lv_area_t*, uint8_t*) {
    lv_display_flush_ready(disp);
}

// Keypad indev: pop one queued LVGL key code per read (PRESSED while queued, then RELEASED), like
// dpf-widgets' keyBuffer. Drives menu focus-group nav.
void keypadReadCb(lv_indev_t* indev, lv_indev_data_t* data) {
    auto* c = static_cast<RenderCore*>(lv_indev_get_driver_data(indev));
    if (c && !c->keyQueue().empty()) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->key   = c->keyQueue().front();
        c->keyQueue().pop_front();
        data->continue_reading = !c->keyQueue().empty();
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// Pointer indev: report the core's synthetic cursor position + button state.
void pointerReadCb(lv_indev_t* indev, lv_indev_data_t* data) {
    auto* c = static_cast<RenderCore*>(lv_indev_get_driver_data(indev));
    if (!c) { data->state = LV_INDEV_STATE_RELEASED; return; }
    data->point = c->mousePos();
    data->state = c->mouseDown() ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

// Collect every live lv_obj under `o` (depth-first). The QUERIES below walk the live tree rather than
// the global comp_map: lv_binding_js can leave a freed component in comp_map after an unmount, and
// dereferencing its dangling instance crashes. The tree only holds live objects.
void collectTree(lv_obj_t* o, std::vector<lv_obj_t*>& out) {
    if (!o) return;
    out.push_back(o);
    const std::uint32_t n = lv_obj_get_child_count(o);
    for (std::uint32_t i = 0; i < n; ++i) collectTree(lv_obj_get_child(o, i), out);
}

// Live lv_binding_js components: the set of comp_map instances still present (so non-component LVGL
// objects + freed entries are both excluded from the queries).
std::unordered_set<BasicComponent*> validComponentSet() {
    std::unordered_set<BasicComponent*> valid;
    for (const auto& [uid, bc] : comp_map) if (bc) valid.insert(bc);
    return valid;
}

// globalThis.__rp_tagTestId(uid, name): the UI calls this from a ref so we can map a stable name to the
// rendered lv_obj. uid is lv_binding_js's component id (a string); comp_map resolves it to the native
// instance. Inert in the real plugin (the function isn't installed there — the UI uses optional
// chaining).
JSValue jsTagTestId(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (!g_active || argc < 2) return JS_UNDEFINED;
    const char* uid  = JS_ToCString(ctx, argv[0]);
    const char* name = JS_ToCString(ctx, argv[1]);
    if (uid && name) {
        auto it = comp_map.find(uid);
        if (it != comp_map.end() && it->second)
            g_active->recordTestId(name, it->second->instance);
    }
    if (uid)  JS_FreeCString(ctx, uid);
    if (name) JS_FreeCString(ctx, name);
    return JS_UNDEFINED;
}

} // namespace

bool Snapshot::isFlat() const {
    if (argb.size() < 8) return true;
    const std::uint32_t first = *reinterpret_cast<const std::uint32_t*>(argb.data());
    for (std::size_t i = 4; i + 4 <= argb.size(); i += 4)
        if (*reinterpret_cast<const std::uint32_t*>(argb.data() + i) != first)
            return false;
    return true;
}

RenderCore::RenderCore(std::uint32_t width, std::uint32_t height)
    : width_(width), height_(height) {}

RenderCore::~RenderCore() {
    // Clean the widget tree while the JS context is still alive (so delete events drain safely), then
    // shut the engine and tear down the LVGL display.
    if (lv_obj_t* scr = lv_screen_active()) lv_obj_clean(scr);
    engine_.shutdown();
    if (keypad_)  lv_indev_delete(keypad_);
    if (pointer_) lv_indev_delete(pointer_);
    if (display_) lv_display_delete(display_);
    if (group_)   lv_group_delete(group_);
    if (g_active == this) g_active = nullptr;
}

bool RenderCore::init() {
    if (booted_) return true;
    g_active = this;

    // --- LVGL: the cheap, non-GL subset of LVGLWidget::init ----------------
    lv_init(); // idempotent in v9
    lv_tick_set_cb(tickCb);

    display_ = lv_display_create(width_, height_);
    if (!display_) return false;

    const lv_color_format_t cf = lv_display_get_color_format(display_);
    const std::uint32_t stride = lv_draw_buf_width_to_stride(width_, cf);
    drawBuf_.assign(static_cast<std::size_t>(stride) * height_, 0);
    lv_display_set_buffers(display_, drawBuf_.data(), nullptr, drawBuf_.size(),
                           LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(display_, flushCb);

    group_ = lv_group_create();
    lv_group_set_default(group_);
    keypad_ = lv_indev_create();
    lv_indev_set_type(keypad_, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(keypad_, keypadReadCb);
    lv_indev_set_driver_data(keypad_, this);
    lv_indev_set_group(keypad_, group_);
    pointer_ = lv_indev_create();
    lv_indev_set_type(pointer_, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(pointer_, pointerReadCb);
    lv_indev_set_driver_data(pointer_, this);

    // --- JS engine ----------------------------------------------------------
    if (!engine_.init()) return false;

    // Match PluginUI: black screen background + root window tracks the display.
    if (lv_obj_t* scr = lv_screen_active()) {
        lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    }
    if (lv_obj_t* win = GetWindowInstance()) {
        lv_obj_set_style_width(win, lv_pct(100), 0);
        lv_obj_set_style_height(win, lv_pct(100), 0);
    }

    engine_.setParamWriteCallback([](uint32_t, float) {});
    engine_.registerParameter(0, "gain");

    installTestIdHook();

    booted_ = true;
    return true;
}

void RenderCore::installTestIdHook() {
    JSContext* ctx = engine_.getContext();
    if (!ctx) return;
    JSValue g = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, g, "__rp_tagTestId",
                      JS_NewCFunction(ctx, jsTagTestId, "__rp_tagTestId", 2));
    JS_FreeValue(ctx, g);
}

void RenderCore::reopenEditor() {
    engine_.detachDisplay();
    pump(10);                 // flush LVGL async deletes from the unmount
    engine_.attachDisplay();
    pump(30);                 // let the re-mounted React tree settle
}

void RenderCore::pump(int iterations) {
    for (int i = 0; i < iterations; ++i) {
        g_tickMs += 23; // keep the UI tick roughly in step with an audio block
        if (engine_.getContext()) engine_.emit("frame", 0, nullptr);
        engine_.tick();
        lv_timer_handler();
    }
}

Snapshot RenderCore::snapshot() {
    Snapshot out;
    lv_obj_t* scr = lv_screen_active();
    if (!scr) return out;
    lv_draw_buf_t* snap = lv_snapshot_take(scr, LV_COLOR_FORMAT_ARGB8888);
    if (!snap) return out;
    out.width  = snap->header.w;
    out.height = snap->header.h;
    const std::size_t bytes = static_cast<std::size_t>(out.width) * out.height * 4;
    out.argb.assign(snap->data, snap->data + bytes);
    lv_draw_buf_destroy(snap);
    return out;
}

bool RenderCore::snapshotPng(const std::string& path) {
    Snapshot s = snapshot();
    if (s.argb.empty()) return false;
    std::vector<unsigned char> rgb(static_cast<std::size_t>(s.width) * s.height * 3);
    for (std::size_t i = 0, n = static_cast<std::size_t>(s.width) * s.height; i < n; ++i) {
        rgb[i * 3 + 0] = s.argb[i * 4 + 2]; // B,G,R,A -> R,G,B
        rgb[i * 3 + 1] = s.argb[i * 4 + 1];
        rgb[i * 3 + 2] = s.argb[i * 4 + 0];
    }
    return lodepng_encode24_file(path.c_str(), rgb.data(), s.width, s.height) == 0;
}

std::size_t RenderCore::widgetCount() const {
    const auto valid = validComponentSet();
    std::vector<lv_obj_t*> objs;
    collectTree(lv_screen_active(), objs);
    std::size_t n = 0;
    for (lv_obj_t* o : objs) {
        auto* bc = static_cast<BasicComponent*>(lv_obj_get_user_data(o));
        if (bc && valid.count(bc)) ++n;
    }
    return n;
}

std::size_t RenderCore::countByType(int compType) const {
    const auto valid = validComponentSet();
    std::vector<lv_obj_t*> objs;
    collectTree(lv_screen_active(), objs);
    std::size_t n = 0;
    for (lv_obj_t* o : objs) {
        auto* bc = static_cast<BasicComponent*>(lv_obj_get_user_data(o));
        if (bc && valid.count(bc) && static_cast<int>(bc->type) == compType) ++n;
    }
    return n;
}

lv_obj_t* RenderCore::findFirstByType(int compType) const {
    const auto valid = validComponentSet();
    std::vector<lv_obj_t*> objs;
    collectTree(lv_screen_active(), objs);
    for (lv_obj_t* o : objs) {
        auto* bc = static_cast<BasicComponent*>(lv_obj_get_user_data(o));
        if (bc && valid.count(bc) && static_cast<int>(bc->type) == compType) return o;
    }
    return nullptr;
}

lv_obj_t* RenderCore::findByText(const std::string& text) const {
    std::vector<lv_obj_t*> objs;
    collectTree(lv_screen_active(), objs);
    for (lv_obj_t* o : objs) {
        if (!lv_obj_check_type(o, &lv_label_class)) continue; // live label: safe to read
        const char* t = lv_label_get_text(o);
        if (t && text == t) return o;
    }
    return nullptr;
}

lv_obj_t* RenderCore::findByTextContaining(const std::string& substr) const {
    std::vector<lv_obj_t*> objs;
    collectTree(lv_screen_active(), objs);
    for (lv_obj_t* o : objs) {
        if (!lv_obj_check_type(o, &lv_label_class)) continue;
        const char* t = lv_label_get_text(o);
        if (t && std::string(t).find(substr) != std::string::npos) return o;
    }
    return nullptr;
}

lv_obj_t* RenderCore::findByTestId(const std::string& name) const {
    auto it = testIds_.find(name);
    if (it == testIds_.end()) return nullptr;
    // Only return it if still live (its slot may have unmounted).
    std::vector<lv_obj_t*> objs;
    collectTree(lv_screen_active(), objs);
    for (lv_obj_t* o : objs) if (o == it->second) return o;
    return nullptr;
}

lv_obj_t* RenderCore::focusedObject() const {
    if (!keypad_) return nullptr;
    lv_group_t* g = lv_indev_get_group(keypad_); // the menu's group once claimed
    return g ? lv_group_get_focused(g) : nullptr;
}

WidgetInfo RenderCore::widgetInfo(lv_obj_t* obj) const {
    WidgetInfo wi;
    if (!obj) return wi;
    wi.found = true;
    lv_area_t a;
    lv_obj_get_coords(obj, &a); // absolute (screen) coordinates
    wi.x = a.x1;
    wi.y = a.y1;
    wi.width  = lv_area_get_width(&a);
    wi.height = lv_area_get_height(&a);
    wi.childCount = lv_obj_get_child_count(obj);
    if (lv_obj_check_type(obj, &lv_label_class)) {
        if (const char* t = lv_label_get_text(obj)) wi.text = t;
    }
    return wi;
}

void RenderCore::tapKey(std::uint32_t lvKey) {
    keyQueue_.push_back(lvKey);
    // Mirror PluginUI: also fire the JS "key" channel (press then release) for handlers that live there
    // (prompt/capture/Esc/game input). LVGL key code -> DPF key code (the value input.ts expects).
    std::uint32_t dpf;
    switch (lvKey) {
        case LV_KEY_UP:    dpf = 0xE036; break;
        case LV_KEY_DOWN:  dpf = 0xE038; break;
        case LV_KEY_RIGHT: dpf = 0xE037; break;
        case LV_KEY_LEFT:  dpf = 0xE035; break;
        case LV_KEY_ENTER: dpf = 0x0D;   break;
        case LV_KEY_ESC:   dpf = 0x1B;   break;
        default:           dpf = lvKey;  break;
    }
    JSContext* ctx = engine_.getContext();
    auto emitKey = [&](bool press) {
        if (!ctx) return;
        JSValue args[2] = { JS_NewUint32(ctx, dpf), JS_NewBool(ctx, press) };
        engine_.emit("key", 2, args);
        JS_FreeValue(ctx, args[0]);
        JS_FreeValue(ctx, args[1]);
    };
    emitKey(true);
    pump(2);  // keypad indev: PRESSED then RELEASED through lv_timer_handler
    emitKey(false);
    pump(1);
}

void RenderCore::clickAt(std::int32_t x, std::int32_t y) {
    JSContext* ctx = engine_.getContext();
    auto emitMouse = [&](bool press) {
        if (!ctx) return;
        JSValue args[4] = { JS_NewUint32(ctx, 0), JS_NewBool(ctx, press),
                            JS_NewFloat64(ctx, x), JS_NewFloat64(ctx, y) };
        engine_.emit("mouse", 4, args);
        for (JSValue& v : args) JS_FreeValue(ctx, v);
    };
    mousePos_  = { x, y };
    mouseDown_ = true;
    emitMouse(true);
    pump(2);  // register the press at (x,y)
    mouseDown_ = false;
    emitMouse(false);
    pump(2);  // release -> LVGL fires CLICKED -> onClick
}

} // namespace rpuigf
