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
import { registeredCores, registeredPlatforms } from "./consoles";
import { rebaseToRelative, rebaseToAbsolute } from "./projectPaths";
import { commonSettingsSchema, type CommonSettings } from "./systemSettings";
import type { RoleInstance } from "@retroplug/core/systemRoles";
import { z } from "zod";
import { clampedInt, stringField, enumField } from "@retroplug/core/configFields";
import { stringifyConfig } from "./configSchema";
import { migrateRaw, type MigrationMap, type RawObject } from "./migrate";
import { MIDI_ROUTING_VALUES, MidiRouting } from "@retroplug/core/midiRouting";
import { type SystemLayout, type AudioRouting, LAYOUT_VALUES, AUDIO_ROUTING_VALUES } from "./settingsEnums";
import type { ProjectSection } from "@retroplug/core/definePackage";
// The product's registrations, and through them every declared contribution kind's `accept`: each kind fills the table
// its declaring package's schema, defaults or placer reads. A module that reads a kind-filled registry imports the
// generated registry for that reason; this one also needs PROJECT_SECTIONS itself.
import { PROJECT_SECTIONS } from "./generated/packages";

/** The settings keys the SHELL owns, which no package may contribute as a section (the generated registry refuses one
 *  that does). Everything else under `settings` belongs to whichever package contributed it. Keep in step with the
 *  generated registry's SHELL_SECTION_KEYS (packages.mjs): the two lists are the one rule, checked and trusted. */
export const CORE_SETTING_KEYS = ["layout", "midiRouting", "audioRouting", "zoom"] as const;

/** Project-level settings. The three routing/layout enums are string-valued (see settingsEnums); `zoom` is a magnitude
 *  (0 = inherit the user default, else 1..6); and `sections` are the blocks a package contributes (the control
 *  surface's `controller`), each parsed by its own contributor, or kept verbatim for a key no package of this product
 *  contributes.
 *  The .rplg keeps them at the top level of `settings`, so a section's key IS its key in the file. */
export interface ProjectSettings {
  layout: SystemLayout;
  midiRouting: MidiRouting;
  audioRouting: AudioRouting; // channelSplit/pinSplit fan ONE system's channels across the outs
  zoom: number; // 0 inherit / 1..6
  sections: Readonly<Record<string, unknown>>;
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
  workingSlot?: { index: number; fingerprint: number; count?: number; above?: number }; // which saved slot a work-RAM working song is (smsggdj)
}

export interface ProjectConfig {
  schemaVersion: string;
  name?: string; // display name (seeded from the primary system's sav/rom stem); absent = derive on load
  settings: ProjectSettings;
  systems: SystemThin[];
}

/** A `.rplg`'s `settings` as the file carries it, which buildConfig writes and serializeConfig serializes: the four
 *  core fields, and every section at the top level under its own key. Distinct from ProjectSettings, whose sections
 *  are nested by key, so a caller cannot read the file shape and mistake it for the parsed one (a built config has no
 *  `sections` member, and a parsed one carries no section at the top level). */
export interface ProjectFileSettings {
  layout: SystemLayout;
  midiRouting: MidiRouting;
  audioRouting: AudioRouting;
  zoom: number;
  /** A section's block, by the key it is written under. */
  [section: string]: unknown;
}

/** A config as the file carries it: buildConfig's output, which serializeConfig writes. */
export interface ProjectFileConfig extends Omit<ProjectConfig, "settings"> {
  settings: ProjectFileSettings;
}

// --- zod validation schemas -----------------------------------------------
// STRICT schemas (unknown keys stripped, except under `settings`: a non-core key there is a package's section and is
// kept, see parseSections). Forward-tolerance across config versions is field `.default()`s (core's configFields
// builders) + refuse-newer detection, not passthrough.
// A malformed/partial config is coerced (clamp/default), not rejected. The role
// `config` stays an open record — its per-kind RoleType schema validates it elsewhere.

/** Project-level settings: defaults filled, enum values snapped to the known set (unknown → default),
 *  zoom clamped to its magnitude range. The sections are the contributors' (see parseSections). */
export const projectSettingsSchema = z.object({
  layout: enumField(LAYOUT_VALUES, "auto"),
  midiRouting: enumField(MIDI_ROUTING_VALUES, "sendToAll"),
  audioRouting: enumField(AUDIO_ROUTING_VALUES, "stereo"),
  zoom: clampedInt(0, 6, 0),
});

/** A section's contributor and the package that declares it: the generated registry's entry, and the unit a warning
 *  names when the contributor's own code throws. */
export interface SectionEntry {
  readonly pkg: string;
  readonly section: ProjectSection;
}

/** The package's section under `key`, or undefined for a key no package of this product contributes (its value is then
 *  kept verbatim). */
export function projectSectionEntryOf(
  key: string,
  sections: readonly SectionEntry[] = PROJECT_SECTIONS,
): SectionEntry | undefined {
  return sections.find((entry) => entry.section.key === key);
}

/** The section a package contributes under `key`, or undefined for a key no package of this product contributes. */
export function projectSectionOf(
  key: string,
  sections: readonly SectionEntry[] = PROJECT_SECTIONS,
): ProjectSection | undefined {
  return projectSectionEntryOf(key, sections)?.section;
}

/** Warning keys already reported: a contributor's throw is one package's fault, so it is reported once per package and
 *  key, not once per load or per publish. */
const warnedSections = new Set<string>();

function warnSectionOnce(entry: SectionEntry, doing: string, error: unknown): void {
  const key = `${entry.pkg}\u0000${entry.section.key}`;
  if (warnedSections.has(key)) return;
  warnedSections.add(key);
  console.warn(
    `[project] the ${entry.pkg} package's section ${entry.section.key} threw while ${doing}: ${String(error)}. ` +
      "Reading its defaults instead; the rest of the project is unchanged.",
  );
}

/** A contributor's defaults, or `{}` when even those throw: a package's own breakage must not stop the project from
 *  loading, and there is nothing better to read than an empty block. */
export function sectionDefaults(entry: SectionEntry): unknown {
  try {
    return entry.section.defaults();
  } catch (e) {
    warnSectionOnce(entry, "reading its defaults", e);
    return {};
  }
}

/** Every contributor's defaults, by key: what a project that never set a section reads, and what a fresh project
 *  starts with. */
export function defaultSections(sections: readonly SectionEntry[] = PROJECT_SECTIONS): Record<string, unknown> {
  const out: Record<string, unknown> = {};
  for (const entry of sections) out[entry.section.key] = sectionDefaults(entry);
  return out;
}

/** A settings object's core fields, in the order the .rplg writes them. */
function coreSettings(settings: ProjectSettings): Omit<ProjectSettings, "sections"> {
  return {
    layout: settings.layout,
    midiRouting: settings.midiRouting,
    audioRouting: settings.audioRouting,
    zoom: settings.zoom,
  };
}

/** The typed read of a section as its contributor parsed it, or the raw JSON of a key no package contributes. The
 *  writer reads through here (ProjectStore.setSection's stamp check and serializeSection): a block stamped newer is
 *  kept verbatim, and written back as it came. */
export function projectSection<T = unknown>(settings: ProjectSettings, key: string): T {
  return settings.sections[key] as T;
}

/** What a READER of a section gets: its contributor's parsed value, or its defaults when the block is stamped newer
 *  than this build reads (the refuse-newer rule at section scope). Only the writer keeps the raw block, through
 *  projectSection and buildConfig: a reader handed a block it cannot parse would be typed `T` and hold something else. */
export function readableProjectSection<T = unknown>(
  settings: ProjectSettings,
  key: string,
  sections: readonly SectionEntry[] = PROJECT_SECTIONS,
): T {
  const raw = projectSection<unknown>(settings, key);
  const entry = projectSectionEntryOf(key, sections);
  if (entry?.section.version !== undefined && raw !== null && typeof raw === "object" && !Array.isArray(raw)) {
    if (readSectionVersion(raw as RawObject) > entry.section.version) return sectionDefaults(entry) as T;
  }
  return raw as T;
}

/** A raw section carried up to its contributor's version and parsed, or kept verbatim. Kept verbatim when it is stamped
 *  NEWER than its contributor reads: the refuse-newer rule at section scope, so the rest of the project loads and this
 *  one block is neither read nor (see serializeProjectSection / ProjectStore.setSection) written. Exported because the
 *  versioned section a package contributes is what holds this rule: no product's section declares a version today (the
 *  controller is additive), so the mechanism is tested through here (test/project/sections.test.ts). */
export function parseProjectSection(contributor: ProjectSection, raw: unknown): unknown {
  const block = raw && typeof raw === "object" && !Array.isArray(raw) ? (raw as RawObject) : {};
  const version = contributor.version;
  // An additive-only section declares no version: nothing is stamped in it, so nothing needs migrating, and a stray
  // stamp in the file is just data its schema drops.
  if (version === undefined) return contributor.schema.parse(block);
  const stamped = readSectionVersion(block);
  if (stamped > version) return raw;
  return contributor.schema.parse(migrateRaw(block, stamped, version, contributor.migrations ?? {}));
}

/** A section as the .rplg writes it: a contributor that declares a version stamps its own, and a block this build kept
 *  verbatim because it is stamped newer is written back exactly as it came. */
export function serializeProjectSection(contributor: ProjectSection, value: unknown): unknown {
  const version = contributor.version;
  if (version === undefined) return value;
  if (!value || typeof value !== "object" || Array.isArray(value)) return value;
  const block = value as RawObject;
  if (readSectionVersion(block) > version) return value;
  return { ...block, schemaVersion: version };
}

/** The version an unstamped section reads as: the shape before any stamp. A section is not stamped until it first
 *  versions, so its first versioned release declares 2 with a step keyed 1, and every project written before that
 *  release carries the v1 shape. (A root is always stamped, so readNumericVersion's unstamped-is-current rule is the
 *  root's alone.) */
const SECTION_BASELINE_VERSION = 1;

/** A section block's own stamp, or the baseline for a block that has none: an unstamped section reads as version 1 (the
 *  shape before any stamp), which is why the first versioned release declares 2. A ROOT is always stamped, so its
 *  readNumericVersion's unstamped-is-current rule is not this one. */
export function readSectionVersion(block: RawObject): number {
  const stamp = block.schemaVersion;
  return typeof stamp === "number" && Number.isFinite(stamp) ? stamp : SECTION_BASELINE_VERSION;
}

/** Refuse a settings key that is not a section a host may read or write: one of the shell's own fields (whose value in
 *  the file would become an object the next load clamps back to the default), or `__proto__`, which a plain assignment
 *  treats as a prototype rather than as a value. The generated registry refuses a contributor of the shell's keys;
 *  `__proto__` it cannot refuse (a manifest's member of that name is not its JSON's to declare), so this is the one
 *  gate for it. */
export function assertSectionKey(key: string): void {
  if ((CORE_SETTING_KEYS as readonly string[]).includes(key)) {
    throw new Error(`${key} is one of the shell's own settings, not a project section`);
  }
  if (key === "__proto__") throw new Error("__proto__ is not a project section: it names a prototype");
}

/** A section's raw value by key: its contributor's parse (its defaults when that throws), or the value itself for a
 *  key no package contributes. A contributor's own `schema.parse` may throw on a block this build does not understand;
 *  parseConfig's contract is that it never throws, so one package's breakage is isolated to its section. */
function parseSectionValue(key: string, raw: unknown, sections: readonly SectionEntry[]): unknown {
  const entry = projectSectionEntryOf(key, sections);
  if (!entry) return raw;
  try {
    return parseProjectSection(entry.section, raw);
  } catch (e) {
    warnSectionOnce(entry, "parsing its saved block", e);
    return sectionDefaults(entry);
  }
}

/** Store a section by key as an own, enumerable property. `Object.defineProperty` rather than assignment, because a
 *  hand-edited `__proto__` key is data E5 keeps: assigning it would set this object's prototype and drop the key on the
 *  next save. */
function putSection(sections: Record<string, unknown>, key: string, value: unknown): void {
  Object.defineProperty(sections, key, { value, enumerable: true, writable: true, configurable: true });
}

/** The sections of a raw `settings` object: every key the shell does not own, through its contributor (or kept
 *  verbatim), then `defaults()` for each contributor whose key the file does not have. */
function parseSections(raw: unknown, contributors: readonly SectionEntry[]): Record<string, unknown> {
  const block = raw && typeof raw === "object" && !Array.isArray(raw) ? (raw as Record<string, unknown>) : {};
  const sections: Record<string, unknown> = {};
  for (const [key, value] of Object.entries(block)) {
    if ((CORE_SETTING_KEYS as readonly string[]).includes(key)) continue;
    putSection(sections, key, parseSectionValue(key, value, contributors));
  }
  for (const entry of contributors) {
    if (!Object.prototype.hasOwnProperty.call(sections, entry.section.key)) {
      putSection(sections, entry.section.key, sectionDefaults(entry));
    }
  }
  return sections;
}

/** A section by key as the .rplg writes it: its contributor's stamping, or the raw value unchanged. */
function serializeSection(key: string, value: unknown): unknown {
  const contributor = projectSectionOf(key);
  return contributor ? serializeProjectSection(contributor, value) : value;
}

export const DEFAULT_SETTINGS: ProjectSettings = {
  ...(projectSettingsSchema.parse({}) as Omit<ProjectSettings, "sections">),
  sections: defaultSections(),
};

// A role instance: kind + an opaque config record (the record is intentionally open;
// the role's own RoleType schema validates the config's fields).
const roleInstanceSchema = z.object({
  kind: z.string(),
  config: z.record(z.string(), z.unknown()).catch(() => ({})),
});

// Structural identity — the ROM's platform + the emulator backend. Both REQUIRED: a valid
// system always has them, and a migration backfills any pre-v2 file (see projectV1toV2).
// An entry missing/with an invalid value fails the strict parse and is dropped in parseConfig.
//
// The known values are the console registry's (src/consoles.ts over the descriptors the product's packages contribute),
// read when the refine runs rather than when this module loads, so a package's console reaches validation without an
// import of it. A file naming a platform or core no build registers is refused rather than parsed: the refinement's
// message names the set, which a direct caller sees and parseConfig's silent drop does not.
export const platformSchema = z.string().superRefine((v: string, ctx) => {
  if (registeredPlatforms().some((platform) => platform.id === v)) return;
  ctx.addIssue({
    code: "custom",
    message: `unknown platform ${JSON.stringify(v)} (known: ${registeredPlatforms().map((p) => p.id).join(", ")})`,
  });
});
export const coreSchema = z.string().superRefine((v: string, ctx) => {
  if (registeredCores().includes(v)) return;
  ctx.addIssue({ code: "custom", message: `unknown core ${JSON.stringify(v)} (known: ${registeredCores().join(", ")})` });
});

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
  // Additive, so no migration: an older project simply has no tracked slot, and Save appends.
  workingSlot: z
    .object({
      index: z.number().int().min(0),
      fingerprint: z.number().int(),
      count: z.number().int().min(0).optional(),
      above: z.number().int().optional(),
    })
    .optional()
    .catch(undefined),
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

// The enum fields that became string-valued in v3, with their index-ordered value tuples (the tuple
// index equals the old integer). Project `settings` enums + the per-system role-config enums, keyed
// by role kind. `zoom` and every genuine-numeric field (linkGroupId, tempoDivisor, …) are excluded.
const V3_SETTINGS_ENUMS: Record<string, readonly string[]> = {
  layout: LAYOUT_VALUES,
  midiRouting: MIDI_ROUTING_VALUES,
  audioRouting: AUDIO_ROUTING_VALUES,
};
// The v2 file's five cart-owned enum lists, FROZEN as the ordinals it wrote (the index was the integer value). A
// package may rename a member or add one, and that is its business: this step rewrites a file written years ago, so it
// reads the values that file's writers had, never today's. test/project/migrate-v3-frozen.test.ts asserts every frozen
// value is still one the owning package's schema accepts, so a rename fails there instead of migrating an old file to a
// default (an append or a reorder is the package's to make and passes).
const V3_SAMEBOY_MODELS = [
  "auto", "dmgB", "mgb", "sgb", "sgbPal", "sgb2", "cgb0", "cgbA", "cgbB", "cgbC", "cgbD", "cgbE", "agb", "gbp",
] as const;
const V3_SAMEBOY_HIGHPASS = ["off", "accurate", "removeDcOffset"] as const;
const V3_MESEN_REGIONS = ["auto", "ntsc", "pal", "dendy", "ntscJapan"] as const;
const V3_MESEN_CHANNEL_EXPORTS = ["mix", "stereoModPins", "pinsPlusRef", "individualMono"] as const;
const V3_LSDJ_SYNC_MODES = [
  "off",
  "midiSync",
  "midiSyncArduinoboy",
  "midiMap",
  "keyboard",
  "keyboardMidi",
  "midiPassthrough",
  "midiOut",
  "masterSync",
] as const;
/** The v2 writer's five cart-owned enum lists by role kind and field, exported so the frozen-list test can hold them
 *  to the packages' current schemas and to what the step writes. */
export const V3_ROLE_ENUMS: Record<string, Record<string, readonly string[]>> = {
  sameboy: { model: V3_SAMEBOY_MODELS, highpass: V3_SAMEBOY_HIGHPASS },
  mesen: { region: V3_MESEN_REGIONS, channelExportMode: V3_MESEN_CHANNEL_EXPORTS },
  "lsdj-sync": { mode: V3_LSDJ_SYNC_MODES },
};

// Rewrite one integer enum field to its string value in place. Idempotent: an already-string value
// (a v3 file) or an out-of-range int is left as-is for the tolerant enum schema to default.
function intFieldToString(obj: RawObject, key: string, values: readonly string[]): void {
  const v = obj[key];
  if (typeof v === "number" && values[v] !== undefined) obj[key] = values[v];
}

/** v2 → v3: the enum settings became string-valued. Rewrites the integer `settings` enums and each
 *  system's role-config enum fields to their string spellings; `zoom` and all genuine-numeric fields
 *  are untouched. Idempotent-safe (only rewrites numbers). */
function projectV2toV3(raw: RawObject): RawObject {
  const settings = raw.settings;
  if (settings && typeof settings === "object") {
    for (const key of Object.keys(V3_SETTINGS_ENUMS)) intFieldToString(settings as RawObject, key, V3_SETTINGS_ENUMS[key]);
  }
  const systems = raw.systems;
  if (Array.isArray(systems)) {
    for (const s of systems) {
      const roles = s && typeof s === "object" ? (s as RawObject).roles : undefined;
      if (!Array.isArray(roles)) continue;
      for (const r of roles) {
        if (!r || typeof r !== "object") continue;
        const role = r as RawObject;
        const fields = typeof role.kind === "string" ? V3_ROLE_ENUMS[role.kind] : undefined;
        const config = role.config;
        if (!fields || !config || typeof config !== "object") continue;
        for (const key of Object.keys(fields)) intFieldToString(config as RawObject, key, fields[key]);
      }
    }
  }
  return raw;
}

// The role kinds renamed in v4, old → new. The EverMIDI project became BlipToaster, and the two role kinds
// it owns are persisted verbatim as `systems[].roles[].kind`, so old projects carry the old spelling.
// NOTE the keys are the PRE-rename spellings and must stay that way — they are the whole point of the step.
const V4_ROLE_RENAMES: Record<string, string> = {
  evermidi: "bliptoaster",
  "evermidi-assets": "bliptoaster-assets",
};

/** v3 → v4: the EverMIDI role kinds renamed to BlipToaster. Rewrites each system's `roles[].kind` in
 *  place. Only the kind string moves — both roles keep their config shape (the marker role has none, and
 *  the assets role keeps its `overrides` list), so no config rewriting is needed. Idempotent-safe: an
 *  already-renamed kind is not a key of the map and is left alone. */
function projectV3toV4(raw: RawObject): RawObject {
  const systems = raw.systems;
  if (Array.isArray(systems)) {
    for (const s of systems) {
      const roles = s && typeof s === "object" ? (s as RawObject).roles : undefined;
      if (!Array.isArray(roles)) continue;
      for (const r of roles) {
        if (!r || typeof r !== "object") continue;
        const role = r as RawObject;
        const renamed = typeof role.kind === "string" ? V4_ROLE_RENAMES[role.kind] : undefined;
        if (renamed) role.kind = renamed;
      }
    }
  }
  return raw;
}

/** Ordered raw-JSON migrations for the project root, keyed by from-version (see migrate.ts):
 *  `PROJECT_MIGRATIONS[v]` upgrades a v-stamped config to v+1. */
const PROJECT_MIGRATIONS: MigrationMap = { 1: projectV1toV2, 2: projectV2toV3, 3: projectV3toV4 };

/** Bring a raw config from its stamped `fromVersion` up to `K_PROJECT`, on the raw object,
 *  before the (single, latest) zod schema validates it. The Newer branch (refuse) lives at
 *  the load seam (ProjectStore.beginLoad); this handles Older. */
function migrateProjectRaw(raw: RawObject, fromVersion: number): RawObject {
  return migrateRaw(raw, fromVersion, K_PROJECT, PROJECT_MIGRATIONS);
}

// --- schema version (port of schemaVersions.ts) ---------------------------

/** The current project-format version. Bump ONLY on a breaking (non-additive) change, and add
 *  the matching `PROJECT_MIGRATIONS[N-1]` step. v2 persists each system's `core`; v3 makes the
 *  enum settings string-valued; v4 renames the EverMIDI role kinds to BlipToaster. */
export const K_PROJECT = 4;

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
export function buildConfig(settings: ProjectSettings, systems: SystemEntry[], name = ""): ProjectFileConfig {
  return {
    schemaVersion: String(K_PROJECT),
    ...(name ? { name } : {}),
    // The core fields, then every section at the top level under its own key: a project with a controller block keeps
    // `settings.controller` where it was, byte for byte, and a block no package contributes is written back as it came.
    // A core key in the sections record is skipped: the shell's own fields are the core's, whatever a writer put there.
    settings: {
      ...coreSettings(settings),
      ...Object.fromEntries(
        Object.entries(settings.sections)
          .filter(([key]) => !(CORE_SETTING_KEYS as readonly string[]).includes(key))
          .map(([key, value]) => [key, serializeSection(key, value)]),
      ),
    },
    systems: systems.map((e) => {
      const t: SystemThin = { platform: e.platform, core: e.core };
      if (e.romPath) t.romPath = e.romPath;
      if (e.savPath) t.savPath = e.savPath;
      if (e.savSuffix) t.savSuffix = e.savSuffix;
      if (e.embeddedRom) t.embeddedRom = e.embeddedRom;
      const settings = thinSettings(e.settings);
      if (settings) t.settings = settings;
      if (e.roles && e.roles.length) t.roles = e.roles;
      if (e.workingSlot) t.workingSlot = { ...e.workingSlot };
      return t;
    }),
  };
}

// Apply `fn` to every asset path a system carries (romPath + savPath), storing the
// result back. The single place path fields are enumerated (mirrors native visitPaths).
function mapSystemPaths(cfg: { systems: SystemThin[] }, fn: (path: string) => string): void {
  for (const s of cfg.systems) {
    if (s.romPath) s.romPath = fn(s.romPath);
    if (s.savPath) s.savPath = fn(s.savPath);
  }
}

/** Serialize to JSON, rebasing each asset path relative to `baseDir` (portable). An
 *  empty `baseDir` leaves paths absolute. `canonicalize` backs the realpath-hard
 *  toRelative test. */
export function serializeConfig(cfg: ProjectFileConfig, baseDir: string, canonicalize: (p: string) => string): string {
  const out: ProjectFileConfig = { ...cfg, settings: { ...cfg.settings }, systems: cfg.systems.map((s) => ({ ...s })) };
  if (baseDir) mapSystemPaths(out, (p) => rebaseToRelative(p, baseDir, canonicalize));
  return stringifyConfig(out);
}

/** Parse config JSON with the zod schemas: migrate an older raw shape up to current
 *  (three steps today - see PROJECT_MIGRATIONS), then validate + default + clamp
 *  (unknown fields are stripped, except a `settings` key no package contributes, which is kept: E5). Never
 *  throws — malformed JSON / a non-object root yield an empty-default config, a
 *  garbage system entry is dropped, out-of-range/wrong-type values are coerced. */
export function parseConfig(json: string, sections: readonly SectionEntry[] = PROJECT_SECTIONS): ProjectConfig {
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

  // Settings: default when missing/invalid, else clamped/defaulted; then the sections, each through its contributor.
  const sp = projectSettingsSchema.safeParse(parsed.settings);
  const core = (sp.success ? sp.data : projectSettingsSchema.parse({})) as Omit<ProjectSettings, "sections">;
  const settings: ProjectSettings = { ...core, sections: parseSections(parsed.settings, sections) };

  // Systems: keep each valid (object) entry, drop garbage.
  const systems: SystemThin[] = [];
  for (const raw of parsed.systems as unknown[]) {
    const r = systemThinSchema.safeParse(raw);
    if (r.success) systems.push(r.data as SystemThin);
  }

  // Spread `parsed` first, then override the validated members. (`parsed` has already been
  // through projectConfigSchema, so unknown root fields are gone by this point.)
  return { ...parsed, schemaVersion: parsed.schemaVersion as string, settings, systems } as ProjectConfig;
}

/** Rebase each asset path to absolute against `baseDir`, in place (load side). */
export function toAbsolute(cfg: ProjectConfig, baseDir: string): void {
  mapSystemPaths(cfg, (p) => rebaseToAbsolute(p, baseDir));
}
