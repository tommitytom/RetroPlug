// LSDj's contribution to the app: its two roles (the sync role whose work is rp.lsdj.sync and rp.lsdj.serial-out, and
// the asset-override role), the ROM provider that attaches them, its Songs/Assets tracker integration and its kit bank
// format. The lists' order within a package is the order they register in where one list carries several.
import { definePackage } from "@retroplug/core/definePackage";
import type { RoleType, ConstructCaps } from "@retroplug/core/systemRoles";
import type { ConstructSpec } from "@retroplug/core/constructSpec";
import { z } from "zod";
import { clampedInt, boolField, enumField } from "@retroplug/core/configFields";
import { LSDJ_MODE_VALUES } from "./enums";
import { LSDJ_ASSETS_ROLE } from "./lsdjAssetsRole";
import { LSDJ_ROM_PROVIDER } from "./romProvider";
import { lsdjIntegration } from "./lsdjIntegration";
import { LSDJ_KIT_FORMAT } from "./kitFormat";
// The keys the owner packages add to the contract (their modules augment PackageContributions): type-only, so nothing
// loads at run time.
import type {} from "@retroplug/tracker/packageContributions";
import type {} from "@retroplug/dsp/packageContributions";

// lsdj-sync load-time hook: a fresh LSDj cart with no SRAM runs a 12-15 s cartridge self-test on boot.
// When nothing else will seed the battery (no savestate, no sram blob, no on-disk .sav for native to
// load), hand it a valid empty sav (savFromJson stamps the jk/rb validity markers LSDj checks) so it
// boots straight to the song screen. Additive: return the spec untouched when real save data is present.
const lsdjSeedSav = (spec: ConstructSpec, caps: ConstructCaps): ConstructSpec => {
  const willLoadData = !!spec.stateBytes || !!spec.sramBytes || (spec.savPath != null && caps.fileExists(spec.savPath));
  if (willLoadData) return spec;
  return { ...spec, sramBytes: caps.savFromJson("{}") };
};

export const LSDJ_SYNC_ROLE: RoleType = {
  kind: "lsdj-sync",
  category: "feature",
  // mode: LsdjSyncMode ("off", "midiSync", … "midiOut", "masterSync"). tempoDivisor subdivides the
  // 24-PPQN clock (24/divisor) for MidiSync + MidiSyncArduinoboy; the menu offers 1/2/4/8. autoStart
  // taps START on the host transport rise to auto-arm a SYNC=MIDI (MidiSync) cart, needed for a
  // headless DAW render, off by default so normal MidiSync keeps its manual-arm behaviour. rp.lsdj.sync
  // runs each mode.
  schema: z.object({
    mode: enumField(LSDJ_MODE_VALUES, "midiSync"),
    tempoDivisor: clampedInt(1, 8, 1),
    autoStart: boolField(false),
  }),
  onConstruct: lsdjSeedSav,
};

export default definePackage({
  roles: [LSDJ_SYNC_ROLE, LSDJ_ASSETS_ROLE],
  romProviders: [LSDJ_ROM_PROVIDER],
  trackerIntegrations: [lsdjIntegration],
  kitFormats: [LSDJ_KIT_FORMAT],
});
