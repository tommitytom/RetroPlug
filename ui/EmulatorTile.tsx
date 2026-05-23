import { Canvas, View } from "lvgljs-ui";
import { useEffect, useRef } from "react";
import { on, off } from "lvgljs";

import { plugin } from "./plugin/client";
import { tileWidth, tileHeight } from "./layout";

// Mirrors LV_IMAGE_ALIGN values from lvgl/src/widgets/image/lv_image.h.
const LV_IMAGE_ALIGN_CONTAIN = 14;

// Cast around lvgljs-ui's Canvas type which doesn't expose a ref prop in
// its public typings. Same trick PluginUI uses for Text.
const CanvasAny = Canvas as any;

interface EmulatorTileProps {
    systemId: number;
    focused: boolean;
    // Integer zoom 1..6. Drives the canvas pixel size; the C++ framebuffer
    // is always native 160x144 and LVGL's nearest-neighbor scaler upscales.
    zoom: number;
}

/**
 * Renders one emulator's framebuffer in a fixed-size Canvas. Subscribes to
 * the "frame" tick from PluginUI::uiIdle and pulls the latest frame on each
 * tick. Aspect ratio is preserved (CONTAIN); scaling is nearest-neighbor.
 *
 * Multi-instance: when `focused` is false the whole tile dims to ~50%
 * opacity (legacy parity, alpha-only — no border, no glow). With N=1 the
 * single tile is always focused so the dim never appears.
 *
 * No "no signal" placeholder: a tile only exists once a system is
 * registered; until the first frame arrives the canvas just renders black,
 * which matches the surrounding window.
 */
export function EmulatorTile({ systemId, focused, zoom }: EmulatorTileProps) {
    const canvasRef = useRef<any>(null);
    const tw = tileWidth(zoom);
    const th = tileHeight(zoom);

    useEffect(() => {
        // pending guards against piling up Promises if the C++ side is
        // momentarily slow — we drop frames rather than queue them, which
        // matches the cadence of the existing sync getFrame path.
        let pending = false;
        let cancelled = false;
        const onFrame = () => {
            if (pending) return;
            pending = true;
            (async () => {
                try {
                    const frame = await plugin.getFrame(systemId);
                    if (cancelled || !frame) return;
                    const canvas = canvasRef.current;
                    if (!canvas) return;
                    // msgpack-javascript decodes BIN as a Uint8Array.subarray
                    // view into the wire buffer; slice() gives us a fresh
                    // ArrayBuffer of exactly the framebuffer bytes for
                    // canvas.setBuffer (which JS_GetArrayBuffer treats as
                    // a whole-buffer object).
                    canvas.setBuffer(frame.buffer.slice().buffer,
                                     frame.width, frame.height);
                } finally {
                    pending = false;
                }
            })();
        };
        on("frame", onFrame);
        return () => { cancelled = true; off("frame", onFrame); };
    }, [systemId]);

    // The "dim unfocused" trick: render a translucent black overlay on top
    // of the canvas. Setting `opacity` on the tile View or `img-opacity` on
    // the Canvas both fall flat:
    //   - LV_STYLE_OPA on a parent only dims that parent's own bg/border;
    //     it does NOT propagate to image children.
    //   - lvgl-js's OpacityStyle pipe only routes `opacity` → `img-opacity`
    //     when `compName === "Image"`. Canvas's compName is "Canvas", so
    //     setting `opacity` (or `img-opacity` directly) gets dropped.
    // A black overlay at 50% opa is portable, doesn't touch lv_binding_js,
    // and exactly matches the legacy alpha-only dim convention.
    return (
        <View
            style={{
                width:  tw,
                height: th,
                "background-color": "#000000",
                "background-opacity": 255,
                "border-width": 0,
                "border-opacity": 0,
                "border-radius": 0,
                "padding-left":  0,
                "padding-right": 0,
                "padding-top":   0,
                "padding-bottom":0,
                overflow: "hidden",
            }}
            onClick={() => { void plugin.$notify("setFocus", systemId); }}
        >
            <CanvasAny
                ref={canvasRef}
                style={{
                    width:  tw,
                    height: th,
                    "border-width": 0,
                    "border-radius": 0,
                    "padding-left":  0,
                    "padding-right": 0,
                    "padding-top":   0,
                    "padding-bottom":0,
                }}
                nearestNeighbor={true}
                innerAlign={LV_IMAGE_ALIGN_CONTAIN}
            />
            {!focused && (
                <View
                    style={{
                        width:  tw,
                        height: th,
                        "background-color": "#000000",
                        // lvgl-js's NormalizeOpacity expects 0..1 floats:
                        // anything > 1 is clamped to 255. So we pass 0.5
                        // (= LV_OPA_50) for a half-dim overlay rather than
                        // the natural-looking `128`. The neighboring `255` /
                        // `0` calls elsewhere in this file happen to be
                        // correct because they're at the clamp extremes.
                        "background-opacity": 0.5,
                        "border-width": 0,
                        "border-opacity": 0,
                        "border-radius": 0,
                        "padding-left":  0,
                        "padding-right": 0,
                        "padding-top":   0,
                        "padding-bottom":0,
                    }}
                    align={{ type: 0x09 /* LV_ALIGN_CENTER */, pos: [0, 0] }}
                />
            )}
        </View>
    );
}
