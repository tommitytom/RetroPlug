// The `bliptoaster-assets` feature role: a per-system, NON-DESTRUCTIVE set of BlipToaster ROM edits — the
// BlipToaster twin of ./risaAssetsRole.ts. It carries NO DSP behaviour: the base `.nes` on disk is never
// touched; the edits are folded into the base ROM in memory at CONSTRUCT time (the onConstruct hook), so
// `effective ROM = base ROM + config`, rebuilt on every load. The config is the persisted source of truth (it
// round-trips through the project's role config), and it holds two kinds of edit:
//
//   `overrides`  a list of replaced ASSETS (a theme / DMC kit / CHR font, by slot). THEMES are palette indices,
//                stored INLINE as a readable object (no file, no base64), like risa. KITS and FONTS are binary,
//                so they LINK their bank file on disk by path — a pre-built 8 KB `.rkit` DMC bank / an 8 KB
//                `.chr` CHR bank, read at construct (kit compilation is offline, like risa). A kit override
//                with `erase: true` empties the slot instead.
//   `settings`   the cart's BAKED RIG DEFAULTS (base MIDI channel, default kit, Mode 1 at boot, velocity curve,
//                default theme, default font) — the 16-byte block in the ROM's code bank. Only the fields the
//                project actually sets are written, so an untouched field keeps whatever the `.nes` baked.
//                See bliptoaster/rom/settings.ts for the format.
//
// Both ride the one patcher (applyConfigToRom), which is also what the menu bakes with — so "what the cart
// runs" and "what Patch ROM in Place writes" cannot drift apart.
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
import {
  BlipToasterRom,
  SETTINGS_KIT_COUNT,
  SETTINGS_THEME_COUNT,
  SETTINGS_FONT_COUNT,
  type BlipToasterSettingsPatch,
} from "./bliptoaster/rom";

export const BLIPTOASTER_ASSETS_ROLE = "bliptoaster-assets";

/** One asset override for `slot`. KITS and FONTS are binary, so they LINK their file on disk by `path` (a
 *  pre-built 8 KB `.rkit` DMC bank / an 8 KB `.chr` CHR bank, read at construct). A kit override with
 *  `erase: true` empties the slot instead of linking a bank. `name` is a display label. */
export interface BlipToasterAssetOverride {
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

// Every settings field is OPTIONAL, and that is the semantic: an absent field means "leave the byte the .nes
// baked", so a project pins only what the user actually changed. Ranges match the ROM's own (settings.ts).
const settingsSchema = z.object({
  baseChannel: z.number().int().min(0).max(15).optional(),
  kit: z.number().int().min(0).max(SETTINGS_KIT_COUNT - 1).optional(),
  ppu: z.boolean().optional(),
  velCurve: z.boolean().optional(),
  theme: z.number().int().min(0).max(SETTINGS_THEME_COUNT - 1).optional(),
  font: z.number().int().min(0).max(SETTINGS_FONT_COUNT - 1).optional(),
});

// The role config: the asset override list plus the baked-settings patch, both empty by default — a BlipToaster
// cart with no edits at all. Additive with defaults, so a project written before `settings` existed still parses
// and needs no migration step (spec/05).
const blipToasterAssetsSchema = z.object({
  overrides: z.array(overrideSchema).default([]),
  settings: settingsSchema.default({}),
});

/** Read the override list off a system's `bliptoaster-assets` role config (empty when absent/invalid). */
export function readOverrides(config: Record<string, unknown> | undefined): BlipToasterAssetOverride[] {
  const raw = config?.overrides;
  return Array.isArray(raw) ? (raw as BlipToasterAssetOverride[]) : [];
}

/** Read the baked-settings patch off a system's `bliptoaster-assets` role config ({} when absent/invalid). */
export function readSettings(config: Record<string, unknown> | undefined): BlipToasterSettingsPatch {
  const raw = config?.settings;
  return raw && typeof raw === "object" && !Array.isArray(raw) ? (raw as BlipToasterSettingsPatch) : {};
}

// Apply one override onto an open BlipToasterRom. Isolated + throwing so the caller can try/catch per entry
// (a moved file / bad asset just skips).
function applyOne(rom: BlipToasterRom, ov: BlipToasterAssetOverride, caps: ConstructCaps): void {
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
 *  bad entry just skips). Returns the base unchanged if it isn't a BlipToaster image. */
export function applyOverridesToRom(
  baseBytes: Uint8Array,
  overrides: BlipToasterAssetOverride[],
  caps: ConstructCaps,
  onSkip?: (ov: BlipToasterAssetOverride, message: string) => void,
): Uint8Array {
  return applyConfigToRom(baseBytes, { overrides }, caps, onSkip);
}

/** Fold a whole `bliptoaster-assets` role config (asset overrides + the baked-settings patch) onto base ROM
 *  bytes, returning the patched image — the ONE patcher both construct and the menu's bake run, so the image
 *  the cart runs and the image "Patch ROM in Place" writes are the same by construction. A bad asset entry just
 *  skips (reported through `onSkip`); returns the base unchanged if it isn't a BlipToaster image. */
export function applyConfigToRom(
  baseBytes: Uint8Array,
  config: Record<string, unknown>,
  caps: ConstructCaps,
  onSkip?: (ov: BlipToasterAssetOverride, message: string) => void,
): Uint8Array {
  const rom = BlipToasterRom.fromBytes(baseBytes);
  if (!rom.isBlipToaster) return baseBytes;
  for (const ov of readOverrides(config)) {
    try {
      applyOne(rom, ov, caps);
    } catch (e) {
      const msg = (e as Error).message;
      if (onSkip) onSkip(ov, msg);
      else console.log(`[bliptoaster-assets] skipped ${ov.type} slot ${ov.slot}: ${msg}`);
    }
  }
  // Settings last, and unconditionally: setSettings writes only the named fields and no-ops on a ROM with no
  // readable block, so there is nothing to guard and nothing an unreadable block can half-write.
  rom.setSettings(readSettings(config));
  return rom.bytes();
}

/** True when a role config asks for anything at all — no asset overrides and no pinned settings field means
 *  construct has no reason to read the ROM off disk, let alone patch it. */
function configIsEmpty(config: Record<string, unknown>): boolean {
  return readOverrides(config).length === 0 && Object.keys(readSettings(config)).length === 0;
}

// Load-time hook: fold the config into the base ROM and hand native the patched bytes. Additive — a
// no-op when the config is empty or when romBytes is already set.
function applyAssetOverrides(spec: ConstructSpec, caps: ConstructCaps, config: Record<string, unknown>): ConstructSpec {
  if (configIsEmpty(config) || spec.romBytes || spec.embeddedRom || !spec.romPath) return spec;
  const base = caps.readFile(spec.romPath);
  if (!base) return spec;
  const patched = applyConfigToRom(base, config, caps);
  return patched !== base ? { ...spec, romBytes: patched } : spec;
}

/** Register the `bliptoaster-assets` feature role (no DSP behaviour; a construct-time asset patcher). */
export function registerBlipToasterAssetsRole(registry: RoleRegistry): void {
  registry.registerRole({
    kind: BLIPTOASTER_ASSETS_ROLE,
    category: "feature",
    scope: "system",
    schema: blipToasterAssetsSchema,
    onConstruct: applyAssetOverrides,
  });
}
