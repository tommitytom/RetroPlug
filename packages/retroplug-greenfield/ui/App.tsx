// The app controller: owns the menu open/close state and swaps between the start menu (empty project)
// and the system grid (with the instance menu swapping into a tile).
//
// Esc is owned HERE, in one place (simpler than legacy's split ladder): menu closed + a system focused →
// open the instance menu anchored to it; menu open → close it. The start menu (empty project) is always
// open. When the grid shows without a menu, the keypad is pointed at the sink group so arrow keys don't
// leak into the clickable tiles.

import { useEffect, useState } from "react";
import { setKeyboardGroup } from "lvgljs";

import { useStores, useSystems, useProjectSettings, useUserConfig, useRecent } from "./stores/useStores";
import { useSinkGroup } from "./lvgl/FocusProvider";
import { Box } from "./lvgl/Box";
import { useNativeEvent } from "./lvgl/useNativeEvent";
import { useWindowSize, requestWindowSize, isWindowSizeControlled } from "./lvgl/useWindowSize";
import { useGameInput } from "./input/useGameInput";
import { SystemGrid } from "./screens/grid/SystemGrid";
import { Menu } from "./screens/menu/Menu";
import { gridContentSize, SystemLayout } from "./screens/grid/layout";
import { buildInstanceMenu, buildStartMenu, type MenuContext } from "./screens/menu/menuDefs";

const KEY_ESCAPE = 0x1b;
const MIN_ZOOM = 1;
const MAX_ZOOM = 6;

export function App() {
  const stores = useStores();
  const systems = useSystems();
  const settings = useProjectSettings();
  const userConfig = useUserConfig();
  const recent = useRecent();
  const sink = useSinkGroup();
  const windowSize = useWindowSize();

  const [menuOpen, setMenuOpen] = useState(true);
  const [menuSystemId, setMenuSystemId] = useState<number | null>(null);

  const empty = systems.length === 0;
  const resolvedZoom = Math.min(MAX_ZOOM, Math.max(MIN_ZOOM, settings.zoom >= MIN_ZOOM && settings.zoom <= MAX_ZOOM ? settings.zoom : userConfig.defaultZoom));

  // Menu-open invariant on empty transitions: empty → the start menu (always open); first system → close.
  useEffect(() => {
    setMenuOpen(empty);
    if (empty) setMenuSystemId(null);
  }, [empty]);

  // Idle: point the keypad at the sink when the grid shows without a menu.
  useEffect(() => {
    if (!empty && !menuOpen && sink) setKeyboardGroup(sink);
  }, [empty, menuOpen, sink]);

  // Fit the window to the grid when instances or zoom change — unless a tiling WM owns geometry, in which
  // case the request is ignored and the grid's fitZoom shrinks tiles to whatever the compositor gave us.
  useEffect(() => {
    if (empty || isWindowSizeControlled()) return;
    const { width, height } = gridContentSize(systems.length, settings.layout as SystemLayout, resolvedZoom);
    requestWindowSize(width, height);
  }, [empty, systems.length, settings.layout, resolvedZoom]);

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

  // Game input: route keyboard to the focused instance's joypad, but only when a tile is showing (not the
  // start menu) and no instance menu is open — otherwise arrows/Enter belong to menu navigation.
  useGameInput({ active: !empty && !menuOpen, focusedId: stores.project.systems.focused() });

  const ctx: MenuContext = { stores, settings, userConfig, systems, recent, version: "" };

  if (empty) {
    const { width, height } = windowSize;
    return (
      <Box style={{ width, height, "background-color": "#000000" }}>
        <Menu width={width} height={height} zoom={resolvedZoom} tree={buildStartMenu(ctx)} onClose={() => {}} />
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
