import { View, Text, Slider, Render, ELvKey } from "lvgljs-ui";
import { useCallback, useEffect, useRef, useState } from "react";
import { useParameter, createGroup, setKeyboardGroup, on, off } from "lvgljs";

import { SystemGrid, SystemEntry, SystemLayout, gridContentSize } from "./SystemGrid";
import {
    GameboyButton,
    KEY_ESCAPE,
    KEY_TAB,
    mapKeyToGameboyButton,
    useKeyboard,
} from "../runtime/lvgljs/input";

// Menu structure. "Link group: N" cycles 0..LINK_GROUP_MAX-1 on each click;
// 0 = standalone. The label re-renders to show the focused instance's
// current group; clicking it does NOT close the menu (so users can cycle
// without re-opening every time).
const LINK_GROUP_LABEL = "Link group:";
const LINK_GROUP_MAX   = 4;

interface PluginNamespace {
    openRomBrowser?: (opts?: { mode?: "add" | "replace" }) => void;
    openSaveProjectBrowser?: () => void;
    openLoadProjectBrowser?: () => void;
    pressButton?: (button: GameboyButton, down: boolean, systemId?: number) => boolean;
    listSystems?: () => SystemEntry[];
    setFocus?: (systemId: number) => boolean;
    getFocus?: () => number;
    removeSystem?: (systemId: number) => boolean;
    setLinkGroupId?: (systemId: number, groupId: number) => boolean;
    setWindowSize?: (w: number, h: number) => boolean;
    isWindowSizeControlled?: () => boolean;
}
const plugin: PluginNamespace =
    (globalThis as any)[Symbol.for("plugin")] ?? {};

const TextAny = Text as any;

interface MenuOverlayProps {
    gain: number;
    items: string[];
    onGainChange: (e: any) => void;
    onSelect: (label: string) => void;
}

function MenuOverlay({ gain, items, onGainChange, onSelect }: MenuOverlayProps) {
    const itemRefs = useRef<any[]>([]);
    const groupRef = useRef<any>(null);
    const [focusedIndex, setFocusedIndex] = useState(0);

    useEffect(() => {
        const group = createGroup();
        groupRef.current = group;
        for (const item of itemRefs.current) {
            if (item) group.add(item);
        }
        if (itemRefs.current[0]) group.focus(itemRefs.current[0]);
        setKeyboardGroup(group);
        return () => {
            setKeyboardGroup(null);
            group.destroy();
            groupRef.current = null;
        };
    }, []);

    // Arrow nav within the menu's own focus group. Esc is handled at the
    // PluginUI root via useKeyboard — we deliberately don't handle it here so
    // there's exactly one Esc owner.
    const onItemKey = useCallback((e: { key: number }) => {
        const refs = itemRefs.current;
        const group = groupRef.current;
        if (!group || refs.length === 0) return;
        let next = focusedIndex;
        if (e.key === ELvKey.LV_KEY_DOWN || e.key === ELvKey.LV_KEY_RIGHT) {
            next = (focusedIndex + 1) % refs.length;
        } else if (e.key === ELvKey.LV_KEY_UP || e.key === ELvKey.LV_KEY_LEFT) {
            next = (focusedIndex - 1 + refs.length) % refs.length;
        } else {
            return;
        }
        if (refs[next]) group.focus(refs[next]);
    }, [focusedIndex]);

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
                "flex-direction": "column",
                "align-items": "center",
                "justify-content": "center",
                overflow: "hidden",
            }}
        >
            <Text
                style={{
                    "text-color": "#4fc3f7",
                    "font-size": 18,
                    padding: 4,
                }}
            >
                {`Master Gain: ${gain.toFixed(1)} dB`}
            </Text>
            <Slider
                style={{
                    width: 320,
                    height: 10,
                    "background-color": "#2d2d44",
                }}
                range={[-90, 12]}
                value={gain}
                onChange={onGainChange}
            />

            {items.map((label, i) => (
                <TextAny
                    key={i}
                    ref={(r: any) => { itemRefs.current[i] = r; }}
                    style={{
                        "text-color": focusedIndex === i ? "#4fc3f7" : "#ffffff",
                        "font-size": 18,
                        padding: 8,
                    }}
                    onFocus={() => setFocusedIndex(i)}
                    onKey={onItemKey}
                    onClick={() => onSelect(label)}
                >
                    {label}
                </TextAny>
            ))}
        </View>
    );
}

function PluginUI() {
    const [gain, setGain] = useParameter("gain", 0);
    // Menu starts open — empty project shows the start menu. The empty-
    // project-implies-open invariant is enforced in the [systems.length]
    // effect below, so we just need a sensible initial value here.
    const [menuOpen, setMenuOpen] = useState(true);
    const [systems, setSystems] = useState<SystemEntry[]>([]);
    const [focusedId, setFocusedId] = useState<number>(0);

    const menuOpenRef = useRef(menuOpen);
    useEffect(() => { menuOpenRef.current = menuOpen; }, [menuOpen]);

    const systemsRef = useRef(systems);
    useEffect(() => { systemsRef.current = systems; }, [systems]);

    const focusedIdRef = useRef(focusedId);
    useEffect(() => { focusedIdRef.current = focusedId; }, [focusedId]);

    // Pull the current system list and focus from C++. Called on mount and
    // every "config-changed" tick (after the DSP commits a project mutation).
    const refreshSystems = useCallback(() => {
        const list = plugin.listSystems?.() ?? [];
        setSystems(list);
        const f = plugin.getFocus?.() ?? 0;
        if (f !== 0 && list.some((s) => s.id === f)) {
            setFocusedId(f);
        } else if (list.length > 0) {
            setFocusedId(list[0].id);
            plugin.setFocus?.(list[0].id);
        } else {
            setFocusedId(0);
        }
    }, []);

    useEffect(() => {
        refreshSystems();
        const handler = () => refreshSystems();
        on("config-changed", handler);
        return () => off("config-changed", handler);
    }, [refreshSystems]);

    // Menu visibility invariant: empty project => menu always open. Adding
    // the first system auto-closes the menu so the user sees the new tile.
    useEffect(() => {
        if (systems.length === 0) {
            setMenuOpen(true);
        } else if (menuOpenRef.current) {
            setMenuOpen(false);
        }
    }, [systems.length]);

    // Window resizing: ask the host/WM for a window that fits the current
    // grid at native zoom. On a tiled WM (Hyprland) the request is silently
    // ignored — the C++ side detects that via onResize and we stop asking.
    useEffect(() => {
        if (systems.length === 0) return;
        if (plugin.isWindowSizeControlled?.()) return;
        const { width, height } = gridContentSize(systems, SystemLayout.Auto);
        plugin.setWindowSize?.(width, height);
    }, [systems.length]);

    // Single source of truth for keyboard routing. C++ forwards every key
    // event to JS via the "key" channel; this handler decides whether it
    // becomes a menu toggle, a Tab cycle, a Game Boy button, or is ignored.
    useKeyboard(useCallback((key: number, press: boolean) => {
        if (key === KEY_ESCAPE) {
            // Esc with empty project does nothing — the menu must stay open.
            if (press && systemsRef.current.length > 0)
                setMenuOpen(o => !o);
            return;
        }
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
            plugin.setFocus?.(next.id);
            return;
        }
        const button = mapKeyToGameboyButton(key);
        if (button !== null) plugin.pressButton?.(button, press);
    }, []));

    // Build the menu items with the focused instance's link group baked
    // into the label. Re-computed every render so the cycling Link group
    // entry shows the current value without an explicit refresh hop.
    const focusedSystem = systems.find((s) => s.id === focusedId);
    const linkGroupSuffix = focusedSystem
        ? String(focusedSystem.linkGroupId ?? 0)
        : "-";
    const menuItems = [
        "Load ROM",
        "Add instance",
        "Remove instance",
        `${LINK_GROUP_LABEL} ${linkGroupSuffix}`,
        "Save project",
        "Load project",
        "Reset",
        "About",
        "Cancel",
    ];

    const onMenuSelect = useCallback((label: string) => {
        // Cycling the link group must not close the menu — users typically
        // want to hit it a few times in a row.
        if (label.startsWith(LINK_GROUP_LABEL)) {
            const sys = systemsRef.current.find((s) => s.id === focusedIdRef.current);
            if (!sys) return;
            const next = (((sys.linkGroupId ?? 0) + 1) % LINK_GROUP_MAX);
            plugin.setLinkGroupId?.(sys.id, next);
            return;
        }
        switch (label) {
            case "Load ROM":
                plugin.openRomBrowser?.({ mode: "replace" });
                break;
            case "Add instance":
                plugin.openRomBrowser?.({ mode: "add" });
                break;
            case "Remove instance":
                if (focusedIdRef.current !== 0) plugin.removeSystem?.(focusedIdRef.current);
                break;
            case "Save project":
                plugin.openSaveProjectBrowser?.();
                break;
            case "Load project":
                plugin.openLoadProjectBrowser?.();
                break;
            case "Reset":
            case "About":
            case "Cancel":
            default:
                break;
        }
        // Don't force-close here; the empty-project effect will keep the
        // menu open if Remove emptied the project.
        if (systemsRef.current.length > 0) setMenuOpen(false);
    }, []);

    const onGainChange = useCallback((e: any) => setGain(e.value), [setGain]);

    // Root is a flex-centered black canvas. The menu and the grid are
    // mutually exclusive children — when menu is open, only the menu
    // renders (it covers the whole window opaquely anyway, so painting
    // the grid behind it would just waste cycles). When menu is closed,
    // only the grid renders, centered in whatever size the WM gave us.
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
            {menuOpen ? (
                <MenuOverlay
                    gain={gain}
                    items={menuItems}
                    onGainChange={onGainChange}
                    onSelect={onMenuSelect}
                />
            ) : (
                <SystemGrid systems={systems} focusedId={focusedId} layout={SystemLayout.Auto} />
            )}
        </View>
    );
}

Render.render(<PluginUI />);
