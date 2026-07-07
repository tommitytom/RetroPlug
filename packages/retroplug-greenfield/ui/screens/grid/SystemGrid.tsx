// The emulator grid: one live tile per system, laid out from the store's systems view.
//
// Reads useSystems() for the tiles and useProjectSettings()/useUserConfig() for the layout + zoom, and
// mutates through useStores() (loadMgb / duplicateSystem / setFocus) with no action wrapper — the store
// observers fan the change back out. Tiles are wrapped in a StableSlot (the insertChildBefore-append
// workaround). Because the display is a fixed size (no window-resize yet), the tile zoom is capped to
// fit the whole grid on screen. An empty project shows a "New mGB" affordance (the one add that needs no
// file dialog); a populated one shows a small toolbar to add another (a focused-system clone). A real
// toolbar/menu arrives with the menu port.

import { View, Text, Dimensions } from "lvgljs-ui";

import { useStores, useSystems, useProjectSettings, useUserConfig } from "../../stores/useStores";
import { StableSlot } from "../../lvgl/StableSlot";
import { EmulatorTile } from "./EmulatorTile";
import type { SystemView } from "../../../src/systemsStore";
import { SystemLayout, shapeFor, tileWidth, tileHeight, GB_NATIVE_W, GB_NATIVE_H } from "./layout";

const MIN_ZOOM = 1;
const MAX_ZOOM = 6;
const TOOLBAR_H = 28;

function displaySize(): { width: number; height: number } {
  try {
    const d = (Dimensions as { window?: { width: number; height: number } }).window;
    if (d && d.width > 0 && d.height > 0) return { width: d.width, height: d.height };
  } catch {
    /* fall through to the default */
  }
  return { width: GB_NATIVE_W * 3, height: GB_NATIVE_H * 3 }; // 480×432 fallback (the harness display)
}

// The largest integer zoom in [1, cap] whose grid fits `area`, so the whole grid is always visible.
function fitZoom(count: number, layout: SystemLayout, cap: number, area: { width: number; height: number }): number {
  const shape = shapeFor(layout, Math.max(count, 1));
  for (let z = Math.min(cap, MAX_ZOOM); z > MIN_ZOOM; z--) {
    if (shape.cols * tileWidth(z) <= area.width && shape.rows * tileHeight(z) <= area.height) return z;
  }
  return MIN_ZOOM;
}

export function SystemGrid() {
  const stores = useStores();
  const systems = useSystems();
  const settings = useProjectSettings();
  const userConfig = useUserConfig();

  const display = displaySize();
  const layout = settings.layout as SystemLayout;
  const resolvedZoom = settings.zoom >= MIN_ZOOM && settings.zoom <= MAX_ZOOM ? settings.zoom : userConfig.defaultZoom;

  const rootStyle = {
    width: display.width,
    height: display.height,
    "background-color": "#0b0b12",
    display: "flex",
    "flex-direction": "column",
    "align-items": "center",
    "justify-content": "center",
  } as const;

  if (systems.length === 0) {
    return (
      <View style={rootStyle}>
        <Text
          onClick={() => stores.project.systems.loadMgb()}
          style={{
            "text-color": "#ffffff",
            "font-size": 18,
            "background-color": "#3355aa",
            "padding-left": 16,
            "padding-right": 16,
            "padding-top": 9,
            "padding-bottom": 9,
            "border-radius": 4,
          }}
        >
          New mGB
        </Text>
      </View>
    );
  }

  const gridArea = { width: display.width, height: display.height - TOOLBAR_H };
  const zoom = fitZoom(systems.length, layout, resolvedZoom, gridArea);
  const shape = shapeFor(layout, systems.length);
  const tw = tileWidth(zoom);
  const th = tileHeight(zoom);
  const single = systems.length === 1;

  const rows: SystemView[][] = [];
  for (let r = 0; r < shape.rows; r++) rows.push(systems.slice(r * shape.cols, (r + 1) * shape.cols));

  return (
    <View style={{ width: display.width, height: display.height, "background-color": "#0b0b12", display: "flex", "flex-direction": "column" }}>
      {/* Toolbar — first-cut affordance to add another instance (clone of the focused system). */}
      <View
        style={{
          width: display.width,
          height: TOOLBAR_H,
          "background-color": "#1a1a26",
          display: "flex",
          "flex-direction": "row",
          "align-items": "center",
          "padding-left": 6,
        }}
      >
        <Text
          onClick={() => stores.project.systems.duplicateSystem(stores.project.systems.focused())}
          style={{
            "text-color": "#ffffff",
            "font-size": 13,
            "background-color": "#2a2a3a",
            "padding-left": 8,
            "padding-right": 8,
            "padding-top": 3,
            "padding-bottom": 3,
            "border-radius": 3,
          }}
        >
          + mGB
        </Text>
      </View>

      {/* Grid content, centered in the remaining area. Intermediate containers are transparent so only
          the dark root + the tiles show (unstyled lvgljs Views default to a light fill). */}
      <View style={{ width: display.width, height: gridArea.height, "background-opacity": 0, display: "flex", "flex-direction": "column", "align-items": "center", "justify-content": "center" }}>
        <View style={{ width: shape.cols * tw, height: shape.rows * th, "background-opacity": 0, "border-width": 0, display: "flex", "flex-direction": "column", overflow: "hidden" }}>
          {rows.map((row, ri) => (
            <View key={`row-${ri}`} style={{ width: shape.cols * tw, height: th, "background-opacity": 0, "border-width": 0, display: "flex", "flex-direction": "row" }}>
              {row.map((sys, ci) => {
                const index = ri * shape.cols + ci;
                return (
                  <StableSlot key={`slot-${sys.id}`} testId={`tile-${index}`} width={tw} height={th}>
                    <EmulatorTile
                      systemId={sys.id}
                      focused={sys.focused || single}
                      width={tw}
                      height={th}
                      dimTestId={`dim-${index}`}
                      onFocus={() => stores.project.systems.setFocus(sys.id)}
                    />
                  </StableSlot>
                );
              })}
            </View>
          ))}
        </View>
      </View>
    </View>
  );
}
