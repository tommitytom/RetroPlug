// ProjectStore: the top-level project state and source of truth. It owns the systems
// store (wiring its onChange to mark the project dirty) + the project settings +
// currentPath + dirty, and drives new / save / export / load with the missing-files
// scan + relink. TS produces AND consumes the config JSON — a fresh project round-trips
// faithfully (native restores omitted rich fields via DefaultIfMissing).
//
// Two on-disk shapes, one config model:
//   - THIN `.rplg` (save) = raw JSON, paths only. save = build config → writeFile;
//     load = readFile → parse → toAbsolute → scan/relink → adopt each system from disk.
//   - EXPORT `.rplg` (PKZIP) = the same thin project.json PLUS the emulator's live blobs
//     (per-system SRAM/savestate). export gathers the blobs via the pump (backend.read*)
//     and frames the archive; native only compresses (backend.zip). A picked PK archive
//     loads back through the same scan/relink tail, its blobs seeding each system.

import type { Backend, ZipEntry } from "./backend";
import { SystemsStore } from "./systemsStore";
import type { RoleRegistry } from "./systemRoles";
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
import { PROJECT_JSON, sramKey, stateKey, partitionEntries } from "./projectBinaries";

export type LoadOutcome =
  | { kind: "loaded"; systems: number }
  | { kind: "incompatible" } // schema stamped newer than this build
  | { kind: "missing"; missing: MissingFile[] } // needs relink before it can apply
  | { kind: "error" }; // unreadable / corrupt archive / not a RetroPlug project

const enc = new TextEncoder();
const dec = new TextDecoder();

// Inclusive upper bounds for the settings enums (native validates + rejects above).
const SETTING_MAX = { layout: 3, midiRouting: 3, audioRouting: 2, zoom: 6 };

// A restored blob as an exact-size ArrayBuffer for ConstructSpec (a zip entry may be a
// view into a larger buffer; slice() copies to a fresh, tightly-sized backing store).
function toArrayBuffer(u8: Uint8Array): ArrayBuffer {
  return u8.slice().buffer;
}

export class ProjectStore {
  readonly systems: SystemsStore;
  private projectSettings: ProjectSettings = { ...DEFAULT_SETTINGS };
  private path = "";
  private dirty = false;
  private pendingLoad: { cfg: ProjectConfig; path: string; blobs: Map<string, Uint8Array> } | null = null;
  private onSystemsChange: () => void = () => {};

  constructor(private readonly backend: Backend, private readonly recent: RecentStore, registry?: RoleRegistry) {
    // Any user mutation of the systems list marks the project dirty and re-drives the DSP.
    this.systems = new SystemsStore(
      backend,
      () => {
        this.markDirty();
        this.onSystemsChange();
      },
      registry,
    );
  }

  /** Install the hook fired whenever the systems structure changes — a user edit, or a load / new
   *  (the quiet rebuild path notifies at the end of commit/newProject). A host wires this to project
   *  the store and push it to the DSP runtime; set it AFTER the runtime exists, since the first
   *  mutation fires it. */
  setOnSystemsChange(fn: () => void): void {
    this.onSystemsChange = fn;
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
    this.onSystemsChange(); // the DSP now runs nothing
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

  /** Export a portable `.rplg` PKZIP: the thin project.json + each live system's
   *  SRAM/savestate gathered from the pump, keyed `systems/{i}/{sram,state}`. TS frames
   *  every entry; native only compresses. Records it in recents + as the current
   *  project, and marks clean. Returns false on a compression / write failure. */
  export(path: string): boolean {
    const cfg = buildConfig(this.projectSettings, this.systems.systems());
    const json = serializeConfig(cfg, dirname(path), (p) => this.backend.canonicalize(p));
    const entries: ZipEntry[] = [{ name: PROJECT_JSON, bytes: enc.encode(json) }];
    // The store's systems() order matches buildConfig's, so index i keys both alike.
    this.systems.systems().forEach((sys, i) => {
      const state = this.backend.readState(sys.id);
      if (state && state.length) entries.push({ name: stateKey(i), bytes: state });
      const sram = this.backend.readSram(sys.id);
      if (sram && sram.length) entries.push({ name: sramKey(i), bytes: sram });
    });
    const archive = this.backend.zip(entries);
    if (!archive || !this.backend.writeFileAtomic(path, archive)) return false;
    this.recent.add(path);
    this.path = path;
    this.dirty = false;
    return true;
  }

  /** Load a `.rplg` — thin (raw JSON) or an export (PKZIP). Refuses a newer schema, and
   *  holds a project with missing files for relink before applying. */
  load(path: string): LoadOutcome {
    const head = this.backend.readFilePrefix(path, 4);
    const isZip =
      !!head && head.length >= 4 && head[0] === 0x50 && head[1] === 0x4b && head[2] === 0x03 && head[3] === 0x04;
    return isZip ? this.loadZip(path) : this.loadThin(path);
  }

  /** Point a missing item at `newPath`, auto-fix its folder-mates, and complete the
   *  load when nothing remains missing. */
  relink(item: MissingFile, newPath: string): LoadOutcome {
    if (!this.pendingLoad) return { kind: "error" };
    const { cfg, path, blobs } = this.pendingLoad;
    const blobKeys = new Set(blobs.keys());
    relinkInConfig(cfg, item, newPath);
    autoFindSiblings(cfg, dirname(newPath), blobKeys, (p) => this.backend.fileExists(p));
    const missing = scanMissingFiles(cfg, blobKeys, (p) => this.backend.fileExists(p));
    if (missing.length) return { kind: "missing", missing };
    this.pendingLoad = null;
    return this.commit(cfg, path, blobs);
  }

  /** Abandon a load that was awaiting relink. */
  cancelLoad(): void {
    this.pendingLoad = null;
  }

  // --- internals ----------------------------------------------------------

  // Thin `.rplg`: raw JSON, no embedded blobs — adopt each system from disk.
  private loadThin(path: string): LoadOutcome {
    const bytes = this.backend.readFile(path);
    if (!bytes) return { kind: "error" };
    return this.beginLoad(parseConfig(dec.decode(bytes)), path, new Map(), dirname(path));
  }

  // Export `.rplg`: PKZIP of project.json + per-system blobs that seed each emulator.
  private loadZip(path: string): LoadOutcome {
    const bytes = this.backend.readFile(path);
    if (!bytes) return { kind: "error" };
    const entries = this.backend.unzip(bytes);
    if (!entries) return { kind: "error" };
    const { config, blobs } = partitionEntries(entries);
    if (!config) return { kind: "error" }; // no project.json → not a RetroPlug archive
    return this.beginLoad(parseConfig(dec.decode(config)), path, blobs, dirname(path));
  }

  // Shared load tail: refuse-newer, absolutize, blob-aware missing scan, then pend for
  // relink or commit. `blobs` is empty for a thin load, populated for an export.
  private beginLoad(cfg: ProjectConfig, path: string, blobs: Map<string, Uint8Array>, baseDir: string): LoadOutcome {
    if (checkVersion(parseProjectVersion(cfg.schemaVersion), K_PROJECT) === VersionCheck.Newer)
      return { kind: "incompatible" };
    toAbsolute(cfg, baseDir);
    const blobKeys = new Set(blobs.keys());
    const missing = scanMissingFiles(cfg, blobKeys, (p) => this.backend.fileExists(p));
    if (missing.length) {
      this.pendingLoad = { cfg, path, blobs };
      return { kind: "missing", missing };
    }
    return this.commit(cfg, path, blobs);
  }

  // Rebuild the systems from a resolved config + adopt the settings; mark clean. Each
  // system's SRAM/savestate blobs (export only) seed its emulator via adopt.
  private commit(cfg: ProjectConfig, path: string, blobs: Map<string, Uint8Array>): LoadOutcome {
    this.systems.clear();
    cfg.systems.forEach((s, i) => {
      const sram = blobs.get(sramKey(i));
      const state = blobs.get(stateKey(i));
      const sysBlobs =
        sram || state
          ? { sramBytes: sram ? toArrayBuffer(sram) : undefined, stateBytes: state ? toArrayBuffer(state) : undefined }
          : undefined;
      this.systems.adopt(s, sysBlobs);
    });
    this.projectSettings = { ...DEFAULT_SETTINGS, ...cfg.settings };
    this.recent.add(path);
    this.path = path;
    this.dirty = false;
    this.onSystemsChange(); // push the rebuilt systems (the adopt path is quiet)
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
