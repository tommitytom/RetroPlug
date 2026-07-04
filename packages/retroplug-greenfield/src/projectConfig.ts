// The project config model + codec. TS owns the config as source of truth: it BUILDS
// the config from the live systems + settings, SERIALIZES it (rebasing asset paths
// relative to the .rplg's folder so it's portable) and PARSES it back (rebasing to
// absolute). Ports schemaVersions.ts + the thin ProjectConfig shape.
//
// "Thin" = the binary blobs (rom/sram/state/kit) are never in the config, and rich
// per-system fields (model/roles/kits/…) are omitted — they restore to native
// defaults via reflect-cpp DefaultIfMissing, so a fresh/uncustomized project
// round-trips faithfully. Only user-customized fields are lost until the
// system-settings/kits domains land.

import type { SystemEntry, SystemKind } from "./systemsList";
import { rebaseToRelative, rebaseToAbsolute } from "./projectPaths";
import { coreSettingsSchema, type CoreSettings } from "./systemSettings";
import type { RoleInstance } from "./systemRoles";
import { z, clampedInt, stringField } from "./configSchema";

/** Project-level settings (four scalars). `zoom` 0 = inherit the user default. */
export interface ProjectSettings {
  layout: number; // 0 Auto / 1 Row / 2 Column / 3 Grid
  midiRouting: number; // 0 SendToAll / 1 FourPerInstance / 2 OnePerInstance / 3 ChannelToInstance
  audioRouting: number; // 0 Stereo / 1 TwoPerInstance / 2 OnePerInstance
  zoom: number; // 0 inherit / 1..6
}

/** A system as serialized: thin, with default fields omitted. */
export interface SystemThin {
  kind: SystemKind;
  romPath?: string;
  savPath?: string; // an explicit override; absent = derive from suffix
  savSuffix?: number;
  embeddedRom?: string; // e.g. "mgb"
  settings?: Partial<CoreSettings>; // only non-default universal settings
  roles?: RoleInstance[]; // the generic roles (backend + feature); absent = re-derive
}

export interface ProjectConfig {
  schemaVersion: string;
  settings: ProjectSettings;
  systems: SystemThin[];
}

// --- zod validation schemas -----------------------------------------------
// Every config object is a z.looseObject so UNKNOWN fields are PRESERVED — a native
// config's richer per-system fields (model / native-shaped roles / …) survive a
// greenfield load→save round-trip instead of being stripped. Fields clamp/default via
// the configSchema helpers, so a malformed/partial user config is coerced, not rejected.

/** Project-level settings: defaults filled, values clamped to their enum ranges. */
export const projectSettingsSchema = z.looseObject({
  layout: clampedInt(0, 3, 0),
  midiRouting: clampedInt(0, 3, 0),
  audioRouting: clampedInt(0, 2, 0),
  zoom: clampedInt(0, 6, 0),
});

export const DEFAULT_SETTINGS: ProjectSettings = projectSettingsSchema.parse({}) as ProjectSettings;

// A role instance: kind + an opaque config record (its per-kind RoleType schema
// validates the config elsewhere; here it's passed through).
const roleInstanceSchema = z.looseObject({
  kind: z.string(),
  config: z.record(z.string(), z.unknown()).catch(() => ({})),
});

// A serialized system: known fields typed/optional, everything else preserved (loose).
const systemThinSchema = z.looseObject({
  kind: z.string().optional(),
  romPath: z.string().optional(),
  savPath: z.string().optional(),
  savSuffix: z.number().optional(),
  embeddedRom: z.string().optional(),
  settings: coreSettingsSchema.optional(),
  roles: z.array(roleInstanceSchema).optional(),
});

// The root: schemaVersion coerced to a string; settings/systems validated separately
// (per-element tolerant) in parseConfig; unknown root fields preserved (loose).
const projectConfigSchema = z.looseObject({
  schemaVersion: stringField(""),
  settings: z.unknown().optional(),
  systems: z.array(z.unknown()).catch(() => []).default(() => []),
});

// --- schema version (port of schemaVersions.ts) ---------------------------

/** Bump ONLY on a breaking (non-additive) project-format change. Mirrors kProject. */
export const K_PROJECT = 1;

export enum VersionCheck {
  Ok = "ok",
  Older = "older",
  Newer = "newer",
}

export function checkVersion(fileVersion: number, current: number): VersionCheck {
  if (fileVersion === current) return VersionCheck.Ok;
  return fileVersion < current ? VersionCheck.Older : VersionCheck.Newer;
}

/** schemaVersion is a legacy string ("1.0", "2", …); take the leading integer, or
 *  the baseline floor when there are no leading digits. */
export function parseProjectVersion(s: string): number {
  const m = /^\s*(\d+)/.exec(s);
  return m ? parseInt(m[1], 10) : K_PROJECT;
}

// --- build / serialize / parse --------------------------------------------

// A system's universal settings, keeping only non-default fields (or undefined).
function thinSettings(s: CoreSettings | undefined): Partial<CoreSettings> | undefined {
  if (!s) return undefined;
  const out: Partial<CoreSettings> = {};
  if (s.gainDb) out.gainDb = s.gainDb; // 0 = default
  if (s.reloadOnRomChange) out.reloadOnRomChange = s.reloadOnRomChange; // false = default
  return Object.keys(out).length ? out : undefined;
}

/** Live systems + settings → a thin ProjectConfig, dropping runtime ids and any
 *  field at its default. Stamps the running schema version. */
export function buildConfig(settings: ProjectSettings, systems: SystemEntry[]): ProjectConfig {
  return {
    schemaVersion: String(K_PROJECT),
    settings: { ...settings },
    systems: systems.map((e) => {
      const t: SystemThin = { kind: e.kind };
      if (e.romPath) t.romPath = e.romPath;
      if (e.savPath) t.savPath = e.savPath;
      if (e.savSuffix) t.savSuffix = e.savSuffix;
      if (e.embeddedRom) t.embeddedRom = e.embeddedRom;
      const settings = thinSettings(e.settings);
      if (settings) t.settings = settings;
      if (e.roles && e.roles.length) t.roles = e.roles;
      return t;
    }),
  };
}

// Apply `fn` to every asset path a system carries (romPath + savPath), storing the
// result back. The single place path fields are enumerated (mirrors native visitPaths).
function mapSystemPaths(cfg: ProjectConfig, fn: (path: string) => string): void {
  for (const s of cfg.systems) {
    if (s.romPath) s.romPath = fn(s.romPath);
    if (s.savPath) s.savPath = fn(s.savPath);
  }
}

/** Serialize to JSON, rebasing each asset path relative to `baseDir` (portable). An
 *  empty `baseDir` leaves paths absolute. `canonicalize` backs the realpath-hard
 *  toRelative test. */
export function serializeConfig(cfg: ProjectConfig, baseDir: string, canonicalize: (p: string) => string): string {
  const out: ProjectConfig = { ...cfg, settings: { ...cfg.settings }, systems: cfg.systems.map((s) => ({ ...s })) };
  if (baseDir) mapSystemPaths(out, (p) => rebaseToRelative(p, baseDir, canonicalize));
  return JSON.stringify(out);
}

/** Parse config JSON with the zod schemas: validates + defaults + clamps, and
 *  PRESERVES unknown fields (forward-tolerance). Never throws — malformed JSON /
 *  a non-object root yield an empty-default config, a garbage system entry is
 *  dropped, and out-of-range/wrong-type values are coerced. */
export function parseConfig(json: string): ProjectConfig {
  let doc: unknown;
  try {
    doc = JSON.parse(json);
  } catch {
    doc = {};
  }
  const root = doc && typeof doc === "object" && !Array.isArray(doc) ? doc : {};
  const parsed = projectConfigSchema.parse(root);

  // Settings: default when missing/invalid, else clamped/defaulted (unknowns kept).
  const sp = projectSettingsSchema.safeParse(parsed.settings);
  const settings = (sp.success ? sp.data : projectSettingsSchema.parse({})) as ProjectSettings;

  // Systems: keep each valid (object) entry with its unknowns preserved; drop garbage.
  const systems: SystemThin[] = [];
  for (const raw of parsed.systems as unknown[]) {
    const r = systemThinSchema.safeParse(raw);
    if (r.success) systems.push(r.data as SystemThin);
  }

  // Spread `parsed` first to preserve unknown ROOT fields, then override the validated ones.
  return { ...parsed, schemaVersion: parsed.schemaVersion as string, settings, systems } as ProjectConfig;
}

/** Rebase each asset path to absolute against `baseDir`, in place (load side). */
export function toAbsolute(cfg: ProjectConfig, baseDir: string): void {
  mapSystemPaths(cfg, (p) => rebaseToAbsolute(p, baseDir));
}
