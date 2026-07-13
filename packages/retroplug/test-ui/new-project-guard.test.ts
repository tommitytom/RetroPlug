// New Project guards unsaved changes (the same confirm the window-close prompt uses, now menu-initiated
// via useProjectModals). With a dirty project, Project -> New Project raises the Save / Don't Save /
// Cancel overlay; Esc cancels (the project + its tile survive), Don't Save discards back to the start
// menu. Drives the real menu on the headless display, like menu.test.ts / close-guard.test.ts.

import { test, expect, ui, navTo, Key } from "ui-harness";

// From the grid: open the instance menu and select the top-level New Project — which (project dirty)
// raises the discard guard instead of discarding.
function selectNewProject(): void {
  ui.tapKey(Key.Esc); // instance menu over the focused tile
  ui.pump(10);
  expect(navTo("New Project")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(10);
}

test("New Project prompts on unsaved changes; Esc keeps, Don't Save discards", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(30);

  // Dirty the project: Load mGB adds a tile (proven dirty by close-guard.test.ts).
  expect(navTo("Load mGB")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(20);
  expect(ui.findByTestId("tile-0") != null).toBeTruthy();

  // The guard overlay appears with all three choices.
  selectNewProject();
  expect(ui.findByTextContaining("Unsaved changes") != null).toBeTruthy();
  expect(ui.findByTextContaining("Don't Save") != null).toBeTruthy();
  expect(ui.findByTextContaining("Cancel") != null).toBeTruthy();

  // Esc cancels → the project + its tile survive.
  ui.tapKey(Key.Esc);
  ui.pump(10);
  expect(ui.findByTextContaining("Unsaved changes")).toBe(null);
  expect(ui.findByTestId("tile-0") != null).toBeTruthy();

  // Re-open and choose Don't Save (arrow to it, Enter) → the project is discarded, back to the start menu.
  selectNewProject();
  expect(navTo("Don't Save")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(20);
  expect(ui.findByTextContaining("Unsaved changes")).toBe(null);
  expect(ui.findByTestId("tile-0")).toBe(null); // discarded
  expect(ui.findByTextContaining("Load mGB") != null).toBeTruthy(); // the start menu is back
});
