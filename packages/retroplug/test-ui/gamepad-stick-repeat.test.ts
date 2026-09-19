// Gamepad left-STICK hold-to-repeat in the menu: a deliberate stick push edge-detects (axisToken
// hysteresis) into one move + an armed repeat, so a held stick keeps moving the selected row exactly like
// a held d-pad button — after the same pause, at the same period. Returning the stick to centre stops it.
// The d-pad repeat (gamepad-repeat*.test.ts) already covers value cycling; this covers the stick's move
// path. Timing margins match that file: the harness advances the LVGL tick 23ms per pump() frame and the
// Menu repeats at 400ms (first repeat ≈ 414ms) then every 100ms (≈ 115ms), so ~575ms fires two repeats.

import { test, expect, ui } from "ui-harness";

const frameMs = 23; // the harness advances the tick this much per pump() frame
const framesFor = (ms: number) => Math.ceil(ms / frameMs);

test("holding the left stick repeats the move after a pause, and centring stops it", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(30);
  expect(ui.focused()!.text.includes("Recent")).toBeTruthy();

  // Push the left stick down (lefty+ = Down): one immediate move.
  ui.gamepadAxis("lefty", 0.8);
  ui.pump(2);
  const immediate = ui.focused()!.text;
  expect(immediate.includes("Load...")).toBeTruthy();

  // Nothing repeats before the hold delay (230ms is well under the 400ms pause).
  ui.pump(framesFor(230) - 2);
  expect(ui.focused()!.text).toBe(immediate);

  // Past the delay + one period (~575ms in), the held stick has repeated twice: Recent→Load...→
  // Load mGB→Project. The cursor has clearly kept moving on its own.
  ui.pump(framesFor(575) - framesFor(230));
  expect(ui.focused()!.text.includes("Project")).toBeTruthy();
  const settled = ui.focused()!.text;

  // Centre the stick: the repeat stops. 920ms is ~8 repeat periods — without the release the cursor would
  // have already run on to the last row (Settings).
  ui.gamepadAxis("lefty", 0);
  ui.pump(framesFor(920));
  expect(ui.focused()!.text).toBe(settled);
});
