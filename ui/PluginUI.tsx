import { View, Render } from "lvgljs-ui";
import { useCallback, useEffect, useRef, useState } from "react";
import { createGroup, setKeyboardGroup, on, off } from "lvgljs";

import { plugin } from "./plugin/client";
import { KitEditor } from "./KitEditor";
import { SystemGrid, SystemEntry, SystemLayout, gridContentSize, getTileBounds } from "./SystemGrid";
import { DEFAULT_ZOOM, tileWidth, tileHeight } from "./layout";
import { Menu } from "./menu/Menu";
import { StartScreen } from "./menu/StartScreen";
import { buildInstanceMenu, type RecentEntry } from "./menu/menuDefs";
import {
    GameboyButton,
    KEY_ESCAPE,
    KEY_TAB,
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
    // Kit editor overlays the system grid and menu when open. Esc closes
    // the editor first; only after it's closed does Esc fall through to
    // the menu toggle. Reachable from the main menu's "Kit Editor" item.
    const [kitEditorOpen, setKitEditorOpen] = useState(false);
    const [systems, setSystems] = useState<SystemEntry[]>([]);
    const [focusedId, setFocusedId] = useState<number>(0);
    // Seeded to 0 and replaced by the first refreshSystems(). The brief
    // 0-flash before that arrives is acceptable — the menu is open at
    // mount and the routing label only matters once a tile exists.
    const [midiRouting, setMidiRouting] = useState<number>(0);
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
        const [list, f, routing, z] = await Promise.all([
            plugin.listSystems(),
            plugin.getFocus(),
            plugin.getMidiRouting(),
            plugin.getZoom(),
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
            } catch (e) {
                console.warn("[bindings] getUserConfig failed", e);
            }
        };
        void apply();
        const handler = () => { void apply(); };
        on("user-config-changed", handler);
        return () => off("user-config-changed", handler);
    }, []);

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
    // changes focus mid-hold). Survives renders via useRef. Also gates
    // OS key-repeat: a second `press=true` for an already-held key is
    // ignored — the GB joypad samples state, no need to re-fire.
    const keyTargetRef = useRef<Map<number, number>>(new Map());

    const kitEditorOpenRef = useRef(kitEditorOpen);
    useEffect(() => { kitEditorOpenRef.current = kitEditorOpen; }, [kitEditorOpen]);

    useKeyboard(useCallback((key: number, press: boolean) => {
        if (key === KEY_ESCAPE) {
            // Esc closes the kit editor before falling through to menu.
            if (press && kitEditorOpenRef.current) {
                setKitEditorOpen(false);
                return;
            }
            if (!press) return;
            // Start screen (no instances): Esc is a no-op. The menu must
            // stay open per the empty-project invariant, and submenu
            // expand/collapse is handled inline by activating the header.
            if (systemsRef.current.length === 0) return;
            // Per-instance: Esc toggles the menu.
            setMenuOpen(o => !o);
            return;
        }
        // Kit editor consumes its own key events through its child group.
        if (kitEditorOpenRef.current) return;
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

    // Right-click on a tile: focus the tile + open the menu. Position
    // resolution: getTileBounds gives the tile's rect inside the grid; we
    // assume window coords ≈ grid coords (true at native zoom on most WMs
    // because we drive setWindowSize to match the grid content size).
    useMouse(useCallback((button: number, press: boolean, x: number, y: number) => {
        if (button !== MOUSE_BUTTON_RIGHT || !press) return;
        if (kitEditorOpenRef.current) return;
        const list = systemsRef.current;
        if (list.length === 0) return;
        for (let i = 0; i < list.length; i++) {
            const b = getTileBounds(i, list.length, SystemLayout.Auto, zoomRef.current);
            if (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h) {
                const sys = list[i];
                if (sys.id !== focusedIdRef.current) {
                    setFocusedId(sys.id);
                    void plugin.$notify("setFocus", sys.id);
                }
                if (!menuOpenRef.current) setMenuOpen(true);
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
        if (systemsRef.current.length > 0) setMenuOpen(false);
    }, []);

    const openKitEditor = useCallback(() => {
        setMenuOpen(false);
        setKitEditorOpen(true);
    }, []);

    const instanceTree = buildInstanceMenu({
        systems,
        focusedSystem,
        midiRouting,
        zoom,
        recentFiles,
        openKitEditor,
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
            ) : systems.length === 0 ? (
                <StartScreen
                    midiRouting={midiRouting}
                    zoom={zoom}
                    recentFiles={recentFiles}
                    sinkGroup={sinkGroupRef.current}
                />
            ) : menuOpen ? (
                <Menu
                    width={tileWidth(zoom)}
                    height={tileHeight(zoom)}
                    zoom={zoom}
                    tree={instanceTree}
                    onClose={closeMenu}
                    sinkGroup={sinkGroupRef.current}
                />
            ) : (
                <SystemGrid systems={systems} focusedId={focusedId} layout={SystemLayout.Auto} zoom={zoom} />
            )}
        </View>
    );
}

Render.render(<PluginUI />);
