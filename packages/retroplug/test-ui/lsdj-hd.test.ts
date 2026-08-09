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

/** How many distinct colours the display is showing. An unpainted (or all-black) screen is 1; a drawn
 *  LSDj grid is many, since the palette's colour-sets and the blended mid shade all appear. */
function distinctColors(): number {
  const { pixels } = ui.snapshot();
  const px = new Uint32Array(pixels);
  const seen = new Set<number>();
  // Sample rather than walk every pixel - enough to tell "painted" from "blank" without the cost.
  for (let i = 0; i < px.length; i += 97) {
    seen.add(px[i] >>> 0);
    if (seen.size > 8) break;
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
  // And it actually PAINTED: the song / chain / phrase grid puts several palette colours on screen. A
  // renderer that threw on every frame would leave this at 1 (the black background) with every widget
  // assertion above still passing.
  expect(distinctColors() > 2).toBeTruthy();

  // Esc is the universal back - the grid returns.
  ui.tapKey(Key.Esc);
  ui.pump(20);
  expect(ui.findByTestId("lsdj-hd")).toBe(null);
  expect(ui.findByTestId("tile-0") != null).toBeTruthy();
});
