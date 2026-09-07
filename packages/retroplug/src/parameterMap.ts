// Per-ROM DAW automation parameters: which MIDI CC each host parameter slot drives, and what to call
// it, so a DAW shows "Pulse 1 Duty" rather than "CC 1".
//
// The plugin exposes a FIXED pool of slots to the host and only ever re-labels them
// (spec/12-dynamic-parameters.md): neither CLAP nor VST3 can change a parameter count, symbol or range
// on a live plugin, and DPF keys its own state save/restore by symbol. This module is the "meaning"
// half - native asks for the map as JSON (`__rp_parameterMapJson`), caches it, and re-declares.
//
// A parameter write becomes an ordinary MIDI CC staged into the engine, so it is routed by the
// project's MIDI routing exactly as a CC the user played would be. That is why `channel` below is the
// PRE-routing channel, and why a slot is claimed only when the live routing mode can actually carry
// its channel to that system: an automation lane that cannot reach the ROM should not exist.
//
// The CC tables are transcribed from each ROM's own documentation, not guessed: mGB's from the MIDI
// implementation map in trash80/mGB's README, BlipToaster's from the per-chip tables in its
// docs/chips/*.md, each of which lists every CC that build responds to and on which channels.
import { MidiRouting } from "./settingsEnums";

/** One control a ROM exposes, expanded to a single automation lane. */
export interface RomParameter {
  /** 0-based MIDI channel the ROM listens on for this control, relative to the ROM's first voice. */
  channel: number;
  /** MIDI CC number, 0..127. */
  cc: number;
  /** Full name for the host's parameter list. */
  name: string;
  /** Abbreviated name for hosts that show a short parameter label. */
  shortName: string;
}

/** A claimed pool slot: the same descriptor, resolved to a pool index and a pre-routing channel. */
export interface ParameterSlot extends RomParameter {
  slot: number;
}

/** Slots the plugin reserves per system. Sized for the largest ROM in the set (BlipToaster's VRC7
 *  build, ~130 lanes across 12 channels) with headroom. Twins kCcSlotsPerSystem in PluginDSP.cpp;
 *  CHANGING IT SHIFTS HOST AUTOMATION on systems 2-4, whose pool indices are `system * this`. */
export const CC_SLOTS_PER_SYSTEM = 160;

/** Systems the pool covers. Matches the plugin's four stereo output pairs (a system routes to one). */
export const MAX_PARAMETER_SYSTEMS = 4;

// --------------------------------------------------------------------------------------------------
// ROM parameter tables

/** One CC a ROM responds to, and which of its 0-based channels accept it. */
interface CcSpec {
  cc: number;
  /** Parameter name, prefixed with the voice name unless `shared`. */
  name: string;
  /** Abbreviated name, prefixed with the voice's short tag unless `shared`. */
  short: string;
  /** 0-based channels that respond to it. */
  channels: number[];
  /** The control is chip-global rather than per-voice (one register shared by several channels), so it
   *  gets ONE lane, addressed on `channels[0]`, with no voice prefix. */
  shared?: boolean;
}

/** A ROM build: what its voices are called, and every CC it responds to. */
interface RomSpec {
  /** 0-based channel -> voice name. A gap (undefined) means that channel carries no voice. */
  voices: (readonly [full: string, short: string] | undefined)[];
  ccs: CcSpec[];
}

// Deliberately NOT exposed as lanes, on every ROM: the RPN bend-range handshake (CC101/100/6/38 is a
// three-message sequence, not a value) and the channel-mode messages (CC120 All Sound Off, CC121 Reset
// All Controllers, CC123 All Notes Off) - automating a panic message is actively harmful.

// --- BlipToaster -----------------------------------------------------------------------------------
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
  { cc: 76, name: "LFO Rate", short: "LFORat", channels: [P1, P2, TRI, NOI] },
  { cc: 77, name: "Vibrato Depth", short: "VibDep", channels: [P1, P2, TRI] },
  { cc: 78, name: "Tremolo Depth", short: "TrmDep", channels: [P1, P2, NOI] },
  { cc: 79, name: "LFO Shape", short: "LFOShp", channels: [P1, P2, TRI, NOI] },
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
      76: [V6_P1, V6_P2, V6_SAW, POLY],
      77: [V6_P1, V6_P2, V6_SAW, POLY],
      78: [V6_P1, V6_P2, V6_SAW, POLY],
      79: [V6_P1, V6_P2, V6_SAW, POLY],
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
    // CC15 (velocity curve) is not on the VRC7 build at all; CC1 becomes instrument select on FM.
    ...extendCore({
      1: [...VRC7_FM, POLY],
      7: [...VRC7_FM, POLY],
      64: [...VRC7_FM, POLY],
      76: [...VRC7_FM, POLY],
      77: [...VRC7_FM, POLY],
      78: [...VRC7_FM, POLY],
      79: [...VRC7_FM, POLY],
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
      1: [S5B_A, S5B_B, S5B_C],
      7: [S5B_A, S5B_B, S5B_C],
      20: [S5B_A, S5B_B, S5B_C],
      64: [S5B_A, S5B_B, S5B_C],
      76: [S5B_A, S5B_B, S5B_C],
      77: [S5B_A, S5B_B, S5B_C],
      78: [S5B_A, S5B_B, S5B_C],
      79: [S5B_A, S5B_B, S5B_C],
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
    76: N163_CH,
    77: N163_CH,
    78: N163_CH,
    79: N163_CH,
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
    76: [M5_P1, M5_P2, POLY],
    77: [M5_P1, M5_P2, POLY],
    78: [M5_P1, M5_P2, POLY],
    79: [M5_P1, M5_P2, POLY],
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

// --- mGB -------------------------------------------------------------------------------------------
// From the MIDI implementation map in trash80/mGB's README: MIDI 1 PU1, 2 PU2, 3 WAV, 4 NOISE.
// (Channel 5 is POLY mode, for which the README documents no CCs, so it claims none.)
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

// --------------------------------------------------------------------------------------------------

/** Expand a build's CC table into one lane per (CC, channel), in channel-then-CC order so a host's
 *  parameter list reads voice by voice. A `shared` CC contributes a single un-prefixed lane. */
export function expandRomSpec(spec: RomSpec): RomParameter[] {
  const out: RomParameter[] = [];

  for (let ch = 0; ch < spec.voices.length; ch++) {
    const voice = spec.voices[ch];
    if (!voice) continue;
    for (const c of spec.ccs) {
      if (!c.channels.includes(ch)) continue;
      if (c.shared) {
        if (c.channels[0] !== ch) continue; // one lane only, on its first channel
        out.push({ channel: ch, cc: c.cc, name: c.name, shortName: c.short });
      } else {
        out.push({
          channel: ch,
          cc: c.cc,
          name: `${voice[0]} ${c.name}`,
          shortName: `${voice[1]} ${c.short}`,
        });
      }
    }
  }

  return out;
}

/** Role kind -> the parameters that ROM exposes. For `bliptoaster` the set depends on the build, so
 *  it is resolved from the role's `chip` config instead; see `parametersForRole`. */
export const MGB_PARAMETERS: RomParameter[] = expandRomSpec(MGB);

/** The parameters a role instance contributes, or undefined if it is not a parameter-bearing role. */
export function parametersForRole(role: { kind: string; config?: unknown }): RomParameter[] | undefined {
  if (role.kind === "mgb") return MGB_PARAMETERS;
  if (role.kind === "bliptoaster") {
    const chip = (role.config as { chip?: string } | undefined)?.chip ?? "2a03";
    // An unknown chip (an older project, or a build newer than this table) falls back to the 2A03 core,
    // which every BlipToaster build has: fewer lanes, never wrong ones.
    return expandRomSpec(BLIPTOASTER_SPECS[chip] ?? BT_2A03);
  }
  return undefined;
}

/** The MIDI channel a CC must be sent on for the project's routing to deliver it to `systemIndex`,
 *  or null when this routing mode cannot address that voice of that system at all. The inverse of
 *  `routeEvent` in midiRouting.ts - keep the two in step. */
export function preRoutingChannel(
  mode: MidiRouting,
  systemIndex: number,
  systemCount: number,
  romChannel: number,
): number | null {
  if (systemIndex < 0 || systemIndex >= systemCount) return null;
  if (romChannel < 0 || romChannel > 15) return null;

  switch (mode) {
    case MidiRouting.SendToAll:
      // Every system receives every event, so the ROM channel passes through untouched. With more
      // than one system the other systems see it too - the same thing a played CC does.
      return romChannel;
    case MidiRouting.FourChannelsPerInstance: {
      // Instance N owns channels 4N..4N+3, delivered unchanged, so only a ROM's first four voices
      // are reachable. mGB's POLY and BlipToaster's DMC + expansion voices fall off the end here.
      if (romChannel >= 4) return null;
      const chan = systemIndex * 4 + romChannel;
      return chan <= 15 && Math.floor(chan / 4) % systemCount === systemIndex ? chan : null;
    }
    case MidiRouting.OneChannelPerInstance:
    case MidiRouting.MidiChannelToInstance:
      // One channel per instance: only the ROM's first voice is addressable.
      // (MidiChannelToInstance additionally rewrites the channel to 0 on delivery, which is what the
      // ROM's first voice listens on anyway.)
      return romChannel === 0 && systemIndex % systemCount === systemIndex ? systemIndex : null;
    default:
      return null;
  }
}

/** A system as this projection needs to see it: just its roles. */
export interface ParameterSystem {
  roles: { kind: string; config?: unknown }[];
}

/** Project the loaded systems onto the plugin's fixed slot pool. Slot `i * CC_SLOTS_PER_SYSTEM + n`
 *  belongs to system `i`; every slot not returned stays unclaimed (hidden, generically named). */
export function projectParameterMap(systems: ParameterSystem[], mode: MidiRouting): ParameterSlot[] {
  const out: ParameterSlot[] = [];
  const systemCount = systems.length;

  for (let i = 0; i < Math.min(systemCount, MAX_PARAMETER_SYSTEMS); i++) {
    let table: RomParameter[] | undefined;
    for (const r of systems[i].roles) {
      table = parametersForRole(r);
      if (table) break;
    }
    if (!table) continue;

    let n = 0;
    for (const p of table) {
      if (n >= CC_SLOTS_PER_SYSTEM) break;
      const channel = preRoutingChannel(mode, i, systemCount, p.channel);
      if (channel === null) continue; // unreachable under this routing mode: leave the slot unclaimed
      out.push({ slot: i * CC_SLOTS_PER_SYSTEM + n, cc: p.cc, channel, name: p.name, shortName: p.shortName });
      n++;
    }
  }

  return out;
}
