// Pure recent-list logic: a most-recent-first list of project entries keyed by
// (already-canonicalized) path. No backend, no IO — just immutable transforms
// over plain arrays, so every rule is unit-testable in isolation. The store
// (recentStore.ts) canonicalizes paths and handles persistence on top of these.

export interface RecentEntry {
  /** Canonical path — the dedupe key. Canonicalization happens in the store. */
  path: string;
  /** Display alias; empty means derive the label from the path basename. */
  name: string;
  /** The project's working-song name at open/save time (a tracker cart's loaded song), if known. Shown
   *  alongside the label. Resolved by the tracker layer (src/tracker) when the entry is recorded. */
  song?: string;
}

export const MAX_ENTRIES = 10;

/** Prepend `path` (dedup by path, moving an existing entry to the front) and cap
 *  to `max`. An empty `name` preserves any existing entry's alias, so a rename
 *  survives a re-add; a non-empty `name` overrides it. `song` sets the working-song
 *  label; `undefined` preserves any existing one. */
export function addEntry(list: RecentEntry[], path: string, name: string, song?: string, max = MAX_ENTRIES): RecentEntry[] {
  const existing = list.find((e) => e.path === path);
  const keepName = name || existing?.name || "";
  const keepSong = song !== undefined ? song : existing?.song;
  const entry: RecentEntry = keepSong !== undefined ? { path, name: keepName, song: keepSong } : { path, name: keepName };
  return [entry, ...list.filter((e) => e.path !== path)].slice(0, max);
}

/** Drop the entry with `path`. */
export function removeEntry(list: RecentEntry[], path: string): RecentEntry[] {
  return list.filter((e) => e.path !== path);
}

/** Set the alias of the entry at `path` (empty clears it). Keeps its song label. */
export function renameEntry(list: RecentEntry[], path: string, name: string): RecentEntry[] {
  return list.map((e) => (e.path === path ? { ...e, name } : e));
}

/** Repoint `oldPath` to `newPath` in place (keeping its position + alias), then
 *  drop any other entry that now collides with `newPath` (the relinked one
 *  wins). A missing `oldPath` leaves the list unchanged. */
export function relinkEntry(list: RecentEntry[], oldPath: string, newPath: string): RecentEntry[] {
  const idx = list.findIndex((e) => e.path === oldPath);
  if (idx < 0) return list;
  const out: RecentEntry[] = [];
  list.forEach((e, i) => {
    if (i === idx) out.push({ ...e, path: newPath }); // keep alias + song
    else if (e.path !== newPath) out.push(e); // drop a pre-existing collision
  });
  return out;
}

/** The label to show: the alias if set, else the path's basename with the project extension stripped
 *  (so an alias-less entry reads `game`, not `game.rplg`). */
export function label(entry: RecentEntry): string {
  return entry.name.trim() || stripProjectExt(basename(entry.path));
}

function basename(p: string): string {
  return p.split(/[\\/]/).pop() ?? p;
}

// Drop a trailing `.rplg` or `.rplg.zip` from a project filename (case-insensitive), leaving other names alone.
function stripProjectExt(name: string): string {
  return name.replace(/\.rplg(\.zip)?$/i, "");
}
