// Headless UI: "Save Project As..." always opens the save dialog (unlike "Save
// Project", which saves silently to the project's known path). Proven via the
// stubbed file-browser counter.
import { test, expect, ui, Key } from "ui-harness";

const MGB = "resources/roms/mGB.gb"; // repo-relative (runner cwd = repo root)

test("Project > Save Project As... opens the save dialog", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(40);

  // Save / Export rows are hidden on the empty start screen — load a ROM first.
  ui.loadRom(MGB);
  ui.pump(60);

  ui.tapKey(Key.Esc); // open the instance menu
  ui.pump(30);
  const projHeader = ui.findByTextContaining("Project >");
  expect(projHeader).toBeTruthy();
  ui.clickAt(projHeader!.x + (projHeader!.width >> 1), projHeader!.y + (projHeader!.height >> 1));
  ui.pump(20);

  // The save-related rows sit together.
  expect(ui.findByText("Save Project")).toBeTruthy();
  expect(ui.findByText("Save Project As...")).toBeTruthy();
  expect(ui.findByText("Export Zip")).toBeTruthy();

  const saveAs = ui.findByText("Save Project As...");
  expect(ui.browserOpenCount()).toBe(0);
  ui.clickAt(saveAs!.x + (saveAs!.width >> 1), saveAs!.y + (saveAs!.height >> 1));
  ui.pump(30);
  expect(ui.browserOpenCount()).toBe(1); // the Save-As dialog opened
});
