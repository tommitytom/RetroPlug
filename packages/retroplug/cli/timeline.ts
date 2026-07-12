// The CLI's timed-event Timeline: MIDI/event "scripting" authored in TypeScript, not JSON. A
// session builds a Timeline fluently, then renderTimeline() advances the audio render in chunks —
// rendering up to each event's scheduled ms, firing it against the AudioDriver, and continuing — and
// returns the concatenated PCM (encode it with cli/wav.ts). Mirrors the legacy JSON CLI's advance loop
// (packages/cli/src/main.ts), but every event is a typed TS value.

import { BUTTON_VALUE } from "../src/keyCodes";
import type { Session } from "./session";

/** Named button values (Right=0..Start=7, position-aligned across GB/NES) + the GBA-only L/R wire bytes.
 *  Pass to Timeline.press / Timeline.tap. */
// Typed as a string→number map (not `as const`): spreading BUTTON_VALUE (a Record<string,number>)
// erases its literal keys, so name access like `Button.A` needs the index signature.
export const Button: Record<string, number> = { ...BUTTON_VALUE, L: 8, R: 9 };

interface NoteOpts {
  channel?: number; // 1-based (default 1)
  velocity?: number; // 0..127 (default 100)
}

// A flat, absolute-ms event the player fires. Internal — authors use the Timeline builder methods.
export type TimelineEvent =
  | { ms: number; kind: "midi"; bytes: number[] }
  | { ms: number; kind: "press"; system: number; button: number; down: boolean }
  | { ms: number; kind: "bpm"; bpm: number }
  | { ms: number; kind: "transport"; running: boolean }
  | { ms: number; kind: "screenshot"; system: number; path: string }
  | { ms: number; kind: "at"; fn: (s: Session) => void };

const statusFor = (base: number, channel = 1) => base | ((channel - 1) & 0x0f);
const noteOnBytes = (note: number, o?: NoteOpts) => [statusFor(0x90, o?.channel), note & 0x7f, (o?.velocity ?? 100) & 0x7f];
const noteOffBytes = (note: number, o?: NoteOpts) => [statusFor(0x80, o?.channel), note & 0x7f, 0];

/** A fluent, TS-authored timeline of timed emulator events. Every method returns `this` and records an
 *  event at absolute time `ms`; build() flattens to a stable ms-sorted list the player consumes. */
export class Timeline {
  private events: TimelineEvent[] = [];

  private push(ev: TimelineEvent): this {
    this.events.push(ev);
    return this;
  }

  /** Stage a raw MIDI message (≤4 bytes) — global host MIDI, fanned to systems by the routing role. */
  midi(ms: number, bytes: number[]): this {
    return this.push({ ms, kind: "midi", bytes });
  }
  noteOn(ms: number, note: number, opts?: NoteOpts): this {
    return this.midi(ms, noteOnBytes(note, opts));
  }
  noteOff(ms: number, note: number, opts?: NoteOpts): this {
    return this.midi(ms, noteOffBytes(note, opts));
  }
  /** A note: noteOn at `ms`, noteOff at `ms + durationMs`. Channel 1-based (default 1), velocity default 100. */
  note(ms: number, note: number, opts: NoteOpts & { durationMs: number }): this {
    return this.noteOn(ms, note, opts).noteOff(ms + opts.durationMs, note, opts);
  }
  /** Press or release `button` on `system` at `ms`. */
  press(ms: number, system: number, button: number, down: boolean): this {
    return this.push({ ms, kind: "press", system, button, down });
  }
  /** Tap `button` on `system`: down at `ms`, up at `ms + holdMs` (default 50). */
  tap(ms: number, system: number, button: number, opts?: { holdMs?: number }): this {
    const hold = opts?.holdMs ?? 50;
    return this.press(ms, system, button, true).press(ms + hold, system, button, false);
  }
  bpm(ms: number, bpm: number): this {
    return this.push({ ms, kind: "bpm", bpm });
  }
  transport(ms: number, running: boolean): this {
    return this.push({ ms, kind: "transport", running });
  }
  screenshot(ms: number, system: number, path: string): this {
    return this.push({ ms, kind: "screenshot", system, path });
  }
  /** Run `fn` against the live Session at `ms` — the render advances to `ms` first, so `fn` observes
   *  the core at exactly that time. This is the observe/assert hook: read APU/CPU/memory and `expect`
   *  on it (`s.backend.getApuState(id)`, `readCpu`, `getCpuRegisters`). */
  at(ms: number, fn: (s: Session) => void): this {
    return this.push({ ms, kind: "at", fn });
  }

  /** The events flattened to a stable ms-sorted list — insertion order breaks ties, so a same-ms noteOn
   *  precedes its noteOff and a tap's down precedes its up. Pure; touches no engine. */
  build(): TimelineEvent[] {
    return this.events
      .map((ev, i) => ({ ev, i }))
      .sort((a, b) => a.ev.ms - b.ev.ms || a.i - b.i)
      .map(({ ev }) => ev);
  }
}

/** Play `timeline` against a booted session: render up to each event's ms, fire it, render on, then
 *  render the tail out to `durationMs`. Returns the concatenated interleaved-stereo PCM (feed to
 *  encodeWav). The engine is persistent, so an event fired between renders lands in the next chunk.
 *
 *  `warmupMs` renders (and DISCARDS) that many ms first, to boot the core before the timeline — many
 *  ROMs ignore input until initialized (n8-midi needs ~1s), so a note at t=0 would otherwise be lost.
 *  The returned PCM starts at the timeline's t=0, not the warm-up. */
export function renderTimeline(
  session: Session,
  timeline: Timeline,
  opts: { durationMs: number; warmupMs?: number },
): Float32Array {
  const audio = session.audio;
  if (opts.warmupMs && opts.warmupMs > 0) audio.renderAudio(opts.warmupMs); // boot, discarded
  const chunks: Float32Array[] = [];
  let cur = 0;
  const advance = (toMs: number) => {
    const d = toMs - cur;
    if (d <= 0) return;
    chunks.push(audio.renderAudio(d));
    cur = toMs;
  };

  for (const ev of timeline.build()) {
    advance(ev.ms);
    switch (ev.kind) {
      case "midi": audio.stageMidiIn(ev.bytes); break;
      case "press": audio.pressButton(ev.system, ev.button, ev.down); break;
      case "bpm": audio.setBpm(ev.bpm); break;
      case "transport": audio.setTransport(ev.running); break;
      case "screenshot": audio.screenshot(ev.system, ev.path); break;
      case "at": ev.fn(session); break; // observe/assert at the scheduled time
    }
  }
  advance(opts.durationMs); // tail

  let n = 0;
  for (const c of chunks) n += c.length;
  const out = new Float32Array(n);
  let o = 0;
  for (const c of chunks) {
    out.set(c, o);
    o += c.length;
  }
  return out;
}
