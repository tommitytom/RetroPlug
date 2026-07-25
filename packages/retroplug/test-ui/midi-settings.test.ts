// Settings > MIDI device pickers, driven on the real headless menu. Guards the whole seam end-to-end: the
// submenu appears only when the host exposes the MIDI hooks (standalone), the cyclers read the live device
// list + current selection, and stepping a cycler calls the native setter with the right device name AND
// repaints the label (the App subscribeMidi wiring). Mirrors settings-cyclers.test.ts. The native host is
// faked via globalThis so this runs with no MIDI hardware.

import { test, expect, ui, navTo, Key } from "ui-harness";

// A fake SDL host: two input devices, one output. selected* start at the defaults ("" = All / None). The
// setters record their calls and mutate the fake state, so a re-render reflects the pick (as the real host
// would after reconnecting the port). Installed at module scope so it's present before the UI boots.
const calls: Array<[string, string]> = [];
const midi = {
  inputs: ["Launchpad MK2", "Arturia KeyStep 32"],
  outputs: ["Deluge"],
  selectedInput: "",
  selectedOutput: "",
};
const g = globalThis as Record<string, unknown>;
g.__rp_isStandalone = true;
g.__rp_getMidiConfig = () => ({ ...midi });
g.__rp_setMidiInput = (name: string) => {
  calls.push(["in", name]);
  midi.selectedInput = name;
};
g.__rp_setMidiOutput = (name: string) => {
  calls.push(["out", name]);
  midi.selectedOutput = name;
};

const labelOf = (prefix: string) => ui.findByTextContaining(prefix)?.text ?? "(missing)";

test("Settings > MIDI: input/output device cyclers pick a device, call the host, and repaint", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(30);

  expect(navTo("Settings")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(8);

  // The MIDI submenu is present (standalone + the seam) — open it.
  expect(navTo("MIDI")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(8);

  // Defaults: input = All Devices, output = None.
  expect(labelOf("Input Device")).toBe("Input Device: All Devices");
  expect(labelOf("Output Device")).toBe("Output Device: None");

  // Step the input cycler forward → the first device; the host setter is called with its name.
  expect(navTo("Input Device")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(8);
  expect(calls.at(-1)).toEqual(["in", "Launchpad MK2"]);
  expect(labelOf("Input Device")).toBe("Input Device: Launchpad MK2");

  // Again → the second device (proves the live list drives the cycle, not a fixed toggle).
  ui.tapKey(Key.Enter);
  ui.pump(8);
  expect(calls.at(-1)).toEqual(["in", "Arturia KeyStep 32"]);
  expect(labelOf("Input Device")).toBe("Input Device: Arturia KeyStep 32");

  // Once more wraps back to All Devices (empty selection).
  ui.tapKey(Key.Enter);
  ui.pump(8);
  expect(calls.at(-1)).toEqual(["in", ""]);
  expect(labelOf("Input Device")).toBe("Input Device: All Devices");

  // The output cycler picks the hardware output; Left steps back to None.
  expect(navTo("Output Device")).toBeTruthy();
  ui.tapKey(Key.Right);
  ui.pump(8);
  expect(calls.at(-1)).toEqual(["out", "Deluge"]);
  expect(labelOf("Output Device")).toBe("Output Device: Deluge");
  ui.tapKey(Key.Left);
  ui.pump(8);
  expect(calls.at(-1)).toEqual(["out", ""]);
  expect(labelOf("Output Device")).toBe("Output Device: None");
});
