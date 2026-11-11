// config.json's sections (docs/modular-graph-phase-f5b-design.md section 6.1): a package owns a root key of the user
// config, exactly as it owns a block of a project's `settings` (projectConfig.ts's sections). The shell keeps every
// non-core root key it does not know verbatim, so the plugin - which lists none of the audio or MIDI packages - writes
// a standalone's `audio` block back instead of erasing it; a section stamped newer than this build's contributor is
// kept verbatim and read as its defaults, refused at the section's own scope; and a write still re-reads the file,
// because two processes (the plugin and a standalone) may share it.
//
// This module is the model plus the parse/serialize half; UserConfigStore owns the file.
import type { ProjectSection } from "@retroplug/core/definePackage";
import { migrateRaw, type RawObject } from "./migrate";
import { readSectionVersion } from "./projectConfig";
import { CONFIG_SECTIONS } from "./generated/packages";
import { CORE_CONFIG_KEYS } from "./userConfig";

// The shell's own root keys are derived from its schema (userConfig.ts), so the list a contributor may not take is the
// schema's shape and nothing else. Re-exported here because this is the module the sections' rules live in.
export { CORE_CONFIG_KEYS };

/** A section's contributor and the package that declares it: the generated registry's entry, and the unit a warning
 *  names when the contributor's own code throws. */
export interface ConfigSectionEntry {
  readonly pkg: string;
  readonly section: ProjectSection;
}

/** The package's section under `key`, or undefined for a root key no package of this product contributes (its value is
 *  then kept verbatim). */
export function configSectionEntryOf(
  key: string,
  sections: readonly ConfigSectionEntry[] = CONFIG_SECTIONS,
): ConfigSectionEntry | undefined {
  return sections.find((entry) => entry.section.key === key);
}

/** The section a package contributes under `key`, or undefined for a key no package of this product contributes. */
export function configSectionOf(
  key: string,
  sections: readonly ConfigSectionEntry[] = CONFIG_SECTIONS,
): ProjectSection | undefined {
  return configSectionEntryOf(key, sections)?.section;
}

/** Warning keys already reported: a contributor's throw is one package's fault, so it is reported once per package and
 *  key, not once per load or per write. */
const warnedSections = new Set<string>();

function warnSectionOnce(entry: ConfigSectionEntry, doing: string, error: unknown): void {
  const key = `${entry.pkg}\u0000${entry.section.key}`;
  if (warnedSections.has(key)) return;
  warnedSections.add(key);
  console.warn(
    `[config] the ${entry.pkg} package's section ${entry.section.key} threw while ${doing}: ${String(error)}. ` +
      "Reading its defaults instead; the rest of config.json is unchanged.",
  );
}

/** A contributor's defaults, or `{}` when even those throw: a package's own breakage must not stop the config from
 *  loading, and there is nothing better to read than an empty block. */
export function configSectionDefaults(entry: ConfigSectionEntry): unknown {
  try {
    return entry.section.defaults();
  } catch (e) {
    warnSectionOnce(entry, "reading its defaults", e);
    return {};
  }
}

/** Every contributor's defaults, by key: what a config that never set a section reads, and what a fresh file starts
 *  with. */
export function defaultConfigSections(
  sections: readonly ConfigSectionEntry[] = CONFIG_SECTIONS,
): Record<string, unknown> {
  const out: Record<string, unknown> = {};
  for (const entry of sections) out[entry.section.key] = configSectionDefaults(entry);
  return out;
}

/** A raw section carried up to its contributor's version and parsed, or kept verbatim. Kept verbatim when it is stamped
 *  NEWER than its contributor reads: the refuse-newer rule at section scope, so the rest of the file loads and this one
 *  block is neither read nor (see serializeConfigSection / UserConfigStore.setSection) written. */
export function parseConfigSection(contributor: ProjectSection, raw: unknown): unknown {
  const block = raw && typeof raw === "object" && !Array.isArray(raw) ? (raw as RawObject) : {};
  const version = contributor.version;
  // An additive-only section declares no version: nothing is stamped in it, so nothing needs migrating, and a stray
  // stamp in the file is just data its schema drops.
  if (version === undefined) return contributor.schema.parse(block);
  const stamped = readSectionVersion(block);
  if (stamped > version) return raw;
  return contributor.schema.parse(migrateRaw(block, stamped, version, contributor.migrations ?? {}));
}

/** A section as config.json writes it: a contributor that declares a version stamps its own, and a block this build
 *  kept verbatim because it is stamped newer is written back exactly as it came. */
export function serializeConfigSection(contributor: ProjectSection, value: unknown): unknown {
  const version = contributor.version;
  if (version === undefined) return value;
  if (!value || typeof value !== "object" || Array.isArray(value)) return value;
  const block = value as RawObject;
  if (readSectionVersion(block) > version) return value;
  return { ...block, schemaVersion: version };
}

/** Refuse a root key that is not a section a host may read or write: one of the shell's own fields (whose value in the
 *  file would become an object the next load clamps back to the default), or `__proto__`, which a plain assignment
 *  treats as a prototype rather than as a value. The generated registry refuses a contributor of the shell's keys;
 *  `__proto__` it cannot refuse (a manifest's member of that name is not its JSON's to declare), so this is the one
 *  gate for it. */
export function assertConfigSectionKey(key: string): void {
  if (CORE_CONFIG_KEYS.includes(key)) {
    throw new Error(`${key} is one of the shell's own config keys, not a config section`);
  }
  if (key === "__proto__") throw new Error("__proto__ is not a config section: it names a prototype");
}

/** What a READER of a section gets: its contributor's parsed value, or its defaults when the block is stamped newer
 *  than this build reads (the refuse-newer rule at section scope). Only the writer keeps the raw block: a reader handed
 *  a block it cannot parse would be typed `T` and hold something else. */
export function readableConfigSection<T = unknown>(
  sections: Readonly<Record<string, unknown>>,
  key: string,
  contributors: readonly ConfigSectionEntry[] = CONFIG_SECTIONS,
): T {
  const entry = configSectionEntryOf(key, contributors);
  if (!Object.prototype.hasOwnProperty.call(sections, key)) {
    // A section this product contributes but the file has never carried reads as its contributor's defaults. The shell
    // does NOT write that block out until the section is set: a file written by an older build round-trips byte for
    // byte, which is what lets the plugin (no audio section) write a standalone's config without inventing one.
    return (entry ? configSectionDefaults(entry) : undefined) as T;
  }
  const raw = sections[key];
  if (entry?.section.version !== undefined && raw !== null && typeof raw === "object" && !Array.isArray(raw)) {
    if (readSectionVersion(raw as RawObject) > entry.section.version) return configSectionDefaults(entry) as T;
  }
  return raw as T;
}

/** Store a section by key as an own, enumerable property. `Object.defineProperty` rather than assignment, because a
 *  hand-edited `__proto__` key is data the shell keeps: assigning it would set this object's prototype and drop the key
 *  on the next save. */
export function putConfigSection(sections: Record<string, unknown>, key: string, value: unknown): void {
  Object.defineProperty(sections, key, { value, enumerable: true, writable: true, configurable: true });
}

/** The sections of a raw config root exactly as the file carried them: every root key the shell does not own, its
 *  value untouched. A block this session does not set is written back from here (the verbatim-keep rule), which is what
 *  preserves a field a newer build added. */
export function rawConfigSections(raw: Record<string, unknown>): Record<string, unknown> {
  const sections: Record<string, unknown> = {};
  for (const [key, value] of Object.entries(raw)) {
    if (CORE_CONFIG_KEYS.includes(key)) continue;
    putConfigSection(sections, key, value);
  }
  return sections;
}

/** The sections of a raw config root: every root key the shell does not own, through its contributor (or kept
 *  verbatim). A contributor's key the file does not have is ABSENT here, not filled: `readableConfigSection` answers
 *  its defaults, and a write of the file leaves the file as it was rather than stamping a block nobody set. */
export function parseConfigSections(
  raw: Record<string, unknown>,
  contributors: readonly ConfigSectionEntry[] = CONFIG_SECTIONS,
): Record<string, unknown> {
  const sections: Record<string, unknown> = {};
  for (const [key, value] of Object.entries(raw)) {
    if (CORE_CONFIG_KEYS.includes(key)) continue;
    const entry = configSectionEntryOf(key, contributors);
    let parsed = value;
    if (entry) {
      try {
        parsed = parseConfigSection(entry.section, value);
      } catch (e) {
        warnSectionOnce(entry, "parsing its saved block", e);
        parsed = configSectionDefaults(entry);
      }
    }
    putConfigSection(sections, key, parsed);
  }
  return sections;
}

/** A section by key as config.json writes it: its contributor's stamping, or the raw value unchanged. */
export function serializeConfigSectionValue(
  key: string,
  value: unknown,
  contributors: readonly ConfigSectionEntry[] = CONFIG_SECTIONS,
): unknown {
  const contributor = configSectionOf(key, contributors);
  return contributor ? serializeConfigSection(contributor, value) : value;
}
