// The Instance menu's MIDI Clock submenu on the headless display. The editor binds __rp_getTransport natively; the
// harness doesn't, so the midi package's fake stream supplies it.
//
// The case that matters is the one that shipped broken: while an EXTERNAL master is running, its tempo changes on its
// own - the user drags a fader in their DAW - and a menu built once sat there showing the tempo that was current when it
// opened. Nothing emitted, so nothing re-rendered, and the row looked locked. The package's service polls the transport
// each frame and reports a repaint; this asserts the label actually follows.
import { test, expect, ui, navTo, Key } from "ui-harness";
import { FakeMidiDevice } from "@retroplug/midi/testSupport/fakeDevice";

const fake = new FakeMidiDevice({
  transport: { playing: true, external: true, bpm: 120, localBpm: 120 },
}).install();

test("MIDI Clock: an external master's tempo change repaints the row; the local tempo is editable", () => {
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
  fake.setExternal(true, 140);
  ui.pump(10);
  expect(ui.findByTextContaining("Tempo: 140 BPM") != null).toBeTruthy();
  expect(ui.findByTextContaining("Tempo: 120 BPM")).toBe(null);

  // ...and it keeps following, downwards too.
  fake.setExternal(true, 87.6);
  ui.pump(10);
  expect(ui.findByTextContaining("Tempo: 88 BPM") != null).toBeTruthy();

  // The master stops: our own tempo comes back, and the row is editable again (Right = +1). The tempo is the
  // PROJECT's now, so the step writes the section and the service drives the host from it.
  fake.setExternal(false);
  fake.transportState = { ...fake.transportState, bpm: 120, localBpm: 120 };
  ui.pump(10);
  expect(ui.findByTextContaining("Tempo: 120 BPM") != null).toBeTruthy();
  expect(navTo("Tempo")).toBeTruthy();
  ui.tapKey(Key.Right);
  ui.pump(10);
  expect(fake.tempos.at(-1)).toBe(121);
  expect(ui.findByTextContaining("Tempo: 121 BPM") != null).toBeTruthy();
});
