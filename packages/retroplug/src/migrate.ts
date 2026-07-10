// Raw-JSON migration framework, shared by every versioned config root (project, user
// config, bindings, recent). TS owns the config format; on a breaking (non-additive)
// change we bump the root's schema-version constant and add ONE raw `(obj) => obj` step
// that upgrades the previous version to the new one. On load we apply the ordered chain
// from the file's stamped version up to the current one, on the RAW object, BEFORE the
// (single, latest) zod schema validates it. We keep only the latest schema — never a
// copy per version — so migrations, not stale schemas, carry old files forward.
//
// The refuse-newer guard (a file stamped ahead of this build) is separate and stays at
// each root's load seam; this module only handles Older → current.
//
// INVARIANT: migrations must be idempotent-safe (guard with `??=` / presence checks).
// A file whose stamp is absent/garbage is treated as current (no migration), matching
// the version-floor convention — so a step may run against an already-current object
// and must no-op rather than corrupt it.

export type RawObject = Record<string, unknown>;

/** A single raw-JSON upgrade: transform a config stamped version N into version N+1. */
export type RawMigration = (raw: RawObject) => RawObject;

/** Migrations keyed by FROM-version: `migrations[v]` upgrades a v-stamped object to v+1. */
export type MigrationMap = Record<number, RawMigration>;

/** Apply `migrations[fromVersion] … migrations[latest-1]` in order to `raw`. A no-op when
 *  `fromVersion >= latest` (already current) or a step is absent (an additive bump with no
 *  transform). Returns the upgraded raw object; the caller then runs the latest zod schema. */
export function migrateRaw(
  raw: RawObject,
  fromVersion: number,
  latest: number,
  migrations: MigrationMap,
): RawObject {
  let obj = raw;
  for (let v = fromVersion; v < latest; v++) {
    const step = migrations[v];
    if (step) obj = step(obj);
  }
  return obj;
}

/** Read a numeric `schemaVersion` stamp from a raw root; when absent or non-numeric, floor
 *  to `current` (an unstamped file is assumed current-shaped, never spuriously "older").
 *  For the project root the stamp is a string — use `parseProjectVersion` there instead. */
export function readNumericVersion(raw: RawObject, current: number): number {
  const v = raw.schemaVersion;
  return typeof v === "number" && Number.isFinite(v) ? v : current;
}
