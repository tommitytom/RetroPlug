// The app controller: owns the menu open/close state and swaps between the start menu (empty project)
// and the system grid (with the instance menu swapping into a tile).
//
// App-level input is owned HERE. Esc always cancels an active overlay (a universal back, independent of
// bindings). Beyond that, the rebindable app actions (resolved from the active bindings) drive open/close
// the menu (default Esc / leftshoulder) and cycle the focused instance (default Tab / rightshoulder). The
// start menu (empty project) is always open. When the grid shows without a menu, the keypad is pointed at
// the sink group so arrow keys don't leak into the clickable tiles.

import { useCallback, useEffect, useMemo, useState } from "react";
import { setKeyboardGroup } from "lvgljs";

import type { SystemInfo } from "@retroplug/core/host";
import { useStores, useSystems, useProjectSettings, useUserConfig, useRecent, useBindings } from "./stores/useStores";
import { useSinkGroup } from "./lvgl/FocusProvider";
import { Box, useNativeEvent, useRerenderOn } from "@retroplug/ui-react";
import { useWindowSize, requestWindowSize, isWindowSizeControlled, setWindowTitle } from "./lvgl/useWindowSize";
import { useCloseGuard } from "./app/useCloseGuard";
import { useProjectModals } from "./app/useProjectModals";
import { useFileBrowser } from "./app/useFileBrowser";
import { useSongImport } from "./app/useSongImport";
import { useSongWatch } from "./app/useSongWatch";
import { useSramAutoSave } from "./app/useSramAutoSave";
import { useJobPump } from "./app/useJobPump";
import { useUiServices } from "./app/useUiServices";
import { topOverlay } from "./app/overlay";
import { native } from "./host/nativeHooks";
import { useGameInput } from "./input/useGameInput";
import { useGamepadInput } from "./input/useGamepadInput";
import { SystemGrid } from "./screens/grid/SystemGrid";
import { Menu } from "./screens/menu/Menu";
import { gridContentSize, hitTestTile, resolveZoom, SystemLayout } from "./screens/grid/layout";
import { buildInstanceMenu, buildStartMenu, composeWindowTitle, trackerCartLabel, type MenuContext } from "./screens/menu/menuDefs";
import { subscribeAudioDraft } from "./screens/menu/audioDraft";
import { subscribeMidi } from "./screens/menu/midiDevices";
import { subscribeTransport, pollTransport } from "./screens/menu/transport";
import type { MenuTree } from "@retroplug/ui/menu";
import { isMenuModalActive } from "./screens/menu/menuModal";
import { buildKeyToAction, buildGamepadToAction, KEY_ESCAPE } from "../src/keyCodes";
import { resolveDropAction } from "../src/fileDrop";
import { createUiHost } from "./host/uiHost";
import { contributedActionIds, contributedActions, windowViewOrReport, windowViewOf } from "./host/menuContributions";
import { runActions } from "./input/appActions";
import { withActionDefaults } from "../src/bindingMap";
import { ViewWindow } from "./views/ViewWindow";
import { viewAppliesTo } from "./views/viewSession";
import { resolveTracker } from "../src/trackerRegistry";

const MOUSE_BUTTON_RIGHT = 2; // DPF kMouseButtonRight (left=1, right=2, middle=3) — the "mouse" bus button field

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
  const browser = useFileBrowser(stores); // in-app file browser overlay (openFileBrowser)
  const songImport = useSongImport(stores);
  useSongWatch(stores); // records the focused cart's song in Recent when it changes (incl. loads made inside the cart)
  useSramAutoSave(stores); // mirrors each battery to its .sav under the Continuous preference (no-op otherwise)
  useJobPump(stores); // settles capability jobs as their completions arrive (no native call while none is pending)
  const version = useMemo(() => stores.backend.version(), [stores.backend]); // static; shown in the menu title

  const [menuOpen, setMenuOpen] = useState(true);
  const [menuSystemId, setMenuSystemId] = useState<number | null>(null);
  // The window view that is open, if any: its id and the system it is showing. It replaces the grid but is NOT an
  // input overlay - the cart keeps playing and keys still reach its joypad, so it doesn't gate `playing`.
  //
  // `host.views.open` looks the id up in the generated registry, so every window view is a package's.
  const [openView, setOpenView] = useState<{ id: string; systemId: number } | null>(null);
  // The UI's host (docs/modular-graph-phase-f4b-design.md section 3.2), built once over the stores: the groups a cart's
  // or a console's UI code reaches the shell through. Its `views` group opens the generated registry's window views,
  // and its import picker is useSongImport's overlay, which is resolved against the live system an import names rather
  // than the snapshot the caller held.
  const beginImport = useCallback(
    (sys: SystemInfo, source: Uint8Array): void => {
      const live = stores.project.systems.view().find((s) => s.id === sys.id);
      if (live) songImport.begin(live, source);
    },
    [stores, songImport.begin],
  );
  const openViewOf = useCallback(
    (id: string, systemId: number): void => {
      const open = windowViewOrReport(id);
      if ("report" in open) {
        console.warn(open.report);
        return;
      }
      const sys = stores.project.systems.view().find((s) => s.id === systemId);
      if (!sys) {
        console.warn(`[views] ${id} was asked for the system ${systemId}, which is not in the project`);
        return;
      }
      if (!viewAppliesTo(open.view, sys)) {
        console.warn(`[views] ${id} does not apply to the system ${systemId}`);
        return;
      }
      setOpenView({ id, systemId });
    },
    [stores],
  );
  const uiHost = useMemo(() => createUiHost(stores, { views: { open: openViewOf }, beginImport }), [
    stores,
    beginImport,
    openViewOf,
  ]);
  // The product's UI services: the devices whose state lives in the native host (the N8 Pro's card job, a control
  // surface's scan and its startup restore) started, subscribed and polled at their own cadences.
  useUiServices(uiHost);
  // The three standalone-only menu sources App still owns. Each owns state that lives in the NATIVE host rather than in
  // a store, so a change there has no path back into React and must be pushed: the Audio submenu's staged rate/block
  // draft, a MIDI device pick, and the transport toggle (which the audio thread publishes back). Without this the label
  // would not repaint until some unrelated render happened to come along. The device state the N8 and the Launchpad
  // show is the products' own UI services now (`useUiServices`), subscribed and polled the same way.
  //
  // A render is a render, so which source fired does not change what happens next. Inert in a DAW / the
  // harness, where nothing ever emits.
  useRerenderOn(subscribeAudioDraft, subscribeMidi, subscribeTransport);
  // The cartridge sound chip sits at ONE level, whether you are hearing the emulator or a real console: a system's
  // "Expansion Volume" knob drives Mesen's expansion channels, and the driven cart's node carries the same value as its
  // own `expansionVolume` param, which the node sends to the cart's master-volume register (and again on every
  // reconnect). The graph does that; the UI pushes nothing.

  const empty = systems.length === 0;
  // The full-window overlay that owns the window + input, if any. Priority order: the file browser first (a
  // browse can be raised from a modal - relink → Locate - so it must sit above it), then the close prompt,
  // the project modals, the song import. The same order decides what Esc cancels.
  const overlay = topOverlay([browser, closeGuard, modals, songImport]);
  const overlayActive = overlay !== null; // a stable effect dep (the hooks return a fresh object each render)
  // "In play": a tile is showing and no menu/overlay owns input. Gates game input AND the cycle actions.
  const playing = !empty && !menuOpen && !overlay;
  // App-action lookups (open menu / cycle instances, and each package's contributed actions), rebuilt only when the
  // profile changes. The profile is filled with the contributed actions' defaults first (design 5.6), so an id the
  // user has never edited reads its default; a `[]` the user wrote stays empty. The value is a LIST per key: one press
  // can carry the shell's action and a package's, and each runs.
  const actionIds = contributedActionIds(); // a module constant of the generated registry: a stable dep
  const actionBindings = useMemo(
    () => withActionDefaults(bindings, contributedActions().map((entry) => entry.action)),
    [bindings],
  );
  const keyToAction = useMemo(
    () => buildKeyToAction(actionBindings.keyboardActions, actionIds),
    [actionBindings, actionIds],
  );
  const padToAction = useMemo(
    () => buildGamepadToAction(actionBindings.gamepadActions, actionIds),
    [actionBindings, actionIds],
  );
  const resolvedZoom = resolveZoom(settings.zoom, userConfig.defaultZoom);

  // The standalone OS window title: version + the focused tracker cart's "<song> - <ROM name>" when one is
  // loaded (LSDj / risa), else the project's display name (its own name, else the one derived from the
  // systems). Re-renders on the project channel (load / adopt / name / new / focus / song-load). Pushed to
  // native via the __rp_setWindowTitle seam (inert elsewhere).
  const focusedSys = systems.find((s) => s.id === stores.project.systems.focused());
  const cartLabel = focusedSys ? trackerCartLabel(stores.backend, focusedSys) : null;
  const windowTitle = composeWindowTitle(version, stores.project.displayName(), cartLabel);

  // Menu-open invariant on empty transitions: empty → the start menu (always open); first system → close.
  useEffect(() => {
    setMenuOpen(empty);
    if (empty) setMenuSystemId(null);
  }, [empty]);

  // Keep the instance menu on its system: re-anchor it when an in-place rebuild gave the system a new id,
  // and CLOSE it when the system is gone for good. Leaving a dead id in place used to strand the menu
  // open-but-invisible, so the next Esc was spent closing a menu nobody could see.
  useEffect(() => {
    if (!menuOpen || menuSystemId == null) return;
    const cur = stores.project.systems.currentId(menuSystemId);
    if (cur === menuSystemId) return;
    if (cur === null) {
      setMenuOpen(empty);
      setMenuSystemId(null);
    } else {
      setMenuSystemId(cur);
    }
  }, [menuOpen, menuSystemId, systems, empty]);

  // Close any window view whose system goes away (removed, or the project reloaded): a stale id would otherwise
  // strand the UI on a screen with nothing to draw. The check is generic over the open view, not the HD player alone.
  useEffect(() => {
    if (openView != null && !systems.some((sys) => sys.id === openView.systemId)) setOpenView(null);
  }, [openView, systems]);

  // Idle: point the keypad at the sink when the grid shows without a menu. Not while a modal overlay owns
  // the keypad (close prompt / project modal) — else closing the menu to raise one steals its focus.
  useEffect(() => {
    if (!empty && !menuOpen && !overlayActive && sink) setKeyboardGroup(sink);
  }, [empty, menuOpen, sink, overlayActive]);

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

  // Dispatch a resolved press's action ids: the shell's three (open/close the menu, or cycle the focused instance,
  // the cycles only in play), then each contributed action, which gets the focused system or null on the start screen.
  const runResolved = (ids: readonly string[]): void => {
    runActions(
      ids,
      {
        OpenMenu: toggleMenuCore,
        CycleNext: () => {
          if (playing) stores.project.systems.focusNext(1);
        },
        CyclePrev: () => {
          if (playing) stores.project.systems.focusNext(-1);
        },
      },
      contributedActions(),
      uiHost,
      empty ? null : stores.project.systems.focused(),
    );
  };

  useNativeEvent("key", (key, press) => {
    if (!press) return;
    // Esc always cancels an active overlay — a universal back, independent of the (rebindable) OpenMenu key.
    if (key === KEY_ESCAPE) {
      if (overlay) return void overlay.onClose();
      if (openView != null) return void setOpenView(null); // a window view → back to the grid
    }
    if (overlay) return; // an overlay owns input; actions don't fire under it
    runResolved(keyToAction.get(key) ?? []);
  });
  // Gamepad app actions. Overlays (close prompt / project modal) render a <Menu>, so they're cancelled by the
  // Menu B button — the gamepad handler just defers under them.
  useNativeEvent("gamepad-button", (_pad, name, press) => {
    if (!press) return;
    if (overlay) return;
    runResolved(padToAction.get(name) ?? []);
  });

  // Right-click an instance → open its menu (anchored to the clicked tile, which also becomes focused). The
  // native editor emits the "mouse" bus as [button, press, x, y]; a left click already focuses/opens rows via
  // LVGL, so we act only on the right button. Ignored on the start screen (its menu is always open) and while
  // a capture/prompt or a full-window overlay owns input.
  useNativeEvent("mouse", (button, press, x, y) => {
    if (button !== MOUSE_BUTTON_RIGHT || !press) return;
    if (empty || isMenuModalActive() || overlay) return;
    const idx = hitTestTile(x, y, systems.length, settings.layout as SystemLayout, settings.zoom, userConfig.defaultZoom, windowSize);
    if (idx == null) return; // must land on an instance
    const targetId = systems[idx].id;
    stores.project.systems.setFocus(targetId); // focus the right-clicked instance, then anchor its menu
    setMenuSystemId(targetId);
    setMenuOpen(true);
  });

  // Drag-and-drop: a dropped ROM / .sav / project routes by instance count. On the start screen or a
  // single instance it loads as a project (the guarded "Load…" behaviour); in a multi-instance project it
  // cold-boot replaces the tile under the cursor (falling back to the focused tile), or loads a bare .sav
  // into that tile. The native editor pushes the OS drop onto the "file-drop" channel (newline-joined
  // paths + window-space x/y); inert in the headless harness, which never emits it.
  // The MIDI Clock submenu's values move without anyone touching the menu (an external master starts,
  // stops, changes tempo), so poll them while the menu is on screen — the same poll-and-compare the render
  // badge does. pollTransport only emits when a DISPLAYED value changes, and the seam is absent in a DAW /
  // the harness, so this is a cheap no-op there.
  useNativeEvent("frame", () => {
    if (menuOpen) pollTransport();
  });

  useNativeEvent("file-drop", (joined, x, y) => {
    const paths = String(joined ?? "").split("\n").filter((p) => p.length > 0);
    if (paths.length === 0) return;
    const idx = hitTestTile(x, y, systems.length, settings.layout as SystemLayout, settings.zoom, userConfig.defaultZoom, windowSize);
    const targetId = idx != null ? systems[idx].id : stores.project.systems.focused();
    const target = systems.find((s) => s.id === targetId);
    const action = resolveDropAction(
      stores.backend,
      {
        count: systems.length,
        targetId,
        onTile: idx != null,
        targetSongExtensions: target ? (resolveTracker(target.roles)?.songImport?.extensions ?? []) : [],
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
      // Both replace what the target cart holds, so an unsaved work-RAM song there is offered a save first.
      case "replace":
        modals.guardSongReplace(action.id, () =>
          stores.project.systems.replaceSystem(action.id, action.romPath, action.explicitSav ? { explicitSav: action.explicitSav } : undefined),
        );
        break;
      case "loadSram":
        modals.guardSongReplace(action.id, () => stores.project.systems.loadSram(action.id, action.sav));
        break;
      case "patchSongs": {
        const sys = systems.find((s) => s.id === action.id);
        const importer = sys ? resolveTracker(sys.roles)?.songImport : undefined;
        if (sys && importer) importer.import(uiHost, sys, action.paths);
        break;
      }
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

  // Exit (standalone Exit menu row): route through the SAME native close path the window-close button
  // uses — __rp_onCloseRequested raises the unsaved-changes prompt and vetoes when dirty; a clean project
  // quits immediately via __rp_quitWindow. Inert in a DAW / the harness (neither global installed).
  const requestExit = (): void => {
    const veto = native.__rp_onCloseRequested?.() ?? false;
    if (!veto) native.__rp_quitWindow?.();
  };

  const ctx: MenuContext = {
    stores,
    host: uiHost,
    settings,
    userConfig,
    bindings,
    systems,
    recent,
    version,
    newProject: modals.newProject,
    loadProject: modals.loadProject,
    loadRomAsProject: modals.loadRomAsProject,
    requestExit,
    beginSongImport: beginImport,
  };

  // A full-window overlay (file browser / close prompt / project modal / song import) draws above
  // everything and owns the keypad; the start menu (empty project) is the same full-window menu, always open.
  if (overlay) return <FullWindowMenu size={windowSize} zoom={resolvedZoom} tree={overlay.tree} onClose={overlay.onClose} />;
  if (empty) return <FullWindowMenu size={windowSize} zoom={resolvedZoom} tree={buildStartMenu(ctx)} onClose={() => {}} />;

  // A window view takes the whole window in place of the grid. A stale id (its system was removed, or the project
  // reloaded) falls through to the grid here and is cleared by the effect above.
  if (openView != null && systems.some((sys) => sys.id === openView.systemId)) {
    const { width, height } = windowSize;
    const view = windowViewOf(openView.id);
    if (view) {
      const system = systems.find((sys) => sys.id === openView.systemId) ?? null;
      return (
        <ViewWindow
          view={view}
          host={uiHost}
          system={system}
          systemId={openView.systemId}
          width={width}
          height={height}
        />
      );
    }
  }

  // The anchor FOLLOWS its system through an in-place rebuild (a keep-open row like FM Audio resets the
  // cart, which comes back under a new id): resolved here so this render already shows the rebuilt cart's
  // menu, and synced into state by the effect above.
  const anchorId = menuOpen && menuSystemId != null ? stores.project.systems.currentId(menuSystemId) : null;
  const anchored = anchorId != null ? systems.find((sys) => sys.id === anchorId) : undefined;
  const menuTree = anchored ? buildInstanceMenu({ ...ctx, system: anchored }) : undefined;
  return (
    <SystemGrid
      host={uiHost}
      menuSystemId={menuTree ? anchorId : null}
      menuTree={menuTree}
      onMenuClose={() => {
        setMenuOpen(false);
        setMenuSystemId(null);
      }}
    />
  );
}

/** A <Menu> filling the window on a black backdrop: every overlay, and the start menu. */
function FullWindowMenu({
  size,
  zoom,
  tree,
  onClose,
}: {
  size: { width: number; height: number };
  zoom: number;
  tree: MenuTree;
  onClose: () => void;
}) {
  const { width, height } = size;
  return (
    <Box style={{ width, height, "background-color": "#000000" }}>
      <Menu width={width} height={height} zoom={zoom} tree={tree} onClose={onClose} />
    </Box>
  );
}
