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
// WHICH controls a ROM exposes is the cart's own knowledge (docs/modular-graph-phase-f4a-design.md section 4.5): its
// role's `parameters` hook answers for the config that role carries, and this module asks the registry for it. So no CC
// table lives here, and no cart's build names do either.
import { MidiRouting } from "@retroplug/core/midiRouting";
import type { RoleRegistry } from "@retroplug/core/systemRoles";
import type { RomParameter } from "@retroplug/core/romParameters";

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
 *  belongs to system `i`; every slot not returned stays unclaimed (hidden, generically named). A system's table is its
 *  first role whose type serves `parameters`, asked with that role's own config (a BlipToaster's `chip`, say). */
export function projectParameterMap(
  systems: ParameterSystem[],
  mode: MidiRouting,
  registry: RoleRegistry,
): ParameterSlot[] {
  const out: ParameterSlot[] = [];
  const systemCount = systems.length;

  for (let i = 0; i < Math.min(systemCount, MAX_PARAMETER_SYSTEMS); i++) {
    let table: readonly RomParameter[] | undefined;
    for (const r of systems[i].roles) {
      table = registry.roleType(r.kind)?.parameters?.((r.config ?? {}) as Record<string, unknown>);
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
