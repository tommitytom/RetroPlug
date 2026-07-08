// Menu rows highlight on mouse hover. Uses the harness's headless pointer-move (ui.moveMouse) to hover a
// non-focused row, then asserts both LVGL's LV_STATE_HOVERED flag AND that onHoveredStyle actually paints
// the row background (a pixel sample past the label text). Guards the framework fix (STYLE_TYPE.STATE_*
// realigned to LVGL 9.x) — without it the style bound to the wrong state and never triggered.

import { test, expect, ui, State } from "ui-harness";

// The row background colour at a pixel (snapshot is ARGB8888: B,G,R,A in memory).
function bgAt(x: number, y: number): { r: number; g: number; b: number } {
  const s = ui.snapshot();
  const idx = (y * s.width + x) * 4;
  return { b: s.pixels[idx], g: s.pixels[idx + 1], r: s.pixels[idx + 2] };
}

test("a menu row shows the hover bar when the pointer moves over it", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(30);

  // "Recent" is a non-focused start-menu row, so any highlight there is purely from hover.
  const row = ui.findByTextContaining("Recent")!;
  expect(row.state & State.Hovered).toBe(0);

  const sx = row.x + row.width - 6; // sample the background near the right edge, past the label glyphs
  const sy = row.y + Math.floor(row.height / 2);
  const before = bgAt(sx, sy);
  expect(before.r < 8 && before.g < 8 && before.b < 8).toBeTruthy(); // black before hover

  ui.moveMouse(row.x + Math.floor(row.width / 2), sy);
  ui.pump(4);

  const hovered = ui.findByTextContaining("Recent")!;
  expect((hovered.state & State.Hovered) !== 0).toBeTruthy(); // LVGL flagged the row hovered
  const after = bgAt(sx, sy);
  expect(after.b > before.b + 4).toBeTruthy(); // the onHoveredStyle navy bar now paints the row bg

  // The hover bar is dimmer than the keyboard-focus bar (on the focused "Load..." row).
  const focus = ui.findByTextContaining("Load...")!;
  const focusBar = bgAt(focus.x + focus.width - 6, focus.y + Math.floor(focus.height / 2));
  console.log(`hover bg=${JSON.stringify(after)} focus bar=${JSON.stringify(focusBar)}`);
  expect(after.b < focusBar.b).toBeTruthy();

  ui.snapshotPng("/tmp/greenfield-ui-hover.png");

  // Moving to blank space clears the hover.
  ui.moveMouse(Math.floor(row.width / 2), row.y + row.height * 8);
  ui.pump(4);
  expect(ui.findByTextContaining("Recent")!.state & State.Hovered).toBe(0);
});
