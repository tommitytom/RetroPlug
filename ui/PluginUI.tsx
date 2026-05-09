import { View, Text, Slider, Render, ELvKey } from "lvgljs-ui";
import { useCallback, useEffect, useRef, useState } from "react";
import { useParameter, createGroup, setKeyboardGroup, on, off } from "lvgljs";

const MENU_ITEMS = ["Load ROM", "Reset", "About", "Cancel"];

// Minimal typed view of the plugin-specific JS surface set up by
// PluginJsBridge. globalThis[Symbol.for("plugin")] is populated at engine init.
interface PluginNamespace {
    openRomBrowser?: () => void;
    loadRomFromPath?: (path: string) => boolean;
    getFrame?: (systemId: number) => unknown;
    setUiCapturesKeyboard?: (captured: boolean) => void;
}
const plugin: PluginNamespace =
    (globalThis as any)[Symbol.for("plugin")] ?? {};

const TextAny = Text as any;

interface MenuOverlayProps {
    gain: number;
    onGainChange: (e: any) => void;
    onSelect: (label: string) => void;
    onClose: () => void;
}

function MenuOverlay({ gain, onGainChange, onSelect, onClose }: MenuOverlayProps) {
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
        // Tell C++ we own keyboard input — arrows/Enter route to LVGL, not
        // to the emulator's button queue, until the menu unmounts.
        plugin.setUiCapturesKeyboard?.(true);
        return () => {
            plugin.setUiCapturesKeyboard?.(false);
            setKeyboardGroup(null);
            group.destroy();
            groupRef.current = null;
        };
    }, []);

    const onItemKey = useCallback((e: { key: number }) => {
        if (e.key === ELvKey.LV_KEY_ESC) {
            onClose();
            return;
        }
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
    }, [focusedIndex, onClose]);

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

    // C++ emits "esc-pressed" from PluginUI::onKeyboard. We can't rely on
    // LVGL's focus-routed key delivery here because the root View isn't
    // focusable, so Esc would be dropped while the menu is closed.
    useEffect(() => {
        const onEsc = () => setMenuOpen(open => !open);
        on("esc-pressed", onEsc);
        return () => off("esc-pressed", onEsc);
    }, []);

    const onGainChange = useCallback((e: any) => setGain(e.value), [setGain]);

    return (
        // Transparent root so the C++-owned framebuffer (created BEFORE this
        // React tree on the LVGL screen) shows through. Master gain control
        // moved into the menu overlay so the emulator owns the window
        // surface. Esc still toggles the menu.
        <View
            style={{
                width: "100%",
                height: "100%",
                "background-opacity": 0,
                "border-opacity": 0,
                "arc-rounded": false,
            }}
        >
            {menuOpen && (
                <MenuOverlay
                    gain={gain}
                    onGainChange={onGainChange}
                    onSelect={onMenuSelect}
                    onClose={() => setMenuOpen(false)}
                />
            )}
        </View>
    );
}

Render.render(<PluginUI />);
