// The MI.MAP app's RUNTIME half (docs/modular-graph-phase-f3-design.md section 4): the app function, the playback model
// it reads and its launch target, registered by the generated script entry before the session node (whose own module
// names no app). No schema: a script pack's bundle carries no zod, and neither does this module.
//
// The predictor is the leaf, not @retroplug/lsdj/playback: that barrel also exports the observed model and the
// sav-derived table builder, which would pull the WRAM reader and the whole sav codec into the bundle.
import type { ControllerAppRuntime, ControllerPlaybackModel } from "@retroplug/launchpad/apps";
import { PredictedLsdjModel, normaliseRowTicks } from "@retroplug/lsdj/playback/predict";
import { lsdjMidiMap } from "./lsdjMidiMap";
import { lsdjMidiMapTarget } from "./launchTarget";

/** The predictor over the song's per-row timing table. The table is whatever shape the app's song type has, and
 *  `normaliseRowTicks` is the one place that reads it. */
const playbackModel = (songRowTicks: unknown): ControllerPlaybackModel =>
  PredictedLsdjModel.fromRowTicks(normaliseRowTicks(songRowTicks));

const runtime: ControllerAppRuntime = {
  id: "lsdj-midimap",
  factory: () => lsdjMidiMap,
  playbackModel,
  target: lsdjMidiMapTarget,
};

export default runtime;
