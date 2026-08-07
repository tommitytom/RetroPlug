// Shared zod schema builders for the config surface — role configs,
// per-system settings, project files, recent files. A config's shape, defaults, and
// clamping all live in one zod schema, so every user config is validated + coerced
// declaratively (a malformed/partial/stale value snaps to a sane one rather than
// being ad-hoc guarded).
//
// Config object schemas are STRICT (z.object — unknown keys stripped). Forward-
// tolerance across config versions comes from field `.default()`s (an old config
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
//
// The read side is zod; the one write-side helper lives here too (stringifyConfig), so every JSON root
// RetroPlug writes is formatted the same way.

import { z } from "zod";

/** A primitive-only array shorter than this (including its indent) stays on one line. Plain
 *  `JSON.stringify(v, null, 2)` puts every element on its own line, which turns a bindings profile —
 *  mostly one- and two-element key-name lists — into 80 lines of noise. */
const INLINE_ARRAY_MAX = 100;

const isPrimitive = (v: unknown): boolean => v === null || typeof v !== "object";

/** 2-space-indented JSON, except an array of primitives short enough to fit stays inline
 *  (`"A": ["Z", "z"]`). Objects and arrays-of-objects always expand. Undefined-valued keys are dropped,
 *  as `JSON.stringify` does. */
function formatJson(value: unknown, indent: string): string {
  const inner = indent + "  ";
  if (Array.isArray(value)) {
    if (!value.length) return "[]";
    if (value.every(isPrimitive)) {
      const line = `[${value.map((v) => JSON.stringify(v) ?? "null").join(", ")}]`;
      if (indent.length + line.length <= INLINE_ARRAY_MAX) return line;
    }
    return `[\n${value.map((v) => inner + formatJson(v, inner)).join(",\n")}\n${indent}]`;
  }
  if (value && typeof value === "object") {
    const entries = Object.entries(value as Record<string, unknown>).filter(([, v]) => v !== undefined);
    if (!entries.length) return "{}";
    return `{\n${entries.map(([k, v]) => `${inner}${JSON.stringify(k)}: ${formatJson(v, inner)}`).join(",\n")}\n${indent}}`;
  }
  return JSON.stringify(value) ?? "null";
}

/** Serialize a config / project root for writing: pretty-printed with a trailing newline, so the files
 *  RetroPlug writes stay readable, hand-editable and diffable. EVERY on-disk JSON root goes through this —
 *  config.json, bindings/<name>.json, recent.json, and the project config (the thin `.rplg` plus the
 *  `project.json` inside an exported zip / the plugin's DPF state chunk). Parsers ignore the whitespace, so
 *  a file written by an older compact build still loads unchanged.
 *
 *  RPC payloads (role config, the DSP system struct, a render spec) are NOT files and stay compact. */
export function stringifyConfig(value: unknown): string {
  return formatJson(value, "") + "\n";
}

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

/** A string-enum field: value must be one of `values`; missing/unknown → `def` (same tolerance as
 *  clampedInt's clamp — a stale/garbage enum snaps to the default rather than failing the parse). */
export function enumField<T extends readonly [string, ...string[]]>(values: T, def: T[number]) {
  return z.enum(values).catch(def).default(def);
}

export { z };
