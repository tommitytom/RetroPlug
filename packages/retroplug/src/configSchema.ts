// The shell's config write side: the one formatter every JSON root RetroPlug writes goes through, so the files stay
// formatted the same way. The read side (the zod field builders a config's shape, defaults and clamping are made of) is
// @retroplug/core's, next to the role contract whose schemas they build - a console package owns a role and writes its
// schema with them, and a package may not import this module.
//
// Parsers ignore whitespace, so a file written by an older compact build still loads unchanged. RPC payloads (role
// config, the DSP system struct, a render spec) are NOT files and stay compact.

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
 *  `project.json` inside an exported zip / the plugin's DPF state chunk). */
export function stringifyConfig(value: unknown): string {
  return formatJson(value, "") + "\n";
}
