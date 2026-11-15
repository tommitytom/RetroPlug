// The LSDj MI.MAP app: does the grid read like a song screen, and does a pad launch the right row at the
// right moment?
//
// Everything about the LEDs is asserted by reading them back OFF THE FAKE DEVICE rather than by
// inspecting the app's intentions, so a test fails when the hardware would end up wrong.
import { test, expect } from "@retroplug/testing";
import { FakeLaunchpad } from "@retroplug/launchpad/testSupport/fakeDevice";
import { ControllerSession } from "@retroplug/launchpad/session";
import { controllerSessionNode } from "@retroplug/launchpad/controllerSession";
import { registerControllerApps } from "@retroplug/launchpad/apps";
import { NodeRig, bytesOn, type BlockSpec } from "@retroplug/testing/rig";
import { lsdjSyncNode } from "@retroplug/lsdj/lsdjSyncNode";
import { MIDIMAP_CLOCK, MIDIMAP_NOTEOFF } from "@retroplug/lsdj/lsdjSyncBytes";
import { songRowTicks } from "@retroplug/lsdj/playback";
import runtime from "../appRuntime";
import { lsdjMidiMap, rowAt, channelAt, followRow, WINDOW_ROWS, MAX_PAGE, type Quantise } from "../lsdjMidiMap";
import { lsdjMidiMapTarget } from "../launchTarget";
import { Palette, padIndex, palette, type Led } from "@retroplug/launchpad";
import { SongSchema, type Song } from "@retroplug/lsdj/model";
import { PredictedLsdjModel } from "@retroplug/lsdj/playback";
import { idlePosition, type PlaybackModel, type PlaybackPosition } from "@retroplug/tracker/playbackModel";

const CONTENT: Led = { mode: "static", colour: palette(Palette.greenDim) };
const PLAYHEAD: Led = { mode: "static", colour: palette(Palette.greenBright) };
const CUED: Led = { mode: "pulse", colour: palette(Palette.yellow) };
const OFF: Led = { mode: "off" };

/** A song from a per-row chain table, as packages/lsdj/ts/test/playback.test.ts builds them. */
function song(rows: (number | null)[][], chains: (number | null)[][]): Song {
  return SongSchema.parse({
    formatVersion: 22,
    rows: rows.map((chainsForRow) => ({ chains: chainsForRow })),
    chains: chains.map((phrases) => ({ phrases })),
  });
}

/** Four channels, `n` rows, every cell pointing at the same one-phrase chain - so every row has content
 *  and lasts 96 ticks. One chain rather than n of them because LSDj only has 128, and nothing here reads
 *  chain identity: the app cares whether a cell has content and how long its row lasts. */
const fullSong = (n: number) => song(Array.from({ length: n }, () => [0, 0, 0, 0]), [[0]]);

/** A model whose position the test sets directly, for asserting the display without simulating time. */
class ScriptedModel implements PlaybackModel {
  readonly channelCount = 4;
  current: PlaybackPosition = idlePosition(4);
  constructor(private readonly source: PredictedLsdjModel) {}
  position(): PlaybackPosition { return this.current; }
  grid() { return this.source.grid(); }
  setRows(rows: (number | null)[]): void {
    this.current = {
      playing: rows.some((r) => r !== null),
      channels: rows.map((r) => ({ playing: r !== null, songRow: r })),
    };
  }
}

interface Harness {
  device: FakeLaunchpad;
  sent: number[][];
  run(opts?: { tick?: number; transport?: boolean; ticks?: number; tickDivisor?: number; input?: number[][] }): void;
}

function harness(playback: PlaybackModel, config: Record<string, unknown> = {}, tickDivisor = 1): Harness {
  const device = new FakeLaunchpad();
  const sent: number[][] = [];
  const session = new ControllerSession(lsdjMidiMap, {
    playback,
    target: lsdjMidiMapTarget((d) => sent.push(d)),
    config,
  });
  device.write(session.connect());
  return {
    device, sent,
    run: (opts = {}) => device.write(session.update({
      input: opts.input ?? [],
      tick: opts.tick ?? 0,
      transport: opts.transport ?? true,
      ticks: opts.ticks ?? 0,
      tickDivisor: opts.tickDivisor ?? tickDivisor,
    })),
  };
}

// --- layout --------------------------------------------------------------------------------------

test("the grid is a song screen twice over: 4 channel columns, then 8 more rows", () => {
  // Left pane, top-left corner: pu1 at the window's first row.
  expect(rowAt(0, 0, 0)).toBe(0);
  expect(channelAt(0)).toBe(0);
  // Right pane continues the song rather than repeating it.
  expect(rowAt(0, 4, 0)).toBe(8);
  expect(rowAt(0, 7, 7)).toBe(15);
  // Columns 0 and 4 are both pu1; 3 and 7 are both noi.
  expect(channelAt(4)).toBe(0);
  expect(channelAt(3)).toBe(3);
  expect(channelAt(7)).toBe(3);
});

test("every pad maps to exactly one (channel, row), across pages", () => {
  for (const page of [0, 1, 7, MAX_PAGE]) {
    const base = page * WINDOW_ROWS;
    const seen = new Set<string>();
    for (let y = 0; y < 8; y++) {
      for (let x = 0; x < 8; x++) seen.add(`${channelAt(x)}:${rowAt(base, x, y)}`);
    }
    expect(seen.size).toBe(64); // no pad shadows another
    expect(seen.has(`0:${base}`)).toBe(true);
    expect(seen.has(`3:${base + 15}`)).toBe(true);
  }
});

test("followRow takes the first PLAYING channel, not the first channel", () => {
  expect(followRow(idlePosition(4))).toBe(null);
  expect(followRow({ playing: true, channels: [
    { playing: false, songRow: null },
    { playing: true, songRow: 9 },
    { playing: true, songRow: 3 },
    { playing: false, songRow: null },
  ] })).toBe(9);
});

// --- LEDs ----------------------------------------------------------------------------------------

test("a song row with a chain is dim, and an empty one is dark", () => {
  // pu1 and wav have content at row 0; pu2 and noi do not.
  const model = new PredictedLsdjModel(song([[0, null, 0, null]], [[0]]));
  const h = harness(model);
  h.run();

  expect(h.device.pad(0, 0)).toEqual(CONTENT); // pu1, row 0
  expect(h.device.pad(1, 0)).toEqual(OFF); // pu2, row 0
  expect(h.device.pad(2, 0)).toEqual(CONTENT); // wav, row 0
  expect(h.device.pad(3, 0)).toEqual(OFF); // noi, row 0
  expect(h.device.pad(0, 1)).toEqual(OFF); // row 1 has nothing at all
});

test("four diverging playheads show as four bright pads in four columns", () => {
  // The B4 case, which is the whole reason a column is a channel: the cart's channels advance
  // independently as their chains end, so at any moment they are usually NOT on the same row.
  const model = new ScriptedModel(new PredictedLsdjModel(fullSong(16)));
  const h = harness(model);
  model.setRows([0, 1, 2, 9]);
  h.run();

  expect(h.device.pad(0, 0)).toEqual(PLAYHEAD); // pu1 on row 0, left pane
  expect(h.device.pad(1, 1)).toEqual(PLAYHEAD); // pu2 on row 1
  expect(h.device.pad(2, 2)).toEqual(PLAYHEAD); // wav on row 2
  expect(h.device.pad(7, 1)).toEqual(PLAYHEAD); // noi on row 9 - the RIGHT pane, noi column
  expect(h.device.pad(3, 0)).toEqual(CONTENT); // noi's row 0 is just content again
});

test("a cued row pulses across all four of its cells, because a launch is song-wide", () => {
  const model = new ScriptedModel(new PredictedLsdjModel(fullSong(16)));
  const h = harness(model, { quantise: "bar" });
  model.setRows([0, 0, 0, 0]);
  h.run({ tick: 10 });

  h.run({ tick: 11, input: [h.device.press(1, 3)] }); // press pu2's column at row 3
  for (let x = 0; x < 4; x++) expect(h.device.pad(x, 3)).toEqual(CUED);
  expect(h.device.pad(4, 3)).toEqual(CONTENT); // the right pane shows row 11 there, a different row
});

test("rows past the launchable range stay dark", () => {
  // Rows 254 and 255 are the 0xFE/0xFF sentinels (B7), so the last page shows 14 rows, not 16.
  const model = new PredictedLsdjModel(fullSong(256));
  const h = harness(model, { page: MAX_PAGE, follow: false });
  h.run();

  expect(h.device.pad(7, 5)).toEqual(CONTENT); // row 253
  expect(h.device.pad(7, 6)).toEqual(OFF); // row 254
  expect(h.device.pad(7, 7)).toEqual(OFF); // row 255
});

// --- launching -----------------------------------------------------------------------------------

test("with nothing playing, a press launches immediately whatever the quantise setting", () => {
  // Otherwise the first press of a session waits for a boundary the stopped cart will never reach.
  const model = new PredictedLsdjModel(fullSong(16));
  const h = harness(model, { quantise: "bar" });
  h.run({ tick: 5 });
  h.run({ tick: 6, input: [h.device.press(0, 2)] });
  expect(h.sent).toEqual([[0x90, 2, 100]]);
});

test("quantise bar: the launch waits for the bar line, and lands exactly on it", () => {
  const model = new ScriptedModel(new PredictedLsdjModel(fullSong(16)));
  const h = harness(model, { quantise: "bar" });
  model.setRows([0, 0, 0, 0]); // something IS playing, so the cue has a boundary to wait for
  h.run({ tick: 100 });

  h.run({ tick: 101, input: [h.device.press(0, 5)] });
  expect(h.sent.length).toBe(0);
  h.run({ tick: 191 }); // one tick short of the bar at 192
  expect(h.sent.length).toBe(0);
  h.run({ tick: 192 });
  expect(h.sent).toEqual([[0x90, 5, 100]]);
});

test("quantise beat fires four times as often as bar", () => {
  const model = new ScriptedModel(new PredictedLsdjModel(fullSong(16)));
  const h = harness(model, { quantise: "beat" });
  model.setRows([0, 0, 0, 0]);
  h.run({ tick: 100 });

  h.run({ tick: 101, input: [h.device.press(0, 5)] });
  h.run({ tick: 119 });
  expect(h.sent.length).toBe(0);
  h.run({ tick: 120 });
  expect(h.sent.length).toBe(1);
});

test("quantise immediate fires in the same update as the press", () => {
  const model = new ScriptedModel(new PredictedLsdjModel(fullSong(16)));
  const h = harness(model, { quantise: "immediate" });
  model.setRows([0, 0, 0, 0]);
  h.run({ tick: 100 });
  h.run({ tick: 101, input: [h.device.press(0, 5)] });
  expect(h.sent).toEqual([[0x90, 5, 100]]);
});

test("at a tempo divisor of d, a bar launch waits out the CART's bar and a hostBar the DAW's", () => {
  // MI.MAP clocks the cart at 24 / tickDivisor ticks a quarter, so a cart bar is 96 x d DAW ticks and a cart beat
  // 24 x d: counting the DAW's 96 at divisor 2 makes a `bar` launch land mid-phrase. The cart's phase is its own
  // clock, walked from ppq 0. `hostBar` and `hostBeat` keep counting the DAW's, which is all a project at the
  // default divisor 1 has ever had.
  const firesAt = (quantise: Quantise, tickDivisor: number): number => {
    const model = new ScriptedModel(new PredictedLsdjModel(fullSong(16)));
    const h = harness(model, { quantise, follow: false }, tickDivisor);
    model.setRows([0, 0, 0, 0]); // something IS playing, so the cue has a boundary to wait for
    h.run({ tick: 50 });
    h.run({ tick: 51, input: [h.device.press(0, 5)] });
    for (let tick = 52; tick <= 51 + 96 * 4; tick++) {
      h.run({ tick });
      if (h.sent.length) return tick;
    }
    return -1;
  };
  expect(firesAt("bar", 1), "bar at 1").toBe(96);
  expect(firesAt("bar", 2), "bar at 2").toBe(192);
  expect(firesAt("bar", 3), "bar at 3").toBe(288);
  expect(firesAt("hostBar", 2), "hostBar at 2").toBe(96);
  expect(firesAt("hostBar", 3), "hostBar at 3").toBe(96);
  expect(firesAt("beat", 2), "beat at 2").toBe(96);
  expect(firesAt("beat", 3), "beat at 3").toBe(72);
  expect(firesAt("hostBeat", 2), "hostBeat at 2").toBe(72);
  expect(firesAt("hostBeat", 3), "hostBeat at 3").toBe(72);
});

test("quantise rowEnd waits for the song to change row, not for a clock boundary", () => {
  const model = new ScriptedModel(new PredictedLsdjModel(fullSong(16)));
  const h = harness(model, { quantise: "rowEnd" });
  model.setRows([2, 2, 2, 2]);
  h.run({ tick: 0 });

  h.run({ tick: 1, input: [h.device.press(0, 6)] });
  expect(h.sent.length).toBe(0);
  h.run({ tick: 500 }); // ticks alone do not release it
  expect(h.sent.length).toBe(0);

  model.setRows([3, 3, 3, 3]); // the row turned over
  h.run({ tick: 501 });
  expect(h.sent).toEqual([[0x90, 6, 100]]);
});

test("a second press replaces the pending cue rather than queueing another launch", () => {
  const model = new ScriptedModel(new PredictedLsdjModel(fullSong(16)));
  const h = harness(model, { quantise: "bar" });
  model.setRows([0, 0, 0, 0]);
  h.run({ tick: 0 });

  h.run({ tick: 1, input: [h.device.press(0, 2)] });
  h.run({ tick: 2, input: [h.device.press(0, 4)] }); // changed my mind
  h.run({ tick: 96 });
  expect(h.sent).toEqual([[0x90, 4, 100]]); // only the later choice launches
});

test("a quantise rule changed while a cue waits moves the wait with it, either way", () => {
  // The rule is read on every update, so a settings row changed while the cued row pulses yellow must move the wait
  // rather than leave it waiting for a boundary that rule no longer counts. Both of a press's facts are kept for it, so
  // a rowEnd cue set to beat/bar fires on the boundary its tick asks for, and a boundary cue set to rowEnd fires on
  // the next row change. The one-way version left a rowEnd cue pulsing for ever, and fired a bar cue at once.
  const model = new ScriptedModel(new PredictedLsdjModel(fullSong(16)));
  const config: Record<string, unknown> = { quantise: "rowEnd", follow: false };
  const h = harness(model, config);
  model.setRows([2, 2, 2, 2]);
  h.run({ tick: 10 });
  h.run({ tick: 11, input: [h.device.press(0, 6)] }); // cued for the row end
  expect(h.sent.length).toBe(0);
  config.quantise = "beat"; // the settings row, while the pad pulses
  h.run({ tick: 23 });
  expect(h.sent.length).toBe(0);
  h.run({ tick: 24 }); // the next beat after the press at host tick 11
  expect(h.sent).toEqual([[0x90, 6, 100]]);

  config.quantise = "bar";
  h.run({ tick: 30 });
  h.run({ tick: 31, input: [h.device.press(0, 7)] }); // cued for the bar at 96
  config.quantise = "rowEnd";
  h.run({ tick: 40 });
  expect(h.sent.length).toBe(1); // not at once, as it would be with no press row to compare
  model.setRows([3, 3, 3, 3]); // the row turned over
  h.run({ tick: 41 });
  expect(h.sent).toEqual([[0x90, 6, 100], [0x90, 7, 100]]);
});

test("a divisor shrink does not fire a cued launch off the grid", () => {
  // A press at host tick 51 is cued for the cart's bar at 192 at divisor 2. At divisor 1 the cart's bar is 96, already
  // behind the playhead at 150: re-deriving alone would fire there, mid-phrase on the divisor-1 grid. The boundary the
  // divisor change MOVED behind the playhead is re-snapped from now onto the new grid, which is 192, the bar both
  // clocks share.
  const model = new ScriptedModel(new PredictedLsdjModel(fullSong(16)));
  const h = harness(model, { quantise: "bar", follow: false }, 2);
  model.setRows([0, 0, 0, 0]);
  h.run({ tick: 50 });
  h.run({ tick: 51, input: [h.device.press(0, 5)] });
  h.run({ tick: 150, tickDivisor: 1 });
  expect(h.sent.length).toBe(0); // 96 is behind the playhead
  h.run({ tick: 192, tickDivisor: 1 });
  expect(h.sent).toEqual([[0x90, 5, 100]]);
});

test("a divisor shrink re-snaps a boundary the new grid does not share", () => {
  // At divisor 3 a press at 51 is cued for the cart's bar at 288. Divisor 2's bars are 192 apart, so 288 is no
  // boundary on the new grid at all, and holding it would fire mid-phrase. The moved boundary is re-snapped from the
  // playhead at 200, so the launch waits for the next divisor-2 bar, 384.
  const model = new ScriptedModel(new PredictedLsdjModel(fullSong(16)));
  const h = harness(model, { quantise: "bar", follow: false }, 3);
  model.setRows([0, 0, 0, 0]);
  h.run({ tick: 50 });
  h.run({ tick: 51, input: [h.device.press(0, 5)] });
  h.run({ tick: 200, tickDivisor: 2 });
  expect(h.sent.length).toBe(0); // 192 is behind the playhead
  h.run({ tick: 383, tickDivisor: 2 });
  expect(h.sent.length).toBe(0); // 288 is not a divisor-2 boundary
  h.run({ tick: 384, tickDivisor: 2 });
  expect(h.sent).toEqual([[0x90, 5, 100]]);
});

test("a rule changed long after the press waits for the new grid, not the old boundary", () => {
  // A rowEnd cue stores no boundary, so the first derive from a bar rule would take the bar of the tick the press
  // landed on - 96 here, behind the playhead at 500, since a chain lasts many bars. That is the reachable form of the
  // mid-phrase launch: the boundary only exists on the new grid because it moved there, so it re-snaps to 576.
  const model = new ScriptedModel(new PredictedLsdjModel(fullSong(16)));
  const config: Record<string, unknown> = { quantise: "rowEnd", follow: false };
  const h = harness(model, config);
  model.setRows([2, 2, 2, 2]);
  h.run({ tick: 10 });
  h.run({ tick: 11, input: [h.device.press(0, 6)] });
  config.quantise = "bar";
  h.run({ tick: 500 });
  expect(h.sent.length).toBe(0);
  h.run({ tick: 575 });
  expect(h.sent.length).toBe(0);
  h.run({ tick: 576 });
  expect(h.sent).toEqual([[0x90, 6, 100]]);
});

test("a rule changed to a finer one fires on its own boundary, not the coarser one", () => {
  // A bar cue pressed at 31 waits for 96, but the beat rule asks for the next beat, 48. The boundary is on the new
  // grid and ahead of the playhead, so it is not re-snapped: the wait is simply the new rule's.
  const model = new ScriptedModel(new PredictedLsdjModel(fullSong(16)));
  const config: Record<string, unknown> = { quantise: "bar", follow: false };
  const h = harness(model, config);
  model.setRows([0, 0, 0, 0]);
  h.run({ tick: 30 });
  h.run({ tick: 31, input: [h.device.press(0, 6)] });
  config.quantise = "beat";
  h.run({ tick: 40 });
  expect(h.sent.length).toBe(0);
  h.run({ tick: 47 });
  expect(h.sent.length).toBe(0);
  h.run({ tick: 48 });
  expect(h.sent).toEqual([[0x90, 6, 100]]);
});

test("a block that steps past an unchanged boundary fires on that block, not the next one", () => {
  // An 8192-frame block or a forward seek can take the tick from 95 over the bar at 96 to 100. That boundary did not
  // move, so it is not re-snapped: only a boundary a rule or divisor change HAS moved waits for a later one.
  const model = new ScriptedModel(new PredictedLsdjModel(fullSong(16)));
  const h = harness(model, { quantise: "bar", follow: false });
  model.setRows([0, 0, 0, 0]);
  h.run({ tick: 0 });
  h.run({ tick: 1, input: [h.device.press(0, 5)] });
  h.run({ tick: 95 });
  expect(h.sent.length).toBe(0);
  h.run({ tick: 100 });
  expect(h.sent).toEqual([[0x90, 5, 100]]);
});

test("releasing a pad does nothing - a release is not a stop", () => {
  // MEASURED (B5): the 0xFE handshake leaves the cart playing and stepping. Sending it on every pad
  // release would be protocol noise whose effect on other LSDj builds is unknown, and "let go to stop"
  // is the wrong semantic for a launcher anyway.
  const model = new PredictedLsdjModel(fullSong(16));
  const h = harness(model, { quantise: "immediate" });
  h.run({ tick: 0 });
  h.run({ tick: 1, input: [h.device.press(0, 3), h.device.release(0, 3)] });
  expect(h.sent).toEqual([[0x90, 3, 100]]); // the launch, and nothing else
});

test("a pad beyond the launchable range launches nothing", () => {
  const model = new PredictedLsdjModel(fullSong(256));
  const h = harness(model, { quantise: "immediate", page: MAX_PAGE, follow: false });
  h.run({ tick: 0 });
  h.run({ tick: 1, input: [h.device.press(7, 7)] }); // row 255
  expect(h.sent.length).toBe(0);
});

// --- paging and follow ---------------------------------------------------------------------------

test("the window follows the playhead, a page at a time", () => {
  const model = new ScriptedModel(new PredictedLsdjModel(fullSong(64)));
  const h = harness(model, { follow: true });

  model.setRows([3, 3, 3, 3]);
  h.run();
  expect(h.device.pad(0, 3)).toEqual(PLAYHEAD); // page 0

  model.setRows([20, 20, 20, 20]);
  h.run();
  // Page 1 covers rows 16-31, so row 20 is left pane, y=4.
  expect(h.device.pad(0, 4)).toEqual(PLAYHEAD);
  expect(h.device.pad(0, 3)).toEqual(CONTENT); // row 19, no longer the playhead
});

test("the window does NOT move while the playhead stays inside it", () => {
  const model = new ScriptedModel(new PredictedLsdjModel(fullSong(64)));
  const h = harness(model, { follow: true });
  model.setRows([0, 0, 0, 0]);
  h.run();
  model.setRows([15, 15, 15, 15]); // still within rows 0-15
  h.run();
  expect(h.device.pad(7, 7)).toEqual(PLAYHEAD); // right pane, last row - page 0 unchanged
});

test("page buttons move the window, and clamp at both ends", () => {
  const model = new PredictedLsdjModel(fullSong(64));
  const h = harness(model, { follow: false });
  h.run();
  expect(h.device.pad(0, 0)).toEqual(CONTENT); // row 0 has content

  h.run({ input: [h.device.pressButton("down")] }); // to page 1
  h.run({ input: [h.device.pressButton("down")] }); // to page 2
  h.run({ input: [h.device.pressButton("up")] }); // back to page 1
  expect(h.device.pad(0, 0)).toEqual(CONTENT); // row 16 also has content

  for (let i = 0; i < 5; i++) h.run({ input: [h.device.pressButton("up")] });
  expect(h.device.button("up")).toEqual(OFF); // clamped at page 0, and the button says so
});

test("the session button toggles follow, and shows which way it is set", () => {
  const model = new ScriptedModel(new PredictedLsdjModel(fullSong(64)));
  const h = harness(model, { follow: true });
  h.run();
  expect(h.device.button("session")).toEqual(PLAYHEAD); // following

  h.run({ input: [h.device.pressButton("session")] });
  expect(h.device.button("session")).toEqual(CONTENT); // not following

  model.setRows([40, 40, 40, 40]);
  h.run();
  expect(h.device.pad(0, 0)).toEqual(CONTENT); // still on page 0, playhead off-screen
});

// --- end to end ----------------------------------------------------------------------------------

test("driven by a real predictor over a long run, the lit playhead tracks the model", () => {
  // The whole stack: presses in, launch on the wire, predictor advanced by the same clock, LEDs read back
  // off the device. Four rows of one phrase each, so the song walks 0,1,2,3,0,... every 96 ticks.
  const model = new PredictedLsdjModel(fullSong(4));
  const h = harness(model, { quantise: "immediate" });

  h.run({ tick: 0 });
  h.run({ tick: 1, input: [h.device.press(0, 0)] }); // launch row 0
  expect(h.sent).toEqual([[0x90, 0, 100]]);

  let checked = 0;
  for (let tick = 2; tick <= 600; tick += 7) {
    h.run({ tick });
    const row = model.position().channels[0].songRow!;
    expect(h.device.pad(0, row)).toEqual(PLAYHEAD);
    for (let other = 0; other < 4; other++) {
      if (other !== row) expect(h.device.pad(0, other)).toEqual(CONTENT);
    }
    checked++;
  }
  expect(checked > 80).toBeTruthy(); // the run really did span several song wraps
});

test("at a tempo divisor of d, a launch cued for the row's end reaches the cart as the cart's row ends", () => {
  // The session node walks the clock the cart hears (24 / tickDivisor a quarter, as rp.lsdj.sync clocks MI.MAP), so
  // the row its predictor turns over on is the cart's own. Counted in the clocks rp.lsdj.sync sent the cart, the launch
  // cued while row 0 plays reaches it once row 0 has had all 96 of its ticks, at every divisor the setting offers
  // (the fractional 24 / d included); walking every host tick would release it early, at 96 / d.
  registerControllerApps([runtime]);
  const released = (tickDivisor: number, tempoDivisor: number): number[] => {
    const controller = new NodeRig(controllerSessionNode, {
      app: "lsdj-midimap",
      target: "system",
      systemId: 1,
      appConfig: { quantise: "rowEnd", follow: true, page: 0 },
      songRowTicks: songRowTicks(fullSong(16)),
      tickDivisor,
    });
    const cart = new NodeRig(lsdjSyncNode, { mode: "midiMap", tempoDivisor });
    const pad = (row: number) => [{ frame: 0, data: [0x90, padIndex({ x: row < 8 ? 0 : 4, y: row % 8 }), 100] }];
    const at: number[] = []; // how many clocks the cart had been sent when each row byte (not a NoteOff's) reached it
    let clocks = 0;
    // One second at 120 BPM: 48 host ticks, 48 / d of the cart's. Row 0 is launched at once (nothing plays yet), row 9
    // is cued in the next block, and the run goes on until the cart has had a little more than its 96 clocks.
    for (let block = 0; block < 2 * tempoDivisor + 3; block++) {
      const spec: BlockSpec = { frames: 44100, playing: true, ppq: 2 * block, tempo: 120 };
      const surface = block === 0 ? pad(0) : block === 1 ? pad(9) : [];
      const launches = controller
        .run({ ...spec, inputs: { controllerIn: surface } })
        .filter((s) => s.port === "launches")
        .map((s) => ({ frame: s.queued, data: s.data }));
      for (const [, byte] of bytesOn(cart.run({ ...spec, inputs: { launches } }), "serialIn")) {
        if (byte === MIDIMAP_CLOCK) clocks++;
        else if (byte !== MIDIMAP_NOTEOFF) at.push(clocks);
      }
    }
    return at;
  };
  for (const divisor of [1, 2, 3, 5, 6, 7, 8]) {
    expect(released(divisor, divisor), `divisor ${divisor}`).toEqual([0, 96]);
  }
  // The session left at 1 while the cart is at 2 releases the cued row at half its length.
  expect(released(1, 2)).toEqual([0, 48]);
});
