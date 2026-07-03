// Headless UI: the Settings submenu exposes the global "SRAM Mirror" cycle (Off /
// On Save / Continuous), moved there from the Project menu. The UI test harness
// wires no UserConfig, so the preference reads as its default (On Save); we assert
// the row renders, not the round-trip. Run: pnpm test:ui menu_srammirror
import { test, expect, ui } from "ui-harness";

test("the Settings submenu shows the SRAM Mirror cycle", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(40); // start screen: the menu is already open

  // Expand the Settings submenu (its header renders as "Settings >").
  const settings = ui.findByTextContaining("Settings >");
  expect(settings).toBeTruthy();
  ui.clickAt(settings!.x + (settings!.width >> 1), settings!.y + (settings!.height >> 1));
  ui.pump(20);

  // Default (no UserConfig wired) is OnProjectSave -> "On Save".
  expect(ui.findByText("SRAM Mirror: On Save")).toBeTruthy();
});
