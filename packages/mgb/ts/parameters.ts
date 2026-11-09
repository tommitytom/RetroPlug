// mGB's DAW parameter table (docs/modular-graph-phase-f4a-design.md section 4.5): the lanes the synth's MIDI
// implementation map exposes, which the `mgb` role serves through its `parameters` hook. From the map in trash80/mGB's
// README: MIDI 1 PU1, 2 PU2, 3 WAV, 4 NOISE (channel 5 is POLY mode, for which the README documents no CCs, so it claims
// none).
import { expandRomSpec, type RomParameter, type RomSpec } from "@retroplug/core/romParameters";

const MGB_PU1 = 0, MGB_PU2 = 1, MGB_WAV = 2, MGB_NOI = 3;
const MGB: RomSpec = {
  voices: [["PU1", "PU1"], ["PU2", "PU2"], ["WAV", "WAV"], ["Noise", "Noi"]],
  ccs: [
    // CC1 and CC2 mean different things on WAV than on the pulses, so they are split into two specs.
    { cc: 1, name: "Pulse Width", short: "Width", channels: [MGB_PU1, MGB_PU2] },
    { cc: 2, name: "Envelope", short: "Env", channels: [MGB_PU1, MGB_PU2, MGB_NOI] },
    { cc: 1, name: "Shape", short: "Shape", channels: [MGB_WAV] },
    { cc: 2, name: "Shape Offset", short: "Offset", channels: [MGB_WAV] },
    { cc: 3, name: "Pitch Sweep", short: "Sweep", channels: [MGB_PU1, MGB_WAV] },
    { cc: 4, name: "Bend Range", short: "Bend", channels: [MGB_PU1, MGB_PU2, MGB_WAV] },
    { cc: 5, name: "Load Preset", short: "Preset", channels: [MGB_PU1, MGB_PU2, MGB_WAV, MGB_NOI] },
    { cc: 10, name: "Pan", short: "Pan", channels: [MGB_PU1, MGB_PU2, MGB_WAV, MGB_NOI] },
    { cc: 64, name: "Sustain", short: "Sus", channels: [MGB_PU1, MGB_PU2, MGB_WAV, MGB_NOI] },
  ],
};

/** mGB's lanes, in the order a host lists them. */
export const MGB_PARAMETERS: readonly RomParameter[] = expandRomSpec(MGB);
