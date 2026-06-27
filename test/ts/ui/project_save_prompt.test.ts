// Headless UI: the menu's "Save Project" falls back to the Save dialog when the
// project has no known path yet (the saveProject RPC returns false → browser).
import { test, expect, ui, Key } from "ui-harness";

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

test("Save Project with no known path opens the save dialog", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(40);
  ui.loadRom(MGB); // adopts directly — no currentProjectPath_
  ui.pump(60);

  ui.tapKey(Key.Esc);
  ui.pump(20);
  expect(focusRow("Project >")?.text.includes("Project >")).toBeTruthy();
  ui.tapKey(Key.Enter); // expand Project
  ui.pump(10);
  expect(focusRow("Save Project")?.text.includes("Save Project")).toBeTruthy();
  ui.tapKey(Key.Enter); // activate → no path → fall back to the dialog
  ui.pump(30);

  expect(ui.browserOpenCount()).toBe(1);
});
