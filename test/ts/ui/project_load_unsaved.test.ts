// Headless UI: with unsaved changes, "Load Project" raises the save prompt
// before discarding the current project. Discard & Continue then proceeds to
// open the load browser (counted by the stubbed file-browser).
import { test, expect, ui, Key } from "ui-harness";

const MGB = "resources/roms/mGB.gb";

// Walk menu focus down to the first row whose label contains `substr` (LVGL
// auto-scrolls to keep it visible — robust to a scrolled menu).
function focusRow(substr: string, max = 30) {
  for (let i = 0; i < max; i++) {
    const f = ui.focused();
    if (f && f.text.includes(substr)) return f;
    ui.tapKey(Key.Down);
    ui.pump(4);
  }
  return ui.focused();
}

test("Load Project on a dirty project prompts to save, then Discard opens the load browser", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(40);
  ui.loadRom(MGB);
  ui.pump(60);

  // Dirty the project via a Link Group cycle (markProjectDirty).
  ui.tapKey(Key.Esc);
  ui.pump(20);
  expect(focusRow("Link Group")?.text.includes("Link Group")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(20);

  // Expand Project and activate Load Project.
  expect(focusRow("Project >")?.text.includes("Project >")).toBeTruthy();
  ui.tapKey(Key.Enter); // expand
  ui.pump(10);
  expect(focusRow("Load Project")?.text.includes("Load Project")).toBeTruthy();
  ui.tapKey(Key.Enter); // activate → unsaved-changes gate
  ui.pump(30);

  expect(ui.findByTextContaining("Unsaved changes")).toBeTruthy();
  const discard = ui.findByText("Discard & Continue");
  expect(discard).toBeTruthy();
  const before = ui.browserOpenCount();

  // Discard & Continue → the load browser opens, and the modal dismisses.
  ui.clickAt(discard!.x + (discard!.width >> 1), discard!.y + (discard!.height >> 1));
  ui.pump(30);
  expect(ui.findByText("Discard & Continue")).toBeFalsy();
  expect(ui.browserOpenCount()).toBe(before + 1);
});
