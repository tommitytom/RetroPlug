// The rebindable app actions, end to end on the headless display: the gamepad opens the menu with the bound
// Open Menu button (leftshoulder) and cycles the focused instance with the bound Cycle button (rightshoulder).
// Focus is read off the grid's dim overlay (present only on an UNFOCUSED tile), the same cue grid.test uses.

import { test, expect, ui, navToPad } from "ui-harness";

test("the gamepad opens the menu and cycles focus between instances (app-action bindings)", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(30);

  // Add the first instance through the start menu — all gamepad (d-pad nav + A select).
  expect(navToPad("Load mGB")).toBeTruthy();
  ui.gamepadTap("a");
  ui.pump(20);
  expect(ui.findByTestId("tile-0") != null).toBeTruthy();

  // Open the instance menu with the bound Open Menu button (leftshoulder), then Duplicate → a second tile.
  ui.gamepadTap("leftshoulder");
  ui.pump(10);
  expect(navToPad("Duplicate Instance")).toBeTruthy();
  ui.gamepadTap("a");
  ui.pump(20);
  expect(ui.findByTestId("tile-1") != null).toBeTruthy();

  // Two tiles; duplicate doesn't steal focus, so tile 0 stays focused (un-dimmed) and tile 1 is dimmed.
  expect(ui.findByTestId("dim-0")).toBe(null);
  expect(ui.findByTestId("dim-1") != null).toBeTruthy();

  // Cycle Instances (rightshoulder = CycleNext) → focus advances to tile 1.
  ui.gamepadTap("rightshoulder");
  ui.pump(10);
  expect(ui.findByTestId("dim-1")).toBe(null); // tile 1 now focused
  expect(ui.findByTestId("dim-0") != null).toBeTruthy();

  // Again → wraps back to tile 0.
  ui.gamepadTap("rightshoulder");
  ui.pump(10);
  expect(ui.findByTestId("dim-0")).toBe(null);
  expect(ui.findByTestId("dim-1") != null).toBeTruthy();

  ui.snapshotPng("/tmp/greenfield-ui-app-actions.png");
});
