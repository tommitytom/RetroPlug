// `retroplug-cli analyze-capture` - measure a multi-channel WAV captured off real hardware.
//
// The black-box half of a hardware check: `n8-play` drives the console, an external recorder captures its
// analog output, and this reports what actually came back - per-channel level, fundamental (detectPitch),
// a short-time envelope, and a band split. The envelope is the point for chips with a HARDWARE volume
// envelope (Sunsoft 5B): a flat tone and a pulsing one have the same RMS, and only the envelope swing tells
// them apart. See the nes-hardware-lab skill for the capture side.
import type { CliTool } from "../tools";
import type { Session } from "../session";
import { decodeWav } from "../wav";
import { detectPitch, centsError } from "../pitch";
import { magnitudeSpectrum } from "../dsp";
import { bandEnergyDb } from "../spectral-metrics";

const flag = (args: string[], name: string): string | undefined => {
  const i = args.indexOf(name);
  return i >= 0 ? args[i + 1] : undefined;
};

const db = (x: number): number => 20 * Math.log10(Math.max(x, 1e-12));
const pad = (x: number, w = 8, p = 2): string => (Number.isFinite(x) ? x.toFixed(p) : "n/a").padStart(w);

/** Pull one 1-based channel out of an interleaved buffer, DC-removed (an AC-coupled capture still drifts). */
export function channelOf(pcm: Float32Array, channels: number, ch1: number): Float32Array {
  const n = Math.floor(pcm.length / channels);
  const out = new Float32Array(n);
  for (let i = 0; i < n; i++) out[i] = pcm[i * channels + (ch1 - 1)];
  let mean = 0;
  for (let i = 0; i < n; i++) mean += out[i];
  mean /= Math.max(n, 1);
  for (let i = 0; i < n; i++) out[i] -= mean;
  return out;
}

export function rms(x: Float32Array): number {
  let s = 0;
  for (let i = 0; i < x.length; i++) s += x[i] * x[i];
  return Math.sqrt(s / Math.max(x.length, 1));
}

export function peak(x: Float32Array): number {
  let m = 0;
  for (let i = 0; i < x.length; i++) m = Math.max(m, Math.abs(x[i]));
  return m;
}

/** Short-time RMS in dBFS, one value per `hopMs` (window = 2 hops). */
export function envelopeDb(x: Float32Array, sampleRate: number, hopMs = 10): number[] {
  const hop = Math.max(1, Math.floor((sampleRate * hopMs) / 1000));
  const win = hop * 2;
  const out: number[] = [];
  for (let i = 0; i + win <= x.length; i += hop) out.push(db(rms(x.subarray(i, i + win))));
  return out;
}

/** Linear (not dB) short-time RMS, for measuring how FAST an envelope repeats. The window is decoupled from
 *  the hop and deliberately long (default 20 ms): a window of only a cycle or two of the CARRIER leaves the
 *  carrier rippling in the envelope, which then aliases and swamps the modulation peak. 20 ms averages away
 *  anything above ~50 Hz while the 1 ms hop keeps the envelope finely sampled. */
export function envelopeLinear(
  x: Float32Array,
  sampleRate: number,
  hopMs: number,
  winMs = 20,
): { env: Float32Array; rate: number } {
  const hop = Math.max(1, Math.floor((sampleRate * hopMs) / 1000));
  const win = Math.max(hop * 2, Math.floor((sampleRate * winMs) / 1000));
  const n = Math.max(0, Math.floor((x.length - win) / hop) + 1);
  const env = new Float32Array(n);
  for (let i = 0; i < n; i++) env[i] = rms(x.subarray(i * hop, i * hop + win));
  let mean = 0;
  for (let i = 0; i < n; i++) mean += env[i];
  mean /= Math.max(n, 1);
  for (let i = 0; i < n; i++) env[i] -= mean; // the modulation, without the carrier level
  return { env, rate: sampleRate / hop };
}

/** Envelope stats over the SOUNDING part only, so leading/trailing silence can't fake a big swing. */
export function envelopeStats(env: number[], floorDb: number): { n: number; min: number; max: number; swing: number } {
  const v = env.filter((e) => e > floorDb);
  if (v.length === 0) return { n: 0, min: NaN, max: NaN, swing: NaN };
  const min = Math.min(...v);
  const max = Math.max(...v);
  return { n: v.length, min, max, swing: max - min };
}

function run(s: Session, args: string[]): void {
  const path = args.find((a) => !a.startsWith("--") && /\.wav$/i.test(a));
  if (!path) throw new Error("usage: retroplug-cli analyze-capture <capture.wav> [options]");

  const bytes = s.backend.readFile(path);
  if (!bytes) throw new Error(`cannot read ${path}`);
  const { sampleRate, channels, pcm } = decodeWav(bytes);
  const frames = Math.floor(pcm.length / channels);
  console.log(`${path}: ${frames} frames, ${channels} ch, ${sampleRate} Hz (${(frames / sampleRate).toFixed(2)} s)`);

  const floorDb = Number(flag(args, "--floor") ?? -70);
  const expectHz = flag(args, "--expect-hz") !== undefined ? Number(flag(args, "--expect-hz")) : undefined;
  const chArg = flag(args, "--channel");

  // No --channel: a level survey of every channel, to find which one the console is actually on.
  if (chArg === undefined) {
    console.log("  ch     rms dBFS    peak dBFS");
    for (let c = 1; c <= channels; c++) {
      const x = channelOf(pcm, channels, c);
      console.log(`  ${String(c).padStart(2)}  ${pad(db(rms(x)))}     ${pad(db(peak(x)))}`);
    }
    return;
  }

  const ch = Number(chArg);
  if (!Number.isInteger(ch) || ch < 1 || ch > channels) throw new Error(`--channel: expected 1..${channels}`);
  let x = channelOf(pcm, channels, ch);

  // Analyse a sub-range, so one capture spanning a whole note can be measured over the SUSTAIN alone - the
  // attack and release otherwise dominate the envelope swing and hide what the chip is doing while holding.
  const trim = flag(args, "--trim");
  if (trim) {
    const [a, b] = trim.split(":").map(Number);
    const from = Math.max(0, Math.floor(a * sampleRate));
    const to = Math.min(x.length, Math.floor((Number.isFinite(b) ? b : frames / sampleRate) * sampleRate));
    if (!(to > from)) throw new Error(`--trim ${trim}: empty range`);
    x = x.slice(from, to);
    console.log(`  trimmed to ${a}..${b} s (${x.length} frames)`);
  }

  const levelDb = db(rms(x));
  console.log(`  channel ${ch}: rms ${pad(levelDb)} dBFS   peak ${pad(db(peak(x)))} dBFS`);

  const env = envelopeDb(x, sampleRate);
  const st = envelopeStats(env, floorDb);
  if (st.n === 0) {
    console.log(`  SILENT - no ${10}ms frame above the ${floorDb} dBFS floor`);
  } else {
    console.log(
      `  envelope (sounding ${st.n} frames): min ${pad(st.min)}  max ${pad(st.max)}  swing ${pad(st.swing)} dB`,
    );
  }

  // How fast the amplitude repeats. Swing says an envelope RUNS; this says how quickly - the only way to
  // show a speed control (S5B CC28) actually does something, since changing rate leaves swing untouched.
  if (args.includes("--env-rate")) {
    const { env, rate } = envelopeLinear(x, sampleRate, 1);
    // A plain spectral peak, NOT detectPitch: an amplitude envelope is not a harmonic tone, and HPS folds
    // it to a subharmonic. Ignore bins below `fmin`, which is where DC drift and the capture's slow wander
    // live, and below the window's own resolution (1/duration) nothing is measurable anyway.
    const durationS = env.length / rate;
    const fmin = Math.max(Number(flag(args, "--env-fmin") ?? 1), 2 / durationS);
    const { mag, binHz } = magnitudeSpectrum(env, { window: "hann", sampleRate: rate });
    let best = -1, bestMag = 0, total = 0;
    for (let i = Math.ceil(fmin / binHz); i < mag.length; i++) {
      total += mag[i];
      if (mag[i] > bestMag) { bestMag = mag[i]; best = i; }
    }
    const share = total > 0 ? bestMag / total : 0;
    if (best > 0 && share > 0.01) {
      console.log(`  envelope rate ${pad(best * binHz)} Hz   (peak share ${pad(share, 5)}, >=${fmin.toFixed(2)} Hz, res ${binHz.toFixed(2)} Hz)`);
    } else {
      console.log(`  envelope rate: none above ${fmin.toFixed(2)} Hz (steady level)`);
    }
  }

  // Print the envelope. A rate number can be fooled by a broad spectrum; the shape cannot - a flat tone, a
  // one-shot decay and a repeating ramp are unmistakable here, and the period is countable by eye.
  if (args.includes("--env-dump")) {
    const hopMs = Number(flag(args, "--env-dump-hop") ?? 10);
    const e = envelopeDb(x, sampleRate, hopMs);
    const lo = Math.max(Math.min(...e), floorDb - 10);
    const hi = Math.max(...e);
    const ramp = " .:-=+*#%@";
    let row = "";
    for (const v of e) {
      const t = (v - lo) / Math.max(hi - lo, 1e-9);
      row += ramp[Math.min(ramp.length - 1, Math.max(0, Math.round(t * (ramp.length - 1))))];
    }
    console.log(`  envelope shape (${hopMs}ms/char, ${lo.toFixed(1)}..${hi.toFixed(1)} dBFS):`);
    for (let i = 0; i < row.length; i += 100) console.log(`    ${(i * hopMs / 1000).toFixed(2)}s |${row.slice(i, i + 100)}|`);
  }

  const p = detectPitch(x, { sampleRate });
  if (p.hz > 0) {
    const vs = expectHz !== undefined ? `   vs ${expectHz} Hz: ${pad(centsError(p.hz, expectHz), 7, 1)} cents` : "";
    console.log(`  pitch ${pad(p.hz)} Hz   confidence ${pad(p.confidence, 5)}${vs}`);
  } else {
    console.log("  pitch: none detected");
  }

  // Waveform shape, which separates "the same square at a different pitch" from "the same pitch with a
  // lopsided duty". Both can have identical rms OR identical spectra, so neither of those tells them apart:
  //   duty  - the fraction of the (DC-removed) signal above zero. A 50% square ~0.50; narrow pulses ~0.1.
  //   crest - peak/rms. A 50% square ~1.0; the narrower the pulse the higher it climbs.
  // A phase-reset hack that truncates cycles shows up here as duty drifting away from 0.5 and crest rising,
  // where a clean retrigger at a new rate leaves both alone.
  if (args.includes("--shape")) {
    let above = 0;
    for (let i = 0; i < x.length; i++) if (x[i] > 0) above++;
    const duty = above / Math.max(x.length, 1);
    const crest = peak(x) / Math.max(rms(x), 1e-12);
    console.log(`  shape: duty ${pad(duty, 6, 3)}   crest ${pad(crest, 6, 2)}`);
  }

  // A tone/noise split: a Sunsoft 5B square with its noise generator mixed in puts real energy well above
  // the fundamental's harmonics, where a clean square has little.
  const band = flag(args, "--band");
  if (band) {
    const [lo, hi] = band.split(":").map(Number);
    console.log(`  band ${lo}-${hi} Hz: ${pad(bandEnergyDb(x, lo, hi, sampleRate))} dB`);
  }
}

export const analyzeCaptureTool: CliTool = {
  name: "analyze-capture",
  summary: "measure a hardware audio capture: level, pitch, envelope swing, band energy",
  help: [
    "usage: retroplug-cli analyze-capture <capture.wav> [--channel N] [--expect-hz F] [--band lo:hi] [--floor dB]",
    "",
    "  Reports what a real console actually produced, from a WAV captured off its analog output.",
    "  With no --channel it surveys every channel's level (find which input the console is on);",
    "  with --channel it reports that channel in full.",
    "",
    "options:",
    "  --channel N     analyse this 1-based channel (else: level survey of all channels)",
    "  --expect-hz F   also report the error in cents vs this frequency",
    "  --band lo:hi    report the energy in a band, e.g. --band 4000:12000 for noise/hiss content",
    "  --shape         report duty (fraction above zero) + crest (peak/rms): tells a clean square at a",
    "                  new pitch apart from a duty-skewed / truncated one, which rms alone cannot",
    "  --env-rate      also report how FAST the amplitude repeats (Hz) - swing shows an envelope is",
    "                  running, this shows a speed control changing it (rate moves, swing does not)",
    "  --trim a:b      analyse only seconds a..b (measure the SUSTAIN, not the attack/release)",
    "  --floor dB      the silence floor for the envelope stats (default -70; a quiet NES capture",
    "                  idles near -76 dBFS, so anything above -70 is really sounding)",
    "",
    "  The envelope swing is what distinguishes a HARDWARE volume envelope (Sunsoft 5B CC20/28/29)",
    "  from a flat tone - both have the same average level, so RMS alone cannot tell them apart.",
    "",
    "example - was the S5B note pulsing, and in tune?",
    "  retroplug-cli analyze-capture /tmp/s5b.wav --channel 3 --expect-hz 440",
  ].join("\n"),
  run,
};
