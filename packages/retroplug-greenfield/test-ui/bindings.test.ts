// The keyboard bindings editor, end to end on the headless display. Reaches Settings → Keyboard Bindings
// from the start menu (no system needed), then drives the capture flow the way a user would: Enter arms a
// button row (label → "Press a key…"), the next key press rebinds it (write-through — the row relabels
// from the re-resolved profile), and Backspace clears it. Keys ride the raw "key" bus via tapKey, exactly
// as the real editor receives them.

import { test, expect, ui, Key } from "ui-harness";

const KEY_BACKSPACE = 0x08; // DPF/LVGL backspace — raw-passed by tapKey
const keyQ = "Q".charCodeAt(0); // a plain ASCII key to bind

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

test("Settings → Keyboard Bindings: arm, capture a key, and clear a binding", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(30);

  // Start menu → Settings → Keyboard Bindings (both submenus expand inline).
  expect(navTo("Settings")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(8);
  expect(navTo("Keyboard Bindings")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(8);

  // The 8 button rows render with their default bindings baked in, plus the reset row.
  expect(ui.findByTextContaining("A: Z, z") != null).toBeTruthy();
  expect(ui.findByTextContaining("Start: Enter") != null).toBeTruthy();
  expect(ui.findByTextContaining("Reset Keyboard to Defaults") != null).toBeTruthy();

  // Focus the A row and arm it: Enter swaps the label to the capture prompt (and holds nav).
  expect(navTo("A: Z, z")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(6);
  expect(ui.focused()!.text.includes("Press a key")).toBeTruthy();

  // Press Q → the binding is captured, persisted, and the row re-resolves to the new key.
  ui.tapKey(keyQ);
  ui.pump(10);
  expect(ui.findByText("A: Q") != null).toBeTruthy();
  expect(ui.findByTextContaining("Press a key") == null).toBeTruthy(); // disarmed

  // Backspace on the (still-focused) A row clears the binding.
  expect(navTo("A: Q")).toBeTruthy();
  ui.tapKey(KEY_BACKSPACE);
  ui.pump(10);
  expect(ui.findByText("A: -") != null).toBeTruthy();

  ui.snapshotPng("/tmp/greenfield-ui-bindings.png");
});
