// recent.json parse/serialize. The on-disk shape matches the existing native file —
// { schemaVersion, entries: [{ path, name }] } — so a user's current recent.json
// still loads when the real backend replaces the C++ one. Entries are validated with
// a zod schema (each entry defaulted/coerced; malformed ones skipped). Reads stay
// tolerant: absent / garbage / newer-than-us all yield an empty list.

import { z } from "./configSchema";
import { MAX_ENTRIES, type RecentEntry } from "./recentList";

/** On-disk schema version. Bump only on a breaking (non-additive) change; a file
 *  stamped newer than this is refused on load. Matches the native `kRecent`. */
export const RECENT_SCHEMA = 2;

// One recent entry: a non-empty path + a display alias (defaulting to "").
const recentEntrySchema = z.looseObject({
  path: z.string().min(1),
  name: z.string().catch("").default(""),
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
  const d = doc as { schemaVersion?: unknown; entries?: unknown };
  if (typeof d.schemaVersion === "number" && d.schemaVersion > RECENT_SCHEMA) return [];
  if (!Array.isArray(d.entries)) return [];

  const out: RecentEntry[] = [];
  for (const raw of d.entries) {
    const r = recentEntrySchema.safeParse(raw);
    if (r.success) {
      out.push({ path: r.data.path, name: r.data.name });
      if (out.length >= max) break;
    }
  }
  return out;
}

/** Serialize entries to recent.json text, stamping the current schema version. */
export function serializeRecent(entries: RecentEntry[]): string {
  return JSON.stringify({
    schemaVersion: RECENT_SCHEMA,
    entries: entries.map((e) => ({ path: e.path, name: e.name })),
  });
}
