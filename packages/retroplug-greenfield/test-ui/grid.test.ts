// The system-grid screen, end to end on the headless display: the empty-state seed adds a live mGB
// tile, the toolbar clones it, and clicking a tile focuses it (the other dims). Proves the whole port:
// useSystems() reactivity (0 → 1 → 2 tiles), the live frame path (getFrame → Canvas blit shows a
// non-blank screen after advancing the core), the StableSlot wrappers, and setFocus's dim signal.

import { test, expect, ui, type WidgetInfo, type UiSnapshot } from "ui-harness";

// True when the snapshot has >1 distinct colour inside `r` — i.e. the tile isn't a flat (unrendered)
// block. A blitted GB frame (boot logo / mGB UI) always varies.
function regionVaried(snap: UiSnapshot, r: WidgetInfo): boolean {
  const { pixels, width } = snap;
  let first = -1;
  for (let yy = r.y; yy < r.y + r.height; yy++) {
    for (let xx = r.x; xx < r.x + r.width; xx++) {
      const i = (yy * width + xx) * 4;
      if (i < 0 || i + 2 >= pixels.length) continue;
      const v = pixels[i] | (pixels[i + 1] << 8) | (pixels[i + 2] << 16);
      if (first < 0) first = v;
      else if (v !== first) return true;
    }
  }
  return false;
}

function clickCenter(w: WidgetInfo): void {
  ui.clickAt(w.x + Math.floor(w.width / 2), w.y + Math.floor(w.height / 2));
}

test("the system grid seeds, clones, focuses, and shows a live emulator frame", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(30);

  // Empty state: the "New mGB" seed, no tiles yet.
  const seed = ui.findByTextContaining("New mGB");
  expect(seed != null).toBeTruthy();
  expect(ui.findByTestId("tile-0")).toBe(null);

  // Seed one mGB → one tile appears (useSystems reactivity through the reconciler).
  clickCenter(seed!);
  ui.pump(20);
  const tile0 = ui.findByTestId("tile-0");
  expect(tile0 != null).toBeTruthy();
  expect(ui.findByTestId("dim-0")).toBe(null); // a single tile is always focused (never dimmed)

  // Advance the core so the tile receives live frames, then confirm the Canvas isn't blank.
  ui.advance(500);
  ui.pump(10);
  const snap0 = ui.snapshot();
  expect(regionVaried(snap0, ui.findByTestId("tile-0")!)).toBeTruthy();

  // Clone the focused system via the toolbar → a second tile.
  const add = ui.findByTextContaining("+ mGB");
  expect(add != null).toBeTruthy();
  clickCenter(add!);
  ui.pump(20);
  expect(ui.findByTestId("tile-0") != null).toBeTruthy();
  expect(ui.findByTestId("tile-1") != null).toBeTruthy();

  // With two tiles exactly one is focused (undimmed). Drive focus and watch the dim overlay move.
  clickCenter(ui.findByTestId("tile-1")!);
  ui.pump(10);
  expect(ui.findByTestId("dim-1")).toBe(null); // tile 1 now focused
  expect(ui.findByTestId("dim-0") != null).toBeTruthy(); // tile 0 dimmed

  clickCenter(ui.findByTestId("tile-0")!);
  ui.pump(10);
  expect(ui.findByTestId("dim-0")).toBe(null); // focus moved to tile 0
  expect(ui.findByTestId("dim-1") != null).toBeTruthy(); // tile 1 dimmed

  ui.advance(300);
  ui.pump(10);
  ui.snapshotPng("/tmp/greenfield-ui-grid.png");
});
