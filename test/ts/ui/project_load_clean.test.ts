// Headless UI: on a clean project, "Load Project" skips the unsaved-changes
// prompt and opens the load browser directly (the gate only fires when dirty).
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

test("Load Project on a clean project opens the browser without prompting", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(40);
  ui.loadRom(MGB); // clean: loadRom does not mark the project dirty
  ui.pump(60);

  ui.tapKey(Key.Esc);
  ui.pump(20);
  expect(focusRow("Project >")?.text.includes("Project >")).toBeTruthy();
  ui.tapKey(Key.Enter); // expand Project
  ui.pump(10);
  expect(focusRow("Load Project")?.text.includes("Load Project")).toBeTruthy();
  ui.tapKey(Key.Enter); // activate
  ui.pump(30);

  // Clean → no prompt, the load browser opened straight away.
  expect(ui.findByTextContaining("Unsaved changes")).toBeFalsy();
  expect(ui.browserOpenCount()).toBe(1);
});
