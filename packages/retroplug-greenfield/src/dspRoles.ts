// The built-in DSP-thread role behaviors: doc-06 translators/sources authored as plain TS over the
// per-system context (dspKernel.ts). `mgb` + `lsdj-sync` are the two we're migrating off their
// legacy C++ roles; `midi-routing` is the project-scope behavior that fans host MIDI to systems,
// reusing the existing routeBlock decision. Registered into a RoleRegistry like registerCoreRoles.

import type { RoleRegistry, ConstructCaps } from "./systemRoles";
import type { ProjectBehavior, SystemBehavior } from "./dspKernel";
import type { ConstructSpec } from "./backend";
import { z, clampedInt } from "./configSchema";
import { routeBlock, MidiRouting } from "./midiRouting";

// mGB: forward every host-MIDI byte verbatim into the system's serial input (== MgbPassthroughRole).
const mgb: SystemBehavior = (c) => c.midi.forEach((e) => e.data.forEach((b) => c.pushSerialIn(e.frame, b)));

// lsdj-sync: mode 1 (MidiSync) emits a 24-PPQN 0xF8 clock via eachTick; mode 0 (Off) emits nothing.
// (== LsdjSyncRole MidiSync — a bare 0xF8 stream, no 0xFA; the START-arm that begins LSDj is a
// UI/user action, not part of the clock.)
const lsdjSync: SystemBehavior = (c) => {
  if ((c.config.mode as number) === 1) c.eachTick(24, (_tick, off) => c.pushSerialIn(off, 0xf8));
};

// lsdj-sync load-time hook: a fresh LSDj cart with no SRAM runs a 12–15 s cartridge self-test on boot.
// When nothing else will seed the battery (no savestate, no sram blob, no on-disk .sav for native to
// load), hand it a valid empty sav — savFromJson stamps the jk/rb validity markers LSDj checks — so it
// boots straight to the song screen. Additive: return the spec untouched when real save data is present.
const lsdjSeedSav = (spec: ConstructSpec, caps: ConstructCaps): ConstructSpec => {
  const willLoadData = !!spec.stateBytes || !!spec.sramBytes || (spec.savPath != null && caps.fileExists(spec.savPath));
  if (willLoadData) return spec;
  return { ...spec, sramBytes: caps.savFromJson("{}").slice().buffer };
};

// midi-routing (project scope): fan the block's GLOBAL midiIn into the per-system inboxes the kernel
// then hands to each system's pipeline. Reuses the pure routeBlock decision (midiRouting.ts).
const midiRouting: ProjectBehavior = (block, routed, config) => {
  const inboxes = routeBlock(block.midiIn, (config.mode as MidiRouting) ?? MidiRouting.SendToAll, block.systems.length);
  block.systems.forEach((s, i) => routed.set(s.id, inboxes[i] ?? []));
};

/** Register the built-in DSP-thread roles into `registry`. */
export function registerDspRoles(registry: RoleRegistry): void {
  registry.registerRole({ kind: "mgb", category: "feature", scope: "system", schema: z.object({}), dsp: mgb });
  registry.registerRole({
    kind: "lsdj-sync",
    category: "feature",
    scope: "system",
    schema: z.object({ mode: clampedInt(0, 7, 1) }), // LsdjSyncMode: Off=0, MidiSync=1, … ArduinoboyMaster=7
    dsp: lsdjSync,
    onConstruct: lsdjSeedSav,
  });
  registry.registerRole({
    kind: "midi-routing",
    category: "feature",
    scope: "project",
    schema: z.object({ mode: clampedInt(0, 3, 0) }), // MidiRouting 0..3
    dsp: midiRouting,
  });
}
