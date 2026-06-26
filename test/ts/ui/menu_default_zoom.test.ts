// Headless UI: the Settings submenu exposes a global "Default Zoom" cycler
// (UserConfig::defaultZoom). Mirrors menu_autosave.test.ts. The harness wires no
// UserConfig, so getUserConfig() returns the baked-in default of 3x.
import { test, expect, ui } from "ui-harness";

test("the Settings submenu shows the Default Zoom cycler", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(40); // start screen: the menu is already open

  // Expand the Settings submenu (its header renders as "Settings >").
  const settings = ui.findByTextContaining("Settings >");
  expect(settings).toBeTruthy();
  ui.clickAt(settings!.x + (settings!.width >> 1), settings!.y + (settings!.height >> 1));
  ui.pump(20);

  expect(ui.findByText("Default Zoom: 3x")).toBeTruthy();
});
