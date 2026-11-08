// The LSDj bridge, end to end, through the bridge's own PORTABLE code only - run by
// packages/retroplug/scripts/build-device-pack.mjs --scenario under Node and under MicroQuickJS, whose two transcripts
// must match byte for byte (pnpm test, ts/test/scenario.test.ts).
//
// The launchpad core has a scenario of its own (packages/launchpad/ts/scenario/launchpadCore.ts): the surface, the
// decoder, the session and the scan. This one walks what the BRIDGE adds - the MI.MAP app, the predictor it reads and
// the launch target - so a path neither a node nor that scenario runs is still proven where the box would run it: every
// quantise mode, the predictor's edit and re-anchor paths, a song built from a decoded song rather than a table, and the
// launches the target sends to the cart.
//
// It imports the package's own modules and its declared dependencies only (launchpad and lsdj), which the scenario mode
// allows and a node pack's bundle does not.
import { PRO_MK3, Palette, palette, type Led } from "@retroplug/launchpad";
import type { TrackerTarget } from "@retroplug/launchpad/launchTarget";
import { ControllerSession, type ControllerApp } from "@retroplug/launchpad/session";
import { PredictedLsdjModel, normaliseRowTicks, songRowTicks } from "@retroplug/lsdj/playback/predict";
import type { Song } from "@retroplug/lsdj/model";
import runtime from "../appRuntime";
import { lsdjMidiMap, MAX_PAGE, QUANTISE_VALUES, rowAt, channelAt, followRow } from "../lsdjMidiMap";
import { launchMessage, lsdjMidiMapTarget, LSDJ_MAX_ROW } from "../launchTarget";
import { nullTarget } from "@retroplug/launchpad/launchTarget";

const out = (line: string): void => console.log(line);
const emit = (what: string, data: unknown): void => out(what + " " + JSON.stringify(data));

// --- the app over a predictor -------------------------------------------------------------------------------------

// Rows 0..5 hold one phrase (96 ticks) on pu1/pu2, rows 0..3 on wav; noi is empty. Enough to see the playhead move, a
// row end, and channels diverge.
const table: (number | null)[][] = [];
for (let ch = 0; ch < 4; ch++) {
  const rows: (number | null)[] = [];
  const filled = ch < 2 ? 6 : ch === 2 ? 4 : 0;
  for (let r = 0; r < 256; r++) rows.push(r < filled ? 96 : null);
  table.push(rows);
}

const launches: number[][] = [];
const model = runtime.playbackModel(table);
const session = new ControllerSession(runtime.factory(), {
  playback: model,
  target: runtime.target((d) => launches.push(d)),
  config: { quantise: "bar", follow: true, page: 0 },
});

const pad = (x: number, y: number, velocity: number): number[] => [0x90, (7 - y) * 10 + x + 11, velocity];
const cc = (n: number, value: number): number[] => [0xb0, n, value];

emit("connect", session.connect());

interface Block {
  input: number[][];
  tick: number;
  transport: boolean;
  /** The ticks the host clocked in the previous block, as walkTicks counted them. */
  ticks: number;
}
const blocks: Block[] = [
  { input: [], tick: 0, transport: false, ticks: 0 },
  { input: [pad(0, 2, 127), pad(0, 2, 0)], tick: 0, transport: false, ticks: 0 }, // stopped: launches at once
];
for (let tick = 0; tick <= 480; tick += 12) {
  const input: number[][] = [];
  if (tick === 108) input.push(pad(1, 4, 100)); // playing: waits for the bar
  if (tick === 300) input.push(cc(PRO_MK3.buttons.up, 127), cc(PRO_MK3.buttons.up, 0));
  if (tick === 312) input.push(cc(PRO_MK3.buttons.down, 127));
  if (tick === 396) input.push(pad(6, 1, 90)); // row 9, on the right-hand pane
  blocks.push({ input, tick, transport: true, ticks: 12 });
}
blocks.push({ input: [], tick: 4000, transport: true, ticks: 0 }); // a seek: the cart was sent no clocks
blocks.push({ input: [], tick: 480, transport: false, ticks: 0 });

for (let i = 0; i < blocks.length; i++) {
  launches.length = 0;
  const leds = session.update(blocks[i]);
  if (leds.length > 0 || launches.length > 0) emit("block " + i, { leds, launches: launches.slice() });
}
emit("position", model.position());
emit("grid", [model.grid().rowCount, model.grid().channelCount, model.grid().hasContent(0, 5),
  model.grid().hasContent(0, 6), model.grid().hasContent(3, 0)]);
emit("disconnect", session.disconnect());

// --- the app's own helpers ------------------------------------------------------------------------------------------

emit("layout", [rowAt(0, 0, 0), rowAt(0, 4, 0), rowAt(0, 3, 7), rowAt(16, 7, 7), channelAt(0), channelAt(3), channelAt(4)]);
emit("follow", [
  followRow({ playing: true, channels: [{ playing: false, songRow: null }, { playing: true, songRow: 3 }] }),
  followRow({ playing: false, channels: [{ playing: false, songRow: null }] }),
  MAX_PAGE,
  QUANTISE_VALUES.length,
]);
const app: ControllerApp = lsdjMidiMap;
emit("app", typeof app);

// --- the predictor's edit and re-anchor paths -----------------------------------------------------------------------

const edited = normaliseRowTicks(table);
edited[0][1] = null; // pu1 row 1 loses its chain
edited[1][1] = 24; // pu2 row 1's chain gets shorter than the ticks it has already played
const p = PredictedLsdjModel.fromRowTicks(normaliseRowTicks(table));
p.launch(1);
p.advance(150);
emit("model playing", p.position());
p.setRowTicks(edited);
emit("model edited", p.position());
p.anchorTo([3, null, 2, null]);
emit("model anchored", p.position());
p.advance(500);
emit("model advanced", p.position());
p.stop();
emit("model stopped", p.position());
p.reset();
emit("model reset", p.position());
emit("normalised junk", normaliseRowTicks({ not: "a table" })[0].slice(0, 3));

// A song built from a decoded song rather than a table - the arithmetic the control plane runs (songRowTicks): a custom
// groove, a chain ending at a hole, and a groove of zeroes that must fall back.
const rows: { chains: (number | null)[] }[] = [];
for (let r = 0; r < 8; r++) rows.push({ chains: [r < 4 ? 0 : null, r < 2 ? 1 : null, null, r === 0 ? 2 : null] });
const song = {
  rows,
  chains: [{ phrases: [0, 1, null, 3] }, { phrases: [5] }, { phrases: [null, 4] }],
  grooves: [{ steps: [4, 8, 0, 6] }],
} as unknown as Song;
const fromSong = songRowTicks(song);
emit("song ticks", [fromSong[0][0], fromSong[1][1], fromSong[3][0], fromSong[0][5]]);
const zero = songRowTicks({ ...song, grooves: [{ steps: [0, 0] }] } as Song);
emit("zero groove", zero[0][0]);

// --- the target ---------------------------------------------------------------------------------------------------

const release: number[][] = [];
const target: TrackerTarget = lsdjMidiMapTarget((d) => release.push(d));
target.release(5);
target.release(200);
target.launch(300); // past the last addressable row: dropped rather than wrapping
target.launch(LSDJ_MAX_ROW);
emit("target release", release);
emit("launch encoding", [launchMessage(0), launchMessage(127), launchMessage(128), launchMessage(LSDJ_MAX_ROW),
  launchMessage(254), launchMessage(-1), launchMessage(1.5), launchMessage(3, false)]);
emit("null target", [nullTarget(LSDJ_MAX_ROW).maxRow, typeof nullTarget(LSDJ_MAX_ROW).launch,
  typeof nullTarget(LSDJ_MAX_ROW).release]);

// --- every quantise mode, through a session with no cart attached -----------------------------------------------------

const paletteLed: Led = { mode: "static", colour: palette(Palette.greenBright) };
emit("led", paletteLed);
for (const quantise of QUANTISE_VALUES) {
  const sent: number[][] = [];
  const s = new ControllerSession(lsdjMidiMap, {
    playback: runtime.playbackModel(table),
    target: lsdjMidiMapTarget((d) => sent.push(d)),
    config: { quantise, follow: false, page: MAX_PAGE },
  });
  s.connect();
  let leds = 0;
  for (let tick = 0; tick <= 300; tick += 6) {
    const input: number[][] = [];
    if (tick === 0) input.push(cc(PRO_MK3.buttons.up, 127)); // page up from the last page
    if (tick === 6) input.push(pad(0, 0, 127)); // first launch: immediate whatever the mode
    if (tick === 30) input.push(pad(0, 3, 127)); // second launch: quantised
    if (tick === 60) input.push(cc(PRO_MK3.buttons.session, 127)); // session button: follow on
    if (tick === 90) input.push(cc(PRO_MK3.buttons.down, 127), cc(PRO_MK3.buttons.down, 127));
    if (tick === 120) input.push(pad(7, 7, 127)); // the window's last row
    leds += s.update({ input, tick, transport: tick > 0, ticks: tick > 0 ? 6 : 0 }).length;
  }
  emit("quantise " + quantise, { launches: sent, ledMessages: leds });
}
const detached = new ControllerSession(lsdjMidiMap, {
  playback: runtime.playbackModel(table), target: nullTarget(LSDJ_MAX_ROW),
});
emit("null target session", detached.update({ input: [pad(0, 0, 127)], tick: 0, transport: false, ticks: 0 }).length);
