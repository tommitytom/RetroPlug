// Zod schema builders for role configs. A role's config shape, defaults, and
// clamping all live in one zod schema (the RoleType.schema) — so extensions declare
// their config once and get validation + defaults + type inference for free. These
// fields CLAMP rather than throw (a settings knob shouldn't reject a stale/overflow
// value — it snaps to the nearest bound), and default when the value is missing or
// the wrong type, so `schema.parse({})` yields a full default config.

import { z } from "zod";

/** An integer field: missing/non-number → `def`, out-of-range → nearest bound. */
export function clampedInt(min: number, max: number, def: number) {
  return z.preprocess((v) => {
    const n = typeof v === "number" && Number.isFinite(v) ? Math.trunc(v) : def;
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
