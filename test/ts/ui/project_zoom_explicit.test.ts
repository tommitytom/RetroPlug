// Req 2 (control): an explicit-zoom project must NOT follow user-default changes
// — guards the derivation's "only inherit when raw zoom is 0" condition. One
// fresh 0->1 load of a project with explicit zoom 2.
import { test, expect, ui, Key } from "ui-harness";

const MGB  = "resources/roms/mGB.gb";
const PROJ = "/tmp/rp_project_zoom_explicit.rplg";

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

test("an explicit-zoom project ignores default-zoom changes", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(40);

  ui.writeProjectJson(PROJ, MGB, 2); // explicit zoom 2 (not default)
  expect(ui.loadProject(PROJ)).toBeTruthy();
  ui.pump(60);
  ui.tapKey(Key.Esc);
  ui.pump(30);

  expandSubmenu("Project");
  expect(ui.findByText("Zoom: 2x")).toBeTruthy();

  // Bump the global default 3 -> 4; the explicit project must not move.
  expandSubmenu("Settings");
  expect(focusRowContaining("Default Zoom:")).toBeTruthy();
  ui.tapKey(Key.Right);
  ui.pump(60);
  expect(ui.focused()!.text).toBe("Default Zoom: 4x");

  expect(ui.findByText("Zoom: 2x")).toBeTruthy();               // still explicit 2x
  expect(ui.findByTextContaining("Zoom: Default")).toBeFalsy(); // not inheriting
});
