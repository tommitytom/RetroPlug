import { View, Text, Slider, Render, ELvKey } from "lvgljs-ui";
import { useCallback, useEffect, useRef, useState } from "react";
import { useParameter, createGroup, setKeyboardGroup } from "lvgljs";

import { EmulatorTile } from "./EmulatorTile";
import {
    GameboyButton,
    KEY_ESCAPE,
    mapKeyToGameboyButton,
    useKeyboard,
} from "../runtime/lvgljs/input";

const MENU_ITEMS = ["Load ROM", "Reset", "About", "Cancel"];
// Single-instance MVP: EmulatorTile renders slot 0; pressButton routes to
// slot 0 in C++. Multi-instance focus tracking arrives at step 5.
const PRIMARY_SYSTEM_ID = 1;

interface PluginNamespace {
    openRomBrowser?: () => void;
    pressButton?: (button: GameboyButton, down: boolean) => boolean;
}
const plugin: PluginNamespace =
    (globalThis as any)[Symbol.for("plugin")] ?? {};

const TextAny = Text as any;

interface MenuOverlayProps {
    gain: number;
    onGainChange: (e: any) => void;
    onSelect: (label: string) => void;
}

function MenuOverlay({ gain, onGainChange, onSelect }: MenuOverlayProps) {
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
                "background-opacity": 180,
                "border-opacity": 0,
                display: "flex",
                "flex-direction": "column",
                "align-items": "center",
                "justify-content": "center",
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

            {MENU_ITEMS.map((label, i) => (
                <TextAny
                    key={label}
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
    const [menuOpen, setMenuOpen] = useState(false);
    const menuOpenRef = useRef(menuOpen);
    useEffect(() => { menuOpenRef.current = menuOpen; }, [menuOpen]);

    // Single source of truth for keyboard routing. C++ forwards every key
    // event to JS via the "key" channel; this handler decides whether it
    // becomes a menu toggle, a Game Boy button, or is ignored (LVGL focus
    // already routed it to a focused menu item).
    useKeyboard(useCallback((key: number, press: boolean) => {
        if (key === KEY_ESCAPE) {
            if (press) setMenuOpen(o => !o);
            return;
        }
        if (menuOpenRef.current) {
            // Menu is open — LVGL focus group routes the key to the focused
            // item; nothing for us to do here.
            return;
        }
        const button = mapKeyToGameboyButton(key);
        if (button !== null) plugin.pressButton?.(button, press);
    }, []));

    const onMenuSelect = useCallback((label: string) => {
        switch (label) {
            case "Load ROM":
                plugin.openRomBrowser?.();
                break;
            case "Reset":
            case "About":
            case "Cancel":
            default:
                break;
        }
        setMenuOpen(false);
    }, []);

    const onGainChange = useCallback((e: any) => setGain(e.value), [setGain]);

    return (
        <View
            style={{
                width: "100%",
                height: "100%",
                "background-color": "#1a1a2e",
                "border-opacity": 0,
                "arc-rounded": false,
            }}
        >
            <EmulatorTile systemId={PRIMARY_SYSTEM_ID} />

            {menuOpen && (
                <MenuOverlay
                    gain={gain}
                    onGainChange={onGainChange}
                    onSelect={onMenuSelect}
                />
            )}
        </View>
    );
}

Render.render(<PluginUI />);
