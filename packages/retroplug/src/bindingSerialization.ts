// bindings/<name>.json parse/serialize. The on-disk shape matches native's BindingMapJson
// — { schemaVersion, name, keyboard, gamepad } — so a user's current profiles still load
// when the real backend replaces the C++ one. Reads stay tolerant: a missing field takes
// its default (additive), an unknown one is stripped, a bad channel becomes {}; malformed
// / non-object / newer-than-us yield null (the caller keeps its current value).

import { bindingMapSchema, type BindingMap } from "./bindingMap";
import { stringifyConfig } from "./configSchema";
import { parseVersionedRoot, type MigrationMap, type RootRefusal } from "./migrate";

/** On-disk schema version. Bump only on a breaking (non-additive) change; a file stamped
 *  newer than this is refused on load, one stamped older is migrated (below). */
export const BINDINGS_SCHEMA = 1;

/** Raw-JSON migrations keyed by from-version (see migrate.ts). Empty — bindings hasn't taken
 *  a breaking bump; the seam is here so the first one is a one-line add. */
const BINDINGS_MIGRATIONS: MigrationMap = {};

/** Parse a profile file, saying WHY on failure — the store needs to tell a corrupt profile
 *  (replaceable) from one written by a newer build (must not be). A valid but partial/older
 *  profile parses with its missing fields defaulted. */
export function parseBindingMapResult(
  json: string,
): { ok: true; value: BindingMap } | { ok: false; reason: RootRefusal; stamped?: number } {
  const root = parseVersionedRoot(json, BINDINGS_SCHEMA, BINDINGS_MIGRATIONS);
  if (!root.ok) return root;
  return { ok: true, value: bindingMapSchema.parse(root.raw) as BindingMap };
}

/** Parse a profile file. Null when the text can't be trusted, for callers that only need the
 *  value; `parseBindingMapResult` distinguishes the failures. */
export function parseBindingMap(json: string): BindingMap | null {
  const r = parseBindingMapResult(json);
  return r.ok ? r.value : null;
}

/** Serialize a profile, stamping the current schema version (native field order, then the TS-only
 *  app-action sections). Emitting keyboardActions/gamepadActions is load-bearing: the editor re-serializes on
 *  every rebind, so omitting them would silently strip a user's customized Open Menu / Cycle bindings. */
export function serializeBindingMap(map: BindingMap): string {
  return stringifyConfig({
    schemaVersion: BINDINGS_SCHEMA,
    name: map.name,
    keyboard: map.keyboard,
    gamepad: map.gamepad,
    keyboardActions: map.keyboardActions,
    gamepadActions: map.gamepadActions,
  });
}
