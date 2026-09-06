// Per-ROM DAW automation parameters: which MIDI CC each host parameter slot drives, and what to call
// it, so a DAW shows "PU1 Pulse Width" rather than "CC 1".
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
import { MidiRouting } from "./settingsEnums";

/** One controllable parameter a ROM exposes, in the ROM's own terms. */
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

/** Slots the plugin reserves per system. Also the cap on how many entries of a ROM table are used. */
export const CC_SLOTS_PER_SYSTEM = 16;

/** Systems the pool covers. Matches the plugin's four stereo output pairs (a system routes to one). */
export const MAX_PARAMETER_SYSTEMS = 4;

// mGB (trash80), from the MIDI implementation map in the official README. mGB listens on five MIDI
// channels: PU1, PU2, WAV, NOISE and POLY. Deliberately omitted are the setup / one-shot controls that
// make no sense as automation lanes: CC4 (pitchbend range), CC5 (load preset) and CC64 (sustain).
export const MGB_PARAMETERS: RomParameter[] = [
  { channel: 0, cc: 1, name: "PU1 Pulse Width", shortName: "PU1 Wid" },
  { channel: 0, cc: 2, name: "PU1 Envelope", shortName: "PU1 Env" },
  { channel: 0, cc: 3, name: "PU1 Pitch Sweep", shortName: "PU1 Swp" },
  { channel: 0, cc: 10, name: "PU1 Pan", shortName: "PU1 Pan" },
  { channel: 1, cc: 1, name: "PU2 Pulse Width", shortName: "PU2 Wid" },
  { channel: 1, cc: 2, name: "PU2 Envelope", shortName: "PU2 Env" },
  { channel: 1, cc: 10, name: "PU2 Pan", shortName: "PU2 Pan" },
  { channel: 2, cc: 1, name: "WAV Shape", shortName: "WAV Shp" },
  { channel: 2, cc: 2, name: "WAV Shape Offset", shortName: "WAV Ofs" },
  { channel: 2, cc: 3, name: "WAV Pitch Sweep", shortName: "WAV Swp" },
  { channel: 2, cc: 10, name: "WAV Pan", shortName: "WAV Pan" },
  { channel: 3, cc: 2, name: "Noise Envelope", shortName: "NOI Env" },
  { channel: 3, cc: 10, name: "Noise Pan", shortName: "NOI Pan" },
];

// BlipToaster, from its own CC definitions (bliptoaster/src/midi/main.h) and README channel map:
// 1 Pulse 1, 2 Pulse 2, 3 Triangle, 4 Noise, 5 DMC samples. BlipToaster exposes far more CCs than a
// pool slot each; this is the subset worth an automation lane. The LFO (CC76-78) is per-channel, so
// the pool carries Pulse 1's.
export const BLIPTOASTER_PARAMETERS: RomParameter[] = [
  { channel: 0, cc: 1, name: "Pulse 1 Duty", shortName: "P1 Duty" },
  { channel: 0, cc: 7, name: "Pulse 1 Volume", shortName: "P1 Vol" },
  { channel: 0, cc: 24, name: "Pulse 1 Sweep Rate", shortName: "P1 Swp" },
  { channel: 0, cc: 26, name: "Pulse 1 Sweep Shift", shortName: "P1 Shf" },
  { channel: 1, cc: 1, name: "Pulse 2 Duty", shortName: "P2 Duty" },
  { channel: 1, cc: 7, name: "Pulse 2 Volume", shortName: "P2 Vol" },
  { channel: 1, cc: 24, name: "Pulse 2 Sweep Rate", shortName: "P2 Swp" },
  { channel: 1, cc: 26, name: "Pulse 2 Sweep Shift", shortName: "P2 Shf" },
  { channel: 2, cc: 27, name: "Triangle Linear", shortName: "Tri Lin" },
  { channel: 3, cc: 7, name: "Noise Volume", shortName: "Noi Vol" },
  { channel: 3, cc: 22, name: "Noise Env Rate", shortName: "Noi Env" },
  { channel: 4, cc: 3, name: "Sample Rate", shortName: "DMC Rat" },
  { channel: 4, cc: 14, name: "Sample Bank", shortName: "DMC Bnk" },
  { channel: 0, cc: 76, name: "LFO Rate", shortName: "LFO Rat" },
  { channel: 0, cc: 77, name: "Vibrato Depth", shortName: "Vib Dep" },
  { channel: 0, cc: 78, name: "Tremolo Depth", shortName: "Trm Dep" },
];

/** Role kind → the parameters that ROM exposes. A system with no listed role claims no slots. */
export const ROLE_PARAMETERS: Record<string, RomParameter[]> = {
  mgb: MGB_PARAMETERS,
  bliptoaster: BLIPTOASTER_PARAMETERS,
};

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
      // are reachable. mGB's POLY and BlipToaster's DMC channel fall off the end here.
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

/** A system as this projection needs to see it: just its role kinds. */
export interface ParameterSystem {
  roles: { kind: string }[];
}

/** Project the loaded systems onto the plugin's fixed slot pool. Slot `i * CC_SLOTS_PER_SYSTEM + n`
 *  belongs to system `i`; every slot not returned stays unclaimed (hidden, generically named). */
export function projectParameterMap(systems: ParameterSystem[], mode: MidiRouting): ParameterSlot[] {
  const out: ParameterSlot[] = [];
  const systemCount = systems.length;

  for (let i = 0; i < Math.min(systemCount, MAX_PARAMETER_SYSTEMS); i++) {
    const table = systems[i].roles.map((r) => ROLE_PARAMETERS[r.kind]).find((t) => t !== undefined);
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
