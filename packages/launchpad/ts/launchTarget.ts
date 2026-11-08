// Where a controller app's row launches go (docs/modular-graph-phase-f3-design.md section 4): the contract between an
// app and whichever wire its launches reach. The app decides WHICH row to launch; the target owns what that means.
//
// The implementation belongs to the app's owner, not here: the MI.MAP target that encodes a row as a NoteOn lives with
// the LSDj bridge (@retroplug/lsdj-launchpad/launchTarget), because the note numbers are MI.MAP's protocol and this
// package knows no tracker. A host builds the target and hands the session a `send` that reaches the cart.
//
// A target that discards everything is here rather than with a tracker, because it names no wire: it is what a host
// with no launch path of its own can hand a session, so an app lights its grid and answers presses without one.

/** A sink for row launches. Implementations own the wire format; the app only names rows. */
export interface TrackerTarget {
  /** The highest launchable row. An app uses this to bound its grid rather than assuming 255. */
  readonly maxRow: number;
  launch(row: number): void;
  /** Release the launched row - the app's own release message, if it has one. */
  release(row: number): void;
}

/** A target that discards everything, for a session whose launches have no wire yet, so an app can light its grid and
 *  respond to presses without one. `maxRow` is the app's own range: this knows no tracker's. */
export function nullTarget(maxRow: number): TrackerTarget {
  return { maxRow, launch(): void {}, release(): void {} };
}
