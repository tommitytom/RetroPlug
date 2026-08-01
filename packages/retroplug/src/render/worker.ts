// The background render worker session. Compiled to a self-contained global-code bundle
// (rp_render_worker_bundle) and run by the native RenderHost on a bare-QuickJS runtime with its own Engine
// — a headless equivalent of `retroplug-cli render`, but driven in-process on a worker thread. The job
// request arrives as JSON in globalThis[Symbol.for("plugin")].args[0]; the worker boots a control plane,
// runs the shared render library, and reports progress / cancellation / completion back through native
// thunks the RenderHost binds:
//   __rp_reportRenderProgress(fraction)  — 0..1, per rendered chunk
//   __rp_isRenderCancelled(): boolean    — polled per chunk; true aborts the render
//   __rp_renderResult(status, message, outputsJson) — "done" | "cancelled" | "error"
// (all optional-chained so the same bundle stays runnable under a plain host that binds none of them).

import { bootSession } from "../bootSession";
import { runRenderJob, RenderCancelled, type RenderOpts, type SplitMode } from "./index";

declare const __rp_reportRenderProgress: ((fraction: number) => void) | undefined;
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
    listSongs: false, // the worker only renders — --list-songs is a CLI-only query
  };
}

function readSpec(): RenderJobSpec {
  const ns = (globalThis as Record<symbol, unknown>)[Symbol.for("plugin")] as { args?: string[] } | undefined;
  const raw = ns?.args?.[0];
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

  const session = bootSession(); // structurally satisfies RenderContext (backend / project / dsp / audio)
  try {
    const result = runRenderJob(session, opts, {
      onProgress: (f) => __rp_reportRenderProgress?.(f),
      isCancelled: () => __rp_isRenderCancelled?.() ?? false,
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
