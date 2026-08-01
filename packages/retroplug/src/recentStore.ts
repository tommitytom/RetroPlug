// RecentStore: the recent-projects list as the app uses it. Ties the pure list
// logic + serialization to the Backend — canonicalizes incoming paths (the
// dedupe key), computes each entry's `missing` flag from the live filesystem,
// persists to <configDir>/recent.json atomically, and fires onChange so the UI
// can re-render. All mutations are no-ops (no write, no notify) when they don't
// actually change the list.

import type { HostBackend } from "./backend";
import { addEntry, removeEntry, relinkEntry, label, type RecentEntry } from "./recentList";
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

  /** Track `path` at the front. `name` is the project's display name (empty keeps any existing one);
   *  `song` sets the working-song label (undefined keeps any existing one). Returns whether the list
   *  changed. */
  add(path: string, name = "", song?: string): boolean {
    if (!path) return false;
    return this.commit(addEntry(this.entries, this.canon(path), name, song));
  }

  /** Drop `path`. Returns false when it wasn't present. */
  remove(path: string): boolean {
    const p = this.canon(path);
    if (!this.has(p)) return false;
    return this.commit(removeEntry(this.entries, p));
  }

  /** Repoint a moved project from `oldPath` to `newPath`. Returns false when
   *  `oldPath` wasn't present. */
  relink(oldPath: string, newPath: string): boolean {
    const op = this.canon(oldPath);
    if (!this.has(op)) return false;
    this.commit(relinkEntry(this.entries, op, this.canon(newPath)));
    return true;
  }

  private has(canonPath: string): boolean {
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
