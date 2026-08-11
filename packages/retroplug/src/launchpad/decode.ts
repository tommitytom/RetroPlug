// Incoming Launchpad traffic -> typed surface events.
//
// In Programmer mode every control sends plain MIDI: the 8x8 pads as Note On/Off, the surrounding
// buttons as Control Change, and (on the Pro MK3, which has velocity-sensitive pads) polyphonic
// aftertouch while a pad is held. This turns those bytes into something an app can switch on without
// knowing any of that.
//
// Two device quirks it absorbs so callers never have to:
//   - The device sends NOTE ON WITH VELOCITY 0 for note off, not Note Off. Both forms mean "up".
//   - A pad's index is only meaningful alongside its kind; the same number is a different control as a
//     CC than as a note. `pad` is filled in only for real 8x8 grid pads.

import { buttonName, padAt, type LaunchpadProfile, type Pad } from "./profile";

export type SurfaceEventKind = "down" | "up" | "pressure";

/** One thing the player did. `pad` is the grid coordinate for an 8x8 pad, null for an edge button;
 *  `button` is the edge button's name, null for a grid pad (and for an unrecognised CC). */
export interface SurfaceEvent {
  kind: SurfaceEventKind;
  /** The device's own control index - the note or CC number. */
  index: number;
  /** 0..127. A `down` carries the strike velocity, a `pressure` the current aftertouch. */
  velocity: number;
  pad: Pad | null;
  button: string | null;
}

const NOTE_OFF = 0x80;
const NOTE_ON = 0x90;
const POLY_AFTERTOUCH = 0xa0;
const CONTROL_CHANGE = 0xb0;

/** Decode one incoming MIDI message, or null when it is not surface input.
 *
 *  Returns null rather than guessing for anything unrecognised - SysEx replies, clock, messages from
 *  some other device sharing the port - so a host can forward what it does not understand instead of
 *  having it silently mistaken for a pad press. */
export function decodeMessage(profile: LaunchpadProfile, data: readonly number[]): SurfaceEvent | null {
  if (data.length < 3) return null;
  const status = data[0] & 0xf0;
  const index = data[1];
  const value = data[2];

  if (status === NOTE_ON || status === NOTE_OFF) {
    const pad = padAt(index);
    // Note On with velocity 0 is the device's note-off (the manual says it sends exactly this).
    const kind: SurfaceEventKind = status === NOTE_OFF || value === 0 ? "up" : "down";
    return { kind, index, velocity: value, pad, button: pad ? null : buttonName(profile, index) };
  }

  if (status === POLY_AFTERTOUCH) {
    const pad = padAt(index);
    return { kind: "pressure", index, velocity: value, pad, button: pad ? null : buttonName(profile, index) };
  }

  if (status === CONTROL_CHANGE) {
    const name = buttonName(profile, index);
    if (name === null) return null; // a CC that is not one of our buttons is not surface input
    return { kind: value === 0 ? "up" : "down", index, velocity: value, pad: null, button: name };
  }

  return null;
}

/** Decode a batch, dropping whatever is not surface input. */
export function decodeMessages(profile: LaunchpadProfile, messages: readonly (readonly number[])[]): SurfaceEvent[] {
  const out: SurfaceEvent[] = [];
  for (const m of messages) {
    const ev = decodeMessage(profile, m);
    if (ev) out.push(ev);
  }
  return out;
}
