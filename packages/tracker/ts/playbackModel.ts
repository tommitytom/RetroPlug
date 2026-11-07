// Where a controller surface gets its "what is playing right now" from - the third tracker concern,
// alongside the song catalog (the battery) and the asset catalog (the ROM's replaceable parts).
//
// It exists because the same Launchpad app has to run against two very different truths:
//
//   OBSERVED  - an emulated cart, whose position can simply be read out of WRAM.
//   PREDICTED - a REAL Game Boy over an Arduinoboy, where there are no memory reads at all and MI.MAP
//               carries no return channel, so position has to be simulated from the song file plus the
//               clock we are generating (docs/launchpad-plan.md 4.2).
//
// The app codes against this interface and never learns which one it has: LED fidelity degrades on the
// hardware path, the app's behaviour does not. Keeping the seam here rather than in the LSDj tree is
// deliberate - risa is the same shape and should implement it without the app changing.
//
// Deliberately ROW-LEVEL. Chain step and phrase step are not modelled: the Launchpad grid launches song
// rows and lights song rows, and every extra field is another thing that can silently drift out of sync
// on the hardware path for no visible gain.

/** One channel's position. `songRow` is null when the channel has nothing to play. */
export interface ChannelPosition {
  playing: boolean;
  songRow: number | null;
}

/** A whole-cart position. `channels` is in the tracker's own channel order (LSDj: pu1, pu2, wav, noi).
 *  Channels each carry their OWN row, which is not a nicety - a real cart's channels advance
 *  independently as their chains end (measured, docs/launchpad-plan.md 2.5 B4). */
export interface PlaybackPosition {
  playing: boolean;
  channels: ChannelPosition[];
}

/** Which cells of the song grid hold something launchable - what the surface lights as "available". */
export interface PlaybackGrid {
  readonly rowCount: number;
  readonly channelCount: number;
  /** True when `channel` has a playable chain at `row`. Out-of-range reads are false, not an error. */
  hasContent(channel: number, row: number): boolean;
}

/** The read side, which is all a surface needs. */
export interface PlaybackModel {
  readonly channelCount: number;
  position(): PlaybackPosition;
  grid(): PlaybackGrid;
}

/** The predicted side additionally has to be DRIVEN, because nothing else is telling it what happened.
 *  A host feeds it the same launches it sends to the cart and the same clock it generates. */
export interface PredictivePlaybackModel extends PlaybackModel {
  /** A launch, exactly as the cart sees it: every channel jumps to `row` (measured, B4). */
  launch(row: number): void;
  /** Advance by `ticks` clock ticks - the same ticks the host is putting on the wire. */
  advance(ticks: number): void;
  /** Playback stopped. Note this is NOT what the MI.MAP NoteOff handshake does (B5) - it is the app's
   *  own stop, or the host transport stopping. */
  stop(): void;
  /** Back to nothing playing, position forgotten. */
  reset(): void;
}

/** An empty position for `channelCount` channels - the "nothing is playing" answer every model starts at. */
export function idlePosition(channelCount: number): PlaybackPosition {
  const channels: ChannelPosition[] = [];
  for (let i = 0; i < channelCount; i++) channels.push({ playing: false, songRow: null });
  return { playing: false, channels };
}
