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

  /** Stage raw MIDI bytes — global host MIDI, fanned to systems by the routing role. Any length: one
   *  channel message, a whole SysEx, or several messages as one run (the routing broadcasts a run longer
   *  than one message unchanged, and the NES N8 FIFO takes it byte-for-byte, in order). Throws HERE, at
   *  authoring time, on an empty array or a non-byte value, so a bad message can never be dropped silently
   *  by the render. */
  midi(ms: number, bytes: number[]): this {
    if (!Array.isArray(bytes) || bytes.length === 0) throw new Error(`Timeline.midi(${ms}): expected a non-empty byte array`);
    for (let i = 0; i < bytes.length; i++) {
      const b = bytes[i];
      if (!Number.isInteger(b) || b < 0 || b > 0xff)
        throw new Error(`Timeline.midi(${ms}): byte ${i} is ${String(b)}, expected an integer 0..255`);
    }
    return this.push({ ms, kind: "midi", bytes: bytes.slice() });
  }
  /** A System Exclusive message: `payload` (7-bit data bytes, manufacturer id first) wrapped in F0 .. F7.
   *  Throws at authoring time if a payload byte has bit 7 set - that would end the message early. */
  sysex(ms: number, payload: number[]): this {
    for (let i = 0; i < payload.length; i++) {
      const b = payload[i];
      if (!Number.isInteger(b) || b < 0 || b > 0x7f)
        throw new Error(`Timeline.sysex(${ms}): payload byte ${i} is ${String(b)}, expected 0..127 (7-bit)`);
    }
    return this.midi(ms, [0xf0, ...payload, 0xf7]);
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

/** A continuous invariant to hold for the whole run. Name the variable with `symbol` (resolved through
 *  `symbolAddress`, so the test must have called `loadLabels` first) or give an `address`; bound it with
 *  `max` / `min` / `equals`, or pass a raw Mesen `condition` for anything those don't cover.
 *
 *  It compiles to a conditional WRITE watchpoint, so it is checked on every write the ROM makes rather
 *  than at the moments a test happens to sample, which is the difference between catching a transient
 *  and hoping a `Timeline.at()` lands on it. */
export interface TimelineInvariant {
  system: number;
  symbol?: string;
  address?: number;
  end?: number;
  max?: number;
  min?: number;
  equals?: number;
  condition?: string;
  /** Shown in the failure message instead of the symbol/address, when a raw condition needs a name. */
  label?: string;
}

/** The Mesen expression that means "this invariant was VIOLATED": the watchpoint fires on the bad
 *  case, so a hit is a failure. `value` is the byte being written. */
function invariantCondition(inv: TimelineInvariant): string {
  if (inv.condition) return inv.condition;
  const parts: string[] = [];
  if (inv.max != null) parts.push(`value > ${inv.max}`);
  if (inv.min != null) parts.push(`value < ${inv.min}`);
  if (inv.equals != null) parts.push(`value != ${inv.equals}`);
  // No bound at all means "any write here is a violation", which is a legitimate thing to assert
  // (a variable that must never be touched after init), so an empty condition is not an error.
  return parts.join(" || ");
}

function invariantName(inv: TimelineInvariant): string {
  return inv.label ?? inv.symbol ?? (inv.address != null ? `$${inv.address.toString(16)}` : "<invariant>");
}

/** Play `timeline` against a booted session: render up to each event's ms, fire it, render on, then
 *  render the tail out to `durationMs`. Returns the concatenated interleaved-stereo PCM (feed to
 *  encodeWav). The engine is persistent, so an event fired between renders lands in the next chunk.
 *
 *  `warmupMs` renders (and DISCARDS) that many ms first, to boot the core before the timeline — many
 *  ROMs ignore input until initialized (n8-midi needs ~1s), so a note at t=0 would otherwise be lost.
 *  The returned PCM starts at the timeline's t=0, not the warm-up.
 *
 *  `invariants` install conditional watchpoints that hold for the whole run (warm-up included) and
 *  THROW if any fires. They own the system's breakpoint set (setBreakpoints replaces it wholesale),
 *  so a test cannot combine these with its own breakpoints on the same system. */
export function renderTimeline(
  session: Session,
  timeline: Timeline,
  opts: { durationMs: number; warmupMs?: number; invariants?: TimelineInvariant[] },
): Float32Array {
  const audio = session.audio;

  // Armed BEFORE the warm-up: a ROM's init is exactly where a "written once and never again"
  // invariant is most likely to be violated.
  const invariants = opts.invariants ?? [];
  const bySystem = new Map<number, TimelineInvariant[]>();
  for (const inv of invariants) {
    const addr =
      inv.address ??
      (inv.symbol != null
        ? session.backend.symbolAddress(inv.system, inv.symbol) ??
          session.backend.symbolAddress(inv.system, "_" + inv.symbol)
        : null);
    if (addr == null)
      throw new Error(
        `renderTimeline: invariant "${invariantName(inv)}" has no address: pass one, or loadLabels before the run`,
      );
    const list = bySystem.get(inv.system) ?? [];
    list.push({ ...inv, address: addr });
    bySystem.set(inv.system, list);
  }
  for (const [system, list] of bySystem) {
    const ok = session.backend.setBreakpoints(
      system,
      list.map((inv) => ({
        type: "write" as const,
        start: inv.address!,
        end: inv.end ?? inv.address!,
        condition: invariantCondition(inv),
      })),
    );
    if (!ok) throw new Error(`renderTimeline: could not install invariants on system ${system}`);
    session.backend.drainBreakHits(system); // discard anything a prior run left
  }

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
      case "midi":
        // The driver's false is the ONLY way a message can fail to reach the kernel now; make it loud.
        if (!audio.stageMidiIn(ev.bytes))
          throw new Error(`Timeline: the host refused a ${ev.bytes.length}-byte MIDI message at ${ev.ms} ms`);
        break;
      case "press": audio.pressButton(ev.system, ev.button, ev.down); break;
      case "bpm": audio.setBpm(ev.bpm); break;
      case "transport": audio.setTransport(ev.running); break;
      case "screenshot": audio.screenshot(ev.system, ev.path); break;
      case "at": ev.fn(session); break; // observe/assert at the scheduled time
    }
  }
  advance(opts.durationMs); // tail

  // A violation is reported with WHERE it happened, because "live_ofs went over 15" without a frame
  // and scanline is the same needle-in-a-haystack the invariant exists to remove.
  for (const [system, list] of bySystem) {
    const batch = session.backend.drainBreakHits(system);
    session.backend.setBreakpoints(system, []); // disarm: leave the core as we found it
    if (batch.hits.length === 0 && batch.overflow === 0) continue;
    const hit = batch.hits[0];
    const inv = list.find((i) => i.address === hit?.address) ?? list[0];
    const where = hit
      ? `scanline ${hit.scanline} cycle ${hit.cycle}, pc $${hit.pc.toString(16)}, value ${hit.value}`
      : "position unavailable";
    const more =
      batch.hits.length - 1 + batch.overflow > 0
        ? ` (+${batch.hits.length - 1 + batch.overflow} more)`
        : "";
    throw new Error(`renderTimeline: invariant "${invariantName(inv)}" violated at ${where}${more}`);
  }

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
