import { View, Render } from "lvgljs-ui";
import { useCallback, useEffect, useRef, useState } from "react";
import { createGroup, setKeyboardGroup, on, off } from "lvgljs";

import { plugin } from "./plugin/client";
import { KitEditor } from "./KitEditor";
import { SystemGrid, SystemEntry, SystemLayout, gridContentSize, getTileBounds } from "./SystemGrid";
import { Menu } from "./menu/Menu";
import { StartScreen } from "./menu/StartScreen";
import { useMenuStack } from "./menu/MenuStack";
import { buildInstanceMenu } from "./menu/menuDefs";
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

    // Menu back-stack. Lifted into PluginUI (rather than living inside the
    // Menu component) so the Esc handler can pop one level before falling
    // through to "close the menu" — Esc lives in the global useKeyboard
    // handler below.
    const menu = useMenuStack("root");

    const menuOpenRef = useRef(menuOpen);
    useEffect(() => { menuOpenRef.current = menuOpen; }, [menuOpen]);

    const systemsRef = useRef(systems);
    useEffect(() => { systemsRef.current = systems; }, [systems]);

    const focusedIdRef = useRef(focusedId);
    useEffect(() => { focusedIdRef.current = focusedId; }, [focusedId]);

    const menuCanPopRef = useRef(menu.canPop);
    useEffect(() => { menuCanPopRef.current = menu.canPop; }, [menu.canPop]);

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
        const [list, f, routing] = await Promise.all([
            plugin.listSystems(),
            plugin.getFocus(),
            plugin.getMidiRouting(),
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

    // Menu visibility invariant: empty project => menu always open. Adding
    // the first system auto-closes the menu so the user sees the new tile.
    useEffect(() => {
        if (systems.length === 0) {
            setMenuOpen(true);
            menu.reset();
        } else if (menuOpenRef.current) {
            setMenuOpen(false);
        }
    }, [systems.length]);

    // Window resizing: ask the host/WM for a window that fits the current
    // grid at native zoom. On a tiled WM (Hyprland) the request is silently
    // ignored — the C++ side detects that via onResize and we stop asking.
    useEffect(() => {
        if (systems.length === 0) return;
        void (async () => {
            if (await plugin.isWindowSizeControlled()) return;
            const { width, height } = gridContentSize(systems, SystemLayout.Auto);
            await plugin.setWindowSize(width, height);
        })();
    }, [systems.length]);

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
            // Esc with empty project does nothing — the menu must stay open.
            if (systemsRef.current.length === 0) return;
            // Menu open + on a submenu → pop one level. Menu open + at
            // root → close. Menu closed → open at root.
            if (menuOpenRef.current) {
                if (menuCanPopRef.current) {
                    menu.pop();
                } else {
                    setMenuOpen(false);
                }
            } else {
                menu.reset();
                setMenuOpen(true);
            }
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
    }, [menu]));

    // Right-click on a tile: focus the tile + open the menu over it. Position
    // resolution: getTileBounds gives the tile's rect inside the grid; the
    // grid is flex-centered by the root, so the same hit-test applies after
    // subtracting the grid's centering offset from the cursor coordinates.
    useMouse(useCallback((button: number, press: boolean, x: number, y: number) => {
        if (button !== MOUSE_BUTTON_RIGHT || !press) return;
        if (kitEditorOpenRef.current) return;
        const list = systemsRef.current;
        if (list.length === 0) return;
        // Hit-test against the grid's local coordinate system. We don't
        // know the window size in JS, but the grid is centered in whatever
        // size DPF gave us — and at native zoom, when the WM honors our
        // setWindowSize request, the grid fills the window exactly so
        // window coords == grid coords. On a tiled WM the menu may be
        // slightly off; we accept that for now rather than plumbing a
        // window-size RPC just for hit-test math.
        for (let i = 0; i < list.length; i++) {
            const b = getTileBounds(i, list.length, SystemLayout.Auto);
            if (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h) {
                const sys = list[i];
                if (sys.id !== focusedIdRef.current) {
                    setFocusedId(sys.id);
                    void plugin.$notify("setFocus", sys.id);
                }
                if (!menuOpenRef.current) {
                    menu.reset();
                    setMenuOpen(true);
                } else {
                    // Re-anchoring: focus changed but menu was already
                    // open. Reset the back-stack so the new instance's
                    // menu starts at root.
                    menu.reset();
                }
                return;
            }
        }
    }, [menu]));

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
        // Don't force-close here if removing the last instance emptied the
        // project; the [systems.length] effect re-opens the menu in that case.
        if (systemsRef.current.length > 0) setMenuOpen(false);
    }, []);

    const openKitEditor = useCallback(() => {
        setMenuOpen(false);
        setKitEditorOpen(true);
    }, []);

    // Compute the focused tile's bounds for menu anchoring. Falls back to
    // (0, 0) if there's no focus yet — the empty-project branch renders
    // StartScreen instead, so the menu in this case never reads x/y.
    const focusedIndex = focusedSystem
        ? systems.findIndex((s) => s.id === focusedSystem.id)
        : -1;
    const tileBounds = focusedIndex >= 0
        ? getTileBounds(focusedIndex, systems.length, SystemLayout.Auto)
        : null;

    const instancePanes = buildInstanceMenu({
        systems,
        focusedSystem,
        midiRouting,
        closeMenu,
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
                    currentPaneId={menu.currentId}
                    onPush={menu.push}
                    onPop={menu.pop}
                    sinkGroup={sinkGroupRef.current}
                />
            ) : (
                <SystemGrid
                    systems={systems}
                    focusedId={focusedId}
                    layout={SystemLayout.Auto}
                    overlay={menuOpen && tileBounds ? (
                        <Menu
                            x={tileBounds.x}
                            y={tileBounds.y}
                            width={tileBounds.w}
                            height={tileBounds.h}
                            panes={instancePanes}
                            currentPaneId={menu.currentId}
                            onPush={menu.push}
                            onPop={menu.pop}
                            onClose={closeMenu}
                            sinkGroup={sinkGroupRef.current}
                        />
                    ) : undefined}
                />
            )}
        </View>
    );
}

Render.render(<PluginUI />);
