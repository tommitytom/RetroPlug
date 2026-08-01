// ProjectStore: the top-level project state and source of truth. It owns the systems
// store (wiring its onChange to mark the project dirty) + the project settings +
// currentPath + dirty, and drives new / save / export / load with the missing-files
// scan + relink. TS produces AND consumes the config JSON — a fresh project round-trips
// faithfully (native restores omitted rich fields via DefaultIfMissing).
//
// Two on-disk shapes, one config model — distinguished by EXTENSION, not by content:
//   - THIN `.rplg` (save) = raw JSON, paths only. save = build config → writeFile;
//     load = readFile → parse → toAbsolute → scan/relink → adopt each system from disk. A `.rplg` is
//     ALWAYS pure JSON — never a zip; load errors if the bytes aren't valid JSON.
//   - EXPORT `.rplg.zip` (PKZIP) = the same thin project.json PLUS the emulator's live blobs
//     (per-system SRAM/savestate). export gathers the blobs via the pump (backend.read*)
//     and frames the archive; native only compresses (backend.zip). A `.rplg.zip`
//     loads back through the same scan/relink tail, its blobs seeding each system.

import type { ControlPlaneBackend, ZipEntry } from "./backend";
import { SystemsStore } from "./systemsStore";
import type { RoleRegistry } from "./systemRoles";
import type { RecentStore } from "./recentStore";
import { resolveSongCatalog } from "./tracker";
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
import {
  type SystemLayout,
  type MidiRouting,
  type AudioRouting,
  LAYOUT_VALUES,
  MIDI_ROUTING_VALUES,
  AUDIO_ROUTING_VALUES,
  audioRoutingToIndex,
} from "./settingsEnums";
import { scanMissingFiles, autoFindSiblings, relinkInConfig, type MissingFile } from "./projectMissing";
import { PROJECT_JSON, sramKey, stateKey, partitionEntries } from "./projectBinaries";

export type LoadOutcome =
  | { kind: "loaded"; systems: number }
  | { kind: "incompatible" } // schema stamped newer than this build
  | { kind: "missing"; missing: MissingFile[] } // needs relink before it can apply
  | { kind: "error" }; // unreadable / corrupt archive / not a RetroPlug project

const enc = new TextEncoder();
const dec = new TextDecoder();

// Parse JSON to a plain object, or null (invalid JSON / not an object). Used both to guard a thin `.rplg`
// load (it must be pure JSON — never a zip) and to edit a single field of a project file in place.
function parseJsonObject(text: string): Record<string, unknown> | null {
  try {
    const doc = JSON.parse(text);
    return doc && typeof doc === "object" && !Array.isArray(doc) ? (doc as Record<string, unknown>) : null;
  } catch {
    return null;
  }
}

// A project is a zip (PKZIP, blobs) iff its name ends `.rplg.zip`; a plain `.rplg` is always thin raw JSON.
// Load/rename dispatch on this extension — NOT on the file's bytes — so a `.rplg` is never treated as a zip.
function isZipProjectPath(path: string): boolean {
  return /\.rplg\.zip$/i.test(path);
}

// Inclusive upper bound for `zoom` (the one numeric setting; native validates + rejects above).
const ZOOM_MAX = 6;

// Allowed value tuples for the string-enum settings — an unknown value is rejected (native re-validates).
const SETTING_VALUES = {
  layout: LAYOUT_VALUES,
  midiRouting: MIDI_ROUTING_VALUES,
  audioRouting: AUDIO_ROUTING_VALUES,
} as const;


export class ProjectStore {
  readonly systems: SystemsStore;
  private projectSettings: ProjectSettings = { ...DEFAULT_SETTINGS };
  private path = "";
  private projectName = ""; // the USER-set name (Project > Name); blank unless typed, persisted in the .rplg
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
  /** The project's own name - what the user typed under Project > Name, "" when they haven't. The only
   *  name persisted in the .rplg; a derived one never is (see `displayName`). */
  name(): string {
    return this.projectName;
  }
  /** The name to SHOW (recents entry, window / menu titles): the user's name when set, else one derived
   *  from the live systems. Never written to the .rplg - a nameless project stays nameless on disk. */
  displayName(): string {
    return this.projectName || this.deriveName();
  }
  /** Set (or clear, with a blank value) the project's own name. Marks the project dirty, so the next save
   *  persists it. Returns whether the name actually changed. */
  setName(name: string): boolean {
    const trimmed = name.trim();
    if (trimmed === this.projectName) return false;
    this.projectName = trimmed;
    this.markDirty(); // fires onChangeCb - titles + the menu label follow
    return true;
  }
  isDirty(): boolean {
    return this.dirty;
  }

  // The name derived from the system instances: the primary system's paired-sav override stem, else its rom
  // stem, minus the extension - "the sav name, or the rom name if there's no [explicit] sav". Primary =
  // focused, else first. Empty (embedded / no systems) yields "", leaving the recents basename fallback.
  private deriveName(): string {
    const list = this.systems.systems();
    const primary = list.find((s) => s.id === this.systems.focused()) ?? list[0];
    return primary ? stem(primary.savPath || primary.romPath || "") : "";
  }

  // The primary system's working-song name (a tracker cart's loaded song), for the recents label. undefined
  // for a non-tracker system or when there's no readable SRAM. Primary = focused, else first.
  private currentSong(): string | undefined {
    const list = this.systems.systems();
    const primary = list.find((s) => s.id === this.systems.focused()) ?? list[0];
    if (!primary) return undefined;
    const catalog = resolveSongCatalog(primary.roles);
    if (!catalog) return undefined;
    const sram = this.backend.readSram(primary.id);
    return sram ? catalog.workingName(sram) ?? undefined : undefined;
  }

  setLayout(v: SystemLayout): boolean {
    return this.setEnumSetting("layout", v);
  }
  setMidiRouting(v: MidiRouting): boolean {
    return this.setEnumSetting("midiRouting", v);
  }
  setAudioRouting(v: AudioRouting): boolean {
    if (!this.setEnumSetting("audioRouting", v)) return false;
    this.pushAudioRouting(); // the one project setting that reaches native audio (the MultiOutRouter)
    return true;
  }
  setZoom(n: number): boolean {
    if (n < 0 || n > ZOOM_MAX) return false; // out of range → reject (native does)
    this.projectSettings = { ...this.projectSettings, zoom: n };
    this.markDirty();
    return true;
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
   *  `<rom>.rplg` sibling (recents + current path). The "new project from a ROM" op - what the start
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
    const cfg = buildConfig(this.projectSettings, this.systems.systems(), this.projectName); // blank name → omitted
    const json = serializeConfig(cfg, dirname(path), (p) => this.backend.canonicalize(p));
    if (!this.backend.writeFile(path, enc.encode(json))) return false;
    this.recent.add(path, this.displayName(), this.currentSong()); // the recents label - derived unless the user named it
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
    if (!this.backend.fileExists(path)) {
      this.save(path);
      return;
    }
    this.recent.add(path, this.displayName(), this.currentSong());
    this.path = path;
    this.onChangeCb();
  }

  /** Export a portable `.rplg` PKZIP: the thin project.json + each live system's
   *  SRAM/savestate gathered from the pump, keyed `systems/{i}/{sram,state}`. TS frames
   *  every entry; native only compresses. Records it in recents + as the current
   *  project, and marks clean. Returns false on a compression / write failure. */
  export(path: string): boolean {
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
    this.recent.add(path, this.displayName(), this.currentSong());
    this.path = path;
    this.dirty = false;
    this.onChangeCb();
    return true;
  }

  /** Export to in-memory PKZIP bytes (the plugin's DPF state chunk) — the same archive `export`
   *  writes, but WITHOUT the recents/currentPath/dirty side-effects a host save has. Paths are left
   *  absolute (`baseDir=""`), so `loadBytes` round-trips them with no rebase. */
  exportBytes(): Uint8Array | null {
    const cfg = buildConfig(this.projectSettings, this.systems.systems(), this.projectName);
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

  /** Load a project from in-memory PKZIP bytes — the plugin's DPF state chunk (base64 of an `exportBytes`
   *  archive). Always a zip; on-disk files load via `load(path)` instead. `baseDir` rebases relative asset
   *  paths (`""` for the absolute-path DPF chunk). No recents/currentPath side-effects. */
  loadBytes(bytes: Uint8Array, baseDir = ""): LoadOutcome {
    const entries = this.backend.unzip(bytes);
    if (!entries) return { kind: "error" };
    const { config, blobs } = partitionEntries(entries);
    if (!config) return { kind: "error" }; // no project.json → not a RetroPlug archive
    return this.beginLoad(parseConfig(dec.decode(config)), "", blobs, baseDir);
  }

  /** Load a project — a thin `.rplg` (raw JSON) or an export `.rplg.zip` (PKZIP). Routing is by
   *  EXTENSION, not content: a `.rplg` is always parsed as JSON (a zip masquerading as `.rplg` errors,
   *  never loads). Refuses a newer schema, and holds a project with missing files for relink. */
  load(path: string): LoadOutcome {
    return isZipProjectPath(path) ? this.loadZip(path) : this.loadThin(path);
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

  // Thin `.rplg`: raw JSON, no embedded blobs — adopt each system from disk. It MUST be pure JSON: a zip
  // (or any non-JSON) is rejected with an error, never silently coerced to an empty project (parseConfig
  // never throws, so the explicit parseJsonObject guard is what enforces "pure JSON or error").
  private loadThin(path: string): LoadOutcome {
    const bytes = this.backend.readFile(path);
    if (!bytes) return { kind: "error" };
    const text = dec.decode(bytes);
    if (!parseJsonObject(text)) {
      console.error(`.rplg is not pure JSON (a zip? zip projects must use .rplg.zip): ${path}`);
      return { kind: "error" };
    }
    return this.beginLoad(parseConfig(text), path, new Map(), dirname(path));
  }

  // Export `.rplg.zip`: PKZIP of project.json + per-system blobs that seed each emulator.
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
    this.projectName = cfg.name ?? ""; // only a name the user gave the project is stored; blank = unnamed
    if (path) this.recent.add(path, this.displayName(), this.currentSong()); // in-memory loads (plugin state chunk) pass "" - no recents entry
    this.path = path;
    this.dirty = false;
    this.onSystemsChange(); // push the rebuilt systems (the adopt path is quiet)
    this.onChangeCb();
    return { kind: "loaded", systems: this.systems.systems().length };
  }

  private setEnumSetting(key: "layout" | "midiRouting" | "audioRouting", value: string): boolean {
    if (!(SETTING_VALUES[key] as readonly string[]).includes(value)) return false; // unknown → reject (native does)
    this.projectSettings = { ...this.projectSettings, [key]: value } as ProjectSettings;
    this.markDirty();
    return true;
  }

  /** Push the current audio-routing mode to native — the one project setting that drives native audio
   *  (the block runner's MultiOutRouter). Converts the string value to native's AudioRouting integer.
   *  Called on set / load / reset so native tracks the TS value. */
  private pushAudioRouting(): void {
    this.backend.setAudioRouting(audioRoutingToIndex(this.projectSettings.audioRouting));
  }

  private markDirty(): void {
    this.dirty = true;
    this.onChangeCb();
  }
}
