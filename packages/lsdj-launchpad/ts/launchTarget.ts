// Where the MI.MAP app's row launches go: the LSDj half of the controller's launch target contract
// (@retroplug/launchpad/launchTarget).
//
// The app decides WHICH row to launch; the target decides what that means on the wire. Two of them
// matter (docs/launchpad-plan.md 7.3) and the difference is not in this file at all - it is in the `send`
// the caller injects:
//
//   emulated cart   send goes out on the controller node's `launches` port into the cart's lsdj-sync
//                   node, whose `midiMap` mode (@retroplug/lsdj's lsdjSyncNode.ts) turns it into link-port bytes
//                   exactly as it always has
//   real Game Boy   send goes out on the node's `midiOut` port, and an Arduinoboy on the other end does
//                   the same job
//
// So both paths carry the SAME launch stream and are in sync by construction - the property buildGraph's
// fan-out gives an emulated cart and the N8 beside it, both fed one protocol node's output. Note what is
// deliberately NOT here: the app does not reimplement the MI.MAP protocol. It emits the ordinary
// NoteOn/NoteOff that the shipped role already consumes, which is why there is no second copy of the wire
// format to keep in step.
import type { TrackerTarget } from "@retroplug/launchpad/launchTarget";

/** The MI.MAP row range. Rows 254 and 255 are byte-identical to the 0xFE handshake and 0xFF clock
 *  sentinels, so they are not addressable (MEASURED, docs/launchpad-plan.md 2.5 B7). */
export const LSDJ_MAX_ROW = 253;

const NOTE_ON = 0x90;
const NOTE_OFF = 0x80;
const LAUNCH_VELOCITY = 100;

/** Encode a song row as the MI.MAP NoteOn the `midiMap` role decodes: ch1 carries rows 0..127 and ch2
 *  rows 128..255, which is `midiMapRow` read backwards. Null for a row outside the range. */
export function launchMessage(row: number, on = true): number[] | null {
  // A whole number, the ES5 way: MicroQuickJS has no Number.isInteger, and this module is in the portable core.
  if (typeof row !== "number" || Math.floor(row) !== row || row < 0 || row > LSDJ_MAX_ROW) return null;
  const channel = row < 128 ? 0 : 1;
  return [(on ? NOTE_ON : NOTE_OFF) | channel, row & 0x7f, on ? LAUNCH_VELOCITY : 0];
}

/** An LSDj MI.MAP target over `send`. Out-of-range rows are dropped rather than wrapping onto some other
 *  row - a silent launch is a much smaller surprise than launching the wrong part of the song. A session with no cart
 *  attached yet runs launchpad's `nullTarget(LSDJ_MAX_ROW)` rather than one of these. */
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
