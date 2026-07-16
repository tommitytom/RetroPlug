// useWindowSize — the live editor window size, reactive to resizes.
//
// Dimensions.window is a live getter off the LVGL display resolution, but it only re-reads on a React
// render, and nothing else triggers a render when the window resizes (a user drag, a WM tile, or our own
// setWindowSize). The editor emits a "resize" event from onResize; this hook seeds from Dimensions.window
// and updates on that event, so the grid/menu re-lay-out to fill the new window.
//
// requestWindowSize / isWindowSizeControlled are the TS side of the editor's native window seam
// (__rp_setWindowSize / __rp_isWindowSizeControlled). They optional-chain the globals, so they are inert
// in the headless harness (which never installs them) — exactly like the __rp_tagTestId testId hook.

import { useState } from "react";
import { Dimensions } from "lvgljs-ui";

import { useNativeEvent } from "./useNativeEvent";

const FALLBACK = { width: 480, height: 432 };

function readWindowSize(): { width: number; height: number } {
  try {
    const d = (Dimensions as { window?: { width: number; height: number } }).window;
    if (d && d.width > 0 && d.height > 0) return { width: d.width, height: d.height };
  } catch {
    /* fall through to the default */
  }
  return { ...FALLBACK };
}

/** The current editor window size, re-read whenever the editor emits a "resize". */
export function useWindowSize(): { width: number; height: number } {
  const [size, setSize] = useState(readWindowSize);
  useNativeEvent("resize", (...args) => {
    const w = args[0] as number;
    const h = args[1] as number;
    if (w > 0 && h > 0) setSize((prev) => (prev.width === w && prev.height === h ? prev : { width: w, height: h }));
    else setSize(readWindowSize());
  });
  return size;
}

/** Ask the editor for a window of `width`×`height` (fit-to-grid). Inert where the native seam isn't
 *  installed (the harness). */
export function requestWindowSize(width: number, height: number): void {
  (globalThis as { __rp_setWindowSize?: (w: number, h: number) => void }).__rp_setWindowSize?.(width, height);
}

/** True when a tiling WM has taken geometry (so the UI should stop asking to resize). False where the
 *  native seam isn't installed — there is no WM to fight there. */
export function isWindowSizeControlled(): boolean {
  return !!(globalThis as { __rp_isWindowSizeControlled?: () => boolean }).__rp_isWindowSizeControlled?.();
}

/** Set the standalone OS window title. Inert where the native seam isn't installed (the harness / a DAW,
 *  where the plugin doesn't own the window). */
export function setWindowTitle(title: string): void {
  (globalThis as { __rp_setWindowTitle?: (t: string) => void }).__rp_setWindowTitle?.(title);
}
