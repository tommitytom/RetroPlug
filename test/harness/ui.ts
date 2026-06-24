// Front door for headless UI tests. Mirrors test/harness/index.ts (the emu
// harness) but exposes a `ui` facade over the native Symbol.for("retroplug-ui")
// namespace installed by the retroplug-ui-test runner (test/ui/UiTsRunner.cpp).
//
// Usage:
//   import { test, expect, ui, CompType, Key } from "ui-harness";
//
// test/expect (and the window-'load' -> runAll TAP trigger) are reused verbatim
// from index.ts — importing it registers the runner. The test runs in this
// runtime; `ui.boot()` spins up the real React UI bundle in a second runtime and
// every ui.* call drives it through C++ bindings (black-box: assert on the
// rendered LVGL tree + snapshot).

export { test, expect } from "./index";

// A located widget's geometry (absolute/screen coords) + content.
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
  loadRom(path: string, savPath?: string): number;
  loadProject(path: string): boolean;
  selectFile(path: string): void;
  writeFile(path: string, bytes: ArrayBuffer): void;
  writeProjectJson(path: string, romPath: string): void;
  seedRecent(path: string, name?: string): void;
  requestCloseConfirm(): void;
  quitRequested(): boolean;
  pump(iterations?: number): void;
  readMemory(sys: number, type: number): ArrayBuffer;
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

const rp: NativeUi = (globalThis as any)[Symbol.for("retroplug-ui")];

// lv_binding_js component types (mirror ECOMP_TYPE in
// deps/lv_binding_js/src/render/native/core/basic/comp.hpp). Canvas piggybacks
// on lv_image, so the emulator tile's <Canvas> reports as Image.
export const CompType = {
  View: 0, Button: 1, Image: 2, Gif: 3, Slider: 4, Arc: 5, Text: 6,
  Window: 7, Switch: 8, Textarea: 9, Checkbox: 10, Dropdownlist: 11,
  ProgressBar: 12, Roller: 13, Line: 14, Calendar: 15, List: 16,
  Tabview: 17, Chart: 18, Mask: 19,
} as const;

// LVGL key codes for tapKey (drive menu focus-group nav + activation; the
// arrows also map to the Game Boy d-pad when a tile is focused).
export const Key = {
  Up: 17, Down: 18, Right: 19, Left: 20,
  Enter: 10, Esc: 27, Del: 127, Backspace: 8,
} as const;

// Memory regions (mirror rp::MemoryType / packages/native/src/system/MemoryType.hpp).
export const Mem = {
  Ram: 0, Rom: 1, Sram: 2, Vram: 3, IORegisters: 4,
  HRam: 5, OAM: 6, NametableRam: 7, ExtWorkRam: 8,
} as const;

/** Count of differing RGBA pixels between two snapshots (-1 if shapes differ). */
export function pixelDiff(a: UiSnapshot, b: UiSnapshot): number {
  if (a.width !== b.width || a.height !== b.height || a.pixels.length !== b.pixels.length)
    return -1;
  let n = 0;
  for (let i = 0; i + 4 <= a.pixels.length; i += 4) {
    if (a.pixels[i] !== b.pixels[i] || a.pixels[i + 1] !== b.pixels[i + 1] ||
        a.pixels[i + 2] !== b.pixels[i + 2] || a.pixels[i + 3] !== b.pixels[i + 3]) n++;
  }
  return n;
}

/** True when every pixel is identical (nothing meaningful rendered). */
export function isFlat(snap: UiSnapshot): boolean {
  const p = snap.pixels;
  if (p.length < 8) return true;
  for (let i = 4; i + 4 <= p.length; i += 4) {
    if (p[i] !== p[0] || p[i + 1] !== p[1] || p[i + 2] !== p[2] || p[i + 3] !== p[3])
      return false;
  }
  return true;
}

export const ui = {
  /** Create a headless display + boot the real React UI bundle. Call first in
   *  each test (beginCase tears down the previous harness). */
  boot(): boolean { return rp.boot(); },
  /** Load a (SameBoy) ROM into the project and focus it; returns the system id.
   *  Optional `savPath` is a .sav file (raw cartridge SRAM) seeding battery RAM
   *  so LSDj boots straight to the song screen. */
  loadRom(path: string, savPath?: string): number { return rp.loadRom(path, savPath); },
  /** Load a project file via the real PluginRpcService path (detects missing
   *  ROMs / kit WAVs → "missing-files" event → relink menu, or commits). */
  loadProject(path: string): boolean { return rp.loadProject(path); },
  /** Inject a file-browser selection (headless stand-in for the native dialog),
   *  e.g. to complete a relink "Locate…" browse. */
  selectFile(path: string): void { rp.selectFile(path); },
  /** Stage raw bytes on disk (e.g. a project JSON or ROM) before loading. */
  writeFile(path: string, bytes: Uint8Array): void {
    rp.writeFile(path, bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength) as ArrayBuffer);
  },
  /** Write a schema-correct thin project JSON (one path-only SameBoy system at
   *  `romPath`). Point it at a non-existent path to drive the relink flow. */
  writeProjectJson(path: string, romPath: string): void { rp.writeProjectJson(path, romPath); },
  /** Add a project to the recent list (optional display alias) and refresh the
   *  menu. getRecentFiles() flags `missing` when `path` is absent on disk. */
  seedRecent(path: string, name?: string): void { rp.seedRecent(path, name); },
  /** Emit "confirm-close" (as PluginUI::onClose does on a vetoed standalone
   *  close) to drive the unsaved-changes modal. */
  requestCloseConfirm(): void { rp.requestCloseConfirm(); },
  /** True once the modal's Discard/Save path invoked quitStandalone(). */
  quitRequested(): boolean { return rp.quitRequested(); },
  /** Advance the UI + emulator `iterations` blocks (settles RPC + render). */
  pump(iterations = 30): void { rp.pump(iterations); },
  /** Read a whole memory region of a system as a copy (see Mem). */
  readMemory(sys: number, type: number): Uint8Array { return new Uint8Array(rp.readMemory(sys, type)); },
  /** Render the active screen to an ARGB snapshot. */
  snapshot(): UiSnapshot {
    const s = rp.snapshot();
    return { width: s.width, height: s.height, pixels: new Uint8Array(s.pixels) };
  },
  /** Write the active screen to a PNG (for eyeball parity with `make screenshot`). */
  snapshotPng(path: string): boolean { return rp.snapshotPng(path); },
  /** Total live lv_binding_js components in the tree. */
  widgetCount(): number { return rp.widgetCount(); },
  /** Count components of an ECOMP_TYPE (see CompType). */
  countByType(compType: number): number { return rp.countByType(compType); },
  /** Find a widget tagged via testId (SystemGrid slots), or null. */
  findByTestId(name: string): WidgetInfo | null { return rp.findByTestId(name); },
  /** Find the first Text widget whose label equals `text`, or null. */
  findByText(text: string): WidgetInfo | null { return rp.findByText(text); },
  /** Find the first Text widget whose label contains `substr` (handles
   *  multi-line labels), or null. */
  findByTextContaining(substr: string): WidgetInfo | null { return rp.findByTextContaining(substr); },
  /** Find the first widget of a type, or null. */
  findFirstByType(compType: number): WidgetInfo | null { return rp.findFirstByType(compType); },
  /** The widget currently focused in the menu's keypad group, or null. Use to
   *  navigate deterministically: tapKey(Down) until focused().text is the target. */
  focused(): WidgetInfo | null { return rp.focused(); },
  /** Tap an LVGL key (see Key) — drives focus-group nav + activation. */
  tapKey(lvKey: number): void { rp.tapKey(lvKey); },
  /** Click (press+release) at absolute (x,y) -> the widget's onClick. */
  clickAt(x: number, y: number): void { rp.clickAt(x, y); },
};
