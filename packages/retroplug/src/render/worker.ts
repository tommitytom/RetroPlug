// The render job's body: the session an engine job's worker (the RenderHost, on a pool thread of the engine) runs,
// which is also `retroplug-cli render`'s render path. It is compiled to a self-contained global-code bundle
// (rp_render_worker_bundle) and run on a bare-QuickJS runtime with its own Engine. The job request arrives as JSON in
// globalThis[Symbol.for("plugin")].args[0]; the worker boots a control plane, runs the shared render library, and
// reports progress / cancellation / completion back through native thunks the RenderHost binds:
//   __rp_reportRenderedMs(ms)            - audio rendered so far, per rendered chunk
//   __rp_isRenderCancelled(): boolean    - polled between boot steps and per chunk; true aborts the render
//   __rp_renderResult(status, message, outputsJson) - "done" | "cancelled" | "error"
// (all optional-chained so the same bundle stays runnable under a plain host that binds none of them).

import { bootSession } from "../bootSession";
import { runRenderJob, RenderCancelled, type RenderOpts, type SplitMode } from "./index";

declare const __rp_reportRenderedMs: ((ms: number) => void) | undefined;
declare const __rp_isRenderCancelled: (() => boolean) | undefined;
declare const __rp_renderResult: ((status: string, message: string, outputs: string[]) => void) | undefined;

/** The wire shape the RenderHost sends: a RenderOpts with the required fields optional (the worker fills
 *  the same defaults parseRenderArgs uses) so the native caller only has to specify what it cares about. */
type RenderJobSpec = Partial<RenderOpts> & { rom: string };

/** Normalize a partial job spec into a complete RenderOpts (mirrors parseRenderArgs' defaults). */
function normalize(spec: RenderJobSpec): RenderOpts {
  return {
    rom: spec.rom,
    sav: spec.sav,
    state: spec.state,
    out: spec.out,
    durationMs: spec.durationMs,
    maxDurationMs: spec.maxDurationMs ?? 600000, // 10 min default cap
    sampleRate: spec.sampleRate,
    split: (spec.split as SplitMode) ?? "mix",
    onExists: spec.onExists ?? "overwrite",
    bpm: spec.bpm,
    transport: spec.transport ?? false,
    start: spec.start ?? true,
    song: spec.song,
    songIndex: spec.songIndex,
    // How the instance is configured, when the caller had one to copy (the UI's System > Render). Absent
    // for a CLI render, which boots the ROM as it ships.
    roles: spec.roles,
    gainDb: spec.gainDb,
    pullMs: spec.pullMs, // a test knob (RenderOpts.pullMs)
    // A UI render's spec says its snapshots must be there when the render boots, which can be after this job waited
    // behind the engine's budget; a CLI render's `--sav` is its own to name a file that does not exist yet.
    inputsMustExist: spec.inputsMustExist,
    listSongs: false, // the worker only renders — --list-songs is a CLI-only query
  };
}

/** What the RenderHost puts on globalThis[Symbol.for("plugin")]: the job spec, and its run's serial, which no other run
 *  in the process has. */
type PluginNamespace = { args?: string[]; renderSerial?: number };
function pluginNamespace(): PluginNamespace | undefined {
  return (globalThis as Record<symbol, unknown>)[Symbol.for("plugin")] as PluginNamespace | undefined;
}

function readSpec(): RenderJobSpec {
  const raw = pluginNamespace()?.args?.[0];
  if (!raw) throw new Error("render worker: no job spec (expected plugin.args[0])");
  const spec = JSON.parse(raw) as RenderJobSpec;
  if (!spec || typeof spec.rom !== "string") throw new Error("render worker: job spec missing 'rom'");
  return spec;
}

function main(): void {
  let opts: RenderOpts;
  try {
    opts = normalize(readSpec());
  } catch (e) {
    __rp_renderResult?.("error", (e as Error)?.message ?? String(e), []);
    return;
  }

  const serial = pluginNamespace()?.renderSerial;
  const session = bootSession(); // structurally satisfies RenderContext (backend / project / audio / registry)
  // A render is an EXPORT. It builds systems, may load a savestate into one, and drops them again, so the
  // before-teardown battery flush would turn an export into a write in the user's save folder.
  session.project.systems.setBatteryFlush(false);
  try {
    const ctx = { backend: session.backend, project: session.project, audio: session.audio, registry: session.registry };
    const result = runRenderJob(ctx, opts, {
      onRendered: (ms) => __rp_reportRenderedMs?.(ms),
      isCancelled: () => __rp_isRenderCancelled?.() ?? false,
      partTag: serial !== undefined ? `r${serial}` : undefined,
    });
    __rp_renderResult?.("done", "", result.outputs);
  } catch (e) {
    if (e instanceof RenderCancelled) {
      __rp_renderResult?.("cancelled", "", []);
      return;
    }
    __rp_renderResult?.("error", (e as Error)?.message ?? String(e), []);
  }
}

main();
