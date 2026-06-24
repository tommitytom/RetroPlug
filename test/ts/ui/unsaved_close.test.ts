// Headless UI: the unsaved-changes modal (shown when the standalone window-close
// is vetoed) renders Save/Discard/Cancel, Cancel dismisses it, and Discard drives
// the quit path. The real onClose veto needs the DPF window (manual test); here we
// emit "confirm-close" directly and assert the modal + its button wiring.
import { test, expect, ui } from "ui-harness";

const MGB = "resources/roms/mGB.gb";

test("unsaved-changes modal: renders, Cancel dismisses, Discard quits", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(40);
  ui.loadRom(MGB);
  ui.pump(40);

  // Pop the modal (stands in for PluginUI::onClose vetoing the close).
  ui.requestCloseConfirm();
  ui.pump(20);
  expect(ui.findByTextContaining("Unsaved changes")).toBeTruthy();
  expect(ui.findByText("Save & Quit")).toBeTruthy();
  expect(ui.findByText("Discard & Quit")).toBeTruthy();
  const cancel = ui.findByText("Cancel");
  expect(cancel).toBeTruthy();

  // Cancel dismisses the modal without quitting.
  ui.clickAt(cancel!.x + (cancel!.width >> 1), cancel!.y + (cancel!.height >> 1));
  ui.pump(20);
  expect(ui.findByText("Discard & Quit")).toBeFalsy();
  expect(ui.quitRequested()).toBeFalsy();

  // Re-open and Discard → quitStandalone fires.
  ui.requestCloseConfirm();
  ui.pump(20);
  const discard = ui.findByText("Discard & Quit");
  expect(discard).toBeTruthy();
  ui.clickAt(discard!.x + (discard!.width >> 1), discard!.y + (discard!.height >> 1));
  ui.pump(20);
  expect(ui.quitRequested()).toBeTruthy();
});
