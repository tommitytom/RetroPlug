// Headless UI: the instance menu's "Remove Instance" drops the focused system;
// removing the only system returns to the empty start screen.
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

test("Remove Instance drops the only system back to the start screen", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(40);
  ui.loadRom(MGB);
  ui.pump(60);
  expect(ui.countByType(CompType.Image)).toBeGreaterThan(0);

  ui.tapKey(Key.Esc);
  ui.pump(20);
  expect(focusRow("Remove Instance")?.text.includes("Remove Instance")).toBeTruthy();
  ui.tapKey(Key.Enter); // removeSystem → RemoveSystem → 0 systems
  ui.pump(60);

  // Back to the empty start screen (its start-only mGB item is present again),
  // and the tile is gone.
  expect(ui.findByTextContaining("Load mGB")).toBeTruthy();
  expect(ui.countByType(CompType.Image)).toBe(0);
});
