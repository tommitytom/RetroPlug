// The shared zod field builders a role config or a settings block is made of: shape, defaults and clamping in one
// schema, so a config is validated and coerced declaratively (a malformed, partial or stale value snaps to a sane one
// rather than being ad-hoc guarded). They live here because a role's schema is part of the role contract, which lives
// in this package: a console package owns its own role and builds it from these, and a package may not import the
// shell. The write-side half of the shell's config persistence (stringifyConfig) stays there - it is the app's file
// format, not a contract a pack contributes.
//
// Config object schemas are STRICT (z.object - unknown keys stripped). Forward-tolerance across config versions comes
// from field `.default()`s (an old config missing a newly-added field gets the default) plus the version-stamp /
// refuse-newer detection, NOT from passthrough: an older reader never sees a newer writer's fields (they're refused),
// and additive fields are filled by defaults. The only other writer is native C++ (a richer/different shape) - that's
// a translation concern for the real adapter, not loose passthrough. Breaking format changes bump the version and get
// an explicit raw migration at the load seam.
//
// These fields CLAMP rather than throw (a settings knob shouldn't reject an overflow value - it snaps to the nearest
// bound), and default when missing or the wrong type, so `schema.parse({})` yields a full default config.
//
// `z` itself is NOT re-exported. A package that builds a schema imports zod in its own file and declares it in its own
// manifest, which is what lets the boundary check (packages/retroplug/scripts/check-boundaries.mjs) see every zod use:
// reached through a re-export here it would be this package's dependency and nobody's refusal. Every module of a build
// resolves `zod` to the one install, so the schemas share one instance whichever file imported it from.

import { z } from "zod";

/** An integer field: missing/non-number → `def`, out-of-range → nearest bound, a fraction → the nearest whole.
 *  A fraction means nothing for any of these fields (a level, an index, a duration, a percentage of unity), and for
 *  `expansionVolume`, the one that reaches the engine as an int param, rounding is also what `ParamType::Int` does.
 *  Where 0 is a sentinel (`zoom` 0 inherits the user default, a controller's `systemId` 0 is the first system, an
 *  ungrouped `linkGroupId` 0), a hand-edited fraction in [0.5, 1) rounds up to 1, the first real value; such a value
 *  is a hand edit the integer contract does not allow. */
export function clampedInt(min: number, max: number, def: number) {
  return z.preprocess((v) => {
    const n = typeof v === "number" && Number.isFinite(v) ? Math.round(v) : def;
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

/** A string-enum field: value must be one of `values`; missing/unknown → `def` (same tolerance as
 *  clampedInt's clamp - a stale/garbage enum snaps to the default rather than failing the parse). */
export function enumField<T extends readonly [string, ...string[]]>(values: T, def: T[number]) {
  return z.enum(values).catch(def).default(def);
}
