// The system-grid screen, end to end on the headless display: a tile seeded through the menu shows a
// live frame (getFrame → Canvas blit), a menu-driven clone makes a second tile, and clicking a tile
// focuses it (the other dims). Systems are now added via the menu (Load mGB / Duplicate), so this test
// drives the menu just enough to populate the grid, then asserts the grid behaviour.

import { test, expect, ui, navTo, type WidgetInfo, type UiSnapshot, Key } from "ui-harness";

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

test("the grid shows a live frame and click-to-focus, populated through the menu", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(30);

  // Start menu → focus "Load mGB", Enter adds a tile and the grid takes over.
  expect(navTo("Load mGB")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(20);
  const tile0 = ui.findByTestId("tile-0");
  expect(tile0 != null).toBeTruthy();

  // Advance the core → the tile shows a live (non-blank) frame.
  ui.advance(500);
  ui.pump(10);
  expect(regionVaried(ui.snapshot(), ui.findByTestId("tile-0")!)).toBeTruthy();

  // Duplicate via the instance menu (Esc → menu; navigate to "Duplicate Instance").
  ui.tapKey(Key.Esc);
  ui.pump(10);
  expect(navTo("Duplicate Instance")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(20);
  expect(ui.findByTestId("tile-0") != null).toBeTruthy();
  expect(ui.findByTestId("tile-1") != null).toBeTruthy();

  // Click-to-focus moves the dim overlay between tiles.
  clickCenter(ui.findByTestId("tile-1")!);
  ui.pump(10);
  expect(ui.findByTestId("dim-1")).toBe(null); // tile 1 focused
  expect(ui.findByTestId("dim-0") != null).toBeTruthy();
  clickCenter(ui.findByTestId("tile-0")!);
  ui.pump(10);
  expect(ui.findByTestId("dim-0")).toBe(null); // focus moved to tile 0
  expect(ui.findByTestId("dim-1") != null).toBeTruthy();

  ui.advance(300);
  ui.pump(10);
  ui.snapshotPng("/tmp/ui-grid.png");
});
