// The context-menu, end to end on the headless display: the start menu (empty project) navigates by
// arrow keys, selects an action, and the instance menu (opened with Esc over a tile) expands submenus +
// cycles values. Drives everything through the keypad the way the useFocusGroup primitive claims it —
// proving focus nav (LVGL-native), Enter/CLICKED select, Left/Right cycle, and the tile↔menu swap.

import { test, expect, ui, Key } from "ui-harness";

// Tap Down until the focused row's label contains `substr` (robust to exact item ordering).
function navTo(substr: string, maxSteps = 24): boolean {
  for (let i = 0; i < maxSteps; i++) {
    const f = ui.focused();
    if (f && f.text.includes(substr)) return true;
    ui.tapKey(Key.Down);
    ui.pump(2);
  }
  const f = ui.focused();
  return !!f && f.text.includes(substr);
}

test("the menu navigates, selects an action, cycles a value, and expands a submenu", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(30);

  // Empty project → the start menu; its first item is focused on mount.
  const first = ui.focused();
  expect(first != null && first.text.includes("Load mGB")).toBeTruthy();

  // Arrow nav moves the LVGL focus.
  ui.tapKey(Key.Down);
  ui.pump(4);
  const second = ui.focused();
  expect(second != null && second.text !== first!.text).toBeTruthy();
  ui.tapKey(Key.Up);
  ui.pump(4);
  expect(ui.focused()!.text.includes("Load mGB")).toBeTruthy();

  // The file-browser "Load..." item renders in the start menu (its browse resolves null in the harness).
  expect(ui.findByTextContaining("Load...") != null).toBeTruthy();

  // Enter on "Load mGB" → a system is added, the menu gives way to the grid.
  ui.tapKey(Key.Enter);
  ui.pump(20);
  expect(ui.findByTestId("tile-0") != null).toBeTruthy();

  // Esc opens the instance menu over the focused tile.
  ui.tapKey(Key.Esc);
  ui.pump(10);
  expect(ui.findByTextContaining("Duplicate Instance") != null).toBeTruthy();
  // The file-browser lifecycle items render alongside it.
  expect(ui.findByTextContaining("Load ROM") != null).toBeTruthy();
  expect(ui.findByTextContaining("Add Instance") != null).toBeTruthy();

  // Cycle a top-level value with Right (Link Group: Off → 1).
  expect(navTo("Link Group")).toBeTruthy();
  ui.tapKey(Key.Right);
  ui.pump(6);
  expect(ui.findByTextContaining("Link Group: 1") != null).toBeTruthy();

  // Expand System — the per-instance Save/Load State + SRAM items appear inline.
  expect(navTo("System")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(15);
  expect(ui.findByTextContaining("Save State") != null).toBeTruthy();

  // Expand a submenu (Settings) — its children appear inline.
  expect(navTo("Settings")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(10);
  expect(ui.findByTextContaining("Default Zoom") != null).toBeTruthy();

  ui.snapshotPng("/tmp/greenfield-ui-menu.png");
});
