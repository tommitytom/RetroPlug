// The render snapshots a crash leaves (docs/known-bugs-batch-1-design.md section 4.2). A UI render writes the system's
// battery and, where the working song is not in it, a savestate into the config directory as
// `.render-src-<key>.<ext>` (ui/host/render.ts), and the submission lists them as its own (EngineRunOptions
// discardFiles), so the engine's pool deletes them however the job ends, and the UI deletes them again when the job
// settles. A process that dies leaves them: nothing of that job is left to delete anything, and the config directory
// has no owner between processes (the standalones, every plugin instance and the CLI share it). So the store graph
// sweeps it once at startup, and takes only what no live job can own any more.
//
// The age is WALL time, not work done, so the rule is not proof that a file's job is gone: a render queued behind the
// engine's budget while the machine suspends for a day, or a clock that jumps (an NTP correction on an RTC-less Pi or
// handheld), can age a live job's snapshot past the bound, and another instance starting then deletes it. That costs
// the render it belonged to: a render whose snapshot went refuses to boot rather than booting blank
// (RenderOpts.inputsMustExist), unless it is already past that check, the window ui/host/render.ts names, where the
// boot finds no sav to put and renders the cart's blank start. The other side is a file nothing of its job is left to
// delete (an instance that ended while TS still held the job, or a crash), which waits up to the bound before this
// takes it.

import type { HostBackend } from "./backend";

/** How old a render snapshot must be before the sweep deletes it. A queued render waits behind the engine's worker
 *  budget, which is seconds to minutes per render ahead of it, so a file of a live job's is never a day old: not by the
 *  work done in that time, which is what the module comment's caveat is about. */
export const RENDER_SNAPSHOT_MAX_AGE_MS = 24 * 60 * 60 * 1000;

/** The name prefix a render's snapshots carry, built by startSystemRender from the job's key. Its native twin is
 *  kRenderSnapshotPrefix (host/engine/OfflineJobs.hpp), which is what the pool will delete on a submission's behalf, so
 *  the two must move together. */
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
