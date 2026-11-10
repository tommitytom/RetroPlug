// An overlay survives the instance menu opening and closing (docs/modular-graph-phase-f4c-design section 8.7):
// the menu replaces the tile, which unmounts the overlay's session; the package's flag is the package's, so closing the
// menu reopens the session and the strip comes back without a second press. SKIPs when no LSDj ROM was staged.
import { test, expect, skip, ui, Key } from "ui-harness";

const LSDJ = () => ui.romDir() + "/lsdj9_4_2.gb";
const BACKTICK = 0x60;

test("the LSDj overlay survives the menu opening and closing", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(30);
  ui.fileDrop(LSDJ(), 0, 0);
  ui.pump(30);
  if (ui.findByTestId("tile-0") == null) skip("overlay-survives-menu: no LSDj ROM staged in romDir");
  ui.advance(1500);
  ui.pump(10);

  ui.tapKey(BACKTICK);
  ui.advance(300);
  ui.pump(10);
  expect(ui.findByTestId("lsdj-overlay") != null).toBeTruthy();

  // Esc opens the instance menu (into the tile's stable slot, which stays tagged): the tile's body, and with it the
  // overlay's session, is gone while the menu shows.
  ui.tapKey(Key.Esc);
  ui.pump(10);
  expect(ui.findByTextContaining("Duplicate Instance") != null).toBeTruthy();
  expect(ui.findByTestId("lsdj-overlay")).toBe(null);

  // Esc again closes the menu; the flag was never flipped, so the tile's overlay opens again.
  ui.tapKey(Key.Esc);
  ui.pump(10);
  expect(ui.findByTestId("tile-0") != null).toBeTruthy();
  ui.advance(300);
  ui.pump(10);
  expect(ui.findByTestId("lsdj-overlay") != null).toBeTruthy();
  expect(ui.findByTextContaining("v9.4.2") != null).toBeTruthy();
});
