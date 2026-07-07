// DevProbe: the smallest UI that exercises the whole binding layer end to end, so the render harness can
// assert it works before any real screen exists.
//
// It proves, through the real reconciler on the headless display:
//   - useProjectSettings() reads a store view, and re-renders when a SILENT setter (setZoom) fires — the
//     path that would never notify without ProjectStore.setOnChange + the provider fan-out.
//   - useIsDirty() flips clean → dirty on that same mutation.
//   - useNativeEvent("frame", …) receives the native bus events pump() emits.
// Tapping "zoom+" drives project.setZoom directly (no action wrapper); the store observer does the rest.

import { useState } from "react";
import { View, Text } from "lvgljs-ui";

import { useStores, useProjectSettings, useIsDirty } from "./stores/useStores";
import { useNativeEvent } from "./lvgl/useNativeEvent";

export function DevProbe() {
  const { project } = useStores();
  const settings = useProjectSettings();
  const dirty = useIsDirty();
  const [frames, setFrames] = useState(0);

  useNativeEvent("frame", () => setFrames((n) => n + 1));

  // Wrap within the valid 1..6 range so repeated taps keep changing the value (setZoom rejects
  // out-of-range and would otherwise no-op at the top).
  const bumpZoom = () => project.setZoom(settings.zoom >= 6 ? 1 : settings.zoom + 1);

  return (
    <View
      style={{
        width: 480,
        height: 432,
        "background-color": "#101024",
        display: "flex",
        "flex-direction": "column",
        "align-items": "flex-start",
        "justify-content": "flex-start",
        "padding-left": 8,
        "padding-top": 8,
        "row-spacing": 4,
      }}
    >
      <Text style={{ "text-color": "#ffffff", "font-size": 18 }}>RetroPlug Greenfield UI</Text>
      <Text style={{ "text-color": "#88ff88", "font-size": 14 }}>{`zoom:${settings.zoom}`}</Text>
      <Text style={{ "text-color": dirty ? "#ffaa44" : "#8888ff", "font-size": 14 }}>
        {`dirty:${dirty ? "yes" : "no"}`}
      </Text>
      <Text style={{ "text-color": "#cccccc", "font-size": 14 }}>{`frames:${frames}`}</Text>
      <Text
        onClick={bumpZoom}
        style={{
          "text-color": "#ffffff",
          "font-size": 16,
          "background-color": "#3355aa",
          "padding-left": 6,
          "padding-right": 6,
          "padding-top": 4,
          "padding-bottom": 4,
        }}
      >
        zoom+
      </Text>
    </View>
  );
}
