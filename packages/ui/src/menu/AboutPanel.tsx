import { View, Text, ELvKey } from "lvgljs-ui";
import { useEffect, useLayoutEffect, useRef } from "react";
import { createGroup, setKeyboardGroup } from "lvgljs";

import { tileWidth, tileHeight } from "../layout";

const TextAny = Text as any;

interface AboutPanelProps {
    zoom:      number;
    onClose:   () => void;
    sinkGroup: any;
}

const ABOUT_LINES = [
    "RetroPlug",
    "",
    "Game Boy / NES / GBA emulator plugin",
    "github.com/tommitytom/RetroPlug",
];

// Modal info panel reachable from the menu. Any key dismisses.
export function AboutPanel({ zoom, onClose, sinkGroup }: AboutPanelProps) {
    const s = zoom / 3;
    const r = (x: number) => Math.round(x * s);
    const width  = tileWidth(zoom);
    const height = tileHeight(zoom);
    const titleFont = r(16);
    const bodyFont  = r(16);
    const padLR     = r(8);
    const padTB     = r(6);

    const focusRef = useRef<any>(null);
    const onCloseRef = useRef(onClose);
    useEffect(() => { onCloseRef.current = onClose; }, [onClose]);
    // useLayoutEffect (not useEffect) so the cleanup that restores the sink
    // group runs in the commit's mutation phase. When this panel is dismissed
    // from the StartScreen the underlying Menu re-mounts in the SAME commit and
    // claims the keypad from its own useLayoutEffect; a passive (useEffect)
    // cleanup here would run AFTER that synchronous claim and clobber it with
    // the sink, leaving menu keyboard nav dead. Matches Menu.tsx's pattern.
    useLayoutEffect(() => {
        const group = createGroup();
        if (focusRef.current) group.add(focusRef.current);
        if (focusRef.current) group.focus(focusRef.current);
        setKeyboardGroup(group);
        return () => {
            setKeyboardGroup(sinkGroup ?? null);
            group.destroy();
        };
    }, [sinkGroup]);

    // Esc closes on key-press here. Enter is deliberately NOT handled on the
    // key event: LVGL turns Enter into PRESSED → CLICKED (on release), so it's
    // closed by onClick below instead. Closing on the Enter *press* would
    // unmount this panel before the release, and on the StartScreen the menu
    // re-mounts in the same commit — the trailing CLICKED would then land on
    // the menu's now-focused first row and activate it (opening the ROM
    // browser). Letting onClick handle Enter keeps the panel as the click
    // target until the event is fully consumed, so nothing leaks underneath.
    const onKey = (e: { key: number }) => {
        (e as any).stopPropagation?.();
        if (e.key === ELvKey.LV_KEY_ESC) {
            onCloseRef.current();
        }
    };

    return (
        <View
            style={{
                width:  width,
                height: height,
                "background-color": "#000000",
                "background-opacity": 255,
                "border-width": 1,
                "border-color": "#4fc3f7",
                "border-opacity": 255,
                "border-radius": 0,
                "padding-left":  padLR,
                "padding-right": padLR,
                "padding-top":   padTB,
                "padding-bottom":padTB,
                display: "flex",
                "flex-direction": "column",
                "align-items": "stretch",
                "justify-content": "flex-start",
                "row-spacing": 0,
                "column-spacing": 0,
                overflow: "hidden",
            }}
        >
            <Text
                style={{
                    "text-color": "#4fc3f7",
                    "font-size": titleFont,
                    "padding-bottom": r(4),
                }}
            >
                About
            </Text>
            <TextAny
                ref={(x: any) => { focusRef.current = x; }}
                onKick={undefined}
                onKey={onKey}
                onClick={() => onCloseRef.current()}
                style={{
                    "text-color": "#ffffff",
                    "font-size": bodyFont,
                    "padding-top":    r(4),
                    "padding-bottom": r(4),
                    "padding-left":   r(4),
                    "padding-right":  r(4),
                }}
            >
                {ABOUT_LINES.join("\n")}
            </TextAny>
        </View>
    );
}
