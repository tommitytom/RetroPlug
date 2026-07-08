// The context-menu, end to end on the headless display: the start menu (empty project) navigates by
// arrow keys, selects an action, and the instance menu (opened with Esc over a tile) expands submenus +
// cycles values. Drives everything through the keypad the way the useFocusGroup primitive claims it —
// proving focus nav (LVGL-native), Enter/CLICKED select, Left/Right cycle, and the tile↔menu swap.

import { test, expect, ui, navTo, Key } from "ui-harness";

test("the menu navigates, selects an action, cycles a value, and expands a submenu", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(30);

  // The start-menu title carries the app version (proves the C++ Version.hpp → version() RPC → UI path,
  // since the harness runs against the real backend).
  expect(ui.findByTextContaining("RetroPlug v") != null).toBeTruthy();

  // Empty project → the start menu; its first item ("Load...") is focused on mount.
  const first = ui.focused();
  expect(first != null && first.text.includes("Load...")).toBeTruthy();

  // Arrow nav moves the LVGL focus.
  ui.tapKey(Key.Down);
  ui.pump(4);
  const second = ui.focused();
  expect(second != null && second.text !== first!.text).toBeTruthy();
  ui.tapKey(Key.Up);
  ui.pump(4);
  expect(ui.focused()!.text.includes("Load...")).toBeTruthy();

  // The embedded-synth "Load mGB" item renders in the start menu.
  expect(ui.findByTextContaining("Load mGB") != null).toBeTruthy();

  // Focus "Load mGB" and Enter → a system is added, the menu gives way to the grid.
  expect(navTo("Load mGB")).toBeTruthy();
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

  // The instance-menu title is "RetroPlug v<version> - <rom>"; for the embedded synth the ROM is "mGB"
  // and (no project name) it isn't duplicated. Match the shape, not the literal version.
  const title = ui.findByTextContaining("mGB");
  expect(title != null && /^RetroPlug v.+ - mGB$/.test(title.text)).toBeTruthy();

  // Cycle a top-level value with Right (Link Group: Off → 1).
  expect(navTo("Link Group")).toBeTruthy();
  ui.tapKey(Key.Right);
  ui.pump(6);
  expect(ui.findByTextContaining("Link Group: 1") != null).toBeTruthy();

  // Expand System — the per-instance Save/Load State + SRAM items appear inline, plus the pathless
  // New SRAM (blank battery) + Reset (reboot) reconstruct actions.
  expect(navTo("System")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(15);
  expect(ui.findByTextContaining("Save State") != null).toBeTruthy();
  expect(ui.findByTextContaining("New SRAM") != null).toBeTruthy();
  expect(ui.findByTextContaining("Reset") != null).toBeTruthy();

  // Scroll-follow (the reported bug): with System expanded the menu overflows the window, so a lower row
  // like Settings sits below the fold. Navigating to it must scroll the container to keep it visible —
  // before the fix, keyboard nav moved focus but never scrolled, leaving the selection off-screen.
  expect(navTo("Settings")).toBeTruthy();
  ui.pump(10);
  const win = ui.snapshot();
  const settingsRow = ui.findByTextContaining("Settings")!;
  expect(settingsRow.y >= 0 && settingsRow.y < win.height).toBeTruthy();

  // Expand Settings — its children appear inline.
  ui.tapKey(Key.Enter);
  ui.pump(10);
  expect(ui.findByTextContaining("Default Zoom") != null).toBeTruthy();

  ui.snapshotPng("/tmp/greenfield-ui-menu.png");
});
