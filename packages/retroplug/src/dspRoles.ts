// The built-in roles whose work is the audio thread's: each is placed as a protocol script node
// (graph/protocolNodes.ts names its type, script/nodes/ holds its code), which runs it for its system every block.
// What a role is to the app stays here, on the control plane: its kind, its zod config schema (the shape the stores
// parse, persist and hand the node as its settings), and a load-time hook. Registered into a RoleRegistry like
// registerCoreRoles; test/system-settings/registry.test.ts holds these kinds to protocolNodes.ts's table.

import type { RoleRegistry, ConstructCaps } from "./systemRoles";
import type { ConstructSpec } from "./backend";
import { z, clampedInt, boolField, enumField } from "./configSchema";
import { LSDJ_MODE_VALUES } from "./settingsEnums";

// lsdj-sync load-time hook: a fresh LSDj cart with no SRAM runs a 12–15 s cartridge self-test on boot.
// When nothing else will seed the battery (no savestate, no sram blob, no on-disk .sav for native to
// load), hand it a valid empty sav (savFromJson stamps the jk/rb validity markers LSDj checks) so it
// boots straight to the song screen. Additive: return the spec untouched when real save data is present.
const lsdjSeedSav = (spec: ConstructSpec, caps: ConstructCaps): ConstructSpec => {
  const willLoadData = !!spec.stateBytes || !!spec.sramBytes || (spec.savPath != null && caps.fileExists(spec.savPath));
  if (willLoadData) return spec;
  return { ...spec, sramBytes: caps.savFromJson("{}") };
};

/** Register the built-in roles that have a protocol node into `registry`. */
export function registerDspRoles(registry: RoleRegistry): void {
  // mgb: every routed MIDI byte into the Game Boy's serial port (rp.mgb.midi). No config.
  registry.registerRole({ kind: "mgb", category: "feature", schema: z.object({}) });
  // NES host-MIDI: routed MIDI to the core's MIDI input (to the always-attached N8 FIFO), rp.n8.midi-to-fifo.
  // Attached to every NES ROM by the rom provider (romProviders.ts), mirroring the native always-on N8 role.
  registry.registerRole({ kind: "nes-n8-midi", category: "feature", schema: z.object({}) });
  // risa-sync (no config) drives risa's N8-FIFO host-sync receive path from the DAW transport (locate /
  // start / 24-PPQN clock / stop), rp.risa.sync. Attached only to sync-capable risa ROMs.
  registry.registerRole({ kind: "risa-sync", category: "feature", schema: z.object({}) });
  // sms-sync: clocks smsggdj by holding a 2-bit counter on controller port 2 (TR + TH) at 24 PPQN, or a
  // Game Gear's EXT port for `machine: "gg"` (rp.sms.sync). Attached only to smsggdj (the SMSGGDJ marker),
  // NOT to every SMS ROM: these are Player 2's button lines, so an unconditional attach would inject phantom
  // P2 presses into any game. `machine` is additive with a default, so no migration step: an existing project
  // stamped before it existed parses as "sms", which is what those projects were.
  registry.registerRole({
    kind: "sms-sync",
    category: "feature",
    schema: z.object({ machine: z.enum(["sms", "gg"]).default("sms") }),
  });
  registry.registerRole({
    kind: "lsdj-sync",
    category: "feature",
    // mode: LsdjSyncMode ("off", "midiSync", … "midiOut", "masterSync"). tempoDivisor subdivides the
    // 24-PPQN clock (24/divisor) for MidiSync + MidiSyncArduinoboy; the menu offers 1/2/4/8. autoStart
    // taps START on the host transport rise to auto-arm a SYNC=MIDI (MidiSync) cart, needed for a
    // headless DAW render, off by default so normal MidiSync keeps its manual-arm behaviour. rp.lsdj.sync
    // runs each mode.
    schema: z.object({ mode: enumField(LSDJ_MODE_VALUES, "midiSync"), tempoDivisor: clampedInt(1, 8, 1), autoStart: boolField(false) }),
    onConstruct: lsdjSeedSav,
  });
}
