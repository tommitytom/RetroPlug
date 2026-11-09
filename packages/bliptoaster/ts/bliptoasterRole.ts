// The "bliptoaster" feature role. A control-plane-only marker role (no DSP behaviour, no config) attached to
// a BlipToaster ROM by the bliptoaster ROM provider (romProviders.ts). Its presence gates the BlipToaster asset
// menu — it is the tracker integration's markerRole — exactly as "risa" gates risa's menu. Registered into
// the control-plane registry only (buildAppRegistry), like registerRisaRole.
import { z } from "zod";
import type { RoleRegistry, RoleType } from "@retroplug/core/systemRoles";
import { BLIPTOASTER_CHIP_SCAN_LEN, blipToasterChip, needsChipLabelScan } from "./romDetect";
import { blipToasterParameters } from "./parameters";

/** The `bliptoaster` marker role: the app's contribution. */
export const BLIPTOASTER_ROLE: RoleType = {
  kind: "bliptoaster",
  category: "feature",
  // `chip` is which expansion build this ROM is, DERIVED from the ROM file at every build (deriveConfig below) rather
  // than written by the provider: it picks the DAW parameter map's CC set, which differs per build. A plain string
  // rather than an enum so a project written by a newer bundle (a chip this build has never heard of) still parses; the
  // lookup falls back to the 2A03 core. Additive with a default, so no migration step - a project saved before this
  // field existed reads as "2a03" until its ROM is re-detected.
  schema: z.object({ chip: z.string().default("2a03") }),
  parameters: blipToasterParameters,
  // The chip is a fact about the FILE, not a user choice, so the store re-derives it at every build: a project written
  // before the field existed, or one whose ROM was swapped for another build at the same path, then reads the build it
  // boots. A ROM predating the SIG chip tag names its build only in the on-screen label inside the PRG region, so
  // `headerBytes` asks for that deeper read exactly then - and never for a tagged ROM or a cart that is not ours.
  deriveConfig: {
    headerBytes: (short) => (needsChipLabelScan(short) ? Math.max(BLIPTOASTER_CHIP_SCAN_LEN, short.length) : short.length),
    derive: (header, config) => ({ ...config, chip: blipToasterChip(header) }),
  },
};

/** Register the role into `registry`, for a registry a test or a host builds by hand. */
export function registerBlipToasterRole(registry: RoleRegistry): void {
  registry.registerRole(BLIPTOASTER_ROLE);
}
