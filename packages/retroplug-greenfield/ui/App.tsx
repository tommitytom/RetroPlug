// The app controller: owns the menu open/close state and swaps between the start menu (empty project)
// and the system grid (with the instance menu swapping into a tile).
//
// Esc is owned HERE, in one place (simpler than legacy's split ladder): menu closed + a system focused →
// open the instance menu anchored to it; menu open → close it. The start menu (empty project) is always
// open. When the grid shows without a menu, the keypad is pointed at the sink group so arrow keys don't
// leak into the clickable tiles.

import { useEffect, useMemo, useState } from "react";
import { setKeyboardGroup } from "lvgljs";

import { useStores, useSystems, useProjectSettings, useUserConfig, useRecent, useBindings } from "./stores/useStores";
import { useSinkGroup } from "./lvgl/FocusProvider";
import { Box } from "./lvgl/Box";
import { useNativeEvent } from "./lvgl/useNativeEvent";
import { useWindowSize, requestWindowSize, isWindowSizeControlled, setWindowTitle } from "./lvgl/useWindowSize";
import { useCloseGuard } from "./lvgl/useCloseGuard";
import { useProjectModals } from "./lvgl/useProjectModals";
import { useGameInput } from "./input/useGameInput";
import { useGamepadInput } from "./input/useGamepadInput";
import { SystemGrid } from "./screens/grid/SystemGrid";
import { Menu } from "./screens/menu/Menu";
import { gridContentSize, SystemLayout } from "./screens/grid/layout";
import { buildInstanceMenu, buildStartMenu, composeWindowTitle, type MenuContext } from "./screens/menu/menuDefs";
import type { MenuTree } from "./screens/menu/menuTree";
import { isMenuModalActive } from "./screens/menu/menuModal";

const KEY_ESCAPE = 0x1b;
// A fixed, non-GB controller button that opens/closes the menu — the gamepad twin of Esc. Deliberately NOT
// a default gameplay binding (a/b/start/back/dpad), so pressing it during play never doubles as a GB button.
const OPEN_MENU_BUTTON = "leftshoulder";
const MIN_ZOOM = 1;
const MAX_ZOOM = 6;

export function App() {
  const stores = useStores();
  const systems = useSystems();
  const settings = useProjectSettings();
  const userConfig = useUserConfig();
  const recent = useRecent();
  const bindings = useBindings();
  const sink = useSinkGroup();
  const windowSize = useWindowSize();
  const closeGuard = useCloseGuard(stores);
  const modals = useProjectModals(stores);
  const version = useMemo(() => stores.backend.version(), [stores.backend]); // static; shown in the menu title

  const [menuOpen, setMenuOpen] = useState(true);
  const [menuSystemId, setMenuSystemId] = useState<number | null>(null);

  const empty = systems.length === 0;
  const resolvedZoom = Math.min(MAX_ZOOM, Math.max(MIN_ZOOM, settings.zoom >= MIN_ZOOM && settings.zoom <= MAX_ZOOM ? settings.zoom : userConfig.defaultZoom));

  // The standalone OS window title: version + project name (re-renders on the project channel, which fires
  // on load / adopt / rename / new). Pushed to native via the __rp_setWindowTitle seam (inert elsewhere).
  const windowTitle = composeWindowTitle(version, stores.project.name());

  // Menu-open invariant on empty transitions: empty → the start menu (always open); first system → close.
  useEffect(() => {
    setMenuOpen(empty);
    if (empty) setMenuSystemId(null);
  }, [empty]);

  // Idle: point the keypad at the sink when the grid shows without a menu. Not while a modal overlay owns
  // the keypad (close prompt / project modal) — else closing the menu to raise one steals its focus.
  useEffect(() => {
    if (!empty && !menuOpen && !closeGuard.active && !modals.active && sink) setKeyboardGroup(sink);
  }, [empty, menuOpen, sink, closeGuard.active, modals.active]);

  // Fit the window to the grid when instances or zoom change — unless a tiling WM owns geometry, in which
  // case the request is ignored and the grid's fitZoom shrinks tiles to whatever the compositor gave us.
  useEffect(() => {
    if (empty || isWindowSizeControlled()) return;
    const { width, height } = gridContentSize(systems.length, settings.layout as SystemLayout, resolvedZoom);
    requestWindowSize(width, height);
  }, [empty, systems.length, settings.layout, resolvedZoom]);

  // Keep the OS window title in sync with the project name / version.
  useEffect(() => setWindowTitle(windowTitle), [windowTitle]);

  // The one guard ladder + open/close toggle, shared by Esc and the gamepad open-button so the two entry
  // points can't drift. Deferrals: overlays (close prompt / project modal) own the key and cancel/close;
  // the start menu is always open; a capture/prompt modal owns it (Menu handles the press).
  const toggleMenu = () => {
    if (closeGuard.active) return void closeGuard.onCancel();
    if (modals.active) return void modals.onClose();
    if (empty) return;
    if (isMenuModalActive()) return;
    if (menuOpen) {
      setMenuOpen(false);
      setMenuSystemId(null);
    } else {
      setMenuSystemId(stores.project.systems.focused());
      setMenuOpen(true);
    }
  };

  useNativeEvent("key", (...args) => {
    const key = args[0] as number;
    const press = args[1] as boolean;
    if (press && key === KEY_ESCAPE) toggleMenu();
  });
  // Gamepad twin of Esc: a fixed non-GB button opens/closes the menu, so a gamepad-only user isn't stranded
  // once a game is running. Unbound in gameplay, so it fires no GB button; while a rebind is armed the
  // isMenuModalActive() deferral above lets Menu bind this button instead of the toggle fighting it.
  useNativeEvent("gamepad-button", (...args) => {
    const name = args[1] as string;
    const press = args[2] as boolean;
    if (press && name === OPEN_MENU_BUTTON) toggleMenu();
  });

  // Game input: route keyboard to the focused instance's joypad, but only when a tile is showing (not the
  // start menu) and no instance menu is open — otherwise arrows/Enter belong to menu navigation.
  useGameInput({ active: !empty && !menuOpen && !closeGuard.active && !modals.active, focusedId: stores.project.systems.focused() });
  // Gamepad: same policy as keyboard (tiles-only, focused instance) — the native SDL poll feeds the
  // "gamepad-button" bus useGamepadInput reads.
  useGamepadInput({ active: !empty && !menuOpen && !closeGuard.active && !modals.active, focusedId: stores.project.systems.focused() });

  const ctx: MenuContext = { stores, settings, userConfig, bindings, systems, recent, version, newProject: modals.newProject, loadProject: modals.loadProject, loadRomAsProject: modals.loadRomAsProject };

  // Unsaved-changes prompt on window close (standalone): a full-window overlay above everything, owning
  // the keypad. Save & Quit / Discard & Quit / Cancel — the guard drives the native quit + dismissal.
  if (closeGuard.active) {
    const { width, height } = windowSize;
    const closeTree: MenuTree = {
      title: "Unsaved changes",
      items: [
        { id: "close-save", label: "Save & Quit", kind: "action", keepOpen: true, onSelect: closeGuard.onSave },
        { id: "close-discard", label: "Discard & Quit", kind: "action", keepOpen: true, onSelect: closeGuard.onDiscard },
        { id: "close-cancel", label: "Cancel", kind: "action", keepOpen: true, onSelect: closeGuard.onCancel },
      ],
    };
    return (
      <Box style={{ width, height, "background-color": "#000000" }}>
        <Menu width={width} height={height} zoom={resolvedZoom} tree={closeTree} onClose={closeGuard.onCancel} />
      </Box>
    );
  }

  // Project modals (discard confirm on New/Load, incompatible/error notice, missing-files relink): a
  // full-window overlay above everything, same pattern as the close prompt. The tree is owned by the hook.
  if (modals.active && modals.modal) {
    const { width, height } = windowSize;
    return (
      <Box style={{ width, height, "background-color": "#000000" }}>
        <Menu width={width} height={height} zoom={resolvedZoom} tree={modals.modal} onClose={modals.onClose} />
      </Box>
    );
  }

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
