// The app controller: owns the menu open/close state and swaps between the start menu (empty project)
// and the system grid (with the instance menu swapping into a tile).
//
// Esc is owned HERE, in one place (simpler than legacy's split ladder): menu closed + a system focused →
// open the instance menu anchored to it; menu open → close it. The start menu (empty project) is always
// open. When the grid shows without a menu, the keypad is pointed at the sink group so arrow keys don't
// leak into the clickable tiles.

import { useEffect, useState } from "react";
import { Dimensions } from "lvgljs-ui";
import { setKeyboardGroup } from "lvgljs";

import { useStores, useSystems, useProjectSettings, useUserConfig, useRecent } from "./stores/useStores";
import { useSinkGroup } from "./lvgl/FocusProvider";
import { Box } from "./lvgl/Box";
import { useNativeEvent } from "./lvgl/useNativeEvent";
import { SystemGrid } from "./screens/grid/SystemGrid";
import { Menu } from "./screens/menu/Menu";
import { buildInstanceMenu, buildStartMenu, type MenuContext } from "./screens/menu/menuDefs";

const KEY_ESCAPE = 0x1b;
const MIN_ZOOM = 1;
const MAX_ZOOM = 6;

function displaySize(): { width: number; height: number } {
  try {
    const d = (Dimensions as { window?: { width: number; height: number } }).window;
    if (d && d.width > 0 && d.height > 0) return { width: d.width, height: d.height };
  } catch {
    /* fall through */
  }
  return { width: 480, height: 432 };
}

export function App() {
  const stores = useStores();
  const systems = useSystems();
  const settings = useProjectSettings();
  const userConfig = useUserConfig();
  const recent = useRecent();
  const sink = useSinkGroup();

  const [menuOpen, setMenuOpen] = useState(true);
  const [menuSystemId, setMenuSystemId] = useState<number | null>(null);

  const empty = systems.length === 0;

  // Menu-open invariant on empty transitions: empty → the start menu (always open); first system → close.
  useEffect(() => {
    setMenuOpen(empty);
    if (empty) setMenuSystemId(null);
  }, [empty]);

  // Idle: point the keypad at the sink when the grid shows without a menu.
  useEffect(() => {
    if (!empty && !menuOpen && sink) setKeyboardGroup(sink);
  }, [empty, menuOpen, sink]);

  useNativeEvent("key", (...args) => {
    const key = args[0] as number;
    const press = args[1] as boolean;
    if (!press || key !== KEY_ESCAPE || empty) return; // start menu is always open
    if (menuOpen) {
      setMenuOpen(false);
      setMenuSystemId(null);
    } else {
      setMenuSystemId(stores.project.systems.focused());
      setMenuOpen(true);
    }
  });

  const ctx: MenuContext = { stores, settings, userConfig, systems, recent, version: "" };
  const resolvedZoom = Math.min(MAX_ZOOM, Math.max(MIN_ZOOM, settings.zoom >= MIN_ZOOM && settings.zoom <= MAX_ZOOM ? settings.zoom : userConfig.defaultZoom));

  if (empty) {
    const d = displaySize();
    return (
      <Box style={{ width: d.width, height: d.height, "background-color": "#000000" }}>
        <Menu width={d.width} height={d.height} zoom={resolvedZoom} tree={buildStartMenu(ctx)} onClose={() => {}} />
      </Box>
    );
  }

  const anchored = menuOpen && menuSystemId != null ? systems.find((sys) => sys.id === menuSystemId) : undefined;
  const menuTree = anchored ? buildInstanceMenu({ ...ctx, system: anchored }) : undefined;
  return (
    <SystemGrid
      menuSystemId={menuTree ? menuSystemId : null}
      menuTree={menuTree}
      onMenuClose={() => {
        setMenuOpen(false);
        setMenuSystemId(null);
      }}
    />
  );
}
