// Headless UI: the Project submenu's "New Project" discards the current systems
// for an empty default project, dropping back to the start screen. It opens no
// file browser (distinguishing it from Load Project).
import { test, expect, ui, Key } from "ui-harness";

const MGB = "resources/roms/mGB.gb"; // repo-relative (runner cwd = repo root)

test("Project > New Project clears systems back to the start screen", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(40);

  // Load a ROM so the project is non-empty (New / Save / Export are hidden on
  // the empty start screen) and the menu auto-closes onto the tile. The
  // start-only "Load mGB..." item is then gone.
  ui.loadRom(MGB);
  ui.pump(60);
  expect(ui.findByTextContaining("Load mGB")).toBe(null);

  // Open the instance menu, expand Project, activate New Project.
  ui.tapKey(Key.Esc);
  ui.pump(30);
  const projHeader = ui.findByTextContaining("Project >");
  expect(projHeader).toBeTruthy();
  ui.clickAt(projHeader!.x + (projHeader!.width >> 1), projHeader!.y + (projHeader!.height >> 1));
  ui.pump(20);

  const newItem = ui.findByText("New Project");
  expect(newItem).toBeTruthy();
  expect(ui.browserOpenCount()).toBe(0);
  ui.clickAt(newItem!.x + (newItem!.width >> 1), newItem!.y + (newItem!.height >> 1));
  ui.pump(60);

  // Back to the start screen: its start-only "Load mGB..." item is present
  // again, and no file browser was ever opened.
  expect(ui.findByTextContaining("Load mGB")).toBeTruthy();
  expect(ui.browserOpenCount()).toBe(0);
});
