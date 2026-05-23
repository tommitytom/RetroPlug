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
    tree:      MenuTree;
    onClose:   () => void;
    sinkGroup: any;
}

interface FlatEntry {
    item:  MenuItem;
    depth: number;
}

// Indent constants. depth=0 (top level) sits flush with the left padding;
// each deeper level adds INDENT_STEP pixels. Tuned to roughly match v0.5's
// 10 px indent at native zoom (we're scaled up).
const BASE_PAD_LEFT = 4;
const INDENT_STEP   = 16;

export function Menu({ width, height, tree, onClose, sinkGroup }: MenuProps) {
    const [openItems, setOpenItems] = useState<Set<string>>(() => new Set());
    // Visual highlight state. Updated by LV_EVENT_FOCUSED via onItemFocus so
    // the blue text-color tracks whichever widget LVGL actually has focused.
    const [focusedIdx, setFocusedIdx] = useState(0);
    // Authoritative navigation cursor. ONLY moved by explicit user actions
    // (arrow nav, click) and by the rebuild useEffect. Specifically NOT
    // touched by onItemFocus, because expanding/collapsing a submenu causes
    // LVGL to fire stray LV_EVENT_FOCUSED events during the React mutation
    // phase: every newly-created Text widget auto-joins lv_group_get_default
    // (see deps/.../components/text/text.cpp:9), which juggles the default
    // group's focused obj. If we let those events drive the ref, the next
    // useEffect read would clamp to whatever junk widget LVGL landed on (in
    // practice always 0), making submenu-expand jump focus to the top item.
    const focusedIdxRef = useRef(0);
    // Suppresses onItemFocus side-effects while a submenu toggle is in
    // flight. Set true by `activate` before setOpenItems, cleared by the
    // group-rebuild useEffect once the new group is wired up. Starts true so
    // initial mount's creation-time focus events are also ignored.
    const isRebuildingRef = useRef(true);
    const onItemFocus = useCallback((idx: number) => {
        if (isRebuildingRef.current) return;
        setFocusedIdx(idx);
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
        // Clamp the existing focus index into the new bounds; LVGL focuses
        // whatever widget is at that index. If nothing's there (empty list)
        // we skip the focus call entirely.
        const clamped = Math.min(focusedIdxRef.current, orderedRefs.length - 1);
        const idx = clamped < 0 ? 0 : clamped;
        focusedIdxRef.current = idx;
        // Re-enable focus-event handling BEFORE the focus call so the
        // resulting LV_EVENT_FOCUSED updates the visual highlight.
        isRebuildingRef.current = false;
        if (orderedRefs[idx]) group.focus(orderedRefs[idx]);
        // Defensive: synchronous setFocusedIdx in case the focus event
        // didn't fire (e.g. orderedRefs[idx] was null).
        setFocusedIdx(idx);
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
    // Driven by focusedIdx (covers arrow nav + click), visibleKey (covers
    // submenu expand/collapse — inner View remounts so we resync scroll),
    // and height (window resize). useLayoutEffect (not useEffect) so the
    // scroll position is applied synchronously before the next LVGL paint,
    // matching the rebuild effect above. Otherwise the just-remounted inner
    // View paints once at scroll=0 before our scrollToY runs, creating a
    // visible "blip" on submenu toggle.
    useLayoutEffect(() => {
        const view = innerViewRef.current;
        if (!view || flat.length === 0) return;

        // Measure once; item height is fixed (font-size + padding constants).
        if (itemHeightRef.current === 0) {
            const firstRef = refsByIdRef.current.get(flat[0].item.id);
            if (firstRef?.getBoundingClientRect) {
                itemHeightRef.current = firstRef.getBoundingClientRect().height;
            }
        }
        const itemH = itemHeightRef.current;
        if (itemH <= 0) return;

        const viewportH   = height - 36;  // matches inner View's height calc
        const visibleRows = Math.max(1, Math.floor(viewportH / itemH));
        const midpoint    = Math.floor((visibleRows - 1) / 2);
        const totalH      = flat.length * itemH;
        const maxScroll   = Math.max(0, totalH - viewportH);
        const target      = Math.min(
            Math.max(0, (focusedIdx - midpoint) * itemH),
            maxScroll,
        );
        // LV_ANIM_OFF so rapid Down-holds don't lag behind the cursor.
        view.scrollToY(target, false);
        // flat.length is captured implicitly via the focusedIdx/visibleKey deps
        // — visibleKey changes whenever flat changes shape.
        // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [focusedIdx, visibleKey, height]);

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
        const refs = getOrderedRefs();
        const group = groupRef.current;
        if (!group || refs.length === 0) return;
        const cur = focusedIdxRef.current;
        let next = cur;
        if (e.key === ELvKey.LV_KEY_DOWN || e.key === ELvKey.LV_KEY_RIGHT) {
            if (cur >= refs.length - 1) return;
            next = cur + 1;
        } else if (e.key === ELvKey.LV_KEY_UP || e.key === ELvKey.LV_KEY_LEFT) {
            if (cur <= 0) return;
            next = cur - 1;
        } else {
            return;
        }
        // Set the ref synchronously BEFORE asking LVGL to move focus, so even
        // if the LV_EVENT_FOCUSED → onItemFocus chain races a subsequent
        // keystroke, the next onItemKey reads the correct `cur`.
        focusedIdxRef.current = next;
        if (refs[next]) group.focus(refs[next]);
    }, []);

    const activate = useCallback((i: number, item: MenuItem) => {
        // Remember which item the user is acting on so the rebuild useEffect
        // restores focus to the same row (the submenu header keeps the same
        // index since it doesn't move when its children appear/disappear
        // below it).
        focusedIdxRef.current = i;
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
            }}
        >
            <Text
                style={{
                    "text-color": "#4fc3f7",
                    "font-size": 16,
                    "padding-bottom": 4,
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
                {flat.map(({ item, depth }, i) => {
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
                                "text-color": focusedIdx === i ? "#4fc3f7" : "#ffffff",
                                "font-size": 18,
                                "padding-top":    4,
                                "padding-bottom": 4,
                                "padding-left":   BASE_PAD_LEFT + depth * INDENT_STEP,
                                "padding-right":  4,
                            }}
                            onFocus={() => onItemFocus(i)}
                            onKey={onItemKey}
                            onClick={() => activate(i, item)}
                        >
                            {label}
                        </TextAny>
                    );
                })}
            </View>
        </View>
    );
}
