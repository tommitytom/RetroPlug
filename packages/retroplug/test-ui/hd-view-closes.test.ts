// The HD view closes when its system is removed (docs/modular-graph-phase-f4c-design section 8.7): a window
// view is opened for a system's id, and that id going away (here a project load rebuilds the session) must return the
// UI to the grid rather than strand it on a screen with nothing to draw. SKIPs when no LSDj ROM was staged.
import { test, expect, skip, ui, navTo, Key } from "ui-harness";

const LSDJ = () => ui.romDir() + "/lsdj9_4_2.gb";

test("the HD view closes when its system is removed", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(30);
  ui.fileDrop(LSDJ(), 0, 0);
  ui.pump(30);
  if (ui.findByTestId("tile-0") == null) skip("hd-view-closes: no LSDj ROM staged in romDir");
  ui.advance(1500);
  ui.pump(10);

  // Open it the way the menu does: an LSDj ROM auto-attaches the sync role, so the submenu has HD Player.
  ui.tapKey(Key.Esc);
  ui.pump(10);
  expect(navTo("LSDj")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(10);
  expect(navTo("HD Player")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(20);
  ui.advance(300);
  ui.pump(10);
  expect(ui.findByTestId("lsdj-hd") != null).toBeTruthy();

  // A load rebuilds the project under a new system id: the open view's system is gone, so the grid returns. (In a
  // single-instance project a dropped ROM is the guarded "Load...", the same route the menu takes, so the unsaved
  // project asks first; Discard is the load.)
  ui.fileDrop(LSDJ(), 0, 0);
  ui.pump(40);
  expect(navTo("Don't Save")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(40);
  expect(ui.findByTestId("lsdj-hd")).toBe(null);
  expect(ui.findByTestId("tile-0") != null).toBeTruthy();
});
