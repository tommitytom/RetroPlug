// config.json parse/serialize. The on-disk shape mirrors native's UserConfigJson —
// { schemaVersion, activeKeyboardBindings, activeGamepadBindings, defaultZoom, sramAutoSave }
// — except the TS side renames native's `sramMirror` key to `sramAutoSave` (the string
// values still match native's enum). Reads stay tolerant: a missing field takes its
// default (additive), an unknown one is stripped; malformed / non-object / newer-than-us
// yield null (the store keeps its current value, mirroring native's "retain previous
// snapshot on parse error").

import { userConfigSchema, type UserConfig } from "./userConfig";
import { stringifyConfig } from "./configSchema";
import { parseVersionedRoot, type MigrationMap, type RawObject, type RootRefusal } from "./migrate";

/** On-disk schema version. Bump only on a breaking (non-additive) change; a file stamped
 *  newer than this is refused on load, one stamped older is migrated (below). */
export const USER_CONFIG_SCHEMA = 2;

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

/** Raw-JSON migrations keyed by from-version (see migrate.ts). */
const USER_CONFIG_MIGRATIONS: MigrationMap = { 1: userConfigV1toV2 };

/** Parse config.json text, saying WHY on failure. The store needs the reason: a malformed file
 *  may be replaced on the next change (so corruption heals), but one stamped newer must not be —
 *  see RootRefusal. A valid but partial/older doc parses with its missing fields defaulted. */
export function parseUserConfigResult(
  json: string,
): { ok: true; value: UserConfig } | { ok: false; reason: RootRefusal; stamped?: number } {
  const root = parseVersionedRoot(json, USER_CONFIG_SCHEMA, USER_CONFIG_MIGRATIONS);
  if (!root.ok) return root;
  return { ok: true, value: userConfigSchema.parse(root.raw) as UserConfig };
}

/** Parse config.json text. Null when the text can't be trusted, for callers that only need the
 *  value; `parseUserConfigResult` distinguishes the failures. */
export function parseUserConfig(json: string): UserConfig | null {
  const r = parseUserConfigResult(json);
  return r.ok ? r.value : null;
}

/** Serialize config.json text, stamping the current schema version. Every schema field must appear here:
 *  commit() diffs two serializations to detect a real change, so a field omitted below can never be
 *  toggled or persisted (it round-trips through parse's default forever). */
export function serializeUserConfig(cfg: UserConfig): string {
  return stringifyConfig({
    schemaVersion: USER_CONFIG_SCHEMA,
    activeKeyboardBindings: cfg.activeKeyboardBindings,
    activeGamepadBindings: cfg.activeGamepadBindings,
    defaultZoom: cfg.defaultZoom,
    sramAutoSave: cfg.sramAutoSave,
    useNativeFileDialogs: cfg.useNativeFileDialogs,
    render: cfg.render,
  });
}
