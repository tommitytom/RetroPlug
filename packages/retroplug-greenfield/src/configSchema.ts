// Shared zod schema builders for the greenfield config surface — role configs,
// per-system settings, project files, recent files. A config's shape, defaults, and
// clamping all live in one zod schema, so every user config is validated + coerced
// declaratively (a malformed/partial/stale value snaps to a sane one rather than
// being ad-hoc guarded).
//
// Config object schemas are STRICT (z.object — unknown keys stripped). Forward-
// tolerance across greenfield versions comes from field `.default()`s (an old config
// missing a newly-added field gets the default) plus the version-stamp/refuse-newer
// detection, NOT from passthrough: an older reader never sees a newer writer's fields
// (they're refused), and additive fields are filled by defaults. The only other
// writer is native C++ (a richer/different shape) — that's a translation concern for
// the real adapter, not loose passthrough. Breaking format changes bump the version
// and get an explicit raw migration at the load seam.
//
// These fields CLAMP rather than throw (a settings knob shouldn't reject an overflow
// value — it snaps to the nearest bound), and default when missing or the wrong type,
// so `schema.parse({})` yields a full default config.

import { z } from "zod";

/** An integer field: missing/non-number → `def`, out-of-range → nearest bound. */
export function clampedInt(min: number, max: number, def: number) {
  return z.preprocess((v) => {
    const n = typeof v === "number" && Number.isFinite(v) ? Math.trunc(v) : def;
    return Math.max(min, Math.min(max, n));
  }, z.number());
}

/** A (non-integer) number field: missing/non-number → `def`, out-of-range → bound. */
export function clampedNumber(min: number, max: number, def: number) {
  return z.preprocess((v) => {
    const n = typeof v === "number" && Number.isFinite(v) ? v : def;
    return Math.max(min, Math.min(max, n));
  }, z.number());
}

/** A boolean field: non-boolean → `def`. */
export function boolField(def: boolean) {
  return z.preprocess((v) => (typeof v === "boolean" ? v : def), z.boolean());
}

/** A string field: non-string → `def`. */
export function stringField(def: string) {
  return z.preprocess((v) => (typeof v === "string" ? v : def), z.string());
}

export { z };
