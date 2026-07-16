// config.json parse/serialize. The on-disk shape mirrors native's UserConfigJson —
// { schemaVersion, activeKeyboardBindings, activeGamepadBindings, defaultZoom, sramAutoSave }
// — except the TS side renames native's `sramMirror` key to `sramAutoSave` (the string
// values still match native's enum). Reads stay tolerant: a missing field takes its
// default (additive), an unknown one is stripped; malformed / non-object / newer-than-us
// yield null (the store keeps its current value, mirroring native's "retain previous
// snapshot on parse error").

import { userConfigSchema, type UserConfig } from "./userConfig";
import { migrateRaw, readNumericVersion, type MigrationMap, type RawObject } from "./migrate";

/** On-disk schema version. Bump only on a breaking (non-additive) change; a file stamped
 *  newer than this is refused on load, one stamped older is migrated (below). */
export const USER_CONFIG_SCHEMA = 1;

/** Raw-JSON migrations keyed by from-version (see migrate.ts). Empty — config.json hasn't
 *  taken a breaking bump; the seam is here so the first one is a one-line add. */
const USER_CONFIG_MIGRATIONS: MigrationMap = {};

/** Parse config.json text. Returns null when the text can't be trusted (malformed JSON,
 *  a non-object root, or a newer schema stamp) — the caller retains its current config.
 *  A valid but partial/older doc parses with its missing fields defaulted. */
export function parseUserConfig(json: string): UserConfig | null {
  let doc: unknown;
  try {
    doc = JSON.parse(json);
  } catch {
    return null;
  }
  if (!doc || typeof doc !== "object" || Array.isArray(doc)) return null;
  const raw = doc as RawObject;
  if (typeof raw.schemaVersion === "number" && raw.schemaVersion > USER_CONFIG_SCHEMA) return null;
  const migrated = migrateRaw(raw, readNumericVersion(raw, USER_CONFIG_SCHEMA), USER_CONFIG_SCHEMA, USER_CONFIG_MIGRATIONS);
  return userConfigSchema.parse(migrated) as UserConfig;
}

/** Serialize config.json text, stamping the current schema version (native field order). */
export function serializeUserConfig(cfg: UserConfig): string {
  return JSON.stringify({
    schemaVersion: USER_CONFIG_SCHEMA,
    activeKeyboardBindings: cfg.activeKeyboardBindings,
    activeGamepadBindings: cfg.activeGamepadBindings,
    defaultZoom: cfg.defaultZoom,
    sramAutoSave: cfg.sramAutoSave,
  });
}
