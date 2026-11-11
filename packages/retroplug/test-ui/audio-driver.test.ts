// Settings > Audio > Driver picker, driven on the real headless menu. Guards the audio-backend seam end to end: the
// Driver row offers every backend's drivers (from the audio package's describe/backendDrivers hooks), names the
// backend so two drivers spelled alike read differently, stepping cycles the options and repaints the label (the
// package's service subscribe wiring) and STAGES a draft only; Apply commits the selection to the section and the
// device.
//
// Also a regression guard for the focus-group bug this feature surfaced: the Audio "Apply" row starts disabled (clean
// draft) and flips enabled once a cycler dirties the draft. The menu's keypad group must rebuild on that
// disabled->enabled toggle, else Apply is never reachable by arrow / gamepad nav (Down stops at "Out Channels", and
// pressing Enter there just cycles it). navTo("Apply") succeeding after a cycler step is the assertion. Mirrors
// midi-settings.test.ts; the native host is faked, so no backend library is initialised.
import { test, expect, ui, navTo, Key } from "ui-harness";
import { FakeAudioDevice } from "@retroplug/audio/testSupport/fakeDevice";

const fake = new FakeAudioDevice().install();
const g = globalThis as Record<string, unknown>;
g.__rp_isStandalone = true;

const labelOf = (prefix: string) => ui.findByTextContaining(prefix)?.text ?? "(missing)";

// Drive the cursor to the top of the menu (saturate Up) so Down-nav is deterministic. Needed now that Apply RETAINS
// focus after a commit (it greys out on the last row rather than yanking back to the top), so a previous Apply can leave
// the cursor parked where Down-nav alone can't reach forward from.
const toTop = () => {
  for (let i = 0; i < 24; i++) {
    ui.tapKey(Key.Up);
    ui.pump(2);
  }
};

test("Settings > Audio > Driver: reads the backends, cycles + repaints, stages, and Apply commits the driver", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(30);

  expect(navTo("Settings")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(8);

  // The Audio submenu is present (standalone + the hooks) - open it.
  expect(navTo("Audio")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(8);

  // Auto names what it resolves to, backend included: "Auto" on its own does not say which driver you are on, and two
  // backends can spell one driver alike ("PortAudio: PipeWire" against "SDL: pipewire").
  expect(labelOf("Driver")).toBe("Driver: Auto (PortAudio: PipeWire)");

  // The mount applied the saved section once (the startup rendezvous); a cycler step must not add to it.
  const appliedAtBoot = fake.applied.length;
  // Step the Driver cycler forward: the first driver of the first backend. Nothing committed yet.
  expect(navTo("Driver")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(8);
  expect(labelOf("Driver")).toBe("Driver: PortAudio: PipeWire");
  expect(fake.applied.length).toBe(appliedAtBoot); // staged only - Audio is draft + Apply

  // Regression: the now-dirty draft enables Apply, and nav must be able to REACH it (the keypad group rebuilds on the
  // disabled->enabled toggle). Before the fix, Down stopped at "Out Channels" and this navTo failed.
  expect(navTo("Apply")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(8);

  // Apply committed the staged choice.
  expect(fake.applied.at(-1)).toEqual({
    backend: "portaudio", driver: "pipewire", device: "", sampleRate: 48000, blockSize: 512, outChannels: 2,
  });
  expect(labelOf("Driver")).toBe("Driver: PortAudio: PipeWire");
});

test("a host whose Auto resolves to nothing leaves the row plain", () => {
  // Graceful rather than "Auto ()": the bracket exists to answer a question, and an empty one answers nothing while
  // looking like a bug. The backends describe no auto driver here, which is what a null-device run reports.
  fake.description.backends = fake.description.backends.map((b) => ({ ...b, autoDriver: "" }));
  // Cycle the row back round to Auto - which also forces the repaint that reads the new description.
  toTop();
  expect(navTo("Driver")).toBeTruthy();
  for (let i = 0; i < 8 && !labelOf("Driver").startsWith("Driver: Auto"); i++) {
    ui.tapKey(Key.Enter);
    ui.pump(8);
  }
  expect(labelOf("Driver")).toBe("Driver: Auto");
});

test("Applying keeps the selection on the Apply row (it greys out, it does not jump to the top)", () => {
  // Runs on from the previous test, which leaves us in Settings > Audio. Step a value the saved section does not have,
  // so the draft is dirty and Apply is reachable (the driver may already be back on what was applied).
  toTop();
  expect(navTo("Block Size")).toBeTruthy();
  ui.tapKey(Key.Right); // step the cycler -> dirty draft
  ui.pump(8);
  expect(navTo("Apply")).toBeTruthy();
  ui.tapKey(Key.Enter); // commit + re-seed -> Apply greys out (disabled)
  ui.pump(8);

  // The selection stays on Apply (not yanked to the first row of the menu).
  const f = ui.focused();
  expect(f != null && f.text === "Apply").toBeTruthy();
  // And arrow nav still works FROM that row (Up lands on the last cycler, not the top).
  ui.tapKey(Key.Up);
  ui.pump(4);
  expect(ui.focused()!.text.includes("Out Channels")).toBeTruthy();
});
