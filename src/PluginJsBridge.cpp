#include "PluginJsBridge.hpp"

extern "C" {
    #include <quickjs.h>
}

PluginJsBridge::PluginJsBridge(LvglJsEngine& eng) : engine(eng) {
    if (DpfJsDisplayData* data = DpfJsDisplayData::get())
        data->bridge = this;
}

PluginJsBridge::~PluginJsBridge() {
    if (DpfJsDisplayData* data = DpfJsDisplayData::get()) {
        if (data->bridge == this)
            data->bridge = nullptr;
    }
}

void PluginJsBridge::pushWaveform(const float* samples, uint32_t count) {
    JSContext* ctx = engine.getContext();
    if (!ctx || count == 0)
        return;
    JSValue buf = JS_NewArrayBufferCopy(ctx,
                                        reinterpret_cast<const uint8_t*>(samples),
                                        count * sizeof(float));
    if (JS_IsException(buf))
        return;
    engine.emit("waveform", 1, &buf);
    JS_FreeValue(ctx, buf);
}
