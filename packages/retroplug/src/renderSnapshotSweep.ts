// The render snapshots a crash leaves (docs/known-bugs-batch-1-design.md section 4.2). A UI render writes the system's
// battery and, where the working song is not in it, a savestate into the config directory as
// `.render-src-<key>.<ext>` (ui/host/render.ts), and the submission lists them as its own (EngineRunOptions
// discardFiles), so the engine's pool deletes them however the job ends. A process that dies leaves them: nothing of
// that job is left to delete anything, and the config directory has no owner between processes (the standalones, every
// plugin instance and the CLI share it). So the store graph sweeps it once at startup, and takes only what no live job
// can own any more.

import type { HostBackend } from "./backend";

/** How old a render snapshot must be before the sweep deletes it. A queued render waits behind the engine's worker
 *  budget, which is seconds to minutes per render ahead of it, so a file of a live job's is never a day old. */
export const RENDER_SNAPSHOT_MAX_AGE_MS = 24 * 60 * 60 * 1000;

/** The name prefix a render's snapshots carry, built by startSystemRender from the job's key. */
export const RENDER_SNAPSHOT_PREFIX = ".render-src-";

/** Delete every render snapshot in `dir` older than RENDER_SNAPSHOT_MAX_AGE_MS, and answer how many went. A name that
 *  is not a snapshot, a subdirectory (listDir marks one with a trailing '/'), a file that cannot be aged and one that
 *  cannot be deleted are all left alone: this is a backstop for what a crash leaves, never a cleanup that may take
 *  what a live render is about to boot. */
export function sweepRenderSnapshots(
  backend: Pick<HostBackend, "listDir" | "fileAgeMs" | "deleteFile">,
  dir: string,
): number {
  let removed = 0;
  for (const name of backend.listDir(dir)) {
    if (!name.startsWith(RENDER_SNAPSHOT_PREFIX) || name.endsWith("/")) continue;
    const path = `${dir}/${name}`;
    const age = backend.fileAgeMs(path);
    if (age === null || age <= RENDER_SNAPSHOT_MAX_AGE_MS) continue;
    if (backend.deleteFile(path)) removed++;
  }
  return removed;
}
