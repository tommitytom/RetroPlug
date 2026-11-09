// LSDj's contribution to the app: its two roles (the sync role whose work is rp.lsdj.sync and rp.lsdj.serial-out, and
// the asset-override role), the ROM provider that attaches them, its Songs/Assets tracker integration and its kit bank
// format. The lists' order within a package is the order they register in where one list carries several.
import { definePackage } from "@retroplug/core/definePackage";
import type { RoleType } from "@retroplug/core/systemRoles";
import type { Host } from "@retroplug/core/host";
import type { ConstructSpec } from "@retroplug/core/constructSpec";
import { z } from "zod";
import { clampedInt, boolField, enumField } from "@retroplug/core/configFields";
import { LSDJ_MODE_VALUES, LsdjSyncMode } from "./enums";
import { LSDJ_ASSETS_ROLE } from "./lsdjAssetsRole";
import { LSDJ_ROM_PROVIDER } from "./romProvider";
import { savFromJson } from "./index";
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
// The bytes are this cart's own codec's: no other cart's battery is anything like them, so the host carries no sav
// encoder.
const lsdjSeedSav = (spec: ConstructSpec, host: Host<"files" | "png">): ConstructSpec => {
  const willLoadData = !!spec.stateBytes || !!spec.sramBytes || (spec.savPath != null && host.files.fileExists(spec.savPath));
  if (willLoadData) return spec;
  return { ...spec, sramBytes: savFromJson("{}") };
};

/** LSDj's clock-to-sound latency in a host-clocked sync mode, in milliseconds: what a host's PDC takes out. */
const LSDJ_HOST_SYNC_LATENCY_MS = 33;

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
  // The host-sync latency a host should compensate for: in the two modes that lock LSDj's playback to the DAW clock it
  // runs a fixed distance behind it (an emulator clock to sound latency, not a drift), so a host applying this as PDC
  // puts LSDj on the grid. Output modes (MI.OUT, master sync) and the rest do not host-clock playback, so they add none.
  hostLatencyMs: (config) =>
    config.mode === LsdjSyncMode.MidiSync || config.mode === LsdjSyncMode.MidiSyncArduinoboy
      ? LSDJ_HOST_SYNC_LATENCY_MS
      : 0,
};

export default definePackage({
  roles: [LSDJ_SYNC_ROLE, LSDJ_ASSETS_ROLE],
  romProviders: [LSDJ_ROM_PROVIDER],
  trackerIntegrations: [lsdjIntegration],
  kitFormats: [LSDJ_KIT_FORMAT],
});
