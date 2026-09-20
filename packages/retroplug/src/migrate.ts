// Raw-JSON migration framework, shared by every versioned config root (project, user
// config, bindings, recent). TS owns the config format; on a breaking (non-additive)
// change we bump the root's schema-version constant and add ONE raw `(obj) => obj` step
// that upgrades the previous version to the new one. On load we apply the ordered chain
// from the file's stamped version up to the current one, on the RAW object, BEFORE the
// (single, latest) zod schema validates it. We keep only the latest schema — never a
// copy per version — so migrations, not stale schemas, carry old files forward.
//
// The refuse-newer guard (a file stamped ahead of this build) lives here too, in
// parseVersionedRoot below — it used to be copied into each root's parser, which is how the
// three of them ended up unable to tell "from the future" from "corrupt".
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

/** Why a versioned root could not be read.
 *
 *  The distinction is not cosmetic: it decides whether the file may be WRITTEN. A malformed
 *  root is junk we are entitled to replace — and do, so a corrupted file heals on the next
 *  change. A root stamped ahead of this build is the opposite: it is somebody's real data in a
 *  format we do not understand yet, and this build overwriting it with its own defaults is how
 *  a downgrade (or a second machine on an older version) silently destroys settings. */
export type RootRefusal = "malformed" | "newer";

/** The outcome of reading a versioned JSON root, before its own schema validates the shape.
 *  `stamped` is the version the file declared, present only for a "newer" refusal (a malformed
 *  root may not have a readable stamp at all) — the stores put it in the warning they log. */
export type RootParse =
  | { ok: true; raw: RawObject }
  | { ok: false; reason: RootRefusal; stamped?: number };

/** Parse + migrate one versioned JSON root: JSON.parse, require an object, refuse a stamp newer
 *  than `current`, then apply the migration chain. The caller runs its own (latest) zod schema on
 *  `raw` — the shared part ends here, since each root's tail differs (a whole-object parse for
 *  config/bindings, a per-entry one for recent). */
export function parseVersionedRoot(json: string, current: number, migrations: MigrationMap): RootParse {
  let doc: unknown;
  try {
    doc = JSON.parse(json);
  } catch {
    return { ok: false, reason: "malformed" };
  }
  if (!doc || typeof doc !== "object" || Array.isArray(doc)) return { ok: false, reason: "malformed" };
  const raw = doc as RawObject;
  if (typeof raw.schemaVersion === "number" && raw.schemaVersion > current)
    return { ok: false, reason: "newer", stamped: raw.schemaVersion };
  return { ok: true, raw: migrateRaw(raw, readNumericVersion(raw, current), current, migrations) };
}

/** One line, on the first refusal only, naming the file and both versions. Without it a
 *  read-only store is indistinguishable from a broken one: the symptom a user sees is that their
 *  settings silently stop sticking, with nothing anywhere to say why. */
export function warnStampedNewer(file: string, stamped: number | undefined, current: number): void {
  console.warn(
    `[retroplug] ${file} is stamped schemaVersion ${stamped ?? "?"}, this build reads ${current}. ` +
      `Keeping it read-only so a newer version's settings are not overwritten; changes will not be saved.`,
  );
}
