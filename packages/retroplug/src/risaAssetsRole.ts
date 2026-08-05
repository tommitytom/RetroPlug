// The `risa-assets` feature role: a per-system, NON-DESTRUCTIVE list of risa ROM asset overrides
// (replaced themes / fonts) — the risa twin of ./lsdjAssetsRole.ts. It carries NO DSP behaviour: the base
// `.nes` on disk is never touched; the overrides are folded into the base ROM in memory at CONSTRUCT time
// (the onConstruct hook), so `effective ROM = base ROM + overrides`, rebuilt on every load. The override
// manifest is the persisted source of truth (it round-trips through the project's role config). THEMES are
// just palette indices, so they're stored INLINE as readable JSON (no file, no base64); FONTS are binary,
// so they LINK to the `.chr` bank file on disk by path (read at construct). KITS are likewise binary, so
// they LINK a pre-built 8 KB DMC bank (`.rkit`) by path — compilation is offline (the compileDmc RPC is on
// the harness facet, unreachable from the plugin), so a kit override just splices a ready-made bank with
// rom.setKit (which dual-writes the resident metadata mirror). Applying reuses the pure-TS patcher (src/risa/rom).
import type { RoleRegistry, ConstructCaps } from "./systemRoles";
import type { ConstructSpec } from "./backend";
import type { RisaTheme } from "./risa/rom";
import { z } from "./configSchema";
import { RisaRom, encodeThemeRecord, encodeThemeName, normalizeTheme, KIT_BANK_SIZE, isBankPopulated } from "./risa/rom";

export const RISA_ASSETS_ROLE = "risa-assets";
export const RISA_CHR_BANK_SIZE = 0x2000; // one font slot = one 8 KB CHR bank

/** One asset override for `slot`. THEMES are palette indices, stored INLINE as a readable `theme` object
 *  (name + 7 "0xNN" role indices — no file, no base64). FONTS and KITS are binary, so they LINK their file
 *  on disk by `path` (a `.chr` bank / a pre-built 8 KB `.rkit` DMC bank, read at construct). A kit override
 *  with `erase: true` empties the slot instead (a "delete this kit" override). `name` is a display label. */
export interface RisaAssetOverride {
  type: "theme" | "font" | "kit";
  slot: number;
  name?: string;
  theme?: RisaTheme; // theme — stored inline
  path?: string; // font / kit — the .chr / .rkit bank file on disk
  erase?: boolean; // kit — empty the slot instead of linking a bank
}

const themeSchema = z.object({
  name: z.string(),
  bg: z.string(),
  normal: z.string(),
  shaded: z.string(),
  alternate: z.string(),
  status: z.string(),
  cursor: z.string(),
  selection: z.string(),
});
const overrideSchema = z.object({
  type: z.enum(["theme", "font", "kit"]),
  slot: z.number().int().nonnegative(),
  name: z.string().optional(),
  theme: themeSchema.optional(),
  path: z.string().optional(),
  erase: z.boolean().optional(),
});

// The role config: just the override list (empty by default — a risa cart with no replacements).
const risaAssetsSchema = z.object({
  overrides: z.array(overrideSchema).default([]),
});

/** Read the override list off a system's `risa-assets` role config (empty when absent/invalid). */
export function readOverrides(config: Record<string, unknown> | undefined): RisaAssetOverride[] {
  const raw = config?.overrides;
  return Array.isArray(raw) ? (raw as RisaAssetOverride[]) : [];
}

// Apply one override onto an open RisaRom. Themes apply from their inline object; fonts read their linked
// `.chr`. Isolated + throwing so the caller can try/catch per entry (a moved file / bad asset just skips).
function applyOne(rom: RisaRom, ov: RisaAssetOverride, caps: ConstructCaps): void {
  if (ov.type === "theme") {
    if (!ov.theme) throw new Error(`theme override slot ${ov.slot}: no theme`);
    const theme = normalizeTheme(ov.theme);
    rom.setTheme(ov.slot, encodeThemeRecord(theme), encodeThemeName(theme));
    return;
  }
  if (ov.type === "kit") {
    if (ov.erase) {
      rom.clearKitBank(ov.slot); // "delete this kit" override
      return;
    }
    if (!ov.path) throw new Error(`kit override slot ${ov.slot}: no path`);
    const bank = caps.readFile(ov.path);
    if (!bank) throw new Error(`kit override slot ${ov.slot}: cannot read ${ov.path}`);
    if (bank.length !== KIT_BANK_SIZE) throw new Error(`kit override slot ${ov.slot}: .rkit must be exactly 8 KB`);
    if (!isBankPopulated(bank)) throw new Error(`kit override slot ${ov.slot}: not a populated kit bank`);
    rom.setKit(ov.slot, bank); // dual-writes the resident metadata mirror
    return;
  }
  // font
  if (!ov.path) throw new Error(`font override slot ${ov.slot}: no path`);
  const bytes = caps.readFile(ov.path);
  if (!bytes) throw new Error(`font override slot ${ov.slot}: cannot read ${ov.path}`);
  if (bytes.length !== RISA_CHR_BANK_SIZE) throw new Error(`font override slot ${ov.slot}: .chr must be exactly 8 KB`);
  rom.setChrFontSlot(ov.slot, bytes);
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
      applyOne(rom, ov, caps);
    } catch (e) {
      const message = (e as Error).message;
      console.log(`[risa-assets] skipped ${ov.type} slot ${ov.slot}: ${message}`);
      onSkip?.(ov, message);
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

/** Register the `risa-assets` feature role (no DSP behaviour; a construct-time asset patcher). */
export function registerRisaAssetsRole(registry: RoleRegistry): void {
  registry.registerRole({
    kind: RISA_ASSETS_ROLE,
    category: "feature",
    scope: "system",
    schema: risaAssetsSchema,
    onConstruct: applyAssetOverrides,
  });
}
