// Menu edge behavior for the gamepad: a HELD d-pad button's repeats stop at the list's end (the edge is a
// wall during the hold — holding Down at the bottom must not bounce the cursor back to the top), but the
// FIRST press of a NEW hold at the edge wraps to the far end, matching the keyboard's fresh-press wrap.
// The start menu (headless, non-standalone) is: Recent, Load..., Load mGB (GB MIDI Synth), [sep], Project,
// Settings. Timing margins match gamepad-repeat.test.ts (repeat ≈ 414ms in, then every ≈ 115ms).

import { test, expect, ui } from "ui-harness";

const frameMs = 23; // the harness advances the tick this much per pump() frame
const framesFor = (ms: number) => Math.ceil(ms / frameMs);

test("held repeats clamp at the menu edge; a fresh press after release wraps to the other side", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(30);
  expect(ui.focused()!.text.includes("Recent")).toBeTruthy();

  // Hold dpdown: one immediate move, then repeats carry the cursor to the bottom row (Settings)…
  ui.gamepadButton("dpdown", true);
  ui.pump(2);
  expect(ui.focused()!.text.includes("Load...")).toBeTruthy();
  ui.pump(framesFor(920) - 2); // repeats at ≈414/529/644ms move it the rest of the way down
  expect(ui.focused()!.text.includes("Settings")).toBeTruthy();

  // …and KEEP clamping there: 920ms more is ~8 repeat periods, none of which may wrap back to the top.
  ui.pump(framesFor(920));
  expect(ui.focused()!.text.includes("Settings")).toBeTruthy();

  // Release, then a FRESH Down press at the bottom wraps to the top row.
  ui.gamepadButton("dpdown", false);
  ui.pump(2);
  ui.gamepadButton("dpdown", true);
  ui.pump(2);
  expect(ui.focused()!.text.includes("Recent")).toBeTruthy();
  ui.gamepadButton("dpdown", false);
  ui.pump(2);

  // And a FRESH Up press at the top wraps to the bottom row.
  ui.gamepadButton("dpup", true);
  ui.pump(2);
  expect(ui.focused()!.text.includes("Settings")).toBeTruthy();
  ui.gamepadButton("dpup", false);
});
