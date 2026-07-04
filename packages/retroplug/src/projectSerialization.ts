// Project save/load serialization — the TS port of the C++ orchestration in
// project/ProjectSerialization.hpp + TestHarnessImpl (saveRplg/saveProjectFile/
// loadRplg). It owns the .rplg zip structure, the entry-key contract, and the
// schema-version check; the irreducibly-native work (walking the live instances
// for the config + blobs, building/activating the emulator, kit compile, miniz)
// stays behind the ProjectHost primitives.
//
// The config JSON is OPAQUE here: the host produces it (reflect-cpp write, schema
// stamped) and consumes it (reflect-cpp read); this layer only JSON.parses it to
// read the version and passes the ORIGINAL string through — so there's no
// reflect-cpp-vs-JSON.stringify format drift.

import { PROJECT_JSON, isBlobEntry } from "./projectBinaries";
import { VersionCheck, checkVersion, parseProjectVersion, K_PROJECT } from "./schemaVersions";
import type { ProjectConfig } from "./projectConfig";

export interface Blob {
  name: string;
  bytes: Uint8Array;
}

// The native byte-mover primitives this orchestration drives (HarnessService in
// the CLI harness). Kept minimal + wire-type-free so it's mockable in tests.
export interface ProjectHost {
  readFile(path: string): Uint8Array;
  writeFile(path: string, bytes: Uint8Array): void;
  /** Assemble a PKZIP blob from entries (miniz). */
  zipEntries(entries: Blob[]): Uint8Array;
  /** Parse a PKZIP blob into its entries (miniz). */
  unzipEntries(bytes: Uint8Array): Blob[];
  /** Live project -> thin config JSON (blobs stripped, schema stamped) + the
   *  blobs as keyed entries. */
  snapshotProjectConfig(): { config: string; blobs: Blob[] };
  /** Rebuild + activate the project from a config + its blob entries; returns
   *  the first restored system id. */
  applyProjectConfig(config: string, blobs: Blob[]): number;
}

const enc = new TextEncoder();
const dec = new TextDecoder();

function isZip(bytes: Uint8Array): boolean {
  return bytes.length >= 2 && bytes[0] === 0x50 && bytes[1] === 0x4b; // "PK"
}

// Self-contained bundle: project.json + every binary blob at its keyed entry.
export function saveRplg(host: ProjectHost, path: string): void {
  const snap = host.snapshotProjectConfig();
  const entries: Blob[] = [{ name: PROJECT_JSON, bytes: enc.encode(snap.config) }, ...snap.blobs];
  host.writeFile(path, host.zipEntries(entries));
}

// Path-only JSON save: config + paths, no embedded binaries (the snapshot
// already stripped the blobs). On load the ROM is re-read from romPath, SRAM
// from the sibling `<rom>.sav`, and kits are recompiled from their samples.
export function saveProjectFile(host: ProjectHost, path: string): void {
  const snap = host.snapshotProjectConfig();
  host.writeFile(path, enc.encode(snap.config));
}

// Inverse of the two saves, autodetecting zip vs path-only JSON. Refuses a
// project stamped newer than this build (detection, not migration).
export function loadRplg(host: ProjectHost, path: string): number {
  const bytes = host.readFile(path);

  let configJson: string;
  let blobs: Blob[] = [];
  if (isZip(bytes)) {
    const entries = host.unzipEntries(bytes);
    const cfg = entries.find(e => e.name === PROJECT_JSON);
    if (!cfg) throw new Error(`loadRplg: no ${PROJECT_JSON} entry in ${path}`);
    configJson = dec.decode(cfg.bytes);
    blobs = entries.filter(e => isBlobEntry(e.name));
  } else {
    configJson = dec.decode(bytes);
  }

  const parsed = JSON.parse(configJson) as ProjectConfig;
  const version = parseProjectVersion(parsed.schemaVersion ?? "");
  if (checkVersion(version, K_PROJECT) === VersionCheck.Newer) {
    throw new Error(
      `loadRplg: project schema v${version} is newer than this build (v${K_PROJECT})`);
  }

  return host.applyProjectConfig(configJson, blobs);
}
