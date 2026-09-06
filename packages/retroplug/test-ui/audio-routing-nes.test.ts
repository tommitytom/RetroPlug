// Project > Audio Routing for a NES, end to end on the headless display. A NES offers BOTH split rows —
// "Channels" (its 5 mono core channels) and "Pins" (its 3 mono 2A03 output pins) — where a Game Boy
// offers only the first (see audio-routing-gb.test.ts, a separate file because the UI harness boots one
// app per FILE and tests within one share it).
//
// This is the on-screen half of the pure-TS gating test (test/menu/leaves.test.ts): it proves the
// filtered cycler actually renders and steps that way through the real Menu component, not just that
// validAudioRoutings returns the right list.

import { test, expect, ui, navTo, Key } from "ui-harness";

test("Audio Routing on a NES cycles through Channels and Pins", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(30);

  ui.fileDrop(ui.romDir() + "/bliptoaster.nes", 0, 0);
  ui.pump(30);
  expect(ui.findByTestId("tile-0") != null).toBeTruthy();

  // Instance menu → Project. "Project" is a substring of the Save / New rows above it, so step past
  // those rather than nav'ing by text (mirrors project-name.test.ts).
  ui.tapKey(Key.Esc);
  ui.pump(10);
  expect(navTo("New Project")).toBeTruthy();
  ui.tapKey(Key.Down);
  ui.pump(2);
  expect(navTo("System")).toBeTruthy(); // the row directly above the Project submenu
  ui.tapKey(Key.Down);
  ui.pump(2);
  expect(ui.focused()!.text).toBe("Project >");
  ui.tapKey(Key.Enter);
  ui.pump(10);
  expect(navTo("Audio Routing: Stereo")).toBeTruthy();

  // Enter steps the cycler forward; the row repaints in place (keepOpen).
  const step = (): string => {
    ui.tapKey(Key.Enter);
    ui.pump(6);
    return ui.focused()!.text;
  };
  expect(step()).toBe("Audio Routing: 2 Ch / Inst");
  expect(step()).toBe("Audio Routing: 1 Ch / Inst");
  expect(step()).toBe("Audio Routing: Channels"); // no "(1 GB)" — it read as a gigabyte
  expect(step()).toBe("Audio Routing: Pins");     // NES-only; the whole point of this test
  expect(step()).toBe("Audio Routing: Stereo");   // wraps

  // Left steps back onto the LAST offered row, which here is the NES-only one.
  ui.tapKey(Key.Left);
  ui.pump(6);
  expect(ui.focused()!.text).toBe("Audio Routing: Pins");
});
