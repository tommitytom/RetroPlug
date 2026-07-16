// The `.rplg.zip` blob contract — a pure port of native's ProjectBinaries. An exported
// project is a PKZIP of one thin `project.json` plus a deterministic entry per
// per-system binary: `systems/{i}/rom`, `.../sram`, `.../state`. TS assembles the
// entries and hands them to the native zip codec; native only compresses. On import
// the same keys route each blob back to its system by config index.
//
// This module is PURE (no Backend, no IO): key strings + entry-list transforms. The
// gather (live pump reads) and the framing live in ProjectStore, which has the backend.

import type { ZipEntry } from "./backend";

/** The thin config entry inside an exported archive. */
export const PROJECT_JSON = "project.json";

/** Per-system blob entry keys, addressed by config INDEX (systems don't exist yet at
 *  load). Kept in one place so the scan (projectMissing) and the framing agree. */
export const romKey = (i: number) => `systems/${i}/rom`;
export const sramKey = (i: number) => `systems/${i}/sram`;
export const stateKey = (i: number) => `systems/${i}/state`;

/** The set of blob keys present in an archive — fed to scanMissingFiles so a system
 *  whose save/state ships in the zip isn't flagged missing. Excludes `project.json`. */
export function blobKeysFromEntries(entries: ZipEntry[]): Set<string> {
  const out = new Set<string>();
  for (const e of entries) if (e.name !== PROJECT_JSON) out.add(e.name);
  return out;
}

/** Split an archive's entries into the config bytes + a key→bytes blob map. A missing
 *  `project.json` yields `config: null` (a malformed archive the caller rejects). */
export function partitionEntries(entries: ZipEntry[]): { config: Uint8Array | null; blobs: Map<string, Uint8Array> } {
  let config: Uint8Array | null = null;
  const blobs = new Map<string, Uint8Array>();
  for (const e of entries) {
    if (e.name === PROJECT_JSON) config = e.bytes;
    else blobs.set(e.name, e.bytes);
  }
  return { config, blobs };
}
