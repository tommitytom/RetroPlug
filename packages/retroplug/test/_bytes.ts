// Shared byte-array helpers for data-safety tests (both consoles previously inlined their own sameBytes).
// Not a *.test.ts, so the runner ignores it.

/** True when two byte arrays are identical (length + every byte). */
export function sameBytes(a: Uint8Array, b: Uint8Array): boolean {
  if (a.length !== b.length) return false;
  for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
  return true;
}

/** The first index where a and b differ (for diagnostics), or -1 if identical. A length mismatch reports
 *  the shorter length. */
export function firstDiff(a: Uint8Array, b: Uint8Array): number {
  const n = Math.min(a.length, b.length);
  for (let i = 0; i < n; i++) if (a[i] !== b[i]) return i;
  return a.length === b.length ? -1 : n;
}
