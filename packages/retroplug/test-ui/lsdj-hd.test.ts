// The LSDj HD player screen, end to end on the headless display. Loads a real LSDj ROM (which
// auto-attaches the lsdj-sync role, so the LSDj submenu appears), opens the HD view from that submenu,
// and asserts:
//   - the screen mounts in place of the grid (the tiles are gone);
//   - it renders the canvas, not the unsupported-cart message;
//   - the canvas actually got pixels - the screen mounting proves nothing on its own, since a renderer
//     that throws every frame still leaves the widget tree intact;
//   - Esc closes it and the grid comes back.
// SKIPs when no LSDj ROM was staged (a large external asset - see run-ui-tests.mjs).

import { test, expect, ui, navTo, Key } from "ui-harness";

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
function distinctColors(): number {
  const { pixels } = ui.snapshot();
  const px = new Uint32Array(pixels);
  const seen = new Set<number>();
  for (let i = 0; i < px.length; i += 97) {
    seen.add(px[i] >>> 0);
    if (seen.size > 4) break;
  }
  return seen.size;
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
  // And it actually PAINTED: a drawn grid puts the palette's colour-sets plus the blended mid shade on
  // screen. A render loop that throws every frame leaves this at 1 (the black background) with every
  // widget assertion above still passing.
  expect(distinctColors() > 2).toBeTruthy();

  // Esc is the universal back - the grid returns.
  ui.tapKey(Key.Esc);
  ui.pump(20);
  expect(ui.findByTestId("lsdj-hd")).toBe(null);
  expect(ui.findByTestId("tile-0") != null).toBeTruthy();
});
