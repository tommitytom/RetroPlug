// Project > Audio Routing for a Game Boy, end to end on the headless display: "Channels" is offered,
// "Pins" is not (a GB has no 2A03 output pins to split). The NES counterpart is audio-routing-nes.test.ts
// — a separate file because the UI harness boots one app per FILE and tests within one share it, so a
// second drop here would make it a two-system project and change the gating under test.
//
// Stepping BACKWARDS off Stereo is the load-bearing assertion: it lands on the last OFFERED row. If the
// cycler stepped a fixed-length table and merely hid Pins, this would land on a phantom row.

import { test, expect, ui, navTo, Key } from "ui-harness";

test("Audio Routing on a Game Boy offers Channels but never Pins", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(30);

  ui.fileDrop(ui.romDir() + "/mGB.gb", 0, 0);
  ui.pump(30);
  expect(ui.findByTestId("tile-0") != null).toBeTruthy();

  // Instance menu → Project (step past the Save / New rows that also contain "Project").
  ui.tapKey(Key.Esc);
  ui.pump(10);
  expect(navTo("New Project")).toBeTruthy();
  ui.tapKey(Key.Down);
  ui.pump(2);
  expect(navTo("System")).toBeTruthy();
  ui.tapKey(Key.Down);
  ui.pump(2);
  expect(ui.focused()!.text).toBe("Project >");
  ui.tapKey(Key.Enter);
  ui.pump(10);
  expect(navTo("Audio Routing: Stereo")).toBeTruthy();

  ui.tapKey(Key.Left);
  ui.pump(6);
  expect(ui.focused()!.text).toBe("Audio Routing: Channels"); // NOT Pins

  // And forward from there wraps to Stereo — Channels really is the last row for a GB.
  ui.tapKey(Key.Enter);
  ui.pump(6);
  expect(ui.focused()!.text).toBe("Audio Routing: Stereo");
});
