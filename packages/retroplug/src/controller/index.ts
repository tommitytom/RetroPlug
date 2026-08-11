// The controller layer: where a Launchpad surface (src/launchpad) meets a tracker's playback position
// (src/tracker/playbackModel) and turns pad presses into row launches.
//
// Pure TS with no I/O and no native dependency - a session takes bytes in and hands bytes back, so the
// whole feature runs against a fake device long before it can reach real hardware.
export {
  ControllerSession, isPredictive,
  type ControllerApp, type ControllerCtx, type SessionInput, type SessionOptions,
} from "./session";

export {
  lsdjMidiMapTarget, launchMessage, nullTarget, LSDJ_MAX_ROW, type TrackerTarget,
} from "./trackerTarget";

export { ControllerRegistry, registerControllerApps, type ControllerAppType } from "./registry";

export {
  lsdjMidiMap, rowAt, channelAt, followRow,
  CHANNELS_ACROSS, PANE_ROWS, WINDOW_ROWS, MAX_PAGE, QUANTISE_VALUES, type Quantise,
} from "./apps/lsdjMidiMap";
