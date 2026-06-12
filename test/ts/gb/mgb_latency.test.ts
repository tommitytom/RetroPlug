// Measures mGB's trigger-to-sound latency: the time from a MIDI note-on being
// dispatched to the first audible sample coming out of the emulator. In the
// harness, sendMidi queues the event so the next getAudio block applies it at
// frame 0 — i.e. the trigger is sample 0 of that buffer — so the audio onset's
// sample offset IS the latency. Repeated trials sample different phases of
// mGB's MIDI-poll loop to show the spread.
import { test, expect, emu } from "harness";

const MGB = "resources/roms/mGB.gb";
const SR = 44100;

// First stereo-frame index whose |sample| crosses `thresh`, or -1.
function firstOnset(buf: Float32Array, thresh: number): number {
  for (let i = 0; i + 1 < buf.length; i += 2)
    if (Math.abs(buf[i]) > thresh || Math.abs(buf[i + 1]) > thresh) return i / 2;
  return -1;
}
function peak(buf: Float32Array): number {
  let p = 0;
  for (let i = 0; i < buf.length; i++) p = Math.max(p, Math.abs(buf[i]));
  return p;
}

test("mGB trigger-to-sound latency", () => {
  const sys = emu.loadRom(MGB);
  emu.runMs(2000); // GB boot logo
  emu.getAudio(300); // settle

  const samples: number[] = [];
  for (let trial = 0; trial < 10; trial++) {
    // Silence the channel and let it decay, then capture the noise floor.
    emu.sendMidi(sys, [0x80, 60, 0]);
    emu.getAudio(250);
    const floor = peak(emu.getAudio(40));

    // Trigger: the note-on applies at sample 0 of the very next captured block.
    emu.sendMidi(sys, [0x90, 60, 127]);
    const buf = emu.getAudio(80);

    // Onset = first sample that clears both the noise floor and 5% of the
    // note's own peak (so the envelope's initial ramp doesn't read as silence).
    const thresh = Math.max(floor * 6, peak(buf) * 0.05, 0.01);
    const onset = firstOnset(buf, thresh);
    if (onset >= 0) {
      samples.push(onset);
      console.log(`[mgb-latency] trial ${trial}: onset=${onset} frames = ` +
        `${((onset / SR) * 1000).toFixed(2)} ms (floor=${floor.toFixed(5)} thresh=${thresh.toFixed(4)})`);
    } else {
      console.log(`[mgb-latency] trial ${trial}: NO ONSET (peak=${peak(buf).toFixed(5)})`);
    }

    // Nudge the next trigger's phase within mGB's poll loop.
    emu.getAudio(5 + trial);
  }

  const ms = samples.map((s) => (s / SR) * 1000);
  const min = Math.min(...ms), max = Math.max(...ms);
  const avg = ms.reduce((a, b) => a + b, 0) / ms.length;
  console.log(`[mgb-latency] over ${ms.length} trials: min=${min.toFixed(2)} ` +
    `avg=${avg.toFixed(2)} max=${max.toFixed(2)} ms`);

  expect(samples.length).toBeGreaterThan(0); // mGB produced sound
  expect(max).toBeLessThan(50); // sanity: latency is small
});
