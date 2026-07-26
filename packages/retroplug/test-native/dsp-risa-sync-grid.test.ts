// Host sync holds risa on the DAW's beat grid, measured from the rendered AUDIO rather than from bytes.
// The song is the one-hit-per-beat metronome (risaSyncSong): risa's row grid is a fixed six clocks of
// 24 PPQN, so four rows is one quarter note whatever the tempo, and a hit every fourth row must land on
// every beat. Detecting those onsets and checking their spacing proves the whole chain end to end -
// role, FIFO, receive path, sequencer - in a way an RMS check can't.
//
// This is the headless twin of the real-Reaper `reaper:risa-sync` leg, which renders the SAME song
// through an actual DAW transport and pairs each onset to a ReaSynth click. This one runs in test:native.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createDspRuntime } from "../src/dspRuntime";
import { createAudioDriver } from "../src/audioDriver";
import { buildRisaMetronomeSav } from "./risaSyncSong";

declare const __DSP_KERNEL_BUNDLE__: string;

const ROM_230 = "/workspaces/resources/roms/risa/risa-v2.3.0/risa-2.3.0-pal.nes";
const SAMPLE_RATE = 44100;

const withSync = (id: number) => ({ systems: [{ id, pipeline: [{ kind: "risa-sync", config: {} }] }] });

// Onsets as a rising-edge crossing of a short-window envelope. The song emits at most one hit per beat,
// so `refractoryMs` (half a beat) is what keeps a decaying noise burst - whose RMS ripples on the way
// down - from registering as several onsets.
function onsetsMs(pcm: Float32Array, sampleRate: number, refractoryMs: number): number[] {
  const win = Math.round(sampleRate * 0.002); // 2 ms RMS window
  const env = new Float32Array(Math.floor(pcm.length / win));
  for (let i = 0; i < env.length; i++) {
    let s = 0;
    for (let k = 0; k < win; k++) s += pcm[i * win + k] * pcm[i * win + k];
    env[i] = Math.sqrt(s / win);
  }
  let peak = 0;
  for (const v of env) if (v > peak) peak = v;
  const open = peak * 0.25;
  const close = peak * 0.05; // must decay most of the way before another onset can arm
  const refractory = Math.round(refractoryMs / 1000 / (win / sampleRate));

  const out: number[] = [];
  let armed = true;
  let since = refractory;
  for (let i = 0; i < env.length; i++) {
    since++;
    if (armed && env[i] > open && since > refractory) {
      out.push((i * win * 1000) / sampleRate);
      armed = false;
      since = 0;
    } else if (!armed && env[i] < close) {
      armed = true;
    }
  }
  return out;
}

test("host sync puts one risa hit on every DAW beat, at the DAW's tempo", () => {
  const be = createRealBackend();
  if (!be.fileExists(ROM_230)) {
    console.log(`# SKIP dsp-risa-sync-grid: missing ${ROM_230}`);
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
  audio.renderAudio(2500); // boot (transport off) — risa materializes the authored song

  // The DAW tempo alone sets the hit rate: risa's project tempo never enters into it under host sync.
  for (const bpm of [120, 90]) {
    const beatMs = 60000 / bpm;
    audio.setBpm(bpm);
    audio.setTransport(true);
    const pcm = audio.renderAudio(Math.round(beatMs * 8)); // 8 beats
    audio.setTransport(false);
    audio.renderAudio(400); // let the tail decay before the next pass

    // The render is interleaved stereo; the noise hit is identical in both, so take the left channel.
    const left = new Float32Array(Math.floor(pcm.length / 2));
    for (let i = 0; i < left.length; i++) left[i] = pcm[i * 2];

    const onsets = onsetsMs(left, SAMPLE_RATE, beatMs * 0.5);
    // The FIRST hit is the arm settling, not a locked beat: risa applies the locate during guarded
    // subframe service and primes the row there, which costs tens of ms once. Steady-state lock is what
    // the sync has to deliver, so measure from the second hit on. (The equivalent split exists in the
    // LSDj analyzer, which has a whole separate mode for startup latency.)
    const locked = onsets.slice(1);
    const gaps = locked.slice(1).map((t, i) => t - locked[i]);
    const worst = gaps.reduce((m, g) => Math.max(m, Math.abs(g - beatMs)), 0);
    // Cumulative error across the whole locked run: catches slow drift that per-gap error would hide.
    const span = locked.length > 1 ? locked[locked.length - 1] - locked[0] : 0;
    const drift = span - beatMs * (locked.length - 1);
    console.log(
      `[risa-sync-grid] ${bpm} bpm: ${onsets.length} onsets (first at ${onsets[0].toFixed(1)}ms), ` +
        `beat=${beatMs.toFixed(1)}ms worst gap error=${worst.toFixed(1)}ms drift over ` +
        `${locked.length - 1} beats=${drift.toFixed(1)}ms`,
    );

    // At least 6 of the 8 beats (one goes to the arm settle, the last can fall past the render end).
    expect(onsets.length >= 6).toBeTruthy();
    // Each beat within one risa service point: the ROM executes clocks at safe subframe boundaries,
    // ~8.7 ms worst case on PAL, and two adjacent hits can be off in opposite directions.
    expect(worst < 18).toBeTruthy();
    // And no accumulation: the run must not walk off the grid, which is what a mis-clocked sync does.
    expect(Math.abs(drift) < 10).toBeTruthy();
  }
});
