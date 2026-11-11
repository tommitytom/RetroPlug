// Settings > Audio > Output Device picker, driven on the real headless menu. Guards the device seam end to end: the
// cycler reads the driver's device list from the audio package's hook (the fake device's description), cycling it
// repaints + stages (no commit), and Apply commits the whole choice to the section and the device. Mirrors
// audio-driver.test.ts; the native host is faked, so no backend library is initialised (the Zoom L6 rule).
import { test, expect, ui, navTo, Key } from "ui-harness";
import { FakeAudioDevice } from "@retroplug/audio/testSupport/fakeDevice";

const fake = new FakeAudioDevice({
  status: { backend: "portaudio", driver: "pipewire", device: "", sampleRate: 48000, blockFrames: 512, channels: 2 },
}).install();
const g = globalThis as Record<string, unknown>;
g.__rp_isStandalone = true;

const labelOf = (prefix: string) => ui.findByTextContaining(prefix)?.text ?? "(missing)";

test("Settings > Audio > Output Device: cycles the driver's devices, stages, and Apply commits the choice", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(30);
  expect(navTo("Settings")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(8);
  expect(navTo("Audio")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(8);

  // Default: the driver's default output, named.
  expect(labelOf("Output Device")).toBe("Output Device: Default (Speakers)");

  // The Block Size row carries the block's latency at the current rate (512 @ 48 kHz), on-screen and not just in the
  // menu model: it's a composed label, so this proves what actually renders.
  expect(labelOf("Block Size")).toBe("Block Size: 512 (10.7 ms)");

  // The mount applied the saved section once (the startup rendezvous), which is the baseline: a cycler step must not
  // add to it.
  const appliedAtBoot = fake.applied.length;
  // Cycle through the (Auto) driver's devices -> Speakers -> HDMI. Staged only (no device setter call yet).
  expect(navTo("Output Device")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(8);
  expect(labelOf("Output Device")).toBe("Output Device: Speakers");
  ui.tapKey(Key.Enter);
  ui.pump(8);
  expect(labelOf("Output Device")).toBe("Output Device: HDMI");
  expect(fake.applied.length).toBe(appliedAtBoot);

  // Apply -> the whole choice lands (driver unchanged = the auto choice's PortAudio/PipeWire).
  expect(navTo("Apply")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(8);
  expect(fake.applied.at(-1)).toEqual({
    backend: "auto", driver: "", device: "HDMI", sampleRate: 48000, blockSize: 512, outChannels: 2,
  });
  expect(labelOf("Output Device")).toBe("Output Device: HDMI");
});
