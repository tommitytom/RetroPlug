// Req 3 (storage): the Project > Zoom row treats Default(0) as a real cycle slot
// (0 → 1 → … → 6 → 0), so a project can be put back on the user default (stored
// as raw 0). One case per file (shared-UI model) → a single fresh 0→1 load.
import { test, expect, ui, Key } from "ui-harness";

const MGB  = "resources/roms/mGB.gb";
const PROJ = "/tmp/rp_project_zoom_cycle.rplg";

function focusRowContaining(substr: string, max = 28) {
  for (let i = 0; i < max; i++) {
    const f = ui.focused();
    if (f && f.text.includes(substr)) return f;
    ui.tapKey(Key.Down);
    ui.pump(6);
  }
  const f = ui.focused();
  return f && f.text.includes(substr) ? f : null;
}

test("the Zoom row cycles Default -> explicit -> back to Default", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(40);

  // Fresh 0->1 load auto-closes the menu; Esc opens the instance menu.
  ui.writeProjectJson(PROJ, MGB, 0); // raw zoom 0 = inherit default
  expect(ui.loadProject(PROJ)).toBeTruthy();
  ui.pump(60);
  ui.tapKey(Key.Esc);
  ui.pump(30);

  expect(focusRowContaining("Project")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(20);

  const row = focusRowContaining("Zoom:");
  expect(row!.text).toBe("Zoom: Default (3x)");

  ui.tapKey(Key.Right); // Default(0) -> 1x; focus stays on the row
  ui.pump(30);
  expect(ui.focused()!.text).toBe("Zoom: 1x");

  ui.tapKey(Key.Left); // 1x -> Default(0): 0 round-trips
  ui.pump(30);
  expect(ui.focused()!.text).toBe("Zoom: Default (3x)");

  ui.tapKey(Key.Left); // Default(0) wraps to the top value 6x
  ui.pump(30);
  expect(ui.focused()!.text).toBe("Zoom: 6x");
});
