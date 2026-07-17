// UI-side seam for background audio rendering. The render itself runs natively (a RenderHost on its own
// worker thread; see packages/native/src/host/render) — this module only snapshots the live system's state
// and drives the __rp_* hooks the editor binds (PluginUI.cpp). Inert (no-ops / empty) in the headless
// harness where the hooks are absent, exactly like openPath / the file browser.

import type { SystemView } from "../../src/systemsStore";
import type { SplitMode } from "../../src/render";

/** The backend methods startSystemRender needs — all on the plugin's control-plane channel (host +
 *  emulator facets), so it accepts either Backend or the narrower ControlPlaneBackend structurally. */
interface RenderBackend {
  configDir(): string;
  readSram(id: number): Uint8Array | null;
  readState(id: number): Uint8Array | null;
  writeFileAtomic(path: string, bytes: Uint8Array): boolean;
}

/** One background render job's status, as __rp_getRenderJobs reports it (mirrors RenderJobRegistry::Status). */
export interface RenderJobStatus {
  id: number;
  systemId: number;
  state: "rendering" | "done" | "error" | "cancelled";
  progress: number; // 0..1
  message: string; // error detail (state === "error")
}

interface RenderHooks {
  __rp_startRender?: (systemId: number, specJson: string) => number;
  __rp_cancelRender?: (jobId: number) => void;
  __rp_dismissRenderJob?: (jobId: number) => void;
  __rp_getRenderJobs?: () => RenderJobStatus[];
}

const hooks = globalThis as RenderHooks;

/** Snapshot the live system's persisted state (its battery SRAM, or a savestate if the cart has no battery)
 *  to a temp file, then start a background render of a FRESH instance built from it — never the running core.
 *  `outPath` is the chosen WAV path (a prefix for split modes, matching the CLI). Returns the job id, or null
 *  if it couldn't start (no on-disk ROM, or the hook is absent in the headless harness). */
export function startSystemRender(backend: RenderBackend, sys: SystemView, split: SplitMode, outPath: string): number | null {
  if (!hooks.__rp_startRender || !sys.romPath) return null;
  const spec: Record<string, unknown> = { rom: sys.romPath, out: outPath, split };

  // A fresh boot from the CURRENT state (not the on-disk sibling .sav): copy the live SRAM / savestate to a
  // temp file the render worker's own Engine loads by path.
  const dir = backend.configDir();
  if (sys.battery) {
    const sram = backend.readSram(sys.id);
    if (sram) {
      const tmp = `${dir}/.render-src-sys${sys.id}.sav`;
      if (backend.writeFileAtomic(tmp, sram)) spec.sav = tmp;
    }
  } else {
    const state = backend.readState(sys.id);
    if (state) {
      const tmp = `${dir}/.render-src-sys${sys.id}.ss0`;
      if (backend.writeFileAtomic(tmp, state)) spec.state = tmp;
    }
  }

  const id = hooks.__rp_startRender(sys.id, JSON.stringify(spec));
  return id > 0 ? id : null;
}

/** Request cancellation of a running render (cooperative — aborts at the next chunk). */
export function cancelRender(jobId: number): void {
  hooks.__rp_cancelRender?.(jobId);
}

/** Drop a finished job (the tile dismisses a completed/errored badge). */
export function dismissRenderJob(jobId: number): void {
  hooks.__rp_dismissRenderJob?.(jobId);
}

/** This editor's background render jobs (for the per-frame tile poll). Empty in the headless harness. */
export function getRenderJobs(): RenderJobStatus[] {
  return hooks.__rp_getRenderJobs?.() ?? [];
}

/** Pick the job a system's tile should badge: the in-progress render if any, else a lingering error, else
 *  none. (Done/cancelled jobs are dismissed by the caller, not badged.) Pure, so it's unit-testable. */
export function pickActiveRenderJob(jobs: readonly RenderJobStatus[], systemId: number): RenderJobStatus | null {
  let active: RenderJobStatus | null = null;
  for (const j of jobs) {
    if (j.systemId !== systemId) continue;
    if (j.state === "done" || j.state === "cancelled") continue;
    if (!active || (active.state !== "rendering" && j.state === "rendering")) active = j;
  }
  return active;
}
