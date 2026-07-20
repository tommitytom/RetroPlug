// UI-side seam for background audio rendering. The render itself runs natively (a RenderHost on its own
// worker thread; see packages/native/src/host/render) — this module only snapshots the live system's state
// and drives the __rp_* hooks the editor binds (PluginUI.cpp). Inert (no-ops / empty) in the headless
// harness where the hooks are absent, exactly like openPath / the file browser.

import type { SystemView } from "../../src/systemsStore";
import type { SplitMode } from "../../src/render";
import { resolveSongCatalog } from "../../src/tracker";
import { stem } from "../../src/pathUtil";

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

/** The render-menu selections carried into a render (from the persisted userConfig.render). */
export interface RenderRequest {
  split: SplitMode;
  sampleRate?: number; // undefined = engine default (44100)
  maxDurationMs?: number; // bounds the render (LSDj auto-length cap + fixed-render length)
}

/** Snapshot the live system's persisted state (its battery SRAM, or a savestate if the cart has no battery)
 *  to a temp file, then start a background render of a FRESH instance built from it — never the running core.
 *  `outPath` is the chosen WAV path (a prefix for split modes, matching the CLI). Returns the job id, or null
 *  if it couldn't start (no on-disk ROM, or the hook is absent in the headless harness). */
export function startSystemRender(backend: RenderBackend, sys: SystemView, req: RenderRequest, outPath: string): number | null {
  if (!hooks.__rp_startRender || !sys.romPath) return null;
  const spec: Record<string, unknown> = { rom: sys.romPath, out: outPath, split: req.split };
  if (req.sampleRate !== undefined) spec.sampleRate = req.sampleRate;
  if (req.maxDurationMs !== undefined) spec.maxDurationMs = req.maxDurationMs;

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

/** The base filename a render should default to: the loaded tracker cart's WORKING song name (LSDj / risa),
 *  else the ROM's stem. A non-tracker ROM has no song catalog (resolveSongCatalog → undefined), so it falls
 *  back to the ROM name — same synchronous read ProjectStore.currentSong() uses for the recents label. The
 *  caller sanitizes it into a filename (song names can carry spaces). */
export function renderBaseName(backend: Pick<RenderBackend, "readSram">, sys: SystemView): string {
  const catalog = resolveSongCatalog(sys.roles); // undefined for a non-tracker ROM
  const sram = catalog ? backend.readSram(sys.id) : null; // control-plane snapshot read
  const song = sram ? catalog!.workingName(sram) : null; // pure byte parse of the header name
  return song || stem(sys.romPath) || "render";
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

/** The split modes valid for a system's platform: mix always; channels on GB/NES; pins on NES only. Used to
 *  gate the Render menu's Split selector + to clamp a stored split to the system being rendered. */
export function validSplits(sys: Pick<SystemView, "core" | "platform">): SplitMode[] {
  const out: SplitMode[] = ["mix"];
  if (sys.core === "sameboy" || sys.platform === "nes") out.push("channels");
  if (sys.platform === "nes") out.push("pins");
  return out;
}

/** Seconds → "M:SS" for the max-duration selector label. */
export function formatDuration(sec: number): string {
  const m = Math.floor(sec / 60);
  const s = Math.floor(sec % 60);
  return `${m}:${s.toString().padStart(2, "0")}`;
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
