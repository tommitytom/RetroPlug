// Front door for headless UI tests. Mirrors the legacy test/harness/ui.ts, but reuses the
// package's own self-contained test/expect (testing/harness.ts) — so the UI
// suite has NO dependency on the legacy emu harness graph (@retroplug/retroplug etc.).
//
// Usage:
//   import { test, expect, ui, isFlat, CompType, Key } from "ui-harness";
//
// The test runs in the harness's own runtime; `ui.*` drives the booted React UI through the
// C++ bindings the retroplug-ui-test runner installs on Symbol.for("retroplug-ui") — a
// black-box assert on the rendered LVGL tree + snapshot. Only the render-tree surface is exposed (no
// legacy loadRom/loadProject/… — system state is driven through the stores over BackendFacade).

export { test, expect } from "../testing/harness";

/** A located widget's geometry (absolute/screen coords) + content. */
export interface WidgetInfo {
  x: number;
  y: number;
  width: number;
  height: number;
  childCount: number;
  state: number; // lv_obj_get_state bitmask (see State) — e.g. hover/focus
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
  tapKey(lvKey: number, mod?: number): void;
  clickAt(x: number, y: number): void;
  rightClick(x: number, y: number): void;
  moveMouse(x: number, y: number): void;
  scrollAt(x: number, y: number, notchesY: number, notchesX?: number): void;
  gamepadButton(name: string, press: boolean, pad?: number): void;
  gamepadAxis(axis: string, value: number, pad?: number): void;
  fileDrop(paths: string, x: number, y: number): void;
  romDir(): string;
  advance(ms: number): void;
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

// DPF keyboard modifier mask bits (mirror Base.hpp Modifier), for tapKey's `mod` arg.
export const Mod = {
  Shift: 1 << 0, Control: 1 << 1, Alt: 1 << 2, Super: 1 << 3,
} as const;

// lv_obj state bits (mirror LVGL 9.x lv_state_t in lv_obj_style.h), for WidgetInfo.state assertions.
export const State = {
  Checked: 0x0004, Focused: 0x0008, FocusKey: 0x0010, Edited: 0x0020,
  Hovered: 0x0040, Pressed: 0x0080, Scrolled: 0x0100, Disabled: 0x0200,
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
  /** Boot the render scaffold + the UI bundle (idempotent — the runner boots it first). */
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
  /** Tap an LVGL key (see Key) — drives focus-group nav + activation. `mod` is the DPF modifier mask
   *  (see Mod) mirrored on the "key" bus, so a test can type a Shift-held (uppercase) character. */
  tapKey(lvKey: number, mod = 0): void { rp.tapKey(lvKey, mod); },
  /** Click (press+release) at absolute (x,y) → the widget's onClick. */
  clickAt(x: number, y: number): void { rp.clickAt(x, y); },
  /** Emit a RIGHT-button "mouse" bus press+release at (x,y) — drives the App's right-click open-menu path. */
  rightClick(x: number, y: number): void { rp.rightClick(x, y); },
  /** Move the (unpressed) pointer to absolute (x,y) → LVGL hover on the widget under it. */
  moveMouse(x: number, y: number): void { rp.moveMouse(x, y); },
  /** Turn the mouse wheel `notchesY` notches at absolute (x,y) — positive = away from the user (content
   *  moves down). Scrolls the scrollable ancestor under the point, as the plugin + SDL hosts do. */
  scrollAt(x: number, y: number, notchesY: number, notchesX = 0): void { rp.scrollAt(x, y, notchesY, notchesX); },
  /** Emit one SDL controller button transition on the "gamepad-button" bus (name = SDL canonical, e.g.
   *  "dpdown"/"a"/"leftshoulder"). Menu nav / open-button / game routing all read this. */
  gamepadButton(name: string, press: boolean, pad = 0): void { rp.gamepadButton(name, press, pad); },
  /** Emit a continuous axis value on the "gamepad-axis" bus (axis = "leftx"/"lefty"/…, value in [-1,1]). */
  gamepadAxis(axis: string, value: number, pad = 0): void { rp.gamepadAxis(axis, value, pad); },
  /** Press+release a controller button (the pad twin of tapKey): a single deliberate tap. */
  gamepadTap(name: string, pad = 0): void {
    rp.gamepadButton(name, true, pad);
    rp.gamepadButton(name, false, pad);
  },
  /** Emit a native file drop on the "file-drop" bus (the editor's PluginUI::uiFileDropped seam): the
   *  newline-joined absolute paths + the window-space drop coordinate. Routes through App's drag-and-drop
   *  handler exactly as an OS drop would. */
  fileDrop(paths: string | string[], x: number, y: number): void {
    rp.fileDrop(Array.isArray(paths) ? paths.join("\n") : paths, x, y);
  },
  /** The absolute resources/roms directory (so a test can drop a real, bootable ROM by path). */
  romDir(): string { return rp.romDir(); },
  /** Advance the emulator by `ms` so tiles receive live frames (pump() only ticks LVGL). */
  advance(ms: number): void { rp.advance(ms); },
};

/** Tap Down until the focused menu row's label contains `substr`, then stop. Order-robust nav — prefer
 *  this over counting Downs / assuming an item is focused first, so menu reorders don't break tests.
 *  Returns whether the target ended up focused. */
export function navTo(substr: string, maxSteps = 24): boolean {
  for (let i = 0; i < maxSteps; i++) {
    const f = ui.focused();
    if (f && f.text.includes(substr)) return true;
    ui.tapKey(Key.Down);
    ui.pump(2);
  }
  const f = ui.focused();
  return !!f && f.text.includes(substr);
}

/** navTo's gamepad twin: tap the d-pad Down button until the focused row matches, proving the pad drives
 *  the same focus nav the keyboard does. Returns whether the target ended up focused. */
export function navToPad(substr: string, maxSteps = 24): boolean {
  for (let i = 0; i < maxSteps; i++) {
    const f = ui.focused();
    if (f && f.text.includes(substr)) return true;
    ui.gamepadTap("dpdown");
    ui.pump(2);
  }
  const f = ui.focused();
  return !!f && f.text.includes(substr);
}
