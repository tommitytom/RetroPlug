// The emulator grid: one live tile per system, laid out from the store's systems view.
//
// Reads useSystems() for the tiles and useProjectSettings()/useUserConfig() for layout + zoom, and
// mutates through useStores() (setFocus) with no action wrapper. Tiles are wrapped in a StableSlot (the
// insertChildBefore-append workaround); the slot whose id matches `menuSystemId` swaps its child from the
// EmulatorTile to the instance <Menu>, so sibling tiles keep rendering. The tile zoom is capped (fitZoom)
// to fit the whole grid in the current window — App resizes the window to the grid, but a tiling WM may
// clamp it, so the cap keeps everything visible either way. Empty projects and the add/duplicate actions
// are handled by the menu (App), not here.

import { useStores, useSystems, useProjectSettings, useUserConfig } from "../../stores/useStores";
import { Box } from "../../lvgl/Box";
import { useWindowSize } from "../../lvgl/useWindowSize";
import { StableSlot } from "../../lvgl/StableSlot";
import { EmulatorTile } from "./EmulatorTile";
import { Menu } from "../menu/Menu";
import type { MenuTree } from "../menu/menuTree";
import type { SystemView } from "../../../src/systemsStore";
import { SystemLayout, shapeFor, tileWidth, tileHeight } from "./layout";

const MIN_ZOOM = 1;
const MAX_ZOOM = 6;

// The largest integer zoom in [1, cap] whose grid fits `area`, so the whole grid is always visible.
function fitZoom(count: number, layout: SystemLayout, cap: number, area: { width: number; height: number }): number {
  const shape = shapeFor(layout, Math.max(count, 1));
  for (let z = Math.min(cap, MAX_ZOOM); z > MIN_ZOOM; z--) {
    if (shape.cols * tileWidth(z) <= area.width && shape.rows * tileHeight(z) <= area.height) return z;
  }
  return MIN_ZOOM;
}

export function SystemGrid({
  menuSystemId,
  menuTree,
  onMenuClose,
}: {
  menuSystemId?: number | null;
  menuTree?: MenuTree;
  onMenuClose?: () => void;
}) {
  const stores = useStores();
  const systems = useSystems();
  const settings = useProjectSettings();
  const userConfig = useUserConfig();
  const display = useWindowSize();

  if (systems.length === 0) return null; // App renders the start menu when empty

  const layout = settings.layout as SystemLayout;
  const resolvedZoom = settings.zoom >= MIN_ZOOM && settings.zoom <= MAX_ZOOM ? settings.zoom : userConfig.defaultZoom;
  const zoom = fitZoom(systems.length, layout, resolvedZoom, display);
  const shape = shapeFor(layout, systems.length);
  const tw = tileWidth(zoom);
  const th = tileHeight(zoom);
  const single = systems.length === 1;

  const rows: SystemView[][] = [];
  for (let r = 0; r < shape.rows; r++) rows.push(systems.slice(r * shape.cols, (r + 1) * shape.cols));

  return (
    <Box style={{ width: display.width, height: display.height, "background-color": "#000000", display: "flex", "flex-direction": "column", "align-items": "center", "justify-content": "center" }}>
      {/* Intermediate containers are transparent so only the dark root + the tiles show. */}
      <Box style={{ width: shape.cols * tw, height: shape.rows * th, "background-opacity": 0, display: "flex", "flex-direction": "column" }}>
        {rows.map((row, ri) => (
          <Box key={`row-${ri}`} style={{ width: shape.cols * tw, height: th, "background-opacity": 0, display: "flex", "flex-direction": "row" }}>
            {row.map((sys, ci) => {
              const index = ri * shape.cols + ci;
              const showMenu = sys.id === menuSystemId && menuTree != null;
              return (
                <StableSlot key={`slot-${sys.id}`} testId={`tile-${index}`} width={tw} height={th}>
                  {showMenu ? (
                    <Menu width={tw} height={th} zoom={zoom} tree={menuTree!} onClose={onMenuClose ?? (() => {})} />
                  ) : (
                    <EmulatorTile
                      systemId={sys.id}
                      focused={sys.focused}
                      single={single}
                      width={tw}
                      height={th}
                      dimTestId={`dim-${index}`}
                      onFocus={() => stores.project.systems.setFocus(sys.id)}
                    />
                  )}
                </StableSlot>
              );
            })}
          </Box>
        ))}
      </Box>
    </Box>
  );
}
