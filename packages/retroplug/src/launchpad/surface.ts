// The write side: a shadow buffer of what every LED SHOULD look like, and a flush that emits only the
// difference from what the device was last told.
//
// Same shape as the LSDj HD canvas (src/lsdj/hd/canvas.ts), for the same reason. Draw calls are cheap
// writes into an array; `flush()` diffs against the last flushed frame and emits messages for the
// changed entries alone. An app can therefore repaint the whole surface every frame in the obvious
// declarative way, and the steady-state cost is a handful of bytes - or, when nothing moved, none at
// all and no MIDI write whatsoever.

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
  for (const cc of Object.values(profile.buttons)) out.push(cc);
  return out;
}

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
  private readonly desired = new Map<number, Led>();
  private readonly sent = new Map<number, Led>();
  private repaintAll = false;

  constructor(profile: LaunchpadProfile = PRO_MK3) {
    this.profile = profile;
    this.indices = allIndices(profile);
    for (const i of this.indices) {
      this.desired.set(i, LED_OFF);
      this.sent.set(i, LED_OFF);
    }
  }

  /** Set one control's LED by device index. Unknown indices are ignored rather than invented. */
  set(index: number, led: Led): void {
    if (!this.desired.has(index)) return;
    this.desired.set(index, isRenderable(led) ? led : LED_OFF); // refuse to encode something the device would misread
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
    for (const i of this.indices) this.desired.set(i, LED_OFF);
  }

  /** Emit the messages that bring the device from its last-flushed state to the desired one, and adopt
   *  that as the new baseline. Empty when nothing changed. */
  flush(): FlushResult {
    const changed: { index: number; led: Led }[] = [];
    for (const index of this.indices) {
      const want = this.desired.get(index)!;
      if (this.repaintAll || !sameLed(want, this.sent.get(index)!)) changed.push({ index, led: want });
    }
    this.repaintAll = false;
    if (changed.length === 0) return { messages: [], dirty: 0 };

    const messages = changed.length > BULK_THRESHOLD || changed.some((c) => needsSysex(c.led))
      ? ledLightingSysex(this.profile, changed)
      : shortMessages(changed);

    for (const c of changed) this.sent.set(c.index, c.led);
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
    return this.desired.get(index);
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
