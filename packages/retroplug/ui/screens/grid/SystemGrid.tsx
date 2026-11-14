// The emulator grid: one live tile per system, laid out from the store's systems view, and a cell after a system for each
// side view open beside it (a package's panel: the NES SD card's file list), which the layout counts as a tile.
//
// Reads useSystems() for the tiles and useProjectSettings()/useUserConfig() for layout + zoom, and
// mutates through useStores() (setFocus) with no action wrapper. Each grid position is a StableSlot (the
// insertChildBefore-append workaround) whose one child is keyed by the cell it holds; the system whose id matches
// `menuSystemId` shows the instance <Menu> in place of its EmulatorTile, so sibling tiles keep rendering. The tile
// zoom is capped (fitZoom) to fit the whole grid in the current window — App resizes the window to the grid, but a
// tiling WM may clamp it, so the cap keeps everything visible either way. Empty projects and the add/duplicate actions
// are handled by the menu (App), not here.

import { useStores, useSystems, useProjectSettings, useUserConfig } from "../../stores/useStores";
import { Box } from "@retroplug/ui-react";
import { useWindowSize } from "../../lvgl/useWindowSize";
import { StableSlot } from "../../lvgl/StableSlot";
import { EmulatorTile } from "./EmulatorTile";
import { Menu } from "../menu/Menu";
import type { MenuTree } from "@retroplug/ui/menu";
import type { UiHost } from "@retroplug/ui/contribution";
import { SideCell, useGridCells, type Cell } from "../../views/SideViews";
import { SystemLayout, shapeFor, tileWidth, tileHeight, displayZoom } from "./layout";

export function SystemGrid({
  host,
  menuSystemId,
  menuTree,
  onMenuClose,
}: {
  /** The UI host a tile's views are handed (docs/modular-graph-phase-f4c-design.md section 5.2). */
  host: UiHost;
  menuSystemId?: number | null;
  menuTree?: MenuTree;
  onMenuClose?: () => void;
}) {
  const stores = useStores();
  const systems = useSystems();
  const settings = useProjectSettings();
  const userConfig = useUserConfig();
  const display = useWindowSize();

  const cells = useGridCells(systems);
  if (systems.length === 0) return null; // App renders the start menu when empty

  const layout = settings.layout as SystemLayout;
  const zoom = displayZoom(cells.length, layout, settings.zoom, userConfig.defaultZoom, display);
  const shape = shapeFor(layout, cells.length);
  const tw = tileWidth(zoom);
  const th = tileHeight(zoom);
  const single = systems.length === 1;

  const rows: Cell[][] = [];
  for (let r = 0; r < shape.rows; r++) rows.push(cells.slice(r * shape.cols, (r + 1) * shape.cols));

  return (
    <Box style={{ width: display.width, height: display.height, "background-color": "#000000", display: "flex", "flex-direction": "column", "align-items": "center", "justify-content": "center" }}>
      {/* Intermediate containers are transparent so only the dark root + the tiles show. */}
      <Box style={{ width: shape.cols * tw, height: shape.rows * th, "background-opacity": 0, display: "flex", "flex-direction": "column" }}>
        {rows.map((row, ri) => (
          <Box key={`row-${ri}`} style={{ width: shape.cols * tw, height: th, "background-opacity": 0, display: "flex", "flex-direction": "row" }}>
            {row.map((cell, ci) => {
              const sys = cell.system;
              const index = cell.index;
              const focus = () => stores.project.systems.setFocus(sys.id);
              // A slot per grid POSITION, its one child keyed by what the cell holds: a panel opening shifts the cells
              // after it, and a slot keyed by its content would be re-inserted mid-row, which lv_binding_js appends.
              if (cell.kind === "side") {
                return (
                  <StableSlot key={`cell-${ci}`} testId={`side-${cell.view.id}-${index}`} width={tw} height={th}>
                    <SideCell key={`side-${cell.view.id}-${sys.id}`} view={cell.view} host={host} system={sys} width={tw} height={th} onFocus={focus} />
                  </StableSlot>
                );
              }
              const showMenu = sys.id === menuSystemId && menuTree != null;
              return (
                <StableSlot key={`cell-${ci}`} testId={`tile-${index}`} width={tw} height={th}>
                  {showMenu ? (
                    <Menu key={`menu-${sys.id}`} width={tw} height={th} zoom={zoom} tree={menuTree!} onClose={onMenuClose ?? (() => {})} />
                  ) : (
                    <EmulatorTile
                      key={`tile-${sys.id}`}
                      systemId={sys.id}
                      host={host}
                      focused={sys.focused}
                      single={single}
                      width={tw}
                      height={th}
                      dimTestId={`dim-${index}`}
                      onFocus={focus}
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
