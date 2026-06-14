// Headless UI: the Fast Boot row is a boolean toggle that must respond to the
// Left/Right arrows like every other cyclable row (Model, Highpass, …), not
// only to Enter. Regression: it shipped with onSelect but no onCycle, so
// horizontal arrows were a no-op on it.
import { test, expect, ui, Key } from "ui-harness";

const MGB = "resources/roms/mGB.gb"; // repo-relative (runner cwd = repo root)

// Walk the focus down the menu until the focused row's text contains `substr`.
// Returns the focused WidgetInfo, or null if not reached within `max` steps.
function focusRowContaining(substr: string, max = 24) {
  for (let i = 0; i < max; i++) {
    const f = ui.focused();
    if (f && f.text.includes(substr)) return f;
    ui.tapKey(Key.Down);
    ui.pump(6);
  }
  const f = ui.focused();
  return f && f.text.includes(substr) ? f : null;
}

test("Right/Left toggles the Fast Boot row in place", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(40);

  ui.loadRom(MGB);
  ui.pump(60); // mount the tile; the menu auto-closes

  // Open the instance menu anchored to the focused tile (Esc toggles it).
  ui.tapKey(Key.Esc);
  ui.pump(30);

  // Fast Boot lives inside the System submenu — expand it first.
  const sysHeader = ui.findByTextContaining("System >");
  expect(sysHeader).toBeTruthy();
  ui.clickAt(sysHeader!.x + (sysHeader!.width >> 1), sysHeader!.y + (sysHeader!.height >> 1));
  ui.pump(20);

  const row = focusRowContaining("Fast Boot:");
  expect(row).toBeTruthy();
  const before = row!.text; // "Fast Boot: On" or "Fast Boot: Off"

  // Right flips it...
  ui.tapKey(Key.Right);
  ui.pump(30);
  const afterRight = ui.focused();
  expect(afterRight).toBeTruthy();
  expect(afterRight!.text.includes("Fast Boot:")).toBeTruthy(); // focus stayed on the row
  expect(afterRight!.text === before).toBeFalsy();              // label actually changed

  // ...and Left flips it back (boolean toggle: either direction flips).
  ui.tapKey(Key.Left);
  ui.pump(30);
  expect(ui.focused()!.text).toBe(before);
});
