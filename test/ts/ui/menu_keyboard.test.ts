// Headless UI: keyboard-driven menu navigation (the primary input). Uses the
// LVGL keypad indev + the menu's focus group; ui.focused() reads the focused
// item so navigation is deterministic (press Down until the target is focused).
//
// These cases stay on the StartScreen menu (no panel/submenu mutation), so they
// share the process safely. Keyboard flows that open the AboutPanel or expand a
// submenu reassign/perturb the keypad group, so they get their own files
// (process isolation): menu_kbd_about.test.ts and menu_kbd_submenu.test.ts.
import { test, expect, ui, Key } from "ui-harness";

test("an item is focused on the StartScreen", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(40);
  const f = ui.focused();
  expect(f).toBeTruthy();
  expect(f!.text.length).toBeGreaterThan(0);
});

test("Down then Up returns focus to the same item", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(40);

  const first = ui.focused();
  expect(first).toBeTruthy();
  ui.tapKey(Key.Down);
  ui.pump(8);
  const second = ui.focused();
  expect(second).toBeTruthy();
  expect(second!.text === first!.text).toBeFalsy(); // focus actually moved
  ui.tapKey(Key.Up);
  ui.pump(8);
  expect(ui.focused()!.text).toBe(first!.text);      // and came back
});

test("Down cycles through several distinct items", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(40);

  const seen = new Set<string>();
  for (let i = 0; i < 5; i++) {
    const f = ui.focused();
    if (f) seen.add(f.text);
    ui.tapKey(Key.Down);
    ui.pump(6);
  }
  // The StartScreen has several focusable items (Load / Recent / Project /
  // Settings / About), so Down should visit more than one.
  expect(seen.size).toBeGreaterThan(1);
});
