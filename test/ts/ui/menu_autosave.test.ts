// Headless UI: the Settings submenu exposes the global "Auto Save" toggle (moved
// there from the Project menu). The UI test harness wires no UserConfig, so the
// preference reads as its default (Off); we assert the row renders, not the
// round-trip. Run: pnpm test:ui menu_autosave
import { test, expect, ui } from "ui-harness";

test("the Settings submenu shows the Auto Save toggle", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(40); // start screen: the menu is already open

  // Expand the Settings submenu (its header renders as "Settings >").
  const settings = ui.findByTextContaining("Settings >");
  expect(settings).toBeTruthy();
  ui.clickAt(settings!.x + (settings!.width >> 1), settings!.y + (settings!.height >> 1));
  ui.pump(20);

  expect(ui.findByText("Auto Save: Off")).toBeTruthy();
});
