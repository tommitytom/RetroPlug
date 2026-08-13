// The ground-truth half of the playback seam: an emulated cart's position, read rather than predicted.
//
// This is a thin adapter, and that is the point. `LsdjReader` already decodes everything needed out of a
// WRAM snapshot; all this does is narrow it to the row-level shape the surface consumes, so a controller
// app written against PlaybackModel cannot tell whether it is driving an emulator or a real Game Boy.
//
// It carries no memory-access seam of its own. The caller supplies the WRAM (and, for the grid, the
// decoded song), because where those come from differs per host: the control plane has backend.readRam
// over RPC, while a DSP-thread role will read the live core directly once that seam exists (M6 in
// docs/launchpad-plan.md).

import type { Song } from "../model";
import { CHANNELS, type LsdjState } from "../runtime";
import {
  idlePosition,
  type ChannelPosition,
  type PlaybackGrid,
  type PlaybackModel,
  type PlaybackPosition,
} from "../../tracker/playbackModel";
import { PredictedLsdjModel } from "./predict";

/** Narrow a decoded `LsdjState` to a row-level position.
 *
 *  Reads each channel's OWN `songRow` rather than the state's aggregate one: that field is the max
 *  across channels (the runtime reader's GBPresenter convention), which zig-zags meaninglessly once
 *  channels diverge - and they do diverge in normal play (measured, docs/launchpad-plan.md 2.5 B4). */
export function positionFromState(state: LsdjState): PlaybackPosition {
  if (!state.supported) return idlePosition(CHANNELS.length);
  const channels: ChannelPosition[] = CHANNELS.map((name) => {
    const c = state.channels[name];
    return { playing: c.playing, songRow: c.playing ? c.songRow : null };
  });
  return { playing: channels.some((c) => c.playing), channels };
}

/** Observed playback for an emulated cart. `setState` is called by whoever owns the WRAM sampling.
 *  The grid comes from the song, since "which rows hold content" is a property of the song rather than
 *  of the moment - and the predicted model derives it identically, so both agree by construction. */
export class ObservedLsdjModel implements PlaybackModel {
  readonly channelCount = CHANNELS.length;

  private current: PlaybackPosition;
  private readonly gridSource: PredictedLsdjModel;

  constructor(song: Song) {
    this.current = idlePosition(this.channelCount);
    this.gridSource = new PredictedLsdjModel(song);
  }

  /** Feed the latest decoded runtime state (once per UI frame, or once per audio block). */
  setState(state: LsdjState): void {
    this.current = positionFromState(state);
  }

  position(): PlaybackPosition {
    return this.current;
  }

  grid(): PlaybackGrid {
    return this.gridSource.grid();
  }
}
