// The project config model + codec. TS owns the config as source of truth: it BUILDS
// the config from the live systems + settings, SERIALIZES it (rebasing asset paths
// relative to the .rplg's folder so it's portable) and PARSES it back (rebasing to
// absolute). Ports schemaVersions.ts + the thin ProjectConfig shape.
//
// "Thin" = the binary blobs (rom/sram/state/kit) are never in the config, and rich
// per-system fields (model/roles/kits/…) are omitted — they restore to native
// defaults via reflect-cpp DefaultIfMissing, so a fresh/uncustomized project
// round-trips faithfully. Only user-customized fields are lost until the
// system-settings/kits domains land.

import type { SystemEntry, SystemKind } from "./systemsList";
import { rebaseToRelative, rebaseToAbsolute } from "./projectPaths";

/** Project-level settings (four scalars). `zoom` 0 = inherit the user default. */
export interface ProjectSettings {
  layout: number; // 0 Auto / 1 Row / 2 Column / 3 Grid
  midiRouting: number; // 0 SendToAll / 1 FourPerInstance / 2 OnePerInstance / 3 ChannelToInstance
  audioRouting: number; // 0 Stereo / 1 TwoPerInstance / 2 OnePerInstance
  zoom: number; // 0 inherit / 1..6
}

export const DEFAULT_SETTINGS: ProjectSettings = { layout: 0, midiRouting: 0, audioRouting: 0, zoom: 0 };

/** A system as serialized: thin, with default fields omitted. */
export interface SystemThin {
  kind: SystemKind;
  romPath?: string;
  savPath?: string; // an explicit override; absent = derive from suffix
  savSuffix?: number;
  embeddedRom?: string; // e.g. "mgb"
}

export interface ProjectConfig {
  schemaVersion: string;
  settings: ProjectSettings;
  systems: SystemThin[];
}

// --- schema version (port of schemaVersions.ts) ---------------------------

/** Bump ONLY on a breaking (non-additive) project-format change. Mirrors kProject. */
export const K_PROJECT = 1;

export enum VersionCheck {
  Ok = "ok",
  Older = "older",
  Newer = "newer",
}

export function checkVersion(fileVersion: number, current: number): VersionCheck {
  if (fileVersion === current) return VersionCheck.Ok;
  return fileVersion < current ? VersionCheck.Older : VersionCheck.Newer;
}

/** schemaVersion is a legacy string ("1.0", "2", …); take the leading integer, or
 *  the baseline floor when there are no leading digits. */
export function parseProjectVersion(s: string): number {
  const m = /^\s*(\d+)/.exec(s);
  return m ? parseInt(m[1], 10) : K_PROJECT;
}

// --- build / serialize / parse --------------------------------------------

/** Live systems + settings → a thin ProjectConfig, dropping runtime ids and any
 *  field at its default. Stamps the running schema version. */
export function buildConfig(settings: ProjectSettings, systems: SystemEntry[]): ProjectConfig {
  return {
    schemaVersion: String(K_PROJECT),
    settings: { ...settings },
    systems: systems.map((e) => {
      const t: SystemThin = { kind: e.kind };
      if (e.romPath) t.romPath = e.romPath;
      if (e.savPath) t.savPath = e.savPath;
      if (e.savSuffix) t.savSuffix = e.savSuffix;
      if (e.embeddedRom) t.embeddedRom = e.embeddedRom;
      return t;
    }),
  };
}

// Apply `fn` to every asset path a system carries (romPath + savPath), storing the
// result back. The single place path fields are enumerated (mirrors native visitPaths).
function mapSystemPaths(cfg: ProjectConfig, fn: (path: string) => string): void {
  for (const s of cfg.systems) {
    if (s.romPath) s.romPath = fn(s.romPath);
    if (s.savPath) s.savPath = fn(s.savPath);
  }
}

/** Serialize to JSON, rebasing each asset path relative to `baseDir` (portable). An
 *  empty `baseDir` leaves paths absolute. `canonicalize` backs the realpath-hard
 *  toRelative test. */
export function serializeConfig(cfg: ProjectConfig, baseDir: string, canonicalize: (p: string) => string): string {
  const out: ProjectConfig = { ...cfg, settings: { ...cfg.settings }, systems: cfg.systems.map((s) => ({ ...s })) };
  if (baseDir) mapSystemPaths(out, (p) => rebaseToRelative(p, baseDir, canonicalize));
  return JSON.stringify(out);
}

/** Parse config JSON, tolerantly filling settings defaults + an empty systems array. */
export function parseConfig(json: string): ProjectConfig {
  let doc: Partial<ProjectConfig> = {};
  try {
    doc = JSON.parse(json) as Partial<ProjectConfig>;
  } catch {
    doc = {};
  }
  return {
    schemaVersion: typeof doc.schemaVersion === "string" ? doc.schemaVersion : "",
    settings: { ...DEFAULT_SETTINGS, ...(doc.settings ?? {}) },
    systems: Array.isArray(doc.systems) ? doc.systems : [],
  };
}

/** Rebase each asset path to absolute against `baseDir`, in place (load side). */
export function toAbsolute(cfg: ProjectConfig, baseDir: string): void {
  mapSystemPaths(cfg, (p) => rebaseToAbsolute(p, baseDir));
}
