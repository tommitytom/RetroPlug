import { View, Text, Slider, Render, ELvKey } from "lvgljs-ui";
import { useCallback, useEffect, useRef, useState } from "react";
import { useParameter, createGroup, setKeyboardGroup, on, off } from "lvgljs";

import { plugin } from "./plugin/client";
import { KitEditor } from "./KitEditor";
import { SystemGrid, SystemEntry, SystemLayout, gridContentSize } from "./SystemGrid";
import {
    GameboyButton,
    KEY_ESCAPE,
    KEY_TAB,
    mapGamepadButtonToGameboyButton,
    mapKeyToGameboyButton,
    useGamepadButton,
    useKeyboard,
} from "../runtime/lvgljs/input";

// Menu structure. "Link group: N" cycles 0..LINK_GROUP_MAX-1 on each click;
// 0 = standalone. The label re-renders to show the focused instance's
// current group; clicking it does NOT close the menu (so users can cycle
// without re-opening every time).
const LINK_GROUP_LABEL = "Link group:";
const LINK_GROUP_MAX   = 4;

// Project-wide MIDI routing. Same cycling pattern as Link group — mode names
// map 1:1 onto the C++ MidiRouting enum (see src/project/ProjectConfig.hpp).
const MIDI_ROUTING_LABEL = "MIDI routing:";
const MIDI_ROUTING_NAMES = [
    "Send to all",
    "4 ch / inst",
    "1 ch / inst",
    "ch → inst",
];

// LSDJ sync mode picker. Visible only when the focused system has an LSDJ
// sync role (i.e. an LSDJ ROM is loaded). Names index 1:1 onto the C++
// LsdjSyncMode enum (src/system/sameboy/roles/LsdjSyncRole.hpp); the enum
// is append-only so this array can be safely indexed by value.
const KIT_EDITOR_LABEL = "Kit Editor";
const LSDJ_MODE_LABEL = "LSDJ mode:";
const LSDJ_MODE_NAMES = [
    "Off",
    "MidiSync",
    "Arduinoboy",
    "MidiMap",
    "Keyboard",
    "KeyboardMidi",
    "Passthrough",
    "MI.OUT",
];

const TextAny = Text as any;

interface MenuOverlayProps {
    gain: number;
    items: string[];
    sinkGroup: any;
    onGainChange: (e: any) => void;
    onSelect: (label: string) => void;
}

function MenuOverlay({ gain, items, sinkGroup, onGainChange, onSelect }: MenuOverlayProps) {
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
            // Hand keyboard back to the parent's empty sink group rather
            // than passing `null` — lvgl-js falls back to the default group
            // on null, which contains every tile View and would let LVGL
            // route arrows / Enter to those tiles instead of leaving the
            // keys for our JS handler.
            setKeyboardGroup(sinkGroup ?? null);
            group.destroy();
            groupRef.current = null;
        };
    }, [sinkGroup]);

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

    const menuOpenRef = useRef(menuOpen);
    useEffect(() => { menuOpenRef.current = menuOpen; }, [menuOpen]);

    const systemsRef = useRef(systems);
    useEffect(() => { systemsRef.current = systems; }, [systems]);

    const focusedIdRef = useRef(focusedId);
    useEffect(() => { focusedIdRef.current = focusedId; }, [focusedId]);

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
    // Async because the migrated bridge returns Promises; we serialise the
    // three reads so a `config-changed` burst doesn't fire setState with
    // stale data from a slower listSystems racing a fast getFocus.
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

    // Single source of truth for keyboard routing. C++ forwards every key
    // event to JS via the "key" channel; this handler decides whether it
    // becomes a menu toggle, a Tab cycle, a Game Boy button, or is ignored.
    const kitEditorOpenRef = useRef(kitEditorOpen);
    useEffect(() => { kitEditorOpenRef.current = kitEditorOpen; }, [kitEditorOpen]);

    useKeyboard(useCallback((key: number, press: boolean) => {
        if (key === KEY_ESCAPE) {
            // Esc closes the kit editor before falling through to menu.
            if (press && kitEditorOpenRef.current) {
                setKitEditorOpen(false);
                return;
            }
            // Esc with empty project does nothing — the menu must stay open.
            if (press && systemsRef.current.length > 0)
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
            // Already-held key: ignore the repeat. The GB joypad already
            // sees this button as pressed; firing again would route a
            // duplicate to the same instance (harmless but pointless).
            if (targets.has(key)) return;
            const target = focusedIdRef.current;
            if (target === 0) return;
            targets.set(key, target);
            void plugin.$notify("pressButton", button, true, target);
        } else {
            // Route the release to whichever instance got the original
            // press — never to whatever happens to be focused right now.
            const target = targets.get(key);
            if (target === undefined) return; // spurious release
            targets.delete(key);
            void plugin.$notify("pressButton", button, false, target);
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

    // Build the menu items with the focused instance's link group baked
    // into the label. Re-computed every render so the cycling Link group
    // entry shows the current value without an explicit refresh hop.
    const focusedSystem = systems.find((s) => s.id === focusedId);
    const linkGroupSuffix = focusedSystem
        ? String(focusedSystem.linkGroupId ?? 0)
        : "-";
    const routingName = MIDI_ROUTING_NAMES[midiRouting] ?? MIDI_ROUTING_NAMES[0];
    // msgpack absent-optional decodes to `null`, not `undefined` — `!= null`
    // catches both. (Was `!== undefined` pre-RPC.)
    const hasLsdjRole = focusedSystem?.lsdjSyncMode != null;
    const hasKitRole  = focusedSystem?.hasLsdjKitRole === true;
    const lsdjModeName = hasLsdjRole
        ? LSDJ_MODE_NAMES[focusedSystem!.lsdjSyncMode ?? 0] ?? LSDJ_MODE_NAMES[0]
        : "";
    const menuItems = [
        "Load ROM",
        "Add instance",
        "Remove instance",
        `${LINK_GROUP_LABEL} ${linkGroupSuffix}`,
        `${MIDI_ROUTING_LABEL} ${routingName}`,
        ...(hasLsdjRole ? [`${LSDJ_MODE_LABEL} ${lsdjModeName}`] : []),
        ...(hasKitRole  ? [KIT_EDITOR_LABEL]                     : []),
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
            void plugin.$notify("setLinkGroupId", sys.id, next);
            return;
        }
        // Same cycling behaviour for the MIDI routing label.
        if (label.startsWith(MIDI_ROUTING_LABEL)) {
            const next = (midiRouting + 1) % MIDI_ROUTING_NAMES.length;
            // Optimistically update so the label flips immediately; the
            // ConfigChanged event will reconcile shortly after.
            setMidiRouting(next);
            void plugin.$notify("setMidiRouting", next);
            return;
        }
        // LSDJ sync mode cycle. Reads through to the most recent systems
        // snapshot (refresh on ConfigChanged) so the label tracks the picker.
        if (label.startsWith(LSDJ_MODE_LABEL)) {
            const sys = systemsRef.current.find((s) => s.id === focusedIdRef.current);
            if (!sys || sys.lsdjSyncMode == null) return;
            const next = (sys.lsdjSyncMode + 1) % LSDJ_MODE_NAMES.length;
            void plugin.$notify("setLsdjSyncConfig", sys.id, next, sys.lsdjTempoDivisor ?? 1);
            return;
        }
        switch (label) {
            case "Load ROM":
                void plugin.$notify("openRomBrowser", { mode: "replace" });
                break;
            case "Add instance":
                void plugin.$notify("openRomBrowser", { mode: "add" });
                break;
            case "Remove instance":
                if (focusedIdRef.current !== 0)
                    void plugin.$notify("removeSystem", focusedIdRef.current);
                break;
            case "Save project":
                void plugin.$notify("openSaveProjectBrowser");
                break;
            case "Load project":
                void plugin.$notify("openLoadProjectBrowser");
                break;
            case KIT_EDITOR_LABEL:
                // Close the menu and open the editor over the system grid.
                setMenuOpen(false);
                setKitEditorOpen(true);
                return;
            case "Reset":
            case "About":
            case "Cancel":
            default:
                break;
        }
        // Don't force-close here; the empty-project effect will keep the
        // menu open if Remove emptied the project.
        if (systemsRef.current.length > 0) setMenuOpen(false);
    }, [midiRouting]);

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
                    sinkGroup={sinkGroupRef.current}
                    onGainChange={onGainChange}
                    onSelect={onMenuSelect}
                />
            ) : kitEditorOpen ? (
                <KitEditor
                    systemId={focusedId}
                    sinkGroup={sinkGroupRef.current}
                    onClose={() => setKitEditorOpen(false)}
                />
            ) : (
                <SystemGrid systems={systems} focusedId={focusedId} layout={SystemLayout.Auto} />
            )}
        </View>
    );
}

Render.render(<PluginUI />);
