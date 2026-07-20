// The `evermidi-assets` feature role: a per-system, NON-DESTRUCTIVE list of EverMIDI ROM asset overrides
// (a replaced theme / DMC kit / CHR font) — the EverMIDI twin of ./risaAssetsRole.ts. It carries NO DSP
// behaviour: the base `.nes` on disk is never touched; the overrides are folded into the base ROM in memory
// at CONSTRUCT time (the onConstruct hook), so `effective ROM = base ROM + overrides`, rebuilt on every load.
// The override manifest is the persisted source of truth (it round-trips through the project's role config).
// THEMES are palette indices, stored INLINE as a readable object (no file, no base64), like risa. KITS and
// FONTS are binary, so they LINK their bank file on disk by path — a pre-built 8 KB `.rkit` DMC bank / an
// 8 KB `.chr` CHR bank, read at construct (kit compilation is offline, like risa). A kit override with
// `erase: true` empties the slot instead. Applying reuses the pure-TS patcher (src/evermidi/rom).
import type { RoleRegistry, ConstructCaps } from "./systemRoles";
import type { ConstructSpec } from "./backend";
import { z } from "./configSchema";
import {
  KIT_BANK_SIZE,
  CHR_BANK_SIZE,
  isBankPopulated,
  encodeThemeRecord,
  encodeThemeName,
  normalizeTheme,
  type RisaTheme,
} from "./risa/rom";
import { EverMidiRom } from "./evermidi/rom";

export const EVERMIDI_ASSETS_ROLE = "evermidi-assets";

/** One asset override for `slot`. KITS and FONTS are binary, so they LINK their file on disk by `path` (a
 *  pre-built 8 KB `.rkit` DMC bank / an 8 KB `.chr` CHR bank, read at construct). A kit override with
 *  `erase: true` empties the slot instead of linking a bank. `name` is a display label. */
export interface EverMidiAssetOverride {
  type: "theme" | "font" | "kit";
  slot: number;
  name?: string;
  theme?: RisaTheme; // theme — stored inline (7 palette-index roles, no file)
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

// The role config: just the override list (empty by default — an EverMIDI cart with no replacements).
const everMidiAssetsSchema = z.object({
  overrides: z.array(overrideSchema).default([]),
});

/** Read the override list off a system's `evermidi-assets` role config (empty when absent/invalid). */
export function readOverrides(config: Record<string, unknown> | undefined): EverMidiAssetOverride[] {
  const raw = config?.overrides;
  return Array.isArray(raw) ? (raw as EverMidiAssetOverride[]) : [];
}

// Apply one override onto an open EverMidiRom. Isolated + throwing so the caller can try/catch per entry
// (a moved file / bad asset just skips).
function applyOne(rom: EverMidiRom, ov: EverMidiAssetOverride, caps: ConstructCaps): void {
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
    rom.setKit(ov.slot, bank);
    return;
  }
  // font
  if (!ov.path) throw new Error(`font override slot ${ov.slot}: no path`);
  const bytes = caps.readFile(ov.path);
  if (!bytes) throw new Error(`font override slot ${ov.slot}: cannot read ${ov.path}`);
  if (bytes.length !== CHR_BANK_SIZE) throw new Error(`font override slot ${ov.slot}: .chr must be exactly 8 KB`);
  rom.setChrFontSlot(ov.slot, bytes);
}

/** Fold a list of overrides onto base ROM bytes, returning the patched image (per-override try/catch so a
 *  bad entry just skips). Returns the base unchanged if it isn't an EverMIDI image. */
export function applyOverridesToRom(
  baseBytes: Uint8Array,
  overrides: EverMidiAssetOverride[],
  caps: ConstructCaps,
): Uint8Array {
  const rom = EverMidiRom.fromBytes(baseBytes);
  if (!rom.isEverMidi) return baseBytes;
  for (const ov of overrides) {
    try {
      applyOne(rom, ov, caps);
    } catch (e) {
      console.log(`[evermidi-assets] skipped ${ov.type} slot ${ov.slot}: ${(e as Error).message}`);
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

/** Register the `evermidi-assets` feature role (no DSP behaviour; a construct-time asset patcher). */
export function registerEverMidiAssetsRole(registry: RoleRegistry): void {
  registry.registerRole({
    kind: EVERMIDI_ASSETS_ROLE,
    category: "feature",
    scope: "system",
    schema: everMidiAssetsSchema,
    onConstruct: applyAssetOverrides,
  });
}
