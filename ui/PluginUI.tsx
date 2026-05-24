import "./runtime/console";

import { View, Render, Dimensions } from "lvgljs-ui";
import { useCallback, useEffect, useRef, useState } from "react";
import { createGroup, setKeyboardGroup, on, off } from "lvgljs";

import { plugin } from "./plugin/client";
import { KitEditor } from "./KitEditor";
import { BindingsEditor } from "./BindingsEditor";
import { SystemGrid, SystemEntry, SystemLayout, gridContentSize, getTileBounds } from "./SystemGrid";
import { DEFAULT_ZOOM } from "./layout";
import { StartScreen } from "./menu/StartScreen";
import { AboutPanel } from "./menu/AboutPanel";
import { buildInstanceMenu, type RecentEntry } from "./menu/menuDefs";
import {
    GameboyButton,
    KEY_ESCAPE,
    KEY_TAB,
    MOUSE_BUTTON_LEFT,
    MOUSE_BUTTON_RIGHT,
    installBindings,
    mapGamepadButtonToGameboyButton,
    mapKeyToGameboyButton,
    useGamepadButton,
    useKeyboard,
    useMouse,
} from "../runtime/lvgljs/input";

function PluginUI() {
    // Menu starts open — empty project shows the start menu. The empty-
    // project-implies-open invariant is enforced in the [systems.length]
    // effect below, so we just need a sensible initial value here.
    const [menuOpen, setMenuOpen] = useState(true);
    // When the menu is open over a specific tile, this is its system id;
    // null otherwise. The grid swaps that tile's EmulatorTile for <Menu>
    // so siblings keep rendering. Stays null on the start screen (no tile
    // exists to anchor to) — that path renders <StartScreen> instead.
    const [menuSystemId, setMenuSystemId] = useState<number | null>(null);
    // Kit editor overlays the system grid and menu when open. Esc closes
    // the editor first; only after it's closed does Esc fall through to
    // the menu toggle. Reachable from the main menu's "Kit Editor" item.
    const [kitEditorOpen, setKitEditorOpen] = useState(false);
    const [aboutOpen, setAboutOpen] = useState(false);
    // Bindings editor (separate modals for keyboard and gamepad — see
    // ui/BindingsEditor.tsx). Like kitEditor / aboutPanel, both consume
    // keyboard / gamepad events themselves while open, so PluginUI must
    // not also dispatch game button presses for those events below.
    const [keyboardEditorOpen, setKeyboardEditorOpen] = useState(false);
    const [gamepadEditorOpen,  setGamepadEditorOpen]  = useState(false);
    const [systems, setSystems] = useState<SystemEntry[]>([]);
    const [focusedId, setFocusedId] = useState<number>(0);
    // Seeded to 0 and replaced by the first refreshSystems(). The brief
    // 0-flash before that arrives is acceptable — the menu is open at
    // mount and the routing label only matters once a tile exists.
    const [midiRouting, setMidiRouting] = useState<number>(0);
    const [audioRouting, setAudioRouting] = useState<number>(0);
    const [layout, setLayout] = useState<number>(0);
    // Integer zoom 1..6. Initialized to DEFAULT_ZOOM; replaced by the first
    // getZoom() in refreshSystems (which resolves project setting and
    // user-config default on the C++ side).
    const [zoom, setZoom] = useState<number>(DEFAULT_ZOOM);
    // Recent ROMs + projects. Fetched on mount and on "recent-files-changed".
    const [recentFiles, setRecentFiles] = useState<RecentEntry[]>([]);

    const menuOpenRef = useRef(menuOpen);
    useEffect(() => { menuOpenRef.current = menuOpen; }, [menuOpen]);

    const systemsRef = useRef(systems);
    useEffect(() => { systemsRef.current = systems; }, [systems]);

    const focusedIdRef = useRef(focusedId);
    useEffect(() => { focusedIdRef.current = focusedId; }, [focusedId]);

    // Mirror zoom into a ref so the mouse-hit-test useCallback (deps [])
    // can read the current value without being recreated on every zoom change.
    const zoomRef = useRef(DEFAULT_ZOOM);
    useEffect(() => { zoomRef.current = zoom; }, [zoom]);

    // Empty "input sink" group, claimed as the keyboard group whenever the
    // menu isn't open. lv_binding_js's setKeyboardGroup(null) falls back to
    // lv_group_get_default() — which contains every tile View (every lvgl-js
    // View is unconditionally added) and is CLICK_FOCUSABLE, so LVGL would
    // happily route arrows / Enter / Tab to those tiles, fire LV_EVENT_PRESSED
    // on Enter (= "click the focused tile"), and generally mangle game-mode
    // input. Pointing the keypad at an empty group leaves keys for our JS
    // handler with no LVGL side-effects.
    const sinkGroupRef = useRef<any>(null);
    useEffect(() => {
        const sink = createGroup();
        sinkGroupRef.current = sink;
        setKeyboardGroup(sink);
        return () => {
            setKeyboardGroup(null);
            sink.destroy();
            sinkGroupRef.current = null;
        };
    }, []);

    // Pull the current system list and focus from C++. Called on mount and
    // every "config-changed" tick (after the DSP commits a project mutation).
    const refreshSystems = useCallback(async () => {
        const [list, f, routing, audio, z, l] = await Promise.all([
            plugin.listSystems(),
            plugin.getFocus(),
            plugin.getMidiRouting(),
            plugin.getAudioRouting(),
            plugin.getZoom(),
            plugin.getLayout(),
        ]);
        setSystems(list);
        if (f !== 0 && list.some((s) => s.id === f)) {
            setFocusedId(f);
        } else if (list.length > 0) {
            setFocusedId(list[0].id);
            void plugin.$notify("setFocus", list[0].id);
        } else {
            setFocusedId(0);
        }
        setMidiRouting(routing);
        setAudioRouting(audio);
        setLayout(l);
        setZoom(z >= 1 && z <= 6 ? z : DEFAULT_ZOOM);
    }, []);

    useEffect(() => {
        void refreshSystems();
        const handler = () => { void refreshSystems(); };
        on("config-changed", handler);
        return () => off("config-changed", handler);
    }, [refreshSystems]);

    // User-config / bindings: fetch on mount, rebuild the runtime key+pad
    // maps. The C++ side emits "user-config-changed" whenever the watcher
    // detects a JSON edit (or setActiveBindings is called); we refetch and
    // reinstall so the next keystroke uses the new map.
    useEffect(() => {
        const apply = async () => {
            try {
                const cfg = await plugin.getUserConfig();
                if (cfg && cfg.bindings) installBindings(cfg.bindings);
                if (cfg) {
                    setActiveKeyboardBindings(cfg.activeKeyboardBindings ?? "");
                    setActiveGamepadBindings(cfg.activeGamepadBindings ?? "");
                    setAvailableProfiles(cfg.availableProfiles ?? []);
                }
            } catch (e) {
                console.warn("[bindings] getUserConfig failed", e);
            }
        };
        void apply();
        const handler = () => { void apply(); };
        on("user-config-changed", handler);
        return () => off("user-config-changed", handler);
    }, []);

    // Active bindings profiles + the list available for cycling in the menu.
    // Updated alongside installBindings on user-config-changed.
    const [activeKeyboardBindings, setActiveKeyboardBindings] = useState<string>("");
    const [activeGamepadBindings,  setActiveGamepadBindings]  = useState<string>("");
    const [availableProfiles,      setAvailableProfiles]      = useState<string[]>([]);

    // Recent files. Same pattern as user-config: fetch on mount, refetch
    // when C++ emits "recent-files-changed" after a successful load/save.
    useEffect(() => {
        const apply = async () => {
            try {
                const list = await plugin.getRecentFiles();
                setRecentFiles(list as RecentEntry[]);
            } catch (e) {
                console.warn("[recent-files] getRecentFiles failed", e);
            }
        };
        void apply();
        const handler = () => { void apply(); };
        on("recent-files-changed", handler);
        return () => off("recent-files-changed", handler);
    }, []);

    // Menu visibility invariant: empty project => menu always open. Adding
    // the first system auto-closes the menu so the user sees the new tile.
    // Removing the last system also drops any remembered project path so a
    // subsequent Save dialog can't default-target the previously loaded file.
    const prevSystemsLenRef = useRef(systems.length);
    useEffect(() => {
        if (systems.length === 0) {
            setMenuOpen(true);
            if (prevSystemsLenRef.current > 0) {
                void plugin.$notify("clearCurrentProjectPath");
            }
        } else if (menuOpenRef.current) {
            setMenuOpen(false);
        }
        prevSystemsLenRef.current = systems.length;
    }, [systems.length]);

    // Window resizing: ask the host/WM for a window that fits the current
    // grid at the current zoom. On a tiled WM (Hyprland) the request is
    // silently ignored — the C++ side detects that via onResize and we
    // stop asking. Zoom changes flow through here too — the dep array
    // includes `zoom` so the menu's "Zoom: Nx" cycle resizes the window.
    useEffect(() => {
        if (systems.length === 0) return;
        void (async () => {
            if (await plugin.isWindowSizeControlled()) return;
            const { width, height } = gridContentSize(systems, SystemLayout.Auto, zoom);
            await plugin.setWindowSize(width, height);
        })();
    }, [systems.length, zoom]);

    // Records which system each currently-held DPF key was pressed against,
    // so the release always goes to the same instance (even after a Tab cycle
    // changes focus mid-hold). Survives renders via useRef. Also gates OS
    // key-repeat: every platform delivers a stream of `press=true` events
    // while a key is held (Win32/macOS natively; X11 too once pugl enables
    // detectable auto-repeat in puglInitWorldInternals — see x11.c). We
    // drop the repeats here because the GB joypad samples state and only
    // needs one transition per physical press.
    const keyTargetRef = useRef<Map<number, number>>(new Map());

    const kitEditorOpenRef = useRef(kitEditorOpen);
    useEffect(() => { kitEditorOpenRef.current = kitEditorOpen; }, [kitEditorOpen]);

    const aboutOpenRef = useRef(aboutOpen);
    useEffect(() => { aboutOpenRef.current = aboutOpen; }, [aboutOpen]);

    const keyboardEditorOpenRef = useRef(keyboardEditorOpen);
    useEffect(() => { keyboardEditorOpenRef.current = keyboardEditorOpen; }, [keyboardEditorOpen]);

    const gamepadEditorOpenRef = useRef(gamepadEditorOpen);
    useEffect(() => { gamepadEditorOpenRef.current = gamepadEditorOpen; }, [gamepadEditorOpen]);

    useKeyboard(useCallback((key: number, press: boolean) => {
        // Bindings editor owns its own keyboard handling (capture mode,
        // prompt input, Esc routing). Bail out early so nothing here
        // intercepts keystrokes meant for it. Closing the editor via Esc
        // is handled inside the editor itself.
        if (keyboardEditorOpenRef.current || gamepadEditorOpenRef.current) {
            return;
        }
        if (key === KEY_ESCAPE) {
            // Esc closes the kit editor before falling through to menu.
            if (press && kitEditorOpenRef.current) {
                setKitEditorOpen(false);
                return;
            }
            if (press && aboutOpenRef.current) {
                setAboutOpen(false);
                if (systemsRef.current.length === 0) setMenuOpen(true);
                return;
            }
            if (!press) return;
            // Start screen (no instances): Esc is a no-op. The menu must
            // stay open per the empty-project invariant, and submenu
            // expand/collapse is handled inline by activating the header.
            if (systemsRef.current.length === 0) return;
            // Per-instance: Esc toggles the menu. Opening via Esc anchors
            // it to the currently focused tile; closing leaves the id —
            // closeMenu / the next opener will overwrite.
            setMenuOpen(o => {
                if (!o) setMenuSystemId(focusedIdRef.current);
                return !o;
            });
            return;
        }
        // Kit editor / About consume their own key events through their child group.
        if (kitEditorOpenRef.current) return;
        if (aboutOpenRef.current) return;
        if (menuOpenRef.current) {
            // Menu is open — LVGL focus group routes the key to the focused
            // item; nothing for us to do here.
            return;
        }
        if (key === KEY_TAB) {
            if (!press) return;
            const list = systemsRef.current;
            if (list.length < 2) return;
            const cur = focusedIdRef.current;
            const idx = list.findIndex((s) => s.id === cur);
            const next = list[(idx + 1) % list.length];
            setFocusedId(next.id);
            void plugin.$notify("setFocus", next.id);
            return;
        }
        const button: GameboyButton | null = mapKeyToGameboyButton(key);
        if (button === null) return;

        // pressButton is a notification ($notify): no response, no Promise
        // round-trip per keystroke. Keyboard input runs on the hot LVGL
        // dispatch path and we don't want a microtask per key event.
        const targets = keyTargetRef.current;
        if (press) {
            if (targets.has(key)) return;
            const target = focusedIdRef.current;
            if (target === 0) return;
            targets.set(key, target);
            void plugin.$notify("pressButton", button, true, target);
        } else {
            const target = targets.get(key);
            if (target === undefined) return;
            targets.delete(key);
            void plugin.$notify("pressButton", button, false, target);
        }
    }, []));

    // Click on a tile: focus it. Right-click also opens the menu. The mouse
    // event delivers window-relative coords, but getTileBounds is grid-local.
    // The root View flex-centers the grid in the window, so when the
    // compositor gives us a window bigger than gridContentSize (tiled WM,
    // host-clamped resize) there's a centering offset we must subtract.
    useMouse(useCallback((button: number, press: boolean, x: number, y: number) => {
        if (!press) return;
        if (button !== MOUSE_BUTTON_LEFT && button !== MOUSE_BUTTON_RIGHT) return;
        if (kitEditorOpenRef.current) return;
        if (button === MOUSE_BUTTON_LEFT && menuOpenRef.current) return;
        const list = systemsRef.current;
        if (list.length === 0) return;
        const z = zoomRef.current;
        const grid = gridContentSize(list, SystemLayout.Auto, z);
        const win = Dimensions.window;
        const offX = Math.max(0, (win.width  - grid.width)  / 2);
        const offY = Math.max(0, (win.height - grid.height) / 2);
        const gx = x - offX;
        const gy = y - offY;
        for (let i = 0; i < list.length; i++) {
            const b = getTileBounds(i, list.length, SystemLayout.Auto, z);
            if (gx >= b.x && gx < b.x + b.w && gy >= b.y && gy < b.y + b.h) {
                const sys = list[i];
                if (sys.id !== focusedIdRef.current) {
                    setFocusedId(sys.id);
                    void plugin.$notify("setFocus", sys.id);
                }
                if (button === MOUSE_BUTTON_RIGHT && !menuOpenRef.current) {
                    setMenuSystemId(sys.id);
                    setMenuOpen(true);
                }
                return;
            }
        }
    }, []));

    // Same release-to-original-target bookkeeping as keyTargetRef above, but
    // keyed by `${padId}:${buttonName}` since a single button is unique per
    // pad. Lets multiple pads drive independently without one's release
    // hijacking another's hold.
    const padTargetRef = useRef<Map<string, number>>(new Map());

    useGamepadButton(useCallback((pad: number, buttonName: string, press: boolean) => {
        if (menuOpenRef.current) return;
        // Bindings editor needs raw gamepad events for capture mode and
        // must not also see them turn into game-button presses.
        if (keyboardEditorOpenRef.current || gamepadEditorOpenRef.current) return;
        const button = mapGamepadButtonToGameboyButton(buttonName);
        if (button === null) return;
        const slot = `${pad}:${buttonName}`;
        const targets = padTargetRef.current;
        if (press) {
            if (targets.has(slot)) return;
            const target = focusedIdRef.current;
            if (target === 0) return;
            targets.set(slot, target);
            void plugin.$notify("pressButton", button, true, target);
        } else {
            const target = targets.get(slot);
            if (target === undefined) return;
            targets.delete(slot);
            void plugin.$notify("pressButton", button, false, target);
        }
    }, []));

    const focusedSystem = systems.find((s) => s.id === focusedId);

    const closeMenu = useCallback(() => {
        // Don't force-close if there are no instances (start screen's empty-
        // project invariant). The [systems.length] effect re-opens the menu
        // when length goes back to 0.
        if (systemsRef.current.length > 0) {
            setMenuOpen(false);
            setMenuSystemId(null);
        }
    }, []);

    const openKitEditor = useCallback(() => {
        setMenuOpen(false);
        setKitEditorOpen(true);
    }, []);

    const openAbout = useCallback(() => {
        setMenuOpen(false);
        setAboutOpen(true);
    }, []);

    const openKeyboardEditor = useCallback(() => {
        setMenuOpen(false);
        setKeyboardEditorOpen(true);
    }, []);

    const openGamepadEditor = useCallback(() => {
        setMenuOpen(false);
        setGamepadEditorOpen(true);
    }, []);

    const instanceTree = buildInstanceMenu({
        systems,
        focusedSystem,
        midiRouting,
        audioRouting,
        layout,
        zoom,
        recentFiles,
        openKitEditor,
        openAbout,
        openKeyboardEditor,
        openGamepadEditor,
        availableProfiles,
        activeKeyboardBindings,
        activeGamepadBindings,
    });

    return (
        <View
            style={{
                width: "100%",
                height: "100%",
                "background-color": "#000000",
                "background-opacity": 255,
                "border-width": 0,
                "border-opacity": 0,
                "border-radius": 0,
                "padding-left":  0,
                "padding-right": 0,
                "padding-top":   0,
                "padding-bottom":0,
                display: "flex",
                "flex-direction": "row",
                "align-items": "center",
                "justify-content": "center",
                "row-spacing": 0,
                "column-spacing": 0,
                overflow: "hidden",
            }}
        >
            {kitEditorOpen ? (
                <KitEditor
                    systemId={focusedId}
                    sinkGroup={sinkGroupRef.current}
                    onClose={() => setKitEditorOpen(false)}
                />
            ) : keyboardEditorOpen ? (
                <BindingsEditor
                    kind="keyboard"
                    zoom={zoom}
                    sinkGroup={sinkGroupRef.current}
                    onClose={() => {
                        setKeyboardEditorOpen(false);
                        if (systemsRef.current.length === 0) setMenuOpen(true);
                    }}
                />
            ) : gamepadEditorOpen ? (
                <BindingsEditor
                    kind="gamepad"
                    zoom={zoom}
                    sinkGroup={sinkGroupRef.current}
                    onClose={() => {
                        setGamepadEditorOpen(false);
                        if (systemsRef.current.length === 0) setMenuOpen(true);
                    }}
                />
            ) : aboutOpen ? (
                <AboutPanel
                    zoom={zoom}
                    onClose={() => {
                        setAboutOpen(false);
                        if (systemsRef.current.length === 0) setMenuOpen(true);
                    }}
                    sinkGroup={sinkGroupRef.current}
                />
            ) : systems.length === 0 ? (
                <StartScreen
                    midiRouting={midiRouting}
                    audioRouting={audioRouting}
                    layout={layout}
                    zoom={zoom}
                    recentFiles={recentFiles}
                    openAbout={openAbout}
                    openKeyboardEditor={openKeyboardEditor}
                    openGamepadEditor={openGamepadEditor}
                    availableProfiles={availableProfiles}
                    activeKeyboardBindings={activeKeyboardBindings}
                    activeGamepadBindings={activeGamepadBindings}
                    sinkGroup={sinkGroupRef.current}
                />
            ) : (
                <SystemGrid
                    systems={systems}
                    focusedId={focusedId}
                    layout={SystemLayout.Auto}
                    zoom={zoom}
                    menuSystemId={menuOpen ? menuSystemId : null}
                    menuTree={instanceTree}
                    onMenuClose={closeMenu}
                    sinkGroup={sinkGroupRef.current}
                />
            )}
        </View>
    );
}

Render.render(<PluginUI />);
