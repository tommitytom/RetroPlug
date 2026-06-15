// Headless UI: the Project submenu exposes "Export Zip" alongside "Save Project"
// (the default JSON save) and "Load Project".
import { test, expect, ui, Key } from "ui-harness";

const MGB = "resources/roms/mGB.gb"; // repo-relative (runner cwd = repo root)

test("the Project submenu lists Export Zip next to Save Project", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(40);

  // Save / Export are hidden on the empty start screen — load a ROM first.
  ui.loadRom(MGB);
  ui.pump(60);

  ui.tapKey(Key.Esc); // open the menu
  ui.pump(30);

  const projHeader = ui.findByTextContaining("Project >");
  expect(projHeader).toBeTruthy();
  ui.clickAt(projHeader!.x + (projHeader!.width >> 1), projHeader!.y + (projHeader!.height >> 1));
  ui.pump(20);

  expect(ui.findByText("Save Project")).toBeTruthy();
  expect(ui.findByText("Export Zip")).toBeTruthy();
  expect(ui.findByText("Load Project")).toBeTruthy();
});
