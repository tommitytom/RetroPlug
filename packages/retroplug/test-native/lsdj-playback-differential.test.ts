// How well does dead reckoning track a real cart?
//
// This is the honest measure of the hardware path. On a real Game Boy over an Arduinoboy there are no
// memory reads and MI.MAP has no return channel, so the Launchpad surface has to light rows from a
// SIMULATION of the cart (PredictedLsdjModel). This test runs that simulation and a real emulated cart
// off the same clock and reports how far apart they get.
//
// It drives the wire protocol directly (the passthrough role) rather than letting the `midiMap` role
// generate the clock, so one 0xFF byte written here is exactly one tick fed to the model. That isolates
// the MODEL's accuracy from the role's clock-generation timing, which is a separate concern with its own
// test (lsdj-midimap.test.ts) and its own DAW-level drift renders.
//
// Run: pnpm test:native lsdj-playback-differential
import { test, expect } from "../testing/harness";
import { type SavInput } from "../src/lsdjSav";
import { SongSchema } from "../src/lsdj/model";
import { CHANNELS } from "../src/lsdj/runtime";
import { PredictedLsdjModel } from "../src/lsdj/playback";
import { LsdjProbe, type ProbeSample } from "./lsdjPlaybackProbe";

const pulse = { type: "pulse", panning: "LeftRight", adsr: { initialLevel: 8, attackSpeed: 8 } } as const;
const phrase = (note: number, inst: number) => ({
  notes: Array.from({ length: 16 }, () => note),
  instruments: Array.from({ length: 16 }, () => inst),
});

// Deliberately NOT a uniform song: pu2's row-0 chain is two phrases against everyone else's one, so the
// channels diverge and the model has to track four independent cursors rather than one shared row.
const ROWS = [
  { chains: [0, 1, 2, 3] },
  { chains: [4, 5, 6, 7] },
  { chains: [8, 9, 10, 11] },
];
const CHAINS = [
  { phrases: [0] }, { phrases: [1, 1] }, { phrases: [2] }, { phrases: [3] },
  { phrases: [0] }, { phrases: [1] }, { phrases: [2] }, { phrases: [3] },
  { phrases: [0] }, { phrases: [1] }, { phrases: [2] }, { phrases: [3] },
];
const SONG_INPUT = {
  formatVersion: 22,
  settings: { syncMode: "MidiMap" as const, tempo: 128 },
  rows: ROWS,
  chains: CHAINS,
  phrases: [phrase(1, 0), phrase(2, 1), phrase(3, 2), phrase(4, 3)],
  instruments: [pulse, pulse, { type: "wave" as const }, { type: "noise" as const }],
};
const SAV: SavInput = { workingSong: SONG_INPUT };

const TICKS = 600; // ~6 chains' worth at 96 ticks/row — several wraps, and time for divergence to show

/** Per-channel rows the model predicts after each of `ticks` ticks, starting from a launch of `row`. */
function predictedTimeline(row: number, ticks: number): (number | null)[][] {
  const model = new PredictedLsdjModel(SongSchema.parse(SONG_INPUT));
  model.launch(row);
  const out: (number | null)[][] = [];
  for (let t = 0; t < ticks; t++) {
    model.advance(1);
    out.push(model.position().channels.map((c) => c.songRow));
  }
  return out;
}

/** Fraction of (sample, channel) pairs where predicted and observed rows agree, sliding the prediction
 *  by `offset` ticks. Samples where the cart reports nothing are skipped rather than counted as
 *  disagreement — a null readout is the reader having no opinion, not the model being wrong. */
function agreement(observed: ProbeSample[], predicted: (number | null)[][], offset: number): { pct: number; compared: number; firstDiff: number } {
  let hits = 0;
  let compared = 0;
  let firstDiff = -1;
  for (let i = 0; i < observed.length; i++) {
    const p = predicted[i + offset];
    if (!p) continue;
    for (let ch = 0; ch < CHANNELS.length; ch++) {
      const obs = observed[i].channels[CHANNELS[ch]].songRow;
      if (obs === null) continue;
      compared++;
      if (obs === p[ch]) hits++;
      else if (firstDiff < 0) firstDiff = observed[i].ticks;
    }
  }
  return { pct: compared ? (100 * hits) / compared : 0, compared, firstDiff };
}

test("the predicted model tracks a real cart's song rows over a long run", () => {
  const p = LsdjProbe.create({ song: SAV });
  if (!p) return console.log("# SKIP lsdj-playback-differential: aboy ROM not found / unsupported version");

  p.launchRaw(0);
  const observed = p.runTicks(TICKS);
  const predicted = predictedTimeline(0, TICKS + 8);

  // The sweep stays even though the model now self-aligns: it is what discovered that the cart spends
  // the launch byte as its first tick (the peak sat at +1 before the model accounted for it), and it is
  // the thing that would catch the alignment silently moving again.
  console.log("[differential] alignment sweep (offset -> agreement%):");
  let best = { offset: 0, pct: -1, compared: 0, firstDiff: -1 };
  for (let offset = 0; offset <= 6; offset++) {
    const a = agreement(observed, predicted, offset);
    console.log(`  offset +${offset}: ${a.pct.toFixed(1)}% over ${a.compared} comparisons (first divergence at tick ${a.firstDiff})`);
    if (a.pct > best.pct) best = { offset, ...a };
  }

  console.log(`[differential] BEST: +${best.offset} ticks, ${best.pct.toFixed(1)}% agreement over ${best.compared} comparisons`);

  // Per-channel breakdown at the best offset, so a single bad channel cannot hide behind a good average.
  for (let ch = 0; ch < CHANNELS.length; ch++) {
    let hits = 0;
    let n = 0;
    for (let i = 0; i < observed.length; i++) {
      const pred = predicted[i + best.offset];
      const obs = observed[i].channels[CHANNELS[ch]].songRow;
      if (!pred || obs === null) continue;
      n++;
      if (obs === pred[ch]) hits++;
    }
    console.log(`  ${CHANNELS[ch]}: ${n ? ((100 * hits) / n).toFixed(1) : "n/a"}% over ${n}`);
  }

  expect(best.compared > 1000).toBeTruthy(); // the run actually produced data to compare

  // MEASURED BOUND, set from real runs rather than guessed up front. The first run scored 100.0% at
  // offset +1; folding that launch tick into the model moved the peak to 0. So the row-level model is
  // EXACT against a real cart over 600 ticks and several song wraps, including channels diverging - and
  // the bound is set just under that to leave room for sampling landing either side of a crossing,
  // while still failing loudly if the model stops tracking.
  expect(best.offset).toBe(0); // self-aligned: no residual constant lead or lag
  expect(best.pct > 99).toBeTruthy();
});
