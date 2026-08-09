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

import { useCallback, useEffect, useLayoutEffect, useRef, useState } from "react";
import { Text, ELvKey } from "lvgljs-ui";

import { useFocusGroup } from "../../lvgl/useFocusGroup";
import { useNativeEvent } from "../../lvgl/useNativeEvent";
import { Box } from "../../lvgl/Box";
import {
  dpfCodeToKeyName,
  axisToken,
  menuNavForButton,
  menuNavForAxisToken,
  KEY_ESCAPE,
  KEY_BACKSPACE,
  KEY_ENTER,
  KEY_PAGE_UP,
  KEY_PAGE_DOWN,
  KEY_DELETE,
  type MenuNav,
} from "../../../src/keyCodes";
import { setMenuModalActive } from "./menuModal";
import { applyCasing } from "./promptCasing";
import type { MenuItem, MenuTree, PromptSpec } from "./menuTree";

const MOD_SHIFT = 1 << 0; // DPF modifier mask bit for Shift (mirrors Base.hpp kModifierShift), on the "key" bus

const CAPTURE_COLOR = "#ffb74d"; // orange, matching the legacy capture-armed row
const DISABLED_COLOR = "#666666"; // greyed text for an inert (unavailable-for-this-cart) row
const WARN_COLOR = "#ffd54f"; // yellow — a warning row (a recent entry whose file is missing)
const GAMEPAD_CAPTURE_AXIS = 0.6; // a stick must pass this (past the play threshold) to bind as an axis token
// Mouse-hover bar: a dimmer navy than the focus bar (#14243f), so the row under the pointer reads as
// highlighted but subordinate to the keyboard-selected row. A pre-dimmed colour at full opacity (a
// state-style's background-opacity isn't reliably applied). LVGL toggles it on LV_STATE_HOVERED.
const ROW_HOVER_STYLE = { "background-color": "#0d1626", "background-opacity": 255 } as const;

/** The live prompt overlay: its spec, the typed value, and any error string (shown red). */
interface PromptState {
  spec: PromptSpec;
  value: string;
  error: string;
}

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
  // The armed capture row (a "capture" item awaiting its next key), or "". The ref is set synchronously so
  // the key-bus handler and onItemKey see the armed state within the same event; the state repaints the row.
  const [capturingId, setCapturingId] = useState<string>("");
  const capturingIdRef = useRef<string>("");
  const setCapturing = useCallback((id: string) => {
    capturingIdRef.current = id;
    setCapturingId(id);
  }, []);
  // The live text/confirm prompt overlay, or null. The ref mirrors it so the once-mounted key handler
  // reads the latest; the state drives the render.
  const [promptState, setPromptState] = useState<PromptState | null>(null);
  const promptStateRef = useRef<PromptState | null>(null);
  const setPrompt = useCallback((next: PromptState | null) => {
    promptStateRef.current = next;
    setPromptState(next);
  }, []);

  // Signal App's Esc handler while a capture/prompt modal is armed, so Esc cancels the modal instead of
  // closing the menu. Flipped from an effect (post-render) so it stays set through the synchronous Esc.
  useEffect(() => {
    setMenuModalActive(!!capturingId || promptState != null);
    return () => setMenuModalActive(false);
  }, [capturingId, promptState]);

  // id → LVGL Text ref, and the current flat (visible) list. Refs are keyed by stable id (not index) so
  // separators can sit in the list without breaking alignment.
  const refsByIdRef = useRef<Map<string, unknown>>(new Map());
  // The authoritative focus cursor (a ref, not React state — the highlight is LVGL-native). Moved only by
  // explicit nav / click / rebuild.
  const focusedIdRef = useRef<string>("");
  // The inner scrollable container + a cached row height, for the keyboard scroll-follow effect below.
  const innerViewRef = useRef<{ scrollToY?: (y: number, animate: boolean) => void } | null>(null);
  const itemHeightRef = useRef<number>(0);
  // `${pad}:${axisName}` → the half-axis token the left stick is currently in, for edge-detected nav (a
  // held stick fires one move, not a stream). Separate from useGamepadInput's — this one drives the menu.
  const menuAxisDirRef = useRef<Map<string, string>>(new Map());

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
        .filter((f) => f.item.kind !== "separator" && !f.item.disabled)
        .map((f) => refsByIdRef.current.get(f.item.id))
        .filter((x) => x != null),
    [],
  );

  // Keep the previously-focused row if it still exists, else the first focusable; sync focusedIdRef.
  const focusTarget = useCallback(() => {
    const focusable = flatRef.current.filter((f) => f.item.kind !== "separator" && !f.item.disabled).map((f) => f.item.id);
    let id = focusedIdRef.current;
    if (!focusable.includes(id)) id = focusable[0] ?? "";
    focusedIdRef.current = id;
    setFocusedId(id);
    return id ? refsByIdRef.current.get(id) : undefined;
  }, []);

  const focus = useFocusGroup(orderedRefs, { deps: [visibleKey], focusTarget });

  const activate = useCallback(
    (item: MenuItem) => {
      if (item.disabled) return; // greyed row: fully inert, don't even move the highlight
      focusedIdRef.current = item.id; // so the rebuild keeps this row focused
      setFocusedId(item.id);
      // Capture / prompt rows arm from the key bus (below), not here — an Enter/click here would race the
      // captured/confirming Enter, so LVGL's CLICKED on those rows is a no-op.
      if (item.kind === "capture" || item.kind === "prompt") return;
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

  // Run the open prompt's onConfirm: a returned error string keeps it open (shown red); null closes it.
  const confirmPrompt = useCallback(() => {
    const ps = promptStateRef.current;
    if (!ps) return;
    const err = ps.spec.onConfirm(ps.value);
    if (err) setPrompt({ ...ps, error: err });
    else setPrompt(null);
  }, [setPrompt]);

  const onItemKey = useCallback(
    (e: { key: number }) => {
      (e as { stopPropagation?: () => void }).stopPropagation?.(); // else bubbles to the scroll View
      if (capturingIdRef.current || promptStateRef.current) return; // freeze nav while a modal is armed
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
      while (next >= 0 && next < entries.length && (entries[next].item.kind === "separator" || entries[next].item.disabled)) next += dir;
      if (next < 0 || next >= entries.length) return; // no wrap-around
      const nextId = entries[next].item.id;
      focusedIdRef.current = nextId;
      setFocusedId(nextId);
      const nextRef = refsByIdRef.current.get(nextId);
      if (nextRef) focus(nextRef); // move keypad focus; the scroll-follow effect keeps the row on-screen
    },
    [focus],
  );

  // Modal key input for "capture" and "prompt" rows. Letter keys reach the UI only on the raw "key" bus,
  // not through LVGL's focus-group nav — so arm / type / bind live here. Esc cancels the armed modal;
  // App's Esc handler defers while one is active (menuModal), so the menu stays open.
  useNativeEvent("key", (...args) => {
    const code = args[0] as number;
    const press = args[1] as boolean;
    const mod = (args[2] as number) ?? 0;
    if (!press) return;
    const itemById = (id: string): MenuItem | undefined => flatRef.current.find((f) => f.item.id === id)?.item;

    // 1. An open prompt owns all input.
    const prompt = promptStateRef.current;
    if (prompt) {
      if (code === KEY_ESCAPE) return setPrompt(null); // cancel — no callback
      if (code === KEY_ENTER) return confirmPrompt();
      if (prompt.spec.confirm) return; // yes/no dialog: only Enter/Esc
      if (code === KEY_BACKSPACE) return setPrompt({ ...prompt, value: prompt.value.slice(0, -1), error: "" });
      if (code >= 0x20 && code <= 0x7e) {
        // DPF's key code is unshifted (always lowercase for letters); apply Shift / the prompt's casing here.
        const ch = applyCasing(String.fromCharCode(code), (mod & MOD_SHIFT) !== 0, prompt.spec.casing);
        if (!prompt.spec.filter || prompt.spec.filter(ch)) {
          setPrompt({ ...prompt, value: (prompt.value + ch).slice(0, 48), error: "" });
        }
      }
      return;
    }

    // 2. An armed capture row consumes the next key — but only a keyboard-source row; a gamepad-source row
    //    waits for the pad bus (below) and ignores stray keys. Esc cancels either.
    const armed = capturingIdRef.current;
    if (armed) {
      if (code === KEY_ESCAPE) return setCapturing(""); // cancel — bind nothing
      if (itemById(armed)?.capture?.source === "gamepad") return; // gamepad row: keep listening on the pad bus
      setCapturing("");
      const name = dpfCodeToKeyName(code);
      if (name == null) return; // an unbindable key — treat as cancel
      itemById(armed)?.capture?.onCapture(name);
      return;
    }

    // 3. Idle: PageUp/PageDown drive a cycler's COARSE step (the arrows do the fine step via the LVGL keypad
    //    path; Page keys aren't LVGL-translated so they only reach here). Then Enter arms a focused
    //    capture/prompt row; Backspace clears a focused capture row.
    const focused = itemById(focusedIdRef.current);
    if (!focused) return;
    // Per-row hotkey (a recent entry opts in): Del invokes its delete.
    if (code === KEY_DELETE && focused.onDelete) return focused.onDelete();
    if (code === KEY_PAGE_UP) return focused.onCoarseStep?.(1);
    if (code === KEY_PAGE_DOWN) return focused.onCoarseStep?.(-1);
    if (focused.kind === "capture") {
      if (code === KEY_ENTER) setCapturing(focused.id);
      else if (code === KEY_BACKSPACE) focused.capture?.onClear();
    } else if (focused.kind === "prompt" && code === KEY_ENTER && focused.prompt) {
      setPrompt({ spec: focused.prompt, value: focused.prompt.initial ?? "", error: "" });
    }
  });

  // Apply a resolved menu-nav action from the gamepad, reusing the exact primitives the keyboard drives:
  // Up/Down/Left/Right feed onItemKey (a synthetic {key} is fine — it optional-chains stopPropagation);
  // Back closes the menu. Select reproduces BOTH the key-bus idle arming AND LVGL's Enter→CLICKED activate,
  // since the gamepad bus never reaches the keypad indev (so there's no CLICKED to lean on).
  const applyMenuNav = useCallback(
    (nav: MenuNav) => {
      if (nav === "up") return onItemKey({ key: ELvKey.LV_KEY_UP });
      if (nav === "down") return onItemKey({ key: ELvKey.LV_KEY_DOWN });
      if (nav === "left") return onItemKey({ key: ELvKey.LV_KEY_LEFT });
      if (nav === "right") return onItemKey({ key: ELvKey.LV_KEY_RIGHT });
      if (nav === "back") return onClose();
      const focused = flatRef.current.find((f) => f.item.id === focusedIdRef.current)?.item;
      if (!focused) return;
      if (focused.kind === "capture") return setCapturing(focused.id);
      if (focused.kind === "prompt" && focused.prompt) {
        return setPrompt({ spec: focused.prompt, value: focused.prompt.initial ?? "", error: "" });
      }
      activate(focused);
    },
    [onItemKey, onClose, activate, setCapturing, setPrompt],
  );

  // Gamepad: bind a rebind row when one is armed, else navigate the menu like the keyboard. menuOpen already
  // gates useGamepadInput off while the menu is up, so game routing never competes for these events.
  useNativeEvent("gamepad-button", (...args) => {
    const name = args[1] as string;
    const press = args[2] as boolean;
    if (!press) return; // the menu acts on press only

    // 1. An open prompt owns the pad: A confirms, B cancels; ignore the rest.
    if (promptStateRef.current) {
      if (name === "a") confirmPrompt();
      else if (name === "b") setPrompt(null);
      return;
    }
    // 2. An armed capture row consumes the button: a gamepad-source row binds its SDL name; a
    //    keyboard-source row (still listening on the key bus) cancels on Back. Either way nav stays frozen.
    const armed = capturingIdRef.current;
    if (armed) {
      const item = flatRef.current.find((f) => f.item.id === armed)?.item;
      if (item?.capture?.source === "gamepad") {
        setCapturing("");
        item.capture.onCapture(name); // raw SDL button name is the token
      } else if (name === "b") {
        setCapturing(""); // cancel a keyboard-armed row via Back
      }
      return;
    }
    // 3. Idle: drive nav (d-pad moves + cycles, A selects, B backs out) exactly like the keyboard.
    const nav = menuNavForButton(name);
    if (nav) applyMenuNav(nav);
  });
  useNativeEvent("gamepad-axis", (...args) => {
    const pad = args[0] as number;
    const axisName = args[1] as string;
    const value = args[2] as number;

    // 1. An armed gamepad-source row binds a deliberate stick flick as a half-axis token.
    const armed = capturingIdRef.current;
    if (armed) {
      if (Math.abs(value) < GAMEPAD_CAPTURE_AXIS) return; // ignore drift; a deliberate flick binds
      const item = flatRef.current.find((f) => f.item.id === armed)?.item;
      if (item?.capture?.source !== "gamepad") return;
      setCapturing("");
      item.capture.onCapture(`${axisName}${value < 0 ? "-" : "+"}`); // half-axis token
      return;
    }
    if (promptStateRef.current) return; // a prompt owns input; don't drift the axis-dir map

    // 2. Idle: the left stick navigates like the d-pad, edge-detected (via the axisToken hysteresis) so a
    //    held stick fires a single move, not a stream.
    if (axisName !== "leftx" && axisName !== "lefty") return;
    const key = `${pad}:${axisName}`;
    const cur = menuAxisDirRef.current.get(key) ?? "";
    const next = axisToken(axisName, value, cur);
    if (next === cur) return;
    menuAxisDirRef.current.set(key, next);
    const nav = menuNavForAxisToken(next); // null when centred
    if (nav) applyMenuNav(nav);
  });

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
          const isCapturing = capturingId === item.id;
          let label: string;
          if (item.kind === "submenu") label = `${item.label} ${openItems.has(item.id) ? "v" : ">"}`;
          else if (isCapturing) {
            const colon = item.label.indexOf(":"); // keep the "<Button>: " head, swap the value for a prompt
            const what = item.capture?.source === "gamepad" ? "button" : "key";
            label = `${colon >= 0 ? item.label.slice(0, colon) : item.label}: Press a ${what}...`;
          } else label = item.label;
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
                "text-color": item.disabled ? DISABLED_COLOR : isCapturing ? CAPTURE_COLOR : item.warn ? WARN_COLOR : isFocused ? "#4fc3f7" : "#ffffff",
                "background-color": "#14243f", // full-width highlight bar on the focused row
                "background-opacity": isFocused ? 255 : 0,
                "font-size": itemFont,
                "padding-top": itemPadVert,
                "padding-bottom": itemPadVert,
                "padding-left": basePadLeft + depth * indentStep,
                "padding-right": r(4),
              }}
              onHoveredStyle={item.disabled ? undefined : ROW_HOVER_STYLE}
              onKey={onItemKey}
              onClick={() => activate(item)}
            >
              {label}
            </TextAny>
          );
        })}
      </Box>
      {promptState &&
        (() => {
          const sp = promptState.spec;
          const promptW = Math.max(r(120), width - r(32));
          const promptX = Math.max(0, Math.floor((width - promptW) / 2));
          const promptY = titleRegionH + r(8);
          const fontSize = r(14);
          const rowH = r(22);
          const hint =
            sp.hint ??
            (sp.confirm ? "Enter to confirm  |  Esc to cancel" : "Enter to confirm  |  Esc to cancel  |  Backspace to erase");
          return (
            <Box
              style={{
                position: "absolute",
                left: promptX,
                top: promptY,
                width: promptW,
                "background-color": "#000000",
                "border-width": 1,
                "border-color": "#4fc3f7",
                display: "flex",
                "flex-direction": "column",
                "row-spacing": r(4),
                "padding-left": r(8),
                "padding-right": r(8),
                "padding-top": r(6),
                "padding-bottom": r(6),
              }}
            >
              <Text style={{ "text-color": "#4fc3f7", "font-size": fontSize, width: "100%", height: rowH }}>{sp.title}</Text>
              {!sp.confirm && (
                <Text
                  style={{
                    "text-color": "#ffffff",
                    "background-color": "#333333",
                    "background-opacity": 255,
                    "font-size": fontSize,
                    width: "100%",
                    height: rowH,
                    "padding-left": r(4),
                    "padding-right": r(4),
                    "padding-top": r(2),
                    "padding-bottom": r(2),
                  }}
                >
                  {promptState.value + "_"}
                </Text>
              )}
              <Text style={{ "text-color": promptState.error ? "#ef5350" : "#888888", "font-size": fontSize, width: "100%", height: rowH }}>
                {promptState.error || hint}
              </Text>
            </Box>
          );
        })()}
    </Box>
  );
}
