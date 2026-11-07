// The `risa-assets` feature role: a per-system, NON-DESTRUCTIVE list of risa ROM asset overrides
// (replaced themes / fonts) — the risa twin of ./lsdjAssetsRole.ts. It carries NO DSP behaviour: the base
// `.nes` on disk is never touched; the overrides are folded into the base ROM in memory at CONSTRUCT time
// (the onConstruct hook), so `effective ROM = base ROM + overrides`, rebuilt on every load. The override
// manifest is the persisted source of truth (it round-trips through the project's role config). THEMES are
// just palette indices, so they're stored INLINE as readable JSON (no file, no base64); FONTS are binary,
// so they LINK to the `.chr` bank file on disk by path (read at construct). KITS are likewise binary, so
// they LINK a pre-built 8 KB DMC bank (`.rkit`) by path - risa's Export writes one out of a ROM and the CLI's
// build-kit compiles one, and a kit override just splices that ready-made bank with rom.setKit (which
// dual-writes the resident metadata mirror). src/kit can compile a .rkit in the plugin too, though no UI row does.
// Applying reuses the pure-TS patcher (src/risa/rom).
import type { RoleRegistry, RoleType, ConstructCaps } from "@retroplug/core/systemRoles";
import type { ConstructSpec } from "./backend";
import { z } from "zod";
import { RisaRom } from "./risa/rom";
import { applyRitOverride, ritOverrideSchema, CHR_BANK_SIZE, type RitAssetOverride } from "@retroplug/tracker/ritAssets";

/** @deprecated Use CHR_BANK_SIZE from @retroplug/tracker/ritAssets - kept because it is a public export. */
export const RISA_CHR_BANK_SIZE = CHR_BANK_SIZE;

/** One asset override for `slot`. THEMES are palette indices, stored INLINE as a readable `theme` object
 *  (name + 7 "0xNN" role indices — no file, no base64). FONTS and KITS are binary, so they LINK their file
 *  on disk by `path` (a `.chr` bank / a pre-built 8 KB `.rkit` DMC bank, read at construct). A kit override
 *  with `erase: true` empties the slot instead (a "delete this kit" override). `name` is a display label. */
export type RisaAssetOverride = RitAssetOverride;

const overrideSchema = ritOverrideSchema;

// The role config: just the override list (empty by default — a risa cart with no replacements).
const risaAssetsSchema = z.object({
  overrides: z.array(overrideSchema).default([]),
});

/** Read the override list off a system's `risa-assets` role config (empty when absent/invalid). */
export function readOverrides(config: Record<string, unknown> | undefined): RisaAssetOverride[] {
  const raw = config?.overrides;
  return Array.isArray(raw) ? (raw as RisaAssetOverride[]) : [];
}

/** Fold a list of overrides onto base ROM bytes, returning the patched image (per-override try/catch so a
 *  bad entry just skips). Returns the base unchanged if it isn't a risa image. `onSkip` sees every entry that
 *  couldn't be applied. A load can shrug those off, but a caller BAKING the result into the ROM on disk (the
 *  menu's Patch ROM in Place) has to know it would be dropping a link. */
export function applyOverridesToRom(
  baseBytes: Uint8Array,
  overrides: RisaAssetOverride[],
  caps: ConstructCaps,
  onSkip?: (ov: RisaAssetOverride, message: string) => void,
): Uint8Array {
  const rom = RisaRom.fromBytes(baseBytes);
  if (!rom.isRisa) return baseBytes;
  for (const ov of overrides) {
    try {
      applyRitOverride(rom, ov, caps);
    } catch (e) {
      const message = (e as Error).message;
      // Logged only when nobody is listening. A caller that supplies onSkip owns the reporting, and a
      // library that writes to the console regardless leaves it no way to stay quiet. BlipToaster's copy
      // of this loop already worked this way; the other two did not, so a failed BlipToaster bake was
      // silent on the console while an identical risa one was not.
      if (onSkip) onSkip(ov, message);
      else console.log(`[risa-assets] skipped ${ov.type} slot ${ov.slot}: ${message}`);
    }
  }
  return rom.bytes();
}

// Load-time hook: fold the overrides into the base ROM and hand native the patched bytes. Additive — a
// no-op when there are no overrides or when romBytes is already set.
function applyAssetOverrides(spec: ConstructSpec, caps: ConstructCaps, config: Record<string, unknown>): ConstructSpec {
  const overrides = readOverrides(config);
  if (overrides.length === 0 || spec.romBytes || spec.embeddedRom || !spec.romPath) return spec;
  const base = caps.readFile(spec.romPath);
  if (!base) return spec;
  const patched = applyOverridesToRom(base, overrides, caps);
  return patched !== base ? { ...spec, romBytes: patched } : spec;
}

/** The `risa-assets` feature role (no DSP behaviour; a construct-time asset patcher): the app's contribution. */
export const RISA_ASSETS_ROLE: RoleType = {
  kind: "risa-assets",
  category: "feature",
  schema: risaAssetsSchema,
  onConstruct: applyAssetOverrides,
};

/** Register the role into `registry`, for a registry a test or a host builds by hand. */
export function registerRisaAssetsRole(registry: RoleRegistry): void {
  registry.registerRole(RISA_ASSETS_ROLE);
}
