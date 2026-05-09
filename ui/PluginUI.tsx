import { View, Text, Slider, Render, ELvKey } from "lvgljs-ui";
import { useCallback, useEffect, useRef, useState } from "react";
import { useParameter, createGroup, setKeyboardGroup } from "lvgljs";

const MENU_ITEMS = ["Reset", "About", "Cancel"];

const TextAny = Text as any;

function MenuOverlay({ onSelect, onClose }: { onSelect: (label: string) => void; onClose: () => void }) {
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
        console.log(`menu: ${label} selected`);
        setMenuOpen(false);
    }, []);

    const onRootKey = useCallback((e: { key: number }) => {
        if (e.key === ELvKey.LV_KEY_ESC) setMenuOpen(true);
    }, []);

    const onGainChange = useCallback((e: any) => setGain(e.value), [setGain]);

    return (
        <View
            style={{
                width: "100%",
                height: "100%",
                "background-color": "#1a1a2e",
                display: "flex",
                "flex-direction": "column",
                "align-items": "center",
                padding: 20,
                "arc-rounded": false
            }}
            onKey={onRootKey}
        >
            <Text
                style={{
                    "text-color": "#e0e0e0",
                    "font-size": 24,
                }}
            >
                RetroPlug
            </Text>

            {/* Vertical spacer where the C++-owned framebuffer overlay sits.
                The lv_image is positioned by PluginUI.cpp to align with the
                top of the window — leave room for it before the gain slider. */}
            <View
                style={{
                    width: 320,
                    height: 290,
                    "background-opacity": 0,
                    "border-opacity": 0,
                }}
            />

            <Text
                style={{
                    "text-color": "#4fc3f7",
                    "font-size": 18,
                }}
            >
                {`Master Gain: ${gain.toFixed(1)} dB`}
            </Text>

            <Slider
                style={{
                    width: 400,
                    height: 10,
                    "background-color": "#2d2d44",
                }}
                range={[-90, 12]}
                value={gain}
                onChange={onGainChange}
            />

            {menuOpen && <MenuOverlay onSelect={onMenuSelect} onClose={() => setMenuOpen(false)} />}
        </View>
    );
}

Render.render(<PluginUI />);
