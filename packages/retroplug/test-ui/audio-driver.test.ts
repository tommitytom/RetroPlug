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
// autoDriver names what "Auto" actually resolves to, which the Driver row shows in brackets - "Auto" on
// its own does not tell you which driver you are on, and that is the one thing you want to know when
// something sounds wrong.
g.__rp_getAudioConfig = () => ({ ...live, drivers: [...drivers], autoDriver: "PipeWire" });
g.__rp_setAudioConfig = (r: number, b: number, ch: number, d: string) => {
  calls.push([r, b, ch, d]);
  live.sampleRate = r;
  live.blockSize = b;
  live.outChannels = ch;
  live.driver = d;
};

const labelOf = (prefix: string) => ui.findByTextContaining(prefix)?.text ?? "(missing)";

// Drive the cursor to the top of the menu (saturate Up) so Down-nav is deterministic. Needed now that
// Apply RETAINS focus after a commit (it greys out on the last row rather than yanking back to the top),
// so a previous Apply can leave the cursor parked where Down-nav alone can't reach forward from.
const toTop = () => {
  for (let i = 0; i < 24; i++) {
    ui.tapKey(Key.Up);
    ui.pump(2);
  }
};

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
  expect(labelOf("Driver")).toBe("Driver: Auto (PipeWire)");

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

test("a host that cannot say what Auto resolved to leaves the row plain", () => {
  // Graceful rather than "Auto ()": the bracket exists to answer a question, and an empty one answers
  // nothing while looking like a bug. Runs on from the previous test, which leaves us in Settings > Audio.
  g.__rp_getAudioConfig = () => ({ ...live, driver: "Auto", drivers: [...drivers] });
  // Cycle the row back round to Auto - which also forces the repaint that reads the new fake.
  toTop();
  expect(navTo("Driver")).toBeTruthy();
  for (let i = 0; i < drivers.length && !labelOf("Driver").startsWith("Driver: Auto"); i++) {
    ui.tapKey(Key.Enter);
    ui.pump(8);
  }
  expect(labelOf("Driver")).toBe("Driver: Auto");
});

test("Applying keeps the selection on the Apply row (it greys out, it does not jump to the top)", () => {
  // Regression: Apply re-seeds the draft, so in the SAME commit it toggles enabled->disabled. The menu's
  // focus target used to only keep the cursor on a row if it were still FOCUSABLE — a just-disabled Apply
  // wasn't, so the selection yanked back to the first row of the menu. It should stay on Apply (now greyed).
  // Runs on from the previous test, which leaves us in Settings > Audio.
  // Dirty the draft so Apply is enabled + reachable, then focus it.
  toTop();
  expect(navTo("Driver")).toBeTruthy();
  ui.tapKey(Key.Right); // step the cycler → dirty draft
  ui.pump(8);
  expect(navTo("Apply")).toBeTruthy();
  ui.tapKey(Key.Enter); // commit + re-seed → Apply greys out (disabled)
  ui.pump(8);

  // The selection stays on Apply (not yanked to the first row of the menu).
  const f = ui.focused();
  expect(f != null && f.text === "Apply").toBeTruthy();
  // And arrow nav still works FROM that row (Up lands on the last cycler, not the top).
  ui.tapKey(Key.Up);
  ui.pump(4);
  expect(ui.focused()!.text.includes("Out Channels")).toBeTruthy();
});
