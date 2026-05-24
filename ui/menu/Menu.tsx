import { View, Text, ELvKey } from "lvgljs-ui";
import { useCallback, useLayoutEffect, useRef, useState } from "react";
import { createGroup, setKeyboardGroup } from "lvgljs";

import type { MenuItem, MenuTree } from "./menuDefs";

// Cast around lvgljs-ui's Text type — it doesn't expose ref / onFocus / onKey
// in its public typings. Same trick the KitEditor and old MenuOverlay use.
const TextAny = Text as any;

interface MenuProps {
    width:     number;
    height:    number;
    // Integer zoom 1..6. All internal pixel constants are scaled by zoom/3
    // so the menu's typography and padding match the panel size.
    zoom:      number;
    tree:      MenuTree;
    onClose:   () => void;
    sinkGroup: any;
}

interface FlatEntry {
    item:  MenuItem;
    depth: number;
}

// Indent constants AT ZOOM=3 (the historical default). At runtime every
// pixel value is multiplied by `zoom/3` so the menu visually scales with
// the panel. depth=0 (top level) sits flush with the left padding; each
// deeper level adds INDENT_STEP px. Tuned to roughly match v0.5's 10 px
// indent at native zoom (we're scaled up).
const BASE_PAD_LEFT = 4;
const INDENT_STEP   = 16;

// Font / spacing baselines at zoom=3. Centralised so the scaling math
// below stays readable. Border-width stays at 1 regardless of zoom — a
// visual accent that shouldn't bloat at high zooms.
const TITLE_FONT_BASE      = 16;
const TITLE_PAD_BOTTOM_BASE = 4;
// Title region height (title font + padding-bottom + outer padding-top).
// Was hard-coded as `36` in `height - 36`. Now derived from zoom.
const TITLE_REGION_BASE    = 36;
const ITEM_FONT_BASE       = 18;
const ITEM_PAD_VERT_BASE   = 4;
const ITEM_PAD_RIGHT_BASE  = 4;
const OUTER_PAD_LR_BASE    = 8;
const OUTER_PAD_TB_BASE    = 6;

export function Menu({ width, height, zoom, tree, onClose, sinkGroup }: MenuProps) {
    // Linear scale relative to the historical zoom=3 baseline.
    const s = zoom / 3;
    const r = (x: number) => Math.round(x * s);
    const titleFont      = r(TITLE_FONT_BASE);
    const titlePadBottom = r(TITLE_PAD_BOTTOM_BASE);
    const titleRegionH   = r(TITLE_REGION_BASE);
    const itemFont       = r(ITEM_FONT_BASE);
    const itemPadVert    = r(ITEM_PAD_VERT_BASE);
    const itemPadRight   = r(ITEM_PAD_RIGHT_BASE);
    const basePadLeft    = r(BASE_PAD_LEFT);
    const indentStep     = r(INDENT_STEP);
    const outerPadLR     = r(OUTER_PAD_LR_BASE);
    const outerPadTB     = r(OUTER_PAD_TB_BASE);
    const [openItems, setOpenItems] = useState<Set<string>>(() => new Set());
    // Visual highlight state. Updated by LV_EVENT_FOCUSED via onItemFocus so
    // the blue text-color tracks whichever widget LVGL actually has focused.
    // Tracked by item id (not flat-array index) so non-focusable separator
    // rows can sit in the flat list without breaking the index alignment
    // between rendered rows and LVGL refs.
    const [focusedItemId, setFocusedItemId] = useState<string>("");
    // Authoritative navigation cursor. ONLY moved by explicit user actions
    // (arrow nav, click) and by the rebuild useEffect. Specifically NOT
    // touched by onItemFocus, because expanding/collapsing a submenu causes
    // LVGL to fire stray LV_EVENT_FOCUSED events during the React mutation
    // phase: every newly-created Text widget auto-joins lv_group_get_default
    // (see deps/.../components/text/text.cpp:9), which juggles the default
    // group's focused obj. If we let those events drive the ref, the next
    // useEffect read would clamp to whatever junk widget LVGL landed on,
    // making submenu-expand jump focus.
    const focusedItemIdRef = useRef<string>("");
    // Suppresses onItemFocus side-effects while a submenu toggle is in
    // flight. Set true by `activate` before setOpenItems, cleared by the
    // group-rebuild useEffect once the new group is wired up. Starts true so
    // initial mount's creation-time focus events are also ignored.
    const isRebuildingRef = useRef(true);
    const onItemFocus = useCallback((id: string) => {
        if (isRebuildingRef.current) return;
        setFocusedItemId(id);
    }, []);

    // Inner scrollable View ref + cached item height. The scroll-follow effect
    // (further down) drives this so the focused row stays at the viewport
    // midpoint, matching v0.5's UX. Cached after the first measurement —
    // item height is determined by font-size + padding, which are constants.
    const innerViewRef = useRef<any>(null);
    const itemHeightRef = useRef<number>(0);

    // Flatten the tree: walk depth-first, including children of any submenu
    // whose id is in `openItems`. The resulting flat list is what the user
    // actually sees (and navigates with arrows).
    const flat: FlatEntry[] = [];
    (function walk(items: MenuItem[], depth: number) {
        for (const item of items) {
            flat.push({ item, depth });
            if (item.kind === "submenu" && openItems.has(item.id) && item.children) {
                walk(item.children, depth + 1);
            }
        }
    })(tree.items, 0);

    // Stable string key for the currently-visible item set — used as the
    // focus-group rebuild dependency. Whenever the visible items change
    // (expand/collapse) we need to rebuild the LVGL group with the new refs.
    const visibleKey = flat.map(f => f.item.id).join(",");

    // Refs keyed by stable item.id, NOT by array index. Persists across
    // renders so stray re-renders (e.g. from setFocusedIdx in useEffect)
    // don't wipe the ref table by re-running an array-reset in the render
    // body. Old refs are cleared by their own null-callback on unmount.
    const refsByIdRef = useRef<Map<string, any>>(new Map());

    // flat is captured by the render closure. Mirror it into a ref so
    // onItemKey (useCallback with []) can recompute the ordered ref list
    // from the latest flat without re-creating the callback identity.
    const flatRef = useRef<FlatEntry[]>(flat);
    flatRef.current = flat;

    // Translate the current flat list into an ordered array of LVGL widget
    // refs, dropping any null entries (items whose ref callback hasn't
    // fired yet — shouldn't happen post-commit, but defensive).
    const getOrderedRefs = useCallback((): any[] => {
        const refs: any[] = [];
        for (const f of flatRef.current) {
            const r = refsByIdRef.current.get(f.item.id);
            if (r) refs.push(r);
        }
        return refs;
    }, []);

    const groupRef = useRef<any>(null);

    // Rebuild the focus group whenever the visible items change. Pattern
    // mirrors KitEditor.tsx — claim the keypad on mount, restore the
    // parent's sink group on unmount / before rebuild.
    //
    // useLayoutEffect (not useEffect) so the focus state lands before the
    // next LVGL paint. The lvgljs reconciler schedules passive effects via
    // queueMicrotask, but LVGL's paint timers (30 ms libuv / 60 fps DPF
    // idle) can fire between commit and that microtask, producing a brief
    // "blip" frame on submenu toggle. Running synchronously eliminates it.
    useLayoutEffect(() => {
        const group = createGroup();
        groupRef.current = group;
        const orderedRefs = getOrderedRefs();
        for (const ref of orderedRefs) group.add(ref);
        // Keep focus on the previously-focused item if it still exists;
        // otherwise fall back to the first focusable (non-separator) item.
        const focusableIds = flatRef.current
            .filter(f => f.item.kind !== "separator")
            .map(f => f.item.id);
        let targetId = focusedItemIdRef.current;
        if (!focusableIds.includes(targetId)) {
            targetId = focusableIds[0] ?? "";
        }
        focusedItemIdRef.current = targetId;
        // Re-enable focus-event handling BEFORE the focus call so the
        // resulting LV_EVENT_FOCUSED updates the visual highlight.
        isRebuildingRef.current = false;
        const targetRef = targetId ? refsByIdRef.current.get(targetId) : null;
        if (targetRef) group.focus(targetRef);
        // Defensive: synchronous setFocusedItemId in case the focus event
        // didn't fire (e.g. ref wasn't registered yet).
        setFocusedItemId(targetId);
        setKeyboardGroup(group);
        return () => {
            setKeyboardGroup(sinkGroup ?? null);
            group.destroy();
            groupRef.current = null;
        };
        // visibleKey changes whenever the set of rendered items changes (an
        // expand / collapse / tree rebuild). sinkGroup is included so that
        // if the parent's sink ref settles after first paint, we re-claim
        // the keypad correctly.
        // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [visibleKey, sinkGroup]);

    // Programmatic scroll-follow: keep the focused row at the viewport
    // midpoint as the user navigates. Once focus crosses the midpoint, each
    // step advances scroll by one row so the selection visually stays in
    // place. At list ends the formula clamps (top: scroll=0; bottom:
    // scroll=maxScroll, focus rides the edge). Mirrors v0.5's MenuView UX.
    //
    // Driven by focusedItemId (covers arrow nav + click), visibleKey (covers
    // submenu expand/collapse — inner View remounts so we resync scroll),
    // and height (window resize). useLayoutEffect (not useEffect) so the
    // scroll position is applied synchronously before the next LVGL paint,
    // matching the rebuild effect above. Otherwise the just-remounted inner
    // View paints once at scroll=0 before our scrollToY runs, creating a
    // visible "blip" on submenu toggle.
    useLayoutEffect(() => {
        const view = innerViewRef.current;
        if (!view || flat.length === 0) return;

        // Use the first focusable row for height measurement — separators
        // are thinner than items and would skew the cache.
        const firstItem = flat.find(f => f.item.kind !== "separator");
        const firstRef = firstItem
            ? refsByIdRef.current.get(firstItem.item.id) : null;
        if (firstRef?.getBoundingClientRect) {
            itemHeightRef.current = firstRef.getBoundingClientRect().height;
        }
        const itemH = itemHeightRef.current;
        if (itemH <= 0) return;

        const focusedFlatIdx = flat.findIndex(f => f.item.id === focusedItemId);
        if (focusedFlatIdx < 0) return;

        const viewportH   = height - titleRegionH;  // matches inner View's height calc
        const visibleRows = Math.max(1, Math.floor(viewportH / itemH));
        const midpoint    = Math.floor((visibleRows - 1) / 2);
        const totalH      = flat.length * itemH;
        const maxScroll   = Math.max(0, totalH - viewportH);
        const target      = Math.min(
            Math.max(0, (focusedFlatIdx - midpoint) * itemH),
            maxScroll,
        );
        // LV_ANIM_OFF so rapid Down-holds don't lag behind the cursor.
        view.scrollToY(target, false);
        // flat.length is captured implicitly via the focusedItemId/visibleKey
        // deps — visibleKey changes whenever flat changes shape. `zoom` is
        // in the deps because typography + viewport height both depend on it.
        // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [focusedItemId, visibleKey, height, zoom]);

    // Up / Down nav. No wrap-around — v0.5 doesn't wrap, and wrap on a
    // potentially long scrollable list is disorienting.
    //
    // Stop bubbling: lv_binding_js sets LV_OBJ_FLAG_EVENT_BUBBLE on every
    // Text widget (see deps/.../components/text/text.cpp), so an unhandled
    // LV_EVENT_KEY would bubble up to the scrollable inner View and LVGL's
    // default key handler would scroll it instead of moving focus. We stop
    // the bubble for every key we see — Enter doesn't go through onKey
    // (it's PRESSED → CLICKED via onClick), so this is safe.
    //
    // Note: lv_binding_js's public TS type names the method `stopPropogation`
    // (with a typo) but the native binding actually exposes `stopPropagation`
    // (correct spelling) — see deps/.../core/event/key/key.cpp:37. We cast
    // around the typed mismatch.
    const onItemKey = useCallback((e: { key: number }) => {
        (e as any).stopPropagation?.();
        const group = groupRef.current;
        const entries = flatRef.current;
        if (!group || entries.length === 0) return;
        const curId = focusedItemIdRef.current;
        const cur   = entries.findIndex(f => f.item.id === curId);
        if (cur < 0) return;

        // Right/Left cycle the focused item's value (Zoom, MIDI routing,
        // Link group, LSDJ mode). Items without onCycle are no-op — focus
        // does NOT move on Right/Left, matching the v0.5 / desktop-app
        // convention where horizontal arrows manipulate the current row.
        if (e.key === ELvKey.LV_KEY_RIGHT) {
            entries[cur]?.item.onCycle?.(1);
            return;
        }
        if (e.key === ELvKey.LV_KEY_LEFT) {
            entries[cur]?.item.onCycle?.(-1);
            return;
        }

        let dir: 1 | -1;
        if      (e.key === ELvKey.LV_KEY_DOWN) dir = 1;
        else if (e.key === ELvKey.LV_KEY_UP)   dir = -1;
        else return;
        // Walk past any separator rows so navigation skips them.
        let next = cur + dir;
        while (next >= 0 && next < entries.length
               && entries[next].item.kind === "separator") {
            next += dir;
        }
        if (next < 0 || next >= entries.length) return;
        const nextId = entries[next].item.id;
        // Set the ref synchronously BEFORE asking LVGL to move focus, so even
        // if the LV_EVENT_FOCUSED → onItemFocus chain races a subsequent
        // keystroke, the next onItemKey reads the correct `cur`.
        focusedItemIdRef.current = nextId;
        const nextRef = refsByIdRef.current.get(nextId);
        if (nextRef) group.focus(nextRef);
    }, []);

    const activate = useCallback((item: MenuItem) => {
        // Remember which item the user is acting on so the rebuild useEffect
        // restores focus to the same row (the submenu header keeps the same
        // id when its children appear/disappear below it).
        focusedItemIdRef.current = item.id;
        if (item.kind === "submenu") {
            // Suppress the stray LV_EVENT_FOCUSED events that fire during
            // the impending mutation phase — see isRebuildingRef comment.
            isRebuildingRef.current = true;
            setOpenItems(prev => {
                const nextSet = new Set(prev);
                if (nextSet.has(item.id)) nextSet.delete(item.id);
                else nextSet.add(item.id);
                return nextSet;
            });
            return;
        }
        item.onSelect?.();
        if (!item.keepOpen) onClose();
    }, [onClose]);

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
                "padding-left":  outerPadLR,
                "padding-right": outerPadLR,
                "padding-top":   outerPadTB,
                "padding-bottom":outerPadTB,
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
                    "padding-bottom": titlePadBottom,
                }}
            >
                {tree.title}
            </Text>
            <View
                // Re-keying on the visible set forces a full unmount/remount
                // of the inner View whenever a submenu opens or closes. This
                // is a workaround for lv_binding_js: its `insertChildBefore`
                // implementation
                // (deps/lv_binding_js/src/render/native/core/basic/comp.cpp:38)
                // ignores the `beforeChild` argument and effectively just
                // appends — so newly-inserted submenu children end up at the
                // bottom of the LVGL widget list, visually misordered.
                // Remounting from scratch lets every Text child mount via
                // appendChild in JSX order, producing the correct LVGL order.
                key={visibleKey}
                ref={innerViewRef}
                style={{
                    width:  "100%",
                    height: height - titleRegionH,
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
                {flat.map(({ item, depth }) => {
                    if (item.kind === "separator") {
                        // Thin dark-grey line between groups. Not focusable,
                        // not clickable — naturally excluded from the LVGL
                        // group since it registers no ref. Outer wrapper is
                        // transparent and provides vertical breathing room
                        // via padding (lvgljs has no `margin`); inner View
                        // is the actual coloured line. Outer height must be
                        // explicit — lvgljs Views without `height` stretch
                        // to fill remaining flex space.
                        const padTB     = r(4);
                        const lineH     = Math.max(1, r(1));
                        const wrapperH  = padTB * 2 + lineH;
                        return (
                            <View
                                key={item.id}
                                style={{
                                    width: "100%",
                                    height: wrapperH,
                                    "background-opacity": 0,
                                    "border-width": 0,
                                    "padding-left":  0,
                                    "padding-right": 0,
                                    "padding-top":    padTB,
                                    "padding-bottom": padTB,
                                }}
                            >
                                <View
                                    style={{
                                        width: "100%",
                                        height: lineH,
                                        "background-color": "#444444",
                                        "background-opacity": 255,
                                        "border-width": 0,
                                        "padding-left":  0,
                                        "padding-right": 0,
                                        "padding-top":   0,
                                        "padding-bottom":0,
                                    }}
                                />
                            </View>
                        );
                    }
                    const isSubmenu = item.kind === "submenu";
                    const isOpen    = isSubmenu && openItems.has(item.id);
                    // Submenu items always show a hint glyph. `>` collapsed,
                    // `v` expanded. Children appearing inline below ALSO
                    // signals expanded state, but the glyph helps users
                    // who're scanning quickly.
                    const label = isSubmenu
                        ? `${item.label} ${isOpen ? "v" : ">"}`
                        : item.label;
                    return (
                        <TextAny
                            key={item.id}
                            ref={(r: any) => {
                                if (r) refsByIdRef.current.set(item.id, r);
                                else refsByIdRef.current.delete(item.id);
                            }}
                            style={{
                                "text-color": focusedItemId === item.id ? "#4fc3f7" : "#ffffff",
                                "font-size": itemFont,
                                "padding-top":    itemPadVert,
                                "padding-bottom": itemPadVert,
                                "padding-left":   basePadLeft + depth * indentStep,
                                "padding-right":  itemPadRight,
                            }}
                            onFocus={() => onItemFocus(item.id)}
                            onKey={onItemKey}
                            onClick={() => activate(item)}
                        >
                            {label}
                        </TextAny>
                    );
                })}
            </View>
        </View>
    );
}
