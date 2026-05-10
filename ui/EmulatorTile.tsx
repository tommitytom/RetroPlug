import { Canvas, Text, View } from "lvgljs-ui";
import { useEffect, useRef, useState } from "react";
import { on, off } from "lvgljs";

// Mirrors LV_IMAGE_ALIGN values from lvgl/src/widgets/image/lv_image.h.
// Defined here so we don't pull in the whole LV constants table.
const LV_IMAGE_ALIGN_CONTAIN = 14;

// Plugin-specific JS surface set up by PluginJsBridge.
interface PluginNamespace {
    getFrame?: (systemId: number) => { width: number; height: number; buffer: ArrayBuffer } | null;
    removeSystem?: (systemId: number) => boolean;
    setFocus?: (systemId: number) => boolean;
}
const plugin: PluginNamespace =
    (globalThis as any)[Symbol.for("plugin")] ?? {};

interface EmulatorTileProps {
    systemId: number;
    focused: boolean;
}

/**
 * Renders one emulator's framebuffer in a Canvas widget. Subscribes to the
 * "frame" tick emitted from PluginUI::uiIdle and pulls the latest frame via
 * plugin.getFrame on every tick. Aspect ratio is preserved (CONTAIN);
 * scaling is nearest-neighbor for crisp pixels.
 *
 * Multi-instance: when `focused` is false the tile dims to 0.5 alpha (legacy
 * parity). A small ✕ in the top-right calls plugin.removeSystem.
 *
 * Renders a "no signal" placeholder until the first frame arrives — this is
 * the state right after a fresh add, before a ROM has been activated.
 */
export function EmulatorTile({ systemId, focused }: EmulatorTileProps) {
    const canvasRef = useRef<any>(null);
    const [hasFrame, setHasFrame] = useState(false);

    useEffect(() => {
        const onFrame = () => {
            const frame = plugin.getFrame?.(systemId);
            if (!frame) return;
            const canvas = canvasRef.current;
            if (!canvas) return;
            canvas.setBuffer(frame.buffer, frame.width, frame.height);
            if (!hasFrame) setHasFrame(true);
        };
        on("frame", onFrame);
        return () => off("frame", onFrame);
    }, [systemId, hasFrame]);

    return (
        <View
            style={{
                width: "100%",
                height: "100%",
                "background-opacity": 0,
                "border-color": focused ? "#4fc3f7" : "#1a1a2e",
                "border-width": 2,
                "border-opacity": 255,
                opacity: focused ? 255 : 128,
            }}
            onClick={() => plugin.setFocus?.(systemId)}
        >
            <Canvas
                ref={canvasRef as any}
                style={{
                    width: "100%",
                    height: "100%",
                }}
                nearestNeighbor={true}
                innerAlign={LV_IMAGE_ALIGN_CONTAIN}
            />
            {!hasFrame && (
                <Text
                    style={{
                        "text-color": "#666",
                        "font-size": 16,
                    }}
                    align={{ type: 0x09 /* LV_ALIGN_CENTER */, pos: [0, 0] }}
                >
                    No ROM loaded - press Esc, Load ROM
                </Text>
            )}
            <Text
                style={{
                    "text-color": "#ffffff",
                    "background-color": "#000000",
                    "background-opacity": 180,
                    "font-size": 14,
                    padding: 4,
                }}
                align={{ type: 0x03 /* LV_ALIGN_TOP_RIGHT */, pos: [-4, 4] }}
                onClick={() => plugin.removeSystem?.(systemId)}
            >
                X
            </Text>
        </View>
    );
}
