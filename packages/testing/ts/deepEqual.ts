// Order-independent structural deep-equal for comparing codec output to the
// frozen C++ golden JSON. The harness's expect().toEqual is JSON.stringify-based
// (key-order sensitive); reflect-cpp and our model may order keys differently, so
// we compare structurally. Both sides are normalised through JSON first so that
// an omitted optional (undefined) and an absent key compare equal.

function walk(a: unknown, b: unknown, path: string, out: string[]): void {
  if (out.length > 30) return;
  if (a === b) return;
  if (Array.isArray(a) || Array.isArray(b)) {
    if (!Array.isArray(a) || !Array.isArray(b)) {
      out.push(`${path}: array-ness mine=${Array.isArray(a)} gold=${Array.isArray(b)}`);
      return;
    }
    if (a.length !== b.length) {
      out.push(`${path}: length mine=${a.length} gold=${b.length}`);
      return;
    }
    for (let i = 0; i < a.length; i++) walk(a[i], b[i], `${path}[${i}]`, out);
    return;
  }
  if (a && b && typeof a === "object" && typeof b === "object") {
    const ao = a as Record<string, unknown>;
    const bo = b as Record<string, unknown>;
    const keys = new Set([...Object.keys(ao), ...Object.keys(bo)]);
    for (const k of keys) {
      if (!(k in ao)) {
        out.push(`${path}.${k}: MISSING in mine (gold=${JSON.stringify(bo[k])})`);
        continue;
      }
      if (!(k in bo)) {
        out.push(`${path}.${k}: EXTRA in mine (=${JSON.stringify(ao[k])})`);
        continue;
      }
      walk(ao[k], bo[k], `${path}.${k}`, out);
    }
    return;
  }
  out.push(`${path}: mine=${JSON.stringify(a)} gold=${JSON.stringify(b)}`);
}

/** Throw with up to 30 path-anchored diffs if `actual` and `expected` differ. */
export function deepEqual(actual: unknown, expected: unknown, label = "value"): void {
  const a = JSON.parse(JSON.stringify(actual));
  const b = JSON.parse(JSON.stringify(expected));
  const out: string[] = [];
  walk(a, b, label, out);
  if (out.length) throw new Error(`${out.length} diff(s):\n    ${out.join("\n    ")}`);
}
