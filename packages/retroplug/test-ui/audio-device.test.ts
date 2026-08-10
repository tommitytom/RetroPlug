// Settings > Audio > Output Device picker, driven on the real headless menu. Guards the device seam: the
// cycler reads the selected driver's device list (devicesByDriver), cycling it repaints + stages (no commit),
// and Apply commits the device as __rp_setAudioConfig's 5th arg. (The driver-change-resets-device behaviour is
// covered at the unit level in test/menu/leaves.test.ts, since navTo here only walks downward.) Mirrors
// audio-driver.test.ts; the native host is faked.

import { test, expect, ui, navTo, Key } from "ui-harness";

const calls: Array<[number, number, number, string, string]> = [];
const live = { sampleRate: 48000, blockSize: 1024, outChannels: 2, driver: "Auto", device: "" };
const drivers = ["Auto", "PipeWire", "ALSA"];
const devicesByDriver: Record<string, string[]> = {
  Auto: ["Speakers", "HDMI"],
  PipeWire: ["Speakers", "HDMI"],
  ALSA: ["default", "sysdefault"],
};
const g = globalThis as Record<string, unknown>;
g.__rp_isStandalone = true;
g.__rp_getAudioConfig = () => ({ ...live, drivers: [...drivers], devicesByDriver });
g.__rp_setAudioConfig = (r: number, b: number, ch: number, d: string, dev: string) => {
  calls.push([r, b, ch, d, dev]);
  live.sampleRate = r;
  live.blockSize = b;
  live.outChannels = ch;
  live.driver = d;
  live.device = dev;
};

const labelOf = (prefix: string) => ui.findByTextContaining(prefix)?.text ?? "(missing)";

test("Settings > Audio > Output Device: cycles the driver's devices, stages, and Apply commits it as the 5th arg", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(30);
  expect(navTo("Settings")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(8);
  expect(navTo("Audio")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(8);

  // Default: the host API default output.
  expect(labelOf("Output Device")).toBe("Output Device: Default");

  // Cycle through the (Auto) driver's devices → Speakers → HDMI. Staged only (no device setter call yet).
  expect(navTo("Output Device")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(8);
  expect(labelOf("Output Device")).toBe("Output Device: Speakers");
  ui.tapKey(Key.Enter);
  ui.pump(8);
  expect(labelOf("Output Device")).toBe("Output Device: HDMI");
  expect(calls.length).toBe(0);

  // Apply → the device rides the 5th __rp_setAudioConfig arg (driver unchanged = Auto).
  expect(navTo("Apply")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(8);
  expect(calls.at(-1)).toEqual([48000, 1024, 2, "Auto", "HDMI"]);
  expect(labelOf("Output Device")).toBe("Output Device: HDMI");
});
