// Where a controller app's row launches go.
//
// The app decides WHICH row to launch; the target decides what that means on the wire. Two of them
// matter (docs/launchpad-plan.md 7.3) and the difference is not in this file at all - it is in the `send`
// the caller injects:
//
//   emulated cart   send pushes into the system's MIDI inbox, and the existing `midiMap` role
//                   (dspRoles.ts) turns it into link-port bytes exactly as it always has
//   real Game Boy   send is ctx.emitMidiOut, and an Arduinoboy on the other end does the same job
//
// So both paths carry the SAME launch stream and are in sync by construction - the property
// Engine::setCoreByteSink gives the risa/N8 mirror. Note what is deliberately NOT here: the app does not
// reimplement the MI.MAP protocol. It emits the ordinary NoteOn/NoteOff that the shipped role already
// consumes, which is why there is no second copy of the wire format to keep in step.

/** The MI.MAP row range. Rows 254 and 255 are byte-identical to the 0xFE handshake and 0xFF clock
 *  sentinels, so they are not addressable (MEASURED, docs/launchpad-plan.md 2.5 B7). */
export const LSDJ_MAX_ROW = 253;

const NOTE_ON = 0x90;
const NOTE_OFF = 0x80;
const LAUNCH_VELOCITY = 100;

/** A sink for row launches. Implementations own the wire format; the app only names rows. */
export interface TrackerTarget {
  /** The highest launchable row. An app uses this to bound its grid rather than assuming 255. */
  readonly maxRow: number;
  launch(row: number): void;
  /** Release the launched row - MI.MAP's 0xFE handshake.
   *
   *  This does NOT stop playback (MEASURED, B5), and neither does launching an empty row (B8). Nothing
   *  in MI.MAP stops a cart except the host transport, so an app must not offer a stop pad and pretend. */
  release(row: number): void;
}

/** Encode a song row as the MI.MAP NoteOn the `midiMap` role decodes: ch1 carries rows 0..127 and ch2
 *  rows 128..255, which is `midiMapRow` read backwards. Null for a row outside the range. */
export function launchMessage(row: number, on = true): number[] | null {
  if (!Number.isInteger(row) || row < 0 || row > LSDJ_MAX_ROW) return null;
  const channel = row < 128 ? 0 : 1;
  return [(on ? NOTE_ON : NOTE_OFF) | channel, row & 0x7f, on ? LAUNCH_VELOCITY : 0];
}

/** An LSDj MI.MAP target over `send`. Out-of-range rows are dropped rather than wrapping onto some other
 *  row - a silent launch is a much smaller surprise than launching the wrong part of the song. */
export function lsdjMidiMapTarget(send: (data: number[]) => void): TrackerTarget {
  return {
    maxRow: LSDJ_MAX_ROW,
    launch(row: number): void {
      const m = launchMessage(row, true);
      if (m) send(m);
    },
    release(row: number): void {
      const m = launchMessage(row, false);
      if (m) send(m);
    },
  };
}

/** A target that discards everything - for a session with no cart attached yet, so an app can light its
 *  grid and respond to presses without a launch path existing. */
export const nullTarget: TrackerTarget = {
  maxRow: LSDJ_MAX_ROW,
  launch(): void {},
  release(): void {},
};
