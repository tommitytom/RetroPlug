// Headless UI: the System submenu exposes "Load SRAM..." alongside the Save
// SRAM entries (regression: Load SRAM was missing entirely).
import { test, expect, ui, Key } from "ui-harness";

const MGB = "resources/roms/mGB.gb"; // repo-relative (runner cwd = repo root)

test("the System submenu lists Load SRAM... next to Save SRAM", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(40);

  ui.loadRom(MGB);
  ui.pump(60); // mount the tile; the menu auto-closes

  // Open the instance menu anchored to the focused tile (Esc toggles it).
  ui.tapKey(Key.Esc);
  ui.pump(30);

  // Expand the System submenu -> its children render inline. The submenu
  // header renders as "System >" (collapsed); the menu title also contains
  // "System", so match the header's trailing marker specifically.
  const sysHeader = ui.findByTextContaining("System >");
  expect(sysHeader).toBeTruthy();
  ui.clickAt(sysHeader!.x + (sysHeader!.width >> 1), sysHeader!.y + (sysHeader!.height >> 1));
  ui.pump(20);

  expect(ui.findByText("Save SRAM")).toBeTruthy();
  expect(ui.findByText("Save SRAM As...")).toBeTruthy();
  expect(ui.findByText("Load SRAM...")).toBeTruthy();
});
