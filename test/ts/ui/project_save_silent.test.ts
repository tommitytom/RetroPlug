// Headless UI: the menu's "Save Project" (not "Save Project As...") writes to
// the project's known path silently — no file dialog.
import { test, expect, ui, Key } from "ui-harness";

const MGB  = "resources/roms/mGB.gb";
const PROJ = "/tmp/rp_project_save_silent.rplg";

function focusRow(substr: string, max = 32) {
  for (let i = 0; i < max; i++) {
    const f = ui.focused();
    if (f && f.text.includes(substr)) return f;
    ui.tapKey(Key.Down);
    ui.pump(5);
  }
  return ui.focused();
}

test("Save Project writes silently to the known path (no dialog)", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(40);
  ui.writeProjectJson(PROJ, MGB, 0);
  expect(ui.loadProject(PROJ)).toBeTruthy(); // sets currentProjectPath_
  ui.pump(60);

  ui.tapKey(Key.Esc);
  ui.pump(30);
  expect(focusRow("Project >")?.text.includes("Project >")).toBeTruthy();
  ui.tapKey(Key.Enter); // expand Project
  ui.pump(10);
  // "Save Project" sits before "Save Project As..." — focusRow stops at the
  // first match walking down from the Project header.
  expect(focusRow("Save Project")?.text.includes("Save Project")).toBeTruthy();
  ui.tapKey(Key.Enter); // activate → silent save to PROJ
  ui.pump(30);

  expect(ui.browserOpenCount()).toBe(0); // no dialog — saved to the known path
});
