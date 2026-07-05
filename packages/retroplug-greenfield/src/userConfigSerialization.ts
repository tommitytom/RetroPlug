// config.json parse/serialize. The on-disk shape mirrors native's UserConfigJson —
// { schemaVersion, activeKeyboardBindings, activeGamepadBindings, defaultZoom, sramAutoSave }
// — except greenfield renames native's `sramMirror` key to `sramAutoSave` (the string
// values still match native's enum). Reads stay tolerant: a missing field takes its
// default (additive), an unknown one is stripped; malformed / non-object / newer-than-us
// yield null (the store keeps its current value, mirroring native's "retain previous
// snapshot on parse error").

import { userConfigSchema, type UserConfig } from "./userConfig";

/** On-disk schema version. Bump only on a breaking (non-additive) change; a file stamped
 *  newer than this is refused on load. Matches the native `kUserConfig`. */
export const USER_CONFIG_SCHEMA = 1;

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
  const d = doc as { schemaVersion?: unknown };
  if (typeof d.schemaVersion === "number" && d.schemaVersion > USER_CONFIG_SCHEMA) return null;
  return userConfigSchema.parse(doc) as UserConfig;
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
