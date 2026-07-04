// Headless UI: the instance menu's "Add Instance" opens the ROM browser and,
// once a ROM is chosen, adds a second system (a second tile).
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

test("Add Instance adds a second tile via the ROM browser", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(40);
  ui.loadRom(MGB);
  ui.pump(60);
  const before = ui.countByType(CompType.Image);

  ui.tapKey(Key.Esc); // open the instance menu
  ui.pump(20);
  expect(focusRow("Add Instance")?.text.includes("Add Instance")).toBeTruthy();
  ui.tapKey(Key.Enter); // openRomBrowser(add)
  ui.pump(20);
  expect(ui.browserOpenCount()).toBe(1);

  ui.selectFile(MGB); // → rom-path-selected → constructSystem(add) → AddSystem
  ui.pump(60);

  expect(ui.countByType(CompType.Image)).toBe(before + 1); // a second tile
});
