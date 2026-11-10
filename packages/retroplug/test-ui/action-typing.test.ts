// A contributed action waits while the menu takes typing (docs/modular-graph-phase-f4c-design.md section 7, E10): an
// armed capture row consumes the next key off the same bus the app actions dispatch from, so a key a package bound must
// bind rather than also run its action. The overlay is the observable: the backtick is LSDj's action's default, and a
// press that ran it under the capture would leave the flag flipped when the menu closes. SKIPs when no LSDj ROM was
// staged.
import { test, expect, skip, ui, navTo, Key } from "ui-harness";

const LSDJ = () => ui.romDir() + "/lsdj9_4_2.gb";
const BACKTICK = 0x60;

test("a contributed action's key captured by a bindings row binds, and does not run the action", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(30);
  ui.fileDrop(LSDJ(), 0, 0);
  ui.pump(30);
  if (ui.findByTestId("tile-0") == null) skip("action-typing: no LSDj ROM staged in romDir");
  ui.advance(1500);
  ui.pump(10);

  // The overlay on, so the flag the backtick flips is a thing the test can see afterwards.
  ui.tapKey(BACKTICK);
  ui.advance(300);
  ui.pump(10);
  expect(ui.findByTestId("lsdj-overlay") != null).toBeTruthy();

  // The instance menu → Settings → Keyboard Bindings, and the A row armed: the capture owns the next key.
  ui.tapKey(Key.Esc);
  ui.pump(10);
  expect(navTo("Settings")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(8);
  expect(navTo("Keyboard Bindings")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(8);
  expect(navTo("A: Z, z")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(6);
  expect(ui.focused()!.text.includes("Press a key")).toBeTruthy();

  // The armed row takes the backtick, and the action does not run under it.
  ui.tapKey(BACKTICK);
  ui.pump(10);
  expect(ui.findByText("A: `") != null).toBeTruthy();

  // Esc closes the menu (the row disarmed with the capture). The action never ran, so the flag is still on and the
  // strip comes back with the tile.
  ui.tapKey(Key.Esc);
  ui.pump(10);
  ui.advance(300);
  ui.pump(10);
  expect(ui.findByTestId("lsdj-overlay") != null).toBeTruthy();

  // With the menu closed the same key is the action's: one press flips the flag off.
  ui.tapKey(BACKTICK);
  ui.pump(10);
  expect(ui.findByTestId("lsdj-overlay")).toBe(null);
});
