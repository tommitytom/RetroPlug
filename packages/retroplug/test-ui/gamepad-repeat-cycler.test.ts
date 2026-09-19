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

test("holding the d-pad cycles a cycler's value repeatedly, and release stops it", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(30);
  expect(navToPad("Load mGB")).toBeTruthy();
  ui.gamepadTap("a");
  ui.pump(20);
  expect(ui.findByTestId("tile-0") != null).toBeTruthy();
  // Link Group is peer-only: duplicate the instance (as gamepad-nav.test.ts does), then reopen the menu.
  ui.gamepadTap("leftshoulder");
  ui.pump(10);
  expect(navToPad("Duplicate Instance")).toBeTruthy();
  ui.gamepadTap("a");
  ui.pump(20);
  ui.gamepadTap("leftshoulder");
  ui.pump(10);
  expect(navToPad("Link Group")).toBeTruthy();
  expect(ui.findByTextContaining("Link Group: 1") != null).toBeTruthy();

  // Press (NOT tap) dpright: one immediate cycle (1 → 2).
  ui.gamepadButton("dpright", true);
  ui.pump(2);
  expect(ui.findByTextContaining("Link Group: 2") != null).toBeTruthy();

  // No repeat before the hold delay.
  ui.pump(framesFor(230) - 2);
  expect(ui.findByTextContaining("Link Group: 2") != null).toBeTruthy();

  // Past the delay + one period, the held button has cycled twice more (2 → 3 → 4).
  ui.pump(framesFor(575) - framesFor(230));
  expect(ui.findByTextContaining("Link Group: 4") != null).toBeTruthy();

  // Release stops the cycling: 920ms more would have wrapped the 0..4 cycler several times.
  ui.gamepadButton("dpright", false);
  ui.pump(framesFor(920));
  expect(ui.findByTextContaining("Link Group: 4") != null).toBeTruthy();
});
