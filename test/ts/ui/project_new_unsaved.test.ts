// Headless UI: with unsaved changes, "New Project" raises the save prompt — the
// same modal the window-close uses — before discarding. Discard & Continue then
// proceeds, clearing the project back to the start screen.
import { test, expect, ui, Key } from "ui-harness";

const MGB = "resources/roms/mGB.gb";

// Walk menu focus down to the first row whose label contains `substr`. LVGL
// auto-scrolls to keep the focused row visible, so this is robust to a scrolled
// menu (unlike clickAt on a possibly off-screen row).
function focusRow(substr: string, max = 30) {
  for (let i = 0; i < max; i++) {
    const f = ui.focused();
    if (f && f.text.includes(substr)) return f;
    ui.tapKey(Key.Down);
    ui.pump(4);
  }
  return ui.focused();
}

test("New Project on a dirty project prompts to save, then Discard clears it", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(40);
  ui.loadRom(MGB);
  ui.pump(60);

  // Open the instance menu and make a project edit (cycle Link Group →
  // markProjectDirty) so there are unsaved changes to guard.
  ui.tapKey(Key.Esc);
  ui.pump(20);
  expect(focusRow("Link Group")?.text.includes("Link Group")).toBeTruthy();
  ui.tapKey(Key.Enter); // cycle → setLinkGroupId → dirty (keepOpen)
  ui.pump(20);

  // Expand Project and activate New Project.
  expect(focusRow("Project >")?.text.includes("Project >")).toBeTruthy();
  ui.tapKey(Key.Enter); // expand
  ui.pump(10);
  expect(focusRow("New Project")?.text.includes("New Project")).toBeTruthy();
  ui.tapKey(Key.Enter); // activate → unsaved-changes gate
  ui.pump(30);

  // The save prompt is up (dirty project), labelled for "Continue".
  expect(ui.findByTextContaining("Unsaved changes")).toBeTruthy();
  expect(ui.findByText("Save & Continue")).toBeTruthy();
  const discard = ui.findByText("Discard & Continue");
  expect(discard).toBeTruthy();
  expect(ui.browserOpenCount()).toBe(0);

  // Discard & Continue → newProject runs; back to the start screen, no dialog.
  ui.clickAt(discard!.x + (discard!.width >> 1), discard!.y + (discard!.height >> 1));
  ui.pump(60);
  expect(ui.findByText("Discard & Continue")).toBeFalsy();
  expect(ui.findByTextContaining("Load mGB")).toBeTruthy();
  expect(ui.browserOpenCount()).toBe(0);
});
