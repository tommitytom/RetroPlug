// recent.json parse/serialize. The on-disk shape matches the existing native file —
// { schemaVersion, entries: [{ path, name }] } — so a user's current recent.json
// still loads when the real backend replaces the C++ one. Entries are validated with
// a zod schema (each entry defaulted/coerced; malformed ones skipped). Reads stay
// tolerant: absent / garbage / newer-than-us all yield an empty list.

import { z, stringifyConfig } from "./configSchema";
import { parseVersionedRoot, type MigrationMap, type RootRefusal } from "./migrate";
import { MAX_ENTRIES, type RecentEntry } from "./recentList";

/** On-disk schema version. Bump only on a breaking (non-additive) change; a file
 *  stamped newer than this is refused on load, one stamped older is migrated (below). */
export const RECENT_SCHEMA = 2;

/** Raw-JSON migrations keyed by from-version (see migrate.ts). Empty — the 1→2 bump was
 *  additive; the seam is here so the first breaking one is a one-line add. */
const RECENT_MIGRATIONS: MigrationMap = {};

// One recent entry: a non-empty path + a display name (defaulting to "") + an optional working-song label
// (additive since the 1→2 schema, so old files without it still load — no migration step needed).
//
// A song name is printable ASCII - every tracker this app knows writes names that way - so a row whose
// song is anything else is not a song row but the residue of one recorded from a cart that had not
// finished booting (rows of box glyphs, in Recent, from bytes that were never a name). Such a row is
// SKIPPED like any other malformed entry, which is also what cleans an already-corrupted recent.json:
// the next change to the list is written without it.
const recentEntrySchema = z.object({
  path: z.string().min(1),
  name: z.string().catch("").default(""),
  song: z.string().regex(/^[\x20-\x7e]*$/).optional(),
});

/** Parse recent.json text, saying WHY on failure. The store needs the reason: a corrupt list is
 *  rewritten clean on the next change, but one stamped newer must be left alone. Individual
 *  malformed ENTRIES are still skipped rather than refusing the file — that is the healing case,
 *  not a refusal. */
export function parseRecentResult(
  json: string,
  max = MAX_ENTRIES,
): { ok: true; entries: RecentEntry[] } | { ok: false; reason: RootRefusal; stamped?: number } {
  const root = parseVersionedRoot(json, RECENT_SCHEMA, RECENT_MIGRATIONS);
  if (!root.ok) return root;
  const entries = (root.raw as { entries?: unknown }).entries;
  if (!Array.isArray(entries)) return { ok: false, reason: "malformed" };
  return { ok: true, entries: collectEntries(entries, max) };
}

/** Parse recent.json text into entries, capped to `max`. Never throws; [] on any refusal, for
 *  callers that only need the list — `parseRecentResult` distinguishes the failures. */
export function parseRecent(json: string, max = MAX_ENTRIES): RecentEntry[] {
  const r = parseRecentResult(json, max);
  return r.ok ? r.entries : [];
}

function collectEntries(entries: unknown[], max: number): RecentEntry[] {
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
  return stringifyConfig({
    schemaVersion: RECENT_SCHEMA,
    entries: entries.map((e) => (e.song !== undefined ? { path: e.path, name: e.name, song: e.song } : { path: e.path, name: e.name })),
  });
}
