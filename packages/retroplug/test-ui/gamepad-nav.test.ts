// The gamepad drives the menu end to end on the headless display, proving the controller reaches the same
// nav primitives the keyboard does. Injects the native "gamepad-button"/"gamepad-axis" buses (ui.gamepad*)
// and asserts on the rendered LVGL tree: d-pad moves focus + cycles a value, A selects, B backs out, the
// left stick moves focus, and the non-GB "leftshoulder" opens the instance menu once a game is running.

import { test, expect, ui, navToPad } from "ui-harness";

test("the gamepad navigates the start menu, adds a system, then opens + drives the instance menu", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(30);

  // Empty project → the start menu; its first item ("Recent") is focused on mount.
  const first = ui.focused();
  expect(first != null && first.text.includes("Recent")).toBeTruthy();

  // D-pad Down/Up move the LVGL focus (the pad twin of arrow nav).
  ui.gamepadTap("dpdown");
  ui.pump(4);
  const second = ui.focused();
  expect(second != null && second.text !== first!.text).toBeTruthy();
  ui.gamepadTap("dpup");
  ui.pump(4);
  expect(ui.focused()!.text.includes("Recent")).toBeTruthy();

  // Focus "Load mGB" with the d-pad and press A → a system is added, the menu gives way to the grid.
  expect(navToPad("Load mGB")).toBeTruthy();
  ui.gamepadTap("a");
  ui.pump(20);
  expect(ui.findByTestId("tile-0") != null).toBeTruthy();

  // No menu is up now. The non-GB "leftshoulder" opens the instance menu (a gamepad-only user's way in).
  ui.gamepadTap("leftshoulder");
  ui.pump(10);
  expect(ui.findByTextContaining("Duplicate Instance") != null).toBeTruthy();

  // Duplicate → a second instance, so the peer-only Link Group row appears (auto-linked at group 1).
  expect(navToPad("Duplicate Instance")).toBeTruthy();
  ui.gamepadTap("a");
  ui.pump(20);
  expect(ui.findByTestId("tile-1") != null).toBeTruthy();
  ui.gamepadTap("leftshoulder");
  ui.pump(10);

  // D-pad Right cycles a value in place (Link Group: 1 → 2), focus staying on the cycler.
  expect(navToPad("Link Group")).toBeTruthy();
  expect(ui.findByTextContaining("Link Group: 1") != null).toBeTruthy();
  ui.gamepadTap("dpright");
  ui.pump(6);
  expect(ui.findByTextContaining("Link Group: 2") != null).toBeTruthy();

  // A on a submenu expands it inline (System → the per-instance state items appear).
  expect(navToPad("System")).toBeTruthy();
  ui.gamepadTap("a");
  ui.pump(15);
  expect(ui.findByTextContaining("Save State") != null).toBeTruthy();

  // B backs out — the whole menu closes (parity with Esc), leaving the grid.
  ui.gamepadTap("b");
  ui.pump(10);
  expect(ui.findByTextContaining("Duplicate Instance")).toBe(null);
  expect(ui.findByTestId("tile-0") != null).toBeTruthy();

  // Reopen with leftshoulder and prove the LEFT STICK moves focus like the d-pad: a downward flick
  // (SDL Y+) advances the cursor by one row, then centre it so a later flick can re-trigger.
  ui.gamepadTap("leftshoulder");
  ui.pump(10);
  const beforeStick = ui.focused();
  expect(beforeStick != null).toBeTruthy();
  ui.gamepadAxis("lefty", 0.8);
  ui.pump(4);
  ui.gamepadAxis("lefty", 0);
  ui.pump(2);
  const afterStick = ui.focused();
  expect(afterStick != null && afterStick.text !== beforeStick!.text).toBeTruthy();

  ui.snapshotPng("/tmp/ui-gamepad-nav.png");
});
