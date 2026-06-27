// Headless UI: the instance menu's "Duplicate Instance" clones the focused
// system (no browser) — a second tile appears.
import { test, expect, ui, Key, CompType } from "ui-harness";

const MGB = "resources/roms/mGB.gb";

function focusRow(substr: string, max = 32) {
  for (let i = 0; i < max; i++) {
    const f = ui.focused();
    if (f && f.text.includes(substr)) return f;
    ui.tapKey(Key.Down);
    ui.pump(5);
  }
  return ui.focused();
}

test("Duplicate Instance clones the focused system into a second tile", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(40);
  ui.loadRom(MGB);
  ui.pump(60);
  const before = ui.countByType(CompType.Image);

  ui.tapKey(Key.Esc);
  ui.pump(20);
  expect(focusRow("Duplicate Instance")?.text.includes("Duplicate Instance")).toBeTruthy();
  ui.tapKey(Key.Enter); // duplicateSystem → AddSystem(clone), no browser
  ui.pump(60);

  expect(ui.browserOpenCount()).toBe(0); // duplicate needs no file dialog
  expect(ui.countByType(CompType.Image)).toBe(before + 1);
});
