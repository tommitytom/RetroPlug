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

import type { SystemEntry } from "./systemsList";
import { defaultCoreFor, type Platform, type Core } from "./platform";
import { rebaseToRelative, rebaseToAbsolute } from "./projectPaths";
import { commonSettingsSchema, type CommonSettings } from "./systemSettings";
import type { RoleInstance } from "./systemRoles";
import { z, clampedInt, stringField } from "./configSchema";
import { migrateRaw, type MigrationMap, type RawObject } from "./migrate";

/** Project-level settings (four scalars). `zoom` 0 = inherit the user default. */
export interface ProjectSettings {
  layout: number; // 0 Auto / 1 Row / 2 Column / 3 Grid
  midiRouting: number; // 0 SendToAll / 1 FourPerInstance / 2 OnePerInstance / 3 ChannelToInstance
  audioRouting: number; // 0 Stereo / 1 TwoPerInstance / 2 OnePerInstance / 3 ChannelSplit (1 GB → 8 outs)
  zoom: number; // 0 inherit / 1..6
}

/** A system as serialized: thin, with default per-system fields omitted. Its two structural
 *  axes are both stored: `platform` (what the ROM targets) and `core` (the emulator backend
 *  running it). `core` is persisted since the v2 schema — it's auto-derived from `platform`
 *  today, but storing it lets an explicit core survive if a chooser is ever added. */
export interface SystemThin {
  platform: Platform;
  core: Core;
  romPath?: string;
  savPath?: string; // an explicit override; absent = derive from suffix
  savSuffix?: number;
  embeddedRom?: string; // e.g. "mgb"
  settings?: Partial<CommonSettings>; // only non-default universal settings
  roles?: RoleInstance[]; // the generic roles (core-config + feature); absent = re-derive
}

export interface ProjectConfig {
  schemaVersion: string;
  name?: string; // display name (seeded from the primary system's sav/rom stem); absent = derive on load
  settings: ProjectSettings;
  systems: SystemThin[];
}

// --- zod validation schemas -----------------------------------------------
// STRICT schemas (unknown keys stripped). Forward-tolerance across greenfield versions
// is field `.default()`s + refuse-newer detection, not passthrough (see configSchema.ts).
// A malformed/partial config is coerced (clamp/default), not rejected. The role
// `config` stays an open record — its per-kind RoleType schema validates it elsewhere.

/** Project-level settings: defaults filled, values clamped to their enum ranges. */
export const projectSettingsSchema = z.object({
  layout: clampedInt(0, 3, 0),
  midiRouting: clampedInt(0, 3, 0),
  audioRouting: clampedInt(0, 3, 0),
  zoom: clampedInt(0, 6, 0),
});

export const DEFAULT_SETTINGS: ProjectSettings = projectSettingsSchema.parse({}) as ProjectSettings;

// A role instance: kind + an opaque config record (the record is intentionally open;
// the role's own RoleType schema validates the config's fields).
const roleInstanceSchema = z.object({
  kind: z.string(),
  config: z.record(z.string(), z.unknown()).catch(() => ({})),
});

// Structural identity — the ROM's platform + the emulator backend. Both REQUIRED: a valid
// system always has them, and a migration backfills any pre-v2 file (see projectV1toV2).
// An entry missing/with an invalid value fails the strict parse and is dropped in parseConfig.
const platformSchema = z.enum(["gb", "nes", "gba"]);
const coreSchema = z.enum(["sameboy", "mesen"]);

// A serialized system: structural fields (platform/core) required; asset paths + overrides
// optional (genuine either-ors — file-backed vs embedded, sav override); unknowns stripped.
// `roles[].config` stays an open record (its per-kind RoleType schema validates it, and it
// still crosses to native reflect-cpp).
const systemThinSchema = z.object({
  platform: platformSchema,
  core: coreSchema,
  romPath: z.string().optional(),
  savPath: z.string().optional(),
  savSuffix: z.number().optional(),
  embeddedRom: z.string().optional(),
  settings: commonSettingsSchema.optional(),
  roles: z.array(roleInstanceSchema).optional(),
});

// The root: schemaVersion coerced to a string; settings/systems validated separately
// (per-element tolerant) in parseConfig.
const projectConfigSchema = z.object({
  schemaVersion: stringField(""),
  name: z.string().optional(),
  settings: z.unknown().optional(),
  systems: z.array(z.unknown()).catch(() => []).default(() => []),
});

/** v1 → v2: persist each system's `core` (previously re-derived from `platform` on every
 *  load and never stored). Backfills `core = defaultCoreFor(platform)`; idempotent (only
 *  fills when absent, and leaves a garbage/unknown-platform entry's core unset so the strict
 *  parse drops it rather than inventing one). */
function projectV1toV2(raw: RawObject): RawObject {
  const systems = raw.systems;
  if (Array.isArray(systems)) {
    for (const s of systems) {
      if (s && typeof s === "object") {
        const sys = s as RawObject;
        const core = defaultCoreFor(sys.platform as Platform); // undefined for a non-platform string
        if (sys.core == null && core) sys.core = core;
      }
    }
  }
  return raw;
}

/** Ordered raw-JSON migrations for the project root, keyed by from-version (see migrate.ts):
 *  `PROJECT_MIGRATIONS[v]` upgrades a v-stamped config to v+1. */
const PROJECT_MIGRATIONS: MigrationMap = { 1: projectV1toV2 };

/** Bring a raw config from its stamped `fromVersion` up to `K_PROJECT`, on the raw object,
 *  before the (single, latest) zod schema validates it. The Newer branch (refuse) lives at
 *  the load seam (ProjectStore.beginLoad); this handles Older. */
function migrateProjectRaw(raw: RawObject, fromVersion: number): RawObject {
  return migrateRaw(raw, fromVersion, K_PROJECT, PROJECT_MIGRATIONS);
}

// --- schema version (port of schemaVersions.ts) ---------------------------

/** The current project-format version. Bump ONLY on a breaking (non-additive) change, and add
 *  the matching `PROJECT_MIGRATIONS[N-1]` step. v2 persists each system's `core`. */
export const K_PROJECT = 2;

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
function thinSettings(s: CommonSettings | undefined): Partial<CommonSettings> | undefined {
  if (!s) return undefined;
  const out: Partial<CommonSettings> = {};
  if (s.gainDb) out.gainDb = s.gainDb; // 0 = default
  if (s.reloadOnRomChange) out.reloadOnRomChange = s.reloadOnRomChange; // false = default
  return Object.keys(out).length ? out : undefined;
}

/** Live systems + settings → a thin ProjectConfig, dropping runtime ids and any
 *  field at its default. Stamps the running schema version. `name` is omitted when
 *  empty (thin convention — the reader derives one). */
export function buildConfig(settings: ProjectSettings, systems: SystemEntry[], name = ""): ProjectConfig {
  return {
    schemaVersion: String(K_PROJECT),
    ...(name ? { name } : {}),
    settings: { ...settings },
    systems: systems.map((e) => {
      const t: SystemThin = { platform: e.platform, core: e.core };
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

/** Parse config JSON with the zod schemas: migrate an older raw shape up to current
 *  (no-op today), then validate + default + clamp (unknown fields stripped). Never
 *  throws — malformed JSON / a non-object root yield an empty-default config, a
 *  garbage system entry is dropped, out-of-range/wrong-type values are coerced. */
export function parseConfig(json: string): ProjectConfig {
  let doc: unknown;
  try {
    doc = JSON.parse(json);
  } catch {
    doc = {};
  }
  const root = (doc && typeof doc === "object" && !Array.isArray(doc) ? doc : {}) as Record<string, unknown>;
  const version = parseProjectVersion(typeof root.schemaVersion === "string" ? root.schemaVersion : "");
  const migrated = migrateProjectRaw(root, version);
  const parsed = projectConfigSchema.parse(migrated);

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
