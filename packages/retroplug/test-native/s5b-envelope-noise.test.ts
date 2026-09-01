// Sunsoft 5B hardware envelope + noise, which Mesen's Sunsoft5bAudio did not implement at all: the
// IsEnvelopeEnabled / IsNoiseEnabled / GetNoisePeriod accessors existed but were never called, so
// UpdateOutputLevel summed tone only. Envelope mode (amp bit 4) therefore read as volume 0 = SILENT, and
// both noise CCs were inert. That is why EverMIDI has no automated tests for either.
//
// Now implemented from nesdev.org/wiki/Sunsoft_5B_audio: a 17-bit LFSR (taps 16/13) at Clock/(32*period),
// a 32-step envelope at Clock/(16*period) with the Continue/Attack/Alternate/Hold shape bits, and the real
// mixer rule - "if both bits are 0, the result is the logical and of noise and tone".
//
// This file covers the CHIP behaviour, which is the default. The Everdrive N8's 5B core has no noise
// generator, so on that cartridge enabling noise MUTES the channel instead; that fork lives behind the
// `s5bNoise` role knob and is covered in cartridge-accuracy.test.ts.
//
// Reference numbers from the physical NES + Everdrive N8 (PAL), measured on capture ch5:
//   envelope OFF -> 0.87 dB of swing over the sustain (a flat tone)
//   envelope ON  -> 22-26 dB of swing (the level visibly ramping)
// The emulator swings further than 22-26 dB simply because it has no analog noise floor to hide the
// bottom of the ramp in.
import { test, expect } from "../testing/harness";
import { bootSession } from "../cli/session";
import { Timeline, renderTimeline } from "../cli/timeline";


const S5B_ROM = "/workspaces/evermidi/rom/build/bliptoaster-s5b.nes";
const CH = 6; // EverMIDI's S5B Square A (BASE01)
const A4 = 69;

const cc = (num: number, val: number) => [0xb0 | (CH - 1), num, val];

function rms(x: Float32Array, from = 0, to = x.length): number {
  let s = 0;
  for (let i = from; i < to; i++) s += x[i] * x[i];
  return Math.sqrt(s / Math.max(to - from, 1));
}

const db = (x: number) => 20 * Math.log10(Math.max(x, 1e-12));

/** Peak-to-trough swing of the short-time level over the sustain - the only thing that separates a
 *  hardware volume envelope from a flat tone, since both have the same average level. */
function swingDb(pcm: Float32Array): number {
  const from = Math.floor(pcm.length * 0.3);
  const to = Math.floor(pcm.length * 0.9);
  const win = Math.floor((to - from) / 60);
  let lo = Infinity;
  let hi = -Infinity;
  for (let i = from; i + win <= to; i += win) {
    const v = db(rms(pcm, i, i + win));
    if (v < lo) lo = v;
    if (v > hi) hi = v;
  }
  return hi - lo;
}

function play(s: ReturnType<typeof bootSession>, setup: number[][], holdMs = 1500): Float32Array {
  const tl = new Timeline();
  let t = 0;
  for (const m of setup) tl.midi((t += 20), m);
  tl.note(t + 60, A4, { durationMs: holdMs, channel: CH, velocity: 100 });
  return renderTimeline(s, tl, { durationMs: holdMs + 200, warmupMs: 1200 });
}

function boot() {
  const s = bootSession();
  if (!s.backend.fileExists(S5B_ROM)) return null;
  const id = s.project.systems.addSystem(S5B_ROM);
  if (id == null) throw new Error("addSystem failed");
  // Explicit: the DEFAULT is "n8" (RetroPlug is music software and the N8 is how NES music is played),
  // and this file characterises the chip. cartridge-accuracy.test.ts covers the fork itself.
  if (!s.project.systems.setRoleConfig(id, "mesen", { s5bNoise: "chip" })) throw new Error("setRoleConfig failed");
  return s;
}

test("S5B hardware envelope: envelope mode sounds and its level moves, where it used to be silent", () => {
  const s = boot();
  if (!s) { console.log(`# SKIP s5b: no ROM at ${S5B_ROM}`); return; }

  // CC28=126 -> envelope period (255-2*126)<<8 = 768, a ~0.22 s ramp, so a 1.5 s hold shows several.
  // (The brief's CC28=64 is period 32512 = a ~9 s ramp - far too slow to see in one render, which matches
  // what the hardware did: at 64 the wobble is much slower than "moderate".)
  const flat = play(s, [cc(20, 0), cc(7, 127)]);
  const env = play(s, [cc(29, 80), cc(28, 126), cc(20, 127)]); // shape 10 = repeating triangle

  const flatDb = db(rms(flat));
  const envDb = db(rms(env));
  console.log(`[s5b-env] flat rms ${flatDb.toFixed(2)} dBFS swing ${swingDb(flat).toFixed(2)} dB`);
  console.log(`[s5b-env] env  rms ${envDb.toFixed(2)} dBFS swing ${swingDb(env).toFixed(2)} dB`);

  // The regression this guards: envelope mode used to render SILENCE (volume nibble 0).
  expect(envDb > -60).toBeTruthy();
  // And it must actually move, not just sound. Hardware showed 22-26 dB against 0.87 dB flat.
  expect(swingDb(env) > 6).toBeTruthy();
  expect(swingDb(flat) < 3).toBeTruthy();
});

test("S5B noise: the chip default gates the tone with the LFSR instead of ignoring the CC", () => {
  const s = boot();
  if (!s) { console.log(`# SKIP s5b: no ROM at ${S5B_ROM}`); return; }

  // CC1 must come AFTER the note: EverMIDI's s5b_note_on unconditionally sets the noise-disable bit, so a
  // CC1 sent before a note-on is clobbered by it (a ROM bug, reported separately).
  const tl = new Timeline()
    .midi(20, cc(20, 0))
    .midi(40, cc(7, 127))
    .midi(60, cc(30, 64))
    .note(100, A4, { durationMs: 1500, channel: CH, velocity: 100 })
    .midi(700, cc(1, 127)); // noise on, mid-note
  const pcm = renderTimeline(s, tl, { durationMs: 1700, warmupMs: 1200 });

  // Default is s5bNoise "chip": the mixer ANDs the tone with a real LFSR, so the channel keeps sounding
  // but its character changes. (It used to ignore the CC entirely - the tone was bit-identical.) The "n8"
  // mode, where this instead goes silent, is covered in cartridge-accuracy.test.ts.
  const third = Math.floor(pcm.length / 3);
  const before = db(rms(pcm, Math.floor(pcm.length * 0.15), third));
  const after = db(rms(pcm, Math.floor(pcm.length * 0.65), Math.floor(pcm.length * 0.95)));
  console.log(`[s5b-noise] tone ${before.toFixed(2)} dBFS -> noise-on ${after.toFixed(2)} dBFS`);

  expect(before > -60).toBeTruthy();   // it was sounding
  expect(after > -60).toBeTruthy();    // and it still is - gated, not muted
  expect(after < before).toBeTruthy(); // AND-ing with the LFSR removes energy
});
