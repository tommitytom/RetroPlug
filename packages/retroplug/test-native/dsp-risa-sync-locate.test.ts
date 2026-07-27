// Where risa LANDS when the DAW moves its playhead, on the real 2.3.0 core.
//
// dsp-risa-sync-grid measures the clock (are the beats evenly spaced?) using a song whose every row is
// identical - which means a locate to the WRONG ROW is invisible to it. This measures the other half:
// after a stop-and-rewind, or a jump into the middle of the song, does risa actually sit on the row the
// arm packet named? A sync that clocks perfectly from the wrong row sounds "off" in a way that's hard to
// point at, and no timing check would catch it.
//
// The scenario at the front of this is the one a DAW does constantly and the harness couldn't express
// before `setPpq`: play, stop, rewind to the start, play again. Resuming from where you paused (ppq keeps
// advancing) was already covered; rewinding was not.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createDspRuntime } from "../src/dspRuntime";
import { createAudioDriver } from "../src/audioDriver";
import { runtime } from "../src/risa";
import { risaLocate } from "../src/risaSync";
import { buildRisaMetronomeSav } from "./risaSyncSong";

declare const __DSP_KERNEL_BUNDLE__: string;

const ROM_230 = "/workspaces/resources/roms/risa/risa-v2.3.0/risa-2.3.0-pal.nes";
const TRACK_NOISE = 3; // the metronome song's only populated track
const withSync = (id: number) => ({ systems: [{ id, pipeline: [{ kind: "risa-sync", config: {} }] }] });

test("a stop, a rewind and a mid-song jump each land risa on the row the arm named", () => {
  const be = createRealBackend();
  if (!be.fileExists(ROM_230)) {
    console.log(`# SKIP dsp-risa-sync-locate: missing ${ROM_230}`);
    return;
  }

  const dsp = createDspRuntime();
  const audio = createAudioDriver();
  const id = 1;
  expect(be.constructSystem({
    romPath: ROM_230, platform: "nes", core: "mesen", embeddedRom: "",
    savPath: null, statePath: null, sramBytes: buildRisaMetronomeSav(),
  }, id)).toBeTruthy();
  expect(dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!)).toBeTruthy();
  expect(dsp.setSystems(withSync(id))).toBeTruthy();
  audio.renderAudio(2500); // boot (transport off)
  audio.setBpm(120);

  const layout = runtime.resolveRisaLayout("2.3.0")!;

  // Locate to `ppq`, then report the row risa SETTLES on. Applying a locate is two-stage (the protocol
  // doc spells this out): FA positions the song/chain structure in main context, and only then does the
  // interrupt-owned subframe path load row floor(tt/6). So for a few ms after playback starts the row
  // still reads 0 - sampling the first playing block would catch that intermediate and call every
  // locate "row 0". Sample densely instead and take the row that PERSISTS: the located row holds for a
  // full row period (125 ms at 120 bpm) while the intermediate lasts a handful of ms.
  const playFrom = (ppq: number) => {
    audio.setPpq(ppq);
    audio.setTransport(true);
    const seen: { songRow: number; chainRow: number; phraseRow: number }[] = [];
    for (let i = 0; i < 20; i++) {
      audio.renderAudio(10);
      const s = runtime.decodeRisaState(be.readRam(id)!, layout);
      if (s.playing) {
        const t = s.tracks[TRACK_NOISE];
        seen.push({ songRow: t.songRow, chainRow: t.chainRow, phraseRow: t.phraseRow });
      }
    }
    audio.setTransport(false);
    audio.renderAudio(700); // FC + decay, so the next run starts from a stopped core

    // Collapse into runs of equal position, then drop any leading run too short to be a real row: a
    // located row holds for ~125 ms (12 samples) at 120 bpm, while the pre-prime intermediate is a
    // handful of ms. The first surviving run is where the locate actually put us.
    const runs: { pos: typeof seen[0]; n: number }[] = [];
    for (const s of seen) {
      const last = runs[runs.length - 1];
      if (last && last.pos.songRow === s.songRow && last.pos.chainRow === s.chainRow
        && last.pos.phraseRow === s.phraseRow) last.n++;
      else runs.push({ pos: s, n: 1 });
    }
    const settled = runs.find((r) => r.n >= 4);
    return settled ? settled.pos : runs[0]?.pos;
  };

  // What the protocol says the arm names for this ppq: song row, chain row, and the six-clock grid row.
  const want = (ppq: number) => {
    const loc = risaLocate(ppq);
    return { songRow: loc.songRow, chainRow: loc.chainRow, phraseRow: Math.floor(loc.tickOffset / 6) };
  };

  // Interleave rewinds with jumps elsewhere, so a "correct" result can't come from risa simply never
  // having moved. Each entry is a fresh transport start after a full stop.
  for (const ppq of [0, 0, 4, 0, 2, 0, 20, 0]) {
    const got = playFrom(ppq);
    console.log(`[risa-sync-locate] ppq=${ppq} want=${JSON.stringify(want(ppq))} got=${JSON.stringify(got)}`);
    expect(got).toEqual(want(ppq));
  }
});

test("the beat grid still holds on a run that follows a stop and rewind", () => {
  const be = createRealBackend();
  if (!be.fileExists(ROM_230)) {
    console.log(`# SKIP dsp-risa-sync-locate: missing ${ROM_230}`);
    return;
  }

  const dsp = createDspRuntime();
  const audio = createAudioDriver();
  const id = 1;
  expect(be.constructSystem({
    romPath: ROM_230, platform: "nes", core: "mesen", embeddedRom: "",
    savPath: null, statePath: null, sramBytes: buildRisaMetronomeSav(),
  }, id)).toBeTruthy();
  expect(dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!)).toBeTruthy();
  expect(dsp.setSystems(withSync(id))).toBeTruthy();
  audio.renderAudio(2500);
  audio.setBpm(120);

  const beatMs = 500;
  const runFromZero = () => {
    audio.setPpq(0);
    audio.setTransport(true);
    const pcm = audio.renderAudio(beatMs * 6);
    audio.setTransport(false);
    audio.renderAudio(700);

    // Onsets from the left channel (see dsp-risa-sync-grid for the detector's shape).
    const win = Math.round(44100 * 0.002);
    const env = new Float32Array(Math.floor(pcm.length / 2 / win));
    for (let i = 0; i < env.length; i++) {
      let s = 0;
      for (let k = 0; k < win; k++) { const v = pcm[(i * win + k) * 2]; s += v * v; }
      env[i] = Math.sqrt(s / win);
    }
    let peak = 0;
    for (const v of env) if (v > peak) peak = v;
    const open = peak * 0.25, close = peak * 0.05;
    const refractory = Math.round(beatMs * 0.5 / 1000 / (win / 44100));
    const out: number[] = [];
    let armed = true, since = refractory;
    for (let i = 0; i < env.length; i++) {
      since++;
      if (armed && env[i] > open && since > refractory) { out.push((i * win * 1000) / 44100); armed = false; since = 0; }
      else if (!armed && env[i] < close) armed = true;
    }
    return out;
  };

  const first = runFromZero();
  const second = runFromZero(); // the reported scenario: stop, rewind, play again

  const gapsOf = (on: number[]) => on.slice(2).map((t, i) => t - on[i + 1]); // skip the arm settle
  const worst = (on: number[]) => gapsOf(on).reduce((m, g) => Math.max(m, Math.abs(g - beatMs)), 0);
  console.log(`[risa-sync-locate] run1 onsets=${first.length} worst=${worst(first).toFixed(1)}ms ` +
    `run2-after-rewind onsets=${second.length} worst=${worst(second).toFixed(1)}ms`);

  // The second run must be as tight as the first: same onset count, same per-beat accuracy.
  expect(second.length).toBe(first.length);
  expect(worst(second) < 18).toBeTruthy();
  // And the two runs must agree with each other beat for beat, not merely each be self-consistent.
  for (let i = 1; i < Math.min(first.length, second.length); i++) {
    expect(Math.abs(second[i] - first[i]) < 18).toBeTruthy();
  }
});
