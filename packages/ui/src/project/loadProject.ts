// Plugin-side project LOAD orchestration — the TS home of what used to be
// PluginRpcService::loadProjectFromPath + the pendingProject_ / missing-files /
// relink machine. Runs the shared @retroplug/retroplug logic (schema check,
// missing-file scan/relink, path rebasing) over the plugin's byte-mover
// primitives, and hands a fully-resolved project to the DSP via commitProject
// (Command::makeLoadProject — async; the DSP applies + re-emits ProjectLoaded).
//
// The parsed config is held here as `pending` between the scan and the relink
// loop, so the native side needs no pending latch. The config is TS-transparent
// for load (parse -> edit paths -> re-serialize); rfl::json::read is
// forward-tolerant and projectConfig.ts carries an index-signature passthrough,
// so unmodelled fields survive the round-trip.

import { projectHost, fileExists, commitProject } from "./projectHost";
import {
  scanMissingFiles, relinkInConfig, autoFindSiblings, toAbsolute, dirname,
  type MissingFile,
} from "@retroplug/retroplug/missing-files";
import { PROJECT_JSON, isBlobEntry } from "@retroplug/retroplug/project-binaries";
import { parseProjectVersion, checkVersion, VersionCheck, K_PROJECT } from "@retroplug/retroplug/schema-versions";
import type { ProjectConfig } from "@retroplug/retroplug/project-config";

const dec = new TextDecoder();

function isZip(bytes: Uint8Array): boolean {
  return bytes.length >= 2 && bytes[0] === 0x50 && bytes[1] === 0x4b; // "PK"
}

interface Blob { name: string; bytes: Uint8Array }

interface Pending {
  config: ProjectConfig;      // parsed, path-rebased, (partially) relinked
  blobs: Blob[];              // the keyed zip blob entries (empty for a thin JSON load)
  blobKeys: ReadonlySet<string>;
  path: string;
}
let pending: Pending | null = null;

export interface LoadResult {
  // Non-empty => the load is held pending; the UI shows the relink menu. Empty +
  // !incompatible => committed (loading). incompatible => schema newer than build.
  missing: MissingFile[];
  incompatible: boolean;
  error?: string;
}

// Begin loading `.rplg` at `path`. Autodetects zip vs path-only JSON.
export function startLoad(path: string): LoadResult {
  pending = null;
  const bytes = projectHost.readFile(path);
  if (bytes.length === 0) return { missing: [], incompatible: false, error: "empty/unreadable" };

  let configJson: string;
  let blobs: Blob[] = [];
  if (isZip(bytes)) {
    const entries = projectHost.unzipEntries(bytes);
    const cfg = entries.find((e) => e.name === PROJECT_JSON);
    if (!cfg) return { missing: [], incompatible: false, error: `no ${PROJECT_JSON}` };
    configJson = dec.decode(cfg.bytes);
    blobs = entries.filter((e) => isBlobEntry(e.name));
  } else {
    configJson = dec.decode(bytes);
  }

  let config: ProjectConfig;
  try {
    config = JSON.parse(configJson) as ProjectConfig;
  } catch {
    return { missing: [], incompatible: false, error: "parse failed" };
  }

  // Refuse a project stamped by a newer build than we understand.
  if (checkVersion(parseProjectVersion(config.schemaVersion ?? ""), K_PROJECT) === VersionCheck.Newer)
    return { missing: [], incompatible: true };

  // Resolve project-relative paths against the .rplg's dir so the scan + the DSP
  // load work in absolute terms (zip exports / old saves are already absolute).
  toAbsolute(config, dirname(path));

  const blobKeys = new Set(blobs.map((b) => b.name));
  const missing = scanMissingFiles(config, blobKeys, fileExists);
  if (missing.length === 0) {
    commit(config, blobs, path);
    return { missing: [], incompatible: false };
  }
  pending = { config, blobs, blobKeys, path };
  return { missing, incompatible: false };
}

// Point one pending item at `newPath` (+ auto-relink siblings in its folder),
// re-scan, and either commit (nothing left) or return the remainder.
export function relinkOne(item: MissingFile, newPath: string): MissingFile[] {
  if (!pending) return [];
  if (!newPath) return scanMissingFiles(pending.config, pending.blobKeys, fileExists);

  relinkInConfig(pending.config, item, newPath);
  const dir = dirname(newPath);
  if (dir) autoFindSiblings(pending.config, dir, pending.blobKeys, fileExists);

  const remaining = scanMissingFiles(pending.config, pending.blobKeys, fileExists);
  if (remaining.length === 0) {
    commit(pending.config, pending.blobs, pending.path);
    pending = null;
    return [];
  }
  return remaining;
}

// Abandon the pending load, keeping the current project.
export function cancelLoad(): void {
  pending = null;
}

function commit(config: ProjectConfig, blobs: Blob[], path: string): void {
  // Re-serialize the (path-rebased / relinked) config; native restores the blobs,
  // recompiles kits, and hands it to the DSP.
  commitProject(JSON.stringify(config), blobs, path);
}
