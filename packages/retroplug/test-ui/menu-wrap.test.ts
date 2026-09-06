// Menu edge-wrap on a FRESH key press: a held arrow's OS repeats stop at the list's end (the edge is a
// wall during the hold), but the first press after a release at the edge wraps to the far end — Up on the
// top row jumps to the bottom, Down on the bottom row jumps to the top. Each harness tapKey is a complete
// press+release, so every tap here is a fresh press. The start menu (headless, non-standalone) is:
// Recent, Load..., Load mGB (GB MIDI Synth), [sep], Project, Settings.

import { test, expect, ui, Key } from "ui-harness";

test("a fresh Up/Down press at the menu edge wraps to the other side", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(30);
  expect(ui.focused()!.text.includes("Recent")).toBeTruthy();

  // Top row + Up (fresh press) → wraps to the bottom row.
  ui.tapKey(Key.Up);
  ui.pump(2);
  expect(ui.focused()!.text.includes("Settings")).toBeTruthy();

  // Bottom row + Down (fresh press) → wraps back to the top row.
  ui.tapKey(Key.Down);
  ui.pump(2);
  expect(ui.focused()!.text.includes("Recent")).toBeTruthy();

  // Not at an edge, a fresh press just moves (the wrap is edge-only).
  ui.tapKey(Key.Down);
  ui.pump(2);
  expect(ui.focused()!.text.includes("Load...")).toBeTruthy();
});
