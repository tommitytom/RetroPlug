// Settings > Audio > Driver picker, driven on the real headless menu. Guards the UI half of the audio-host-API
// seam: the Driver cycler reads the live driver list the SDL host enumerates (getAudioDrivers), stepping it
// cycles through the options and repaints the label immediately (the App subscribeAudioDraft wiring), and it
// STAGES a draft only — the device setter isn't called until Apply (that commit, incl. the driver as the 4th
// __rp_setAudioConfig arg, is asserted at the unit level in test/menu/leaves.test.ts, where the Apply row is
// reached directly rather than through the menu's focus-nav). Mirrors midi-settings.test.ts; the native host is
// faked via globalThis so this runs with no audio hardware.

import { test, expect, ui, navTo, Key } from "ui-harness";

// A fake SDL host offering three drivers; __rp_setAudioConfig records its calls so we can prove a cycler step
// does NOT commit (draft semantics). Installed at module scope so it's present before the UI boots.
const calls: Array<[number, number, number, string]> = [];
const live = { sampleRate: 48000, blockSize: 1024, outChannels: 2, driver: "Auto" };
const drivers = ["Auto", "PipeWire", "ALSA"];
const g = globalThis as Record<string, unknown>;
g.__rp_isStandalone = true;
g.__rp_getAudioConfig = () => ({ ...live, drivers: [...drivers] });
g.__rp_setAudioConfig = (r: number, b: number, ch: number, d: string) => {
  calls.push([r, b, ch, d]);
};

const labelOf = (prefix: string) => ui.findByTextContaining(prefix)?.text ?? "(missing)";

test("Settings > Audio > Driver: cycler reads the host driver list, cycles + repaints, and stages (no commit)", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(30);

  expect(navTo("Settings")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(8);

  // The Audio submenu is present (standalone + the seam) — open it.
  expect(navTo("Audio")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(8);

  // Default: the driver is Auto (the PipeWire-preferred default).
  expect(labelOf("Driver")).toBe("Driver: Auto");

  // Step the Driver cycler forward → PipeWire → ALSA → wraps to Auto. The label repaints each step (proving
  // the subscribe wiring), and the live device setter is NOT called (Audio stages a draft; only Apply commits).
  expect(navTo("Driver")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(8);
  expect(labelOf("Driver")).toBe("Driver: PipeWire");

  ui.tapKey(Key.Enter);
  ui.pump(8);
  expect(labelOf("Driver")).toBe("Driver: ALSA");

  ui.tapKey(Key.Enter);
  ui.pump(8);
  expect(labelOf("Driver")).toBe("Driver: Auto");

  // Right/Left cycle it too (not just Enter).
  ui.tapKey(Key.Right);
  ui.pump(8);
  expect(labelOf("Driver")).toBe("Driver: PipeWire");
  ui.tapKey(Key.Left);
  ui.pump(8);
  expect(labelOf("Driver")).toBe("Driver: Auto");

  // Staged only — the device was never reconfigured.
  expect(calls.length).toBe(0);
});
