// RecentStore: the recent-projects list as the app uses it. Ties the pure list
// logic + serialization to the Backend - canonicalizes incoming paths (half the
// dedupe key, the song being the other half), computes each entry's `missing` flag
// from the live filesystem, persists to <configDir>/recent.json atomically, and fires
// onChange so the UI can re-render. All mutations are no-ops (no write, no notify)
// when they don't actually change the list - which is what lets the song watcher call
// `add` on a timer.

import type { HostBackend } from "./backend";
import { addEntry, removeEntry, relinkEntry, entryKey, label, type RecentEntry } from "./recentList";
import { parseRecent, serializeRecent } from "./recentSerialization";

const RECENT_FILE = "recent.json";
const enc = new TextEncoder();
const dec = new TextDecoder();

/** A recent entry as the UI sees it: canonical path, recorded name, whether the
 *  file still exists, and the resolved display label. */
export interface RecentView {
  path: string;
  name: string;
  missing: boolean;
  label: string;
  /** The project's working-song name at the time it was recorded, if known (a tracker cart's loaded song). */
  song?: string;
}

export class RecentStore {
  private entries: RecentEntry[] = [];

  constructor(private readonly backend: HostBackend, private readonly onChange: () => void = () => {}) {}

  /** Read recent.json into memory. Safe to call once at startup; a missing or
   *  unreadable file leaves the list empty. */
  load(): void {
    const bytes = this.backend.readFile(this.filePath());
    this.entries = bytes ? parseRecent(dec.decode(bytes)) : [];
  }

  /** Snapshot for the UI (most-recent-first), with live `missing` flags + labels. */
  view(): RecentView[] {
    return this.entries.map((e) => ({
      path: e.path,
      name: e.name,
      missing: !this.backend.fileExists(e.path),
      label: label(e),
      song: e.song,
    }));
  }

  /** Track the (`path`, `song`) row at the front - one row per song a project has had loaded. `name` is
   *  the project's display name (empty keeps that row's existing one). Returns whether the list changed
   *  (false when that row was already at the front unchanged - no write, no notify). */
  add(path: string, name = "", song?: string): boolean {
    if (!path) return false;
    return this.commit(addEntry(this.entries, this.canon(path), name, song));
  }

  /** Drop the (`path`, `song`) row, leaving that project's other songs. Returns false when absent. */
  remove(path: string, song?: string): boolean {
    const p = this.canon(path);
    if (!this.hasKey(entryKey(p, song))) return false;
    return this.commit(removeEntry(this.entries, p, song));
  }

  /** Repoint a moved project from `oldPath` to `newPath` - every row of that project. Returns false when
   *  `oldPath` wasn't present. */
  relink(oldPath: string, newPath: string): boolean {
    const op = this.canon(oldPath);
    if (!this.hasPath(op)) return false;
    this.commit(relinkEntry(this.entries, op, this.canon(newPath)));
    return true;
  }

  private hasKey(key: string): boolean {
    return this.entries.some((e) => entryKey(e.path, e.song) === key);
  }

  private hasPath(canonPath: string): boolean {
    return this.entries.some((e) => e.path === canonPath);
  }

  private canon(path: string): string {
    return this.backend.canonicalize(path);
  }

  private filePath(): string {
    return `${this.backend.configDir()}/${RECENT_FILE}`;
  }

  // Adopt `next` if it differs from the current list: persist atomically + notify.
  private commit(next: RecentEntry[]): boolean {
    const after = serializeRecent(next);
    if (after === serializeRecent(this.entries)) return false; // genuine no-op
    this.entries = next;
    this.backend.writeFileAtomic(this.filePath(), enc.encode(after));
    this.onChange();
    return true;
  }
}
