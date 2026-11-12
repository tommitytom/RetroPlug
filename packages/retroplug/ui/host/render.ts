// UI-side seam for background audio rendering. The render itself is an engine job (backend.engineRun, run by
// the render library on a worker of the engine's offline pool) - this module only snapshots the live system's
// state, submits the job through the store graph's RenderJobs list, and holds the pure helpers the Render menu and
// the tile badge share.

import type { SystemView } from "../../src/systemsStore";
import type { SplitMode, OnExists } from "../../src/render";
import { resolveSongCatalog } from "../../src/trackerRegistry";
import { consoleOfPlatform } from "../../src/consoles";
import { stem } from "../../src/pathUtil";
import { resolveSavPath } from "../../src/savPaths";
import { RENDER_SNAPSHOT_PREFIX } from "../../src/renderSnapshotSweep";
import type { RenderJobs, RenderJobStatus } from "../../src/renderJobs";

export type { RenderJobStatus };

/** The backend methods startSystemRender needs — all on the plugin's control-plane channel (host +
 *  emulator facets), so it accepts either Backend or the narrower ControlPlaneBackend structurally. */
interface RenderBackend {
  configDir(): string;
  readSram(id: number): Uint8Array | null;
  readState(id: number): Uint8Array | null;
  writeFileAtomic(path: string, bytes: Uint8Array): boolean;
  deleteFile(path: string): boolean;
}

/** The render-menu selections carried into a render (from the persisted userConfig.render). */
export interface RenderRequest {
  split: SplitMode;
  sampleRate?: number; // undefined = engine default (44100)
  maxDurationMs?: number; // bounds the render (LSDj auto-length cap + fixed-render length)
  onExists?: OnExists; // "overwrite" (default) or "rename" to the next free name
}

/** Snapshot the live system's persisted state (its battery SRAM, or a savestate if the cart has no battery)
 *  to temp files of the render's own, then start a background render (an engine job, listed in `renders`) of a FRESH
 *  instance built from them, never the running core.
 *  The instance's CONFIGURATION (its roles + gain) rides along, so "fresh" means a new core set up the way
 *  this one is, not one at the format's defaults. `outPath` is the chosen WAV path (a prefix for split modes,
 *  matching the CLI). Returns the job's id in `renders`, or null if it couldn't start (no on-disk ROM). */
export function startSystemRender(
  backend: RenderBackend,
  renders: Pick<RenderJobs, "start">,
  sys: SystemView,
  req: RenderRequest,
  outPath: string,
): number | null {
  if (!sys.romPath) return null;
  const spec: Record<string, unknown> = { rom: sys.romPath, out: outPath, split: req.split };
  if (req.sampleRate !== undefined) spec.sampleRate = req.sampleRate;
  if (req.maxDurationMs !== undefined) spec.maxDurationMs = req.maxDurationMs;
  if (req.onExists !== undefined) spec.onExists = req.onExists;

  // How this instance is CONFIGURED, so the offline core comes up the way you are hearing it. The render
  // boots a fresh system from the ROM on disk; without these it would take each role's schema defaults and
  // quietly render a PAL project at NTSC, a Game Boy as the wrong model, a replaced DMC kit from the
  // original samples, or an expansion chip at unity whatever the instance's Expansion Volume says. The
  // ROM's own bytes still come from disk - the asset overrides are re-folded at construct, by the same
  // patcher the live cart used, so they land the same way.
  if (sys.roles.length) spec.roles = sys.roles;
  if (sys.settings.gainDb) spec.gainDb = sys.settings.gainDb;

  // A fresh boot from the CURRENT state (not the on-disk sibling .sav): copy the live SRAM / savestate to temp files
  // the render worker's own Engine loads by path. The worker reads them when it boots the render, which is when a
  // worker is free: renders queue behind the engine's budget, so that can be minutes on, after further edits and
  // renders. So each render's files are its own, named by the job's key (unique across instances, which share the
  // config directory), and they go with the job: the submission lists them as its discardFiles, which the engine's
  // pool deletes however the job ends, an instance that ends or a crash before the job settled included (the startup
  // sweep is the backstop for what a crash leaves). A snapshot that cannot be written refuses the render, listed as an
  // error, rather than leave the field out and boot the sibling .sav on disk instead of what you hear.
  const dir = backend.configDir();
  const snapshots: string[] = [];
  // Whether the spec reached the submission. The files are the engine's pool's from the moment they ride it (the job's
  // discardFiles, which the pool deletes however the job ends), so the one road left to this module is a spec that
  // threw: a snapshot whose write failed leaves the ones before it written, and nothing was submitted to delete them.
  let built = false;
  const prepare = (key: string): object => {
    const snapshot = (ext: string, bytes: Uint8Array): string => {
      const path = `${dir}/${RENDER_SNAPSHOT_PREFIX}${key}.${ext}`;
      snapshots.push(path);
      if (!backend.writeFileAtomic(path, bytes))
        throw new Error(`render: could not snapshot the system's state to ${path}`);
      return path;
    };
    if (sys.battery) {
      const sram = backend.readSram(sys.id);
      if (sram) spec.sav = snapshot("sav", sram);
      // ...and for a cart whose WORKING SONG is not in the battery (smsggdj: it lives in work RAM and the
      // cart boots blank on purpose), the battery does not describe what you are hearing - a fresh boot from
      // it renders silence. Carry a savestate as well, which does capture work RAM, so the render starts
      // from the song that is actually loaded. Additive: `state` is applied after the sav at construct, and
      // no other console sets this flag, so nothing else changes.
      if (resolveSongCatalog(sys.roles)?.workingSongOutsideBattery) {
        const state = backend.readState(sys.id);
        if (state) spec.state = snapshot("ss0", state);
      }
    } else {
      const state = backend.readState(sys.id);
      if (state) spec.state = snapshot("ss0", state);
    }
    built = true;
    return spec;
  };
  return renders.start(
    sys.id,
    prepare,
    () => {
      if (!built) snapshots.forEach((p) => backend.deleteFile(p));
    },
    () => snapshots,
  );
}

/** The base filename a render should default to, in priority order: the loaded tracker cart's WORKING song
 *  name (LSDj / risa) → the loaded SAV's basename (a battery cart) → the ROM's stem. A non-tracker ROM has
 *  no song catalog (resolveSongCatalog → undefined); a battery cart's sav name is usually the ROM stem but
 *  differs when a custom .sav is loaded. The caller sanitizes it into a filename (names can carry spaces). */
export function renderBaseName(backend: Pick<RenderBackend, "readSram">, sys: SystemView): string {
  const catalog = resolveSongCatalog(sys.roles); // undefined for a non-tracker ROM
  const sram = catalog ? backend.readSram(sys.id) : null; // control-plane snapshot read
  const song = sram ? catalog!.workingName(sram) : null; // pure byte parse of the header name
  const savName = sys.battery ? stem(resolveSavPath(sys.romPath, sys.savSuffix, sys.savPath)) : null;
  return song || savName || stem(sys.romPath) || "render";
}

/** The split modes valid for a system's platform: mix always, then each layout the console descriptor declares
 *  (`splits.channels` / `splits.pins`: the Game Boy's four channels, the NES's five and its pins, nothing for the GBA
 *  or Sega consoles). Used to gate the Render menu's Split selector and to clamp a stored split to the system being
 *  rendered. */
export function validSplits(sys: Pick<SystemView, "platform">): SplitMode[] {
  const splits = consoleOfPlatform(sys.platform)?.splits;
  const out: SplitMode[] = ["mix"];
  if (splits?.channels) out.push("channels");
  if (splits?.pins) out.push("pins");
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

/** The tile badge's text for a job: how much audio has been rendered so far, and what tapping does. There is
 *  deliberately NO percentage - a tracker render runs to the song's HFF stop, whose position isn't known
 *  until it lands, so the only honest number is the duration rendered. Doubles as the tile's re-render key
 *  (it changes once a second), so it must depend on nothing but the job. */
export function renderBadgeLabel(job: RenderJobStatus): string {
  if (job.state !== "rendering") return " render failed (tap to dismiss)";
  return ` rendering ${formatDuration(job.renderedMs / 1000)} (tap to cancel)`;
}
