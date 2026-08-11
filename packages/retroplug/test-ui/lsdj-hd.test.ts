// The LSDj HD player screen, end to end on the headless display. Loads a real LSDj ROM (which
// auto-attaches the lsdj-sync role, so the LSDj submenu appears), opens the HD view from that submenu,
// and asserts:
//   - the screen mounts in place of the grid (the tiles are gone);
//   - it renders the canvas, not the unsupported-cart message;
//   - the canvas actually got pixels - the screen mounting proves nothing on its own, since a renderer
//     that throws every frame still leaves the widget tree intact;
//   - Esc closes it and the grid comes back.
// SKIPs when no LSDj ROM was staged (a large external asset - see run-ui-tests.mjs).

import { test, expect, ui, navTo, Key, type UiSnapshot } from "ui-harness";

const LSDJ = () => ui.romDir() + "/lsdj9_4_2.gb";

/** How many distinct colours the display shows (capped, so it stops early).
 *
 *  This proves the canvas received PIXELS - the failure mode where the render loop throws every frame and
 *  leaves the widget tree perfectly intact, so every findByTestId assertion below still passes.
 *
 *  It deliberately does NOT try to prove the glyphs have the right SHAPE. Rendering every text cell as a
 *  solid block still puts 6 colours on screen against a correct render's 8, and the obvious pixel
 *  statistics don't separate them either (max scanline colour-transitions measures 207 broken vs 265
 *  correct - the sparse mid-shade pixels swamp the signal). That check belongs where it can be exact:
 *  test/lsdj/hd.test.ts asserts the shade -> colour mapping on a known glyph and fails outright if the
 *  font shades are mapped wrong. */
// snapshot().pixels is a Uint8Array of ARGB8888 BYTES (B,G,R,A in memory), width*height*4 - not a word
// array. Index it by byte.
const rgbAt = (s: UiSnapshot, x: number, y: number): number => {
  const i = (y * s.width + x) * 4;
  return (s.pixels[i] << 16) | (s.pixels[i + 1] << 8) | s.pixels[i + 2];
};

function distinctColors(s: UiSnapshot): number {
  const seen = new Set<number>();
  for (let y = 0; y < s.height; y += 7) {
    for (let x = 0; x < s.width; x += 7) {
      seen.add(rgbAt(s, x, y));
      if (seen.size > 4) return seen.size;
    }
  }
  return seen.size;
}

/** Whether scanline `y` is a single flat colour. */
function rowIsFlat(s: UiSnapshot, y: number): boolean {
  const first = rgbAt(s, 0, y);
  for (let x = 1; x < s.width; x++) if (rgbAt(s, x, y) !== first) return false;
  return true;
}

test("the LSDj HD player opens from the instance menu and Esc returns to the grid", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(30);

  ui.fileDrop(LSDJ(), 0, 0);
  ui.pump(30);
  if (ui.findByTestId("tile-0") == null) {
    console.log("# SKIP lsdj-hd: no LSDj ROM staged in romDir");
    return;
  }

  // Boot far enough that the core is running, so readRam/readSram yield live memory.
  ui.advance(1500);
  ui.pump(10);

  // Esc over the tile opens the instance menu; the LSDj submenu holds the HD Player row.
  ui.tapKey(Key.Esc);
  ui.pump(10);
  expect(navTo("LSDj")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(10);

  expect(navTo("HD Player")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(20);
  ui.advance(300); // frame ticks drive the first render (song decode + full canvas paint)
  ui.pump(10);

  // The HD screen replaced the grid.
  expect(ui.findByTestId("lsdj-hd") != null).toBeTruthy();
  expect(ui.findByTestId("tile-0")).toBe(null);
  // It found a supported cart, so it drew the canvas rather than the fallback message.
  expect(ui.findByTestId("lsdj-hd-unsupported")).toBe(null);
  const snap = ui.snapshot();

  // And it actually PAINTED: a drawn grid puts the palette's colour-sets plus the blended mid shade on
  // screen. A render loop that throws every frame leaves this at 1 (the black background) with every
  // widget assertion above still passing.
  expect(distinctColors(snap) > 2).toBeTruthy();

  // The view SCALES TO FIT the window instead of being cropped by it. The harness display is 480x432
  // (aspect 1.111) and the HD grid is 792x576 (aspect 1.375) - wider - so fitting it leaves flat
  // letterbox bands top and bottom, and the middle carries content. Filling the window instead (LVGL's
  // COVER, which is what the inner-align constant used to select) would crop the left and right edges
  // away and leave no flat bands at all.
  expect(rowIsFlat(snap, 0)).toBeTruthy();
  expect(rowIsFlat(snap, snap.height - 1)).toBeTruthy();
  expect(rowIsFlat(snap, Math.floor(snap.height / 2))).toBeFalsy();

  // Esc is the universal back - the grid returns.
  ui.tapKey(Key.Esc);
  ui.pump(20);
  expect(ui.findByTestId("lsdj-hd")).toBe(null);
  expect(ui.findByTestId("tile-0") != null).toBeTruthy();
});
