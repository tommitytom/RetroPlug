// mGB's contribution to the app: its role and the provider that attaches it. The role has no config (every routed MIDI
// byte goes to the serial port), and its work is rp.mgb.midi, a protocol node (./mgbMidiNode) the audio thread runs.
import { definePackage } from "@retroplug/core/definePackage";
import type { RoleType } from "@retroplug/core/systemRoles";
import { z } from "zod";
import { MGB_ROM_PROVIDER } from "./romProvider";

// mgb: every routed MIDI byte into the Game Boy's serial port (rp.mgb.midi). No config.
export const MGB_ROLE: RoleType = { kind: "mgb", category: "feature", schema: z.object({}) };

export default definePackage({ roles: [MGB_ROLE], romProviders: [MGB_ROM_PROVIDER] });
