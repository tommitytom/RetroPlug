// Headless UI: when the project has a known path, the prompt's "Save & Continue"
// saves silently (no dialog) and then performs the intent (here: New Project →
// the start screen). Proves the save-then-proceed branch with a path set.
import { test, expect, ui, Key } from "ui-harness";

const MGB  = "resources/roms/mGB.gb";
const PROJ = "/tmp/rp_project_save_continue.rplg";

function focusRow(substr: string, max = 32) {
  for (let i = 0; i < max; i++) {
    const f = ui.focused();
    if (f && f.text.includes(substr)) return f;
    ui.tapKey(Key.Down);
    ui.pump(5);
  }
  return ui.focused();
}

test("New Project: Save & Continue (known path) saves silently then proceeds", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(40);

  // Load through the real project path so currentProjectPath_ is set (and clean).
  ui.writeProjectJson(PROJ, MGB, 0);
  expect(ui.loadProject(PROJ)).toBeTruthy();
  ui.pump(60);
  ui.tapKey(Key.Esc); // the fresh load auto-closed the menu
  ui.pump(30);

  // Dirty it, then New Project → prompt.
  focusRow("Link Group");
  ui.tapKey(Key.Enter);
  ui.pump(20);
  expect(focusRow("Project >")?.text.includes("Project >")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(10);
  expect(focusRow("New Project")?.text.includes("New Project")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(30);

  const save = ui.findByText("Save & Continue");
  expect(save).toBeTruthy();
  expect(ui.browserOpenCount()).toBe(0);

  // Save & Continue → silent save to PROJ (no dialog), then newProject runs.
  ui.clickAt(save!.x + (save!.width >> 1), save!.y + (save!.height >> 1));
  ui.pump(80);

  // No browser was ever opened (the save was silent), the modal is gone, and
  // we proceeded to the empty start screen.
  expect(ui.browserOpenCount()).toBe(0);
  expect(ui.findByTextContaining("Unsaved changes")).toBeFalsy();
  expect(ui.findByTextContaining("Load mGB")).toBeTruthy();
});
