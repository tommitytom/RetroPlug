// The mouse wheel scrolls an overflowing menu. LVGL has no wheel handling of its own, so every UI host
// runs the same shared hit-test scroll (retroplug::ui::scrollAtPoint): the DPF editor from onScroll, the
// SDL2 standalone from SDL_MOUSEWHEEL — which it simply never handled, so the wheel was dead there — and
// this harness from ui.scrollAt. Asserting on the rendered row positions proves the container actually
// moved, not just that the call returned.

import { test, expect, ui, navTo, Key } from "ui-harness";

test("the mouse wheel scrolls an overflowing menu, and stops at its ends", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(30);

  // Get to a menu tall enough to overflow: load the embedded synth, open the instance menu, expand System
  // (the same overflow the keyboard scroll-follow assertion in menu.test.ts relies on).
  expect(navTo("Load mGB")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(20);
  ui.tapKey(Key.Esc);
  ui.pump(10);
  expect(navTo("System")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(15);

  // A row near the top of the list, and a point inside the menu to put the cursor on.
  const row = () => ui.findByTextContaining("Duplicate Instance")!;
  expect(row() != null).toBeTruthy();
  const px = row().x + Math.floor(row().width / 2);
  const py = row().y + Math.floor(row().height / 2);

  // Start from the top: navigating to System already scroll-followed the container part way down, and a
  // spin up is bounded, so this parks it at a known end (and proves the up direction moves the list down).
  const partScrolled = row().y;
  ui.scrollAt(px, py, 50);
  const top = row().y;
  console.log(`row y: ${partScrolled} (scroll-followed) -> ${top} (wheeled to the top)`);
  expect(top > partScrolled).toBeTruthy();
  ui.scrollAt(px, py, 50); // already at the top → bounded, no runaway offset
  expect(row().y).toBe(top);

  // Wheel down → the list moves up by exactly one step per notch (kWheelStep = 24px).
  ui.scrollAt(px, py, -3);
  expect(row().y).toBe(top - 3 * 24);
  ui.scrollAt(px, py, 3);
  expect(row().y).toBe(top);

  // Bounded at the far end too: a big spin down leaves the last row on-screen rather than scrolling the
  // content clean out of the viewport.
  const win = ui.snapshot();
  ui.scrollAt(px, py, -50);
  const last = ui.findByTextContaining("Settings")!;
  expect(last.y >= 0 && last.y + last.height <= win.height).toBeTruthy();

  // The wheel is inert where nothing overflows: back on the grid, no scrollable ancestor claims it.
  ui.tapKey(Key.Esc);
  ui.pump(10);
  const tile = ui.findByTestId("tile-0")!;
  const tileY = tile.y;
  ui.scrollAt(tile.x + 4, tile.y + 4, -5);
  expect(ui.findByTestId("tile-0")!.y).toBe(tileY);

  ui.snapshotPng("/tmp/ui-wheel-scroll.png");
});
