// The raw-JSON migration framework (migrate.ts): ordered application from the file's
// stamped version up to current, identity when already-current or a step is absent.
import { test, expect } from "../../testing/harness";
import { migrateRaw, readNumericVersion, type MigrationMap } from "../../src/migrate";

const STEPS: MigrationMap = {
  1: (o) => ({ ...o, a: 1 }), // v1 -> v2
  2: (o) => ({ ...o, b: 2 }), // v2 -> v3
  // no [3] — an additive v3 -> v4 bump with no transform
};

test("applies migrations[from..latest-1] in order", () => {
  expect(migrateRaw({ v: "x" }, 1, 3, STEPS)).toEqual({ v: "x", a: 1, b: 2 });
});

test("starts from the file's version (skips already-applied steps)", () => {
  expect(migrateRaw({ v: "x" }, 2, 3, STEPS)).toEqual({ v: "x", b: 2 }); // only 2->3 runs
});

test("identity when already current (from >= latest)", () => {
  const obj = { v: "x" };
  expect(migrateRaw(obj, 3, 3, STEPS)).toBe(obj); // same reference, untouched
});

test("an absent step is a no-op (additive bump)", () => {
  expect(migrateRaw({ v: "x" }, 3, 4, STEPS)).toEqual({ v: "x" }); // migrations[3] absent
});

test("runs the full chain from an unstamped-old baseline", () => {
  expect(migrateRaw({}, 1, 3, STEPS)).toEqual({ a: 1, b: 2 });
});

test("readNumericVersion reads a numeric stamp, floors to current otherwise", () => {
  expect(readNumericVersion({ schemaVersion: 2 }, 5)).toBe(2);
  expect(readNumericVersion({}, 5)).toBe(5); // absent -> current
  expect(readNumericVersion({ schemaVersion: "3" }, 5)).toBe(5); // non-number -> current
  expect(readNumericVersion({ schemaVersion: NaN }, 5)).toBe(5); // non-finite -> current
});
