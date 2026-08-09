// Settings > Audio > Driver picker, driven on the real headless menu. Guards the audio-host-API seam
// end-to-end: the Driver cycler reads the live driver list the SDL host enumerates (getAudioDrivers), stepping
// it cycles the options + repaints the label (the App subscribeAudioDraft wiring) and STAGES a draft only, and
// Apply commits the selection (incl. the driver as __rp_setAudioConfig's 4th arg).
//
// Also a regression guard for the focus-group bug this feature surfaced: the Audio "Apply" row starts disabled
// (clean draft) and flips enabled once a cycler dirties the draft. The menu's keypad group must rebuild on that
// disabled->enabled toggle, else Apply is never reachable by arrow / gamepad nav (Down stops at "Out Channels",
// and pressing Enter there just cycles it) — which is exactly what a user hit. navTo("Apply") succeeding after a
// cycler step is the assertion. Mirrors midi-settings.test.ts; the native host is faked via globalThis.

import { test, expect, ui, navTo, Key } from "ui-harness";

// A fake SDL host offering three drivers; __rp_setAudioConfig records its calls AND mutates the live cfg (so a
// re-render reflects the applied value, as the real host would after reopening the stream). Module scope so
// it's present before the UI boots.
const calls: Array<[number, number, number, string]> = [];
const live = { sampleRate: 48000, blockSize: 1024, outChannels: 2, driver: "Auto" };
const drivers = ["Auto", "PipeWire", "ALSA"];
const g = globalThis as Record<string, unknown>;
g.__rp_isStandalone = true;
g.__rp_getAudioConfig = () => ({ ...live, drivers: [...drivers] });
g.__rp_setAudioConfig = (r: number, b: number, ch: number, d: string) => {
  calls.push([r, b, ch, d]);
  live.sampleRate = r;
  live.blockSize = b;
  live.outChannels = ch;
  live.driver = d;
};

const labelOf = (prefix: string) => ui.findByTextContaining(prefix)?.text ?? "(missing)";

test("Settings > Audio > Driver: reads the host list, cycles + repaints, stages, and Apply commits the driver", () => {
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

  // Step the Driver cycler forward → PipeWire (label repaints via the subscribe wiring). Nothing committed yet.
  expect(navTo("Driver")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(8);
  expect(labelOf("Driver")).toBe("Driver: PipeWire");
  expect(calls.length).toBe(0); // staged only — Audio is draft + Apply

  // Regression: the now-dirty draft enables Apply, and nav must be able to REACH it (the keypad group rebuilds
  // on the disabled->enabled toggle). Before the fix, Down stopped at "Out Channels" and this navTo failed.
  expect(navTo("Apply")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(8);

  // Apply committed the staged driver as the 4th arg (rate/block/channels unchanged).
  expect(calls.at(-1)).toEqual([48000, 1024, 2, "PipeWire"]);
  expect(live.driver).toBe("PipeWire");
  expect(labelOf("Driver")).toBe("Driver: PipeWire");
});
