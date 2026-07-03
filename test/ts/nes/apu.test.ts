// NES APU per-channel state (Mesen debugger). evermidi == n8-midi.nes: MIDI
// notes on channels 1-4 drive APU Pulse1 / Pulse2 / Triangle / Noise. The raw
// $4000-$4013 APU registers are write-only on the NES (open bus on read), so
// getApuState reads Mesen's decoded APU snapshot instead — the way to verify a
// MIDI-driven ROM's actual effect on the sound chip (period/duty/volume/enable),
// beyond "audio got louder".
//
// KNOWN ISSUE surfaced by getApuState: the committed resources/roms/n8-midi.nes
// does NOT drive APU Pulse 2 — a note on MIDI channel 2 lands on Pulse 1 instead
// (Pulse 2's timer/volume are never written). Nibbles 0/2/3 -> Pulse1/Triangle/
// Noise map correctly. pulse2_note_on looks correct in the current evermidi
// source, so this is most likely a stale committed binary; re-verify after a
// fresh cc65 build. These tests therefore assert on the three working synthesis
// types (square / triangle / noise) and deliberately skip Pulse 2.
import { test, expect, emu } from "harness";

const NES = "resources/roms/n8-midi.nes";
const GB = "../resources/roms/lsdj/lsdj9_4_2.gb";

// MIDI status bytes for note-on/off on channel `ch` (0-based; ch0 = MIDI ch 1).
const noteOn = (ch: number, note: number, vel: number) => [0x90 | ch, note, vel];
const noteOff = (ch: number, note: number) => [0x80 | ch, note, 0];

test("pulse1 is silent at boot, then a MIDI note on channel 1 drives it", () => {
  const sys = emu.loadRom(NES);
  emu.runMs(1000); // settle into the FIFO-poll idle loop

  const before = emu.getApuState(sys);
  expect(before.pulse1.outputVolume).toBe(0); // no note yet -> silent

  emu.sendMidi(sys, noteOn(0, 60, 100)); // MIDI ch 1 -> APU Pulse 1
  emu.runMs(200);

  const after = emu.getApuState(sys);
  expect(after.pulse1.period).toBeGreaterThan(0); // note wrote the timer
  expect(after.pulse1.envelopeVolume).toBeGreaterThan(0); // velocity -> volume
  expect(after.pulse1.frequency).toBeGreaterThan(20); // a musical pitch, not the
  expect(after.pulse1.frequency).toBeLessThan(5000); // period=0 boot value
});

test("note off silences pulse1", () => {
  const sys = emu.loadRom(NES);
  emu.runMs(1000);
  emu.sendMidi(sys, noteOn(0, 60, 100));
  emu.runMs(200);
  // envelopeVolume (the set volume) is the stable "note on" signal; outputVolume
  // is duty-gated and oscillates through 0, so it's not safe to sample here.
  expect(emu.getApuState(sys).pulse1.envelopeVolume).toBeGreaterThan(0);

  emu.sendMidi(sys, noteOff(0, 60));
  emu.runMs(200);
  const off = emu.getApuState(sys);
  // Silenced however the ROM chooses: channel disabled (length counter -> 0),
  // volume zeroed, or output gated to 0.
  const silenced =
    !off.pulse1.enabled || off.pulse1.outputVolume === 0 || off.pulse1.envelopeVolume === 0;
  expect(silenced).toBeTruthy();
});

test("MIDI notes drive the square, triangle and noise channels", () => {
  const sys = emu.loadRom(NES);
  emu.runMs(1000);

  emu.sendMidi(sys, noteOn(0, 60, 100)); emu.runMs(60); // ch 1 -> Pulse 1 (square)
  emu.sendMidi(sys, noteOn(2, 55, 100)); emu.runMs(60); // ch 3 -> Triangle
  emu.sendMidi(sys, noteOn(3, 48, 100)); emu.runMs(60); // ch 4 -> Noise

  const s = emu.getApuState(sys);
  // Square: timer period + velocity-derived envelope volume.
  expect(s.pulse1.period).toBeGreaterThan(0);
  expect(s.pulse1.envelopeVolume).toBeGreaterThan(0);
  // Triangle: timer period + linear counter (its channel-specific gate).
  expect(s.triangle.period).toBeGreaterThan(0);
  expect(s.triangle.linearCounter).toBeGreaterThan(0);
  // Noise: no pitch period as such; the envelope carries the velocity volume.
  expect(s.noise.envelopeVolume).toBeGreaterThan(0);
});

test("getApuState is Mesen-NES-only (throws on SameBoy)", () => {
  const gb = emu.loadRom(GB);
  let threw = false;
  try {
    emu.getApuState(gb);
  } catch {
    threw = true;
  }
  expect(threw).toBeTruthy();
});
