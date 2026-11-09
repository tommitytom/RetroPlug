// mGB's DAW parameter lanes (docs/modular-graph-phase-f4a-design.md section 4.5): the table is the synth package's, and
// the shell's slot projection asks the `mgb` role for it. The recording below was taken from the design commit's
// parameterMap.ts before the table moved, so this pins the MOVED table lane for lane: a lane added, dropped, renamed or
// renumbered changes what a host lists, and nothing else would notice.
import { test, expect } from "@retroplug/testing";
import type { RomParameter } from "@retroplug/core/romParameters";
import { MGB_PARAMETERS } from "../parameters";

/** The lanes as the shell expanded them before the move, recorded from 96d748e5c's parameterMap.ts. */
const RECORDED_MGB: readonly RomParameter[] = [
  { channel: 0, cc: 1, name: "PU1 Pulse Width", shortName: "PU1 Width" },
  { channel: 0, cc: 2, name: "PU1 Envelope", shortName: "PU1 Env" },
  { channel: 0, cc: 3, name: "PU1 Pitch Sweep", shortName: "PU1 Sweep" },
  { channel: 0, cc: 4, name: "PU1 Bend Range", shortName: "PU1 Bend" },
  { channel: 0, cc: 5, name: "PU1 Load Preset", shortName: "PU1 Preset" },
  { channel: 0, cc: 10, name: "PU1 Pan", shortName: "PU1 Pan" },
  { channel: 0, cc: 64, name: "PU1 Sustain", shortName: "PU1 Sus" },
  { channel: 1, cc: 1, name: "PU2 Pulse Width", shortName: "PU2 Width" },
  { channel: 1, cc: 2, name: "PU2 Envelope", shortName: "PU2 Env" },
  { channel: 1, cc: 4, name: "PU2 Bend Range", shortName: "PU2 Bend" },
  { channel: 1, cc: 5, name: "PU2 Load Preset", shortName: "PU2 Preset" },
  { channel: 1, cc: 10, name: "PU2 Pan", shortName: "PU2 Pan" },
  { channel: 1, cc: 64, name: "PU2 Sustain", shortName: "PU2 Sus" },
  { channel: 2, cc: 1, name: "WAV Shape", shortName: "WAV Shape" },
  { channel: 2, cc: 2, name: "WAV Shape Offset", shortName: "WAV Offset" },
  { channel: 2, cc: 3, name: "WAV Pitch Sweep", shortName: "WAV Sweep" },
  { channel: 2, cc: 4, name: "WAV Bend Range", shortName: "WAV Bend" },
  { channel: 2, cc: 5, name: "WAV Load Preset", shortName: "WAV Preset" },
  { channel: 2, cc: 10, name: "WAV Pan", shortName: "WAV Pan" },
  { channel: 2, cc: 64, name: "WAV Sustain", shortName: "WAV Sus" },
  { channel: 3, cc: 2, name: "Noise Envelope", shortName: "Noi Env" },
  { channel: 3, cc: 5, name: "Noise Load Preset", shortName: "Noi Preset" },
  { channel: 3, cc: 10, name: "Noise Pan", shortName: "Noi Pan" },
  { channel: 3, cc: 64, name: "Noise Sustain", shortName: "Noi Sus" },
];

test("mGB's role serves exactly the lanes the shell's own table expanded", () => {
  expect(MGB_PARAMETERS).toEqual(RECORDED_MGB);
});
