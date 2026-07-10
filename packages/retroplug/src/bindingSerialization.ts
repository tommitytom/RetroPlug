// bindings/<name>.json parse/serialize. The on-disk shape matches native's BindingMapJson
// — { schemaVersion, name, keyboard, gamepad } — so a user's current profiles still load
// when the real backend replaces the C++ one. Reads stay tolerant: a missing field takes
// its default (additive), an unknown one is stripped, a bad channel becomes {}; malformed
// / non-object / newer-than-us yield null (the caller keeps its current value).

import { bindingMapSchema, type BindingMap } from "./bindingMap";

/** On-disk schema version. Bump only on a breaking (non-additive) change; a file stamped
 *  newer than this is refused on load. Matches the native `kBindings`. */
export const BINDINGS_SCHEMA = 1;

/** Parse a profile file. Returns null when the text can't be trusted (malformed JSON, a
 *  non-object root, or a newer schema stamp) — the caller retains its current value. A
 *  valid but partial/older profile parses with its missing fields defaulted. */
export function parseBindingMap(json: string): BindingMap | null {
  let doc: unknown;
  try {
    doc = JSON.parse(json);
  } catch {
    return null;
  }
  if (!doc || typeof doc !== "object" || Array.isArray(doc)) return null;
  const d = doc as { schemaVersion?: unknown };
  if (typeof d.schemaVersion === "number" && d.schemaVersion > BINDINGS_SCHEMA) return null;
  return bindingMapSchema.parse(doc) as BindingMap;
}

/** Serialize a profile, stamping the current schema version (native field order, then the greenfield-only
 *  app-action sections). Emitting keyboardActions/gamepadActions is load-bearing: the editor re-serializes on
 *  every rebind, so omitting them would silently strip a user's customized Open Menu / Cycle bindings. */
export function serializeBindingMap(map: BindingMap): string {
  return JSON.stringify({
    schemaVersion: BINDINGS_SCHEMA,
    name: map.name,
    keyboard: map.keyboard,
    gamepad: map.gamepad,
    keyboardActions: map.keyboardActions,
    gamepadActions: map.gamepadActions,
  });
}
