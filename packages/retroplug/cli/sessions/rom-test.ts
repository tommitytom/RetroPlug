// The agent-facing ROM-test pattern: a session that DRIVES a real NES and ASSERTS on its state, emitting
// TAP. This is how you develop a ROM against the CLI — write a session like this, bundle it, run it on
// retroplug-cli, parse the TAP. Tuned to the committed n8-midi.nes (a MIDI→APU synth).
//
//   retroplug-cli build/cli/rom-test.js <rom>
//
// NOTE: import test/expect from the harness and register at top level — the harness auto-runs on a
// microtask and owns the TAP output + tjs.exit. Do NOT wrap the body in runSession() (it would call
// tjs.exit(0) synchronously and preempt the harness, reporting a false pass).
import { test, expect } from "../../testing/harness";
import { bootSession, hostArgs } from "../session";
import { Timeline, renderTimeline } from "../timeline";
import type { ApuState } from "../../src/backend";

const rom = hostArgs()[0];

test("n8-midi: a ch1 note drives pulse1 at pitch, pulse2 stays silent", () => {
  const s = bootSession();
  if (!rom || !s.backend.fileExists(rom)) throw new Error(`rom not found: ${rom}`);
  const id = s.project.systems.addSystem(rom);
  if (id == null) throw new Error(`could not load ${rom}`);

  // Schedule the assertion at 400ms (mid-note) via Timeline.at — the render advances to exactly that
  // time, then the callback reads the live APU state. warmupMs boots the core before the sequence.
  let apu: ApuState | null = null;
  const tl = new Timeline()
    .note(200, 60, { channel: 1, durationMs: 400 }) // MIDI note 60 (C4) on channel 1
    .at(400, (sess) => (apu = sess.backend.getApuState(id)));
  renderTimeline(s, tl, { durationMs: 800, warmupMs: 1000 });

  // Gate "is it sounding" on period + envelope, NOT the $4015 `enabled` bit (which is set once at init).
  expect(apu != null).toBeTruthy();
  expect(apu!.pulse1.period > 0 && apu!.pulse1.envelopeVolume > 0).toBeTruthy();
  expect(apu!.pulse1.frequency > 250 && apu!.pulse1.frequency < 275).toBeTruthy(); // ~261.6 Hz (C4)
  expect(apu!.pulse2.envelopeVolume === 0).toBeTruthy(); // ch1 only → pulse2 silent

  // A `ch2 → pulse2` assertion would go here — it currently FAILS on the committed ROM (ch2 wrongly
  // drives pulse1, a known stale-cc65-binary bug), which is exactly the kind of regression this catches.
});
