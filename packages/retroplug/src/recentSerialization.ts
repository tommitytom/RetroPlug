// recent.json parse/serialize. The on-disk shape matches the existing native file —
// { schemaVersion, entries: [{ path, name }] } — so a user's current recent.json
// still loads when the real backend replaces the C++ one. Entries are validated with
// a zod schema (each entry defaulted/coerced; malformed ones skipped). Reads stay
// tolerant: absent / garbage / newer-than-us all yield an empty list.

import { z } from "./configSchema";
import { migrateRaw, readNumericVersion, type MigrationMap, type RawObject } from "./migrate";
import { MAX_ENTRIES, type RecentEntry } from "./recentList";

/** On-disk schema version. Bump only on a breaking (non-additive) change; a file
 *  stamped newer than this is refused on load, one stamped older is migrated (below). */
export const RECENT_SCHEMA = 2;

/** Raw-JSON migrations keyed by from-version (see migrate.ts). Empty — the 1→2 bump was
 *  additive; the seam is here so the first breaking one is a one-line add. */
const RECENT_MIGRATIONS: MigrationMap = {};

// One recent entry: a non-empty path + a display alias (defaulting to "") + an optional working-song label
// (additive since the 1→2 schema, so old files without it still load — no migration step needed).
const recentEntrySchema = z.object({
  path: z.string().min(1),
  name: z.string().catch("").default(""),
  song: z.string().optional(),
});

/** Parse recent.json text into entries, capped to `max`. Never throws: malformed
 *  JSON, a non-object root, or a newer schema stamp all return []; a malformed entry
 *  is skipped rather than failing the whole list. */
export function parseRecent(json: string, max = MAX_ENTRIES): RecentEntry[] {
  let doc: unknown;
  try {
    doc = JSON.parse(json);
  } catch {
    return [];
  }
  if (!doc || typeof doc !== "object" || Array.isArray(doc)) return [];
  const rawRoot = doc as RawObject;
  if (typeof rawRoot.schemaVersion === "number" && rawRoot.schemaVersion > RECENT_SCHEMA) return [];
  const migrated = migrateRaw(rawRoot, readNumericVersion(rawRoot, RECENT_SCHEMA), RECENT_SCHEMA, RECENT_MIGRATIONS);
  const entries = (migrated as { entries?: unknown }).entries;
  if (!Array.isArray(entries)) return [];

  const out: RecentEntry[] = [];
  for (const raw of entries) {
    const r = recentEntrySchema.safeParse(raw);
    if (r.success) {
      out.push(r.data.song !== undefined ? { path: r.data.path, name: r.data.name, song: r.data.song } : { path: r.data.path, name: r.data.name });
      if (out.length >= max) break;
    }
  }
  return out;
}

/** Serialize entries to recent.json text, stamping the current schema version. */
export function serializeRecent(entries: RecentEntry[]): string {
  return JSON.stringify({
    schemaVersion: RECENT_SCHEMA,
    entries: entries.map((e) => (e.song !== undefined ? { path: e.path, name: e.name, song: e.song } : { path: e.path, name: e.name })),
  });
}
