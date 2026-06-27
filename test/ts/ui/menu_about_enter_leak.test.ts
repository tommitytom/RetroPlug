// Regression: closing the AboutPanel with Enter must not leak a click into the
// menu underneath it.
//
// AboutPanel used to close on the Enter *press* (onKey). On the StartScreen that
// unmounts the panel and re-mounts the menu in the SAME commit; the SAME Enter
// then fires LV_EVENT_CLICKED on release, which lands on the freshly-focused
// first menu row ("Load...") and activates it -> openRomBrowser. In the app a
// file browser pops and steals keyboard focus ("keyboard stops working").
// Closing on the Enter release (onClick) keeps the panel as the click target
// until the event is consumed, so nothing leaks.
//
// Probe: ui.browserOpenCount() — opening/closing About should pop zero browsers.
import { test, expect, ui, Key } from "ui-harness";

const ABOUT_BODY = "Game Boy / NES / GBA emulator plugin"; // ui/menu/AboutPanel.tsx

function focusAbout() {
  for (let i = 0; i < 12; i++) {
    const f = ui.focused();
    if (f && f.text.includes("About")) return true;
    ui.tapKey(Key.Down);
    ui.pump(6);
  }
  return false;
}

test("Enter-closing About does not pop the ROM browser from the row underneath", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(40);
  expect(ui.browserOpenCount()).toBe(0);

  expect(focusAbout()).toBeTruthy();
  ui.tapKey(Key.Enter); // open About
  ui.pump(20);
  expect(ui.findByTextContaining(ABOUT_BODY)).toBeTruthy();

  ui.tapKey(Key.Enter); // close About
  ui.pump(20);
  expect(ui.findByTextContaining(ABOUT_BODY)).toBe(null);

  // The bug: the trailing CLICKED activated "Load..." -> openRomBrowser.
  expect(ui.browserOpenCount()).toBe(0);
  // And the start menu is untouched (not replaced by a stray load).
  expect(ui.findByText("Load...")).toBeTruthy();
});
