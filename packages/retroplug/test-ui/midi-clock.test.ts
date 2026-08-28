// The Instance menu's MIDI Clock submenu on the headless display. The editor binds __rp_getTransport
// natively; the harness doesn't, so (like render-badge.test.ts stubbing __rp_getRenderJobs) we stub the
// seam and drive it.
//
// The case that matters is the one that shipped broken: while an EXTERNAL master is running, its tempo
// changes on its own — the user drags a fader in their DAW — and a menu built once sat there showing the
// tempo that was current when it opened. Nothing emitted, so nothing re-rendered, and the row looked
// locked. App polls the seam per frame while the menu is open; this asserts the label actually follows.

import { test, expect, ui, navTo, Key } from "ui-harness";

interface FakeTransport {
  playing: boolean;
  external: boolean;
  bpm: number;
  localBpm: number;
}

type TransportGlobals = {
  __rp_getTransport?: () => FakeTransport;
  __rp_setTransport?: (playing: boolean) => void;
  __rp_setClockBpm?: (bpm: number) => void;
};

test("MIDI Clock: an external master's tempo change repaints the row; the local tempo is editable", () => {
  const g = globalThis as TransportGlobals;
  const live: FakeTransport = { playing: true, external: true, bpm: 120, localBpm: 120 };
  g.__rp_getTransport = () => ({ ...live });
  g.__rp_setTransport = (playing) => (live.playing = playing);
  g.__rp_setClockBpm = (bpm) => {
    live.localBpm = bpm;
    live.bpm = bpm;
  };

  expect(ui.boot()).toBeTruthy();
  ui.pump(30);
  expect(navTo("Load mGB")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(30);

  // Open the instance menu and step into MIDI Clock.
  ui.tapKey(Key.Esc);
  ui.pump(10);
  expect(navTo("MIDI Clock")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(10);
  expect(ui.findByTextContaining("Source: External MIDI clock") != null).toBeTruthy();
  expect(ui.findByTextContaining("Tempo: 120 BPM") != null).toBeTruthy();

  // The DAW's tempo fader moves. Nobody touched the menu: only the per-frame poll can catch this.
  live.bpm = 140;
  ui.pump(10);
  expect(ui.findByTextContaining("Tempo: 140 BPM") != null).toBeTruthy();
  expect(ui.findByTextContaining("Tempo: 120 BPM")).toBe(null);

  // ...and it keeps following, downwards too.
  live.bpm = 87.6;
  ui.pump(10);
  expect(ui.findByTextContaining("Tempo: 88 BPM") != null).toBeTruthy();

  // The master stops: our own tempo comes back, and the row is editable again (Right = +1).
  live.external = false;
  live.bpm = live.localBpm;
  ui.pump(10);
  expect(ui.findByTextContaining("Tempo: 120 BPM") != null).toBeTruthy();
  expect(navTo("Tempo")).toBeTruthy();
  ui.tapKey(Key.Right);
  ui.pump(10);
  expect(live.localBpm).toBe(121);
  expect(ui.findByTextContaining("Tempo: 121 BPM") != null).toBeTruthy();

  delete g.__rp_getTransport;
  delete g.__rp_setTransport;
  delete g.__rp_setClockBpm;
});
