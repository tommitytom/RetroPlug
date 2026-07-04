// Shared zod schema builders for the greenfield config surface — role configs,
// per-system settings, project files, recent files. A config's shape, defaults, and
// clamping all live in one zod schema, so every user config is validated + coerced
// declaratively (a malformed/partial/stale value snaps to a sane one rather than
// being ad-hoc guarded). Config object schemas use `z.looseObject` so unknown fields
// are PRESERVED (forward-tolerance — a native config's richer fields survive a
// greenfield load→save round-trip instead of being stripped).
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
