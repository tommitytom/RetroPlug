// ProjectStore: the top-level project state and source of truth. It owns the systems
// store (wiring its onChange to mark the project dirty) + the project settings +
// currentPath + dirty, and drives new / save / load with the missing-files scan +
// relink. TS produces AND consumes the thin config JSON — a fresh project round-trips
// faithfully (native restores omitted rich fields via DefaultIfMissing).
//
// Thin only: a thin .rplg is raw JSON, so save = build config → writeFile, and load =
// readFile → parse → toAbsolute → scan/relink → reconstruct each system from its path
// (systems.adopt). No zip, no blobs, no new native. A "PK" (zip/export) file is
// refused until the export follow-on lands.

import type { Backend } from "./backend";
import { SystemsStore } from "./systemsStore";
import type { RecentStore } from "./recentStore";
import { dirname } from "./pathUtil";
import {
  type ProjectConfig,
  type ProjectSettings,
  DEFAULT_SETTINGS,
  K_PROJECT,
  VersionCheck,
  checkVersion,
  parseProjectVersion,
  buildConfig,
  serializeConfig,
  parseConfig,
  toAbsolute,
} from "./projectConfig";
import { scanMissingFiles, autoFindSiblings, relinkInConfig, type MissingFile } from "./projectMissing";

export type LoadOutcome =
  | { kind: "loaded"; systems: number }
  | { kind: "incompatible" } // schema stamped newer than this build
  | { kind: "missing"; missing: MissingFile[] } // needs relink before it can apply
  | { kind: "error" }; // unreadable / not a thin project (e.g. a zip)

const enc = new TextEncoder();
const dec = new TextDecoder();
const NO_BLOBS: ReadonlySet<string> = new Set(); // a thin load embeds nothing

// Inclusive upper bounds for the settings enums (native validates + rejects above).
const SETTING_MAX = { layout: 3, midiRouting: 3, audioRouting: 2, zoom: 6 };

export class ProjectStore {
  readonly systems: SystemsStore;
  private projectSettings: ProjectSettings = { ...DEFAULT_SETTINGS };
  private path = "";
  private dirty = false;
  private pendingLoad: { cfg: ProjectConfig; path: string } | null = null;

  constructor(private readonly backend: Backend, private readonly recent: RecentStore) {
    // Any user mutation of the systems list marks the project dirty.
    this.systems = new SystemsStore(backend, () => this.markDirty());
  }

  settings(): ProjectSettings {
    return { ...this.projectSettings };
  }
  currentPath(): string {
    return this.path;
  }
  isDirty(): boolean {
    return this.dirty;
  }

  setLayout(n: number): boolean {
    return this.setSetting("layout", n);
  }
  setMidiRouting(n: number): boolean {
    return this.setSetting("midiRouting", n);
  }
  setAudioRouting(n: number): boolean {
    return this.setSetting("audioRouting", n);
  }
  setZoom(n: number): boolean {
    return this.setSetting("zoom", n);
  }

  /** Empty project: tear down systems, reset settings/path, mark clean. */
  newProject(): void {
    this.systems.clear();
    this.projectSettings = { ...DEFAULT_SETTINGS };
    this.path = "";
    this.dirty = false;
  }

  /** Save a thin `.rplg` (raw JSON, paths rebased relative to its folder). Records it
   *  in recents + as the current project, and marks clean. */
  save(path: string): boolean {
    const cfg = buildConfig(this.projectSettings, this.systems.systems());
    const json = serializeConfig(cfg, dirname(path), (p) => this.backend.canonicalize(p));
    if (!this.backend.writeFile(path, enc.encode(json))) return false;
    this.recent.add(path);
    this.path = path;
    this.dirty = false;
    return true;
  }

  /** Load a thin `.rplg`. Refuses a zip (deferred), refuses a newer schema, and holds
   *  a project with missing files for relink before applying. */
  load(path: string): LoadOutcome {
    const head = this.backend.readFilePrefix(path, 2);
    if (head && head.length >= 2 && head[0] === 0x50 && head[1] === 0x4b) return { kind: "error" }; // "PK" zip
    const bytes = this.backend.readFile(path);
    if (!bytes) return { kind: "error" };

    const cfg = parseConfig(dec.decode(bytes));
    if (checkVersion(parseProjectVersion(cfg.schemaVersion), K_PROJECT) === VersionCheck.Newer)
      return { kind: "incompatible" };

    toAbsolute(cfg, dirname(path));
    const missing = scanMissingFiles(cfg, NO_BLOBS, (p) => this.backend.fileExists(p));
    if (missing.length) {
      this.pendingLoad = { cfg, path };
      return { kind: "missing", missing };
    }
    return this.commit(cfg, path);
  }

  /** Point a missing item at `newPath`, auto-fix its folder-mates, and complete the
   *  load when nothing remains missing. */
  relink(item: MissingFile, newPath: string): LoadOutcome {
    if (!this.pendingLoad) return { kind: "error" };
    const { cfg, path } = this.pendingLoad;
    relinkInConfig(cfg, item, newPath);
    autoFindSiblings(cfg, dirname(newPath), NO_BLOBS, (p) => this.backend.fileExists(p));
    const missing = scanMissingFiles(cfg, NO_BLOBS, (p) => this.backend.fileExists(p));
    if (missing.length) return { kind: "missing", missing };
    this.pendingLoad = null;
    return this.commit(cfg, path);
  }

  /** Abandon a load that was awaiting relink. */
  cancelLoad(): void {
    this.pendingLoad = null;
  }

  // --- internals ----------------------------------------------------------

  // Rebuild the systems from a resolved config + adopt the settings; mark clean.
  private commit(cfg: ProjectConfig, path: string): LoadOutcome {
    this.systems.clear();
    for (const s of cfg.systems) this.systems.adopt(s);
    this.projectSettings = { ...DEFAULT_SETTINGS, ...cfg.settings };
    this.recent.add(path);
    this.path = path;
    this.dirty = false;
    return { kind: "loaded", systems: this.systems.systems().length };
  }

  private setSetting(key: keyof ProjectSettings, n: number): boolean {
    if (n < 0 || n > SETTING_MAX[key]) return false; // out of range → reject (native does)
    this.projectSettings = { ...this.projectSettings, [key]: n };
    this.markDirty();
    return true;
  }

  private markDirty(): void {
    this.dirty = true;
  }
}
