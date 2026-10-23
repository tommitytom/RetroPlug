// The one JSON spelling descriptors, layouts and settings comparisons use, so a TS node and a C++ node of
// the same shape produce the same bytes. Its C++ twin is packages/native/src/graph/CanonicalJson.cpp:
// object keys sorted (JavaScript's default order, by UTF-16 code unit, which the C++ side reproduces), no
// whitespace, strings and numbers exactly as JSON.stringify spells them, undefined object members dropped
// and undefined array elements (holes included) written as null. Both are held to the same golden tables
// (packages/native/test/graph/golden/, from gen-canonical-cases.mjs), which keeps them in step.
//
// For plain JSON data: toJSON methods and function-valued members are not given JSON.stringify's
// treatment, because descriptors and settings never contain them.
export function canonicalJson(value: unknown): string {
  if (Array.isArray(value)) {
    const parts: string[] = [];
    for (let i = 0; i < value.length; ++i) parts.push(canonicalJson(value[i]));
    return "[" + parts.join(",") + "]";
  }
  if (value !== null && typeof value === "object") {
    const o = value as Record<string, unknown>;
    const keys = Object.keys(o).filter((k) => o[k] !== undefined).sort();
    return "{" + keys.map((k) => JSON.stringify(k) + ":" + canonicalJson(o[k])).join(",") + "}";
  }
  if (typeof value === "number" && !Number.isFinite(value)) return "null";
  return JSON.stringify(value) ?? "null";
}
