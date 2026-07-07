// Front door for greenfield headless UI tests. Mirrors the legacy test/harness/ui.ts, but reuses the
// greenfield package's own self-contained test/expect (testing/harness.ts) — so the greenfield UI
// suite has NO dependency on the legacy emu harness graph (@retroplug/retroplug etc.).
//
// Usage:
//   import { test, expect, ui, isFlat, CompType, Key } from "ui-harness";
//
// The test runs in the harness's own runtime; `ui.*` drives the booted greenfield React UI through the
// C++ bindings the retroplug-greenfield-ui-test runner installs on Symbol.for("retroplug-ui") — a
// black-box assert on the rendered LVGL tree + snapshot. Only the render-tree surface is exposed (no
// legacy loadRom/loadProject/… — greenfield drives system state through the stores over BackendFacade).

export { test, expect } from "../testing/harness";

/** A located widget's geometry (absolute/screen coords) + content. */
export interface WidgetInfo {
  x: number;
  y: number;
  width: number;
  height: number;
  childCount: number;
  text: string; // non-empty only for Text widgets
}

export interface UiSnapshot {
  width: number;
  height: number;
  pixels: Uint8Array; // ARGB8888 (B,G,R,A in memory), width*height*4
}

interface NativeUi {
  boot(): boolean;
  pump(iterations?: number): void;
  reopen(): void;
  snapshot(): { width: number; height: number; pixels: ArrayBuffer };
  snapshotPng(path: string): boolean;
  widgetCount(): number;
  countByType(compType: number): number;
  findByTestId(name: string): WidgetInfo | null;
  findByText(text: string): WidgetInfo | null;
  findByTextContaining(substr: string): WidgetInfo | null;
  findFirstByType(compType: number): WidgetInfo | null;
  focused(): WidgetInfo | null;
  tapKey(lvKey: number): void;
  clickAt(x: number, y: number): void;
}

const rp: NativeUi = (globalThis as Record<symbol, unknown>)[Symbol.for("retroplug-ui")] as NativeUi;

// lv_binding_js component types (mirror ECOMP_TYPE in
// deps/lv_binding_js/src/render/native/core/basic/comp.hpp).
export const CompType = {
  View: 0, Button: 1, Image: 2, Gif: 3, Slider: 4, Arc: 5, Text: 6,
  Window: 7, Switch: 8, Textarea: 9, Checkbox: 10, Dropdownlist: 11,
  ProgressBar: 12, Roller: 13, Line: 14, Calendar: 15, List: 16,
  Tabview: 17, Chart: 18, Mask: 19,
} as const;

// LVGL key codes for tapKey (mirror LV_KEY_*).
export const Key = {
  Up: 17, Down: 18, Right: 19, Left: 20, Esc: 27, Enter: 10,
} as const;

/** True when every pixel is identical (nothing meaningful rendered). */
export function isFlat(snap: UiSnapshot): boolean {
  const p = snap.pixels;
  if (p.length < 8) return true;
  for (let i = 4; i + 4 <= p.length; i += 4) {
    if (p[i] !== p[0] || p[i + 1] !== p[1] || p[i + 2] !== p[2] || p[i + 3] !== p[3]) return false;
  }
  return true;
}

export const ui = {
  /** Boot the render scaffold + the greenfield UI bundle (idempotent — the runner boots it first). */
  boot(): boolean { return rp.boot(); },
  /** Advance the render loop `iterations` blocks (settles the React mount + effects). */
  pump(iterations = 30): void { rp.pump(iterations); },
  /** Detach + re-attach the display on the same runtime (unmount → re-mount). */
  reopen(): void { rp.reopen(); },
  /** Render the active screen to an ARGB snapshot. */
  snapshot(): UiSnapshot {
    const s = rp.snapshot();
    return { width: s.width, height: s.height, pixels: new Uint8Array(s.pixels) };
  },
  /** Write the active screen to a PNG (eyeball parity with `pnpm screenshot`). */
  snapshotPng(path: string): boolean { return rp.snapshotPng(path); },
  /** Total live lv_binding_js components in the tree. */
  widgetCount(): number { return rp.widgetCount(); },
  /** Count components of an ECOMP_TYPE (see CompType). */
  countByType(compType: number): number { return rp.countByType(compType); },
  /** Find a widget tagged via testId, or null. */
  findByTestId(name: string): WidgetInfo | null { return rp.findByTestId(name); },
  /** Find the first Text widget whose label equals `text`, or null. */
  findByText(text: string): WidgetInfo | null { return rp.findByText(text); },
  /** Find the first Text widget whose label contains `substr`, or null. */
  findByTextContaining(substr: string): WidgetInfo | null { return rp.findByTextContaining(substr); },
  /** Find the first widget of a type, or null. */
  findFirstByType(compType: number): WidgetInfo | null { return rp.findFirstByType(compType); },
  /** The widget currently focused in the keypad group, or null. */
  focused(): WidgetInfo | null { return rp.focused(); },
  /** Tap an LVGL key (see Key) — drives focus-group nav + activation. */
  tapKey(lvKey: number): void { rp.tapKey(lvKey); },
  /** Click (press+release) at absolute (x,y) → the widget's onClick. */
  clickAt(x: number, y: number): void { rp.clickAt(x, y); },
};
