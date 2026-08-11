// LSDj playback position: observed from a live cart's WRAM, or predicted from the song file plus the
// clock we generate when there is no cart to read (a real Game Boy over an Arduinoboy). Both satisfy
// the tracker-agnostic seam in ../../tracker/playbackModel.
export { PredictedLsdjModel, phraseTicks, chainPhraseCount } from "./predict";
export { ObservedLsdjModel, positionFromState } from "./observe";
