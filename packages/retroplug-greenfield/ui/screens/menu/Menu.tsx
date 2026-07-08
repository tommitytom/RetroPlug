// The context-menu renderer: a keyboard-driven tree, ported from the legacy Menu (packages/ui/src/menu/
// Menu.tsx) but leaning on LVGL primitives to shed the legacy focus-tracking machinery.
//
// Cleaner than legacy:
//   - The focus highlight is a React state updated ONLY by explicit nav / click / rebuild — never by
//     LVGL onFocus events — so there's nothing for stray FOCUSED events to corrupt and no isRebuildingRef
//     suppression (LVGL still owns actual keypad focus; this state just paints the row).
//   - The keypad group is claimed by useFocusGroup (the extracted primitive).
// Kept from legacy:
//   - Re-key the inner scrollable View on the visible-item set (`visibleKey`) — the proven
//     insertChildBefore-append workaround (lv_binding_js appends instead of inserting).
//   - Zoom typography scaled off `s = zoom/3`.
//
// Navigation: LVGL turns Enter on the focused row into PRESSED→CLICKED → the row's onClick → activate().
// Arrow Up/Down move a ref-tracked cursor and call the group's focus(); Left/Right cycle a "cycler"
// item's value. Esc is NOT handled here — the menu controller owns open/close.

import { useCallback, useLayoutEffect, useRef, useState } from "react";
import { Text, ELvKey } from "lvgljs-ui";

import { useFocusGroup } from "../../lvgl/useFocusGroup";
import { Box } from "../../lvgl/Box";
import type { MenuItem, MenuTree } from "./menuTree";

// lvgljs-ui's Text type doesn't expose ref / onKey / onFocusedStyle; cast to reach them.
const TextAny = Text as any;

interface FlatEntry {
  item: MenuItem;
  depth: number;
}

// Font / spacing baselines at zoom=3 (the historical baseline); every px scales by zoom/3.
const TITLE_FONT_BASE = 16;
const TITLE_REGION_BASE = 36;
const ITEM_FONT_BASE = 18;
const ITEM_PAD_VERT_BASE = 4;
const BASE_PAD_LEFT = 4;
const INDENT_STEP = 16;
const OUTER_PAD_LR_BASE = 8;
const OUTER_PAD_TB_BASE = 6;

export interface MenuProps {
  width: number;
  height: number;
  zoom: number; // integer 1..6
  tree: MenuTree;
  onClose: () => void;
}

export function Menu({ width, height, zoom, tree, onClose }: MenuProps) {
  const s = zoom / 3;
  const r = (x: number) => Math.round(x * s);
  const titleFont = r(TITLE_FONT_BASE);
  const titleRegionH = r(TITLE_REGION_BASE);
  const itemFont = r(ITEM_FONT_BASE);
  const itemPadVert = r(ITEM_PAD_VERT_BASE);
  const basePadLeft = r(BASE_PAD_LEFT);
  const indentStep = r(INDENT_STEP);
  const outerPadLR = r(OUTER_PAD_LR_BASE);
  const outerPadTB = r(OUTER_PAD_TB_BASE);

  const [openItems, setOpenItems] = useState<Set<string>>(() => new Set());
  // The focus highlight, driven ONLY by explicit nav / click / rebuild (never by LVGL onFocus events, so
  // no stray-focus suppression is needed). LVGL owns actual keypad focus; this just paints the row.
  const [focusedId, setFocusedId] = useState<string>("");

  // id → LVGL Text ref, and the current flat (visible) list. Refs are keyed by stable id (not index) so
  // separators can sit in the list without breaking alignment.
  const refsByIdRef = useRef<Map<string, unknown>>(new Map());
  // The authoritative focus cursor (a ref, not React state — the highlight is LVGL-native). Moved only by
  // explicit nav / click / rebuild.
  const focusedIdRef = useRef<string>("");
  // The inner scrollable container + a cached row height, for the keyboard scroll-follow effect below.
  const innerViewRef = useRef<{ scrollToY?: (y: number, animate: boolean) => void } | null>(null);
  const itemHeightRef = useRef<number>(0);

  // Flatten depth-first, descending only into open submenus.
  const flat: FlatEntry[] = [];
  (function walk(items: MenuItem[], depth: number) {
    for (const item of items) {
      flat.push({ item, depth });
      if (item.kind === "submenu" && openItems.has(item.id) && item.children) walk(item.children, depth + 1);
    }
  })(tree.items, 0);
  const flatRef = useRef<FlatEntry[]>(flat);
  flatRef.current = flat;
  const visibleKey = flat.map((f) => f.item.id).join(",");

  const orderedRefs = useCallback(
    () =>
      flatRef.current
        .filter((f) => f.item.kind !== "separator")
        .map((f) => refsByIdRef.current.get(f.item.id))
        .filter((x) => x != null),
    [],
  );

  // Keep the previously-focused row if it still exists, else the first focusable; sync focusedIdRef.
  const focusTarget = useCallback(() => {
    const focusable = flatRef.current.filter((f) => f.item.kind !== "separator").map((f) => f.item.id);
    let id = focusedIdRef.current;
    if (!focusable.includes(id)) id = focusable[0] ?? "";
    focusedIdRef.current = id;
    setFocusedId(id);
    return id ? refsByIdRef.current.get(id) : undefined;
  }, []);

  const focus = useFocusGroup(orderedRefs, { deps: [visibleKey], focusTarget });

  const activate = useCallback(
    (item: MenuItem) => {
      focusedIdRef.current = item.id; // so the rebuild keeps this row focused
      setFocusedId(item.id);
      if (item.kind === "submenu") {
        setOpenItems((prev) => {
          const next = new Set(prev);
          if (next.has(item.id)) next.delete(item.id);
          else next.add(item.id);
          return next;
        });
        return;
      }
      item.onSelect?.();
      if (!item.keepOpen) onClose();
    },
    [onClose],
  );

  const onItemKey = useCallback(
    (e: { key: number }) => {
      (e as { stopPropagation?: () => void }).stopPropagation?.(); // else bubbles to the scroll View
      const entries = flatRef.current;
      const cur = entries.findIndex((f) => f.item.id === focusedIdRef.current);
      if (cur < 0) return;
      const item = entries[cur].item;
      // Left/Right cycle the focused item's value; focus does not move.
      if (e.key === ELvKey.LV_KEY_RIGHT) return item.onCycle?.(1);
      if (e.key === ELvKey.LV_KEY_LEFT) return item.onCycle?.(-1);
      let dir: 1 | -1;
      if (e.key === ELvKey.LV_KEY_DOWN) dir = 1;
      else if (e.key === ELvKey.LV_KEY_UP) dir = -1;
      else return;
      let next = cur + dir;
      while (next >= 0 && next < entries.length && entries[next].item.kind === "separator") next += dir;
      if (next < 0 || next >= entries.length) return; // no wrap-around
      const nextId = entries[next].item.id;
      focusedIdRef.current = nextId;
      setFocusedId(nextId);
      const nextRef = refsByIdRef.current.get(nextId);
      if (nextRef) focus(nextRef); // move keypad focus; the scroll-follow effect keeps the row on-screen
    },
    [focus],
  );

  // Keyboard scroll-follow: keep the focused row near the viewport midpoint so an expanded submenu that
  // overflows the window still tracks the cursor. Container-level scrollToY with explicit midpoint math
  // (clamped at the ends) — the child's scrollIntoView didn't follow the cursor. useLayoutEffect so the
  // scroll lands before paint (no blip when a submenu toggle remounts the inner container). Ported from the
  // legacy Menu; driven by focusedId (arrow nav + click), visibleKey (expand/collapse), height + zoom.
  useLayoutEffect(() => {
    const view = innerViewRef.current;
    const entries = flatRef.current;
    if (!view?.scrollToY || entries.length === 0) return;

    // Measure a real row once (font + padding are constant): the first focusable row — separators are thinner.
    const firstItem = entries.find((f) => f.item.kind !== "separator");
    const firstRef = firstItem
      ? (refsByIdRef.current.get(firstItem.item.id) as { getBoundingClientRect?: () => { height: number } } | undefined)
      : undefined;
    const measured = firstRef?.getBoundingClientRect?.().height;
    if (measured && measured > 0) itemHeightRef.current = measured;
    const itemH = itemHeightRef.current;
    if (itemH <= 0) return;

    const focusedFlatIdx = entries.findIndex((f) => f.item.id === focusedId);
    if (focusedFlatIdx < 0) return;

    const viewportH = height - titleRegionH; // matches the inner container's height
    const visibleRows = Math.max(1, Math.floor(viewportH / itemH));
    const midpoint = Math.floor((visibleRows - 1) / 2);
    const maxScroll = Math.max(0, entries.length * itemH - viewportH);
    const target = Math.min(Math.max(0, (focusedFlatIdx - midpoint) * itemH), maxScroll);
    view.scrollToY(target, false); // LV_ANIM_OFF so rapid Down-holds don't lag the cursor
  }, [focusedId, visibleKey, height, zoom]);

  return (
    <Box
      style={{
        width,
        height,
        "background-color": "#000000",
        "border-width": 1,
        "border-color": "#4fc3f7",
        display: "flex",
        "flex-direction": "column",
        "padding-left": outerPadLR,
        "padding-right": outerPadLR,
        "padding-top": outerPadTB,
        "padding-bottom": outerPadTB,
      }}
    >
      <Text style={{ "text-color": "#4fc3f7", "font-size": titleFont, "padding-bottom": r(4) }}>{tree.title}</Text>
      {/* Re-keyed on the visible set: a submenu toggle fully remounts the row list so every Text mounts via
          appendChild in JSX order (lv_binding_js's insertChildBefore just appends). */}
      <Box
        key={visibleKey}
        innerRef={innerViewRef}
        style={{
          width: "100%",
          height: height - titleRegionH,
          "background-opacity": 0,
          display: "flex",
          "flex-direction": "column",
          "align-items": "stretch",
          overflow: "auto",
          "scroll-dir": "none", // desktop: no drag-scroll / scrollbar; keyboard scroll still works
        }}
      >
        {flat.map(({ item, depth }) => {
          if (item.kind === "separator") {
            const padTB = r(4);
            const lineH = Math.max(1, r(1));
            return (
              <Box key={item.id} style={{ width: "100%", height: padTB * 2 + lineH, "background-opacity": 0, "padding-top": padTB, "padding-bottom": padTB }}>
                <Box style={{ width: "100%", height: lineH, "background-color": "#444444" }} />
              </Box>
            );
          }
          const label = item.kind === "submenu" ? `${item.label} ${openItems.has(item.id) ? "v" : ">"}` : item.label;
          const isFocused = focusedId === item.id;
          return (
            <TextAny
              key={item.id}
              ref={(node: unknown) => {
                if (node) refsByIdRef.current.set(item.id, node);
                else refsByIdRef.current.delete(item.id);
              }}
              style={{
                width: "100%",
                "text-color": isFocused ? "#4fc3f7" : "#ffffff",
                "background-color": "#14243f", // full-width highlight bar on the focused row
                "background-opacity": isFocused ? 255 : 0,
                "font-size": itemFont,
                "padding-top": itemPadVert,
                "padding-bottom": itemPadVert,
                "padding-left": basePadLeft + depth * indentStep,
                "padding-right": r(4),
              }}
              onKey={onItemKey}
              onClick={() => activate(item)}
            >
              {label}
            </TextAny>
          );
        })}
      </Box>
    </Box>
  );
}
