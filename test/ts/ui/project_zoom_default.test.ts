// Req 1: a freshly-created (empty) project carries raw zoom 0, so the Project >
// Zoom row must read "Zoom: Default (3x)" — distinguishing "inherit the user
// default" from an explicit "Zoom: 3x". Single case → runs on a clean boot.
//
// (Cases in one file share one booted UI — beginCase is a no-op — and the zoom
// state is awkward to reset between them, so the zoom-default coverage is split
// one-case-per-file: see project_zoom_cycle / _inherit / _explicit.)
import { test, expect, ui, Key } from "ui-harness";

function focusRowContaining(substr: string, max = 24) {
  for (let i = 0; i < max; i++) {
    const f = ui.focused();
    if (f && f.text.includes(substr)) return f;
    ui.tapKey(Key.Down);
    ui.pump(6);
  }
  const f = ui.focused();
  return f && f.text.includes(substr) ? f : null;
}

test("a fresh project shows 'Zoom: Default (3x)', not 'Zoom: 3x'", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(40); // start screen: fresh project, raw zoom 0

  // Expand the Project submenu (Enter on the focused header).
  expect(focusRowContaining("Project")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(20);

  expect(ui.findByText("Zoom: Default (3x)")).toBeTruthy();
  expect(ui.findByText("Zoom: 3x")).toBeFalsy();
});
