// Coverage for the CLI render surface added on top of the harness: routed MIDI
// (dispatchMidi), battery-RAM serialization (saveSram), and the streaming
// renderWav / renderWavPerSystem that write a render straight to disk without
// crossing the whole PCM buffer over the wire. These are the primitives the
// TypeScript CLI render command is built on.
import { test, expect, emu, Mem, Routing } from "harness";

const MGB = "resources/roms/mGB.gb";
const LSDJ = "../resources/roms/lsdj/lsdj9_4_2.gb";

const rms = (a: Float32Array): number => {
  let s = 0;
  for (let i = 0; i < a.length; i++) s += a[i] * a[i];
  return a.length ? Math.sqrt(s / a.length) : 0;
};

// A WavWriter file starts with the standard RIFF/WAVE chunk header.
const isWav = (b: Uint8Array): boolean =>
  b.length > 44 &&
  b[0] === 0x52 && b[1] === 0x49 && b[2] === 0x46 && b[3] === 0x46 && // "RIFF"
  b[8] === 0x57 && b[9] === 0x41 && b[10] === 0x56 && b[11] === 0x45; // "WAVE"

test("saveSram serializes a 128 KiB LSDj battery image", () => {
  const sav = emu.savFromJson(JSON.stringify({
    workingSong: { formatVersion: 22, settings: { syncMode: "Lsdj" } },
  }));
  const sys = emu.loadRom(LSDJ, sav);
  emu.runMs(2000);

  const battery = emu.saveSram(sys);
  expect(battery.length).toBe(0x20000); // LSDj uses the full 128 KiB SRAM
  // 'jk' SRAM-init magic at 0x813E proves the image is an initialised sav, not
  // a blank region (the same check gen-lsdj-savs.ts validates against).
  expect(battery[0x813e]).toBe(0x6a); // 'j'
  expect(battery[0x813f]).toBe(0x6b); // 'k'
});

test("renderWav streams a valid WAV to disk", () => {
  const sys = emu.loadRom(MGB);
  emu.runMs(1500); // GB boot logo
  emu.dispatchMidi([0x90, 60, 100]); // SendToAll (default): note on, mGB pulse 1

  const path = "/tmp/rp-render.test.wav";
  emu.renderWav(path, 500);

  const bytes = emu.readFile(path);
  expect(isWav(bytes)).toBeTruthy();
  // 500 ms stereo 16-bit @ 44.1 kHz ≈ 88 KiB of PCM + header.
  expect(bytes.length).toBeGreaterThan(80000);
});

test("dispatchMidi OneChannelPerInstance routes by channel nibble", () => {
  const a = emu.loadRom(MGB); // system 0
  const b = emu.loadRom(MGB); // system 1
  expect(a).toBeLessThan(b);
  emu.runMs(1500); // boot both

  // Channel 0 -> system 0 only. The follower must stay silent.
  emu.dispatchMidi([0x90, 60, 100], Routing.OneChannelPerInstance);
  const r0 = emu.runMsPerSystem(800);
  const sys0a = rms(r0[0]);
  const sys1a = rms(r0[1]);
  console.log(`ch0 routed: sys0=${sys0a.toFixed(5)} sys1=${sys1a.toFixed(5)}`);
  expect(sys0a).toBeGreaterThan(0.001);
  expect(sys1a).toBeLessThan(sys0a);

  // Channel 1 -> system 1. Now the follower becomes audible.
  emu.dispatchMidi([0x91, 64, 100], Routing.OneChannelPerInstance);
  const r1 = emu.runMsPerSystem(800);
  console.log(`ch1 routed: sys1=${rms(r1[1]).toFixed(5)}`);
  expect(rms(r1[1])).toBeGreaterThan(0.001);
});

test("renderWavPerSystem writes the mix plus one WAV per system", () => {
  emu.loadRom(MGB); // system 0
  emu.loadRom(MGB); // system 1
  emu.runMs(1500);
  emu.dispatchMidi([0x90, 60, 100]); // SendToAll: both systems sound

  const mix = "/tmp/rp-persys-mix.test.wav";
  const s0 = "/tmp/rp-persys-s0.test.wav";
  const s1 = "/tmp/rp-persys-s1.test.wav";
  emu.renderWavPerSystem(mix, [s0, s1], 300);

  for (const p of [mix, s0, s1]) expect(isWav(emu.readFile(p))).toBeTruthy();
  // One pass: the mix and per-system files cover the same window -> same length.
  expect(emu.readFile(mix).length).toBe(emu.readFile(s0).length);
});
