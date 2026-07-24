// A text prompt can type UPPERCASE, end to end on the headless display. DPF's "key" bus carries the
// unshifted code point (the A key is always 'a'), so before this the field could never receive a capital.
// The fix threads the DPF modifier mask onto the bus and the menu applies Shift itself. We drive the
// render Filename prompt (its filter allows both cases): a Shift-held letter lands uppercase, a plain one
// lands lowercase — proving the mixed-case path a project rename relies on.

import { test, expect, ui, navTo, Key, Mod } from "ui-harness";

const Z = "z".charCodeAt(0); // the Z key's unshifted code point, as DPF delivers it

test("a text prompt types uppercase under Shift and lowercase without it", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(30);

  // Drop a real ROM → a disk-backed system; the render Filename derives to the ROM stem "mGB".
  ui.fileDrop(ui.romDir() + "/mGB.gb", 0, 0);
  ui.pump(30);
  expect(ui.findByTestId("tile-0") != null).toBeTruthy();

  // Instance menu → System → Render → arm the Filename prompt.
  ui.tapKey(Key.Esc);
  ui.pump(10);
  expect(navTo("System")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(10);
  expect(navTo("Render")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(10);
  expect(navTo("Filename: mGB")).toBeTruthy();
  ui.tapKey(Key.Enter); // open the text prompt (seeded "mGB")
  ui.pump(10);

  // Type Shift+Z then a plain z. The prompt overlay shows the live value + "_".
  ui.tapKey(Z, Mod.Shift);
  ui.pump(4);
  ui.tapKey(Z);
  ui.pump(4);

  // "mGB" + "Z" (shifted) + "z" (plain) = "mGBZz": the capital proves Shift reached the field, and the
  // trailing lowercase proves an unshifted key still lands lowercase (mixed case, not force-upper).
  expect(ui.findByTextContaining("mGBZz") != null).toBeTruthy();
  expect(ui.findByTextContaining("mGBzz")).toBe(null); // NOT both-lowercase — Shift really uppercased
});
