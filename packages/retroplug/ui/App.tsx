// The app controller: owns the menu open/close state and swaps between the start menu (empty project)
// and the system grid (with the instance menu swapping into a tile).
//
// App-level input is owned HERE. Esc always cancels an active overlay (a universal back, independent of
// bindings). Beyond that, the rebindable app actions (resolved from the active bindings) drive open/close
// the menu (default Esc / leftshoulder) and cycle the focused instance (default Tab / rightshoulder). The
// start menu (empty project) is always open. When the grid shows without a menu, the keypad is pointed at
// the sink group so arrow keys don't leak into the clickable tiles.

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
import { gridContentSize, hitTestTile, resolveZoom, SystemLayout } from "./screens/grid/layout";
import { buildInstanceMenu, buildStartMenu, composeWindowTitle, type MenuContext } from "./screens/menu/menuDefs";
import type { MenuTree } from "./screens/menu/menuTree";
import { isMenuModalActive } from "./screens/menu/menuModal";
import { buildKeyToAction, buildGamepadToAction, type AppAction } from "../src/keyCodes";
import { resolveDropAction } from "../src/fileDrop";

const KEY_ESCAPE = 0x1b;

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
  // "In play": a tile is showing and no menu/overlay owns input. Gates game input AND the cycle actions.
  const playing = !empty && !menuOpen && !closeGuard.active && !modals.active;
  // App-action lookups (open menu / cycle instances), rebuilt only when the bindings change.
  const keyToAction = useMemo(() => buildKeyToAction(bindings.keyboardActions), [bindings.keyboardActions]);
  const padToAction = useMemo(() => buildGamepadToAction(bindings.gamepadActions), [bindings.gamepadActions]);
  const resolvedZoom = resolveZoom(settings.zoom, userConfig.defaultZoom);

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

  // Fit the window to the grid when the instance count / zoom / layout changes. Deliberately NOT reactive to
  // windowSize: re-asserting the size on every observed resize fights a host/compositor that reverts our
  // size (Renoise on XWayland snaps its embed back to 480×432, and re-requesting just oscillated forever),
  // and can't help where it matters anyway — after a drag Hyprland ignores client resizes entirely. So we set
  // the size on a real change and otherwise leave the window to fitZoom. The initial size is also applied
  // pre-map by the editor (PluginUI::applyInitialWindowSize) so the window opens correct.
  useEffect(() => {
    if (empty || isWindowSizeControlled()) return;
    const { width, height } = gridContentSize(systems.length, settings.layout as SystemLayout, resolvedZoom);
    requestWindowSize(width, height);
  }, [empty, systems.length, settings.layout, resolvedZoom]);

  // Keep the OS window title in sync with the project name / version.
  useEffect(() => setWindowTitle(windowTitle), [windowTitle]);

  // Open/close the instance menu. NOT the overlay-cancel path (that's hardwired to Esc below): the start menu
  // is always open, and a capture/prompt modal owns the press (Menu handles it) via the isMenuModalActive
  // deferral — load-bearing so a gamepad rebind can bind the open-menu button instead of the toggle fighting it.
  const toggleMenuCore = () => {
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

  // Dispatch a resolved app action: open/close the menu, or cycle the focused instance (only in play).
  const runAction = (action: AppAction | undefined) => {
    if (action === "OpenMenu") toggleMenuCore();
    else if (action === "CycleNext" && playing) stores.project.systems.focusNext(1);
    else if (action === "CyclePrev" && playing) stores.project.systems.focusNext(-1);
  };

  useNativeEvent("key", (...args) => {
    const key = args[0] as number;
    const press = args[1] as boolean;
    if (!press) return;
    // Esc always cancels an active overlay — a universal back, independent of the (rebindable) OpenMenu key.
    if (key === KEY_ESCAPE) {
      if (closeGuard.active) return void closeGuard.onCancel();
      if (modals.active) return void modals.onClose();
    }
    if (closeGuard.active || modals.active) return; // an overlay owns input; actions don't fire under it
    runAction(keyToAction.get(key));
  });
  // Gamepad app actions. Overlays (close prompt / project modal) render a <Menu>, so they're cancelled by the
  // Menu B button — the gamepad handler just defers under them.
  useNativeEvent("gamepad-button", (...args) => {
    const name = args[1] as string;
    const press = args[2] as boolean;
    if (!press) return;
    if (closeGuard.active || modals.active) return;
    runAction(padToAction.get(name));
  });

  // Drag-and-drop: a dropped ROM / .sav / project routes by instance count. On the start screen or a
  // single instance it loads as a project (the guarded "Load…" behaviour); in a multi-instance project it
  // cold-boot replaces the tile under the cursor (falling back to the focused tile), or loads a bare .sav
  // into that tile. The native editor pushes the OS drop onto the "file-drop" channel (newline-joined
  // paths + window-space x/y); inert in the headless harness, which never emits it.
  useNativeEvent("file-drop", (...args) => {
    const paths = String(args[0] ?? "").split("\n").filter((p) => p.length > 0);
    if (paths.length === 0) return;
    const idx = hitTestTile(args[1] as number, args[2] as number, systems.length, settings.layout as SystemLayout, settings.zoom, userConfig.defaultZoom, windowSize);
    const action = resolveDropAction(
      stores.backend,
      {
        count: systems.length,
        targetId: idx != null ? systems[idx].id : stores.project.systems.focused(),
        onTile: idx != null,
        siblingRom: (sav) => stores.project.systems.resolveSiblingRom(sav),
      },
      paths,
    );
    switch (action.type) {
      case "loadProject":
        modals.loadProject(action.path);
        break;
      case "loadRom":
        modals.loadRomAsProject(action.romPath, action.explicitSav);
        break;
      case "replace":
        stores.project.systems.replaceSystem(action.id, action.romPath, action.explicitSav ? { explicitSav: action.explicitSav } : undefined);
        break;
      case "loadSram":
        stores.project.systems.loadSram(action.id, action.sav);
        break;
      case "pairSav":
        void stores.fileSelection.pairDroppedSav(action.sav).then((rom) => {
          if (rom) modals.loadRomAsProject(rom, action.sav);
        });
        break;
      case "ignore":
        break;
    }
  });

  // Game input: route keyboard/gamepad to the focused instance's joypad only while in play (a tile showing,
  // no menu/overlay) — otherwise arrows/Enter belong to menu navigation.
  useGameInput({ active: playing, focusedId: stores.project.systems.focused() });
  useGamepadInput({ active: playing, focusedId: stores.project.systems.focused() });

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
