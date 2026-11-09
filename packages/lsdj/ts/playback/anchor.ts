// Re-anchoring the predictor when the CART starts playing on its own.
//
// The predictor knows where the song is because it sent the launch and it generates the clock. It does not
// know about anything the player does on the handheld - and pressing START on LSDj's song screen is exactly
// that. The cart begins at whatever row LSDj's own cursor is on, the prediction carries on from wherever it
// thought it was, and from then until the next pad press the lit playhead points at the wrong row. Reported
// from a hardware session.
//
// The fix is the re-anchor sketched in docs/launchpad-plan.md 4.4, at the one moment it is both cheap and
// unambiguous: the not-playing -> playing EDGE. Mid-song correction would mean streaming the cart's position
// to the audio thread every block (that is M6, and it needs a memory seam the script nodes do not have); a start
// edge is a rare, discrete event that fits the existing "push it as role config" path exactly.
//
// Emulated carts only, necessarily: on a real Game Boy there is no WRAM to read and dead reckoning is all
// there is. That asymmetry is the whole shape of this feature, not a shortcut here.

import { CHANNELS, type LsdjState } from "../runtime";
import type { ControllerAnchor } from "@retroplug/tracker/trackerPlayback";

// The anchor itself is the tracker contract's (@retroplug/tracker/trackerPlayback), because the controller poll hands
// it to the graph without naming a cart; this module is what derives it from an LSDj state.
export type { ControllerAnchor };

/** Each channel's own song row, or null where it is not playing.
 *
 *  Per channel rather than one row for the song, because they genuinely diverge as their chains end
 *  (measured, docs/launchpad-plan.md 2.5 B4) - and the state's aggregate `songRow` is the max across
 *  channels, which is not any one channel's position. */
export function anchorRowsFromState(state: LsdjState): (number | null)[] {
  if (!state.supported) return CHANNELS.map(() => null);
  return CHANNELS.map((name) => {
    const c = state.channels[name];
    return c.playing ? c.songRow : null;
  });
}

/** Whether `state` shows anything playing at all - the signal the start edge is detected on. */
export function anyChannelPlaying(state: LsdjState): boolean {
  return state.supported && CHANNELS.some((name) => state.channels[name].playing);
}
