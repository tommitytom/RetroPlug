// Long-run host-sync drift for smsggdj: the headless twin of `pnpm reaper:sms-sync`.
//
// The Reaper leg is the only check that the plugin tracks a REAL DAW transport, but it needs a Reaper
// install, it is not in CI, and its only observable is the rendered audio. This runs the same 30 s at
// 120 BPM against the real core and the real DSP kernel, and measures the two things that render
// cannot separate:
//
//   1. THE ROM'S OWN ROW COUNTER, polled every 256 frames (5.8 ms - finer than the ~16.7 ms video
//      frame smsggdj samples its sync lines on). This is ground truth: no onset detection anywhere
//      near it, so it says what the sequencer actually did.
//   2. THE RENDERED AUDIO, run through the same onset rule tools/reaper-timing-analyze.py uses.
//
// Both matter, because the first version of this measurement was wrong in a way only the pair catches.
// The Reaper render reported a 97 ms sawtooth and this twin reproduced it at 91 ms - but the row
// counter over that very same audio was flat to +/-10 ms. The sawtooth was the fixture's ringing note
// re-crossing the detector's threshold just inside its 100 ms min-spacing window, which discards the
// real onset in favour of a spurious one ~95 ms early. Nothing was wrong with the sync at all. So the
// audio assertion here is not a duplicate of the row-counter one: it is what stops the fixture drifting
// back into a shape that its own measurement cannot read (see smsSyncSong.ts's KILL_CMD).
//
// What the row counter is allowed to do: wobble by up to one video frame. IN24 puts a row every 125 ms
// and the ROM consumes clocks once per frame, so at NTSC a row lands after either 7 or 8 frames
// (116.1 / 133.5 ms) either side of the ideal. That alternation is inherent to the protocol - the DAW
// cannot place a row between frames. What it must NOT do is accumulate, which is what the mean spacing
// and the grid-anchored residual below actually test.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createDspRuntime } from "../src/dspRuntime";
import { createAudioDriver } from "../src/audioDriver";
import { encodeWav } from "../src/render/wav";
import {
  buildSmsMetronomeSav,
  pokeMetronomeIntoWram,
  SMS_SYNC_IN24,
  SMS_ROWS_PER_BEAT,
} from "./smsSyncSong";

declare const __DSP_KERNEL_BUNDLE__: string;
declare const __REPO_RESOURCES_DIR__: string;

const ROM = __REPO_RESOURCES_DIR__ + "/roms/smsggdj_v0_45.sms";
/** Written unconditionally: it is the artifact that makes a failure diagnosable, and it is the exact
 *  stereo shape tools/reaper-timing-analyze.py --drift expects, so the Reaper analyzer runs on it. */
const OUT_WAV = "/tmp/headless-sms-sync.wav";

const PAUSE = 7;
const BPM = 120;
const SECONDS = 30;
const POLL_FRAMES = 256;
const PHRASE_STEP = 0x1b02;
const PHRASE_STEPS = 16;

/** One NTSC video frame. The ROM samples its sync lines once per frame, so this is the floor on how
 *  precisely a row can be placed - not a tolerance we could tighten by trying harder. */
const FRAME_MS = 1000 / 59.92;
/** Grid-anchored residual budget: two video frames. A row alternates between landing 7 and 8 frames
 *  after the last, so it swings +/-half a frame around the grid, and row 0 - the anchor - can itself sit
 *  anywhere within a frame; one frame of each is the floor. Measured 13.2 - 19.6 ms across runs, so
 *  this leaves real headroom while staying an order of magnitude under any true accumulation. */
const DRIFT_TOLERANCE_MS = 2 * FRAME_MS;

// --- the analyzer's onset rule, ported (tools/reaper-timing-analyze.py:onsets_from_envelope) ---
const ENV_RATE_HZ = 4000;
const ENV_SMOOTH_MS = 30;
const THRESHOLD_FRAC = 0.2;

/** Peak-decimate to ~4 kHz, box-smooth, then return every rising edge through 20% of the peak. The
 *  min-spacing pass the analyzer applies afterwards is deliberately NOT ported: the edges BEFORE it are
 *  the diagnostic, because min-spacing is what silently swapped a real onset for a spurious one. */
function risingEdges(mono: Float32Array, sampleRate: number): number[] {
  const decim = Math.max(1, Math.round(sampleRate / ENV_RATE_HZ));
  const envRate = sampleRate / decim;
  const n = Math.floor(mono.length / decim);
  const env = new Float32Array(n);
  for (let i = 0; i < n; i++) {
    let m = 0;
    for (let j = i * decim; j < (i + 1) * decim; j++) m = Math.max(m, Math.abs(mono[j]));
    env[i] = m;
  }
  const w = Math.max(1, Math.round((envRate * ENV_SMOOTH_MS) / 1000));
  const sm = new Float32Array(n); // running sum: a naive convolution here is minutes of QuickJS
  let acc = 0;
  for (let i = 0; i < n; i++) {
    acc += env[i];
    if (i >= w) acc -= env[i - w];
    sm[Math.max(0, i - (w >> 1))] = acc / Math.min(i + 1, w);
  }
  let peak = 0;
  for (let i = 0; i < n; i++) peak = Math.max(peak, sm[i]);
  const thr = THRESHOLD_FRAC * peak;
  const edges: number[] = [];
  let above = false;
  for (let i = 0; i < n; i++) {
    const now = sm[i] > thr;
    if (now && !above) edges.push((i / envRate) * 1000);
    above = now;
  }
  return edges;
}

test("the DAW transport holds smsggdj on the grid for 30s, with no accumulation", () => {
  const be = createRealBackend();
  if (!be.fileExists(ROM)) {
    console.log(`# SKIP dsp-sms-sync-drift: missing ${ROM}`);
    return;
  }
  const audio = createAudioDriver();
  const sr = audio.sampleRate();

  expect(
    be.constructSystem(
      {
        romPath: ROM,
        platform: "sms",
        core: "mesen",
        embeddedRom: "",
        savPath: null,
        statePath: null,
        sramBytes: buildSmsMetronomeSav(SMS_SYNC_IN24),
        settings: JSON.stringify({ enableFm: false }),
      },
      1,
    ),
  ).toBeTruthy();
  audio.renderAudio(3000); // boot: splash, config_load (takes IN24 + fm off), song_new
  expect(pokeMetronomeIntoWram(be, 1) > 0).toBeTruthy();

  const dsp = createDspRuntime();
  expect(dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!)).toBeTruthy();
  expect(dsp.setSystems({ systems: [{ id: 1, pipeline: [{ kind: "sms-sync", config: {} }] }] })).toBeTruthy();

  // Arm: in IN24 this parks the ROM in WAIT holding for the first host clock.
  audio.pressButton(1, PAUSE, true);
  audio.renderAudio(100);
  audio.pressButton(1, PAUSE, false);
  audio.renderAudio(100);

  audio.setBpm(BPM);
  audio.setPpq(0);
  audio.setTransport(true);

  // Sample 0 of the first block below is ppq 0, so the beat grid is analytic from here.
  const pollMs = (POLL_FRAMES / sr) * 1000;
  const totalFrames = Math.round(SECONDS * sr);
  const left = new Float32Array(totalFrames);
  const rowAt: number[] = []; // absolute row -> frame at which its advance was observed
  let frame = 0;
  let prevStep = be.readRam(1)![PHRASE_STEP];

  while (frame < totalFrames) {
    const buf = audio.renderAudio(pollMs);
    const frames = buf.length / 2;
    for (let f = 0; f < frames && frame + f < totalFrames; f++) left[frame + f] = buf[f * 2];
    frame += frames;
    const step = be.readRam(1)![PHRASE_STEP];
    let delta = (step - prevStep + PHRASE_STEPS) % PHRASE_STEPS;
    prevStep = step;
    while (delta-- > 0) rowAt.push(frame);
  }

  // --- 1. the row counter ---
  const framesPerRow = (sr * 60) / (BPM * SMS_ROWS_PER_BEAT);
  const expectedRows = Math.floor(totalFrames / framesPerRow);
  expect(Math.abs(rowAt.length - expectedRows) <= 2).toBeTruthy(); // right tempo, nothing dropped

  const t0 = rowAt[0];
  let peakResid = 0;
  for (let r = 0; r < rowAt.length; r++) {
    const resid = ((rowAt[r] - t0 - r * framesPerRow) / sr) * 1000;
    peakResid = Math.max(peakResid, Math.abs(resid));
  }
  let minGap = Infinity;
  let maxGap = 0;
  for (let r = 1; r < rowAt.length; r++) {
    const g = ((rowAt[r] - rowAt[r - 1]) / sr) * 1000;
    minGap = Math.min(minGap, g);
    maxGap = Math.max(maxGap, g);
  }
  const meanGap = ((rowAt[rowAt.length - 1] - t0) / sr) * 1000 / (rowAt.length - 1);
  const idealGap = (framesPerRow / sr) * 1000;
  console.log(
    `[sms-drift] ${rowAt.length} rows, residual peak ${peakResid.toFixed(2)} ms, ` +
      `gap mean ${meanGap.toFixed(2)} min ${minGap.toFixed(2)} max ${maxGap.toFixed(2)} (ideal ${idealGap.toFixed(2)})`,
  );
  // The whole claim, and it is an ACCUMULATION claim: anchored on row 0, no row over 30 s strays
  // further than a video frame from where the DAW put it.
  expect(peakResid < DRIFT_TOLERANCE_MS).toBeTruthy();
  // Mean spacing pins the tempo independently of the residual - a role clocking at the wrong PPQN
  // would ramp the residual, but this says the rate itself is right to well under a frame.
  expect(Math.abs(meanGap - idealGap) < 1).toBeTruthy();
  // ...and the wobble really is only the frame grid, not something larger hiding under the mean.
  expect(maxGap - minGap < 2.5 * FRAME_MS).toBeTruthy();

  // --- 2. the audio, under the analyzer's own onset rule ---
  const beats = Math.floor((SECONDS * BPM) / 60);
  const edges = risingEdges(left, sr);
  console.log(`[sms-drift] ${edges.length} rising edges for ${beats} beats`);
  // Exactly one edge per beat. More means the note is ringing into the next hit and the drift render
  // is measuring its own detector rather than the sync - which is precisely how this fixture failed.
  expect(edges.length).toBe(beats);

  const pcm = new Float32Array(totalFrames * 2);
  for (let f = 0; f < totalFrames; f++) pcm[f * 2] = left[f];
  const framesPerBeat = (sr * 60) / BPM;
  const clickFrames = Math.round(sr * 0.02);
  for (let b = 0; Math.round(b * framesPerBeat) + clickFrames < totalFrames; b++) {
    const start = Math.round(b * framesPerBeat);
    for (let i = 0; i < clickFrames; i++)
      pcm[(start + i) * 2 + 1] = 0.6 * Math.exp(-i / (sr * 0.004)) * Math.sin((2 * Math.PI * 1000 * i) / sr);
  }
  expect(be.writeFile(OUT_WAV, encodeWav(pcm, sr, 2))).toBeTruthy();
  console.log(`[sms-drift] wrote ${OUT_WAV} (analyze: tools/reaper-timing-analyze.py ${OUT_WAV} --drift)`);

  expect(be.removeSystem(1)).toBeTruthy();
});
