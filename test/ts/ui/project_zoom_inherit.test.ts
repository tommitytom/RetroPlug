// Req 3 (load) + Req 2 (live): a project saved at the default zoom round-trips as
// raw 0 (resolving to the user default), and when the user default changes in
// Settings the project's resolved zoom follows immediately. One fresh 0->1 load.
import { test, expect, ui, Key } from "ui-harness";

const MGB  = "resources/roms/mGB.gb";
const PROJ = "/tmp/rp_project_zoom_inherit.rplg";

function focusRowContaining(substr: string, max = 32) {
  for (let i = 0; i < max; i++) {
    const f = ui.focused();
    if (f && f.text.includes(substr)) return f;
    ui.tapKey(Key.Down);
    ui.pump(6);
  }
  const f = ui.focused();
  return f && f.text.includes(substr) ? f : null;
}

function expandSubmenu(name: string) {
  const header = focusRowContaining(name);
  expect(header).toBeTruthy();
  if (!header!.text.trim().endsWith("v")) {
    ui.tapKey(Key.Enter);
    ui.pump(20);
  }
}

test("a default-zoom project loads as Default and follows the user default live", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(40);

  ui.writeProjectJson(PROJ, MGB, 0); // raw 0 = inherit default
  expect(ui.loadProject(PROJ)).toBeTruthy();
  ui.pump(60);
  ui.tapKey(Key.Esc); // open the instance menu (fresh 0->1 load closed it)
  ui.pump(30);

  // Req 3: loaded raw-0 project resolves to the user default (3x), shown as Default.
  expandSubmenu("Project");
  expect(ui.findByText("Zoom: Default (3x)")).toBeTruthy();
  expect(ui.findByText("Zoom: 3x")).toBeFalsy();

  // Req 2: cycle the global Default Zoom 3 -> 2; the project follows immediately.
  expandSubmenu("Settings");
  const dz = focusRowContaining("Default Zoom:");
  expect(dz!.text).toBe("Default Zoom: 3x");
  ui.tapKey(Key.Left);
  ui.pump(60); // setDefaultZoom -> "user-config-changed" -> refetch -> re-derive
  expect(ui.focused()!.text).toBe("Default Zoom: 2x");

  expect(ui.findByText("Zoom: Default (2x)")).toBeTruthy();
  expect(ui.findByText("Zoom: Default (3x)")).toBeFalsy();
});
