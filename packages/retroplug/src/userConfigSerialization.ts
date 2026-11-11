// config.json parse/serialize. The on-disk shape is { schemaVersion, the shell's own fields, then each package's
// section under its root key } (docs/modular-graph-phase-f5b-design.md section 6.1): the core fields keep the order
// they always had, and a section this session did not set is written back as the file carried it, so a field a newer
// build added survives and a plugin never erases a standalone's blocks. Reads stay tolerant: a missing core field
// takes its default (additive), an unknown one is stripped from the core object; malformed / non-object /
// newer-than-us yield a refusal (the store keeps its current value).

import { userConfigSchema, type ConfigModel, type UserConfig, configFileShape } from "./userConfig";
import { stringifyConfig } from "./configSchema";
import { parseVersionedRoot, type MigrationMap, type RawObject, type RootRefusal } from "./migrate";
import { parseConfigSections, rawConfigSections, type ConfigSectionEntry } from "./configSections";
import { CONFIG_SECTIONS } from "./generated/packages";

/** On-disk schema version. Bump only on a breaking (non-additive) change; a file stamped
 *  newer than this is refused on load, one stamped older is migrated (below). */
export const USER_CONFIG_SCHEMA = 3;

/** v1→v2: `render.lastDir` was renamed to `render.outputDir`. Carry the value forward so a
 *  remembered folder survives the rename; zod then strips the leftover `lastDir` (unknown key).
 *  Idempotent — only fills `outputDir` when it's absent. */
function userConfigV1toV2(raw: RawObject): RawObject {
  const render = raw.render;
  if (render && typeof render === "object" && !Array.isArray(render)) {
    const r = render as RawObject;
    if (r.outputDir == null && typeof r.lastDir === "string") r.outputDir = r.lastDir;
  }
  return raw;
}

/** v2→v3: the `Off` sramAutoSave mode was retired (it never suppressed the save-path write, so it was
 *  `OnProjectSave` wearing a different label). Map it forward explicitly. Zod's `.catch` would coerce an
 *  unknown value to the same default anyway, but relying on that would leave no record in the code that
 *  `Off` ever existed - and a silent coercion is a landmine for whoever adds the next mode.
 *  Idempotent — only rewrites the retired value. */
function userConfigV2toV3(raw: RawObject): RawObject {
  if (raw.sramAutoSave === "Off") raw.sramAutoSave = "OnProjectSave";
  return raw;
}

/** Raw-JSON migrations keyed by from-version (see migrate.ts). */
const USER_CONFIG_MIGRATIONS: MigrationMap = { 1: userConfigV1toV2, 2: userConfigV2toV3 };

/** Parse config.json text, saying WHY on failure. The store needs the reason: a malformed file
 *  may be replaced on the next change (so corruption heals), but one stamped newer must not be —
 *  see RootRefusal. A valid but partial/older doc parses with its missing fields defaulted, and each non-core root
 *  key comes back as a section (its contributor's parse, or the raw block). */
export function parseUserConfigResult(
  json: string,
  contributors: readonly ConfigSectionEntry[] = CONFIG_SECTIONS,
): { ok: true; model: ConfigModel } | { ok: false; reason: RootRefusal; stamped?: number } {
  const root = parseVersionedRoot(json, USER_CONFIG_SCHEMA, USER_CONFIG_MIGRATIONS);
  if (!root.ok) return root;
  return {
    ok: true,
    model: {
      core: userConfigSchema.parse(root.raw) as UserConfig,
      sections: parseConfigSections(root.raw, contributors),
      // The file's own blocks beside the parsed ones: a block this session does not set is written back from these, so
      // an additive field this build's contributor drops survives (the verbatim-keep rule).
      raw: rawConfigSections(root.raw),
      written: [],
    },
  };
}

/** Parse config.json text. Null when the text can't be trusted, for callers that only need the
 *  value; `parseUserConfigResult` distinguishes the failures. */
export function parseUserConfig(json: string): UserConfig | null {
  const r = parseUserConfigResult(json);
  return r.ok ? r.model.core : null;
}

/** Serialize config.json text, stamping the current schema version. Every core schema field must appear here:
 *  commit() diffs two serializations to detect a real change, so a field omitted below can never be
 *  toggled or persisted (it round-trips through parse's default forever). The sections follow, in the order the model
 *  holds them: a contributor's key in product order, then a key no package contributes in the order it was read. */
export function serializeUserConfig(
  model: ConfigModel,
  contributors: readonly ConfigSectionEntry[] = CONFIG_SECTIONS,
): string {
  return stringifyConfig({ schemaVersion: USER_CONFIG_SCHEMA, ...configFileShape(model, contributors) });
}
