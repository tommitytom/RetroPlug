// Gamepad hold-to-repeat in the menu: SDL emits exactly ONE press event per gamepad-button press (unlike
// keys, which the OS auto-repeats into LVGL's keypad indev), so a held d-pad button must be re-fired by the
// Menu itself — after a short pause, then at a fixed period — for held Down to keep moving the selected
// row and held Right to keep cycling a value.
//
// Timing is asserted with generous margins. The harness advances the LVGL tick 23ms per pump() frame and
// the Menu repeats at HOLD_REPEAT_DELAY_MS=400 (first repeat on the first frame ≥400ms in ≈ 414ms) then
// every HOLD_REPEAT_PERIOD_MS=100 (≈ every 115ms), so a hold of ~575ms fires exactly two repeats.

import { test, expect, ui, navToPad } from "ui-harness";

const frameMs = 23; // the harness advances the tick this much per pump() frame
const framesFor = (ms: number) => Math.ceil(ms / frameMs);

test("holding a d-pad button repeats the move after a pause, and release stops it", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(30);
  expect(ui.focused()!.text.includes("Recent")).toBeTruthy();

  // Press (NOT tap) dpdown: the press moves exactly one row immediately.
  ui.gamepadButton("dpdown", true);
  ui.pump(2);
  const immediate = ui.focused()!.text;
  expect(immediate.includes("Load...")).toBeTruthy();

  // Nothing repeats before the hold delay (230ms is well under the 400ms pause).
  ui.pump(framesFor(230) - 2);
  expect(ui.focused()!.text).toBe(immediate);

  // Past the delay + one period (~575ms in), the held button has repeated twice: Recent→Load...→
  // Load mGB→Project. The cursor has clearly kept moving on its own.
  ui.pump(framesFor(575) - framesFor(230));
  expect(ui.focused()!.text.includes("Project")).toBeTruthy();
  const settled = ui.focused()!.text;

  // Release: the repeat stops. 920ms is ~8 repeat periods — without the release the cursor would have
  // already run on to the last row (Settings).
  ui.gamepadButton("dpdown", false);
  ui.pump(framesFor(920));
  expect(ui.focused()!.text).toBe(settled);
});
