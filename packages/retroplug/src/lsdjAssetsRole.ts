// The `lsdj-assets` feature role: a per-system, NON-DESTRUCTIVE list of LSDj ROM asset overrides
// (replaced kits / palettes / fonts). It carries NO DSP behaviour — the base `.gb` on disk is never
// touched; instead the overrides are applied to the base ROM in memory at CONSTRUCT time (the onConstruct
// hook), so `effective ROM = base ROM + overrides`, rebuilt on every load. The override manifest is the
// persisted source of truth (it round-trips through the project's role config). Each override LINKS to the
// asset FILE on disk by path (a `.kit` bank / `.lsdpal` / `.png`) — NEVER embedded bytes — mirroring how the
// project references its ROM/sav by path; the file is read at construct. Applying reuses the pure-TS patcher
// (src/lsdj/rom).
import type { RoleRegistry, ConstructCaps } from "./systemRoles";
import type { ConstructSpec } from "./backend";
import type { RomColorSet } from "./lsdj/rom";
import { z } from "./configSchema";
import { LsdjRom, encodeLsdpal } from "./lsdj/rom";
import { lsdprjKitBank } from "./lsdjSav";

export const LSDJ_ASSETS_ROLE = "lsdj-assets";

/** One asset override for `slot`. KITS and FONTS are binary, so they LINK to the file on disk by `path`
 *  (read at construct — never embedded bytes). PALETTES are just colours, so they're stored INLINE as
 *  structured `colorSets` (readable JSON, no file needed). `name` is a display label captured at import.
 *  A kit override may source its bank from a `.lsdprj` (path = the .lsdprj, `lsdprjKit` = the bank ordinal). */
export interface LsdjAssetOverride {
  type: "kit" | "palette" | "font";
  slot: number;
  name?: string;
  path?: string; // kit / font — the .kit / .png / .lsdprj file on disk
  colorSets?: RomColorSet[]; // palette — 5 colour-sets × 4 RGB colours, stored inline
  erase?: boolean; // kit — a "delete" override: empty the slot rather than replace it (no path/colours)
  lsdprjKit?: number; // kit — the bank ordinal to extract from a `.lsdprj` at `path` (imported song's kits)
}

const colorSchema = z.object({ r: z.number().int(), g: z.number().int(), b: z.number().int() });
const overrideSchema = z.object({
  type: z.enum(["kit", "palette", "font"]),
  slot: z.number().int().nonnegative(),
  name: z.string().optional(),
  path: z.string().optional(),
  colorSets: z.array(z.object({ colors: z.array(colorSchema) })).optional(),
  erase: z.boolean().optional(),
  lsdprjKit: z.number().int().nonnegative().optional(),
});

// The role config: just the override list (empty by default — an LSDj cart with no replacements).
const lsdjAssetsSchema = z.object({
  overrides: z.array(overrideSchema).default([]),
});

/** Read the override list off a system's `lsdj-assets` role config (empty when absent/invalid). */
export function readOverrides(config: Record<string, unknown> | undefined): LsdjAssetOverride[] {
  const raw = config?.overrides;
  return Array.isArray(raw) ? (raw as LsdjAssetOverride[]) : [];
}

// Apply one override onto an open LsdjRom. Palettes apply from their inline colour-sets; kits/fonts read
// their linked file. Isolated + throwing so the caller can try/catch per entry (a moved/deleted file, a
// bad asset, or a malformed override just skips that entry).
function applyOne(rom: LsdjRom, ov: LsdjAssetOverride, caps: ConstructCaps): void {
  if (ov.type === "kit" && ov.erase) {
    rom.eraseKit(ov.slot);
    return;
  }
  if (ov.type === "palette") {
    if (!ov.colorSets) throw new Error(`palette override slot ${ov.slot}: no colorSets`);
    rom.importPaletteFile(ov.slot, encodeLsdpal(ov.name ?? "", ov.colorSets));
    return;
  }
  if (!ov.path) throw new Error(`${ov.type} override slot ${ov.slot}: no path`);
  const bytes = caps.readFile(ov.path);
  if (!bytes) throw new Error(`override ${ov.type} slot ${ov.slot}: cannot read ${ov.path}`);
  if (ov.type === "kit") {
    // A kit override sourced from a .lsdprj extracts its bank ordinal; otherwise `bytes` is a whole .kit.
    const bank = ov.lsdprjKit != null ? lsdprjKitBank(bytes, ov.lsdprjKit) : bytes;
    if (!bank) throw new Error(`kit override slot ${ov.slot}: no bank ${ov.lsdprjKit} in ${ov.path}`);
    rom.importKitFile(ov.slot, bank);
  } else {
    const img = caps.pngDecode(bytes);
    if (!img) throw new Error(`font override slot ${ov.slot}: not a decodable PNG`);
    rom.importFontImage(ov.slot, img);
  }
}

/** Fold a list of overrides onto base ROM bytes, returning the patched image (per-override try/catch so a
 *  bad entry just skips). Shared by the construct hook and callers needing the effective ROM (e.g. the menu
 *  deduping a `.lsdprj`'s kits). Returns the base unchanged if it isn't an LSDj image. */
export function applyOverridesToRom(baseBytes: Uint8Array, overrides: LsdjAssetOverride[], caps: ConstructCaps): Uint8Array {
  const rom = LsdjRom.fromBytes(baseBytes);
  if (!rom.isLsdj) return baseBytes;
  for (const ov of overrides) {
    try {
      applyOne(rom, ov, caps);
    } catch (e) {
      console.log(`[lsdj-assets] skipped ${ov.type} slot ${ov.slot}: ${(e as Error).message}`);
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

/** Register the `lsdj-assets` feature role (no DSP behaviour; a construct-time asset patcher). */
export function registerLsdjAssetsRole(registry: RoleRegistry): void {
  registry.registerRole({
    kind: LSDJ_ASSETS_ROLE,
    category: "feature",
    scope: "system",
    schema: lsdjAssetsSchema,
    onConstruct: applyAssetOverrides,
  });
}
