// A full-window overlay owns ALL input, including the mouse. App answers "is an overlay up" once, from the
// overlay list; before that it was a hand-written chain per handler, and the right-click handler's copy left
// out the in-app file browser - so a right-click while a browse was open quietly opened the instance menu
// underneath it, which then appeared the moment the browse closed. The headless harness installs no native
// file dialog, so "Load..." opens the in-app browser here, exactly as on a helper-less host.

import { test, expect, ui, navTo, Key } from "ui-harness";

test("a right-click under the in-app file browser does nothing, and cancelling it returns to the bare grid", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(30);
  expect(navTo("Load mGB")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(20);
  const tile = ui.findByTestId("tile-0")!;
  expect(tile != null).toBeTruthy();
  const x = tile.x + Math.floor(tile.width / 2);
  const y = tile.y + Math.floor(tile.height / 2);

  // Instance menu → Load... raises the browser, which replaces the grid.
  ui.tapKey(Key.Esc);
  ui.pump(10);
  expect(navTo("Load...")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(20);
  expect(ui.findByTestId("tile-0")).toBe(null);

  // Right-click where the tile was, then cancel the browse.
  ui.rightClick(x, y);
  ui.pump(10);
  ui.tapKey(Key.Esc);
  ui.pump(20);

  // Back on the grid with NO instance menu (its title carries "RetroPlug", which the bare grid never shows).
  expect(ui.findByTestId("tile-0") != null).toBeTruthy();
  expect(ui.findByTextContaining("RetroPlug")).toBe(null);
});
