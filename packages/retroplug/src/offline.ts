// Offline jobs (docs/modular-graph-phase-d2-design.md section 3): a graph document run to completion on a host of its
// own, apart from the live engine, with the buffers it recorded handed back as a promise. Native takes or refuses a job
// at once (submitOfflineJob), runs it on a worker thread of the engine's offline pool, and hands its completion back
// later (drainOfflineJobs, from a bounded queue). This is the promise side of that; only the real backend has it, since
// only a native host runs graphs (the mock rejects).
//
// Engine jobs (docs/modular-graph-phase-d3-design.md section 3) ride the same queue: a script bundle of a kind (the
// one is "render") run on a worker of the same pool with a runtime and an Engine of its own (submitEngineJob).
// Native's one drain hands back both kinds' completions, and every running engine job's progress and console lines,
// so one queue here holds both kinds' promises. An engine job's promise settles with its status ("done", "cancelled"
// or "error") rather than rejecting on a failure: a render that fails has written nothing, and says why in its
// message, and only a refusal (or a port that throws) rejects it. Each handle, of either kind, can cancel its job: a
// run still held here settles at once, and a submitted one through native's cancel, settled by a later drain like any
// completion (a graph job's promise then rejects with the reason "cancelled", an engine job's resolves with that
// status).
//
// The poll follows the capability jobs' (src/capability.ts), whose live set it joins, so one pump settles both kinds:
// once right after each submission, from the UI's frame tick (ui/app/useJobPump.ts, through backend.pumpJobs) while
// anything is pending, and after each audio-driver call that lets time pass (pumpAllJobs). What it cannot share is the
// capability jobs' wait: a capability submission blocks until its job settles, which would run offline jobs one at a
// time, where a kit compile wants its slots in parallel. So in a host with no UI tick (the CLI, the Node addon, the
// test host: the audio driver turns the wait on as it is made, setOfflineWait) a submission starts a poll that runs as
// a microtask chain, one drain a step, each step queued behind whatever microtasks the settled promises queued. A step
// drains without waiting while the chain is busy, and only after QUIET_STEPS steps in a row that settled nothing and
// saw no submission does one block in native (drainOfflineJobs waits up to the wait for a completion while a job is in
// flight): a caller that awaits through a few async layers before submitting its next job reaches the submission in
// those quiet steps, rather than finding the chain already asleep until some other job completes. Everything submitted
// in one synchronous run is in flight at once, a chain of jobs keeps moving, and `await offlineRun(...)` settles with
// no timer at all. A host whose UI tick pumps never turns the wait on, so no step ever blocks its UI thread. In a host
// with no tick, one `host.pump()` does not return until nothing is pending: libuv timers and I/O do not run meanwhile,
// and a CLI's Ctrl-C between pumps is not seen. That is milliseconds for a kit; a minute-long render yields to the
// host's loop after each waited step where the host has timers (setOfflineYield, which the CLI's render sets, and the
// audio driver on Node, so an awaited job there leaves Node's timers and I/O running).
//
// Native holds at most OFFLINE_MAX_JOBS outstanding per engine and refuses a submission past that as busy. The queue
// surfaces that only after a drain that threw: one runtime's pending promises are exactly its engine's outstanding jobs
// (each pending id is one job native holds until a drain hands it back; after a drain that threw, the jobs native still
// holds are no longer counted here), so a run made while OFFLINE_MAX_JOBS are pending is held here, in submission
// order, and submitted as completions make room. A held job is refused (its other refusals) only when it is
// submitted.

import { setQueueLive, type JobQueue } from "./capability";
import {
  OfflineRunError,
  type OfflineCap,
  type OfflineInputReport,
  type OfflineOutput,
  type OfflineRunRequest,
  type OfflineRunResult,
} from "@retroplug/core/offlineJob";

// The shape of a job, its inputs and its results lives in core (packages/core/ts/offlineJob.ts), the engine's job API;
// this module owns the queue that runs them and re-exports the shape, so its callers keep one spelling.
export {
  offlineInputRef,
  offlineOutputRef,
  type OfflineBuffer,
  type OfflineCap,
  type OfflineCapArithmetic,
  type OfflineFileInput,
  type OfflineInput,
  type OfflineInputReport,
  type OfflineOutput,
  type OfflineRunRequest,
  type OfflineRunResult,
  type OfflineSamplesInput,
} from "@retroplug/core/offlineJob";

// The failure type a job rejects with is the job shape's (core/offlineJob.ts), so a package's compiler can catch it
// without the shell; re-exported here beside the queue that raises it.
export { OfflineRunError };

// The wire shapes (native BackendTypes.hpp: RpcOfflineRequest, RpcOfflineSubmitResult, RpcOfflineCompletion). An
// absent optional is left out, which native reads as nullopt.
export interface OfflineWireInput {
  samples?: Uint8Array;
  channels?: number;
  rate?: number;
  path?: string;
  offset?: number;
  length?: number;
  cap?: OfflineCap;
}
export interface OfflineWireRequest {
  document?: string;
  inputs: OfflineWireInput[];
  outputs: OfflineOutput[];
  returns: number[];
  frames: number;
  rate: number;
}
export interface OfflineSubmitResult {
  id?: number | null;
  error?: string | null;
}
export interface OfflineWireBuffer {
  handle: number;
  channels: number;
  frames: number;
  rate: number;
  samples: Uint8Array;
  nonFinite: boolean;
}
export interface OfflineWireCompletion {
  id: number;
  ok: boolean;
  error: string;
  buffers: OfflineWireBuffer[];
  inputs: OfflineInputReport[];
  guardsFired: number;
  guardReason: string;
}

/** The kinds of engine job: the embedded script bundles a job can run. */
export type EngineJobKind = "render";

/** How an engine job ended: it ran to its end ("done"), was cancelled, or failed ("error", with its message). */
export type EngineRunStatus = "done" | "cancelled" | "error";

/** A finished engine job: its status, the error's message ("" otherwise), the files it wrote, and the milliseconds of
 *  audio it ended at: for a render, the length it wrote, which for one that detected its song's end can be below the
 *  most onProgress reported (that count included the chunks held back past the stop). */
export interface EngineRunResult {
  status: EngineRunStatus;
  message: string;
  outputs: string[];
  renderedMs: number;
}

/** One console write a job's script made (console.log and the rest), without its final newline, and its stream. A
 *  write can hold newlines of its own: a multi-line string, or a script exception's report with its stack. */
export interface EngineLogLine {
  stream: "stdout" | "stderr";
  text: string;
}

/** What an engine job reports while it runs, delivered from the pump that drains it. */
export interface EngineRunOptions {
  /** The milliseconds of audio rendered so far, each time it has risen since the last report: a running maximum,
   *  never going back. A render that detects its song's end lowers its own count once, at the end, to the length it
   *  wrote; that length is the result's `renderedMs`, not a last call here. */
  onProgress?: (renderedMs: number) => void;
  /** Each console write the job's script made, in order. Native keeps a bounded record between drains
   *  (EngineJobRecord: 256 lines, 64 KiB); writes past it were dropped, and arrive as one stderr line saying how
   *  many. */
  onLog?: (line: EngineLogLine) => void;
  /** Files the submission owns, which the engine's pool deletes however the job ends: on the job's worker once its
   *  body has returned and before its completion is queued, and with the job itself on a road that drops it unrun (a
   *  refusal, a cancel while it waits in the queue, the pool's teardown). So they are gone by the time the job settles,
   *  and a caller does not delete them itself. Never a user's own file (a `--sav` handed to a render): a UI render
   *  lists the snapshots it wrote into the config directory, which every instance shares. */
  discardFiles?: string[];
}

/** An engine job's console lines on this runtime's own: stderr to console.error, the rest to console.log. */
export function echoJobLog(line: EngineLogLine): void {
  if (line.stream === "stderr") console.error(line.text);
  else console.log(line.text);
}

/** A job's promise, with a way to cancel the job. `cancel()` answers whether there was a job to cancel: true for one
 *  held here or queued or running in native, which then ends cancelled (a graph job's promise rejects with the reason
 *  "cancelled", an engine job's resolves with that status); false once it has finished. */
export interface JobHandle<T> extends Promise<T> {
  cancel(): boolean;
}
export type OfflineRunHandle = JobHandle<OfflineRunResult>;
export type EngineRunHandle = JobHandle<EngineRunResult>;

/** `promise` as a handle whose cancel is `cancel`. */
export function jobHandle<T>(promise: Promise<T>, cancel: () => boolean): JobHandle<T> {
  return Object.assign(promise, { cancel });
}

// The engine job wire shapes (native BackendTypes.hpp: RpcEngineJobRequest, RpcEngineJobProgress,
// RpcEngineJobCompletion).
export interface EngineWireRequest {
  kind: string;
  spec: string;
  /** Always present, an empty list when the caller lists none: native reads the request without tolerating a missing
   *  field (rfl), and an empty list deletes nothing. */
  discardFiles: string[];
}
export interface EngineWireProgress {
  id: number;
  renderedMs: number;
  lines: EngineLogLine[];
  droppedLines: number;
}
export interface EngineWireCompletion extends EngineWireProgress {
  status: string;
  message: string;
  outputs: string[];
}

/** What drainOfflineJobs answers: the graph jobs' completions, how many of the engine's jobs (of both kinds) are
 *  still queued or running, the engine jobs' completions, and every running engine job's progress. */
export interface OfflineWireDrain {
  completions: OfflineWireCompletion[];
  inFlight: number;
  engineCompletions?: EngineWireCompletion[];
  engineProgress?: EngineWireProgress[];
}

/** The native calls the jobs ride: what a backend hands OfflineJobs. */
export interface OfflinePort {
  submitOfflineJob(request: OfflineWireRequest): OfflineSubmitResult;
  submitEngineJob(request: EngineWireRequest): OfflineSubmitResult;
  drainOfflineJobs(waitMs: number): OfflineWireDrain;
  cancelOfflineJob(id: number): boolean;
}

/** Native's bound on one engine's outstanding offline jobs (OfflineJobs::kMaxJobs): past it the queue holds runs
 *  back. */
export const OFFLINE_MAX_JOBS = 64;

// How many quiet steps in a row (nothing settled, nothing submitted) the poll takes before one waits in native. Each
// step gives a chain one more microtask hop, so a chain whose next submission comes more than about this many hops
// after its completion falls back to waiting for the next completion, or for the wait itself.
const QUIET_STEPS = 8;

// How long one poll step may wait in native for a completion, or 0 for no poll at all (a host whose UI tick pumps).
let offlineWaitMs = 0;

/** Turn the poll on (a wait per step, in ms) or off (0): the audio driver turns it on as it is made, in the hosts
 *  with no UI tick. */
export function setOfflineWait(ms: number): void {
  offlineWaitMs = ms > 0 ? ms : 0;
}

// How a host with timers lets its event loop run between waited steps, or null for a pure microtask chain.
let offlineYield: ((resume: () => void) => void) | null = null;

/** Make the poll hand control back to the host's event loop after each step that waited in native: `yieldTo(resume)`
 *  must call `resume` from a later macrotask (a CLI passes setTimeout(resume, 0)). Without it, one `host.pump()`
 *  does not return until nothing is pending, so a minute-long render holds libuv's timers, I/O and a Ctrl-C. A host
 *  with no timers (the native test host never fires setTimeout) must not set it, or the poll would never resume. */
export function setOfflineYield(yieldTo: ((resume: () => void) => void) | null): void {
  offlineYield = yieldTo;
}

function toWire(request: OfflineRunRequest): OfflineWireRequest {
  const inputs = (request.inputs ?? []).map((input): OfflineWireInput => {
    if ("samples" in input) {
      const s = input.samples;
      const bytes = new Uint8Array(s.buffer, s.byteOffset, s.byteLength);
      return { samples: bytes, channels: input.channels, rate: input.rate };
    }
    const w: OfflineWireInput = { path: input.path };
    if (input.offset !== undefined) w.offset = input.offset;
    if (input.length !== undefined) w.length = input.length;
    if (input.cap !== undefined) w.cap = input.cap;
    return w;
  });
  const outputs = request.outputs ?? [];
  const wire: OfflineWireRequest = {
    inputs,
    outputs,
    returns: request.returns ?? outputs.map((_, j) => inputs.length + j + 1),
    frames: request.frames,
    rate: request.rate,
  };
  if (request.document !== undefined) wire.document = request.document;
  return wire;
}

function floats(bytes: Uint8Array): Float32Array {
  // A copy, so the floats sit on a 4-byte boundary whatever offset the bytes arrived at.
  return bytes.byteLength ? new Float32Array(bytes.slice().buffer) : new Float32Array(0);
}

function asError(e: unknown): Error {
  return e instanceof Error ? e : new Error(String(e));
}

type Waiter = (
  | { kind: "graph"; resolve: (value: OfflineRunResult) => void }
  | { kind: "engine"; resolve: (value: EngineRunResult) => void; options: EngineRunOptions; renderedMs: number }
) & {
  reject: (error: Error) => void;
  id: number | null; // native's id once submitted
};

type Submission = { kind: "graph"; wire: OfflineWireRequest } | { kind: "engine"; wire: EngineWireRequest };

// A caller's callback that throws is the caller's bug, and must not strand the other promises the drain settles.
function report(callback: () => void): void {
  try {
    callback();
  } catch (e) {
    console.error(`an engine job's callback threw: ${asError(e).message}`);
  }
}

/** The promises over one queue of offline jobs, graph and engine: native's, which is one per engine. */
export class OfflineJobs implements JobQueue {
  private readonly pending = new Map<number, Waiter>();
  // Runs made while OFFLINE_MAX_JOBS were pending, oldest first, submitted as completions make room.
  private readonly held: { submission: Submission; waiter: Waiter }[] = [];
  private polling = false;
  private settling = false;   // a drain's results are being settled: an engine job's callback runs inside it
  private quiet = 0;          // the poll's steps in a row that settled nothing and saw no submission
  private submitted = false;  // a submission since the poll's last step

  constructor(private readonly port: OfflinePort) {}

  /** Run a graph job. Rejects with native's reason when it is refused (at once, or when a held run is submitted);
   *  otherwise settles with its completion, or rejects with an OfflineRunError carrying native's reason ("cancelled"
   *  for a cancel). Never rejects because native's bound is full: the run waits here for room instead. A drain that
   *  threw (settle) is the one exception: the jobs native still holds are no longer counted here, so a run submitted
   *  before they are drained can be refused busy. */
  run(request: OfflineRunRequest): OfflineRunHandle {
    let waiter: Waiter | null = null;
    const promise = new Promise<OfflineRunResult>(
      (resolve, reject) => (waiter = { kind: "graph", resolve, reject, id: null }),
    );
    return jobHandle(promise, this.start({ kind: "graph", wire: toWire(request) }, waiter!));
  }

  /** Run an engine job of `kind` over `spec` (a value is sent as its JSON). Rejects with native's reason when it is
   *  refused; otherwise settles with its status, message and outputs, a failure and a cancel included. Held here past
   *  native's bound like a graph job, the two kinds sharing it. */
  engine(kind: EngineJobKind, spec: object | string, options: EngineRunOptions = {}): EngineRunHandle {
    let waiter: Waiter | null = null;
    const promise = new Promise<EngineRunResult>(
      (resolve, reject) => (waiter = { kind: "engine", resolve, reject, id: null, options, renderedMs: 0 }),
    );
    const wire: EngineWireRequest = {
      kind,
      spec: typeof spec === "string" ? spec : JSON.stringify(spec),
      discardFiles: [...(options.discardFiles ?? [])],
    };
    return jobHandle(promise, this.start({ kind: "engine", wire }, waiter!));
  }

  /** Drain native's completions without waiting and settle their promises; nothing (no native call) while none is
   *  pending. Returns how many are still pending, held runs included. */
  pump(): number {
    return this.settle(0);
  }

  /** How many promises are waiting on a completion, held runs included. */
  pendingCount(): number {
    return this.outstanding();
  }

  private outstanding(): number {
    return this.pending.size + this.held.length;
  }

  // Submit (or hold) a run, start the poll, and answer its handle's cancel.
  private start(submission: Submission, waiter: Waiter): () => boolean {
    if (this.pending.size >= OFFLINE_MAX_JOBS) this.held.push({ submission, waiter });
    else this.submit(submission, waiter);
    if (this.outstanding() > 0) setQueueLive(this, true);
    this.pump();
    this.poll();
    return () => this.cancel(waiter);
  }

  private cancel(waiter: Waiter): boolean {
    const at = this.held.findIndex((h) => h.waiter === waiter);
    if (at >= 0) {
      // Never submitted: it settles here, as native would settle a queued job it cancelled.
      this.held.splice(at, 1);
      if (waiter.kind === "graph") waiter.reject(new OfflineRunError("cancelled", []));
      else waiter.resolve({ status: "cancelled", message: "", outputs: [], renderedMs: 0 });
      if (this.outstanding() === 0) setQueueLive(this, false);
      return true;
    }
    if (waiter.id == null || this.pending.get(waiter.id) !== waiter) return false;
    // Native completes it cancelled (a queued job at once, a running one at its next block or chunk), and the next
    // drain settles it, a pump's or the poll's.
    try {
      return this.port.cancelOfflineJob(waiter.id);
    } catch {
      return false;
    }
  }

  private submit(submission: Submission, waiter: Waiter): void {
    this.submitted = true;
    let submitted: OfflineSubmitResult;
    try {
      submitted =
        submission.kind === "graph"
          ? this.port.submitOfflineJob(submission.wire)
          : this.port.submitEngineJob(submission.wire);
    } catch (e) {
      // A request the port throws on (an RPC error, say) fails that run, never the queue: a held run reaches this from
      // settle's loop, where a throw would otherwise strand the rest of the promises.
      waiter.reject(asError(e));
      return;
    }
    if (submitted.id == null) {
      const what = submission.kind === "graph" ? "offline" : "engine";
      waiter.reject(new Error(submitted.error ?? `the ${what} job was refused`));
    } else {
      waiter.id = submitted.id;
      this.pending.set(submitted.id, waiter);
    }
  }

  // An engine job's lines and progress, as a drain reports them, to its callbacks.
  private progress(waiter: Waiter & { kind: "engine" }, p: EngineWireProgress): void {
    const { onLog, onProgress } = waiter.options;
    if (onLog) {
      for (const line of p.lines) report(() => onLog(line));
      if (p.droppedLines > 0) {
        const text = `[${p.droppedLines} console line${p.droppedLines === 1 ? "" : "s"} dropped]`;
        report(() => onLog({ stream: "stderr", text }));
      }
    }
    // A running maximum: a render's own count drops once, at its end, to the length it wrote (the result carries it).
    if (p.renderedMs > waiter.renderedMs) {
      waiter.renderedMs = p.renderedMs;
      if (onProgress) report(() => onProgress(p.renderedMs));
    }
  }

  private settle(waitMs: number): number {
    // An engine job's callbacks run synchronously inside a settle, and one may submit or pump (a progress callback
    // that starts a job, say): it submits, but the drain is left to this settle's caller, so one drain's results are
    // never interleaved with another's (a nested drain would find nothing in flight and take the jobs this one has
    // still to settle for lost).
    if (this.settling) return this.outstanding();
    this.settling = true;
    try {
      return this.drainAndSettle(waitMs);
    } finally {
      this.settling = false;
    }
  }

  private drainAndSettle(waitMs: number): number {
    if (this.pending.size === 0) {
      if (this.held.length === 0) setQueueLive(this, false);
      return this.outstanding();
    }
    let drained: OfflineWireDrain;
    try {
      drained = this.port.drainOfflineJobs(waitMs);
    } catch (e) {
      // The queue can no longer learn what happened to anything it holds: every waiting run fails with the port's
      // reason rather than being polled forever. Native may still hold those jobs until a later drain hands them back
      // and they are dropped, so until then its outstanding count and this queue's promises differ: a fan-out made
      // from here is refused rather than held if it passes the bound.
      const error = asError(e);
      const waiting = [...this.pending.values(), ...this.held.map((h) => h.waiter)];
      this.pending.clear();
      this.held.length = 0;
      for (const p of waiting) p.reject(error);
      setQueueLive(this, false);
      return 0;
    }
    // The jobs this drain answered for: one an engine job's callback submits below is not among them.
    const asked = new Set(this.pending.keys());
    for (const c of drained.completions) {
      const p = this.pending.get(c.id);
      if (!p || p.kind !== "graph") continue;
      this.pending.delete(c.id);
      if (!c.ok) {
        p.reject(new OfflineRunError(c.error, c.inputs));
        continue;
      }
      p.resolve({
        buffers: c.buffers.map((b) => ({
          handle: b.handle,
          channels: b.channels,
          frames: b.frames,
          rate: b.rate,
          samples: floats(b.samples),
          nonFinite: b.nonFinite,
        })),
        inputs: c.inputs,
        guardsFired: c.guardsFired,
        guardReason: c.guardReason,
      });
    }
    for (const p of drained.engineProgress ?? []) {
      const w = this.pending.get(p.id);
      if (w?.kind === "engine") this.progress(w, p);
    }
    for (const c of drained.engineCompletions ?? []) {
      const w = this.pending.get(c.id);
      if (w?.kind !== "engine") continue;
      this.pending.delete(c.id);
      this.progress(w, c); // its last lines, and the length it ended at
      const status = c.status as EngineRunStatus;
      w.resolve({ status, message: c.message, outputs: c.outputs, renderedMs: c.renderedMs });
    }
    // A promise whose job native no longer holds (none came back, and nothing is in flight) would otherwise be polled
    // forever: it cannot complete, so it fails. One runtime binding one engine never gets here.
    if (drained.inFlight === 0) {
      const lost = [...this.pending].filter(([id]) => asked.has(id));
      for (const [id] of lost) this.pending.delete(id);
      for (const [, p] of lost) p.reject(new Error("the engine holds no record of this offline job"));
    }
    while (this.held.length && this.pending.size < OFFLINE_MAX_JOBS) {
      const next = this.held.shift()!;
      this.submit(next.submission, next.waiter);
    }
    if (this.outstanding() === 0) setQueueLive(this, false);
    return this.outstanding();
  }

  // The no-UI-tick hosts' poll: a microtask chain, one step per turn, until nothing is pending. A step drains without
  // waiting unless the chain has been quiet for QUIET_STEPS steps; a step that waited starts the count again, so the
  // promises it settled get their quiet steps to submit what follows them. One chain per queue: a submission while one
  // runs joins it.
  private poll(): void {
    if (this.polling || offlineWaitMs === 0 || this.outstanding() === 0) return;
    this.polling = true;
    this.quiet = 0;
    const step = (): void => {
      if (offlineWaitMs === 0) {
        this.polling = false;
        return;
      }
      let again = false;
      let waited = false;
      try {
        const wait = this.submitted || this.quiet < QUIET_STEPS ? 0 : offlineWaitMs;
        const before = this.pending.size;
        this.submitted = false;
        waited = wait > 0;
        const left = this.settle(wait);
        if (left > 0) {
          if (wait > 0 || this.submitted || this.pending.size !== before) this.quiet = 0;
          else this.quiet++;
          again = true;
        }
      } finally {
        // Any exit that is not a requeue clears the flag, so a throw cannot leave the queue's poll switched off for
        // every later submission.
        if (!again) this.polling = false;
      }
      if (!again) return;
      if (waited && offlineYield) offlineYield(step);
      else void Promise.resolve().then(step);
    };
    void Promise.resolve().then(step);
  }
}
