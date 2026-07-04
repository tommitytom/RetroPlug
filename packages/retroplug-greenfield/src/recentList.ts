// Pure recent-list logic: a most-recent-first list of project entries keyed by
// (already-canonicalized) path. No backend, no IO — just immutable transforms
// over plain arrays, so every rule is unit-testable in isolation. The store
// (recentStore.ts) canonicalizes paths and handles persistence on top of these.

export interface RecentEntry {
  /** Canonical path — the dedupe key. Canonicalization happens in the store. */
  path: string;
  /** Display alias; empty means derive the label from the path basename. */
  name: string;
}

export const MAX_ENTRIES = 10;

/** Prepend `path` (dedup by path, moving an existing entry to the front) and cap
 *  to `max`. An empty `name` preserves any existing entry's alias, so a rename
 *  survives a re-add; a non-empty `name` overrides it. */
export function addEntry(list: RecentEntry[], path: string, name: string, max = MAX_ENTRIES): RecentEntry[] {
  const existing = list.find((e) => e.path === path);
  const keepName = name || existing?.name || "";
  return [{ path, name: keepName }, ...list.filter((e) => e.path !== path)].slice(0, max);
}

/** Drop the entry with `path`. */
export function removeEntry(list: RecentEntry[], path: string): RecentEntry[] {
  return list.filter((e) => e.path !== path);
}

/** Set the alias of the entry at `path` (empty clears it). */
export function renameEntry(list: RecentEntry[], path: string, name: string): RecentEntry[] {
  return list.map((e) => (e.path === path ? { path: e.path, name } : e));
}

/** Repoint `oldPath` to `newPath` in place (keeping its position + alias), then
 *  drop any other entry that now collides with `newPath` (the relinked one
 *  wins). A missing `oldPath` leaves the list unchanged. */
export function relinkEntry(list: RecentEntry[], oldPath: string, newPath: string): RecentEntry[] {
  const idx = list.findIndex((e) => e.path === oldPath);
  if (idx < 0) return list;
  const out: RecentEntry[] = [];
  list.forEach((e, i) => {
    if (i === idx) out.push({ path: newPath, name: e.name });
    else if (e.path !== newPath) out.push(e); // drop a pre-existing collision
  });
  return out;
}

/** The label to show: the alias if set, else the path's basename. */
export function label(entry: RecentEntry): string {
  return entry.name.trim() || basename(entry.path);
}

function basename(p: string): string {
  return p.split(/[\\/]/).pop() ?? p;
}
