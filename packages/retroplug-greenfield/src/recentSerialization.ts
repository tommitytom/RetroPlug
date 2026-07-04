// recent.json parse/serialize. The on-disk shape matches the existing native
// file — { schemaVersion, entries: [{ path, name }] } — so a user's current
// recent.json still loads when the real backend replaces the C++ one. Reads are
// tolerant: absent / garbage / newer-than-us all yield an empty list.

import { MAX_ENTRIES, type RecentEntry } from "./recentList";

/** On-disk schema version. Bump only on a breaking (non-additive) change; a file
 *  stamped newer than this is refused on load. Matches the native `kRecent`. */
export const RECENT_SCHEMA = 2;

interface RecentFilesJson {
  schemaVersion?: number;
  entries?: unknown;
}

/** Parse recent.json text into entries, capped to `max`. Never throws:
 *  malformed JSON, a non-object root, or a newer schema stamp all return []. */
export function parseRecent(json: string, max = MAX_ENTRIES): RecentEntry[] {
  let doc: RecentFilesJson;
  try {
    doc = JSON.parse(json) as RecentFilesJson;
  } catch {
    return [];
  }
  if (!doc || typeof doc !== "object" || Array.isArray(doc)) return [];
  if (typeof doc.schemaVersion === "number" && doc.schemaVersion > RECENT_SCHEMA) return [];
  if (!Array.isArray(doc.entries)) return [];

  const out: RecentEntry[] = [];
  for (const raw of doc.entries) {
    if (!raw || typeof raw !== "object") continue;
    const path = (raw as { path?: unknown }).path;
    if (typeof path !== "string" || path === "") continue;
    const name = (raw as { name?: unknown }).name;
    out.push({ path, name: typeof name === "string" ? name : "" });
    if (out.length >= max) break;
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
