// mGB's contribution to the app: its role and the provider that attaches it. The role has no config (every routed MIDI
// byte goes to the serial port), and its work is rp.mgb.midi, a protocol node (./mgbMidiNode) the audio thread runs.
import { definePackage } from "@retroplug/core/definePackage";
import type { RoleType } from "@retroplug/core/systemRoles";
import { z } from "zod";
import { MGB_PARAMETERS } from "./parameters";
import { MGB_ROM_PROVIDER } from "./romProvider";

// mgb: every routed MIDI byte into the Game Boy's serial port (rp.mgb.midi). No config, but the DAW's automation
// lanes are the synth's own (its MIDI implementation map): the role serves them, so the shell holds no mGB table.
export const MGB_ROLE: RoleType = {
  kind: "mgb",
  category: "feature",
  schema: z.object({}),
  parameters: () => MGB_PARAMETERS,
};

export default definePackage({
  roles: [MGB_ROLE],
  romProviders: [MGB_ROM_PROVIDER],
  // The synth's bytes are baked into every product that lists this package (the manifest's embed), and the id, its label
  // and the console it runs on are declared here, so a load of the embedded ROM takes its platform from this rather than
  // the shell assuming one. The generator pairs the two halves.
  embeds: [{ id: "mgb", label: "mGB (GB MIDI Synth)", shortLabel: "mGB", platform: "gb" }],
});
