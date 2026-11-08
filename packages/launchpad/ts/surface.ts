// The write side: a shadow buffer of what every LED SHOULD look like, and a flush that emits only the
// difference from what the device was last told.
//
// Same shape as the LSDj HD canvas (@retroplug/lsdj/ts/hd/canvas.ts), for the same reason. Draw calls are cheap
// writes into an array; `flush()` diffs against the last flushed frame and emits messages for the
// changed entries alone. An app can therefore repaint the whole surface every frame in the obvious
// declarative way, and the steady-state cost is a handful of bytes - or, when nothing moved, none at
// all and no MIDI write whatsoever.
//
// Part of the portable core (ts/portable.ts), so it keeps to what MicroQuickJS runs: the shadow buffers are arrays
// indexed by device note number rather than Maps, which it does not have.

import { PRO_MK3, type LaunchpadProfile } from "./profile";
import { LED_OFF, isRenderable, ledLightingSysex, lightMessage, needsSysex, type Colour, type Led } from "./protocol";

/** Above this many changed LEDs, one bulk SysEx beats a burst of short messages.
 *
 *  The byte arithmetic is close to a wash - N short messages cost 3N bytes, a bulk SysEx costs 8 bytes
 *  of framing plus 3-5 per entry - so the real reason is message COUNT: each short message is its own
 *  USB packet and its own write, while the bulk form is one of each no matter how many LEDs it carries.
 *  Small edits stay short messages because they are simpler to read on a MIDI monitor when debugging. */
export const BULK_THRESHOLD = 8;

/** Every addressable index on the surface: the 64 grid pads plus the named edge buttons. */
function allIndices(profile: LaunchpadProfile): number[] {
  const out: number[] = [];
  for (let row = 1; row <= 8; row++) for (let col = 1; col <= 8; col++) out.push(row * 10 + col);
  for (const name of Object.keys(profile.buttons)) out.push(profile.buttons[name]);
  return out;
}

/** Device indices are MIDI note / CC numbers, so one slot per 7-bit value covers every control. */
const INDEX_SLOTS = 128;

/** A slot index, and nothing else: an integer in range. A fractional index would otherwise slip past the
 *  null check (it reads `undefined`) and write a non-integer array index - a stray property under QuickJS,
 *  a TypeError under MicroQuickJS. */
const isSlot = (index: number): boolean => Math.floor(index) === index && index >= 0 && index < INDEX_SLOTS;

function sameColour(x: Colour, y: Colour): boolean {
  if (x.kind === "palette" && y.kind === "palette") return x.index === y.index;
  if (x.kind === "rgb" && y.kind === "rgb") return x.r === y.r && x.g === y.g && x.b === y.b;
  return false;
}

function sameLed(a: Led, b: Led): boolean {
  if (a.mode === "off" && b.mode === "off") return true;
  if (a.mode === "static" && b.mode === "static") return sameColour(a.colour, b.colour);
  if (a.mode === "pulse" && b.mode === "pulse") return sameColour(a.colour, b.colour);
  if (a.mode === "flash" && b.mode === "flash") return sameColour(a.colour, b.colour) && sameColour(a.alt, b.alt);
  return false;
}

/** What a flush produced. `messages` is ready to write in order; `dirty` is how many LEDs changed, so a
 *  caller can skip the write entirely (and skip logging) when it is zero. */
export interface FlushResult {
  messages: number[][];
  dirty: number;
}

export class Surface {
  readonly profile: LaunchpadProfile;

  private readonly indices: number[];
  // By device index; null for an index the surface does not have. Built full-length up front - the
  // portable core runs where an array may not be written past its end.
  private readonly desired: (Led | null)[] = [];
  private readonly sent: (Led | null)[] = [];
  private repaintAll = false;
  // Whether any desired LED has changed since the last flush. flush() is called every audio block and the
  // usual answer is "nothing", which should not cost a walk over every LED to find out.
  private dirty = false;

  // Reused between flushes. This runs once per audio block, and the overwhelmingly common case is "nothing
  // changed" - which used to allocate a scratch array and a result object anyway, for a call that emits
  // nothing. The message arrays below still allocate when LEDs genuinely change; that is unavoidable and
  // happens only when the picture moves.
  private readonly changed: { index: number; led: Led }[] = [];
  private readonly silent: FlushResult = { messages: [], dirty: 0 };

  constructor(profile: LaunchpadProfile = PRO_MK3) {
    this.profile = profile;
    this.indices = allIndices(profile);
    for (let i = 0; i < INDEX_SLOTS; i++) {
      this.desired.push(null);
      this.sent.push(null);
    }
    for (const i of this.indices) {
      this.desired[i] = LED_OFF;
      this.sent[i] = LED_OFF;
    }
  }

  /** Set one control's LED by device index. Unknown indices are ignored rather than invented. */
  set(index: number, led: Led): void {
    if (!isSlot(index) || this.desired[index] === null) return;
    const next = isRenderable(led) ? led : LED_OFF; // refuse to encode something the device would misread
    if (sameLed(this.desired[index]!, next)) return;
    this.desired[index] = next;
    this.dirty = true;
  }

  /** Set a grid pad by top-left coordinate. */
  setPad(x: number, y: number, led: Led): void {
    if (x < 0 || x > 7 || y < 0 || y > 7) return;
    this.set((7 - y) * 10 + x + 11, led);
  }

  /** Set a named edge button (see the profile's button table). */
  setButton(name: string, led: Led): void {
    const cc = this.profile.buttons[name];
    if (cc !== undefined) this.set(cc, led);
  }

  /** Blank everything. Takes effect on the next flush like any other change. */
  clear(): void {
    for (const i of this.indices) this.set(i, LED_OFF);
  }

  /** Emit the messages that bring the device from its last-flushed state to the desired one, and adopt
   *  that as the new baseline. Empty when nothing changed. */
  flush(): FlushResult {
    if (!this.dirty && !this.repaintAll) return this.silent;
    this.dirty = false;
    const changed = this.changed;
    changed.length = 0;
    const indices = this.indices;
    let bulk = this.repaintAll;
    for (let i = 0; i < indices.length; i++) {
      const index = indices[i];
      const want = this.desired[index]!;
      if (!this.repaintAll && sameLed(want, this.sent[index]!)) continue;
      changed.push({ index, led: want });
      if (needsSysex(want)) bulk = true; // RGB has no short form, so one entry forces the whole batch
    }
    this.repaintAll = false;
    if (changed.length === 0) return this.silent;

    const messages = bulk || changed.length > BULK_THRESHOLD
      ? ledLightingSysex(this.profile, changed)
      : shortMessages(changed);

    for (let i = 0; i < changed.length; i++) this.sent[changed[i].index] = changed[i].led;
    return { messages, dirty: changed.length };
  }

  /** Forget what the device has been told, so the next flush repaints EVERY LED including the ones that
   *  are off. Needed on reconnect, and after entering Programmer mode - which blanks the surface, so the
   *  device no longer matches our baseline and a plain diff would send nothing. */
  invalidate(): void {
    this.repaintAll = true;
  }

  /** The LED currently requested at an index - for tests and for a caller diffing its own state. */
  peek(index: number): Led | undefined {
    return (isSlot(index) ? this.desired[index] : null) ?? undefined;
  }
}

/** One 3-byte message per changed LED. An entry needing SysEx never reaches here (flush checks first),
 *  but if one somehow did it is dropped rather than mis-encoded. */
function shortMessages(changed: readonly { index: number; led: Led }[]): number[][] {
  const out: number[][] = [];
  for (const c of changed) {
    const m = lightMessage(c.index, c.led);
    if (m) out.push(m);
  }
  return out;
}
