import { View, Text, ELvKey } from "lvgljs-ui";
import { useCallback, useEffect, useLayoutEffect, useRef, useState } from "react";
import { createGroup, off, on, setKeyboardGroup } from "lvgljs";

import type { MenuItem, MenuTree, PromptSpec } from "./menuDefs";
import {
    KEY_BACKSPACE, KEY_ENTER, KEY_ESCAPE, dpfKeyToName,
} from "../../runtime/lvgljs/input";
import { isValidProfileChar } from "../useBindingsEditor";

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

interface PromptState {
    itemId:   string;
    spec:     PromptSpec;
    value:    string;
    error:    string;
    pending:  boolean;
}

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
    // Capture mode: id of the capture-kind item that's listening for the
    // next "key" / "gamepad-button" event. Null otherwise.
    const [capturingId, setCapturingId] = useState<string | null>(null);
    const capturingIdRef = useRef<string | null>(null);
    useEffect(() => { capturingIdRef.current = capturingId; }, [capturingId]);
    // Inline prompt overlay state. Null = no overlay.
    const [promptState, setPromptState] = useState<PromptState | null>(null);
    const promptStateRef = useRef<PromptState | null>(null);
    useEffect(() => { promptStateRef.current = promptState; }, [promptState]);
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
        // Modal: ignore arrow nav while capture / prompt is active. The
        // global "key" subscriber already handles input for those modes.
        if (capturingIdRef.current || promptStateRef.current) return;
        const group = groupRef.current;
        const entries = flatRef.current;
        if (!group || entries.length === 0) return;
        const curId = focusedItemIdRef.current;
        const cur   = entries.findIndex(f => f.item.id === curId);
        if (cur < 0) return;

        // Capture rows: Backspace / Delete clears the binding in place.
        const focused = entries[cur]?.item;
        if (focused?.kind === "capture" && focused.capture
            && (e.key === ELvKey.LV_KEY_DEL || e.key === ELvKey.LV_KEY_BACKSPACE)) {
            focused.capture.onClear();
            return;
        }

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

    // Mirror the most recent tree into a ref so the "key" subscriber (which
    // is mounted once with [] deps) can find capture/prompt items by id
    // without itself being recreated on every parent re-render.
    const treeRef = useRef<MenuTree>(tree);
    treeRef.current = tree;

    // Walks both the open and unopened branches — capture/prompt items the
    // user activated must still be findable while their submenu is open.
    const findItem = useCallback((id: string): MenuItem | null => {
        const walk = (items: MenuItem[]): MenuItem | null => {
            for (const it of items) {
                if (it.id === id) return it;
                if (it.children) {
                    const found = walk(it.children);
                    if (found) return found;
                }
            }
            return null;
        };
        return walk(treeRef.current.items);
    }, []);

    const activate = useCallback((item: MenuItem) => {
        // Capture / prompt mode owns input; click-events on focused rows
        // (Enter → LVGL CLICKED → here) are swallowed so the just-captured
        // key doesn't immediately re-arm capture for the same item.
        if (capturingIdRef.current || promptStateRef.current) return;

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
        if (item.kind === "capture" && item.capture) {
            setCapturingId(item.id);
            return;
        }
        if (item.kind === "prompt" && item.prompt) {
            setPromptState({
                itemId:  item.id,
                spec:    item.prompt,
                value:   item.prompt.initial ?? "",
                error:   "",
                pending: false,
            });
            return;
        }
        item.onSelect?.();
        const implicitKeepOpen = item.kind === "capture" || item.kind === "prompt";
        if (!item.keepOpen && !implicitKeepOpen) onClose();
    }, [onClose]);

    // Confirm the current prompt. Runs validate() synchronously, then
    // onConfirm() (which may be async). On success closes the overlay;
    // on error updates `error` and leaves the overlay open. `pending`
    // gates re-entrancy so repeated Enter presses don't fire concurrent
    // RPCs.
    const confirmPrompt = useCallback(async () => {
        const ps = promptStateRef.current;
        if (!ps || ps.pending) return;
        const v = ps.value;
        if (ps.spec.validate) {
            const err = ps.spec.validate(v);
            if (err) {
                setPromptState(p => p ? { ...p, error: err } : null);
                return;
            }
        }
        setPromptState(p => p ? { ...p, pending: true } : null);
        try {
            const err = await ps.spec.onConfirm(v);
            if (err) {
                setPromptState(p => p ? { ...p, error: err, pending: false } : null);
                return;
            }
            setPromptState(null);
        } catch (e) {
            console.warn("[menu:prompt] onConfirm threw", e);
            setPromptState(p => p ? { ...p, error: "Unexpected error.", pending: false } : null);
        }
    }, []);

    // Global key channel: Esc routing + capture-mode binding + prompt
    // editing. Mounted once with a stable wrapper that always reads the
    // latest closure via refs (same pattern as the rest of the codebase
    // for raw DPF events).
    const keyHandlerRef = useRef<(key: number, press: boolean) => void>(() => {});
    keyHandlerRef.current = (key, press) => {
        if (!press) return;

        // 1. Prompt overlay owns input.
        const ps = promptStateRef.current;
        if (ps) {
            if (key === KEY_ESCAPE) { setPromptState(null); return; }
            if (key === KEY_ENTER)  { void confirmPrompt();  return; }
            if (key === KEY_BACKSPACE) {
                if (ps.spec.confirm) return;
                setPromptState(p => p
                    ? { ...p, value: p.value.slice(0, -1), error: "" }
                    : null);
                return;
            }
            if (ps.spec.confirm) return;   // confirm dialogs: Y/N only
            if (key >= 0x20 && key <= 0x7E) {
                const ch = String.fromCharCode(key);
                if (isValidProfileChar(ch)) {
                    setPromptState(p => p
                        ? { ...p, value: (p.value + ch).slice(0, 48), error: "" }
                        : null);
                }
            }
            return;
        }

        // 2. Capture mode: next key becomes the binding (keyboard only).
        const capId = capturingIdRef.current;
        if (capId) {
            if (key === KEY_ESCAPE) { setCapturingId(null); return; }
            const item = findItem(capId);
            if (!item?.capture) { setCapturingId(null); return; }
            if (item.capture.kind !== "keyboard") return;  // wait for gamepad
            const name = dpfKeyToName(key);
            if (!name) return;
            item.capture.onCapture(name);
            setCapturingId(null);
            return;
        }

        // 3. Idle: Esc closes the menu. The Esc-opens-the-menu path lives
        //    in PluginUI; here we handle the close half so capture / prompt
        //    can intercept Esc before it ever reaches the parent.
        if (key === KEY_ESCAPE) onClose();
    };

    const padHandlerRef = useRef<(pad: number, button: string, pressed: boolean) => void>(() => {});
    padHandlerRef.current = (_pad, button, pressed) => {
        if (!pressed) return;
        const capId = capturingIdRef.current;
        if (!capId) return;
        const item = findItem(capId);
        if (!item?.capture || item.capture.kind !== "gamepad") return;
        item.capture.onCapture(button);
        setCapturingId(null);
    };

    useEffect(() => {
        const keyWrap = (k: number, p: boolean) => keyHandlerRef.current(k, p);
        const padWrap = (p: number, b: string, pr: boolean) => padHandlerRef.current(p, b, pr);
        on("key", keyWrap);
        on("gamepad-button", padWrap);
        return () => {
            off("key", keyWrap);
            off("gamepad-button", padWrap);
        };
    }, []);

    // True when Menu is "modal" — capture or prompt is active and Esc
    // shouldn't propagate to PluginUI's menu-toggle. Exposed via a ref so
    // PluginUI can check it without subscribing to Menu state. Currently
    // PluginUI defers Esc handling whenever the menu is open at all, so
    // this isn't strictly needed — kept here for future read-out.
    const isModal = capturingId != null || promptState != null;
    void isModal;

    // Clear capture mode on row navigation or when the focused item changes
    // — leaving capture armed on an off-screen row is confusing.
    useEffect(() => {
        if (capturingId && focusedItemId !== capturingId) {
            setCapturingId(null);
        }
    }, [focusedItemId, capturingId]);

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
                    const isCapturing = item.kind === "capture"
                        && capturingId === item.id;
                    // Submenu items always show a hint glyph. `>` collapsed,
                    // `v` expanded. Children appearing inline below ALSO
                    // signals expanded state, but the glyph helps users
                    // who're scanning quickly. Capture items in capture mode
                    // swap their stored value for a "Press a key..." prompt
                    // so the user can see what's expected.
                    let label = item.label;
                    if (isSubmenu) {
                        label = `${item.label} ${isOpen ? "v" : ">"}`;
                    } else if (isCapturing && item.capture) {
                        // Strip the value suffix (everything after the first
                        // ": ") and replace with the capture prompt.
                        const colonIdx = item.label.indexOf(": ");
                        const head = colonIdx >= 0 ? item.label.slice(0, colonIdx) : item.label;
                        label = `${head}: Press a ${item.capture.kind === "keyboard" ? "key" : "button"}...`;
                    }
                    // Capture-mode rows render in orange to distinguish them
                    // from a plain focused row (which is the same cyan as
                    // every other selection in the menu).
                    const textColor = isCapturing
                        ? "#ffb74d"
                        : (focusedItemId === item.id ? "#4fc3f7" : "#ffffff");
                    return (
                        <TextAny
                            key={item.id}
                            ref={(r: any) => {
                                if (r) refsByIdRef.current.set(item.id, r);
                                else refsByIdRef.current.delete(item.id);
                            }}
                            style={{
                                "text-color": textColor,
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
            {promptState && (() => {
                // Inline modal overlay anchored top-centre, sized to the
                // menu width minus a small inset. Same colour palette as
                // the rest of the menu (cyan accent + neutral panels).
                const promptW = Math.max(120, width - r(32));
                const promptH = r(promptState.spec.confirm ? 72 : 96);
                const promptX = Math.max(0, Math.floor((width  - promptW) / 2));
                const promptY = titleRegionH + r(8);
                const fontSize = r(14);
                const rowH     = r(22);
                const sp = promptState.spec;
                const hint = sp.hint
                    ?? (sp.confirm
                        ? "Enter to confirm  |  Esc to cancel"
                        : "Enter to confirm  |  Esc to cancel  |  Backspace to erase");
                return (
                    <View
                        style={{
                            position: "absolute",
                            left: promptX,
                            top:  promptY,
                            width:  promptW,
                            height: promptH,
                            "background-color": "#161628",
                            "background-opacity": 255,
                            "border-width": 1,
                            "border-color": "#4fc3f7",
                            "border-opacity": 255,
                            "padding-left":  r(8),
                            "padding-right": r(8),
                            "padding-top":   r(6),
                            "padding-bottom":r(6),
                            display: "flex",
                            "flex-direction": "column",
                            "row-spacing": r(4),
                        }}
                    >
                        <Text style={{
                            "text-color": "#4fc3f7",
                            "font-size":  fontSize,
                            width:        "100%",
                            height:       rowH,
                        }}>
                            {sp.title}
                        </Text>
                        {!sp.confirm && (
                            <Text style={{
                                "text-color": "#ffffff",
                                "background-color": "#1a1a2e",
                                "background-opacity": 255,
                                "font-size": fontSize,
                                width:       "100%",
                                height:      rowH,
                                "padding-left":  r(4),
                                "padding-right": r(4),
                                "padding-top":   r(2),
                                "padding-bottom":r(2),
                            }}>
                                {promptState.value + "_"}
                            </Text>
                        )}
                        <Text style={{
                            "text-color": promptState.error ? "#ef5350" : "#888888",
                            "font-size":  fontSize,
                            width:        "100%",
                            height:       rowH,
                        }}>
                            {promptState.error || hint}
                        </Text>
                    </View>
                );
            })()}
        </View>
    );
}
