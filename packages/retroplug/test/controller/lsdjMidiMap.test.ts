// The LSDj MI.MAP app: does the grid read like a song screen, and does a pad launch the right row at the
// right moment?
//
// Everything about the LEDs is asserted by reading them back OFF THE FAKE DEVICE rather than by
// inspecting the app's intentions, so a test fails when the hardware would end up wrong.
import { test, expect } from "../../testing/harness";
import { FakeLaunchpad } from "./_fakeDevice";
import {
  ControllerSession, lsdjMidiMap, lsdjMidiMapTarget, rowAt, channelAt, followRow,
  WINDOW_ROWS, MAX_PAGE,
} from "../../src/controller";
import { Palette, palette, type Led } from "../../src/launchpad";
import { SongSchema, type Song } from "../../src/lsdj/model";
import { PredictedLsdjModel } from "../../src/lsdj/playback";
import { idlePosition, type PlaybackModel, type PlaybackPosition } from "../../src/tracker/playbackModel";

const CONTENT: Led = { mode: "static", colour: palette(Palette.greenDim) };
const PLAYHEAD: Led = { mode: "static", colour: palette(Palette.greenBright) };
const CUED: Led = { mode: "pulse", colour: palette(Palette.yellow) };
const OFF: Led = { mode: "off" };

/** A song from a per-row chain table, as test/lsdj/playback.test.ts builds them. */
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
  run(opts?: { tick?: number; transport?: boolean; input?: number[][] }): void;
}

function harness(playback: PlaybackModel, config: Record<string, unknown> = {}): Harness {
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
