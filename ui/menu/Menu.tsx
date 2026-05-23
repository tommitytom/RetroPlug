import { View, Text, ELvKey } from "lvgljs-ui";
import { useCallback, useEffect, useRef, useState } from "react";
import { createGroup, setKeyboardGroup } from "lvgljs";

import type { MenuItem, MenuPane } from "./menuDefs";

// Cast around lvgljs-ui's Text type — it doesn't expose ref / onFocus / onKey
// in its public typings. Same trick the KitEditor and the old MenuOverlay use.
const TextAny = Text as any;

interface MenuProps {
    // Panel size in pixels. When `x`/`y` are provided the panel is placed
    // absolutely via LV_ALIGN_TOP_LEFT — used when anchoring over a focused
    // tile. When `x`/`y` are omitted, no `align` prop is emitted and the
    // parent's flex layout positions the panel (used by StartScreen, where
    // we want a centered panel in an empty window).
    x?:     number;
    y?:     number;
    width:  number;
    height: number;

    // All available panes; the one with id == currentPaneId is rendered.
    // Recomputed by the parent every render so labels with live state
    // (link group, MIDI routing, LSDJ mode) refresh as they cycle.
    panes:         Record<string, MenuPane>;
    currentPaneId: string;

    // Back-stack ops, lifted into the parent so Esc (handled in PluginUI's
    // useKeyboard hook) can pop one level before falling through to close.
    onPush: (paneId: string) => void;
    onPop:  () => void;

    // Called when the user picks an action item that isn't `keepOpen`. The
    // action itself is fired by Menu; this just tells the parent the user
    // is done and the menu can close.
    onClose: () => void;

    // The parent's empty input-sink group. Restored as the keyboard group
    // when the Menu unmounts so LVGL doesn't fall back to the default group
    // (which contains every clickable View and would mangle game input).
    sinkGroup: any;
}

export function Menu({
    x, y, width, height,
    panes, currentPaneId,
    onPush, onPop, onClose, sinkGroup,
}: MenuProps) {
    const pane = panes[currentPaneId] ?? panes["root"];
    const items = pane.items;

    const itemRefs = useRef<any[]>([]);
    itemRefs.current = [];

    const groupRef = useRef<any>(null);
    const [focusedIdx, setFocusedIdx] = useState(0);
    const focusedIdxRef = useRef(focusedIdx);
    useEffect(() => { focusedIdxRef.current = focusedIdx; }, [focusedIdx]);

    // Rebuild the focus group whenever the pane changes (item set differs).
    // Same lifecycle as KitEditor.tsx — claim keyboard on entry, restore the
    // parent's sink group on unmount / re-population.
    useEffect(() => {
        const group = createGroup();
        groupRef.current = group;
        for (const ref of itemRefs.current) {
            if (ref) group.add(ref);
        }
        // Land focus on the first non-back item so Esc-to-pop and Down-arrow
        // don't both start on "◂ Back" (which would be slightly awkward).
        const firstNonBack = items.findIndex((it) => it.kind !== "back");
        const initial = firstNonBack >= 0 ? firstNonBack : 0;
        if (itemRefs.current[initial]) group.focus(itemRefs.current[initial]);
        focusedIdxRef.current = initial;
        setFocusedIdx(initial);
        setKeyboardGroup(group);
        return () => {
            setKeyboardGroup(sinkGroup ?? null);
            group.destroy();
            groupRef.current = null;
        };
    }, [currentPaneId, sinkGroup, items.length]);

    const onItemKey = useCallback((e: { key: number }) => {
        const refs = itemRefs.current;
        const group = groupRef.current;
        if (!group || refs.length === 0) return;
        let next = focusedIdxRef.current;
        if (e.key === ELvKey.LV_KEY_DOWN || e.key === ELvKey.LV_KEY_RIGHT) {
            next = (focusedIdxRef.current + 1) % refs.length;
        } else if (e.key === ELvKey.LV_KEY_UP || e.key === ELvKey.LV_KEY_LEFT) {
            next = (focusedIdxRef.current - 1 + refs.length) % refs.length;
        } else {
            return;
        }
        if (refs[next]) group.focus(refs[next]);
    }, []);

    const activate = useCallback((item: MenuItem) => {
        if (item.kind === "back") {
            onPop();
            return;
        }
        if (item.kind === "submenu" && item.submenu) {
            onPush(item.submenu);
            return;
        }
        item.onSelect?.();
        if (!item.keepOpen) onClose();
    }, [onPush, onPop, onClose]);

    const panelStyle: Record<string, unknown> = {
        width:  width,
        height: height,
        "background-color": "#000000",
        "background-opacity": 255,
        "border-width": 1,
        "border-color": "#4fc3f7",
        "border-opacity": 255,
        "border-radius": 0,
        "padding-left":  8,
        "padding-right": 8,
        "padding-top":   6,
        "padding-bottom":6,
        display: "flex",
        "flex-direction": "column",
        "align-items": "stretch",
        "justify-content": "flex-start",
        "row-spacing": 0,
        "column-spacing": 0,
        overflow: "hidden",
    };
    const anchored = x != null && y != null;
    const ViewAny = View as any;
    return (
        <ViewAny
            style={panelStyle}
            {...(anchored ? { align: { type: 0x01 /* LV_ALIGN_TOP_LEFT */, pos: [x, y] } } : {})}
        >
            <Text
                style={{
                    "text-color": "#4fc3f7",
                    "font-size": 16,
                    "padding-bottom": 4,
                }}
            >
                {pane.title}
            </Text>
            <View
                style={{
                    width:  "100%",
                    height: height - 36,
                    "background-opacity": 0,
                    "border-width": 0,
                    "border-opacity": 0,
                    "padding-left":  0,
                    "padding-right": 0,
                    "padding-top":   0,
                    "padding-bottom":0,
                    display: "flex",
                    "flex-direction": "column",
                    "align-items": "stretch",
                    "row-spacing": 0,
                    "column-spacing": 0,
                    overflow: "auto",
                }}
            >
                {items.map((item, i) => (
                    <TextAny
                        key={item.id}
                        ref={(r: any) => { itemRefs.current[i] = r; }}
                        style={{
                            "text-color": focusedIdx === i ? "#4fc3f7" : "#ffffff",
                            "font-size": 18,
                            "padding-top":    4,
                            "padding-bottom": 4,
                            "padding-left":   4,
                            "padding-right":  4,
                        }}
                        onFocus={() => setFocusedIdx(i)}
                        onKey={onItemKey}
                        onClick={() => activate(item)}
                    >
                        {item.label}
                    </TextAny>
                ))}
            </View>
        </ViewAny>
    );
}
