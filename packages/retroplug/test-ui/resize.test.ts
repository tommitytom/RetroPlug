// Fit-to-grid: adding an instance asks the editor to resize the window to the grid. The plugin editor
// installs __rp_setWindowSize on the shared context; the headless harness doesn't, so the UI's
// requestWindowSize is normally a no-op here. This test installs a spy in its place (same JS context as
// the UI bundle) and drives the menu to add a second instance — the App effect should request a window
// twice as wide (2 columns) at the same height. Zoom-agnostic: asserts the ratio, not absolute pixels.

import { test, expect, ui, navTo, Key } from "ui-harness";

test("adding a second instance requests a window resized to fit the grid", () => {
  const calls: Array<[number, number]> = [];
  (globalThis as unknown as { __rp_setWindowSize?: (w: number, h: number) => void }).__rp_setWindowSize = (w, h) =>
    calls.push([w, h]);

  expect(ui.boot()).toBeTruthy();
  ui.pump(30);

  // Start menu → Load mGB: one tile → the fit-to-grid effect requests a single-column window.
  expect(navTo("Load mGB")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(20);
  expect(ui.findByTestId("tile-0") != null).toBeTruthy();
  const single = calls[calls.length - 1];
  expect(single != null).toBeTruthy(); // the effect ran for the first system

  // Duplicate via the instance menu → two tiles → a two-column window (2× wide, same height).
  ui.tapKey(Key.Esc);
  ui.pump(10);
  expect(navTo("Duplicate Instance")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(20);
  expect(ui.findByTestId("tile-1") != null).toBeTruthy();

  const dual = calls[calls.length - 1];
  console.log(`[resize] single=${JSON.stringify(single)} dual=${JSON.stringify(dual)}`);
  expect(dual[0]).toBe(single[0] * 2); // 2 columns
  expect(dual[1]).toBe(single[1]); // same row height
});
