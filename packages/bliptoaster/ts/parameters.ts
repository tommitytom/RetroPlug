// BlipToaster's DAW parameter tables (docs/modular-graph-phase-f4a-design.md section 4.5): one per expansion build,
// keyed by the `chip` on the system's `bliptoaster` role, plus the lanes each expands to. The `bliptoaster` role serves
// them through its `parameters` hook, so the shell's slot projection asks this cart rather than holding a copy.
//
// The CCs are transcribed from BlipToaster's own per-chip documentation (its docs/chips/*.md), not guessed.
import { expandRomSpec, type CcSpec, type RomParameter, type RomSpec } from "@retroplug/core/romParameters";

// Channel map is the same on every build: 1 Pulse 1, 2 Pulse 2, 3 Triangle, 4 Noise, 5 DMC, then the
// expansion voices from 6, and (where the build has it) polyphony on 12.
const BT_CORE_VOICES = [
  ["Pulse 1", "P1"],
  ["Pulse 2", "P2"],
  ["Triangle", "Tri"],
  ["Noise", "Noi"],
  ["DMC", "DMC"],
] as const;

const P1 = 0, P2 = 1, TRI = 2, NOI = 3, DMC = 4, POLY = 11;

// The 2A03 core, present on every BlipToaster build. Per-CC channel lists are from docs/chips/2a03.md.
const BT_CORE_CCS: CcSpec[] = [
  { cc: 1, name: "Duty", short: "Duty", channels: [P1, P2] },
  { cc: 7, name: "Volume", short: "Vol", channels: [P1, P2, NOI] },
  { cc: 15, name: "Velocity Curve", short: "VelCrv", channels: [P1, P2, NOI] },
  { cc: 20, name: "Envelope Mode", short: "EnvMod", channels: [P1, P2, NOI] },
  { cc: 21, name: "Sustain / Length Flag", short: "Sus", channels: [P1, P2, TRI, NOI] },
  { cc: 22, name: "Envelope Rate", short: "EnvRat", channels: [P1, P2, NOI] },
  { cc: 23, name: "Gate Length", short: "Gate", channels: [P1, P2, TRI, NOI] },
  { cc: 24, name: "Sweep Rate", short: "SwpRat", channels: [P1, P2] },
  { cc: 25, name: "Sweep Direction", short: "SwpDir", channels: [P1, P2] },
  { cc: 26, name: "Sweep Shift", short: "SwpShf", channels: [P1, P2] },
  { cc: 27, name: "Linear Reload", short: "LinRel", channels: [TRI] },
  { cc: 64, name: "Sustain Pedal", short: "Pedal", channels: [P1, P2, TRI, NOI] },
  { cc: 115, name: "MOD Hack", short: "MOD", channels: [P1, P2] },
  { cc: 116, name: "MOD Rate", short: "MODRat", channels: [P1, P2] },
  { cc: 117, name: "Fine Bend", short: "Fine", channels: [P1, P2, TRI] },
  // DMC / sample channel
  { cc: 3, name: "Sample Rate", short: "Rate", channels: [DMC] },
  { cc: 9, name: "Traveler Step", short: "TrvStp", channels: [DMC] },
  { cc: 14, name: "Kit / Index Mode", short: "Kit", channels: [DMC] },
  { cc: 85, name: "Address Override", short: "AdrEn", channels: [DMC] },
  { cc: 86, name: "Start Address", short: "Start", channels: [DMC] },
  { cc: 87, name: "Length Override", short: "Len", channels: [DMC] },
  { cc: 88, name: "Loop", short: "Loop", channels: [DMC] },
  { cc: 89, name: "Direct PCM", short: "PCM", channels: [DMC] },
  { cc: 90, name: "DAC Value", short: "DAC", channels: [DMC] },
  { cc: 118, name: "Wave Traveler", short: "Trav", channels: [DMC] },
  { cc: 119, name: "Traveler Speed", short: "TrvSpd", channels: [DMC] },
];

/** Copy the core CC set, adding `channels` to whichever CCs an expansion build extends. */
function extendCore(extra: Record<number, number[]>): CcSpec[] {
  return BT_CORE_CCS.map((c) =>
    extra[c.cc] ? { ...c, channels: [...c.channels, ...extra[c.cc]] } : c,
  );
}

const BT_2A03: RomSpec = { voices: [...BT_CORE_VOICES], ccs: BT_CORE_CCS };

// VRC6: 2 pulse (8 duty steps) + a sawtooth whose rate register is its amplitude, plus square polyphony.
const V6_P1 = 5, V6_P2 = 6, V6_SAW = 7;
const BT_VRC6: RomSpec = {
  voices: [
    ...BT_CORE_VOICES,
    ["VRC6 Pulse 1", "V6P1"],
    ["VRC6 Pulse 2", "V6P2"],
    ["VRC6 Saw", "Saw"],
    undefined, undefined, undefined,
    ["Poly", "Poly"],
  ],
  ccs: [
    ...extendCore({
      1: [V6_P1, V6_P2, POLY],
      7: [V6_P1, V6_P2, V6_SAW, POLY],
      15: [V6_P1, V6_P2, V6_SAW, POLY],
      64: [V6_P1, V6_P2, V6_SAW, POLY],
      115: [POLY],
      116: [POLY],
      117: [V6_P1, V6_P2, V6_SAW, POLY],
    }),
    { cc: 71, name: "Overdrive", short: "Drive", channels: [V6_SAW] },
  ],
};

// VRC7: six 2-op FM voices plus FM polyphony. The custom patch is a SINGLE shared user instrument
// (OPLL has only one), so its CCs are chip-global and get one lane each, not one per FM voice.
const FM1 = 5;
const VRC7_FM = [5, 6, 7, 8, 9, 10];
const vrc7Patch = (cc: number, name: string, short: string): CcSpec => ({
  cc, name, short, channels: [FM1], shared: true,
});
const BT_VRC7: RomSpec = {
  voices: [
    ...BT_CORE_VOICES,
    ["FM 1", "FM1"], ["FM 2", "FM2"], ["FM 3", "FM3"],
    ["FM 4", "FM4"], ["FM 5", "FM5"], ["FM 6", "FM6"],
    ["Poly", "Poly"],
  ],
  ccs: [
    // CC15 (velocity curve) is not on the VRC7 build at all; CC1 (the wheel) is unassigned on FM since
    // BlipToaster 2026-10-22 (it was vibrato depth, an alias of the ROM's LFO, which is gone).
    ...extendCore({
      7: [...VRC7_FM, POLY],
      64: [...VRC7_FM, POLY],
      117: [...VRC7_FM, POLY],
    }).filter((c) => c.cc !== 15),
    vrc7Patch(70, "FM Sustain Level", "FMSus"),
    vrc7Patch(71, "FM Harmonic Intensity", "FMHarm"),
    vrc7Patch(72, "FM Release", "FMRel"),
    vrc7Patch(73, "FM Attack", "FMAtk"),
    vrc7Patch(74, "FM Brightness", "FMBrt"),
    vrc7Patch(75, "FM Decay", "FMDec"),
    vrc7Patch(102, "FM Modulator Multiple", "FMMMul"),
    vrc7Patch(103, "FM Carrier Multiple", "FMCMul"),
    vrc7Patch(104, "FM Modulator Attack", "FMMAtk"),
    vrc7Patch(105, "FM Modulator Decay", "FMMDec"),
    vrc7Patch(106, "FM Modulator Sustain", "FMMSus"),
    vrc7Patch(107, "FM Modulator Release", "FMMRel"),
    vrc7Patch(108, "FM Vibrato", "FMVib"),
    vrc7Patch(109, "FM Tremolo", "FMTrm"),
    vrc7Patch(110, "FM Sustaining Envelope", "FMEG"),
    vrc7Patch(111, "FM Modulator Waveform", "FMMWav"),
    vrc7Patch(112, "FM Carrier Waveform", "FMCWav"),
    vrc7Patch(113, "FM Key-Scale Level", "FMKSL"),
    vrc7Patch(114, "FM Key-Scale Rate", "FMKSR"),
  ],
};

// Sunsoft 5B: three squares sharing ONE hardware envelope and ONE noise generator, so CC28/29/30 are
// chip-global (one lane each), while the per-voice noise mix and volume are not.
const S5B_A = 5, S5B_B = 6, S5B_C = 7;
const BT_S5B: RomSpec = {
  voices: [
    ...BT_CORE_VOICES,
    ["Square A", "SqA"], ["Square B", "SqB"], ["Square C", "SqC"],
  ],
  ccs: [
    ...extendCore({
      7: [S5B_A, S5B_B, S5B_C],
      20: [S5B_A, S5B_B, S5B_C],
      64: [S5B_A, S5B_B, S5B_C],
      117: [S5B_A, S5B_B, S5B_C],
    }),
    { cc: 28, name: "S5B Envelope Rate", short: "S5BRat", channels: [S5B_A], shared: true },
    { cc: 29, name: "S5B Envelope Shape", short: "S5BShp", channels: [S5B_A], shared: true },
    { cc: 30, name: "S5B Noise Period", short: "S5BNoi", channels: [S5B_A], shared: true },
  ],
};

// Namco 163: four wavetable voices; CC1 selects the waveform.
const N163_CH = [5, 6, 7, 8];
const BT_N163: RomSpec = {
  voices: [
    ...BT_CORE_VOICES,
    ["Wave 0", "W0"], ["Wave 1", "W1"], ["Wave 2", "W2"], ["Wave 3", "W3"],
  ],
  ccs: extendCore({
    1: N163_CH,
    7: N163_CH,
    15: N163_CH,
    64: N163_CH,
    117: N163_CH,
  }),
};

// MMC5: two squares mirroring the 2A03 pulses (envelope + length, but no sweep), plus square polyphony.
const M5_P1 = 5, M5_P2 = 6;
const BT_MMC5: RomSpec = {
  voices: [
    ...BT_CORE_VOICES,
    ["MMC5 Pulse 1", "M5P1"], ["MMC5 Pulse 2", "M5P2"],
    undefined, undefined, undefined, undefined,
    ["Poly", "Poly"],
  ],
  ccs: extendCore({
    1: [M5_P1, M5_P2, POLY],
    7: [M5_P1, M5_P2, POLY],
    15: [M5_P1, M5_P2, POLY],
    20: [M5_P1, M5_P2],
    21: [M5_P1, M5_P2],
    22: [M5_P1, M5_P2],
    23: [M5_P1, M5_P2],
    64: [M5_P1, M5_P2, POLY],
    115: [M5_P1, M5_P2, POLY],
    116: [M5_P1, M5_P2, POLY],
    117: [M5_P1, M5_P2, POLY],
  }),
};

/** BlipToaster build -> its parameter set. Keyed by the `chip` on the system's `bliptoaster` role. */
export const BLIPTOASTER_SPECS: Record<string, RomSpec> = {
  "2a03": BT_2A03,
  vrc6: BT_VRC6,
  vrc7: BT_VRC7,
  s5b: BT_S5B,
  n163: BT_N163,
  mmc5: BT_MMC5,
};

/** The lanes a build exposes, by the `chip` config its role carries. An unknown chip (an older project, or a build newer
 *  than this table) falls back to the 2A03 core, which every BlipToaster build has: fewer lanes, never wrong ones. */
export function blipToasterParameters(config: Record<string, unknown>): readonly RomParameter[] {
  const chip = typeof config.chip === "string" ? config.chip : "2a03";
  return expandRomSpec(BLIPTOASTER_SPECS[chip] ?? BT_2A03);
}
