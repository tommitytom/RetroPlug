// Replacement for examples/scripts/lsdj_smoke.json (the `cli-smoke` render) and
// examples/scripts/mgb_smoke.json (the Reaper mGB fixture bootstrap).
//
// mGB is a MIDI-driven Game Boy synth (no song state, no UI navigation). This:
//   1. boots mGB and plays a C-major chord across three channels (the mgb_smoke
//      behaviour), asserting it produces audio;
//   2. writes /tmp/cli-smoke.wav for the reaper-analyze-smoke MCP workflow
//      (the old cli-smoke target);
//   3. emits /tmp/mgb_smoke_author.rplg — the fixture reaper-mgb-author bakes
//      into examples/reaper/mgb_smoke.rpp for the reaper-mgb-smoke DAW test.
import { test, expect, emu } from "harness";

const MGB = "resources/roms/mGB.gb";

const rms = (a: Float32Array): number => {
  let s = 0;
  for (let i = 0; i < a.length; i++) s += a[i] * a[i];
  return a.length ? Math.sqrt(s / a.length) : 0;
};

test("mGB plays a MIDI chord and authors the cli-smoke + Reaper fixtures", () => {
  const sys = emu.loadRom(MGB);
  emu.runMs(1500); // GB boot logo

  const idle = rms(emu.getAudio(500));

  // C-major chord across mGB's per-channel MIDI inputs (ch0/1/2).
  emu.sendMidi(sys, [0x90, 60, 100]);
  emu.sendMidi(sys, [0x91, 64, 100]);
  emu.sendMidi(sys, [0x92, 67, 100]);
  const playing = emu.getAudio(1500); // capture the chord
  emu.sendMidi(sys, [0x80, 60, 0]);
  emu.sendMidi(sys, [0x81, 64, 0]);
  emu.sendMidi(sys, [0x82, 67, 0]);
  const tail = emu.getAudio(1000);

  console.log(`mgb RMS idle=${idle.toFixed(5)} playing=${rms(playing).toFixed(5)}`);
  expect(rms(playing)).toBeGreaterThan(0.001); // chord is audible
  expect(rms(playing)).toBeGreaterThan(idle);

  // Artifacts for the (repointed) reaper targets.
  const wav = new Float32Array(playing.length + tail.length);
  wav.set(playing); wav.set(tail, playing.length);
  emu.writeWav("/tmp/cli-smoke.wav", wav);
  emu.saveRplg("/tmp/mgb_smoke_author.rplg");
});
