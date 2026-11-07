// n8's contribution to the app: the `nes-n8-midi` role, the ROM provider that attaches it, and the rp.n8.midi-to-fifo
// node that runs it. The role is the always-on host-MIDI passthrough every NES gets, since the FIFO the node writes to
// is always there and a ROM that ignores $40F0 is undisturbed. That is also why the match is the PLATFORM and not a
// ROM marker.
//
// The role's schema is empty: the passthrough has no settings, and what it does is the node's (./n8MidiToFifoNode).
import { definePackage } from "@retroplug/core/definePackage";
import type { RoleType, RomProvider } from "@retroplug/core/systemRoles";
import { z } from "zod";

// NES host-MIDI: routed MIDI to the core's MIDI input (to the always-attached N8 FIFO). A role on a Mesen NES, and
// feature-category with no config, so an existing project parses unchanged.
export const NES_N8_MIDI_ROLE: RoleType = { kind: "nes-n8-midi", category: "feature", schema: z.object({}) };

/** Any NES ROM -> the always-on host-MIDI passthrough to its core. */
export const NES_N8_MIDI_ROM_PROVIDER: RomProvider = (rom) =>
  rom.platform === "nes" ? [{ kind: "nes-n8-midi", config: {} }] : [];

export default definePackage({
  roles: [NES_N8_MIDI_ROLE],
  romProviders: [NES_N8_MIDI_ROM_PROVIDER],
});
