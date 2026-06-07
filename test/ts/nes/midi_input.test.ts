// Replacement for examples/scripts/n8_midi_input.json.
//
// The JSON booted the n8-midi NES ROM, sent a short sequence of note on/off
// messages across MIDI channels, and screenshotted to eyeball it. n8-midi only
// emits sound in response to MIDI, so the assertion the screenshot only implied
// is straightforward: audio is silent until notes arrive, then audible.
import { test, expect, emu } from "harness";

const NES = "resources/roms/n8-midi.nes";

const rms = (a: Float32Array): number => {
  let s = 0;
  for (let i = 0; i < a.length; i++) s += a[i] * a[i];
  return a.length ? Math.sqrt(s / a.length) : 0;
};

test("n8-midi NES ROM turns host MIDI notes into audio", () => {
  const sys = emu.loadRom(NES);
  emu.runMs(1000); // settle

  const idle = rms(emu.getAudio(1000)); // no MIDI yet

  // A few notes across channels (host MIDI -> NES MIDI FIFO -> APU).
  emu.sendMidi(sys, [0x90, 60, 100]); emu.runMs(400); emu.sendMidi(sys, [0x80, 60, 0]);
  emu.sendMidi(sys, [0x91, 64, 100]); emu.runMs(400); emu.sendMidi(sys, [0x81, 64, 0]);
  emu.sendMidi(sys, [0x93, 48, 100]);
  const playing = rms(emu.getAudio(1500));
  emu.sendMidi(sys, [0x83, 48, 0]);
  emu.screenshot(sys, "/tmp/n8_midi_input.png");

  console.log(`n8_midi RMS idle=${idle.toFixed(5)} playing=${playing.toFixed(5)}`);
  expect(playing).toBeGreaterThan(0.001);  // notes produced audio
  expect(playing).toBeGreaterThan(idle);   // and it was driven by the MIDI
});
