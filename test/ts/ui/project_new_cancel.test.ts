// Headless UI: the unsaved-changes prompt's Cancel is non-destructive — it
// dismisses the modal and keeps the current project (no New Project, no dialog).
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

test("New Project: Cancel keeps the current project", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(40);
  ui.loadRom(MGB);
  ui.pump(60);

  // Open the instance menu and dirty the project (cycle Link Group →
  // markProjectDirty) so New Project raises the prompt.
  ui.tapKey(Key.Esc);
  ui.pump(20);
  focusRow("Link Group");
  ui.tapKey(Key.Enter);
  ui.pump(20);

  expect(focusRow("Project >")?.text.includes("Project >")).toBeTruthy();
  ui.tapKey(Key.Enter); // expand Project
  ui.pump(10);
  expect(focusRow("New Project")?.text.includes("New Project")).toBeTruthy();
  ui.tapKey(Key.Enter); // activate → prompt
  ui.pump(30);

  expect(ui.findByTextContaining("Unsaved changes")).toBeTruthy();
  const cancel = ui.findByText("Cancel");
  expect(cancel).toBeTruthy();

  // Cancel → modal dismisses, project is untouched: still a system (a tile),
  // NOT the empty start screen, and nothing was discarded or saved.
  ui.clickAt(cancel!.x + (cancel!.width >> 1), cancel!.y + (cancel!.height >> 1));
  ui.pump(40);
  expect(ui.findByTextContaining("Unsaved changes")).toBeFalsy();
  expect(ui.findByTextContaining("Load mGB")).toBeFalsy(); // not the start screen
  expect(ui.countByType(CompType.Image)).toBeGreaterThan(0); // the tile survives
  expect(ui.browserOpenCount()).toBe(0);
});
