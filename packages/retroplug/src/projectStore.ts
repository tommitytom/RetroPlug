// ProjectStore: the top-level project state and source of truth. It owns the systems
// store (wiring its onChange to mark the project dirty) + the project settings +
// currentPath + dirty, and drives new / save / export / load with the missing-files
// scan + relink. TS produces AND consumes the config JSON — a fresh project round-trips
// faithfully (native restores omitted rich fields via DefaultIfMissing).
//
// Two on-disk shapes, one config model:
//   - THIN `.rplg` (save) = raw JSON, paths only. save = build config → writeFile;
//     load = readFile → parse → toAbsolute → scan/relink → adopt each system from disk.
//   - EXPORT `.rplg.zip` (PKZIP) = the same thin project.json PLUS the emulator's live blobs
//     (per-system SRAM/savestate). export gathers the blobs via the pump (backend.read*)
//     and frames the archive; native only compresses (backend.zip). A picked PK archive
//     loads back through the same scan/relink tail, its blobs seeding each system.

import type { ControlPlaneBackend, ZipEntry } from "./backend";
import { SystemsStore } from "./systemsStore";
import type { RoleRegistry } from "./systemRoles";
import type { RecentStore } from "./recentStore";
import { dirname, stem } from "./pathUtil";
import { siblingRplgPath } from "./savPaths";
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

// Parse JSON to a plain object, or null (invalid JSON / not an object). Used to edit a single field of a
// project file without disturbing the rest — a targeted name swap, not a full parse/rebuild.
function parseJsonObject(text: string): Record<string, unknown> | null {
  try {
    const doc = JSON.parse(text);
    return doc && typeof doc === "object" && !Array.isArray(doc) ? (doc as Record<string, unknown>) : null;
  } catch {
    return null;
  }
}

// Inclusive upper bounds for the settings enums (native validates + rejects above).
const SETTING_MAX = { layout: 3, midiRouting: 3, audioRouting: 3, zoom: 6 };


export class ProjectStore {
  readonly systems: SystemsStore;
  private projectSettings: ProjectSettings = { ...DEFAULT_SETTINGS };
  private path = "";
  private projectName = ""; // display name; seeded from the primary system's sav/rom stem, persisted in the .rplg
  private dirty = false;
  private pendingLoad: { cfg: ProjectConfig; path: string; blobs: Map<string, Uint8Array> } | null = null;
  private onSystemsChange: () => void = () => {};
  private onChangeCb: () => void = () => {};

  constructor(private readonly backend: ControlPlaneBackend, private readonly recent: RecentStore, registry?: RoleRegistry) {
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

  /** Install a hook fired on ANY project-state change — settings edits (setLayout / setZoom / …) and
   *  dirty transitions (mutation, save, export, new, load). Distinct from setOnSystemsChange, which
   *  fires only on a systems-structure change (and drives the DSP projection). A UI multiplexer wires
   *  this to re-render the settings / dirty views, which would otherwise never be notified. */
  setOnChange(fn: () => void): void {
    this.onChangeCb = fn;
  }

  settings(): ProjectSettings {
    return { ...this.projectSettings };
  }
  currentPath(): string {
    return this.path;
  }
  /** The project's display name (seeded from the primary system's sav/rom stem, persisted in the .rplg). */
  name(): string {
    return this.projectName;
  }
  isDirty(): boolean {
    return this.dirty;
  }

  // The name to seed from: the primary system's paired-sav override stem, else its rom stem, minus the
  // extension — "the sav name, or the rom name if there's no [explicit] sav". Primary = focused, else first.
  // Empty (embedded / no systems) yields "", leaving the recents basename fallback to label it.
  private deriveName(): string {
    const list = this.systems.systems();
    const primary = list.find((s) => s.id === this.systems.focused()) ?? list[0];
    return primary ? stem(primary.savPath || primary.romPath || "") : "";
  }
  // Seed the name from the primary system if it doesn't have one yet (a no-op once named).
  private ensureName(): void {
    if (!this.projectName) this.projectName = this.deriveName();
  }

  setLayout(n: number): boolean {
    return this.setSetting("layout", n);
  }
  setMidiRouting(n: number): boolean {
    return this.setSetting("midiRouting", n);
  }
  setAudioRouting(n: number): boolean {
    if (!this.setSetting("audioRouting", n)) return false;
    this.pushAudioRouting(); // the one project setting that reaches native audio (the MultiOutRouter)
    return true;
  }
  setZoom(n: number): boolean {
    return this.setSetting("zoom", n);
  }

  /** Empty project: tear down systems, reset settings/path, mark clean. */
  newProject(): void {
    this.systems.clear();
    this.projectSettings = { ...DEFAULT_SETTINGS };
    this.pushAudioRouting(); // reset native routing to the default
    this.path = "";
    this.projectName = "";
    this.dirty = false;
    this.onSystemsChange(); // the DSP now runs nothing
    this.onChangeCb();
  }

  /** Open a ROM as a fresh project: reset to empty, build the ROM as the sole system, then adopt its
   *  `<rom>.rplg` sibling (name + recents + current path). The "new project from a ROM" op — what the start
   *  menu does from empty, reusable when a project is already open. The caller resolves the ROM-vs-project
   *  branch first (FileSelection.resolveLoad), so `loadRom`'s sibling-project defer never fires here. */
  openRom(romPath: string, opts?: { explicitSav?: string }): void {
    this.newProject();
    this.systems.loadRom(romPath, opts);
    this.adoptRomProject(romPath);
  }

  /** Save a thin `.rplg` (raw JSON, paths rebased relative to its folder). Records it
   *  in recents + as the current project, and marks clean. */
  save(path: string): boolean {
    this.ensureName(); // a manually-built project (New Project + Add) gets named at its first save
    const cfg = buildConfig(this.projectSettings, this.systems.systems(), this.projectName);
    const json = serializeConfig(cfg, dirname(path), (p) => this.backend.canonicalize(p));
    if (!this.backend.writeFile(path, enc.encode(json))) return false;
    this.recent.add(path, this.projectName); // seed the recents display name (Save-As keeps it, not the file stem)
    this.path = path;
    this.dirty = false;
    this.onChangeCb();
    return true;
  }

  /** Adopt a freshly-loaded ROM as its sibling project so it lands in recents. On the first open of a
   *  ROM (no `<rom>.rplg` yet — `systems.loadRom` would have deferred to it otherwise), write the thin
   *  sibling via `save`, which records it in recents + adopts it as the current project + marks clean.
   *  A pre-existing sibling (only reachable via a paired-`.sav` load, which bypasses the auto-defer) is
   *  left untouched — just track it in recents + as the current path, keeping `dirty` so a pinned save
   *  override survives to the next save. Mirrors legacy's writeSiblingProject-on-load. */
  adoptRomProject(romPath: string): void {
    if (!romPath) return; // embedded ROMs (mgb) have no on-disk sibling to track
    const path = siblingRplgPath(romPath);
    this.projectName = this.deriveName(); // this ROM defines the project's name (its sav/rom stem)
    if (!this.backend.fileExists(path)) {
      this.save(path);
      return;
    }
    this.recent.add(path, this.projectName);
    this.path = path;
    this.onChangeCb();
  }

  /** Rename the project at `path`: edit its persisted `name` (the source of truth), refresh the recents
   *  display alias, and sync the live name when it's the open project. Works on a thin `.rplg` or an
   *  export `.rplg.zip`. A blank name is rejected. When the file can't be read/written the alias is still
   *  updated (best-effort) so the entry shows the chosen name; the on-disk name would re-seed on next
   *  save. Returns whether the file itself was rewritten. */
  renameProject(path: string, name: string): boolean {
    const trimmed = name.trim();
    if (!path || !trimmed) return false;
    const wrote = this.writeProjectName(path, trimmed);
    this.recent.rename(path, trimmed); // update the recents display alias (fires its onChange)
    if (this.path && this.backend.canonicalize(path) === this.backend.canonicalize(this.path)) {
      this.projectName = trimmed; // keep the open project's live name in sync
      this.onChangeCb();
    }
    return wrote;
  }

  // Edit ONLY the `name` field of a project file on disk, leaving everything else (paths, systems,
  // settings) byte-identical. Thin `.rplg` = a JSON field swap; export `.rplg.zip` = rewrite its
  // project.json entry. Returns false when the file is unreadable / unparseable / unwritable.
  private writeProjectName(path: string, name: string): boolean {
    const bytes = this.backend.readFile(path);
    if (!bytes) return false;
    const isZip =
      bytes.length >= 4 && bytes[0] === 0x50 && bytes[1] === 0x4b && bytes[2] === 0x03 && bytes[3] === 0x04;
    if (!isZip) {
      const doc = parseJsonObject(dec.decode(bytes));
      if (!doc) return false;
      doc.name = name;
      return this.backend.writeFile(path, enc.encode(JSON.stringify(doc)));
    }
    const entries = this.backend.unzip(bytes);
    if (!entries) return false;
    const out: ZipEntry[] = entries.map((e) => {
      if (e.name !== PROJECT_JSON) return e;
      const doc = parseJsonObject(dec.decode(e.bytes)) ?? {};
      doc.name = name;
      return { name: e.name, bytes: enc.encode(JSON.stringify(doc)) };
    });
    const archive = this.backend.zip(out);
    return !!archive && this.backend.writeFileAtomic(path, archive);
  }

  /** Export a portable `.rplg` PKZIP: the thin project.json + each live system's
   *  SRAM/savestate gathered from the pump, keyed `systems/{i}/{sram,state}`. TS frames
   *  every entry; native only compresses. Records it in recents + as the current
   *  project, and marks clean. Returns false on a compression / write failure. */
  export(path: string): boolean {
    this.ensureName();
    const cfg = buildConfig(this.projectSettings, this.systems.systems(), this.projectName);
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
    this.recent.add(path, this.projectName);
    this.path = path;
    this.dirty = false;
    this.onChangeCb();
    return true;
  }

  /** Export to in-memory PKZIP bytes (the plugin's DPF state chunk) — the same archive `export`
   *  writes, but WITHOUT the recents/currentPath/dirty side-effects a host save has. Paths are left
   *  absolute (`baseDir=""`), so `loadBytes` round-trips them with no rebase. */
  exportBytes(): Uint8Array | null {
    const cfg = buildConfig(this.projectSettings, this.systems.systems(), this.projectName || this.deriveName());
    const json = serializeConfig(cfg, "", (p) => this.backend.canonicalize(p));
    const entries: ZipEntry[] = [{ name: PROJECT_JSON, bytes: enc.encode(json) }];
    this.systems.systems().forEach((sys, i) => {
      const state = this.backend.readState(sys.id);
      if (state && state.length) entries.push({ name: stateKey(i), bytes: state });
      const sram = this.backend.readSram(sys.id);
      if (sram && sram.length) entries.push({ name: sramKey(i), bytes: sram });
    });
    return this.backend.zip(entries);
  }

  /** Load a project from in-memory PKZIP bytes (the plugin's DPF state chunk / an autoloaded `.rplg`).
   *  `baseDir` rebases relative asset paths (`""` for an absolute-path chunk from `exportBytes`, or the
   *  `.rplg`'s folder for an on-disk file). No recents/currentPath side-effects. */
  loadBytes(bytes: Uint8Array, baseDir = ""): LoadOutcome {
    const entries = this.backend.unzip(bytes);
    if (!entries) return { kind: "error" };
    const { config, blobs } = partitionEntries(entries);
    if (!config) return { kind: "error" }; // no project.json → not a RetroPlug archive
    return this.beginLoad(parseConfig(dec.decode(config)), "", blobs, baseDir);
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
      // A savestate is the complete machine snapshot (it already contains the battery-backed SRAM), so
      // boot from it ALONE — also seeding sramBytes cold-reseeds the battery and drops the restored
      // runtime state (e.g. an LSDj armed in "wait for MIDI"). The sram blob only seeds a system that
      // has no savestate (a thin load, or a battery-only export).
      const sysBlobs = state ? { stateBytes: state } : sram ? { sramBytes: sram } : undefined;
      this.systems.adopt(s, sysBlobs);
    });
    this.projectSettings = { ...DEFAULT_SETTINGS, ...cfg.settings };
    this.pushAudioRouting(); // apply the loaded project's routing to native audio
    this.projectName = cfg.name || this.deriveName(); // stored name wins; an old nameless .rplg derives one
    if (path) this.recent.add(path, this.projectName); // in-memory loads (plugin state chunk) pass "" — no recents entry
    this.path = path;
    this.dirty = false;
    this.onSystemsChange(); // push the rebuilt systems (the adopt path is quiet)
    this.onChangeCb();
    return { kind: "loaded", systems: this.systems.systems().length };
  }

  private setSetting(key: keyof ProjectSettings, n: number): boolean {
    if (n < 0 || n > SETTING_MAX[key]) return false; // out of range → reject (native does)
    this.projectSettings = { ...this.projectSettings, [key]: n };
    this.markDirty();
    return true;
  }

  /** Push the current audio-routing mode to native — the one project setting that drives native audio
   *  (the block runner's MultiOutRouter). Called on set / load / reset so native tracks the TS value. */
  private pushAudioRouting(): void {
    this.backend.setAudioRouting(this.projectSettings.audioRouting);
  }

  private markDirty(): void {
    this.dirty = true;
    this.onChangeCb();
  }
}
