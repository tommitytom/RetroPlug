// Headless UI regression: a project's saved zoom must survive being loaded and
// applied. The bug was in the LoadProject apply (PluginDSP::applyProjectFromConfig
// and its harness mirror), which reset project.config() to defaults and dropped
// the loaded settings — so a project saved at a non-default zoom came back at the
// default 3. The harness pump() drains and applies the LoadProject command, so
// this exercises the real apply path.
//
// Zoom 2 is used (not a larger value) because the headless display is fixed at
// 480x432 = exactly zoom 3; a larger zoom would render the menu past the display
// edge and break coordinate-based clicks. Zoom 2 (320x288) fits and is distinct
// from the default 3.
import { test, expect, ui, Key } from "ui-harness";

const MGB  = "resources/roms/mGB.gb"; // repo-relative (runner cwd = repo root)
const PROJ = "/tmp/rp_project_zoom.rplg";

test("a loaded project's explicit zoom survives the apply (regression)", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(40);

  // Thin project pointing at a real ROM, with an explicit project zoom of 2.
  ui.writeProjectJson(PROJ, MGB, 2);
  expect(ui.loadProject(PROJ)).toBeTruthy();
  ui.pump(60); // applies the LoadProject command -> apply-from-config

  // Open the instance menu (Esc toggles it) and expand the Project submenu.
  ui.tapKey(Key.Esc);
  ui.pump(30);
  const projHeader = ui.findByTextContaining("Project >");
  expect(projHeader).toBeTruthy();
  ui.clickAt(projHeader!.x + (projHeader!.width >> 1), projHeader!.y + (projHeader!.height >> 1));
  ui.pump(20);

  // The Zoom row reflects the resolved project zoom. Before the fix this read
  // "Zoom: 3x" (loaded settings discarded on apply).
  expect(ui.findByText("Zoom: 2x")).toBeTruthy();
  expect(ui.findByText("Zoom: 3x")).toBeFalsy();
});
