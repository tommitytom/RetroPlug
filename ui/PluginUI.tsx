import { View, Text, Slider, Button, Mask, Render, ELvKey } from "lvgljs-ui";
import { useCallback, useEffect, useRef, useState } from "react";
import { useParameter, createGroup, setKeyboardGroup } from "lvgljs";
import { Waveform } from "./Waveform";

const MENU_ITEMS = ["Reset", "About", "Cancel"];

// lvgljs-ui components are declared as React.ComponentType<Props> with no
// `ref` in their prop types, but at runtime the bridge returns each tagName
// as a host component string and react-reconciler routes refs through
// getPublicInstance. Cast through `any` so we can collect refs to focus-
// group children.
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
        <Mask>
            <View
                style={{
                    width: "100%",
                    height: "100%",
                    "background-opacity": 0,
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
        </Mask>
    );
}

const FREQ_MIN = 20;
const FREQ_MAX = 20000;
const FREQ_SLIDER_STEPS = 1000;

// Log mapping between linear slider position [0..FREQ_SLIDER_STEPS] and Hz [FREQ_MIN..FREQ_MAX].
function sliderToHz(pos: number): number {
    const ratio = pos / FREQ_SLIDER_STEPS;
    return FREQ_MIN * Math.pow(FREQ_MAX / FREQ_MIN, ratio);
}
function hzToSlider(hz: number): number {
    const clamped = Math.max(FREQ_MIN, Math.min(FREQ_MAX, hz));
    return Math.round(
        FREQ_SLIDER_STEPS * (Math.log(clamped / FREQ_MIN) / Math.log(FREQ_MAX / FREQ_MIN))
    );
}

function PluginUI() {
    const [gain, setGain] = useParameter("gain", -50);
    const [freqHz, setFreqHz] = useParameter("freq", 440);
    const [shape, setShape] = useParameter("shape", 0);
    const [menuOpen, setMenuOpen] = useState(false);

    const onMenuSelect = useCallback((label: string) => {
        console.log(`menu: ${label} selected`);
        setMenuOpen(false);
    }, []);

    const onRootKey = useCallback((e: { key: number }) => {
        if (e.key === ELvKey.LV_KEY_ESC) setMenuOpen(true);
    }, []);

    const onGainChange = useCallback((e: any) => setGain(e.value), [setGain]);

    const onFreqChange = useCallback(
        (e: any) => setFreqHz(sliderToHz(e.value)),
        [setFreqHz],
    );

    const onToggleShape = useCallback(
        () => setShape(shape > 0.5 ? 0 : 1),
        [shape, setShape],
    );

    const onReset = useCallback(() => {
        setGain(-50);
        setFreqHz(440);
        setShape(0);
    }, [setGain, setFreqHz, setShape]);

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
                LVGL Test Tone
            </Text>

            <Waveform />

            <Text
                style={{
                    "text-color": "#4fc3f7",
                    "font-size": 18,
                }}
            >
                {`Gain: ${gain.toFixed(1)} dB`}
            </Text>

            <Slider
                style={{
                    width: 400,
                    height: 10,
                    "background-color": "#2d2d44",
                }}
                range={[-90, 30]}
                value={gain}
                onChange={onGainChange}
            />

            <Text
                style={{
                    "text-color": "#4fc3f7",
                    "font-size": 18,
                }}
            >
                {`Freq: ${freqHz < 1000 ? freqHz.toFixed(1) + " Hz" : (freqHz / 1000).toFixed(2) + " kHz"}`}
            </Text>

            <Slider
                style={{
                    width: 400,
                    height: 10,
                    "background-color": "#2d2d44",
                }}
                range={[0, FREQ_SLIDER_STEPS]}
                value={hzToSlider(freqHz)}
                onChange={onFreqChange}
            />

            <Button
                style={{
                    width: 160,
                    height: 40,
                    "background-color": shape < 0.5 ? "#4fc3f7" : "#f7a14f",
                    "border-radius": 8,
                }}
                onClick={onToggleShape}
            >
                <Text style={{ "text-color": "#1a1a2e", "font-size": 14 }}>
                    {shape < 0.5 ? "Shape: Sine" : "Shape: Square"}
                </Text>
            </Button>

            <Button
                style={{
                    width: 120,
                    height: 40,
                    "background-color": "#2d2d44",
                    "border-radius": 8,
                }}
                onClick={onReset}
            >
                <Text style={{ "text-color": "#e0e0e0", "font-size": 14 }}>
                    Reset
                </Text>
            </Button>

            <Button
                style={{
                    width: 120,
                    height: 40,
                    "background-color": "#3a3a55",
                    "border-radius": 8,
                }}
                onClick={() => setMenuOpen(true)}
            >
                <Text style={{ "text-color": "#e0e0e0", "font-size": 14 }}>
                    Menu
                </Text>
            </Button>

            {menuOpen && <MenuOverlay onSelect={onMenuSelect} onClose={() => setMenuOpen(false)} />}
        </View>
    );
}

Render.render(<PluginUI />);
