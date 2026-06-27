// Headless UI: when the project has NO saved path, the prompt's "Save & Continue"
// opens the Save dialog (it can't save silently). It then waits for the save to
// land before performing the intent — so headlessly (no file chosen) the modal
// stays up and the project is not discarded.
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

test("New Project: Save & Continue with no saved path opens the save dialog", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(40);
  // loadRom adopts the system directly — it sets no currentProjectPath_.
  ui.loadRom(MGB);
  ui.pump(60);

  ui.tapKey(Key.Esc);
  ui.pump(20);
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

  ui.clickAt(save!.x + (save!.width >> 1), save!.y + (save!.height >> 1));
  ui.pump(30);

  // No path → the Save dialog opened; with no file chosen the intent does not
  // proceed, so the modal stays up and nothing was discarded.
  expect(ui.browserOpenCount()).toBe(1);
  expect(ui.findByTextContaining("Unsaved changes")).toBeTruthy();
  expect(ui.findByTextContaining("Load mGB")).toBeFalsy(); // not the start screen
});
