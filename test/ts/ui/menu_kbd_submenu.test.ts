// Headless UI: expand a StartScreen submenu ("Recent") entirely by keyboard —
// navigate to it with the arrow keys and activate with Enter. Isolated in its
// own file for a pristine focus state.
import { test, expect, ui, Key } from "ui-harness";

test("keyboard: arrows to Recent + Enter expands the submenu", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(40);
  expect(ui.findByTextContaining("No Recent Files")).toBe(null);

  // Down until the "Recent" submenu item is focused.
  let onRecent = false;
  for (let i = 0; i < 12 && !onRecent; i++) {
    const f = ui.focused();
    if (f && f.text.includes("Recent")) { onRecent = true; break; }
    ui.tapKey(Key.Down);
    ui.pump(6);
  }
  expect(onRecent).toBeTruthy();

  ui.tapKey(Key.Enter); // expand inline
  ui.pump(20);
  expect(ui.findByTextContaining("No Recent Files")).toBeTruthy();
});
