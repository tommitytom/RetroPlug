// Offline jobs (docs/modular-graph-phase-d2-design.md section 3): a graph document run to completion on a host of its
// own, apart from the live engine, with the buffers it recorded handed back as a promise. Native takes or refuses a job
// at once (submitOfflineJob), runs it on a worker thread of the engine's offline pool, and hands its completion back
// later (drainOfflineJobs, from a bounded queue). This is the promise side of that; only the real backend has it, since
// only a native host runs graphs (the mock rejects).
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
// and a CLI's Ctrl-C between pumps is not seen. That is milliseconds for a kit; D3's minute-long renders will need a
// macrotask yield once a waited step returns in a host with timers.
//
// Native holds at most OFFLINE_MAX_JOBS outstanding per engine and refuses a submission past that as busy. The queue
// surfaces that only after a drain that threw: one runtime's pending promises are exactly its engine's outstanding jobs
// (each pending id is one job native holds until a drain hands it back; after a drain that threw, the jobs native still
// holds are no longer counted here), so a run made while OFFLINE_MAX_JOBS are pending is held here, in submission
// order, and submitted as completions make room. A held job is refused (its other refusals) only when it is
// submitted.

import { setQueueLive, type JobQueue } from "./capability";

/** How a file input's frame cap scales a frame count at a kit rate to the decoded rate: LSDj's window computes it in
 *  float with the ratio first ("float-ratio"), risa's in double with the product first ("double-scale"). */
export type OfflineCapArithmetic = "float-ratio" | "double-scale";

/** A file input's cap: at most `frames` frames at `rate`, scaled to the decoded rate as `arithmetic` says. */
export interface OfflineCap {
  frames: number;
  rate: number;
  arithmetic: OfflineCapArithmetic;
}

/** Audio the call carries: interleaved, `channels` wide, at `rate`. */
export interface OfflineSamplesInput {
  samples: Float32Array;
  channels: number;
  rate: number;
}

/** A file native decodes (the kit compiler's decode: mono, at the file's own rate) and windows: from `offset`
 *  (clamped to the end), `length` frames (absent or 0: to the end), at most the cap. */
export interface OfflineFileInput {
  path: string;
  offset?: number;
  length?: number;
  cap?: OfflineCap;
}

export type OfflineInput = OfflineSamplesInput | OfflineFileInput;

/** An empty buffer the document's recorder writes. */
export interface OfflineOutput {
  channels: number;
  frames: number;
  rate: number;
}

/** A job: `document` (a graph document's JSON, as setGraph takes it; absent for none, so the job returns its inputs)
 *  run for `frames` frames at `rate`, over its inputs then its outputs, which the document names res:1, res:2 and on
 *  (offlineInputRef, offlineOutputRef). `returns` names the handles whose buffers come back, every output by
 *  default. */
export interface OfflineRunRequest {
  document?: string;
  inputs?: OfflineInput[];
  outputs?: OfflineOutput[];
  returns?: number[];
  frames: number;
  rate: number;
}

/** A returned buffer: interleaved, `channels` x `frames` at `rate`, and whether any sample is not finite (native reads
 *  it as it copies the samples out, which is far cheaper than a scan here). */
export interface OfflineBuffer {
  handle: number;
  channels: number;
  frames: number;
  rate: number;
  samples: Float32Array;
  nonFinite: boolean;
}

/** What a file input decoded to and kept: the decoded rate and length, the window's length (0: an empty window, an
 *  input no resource names), and whether a sample the window kept is not finite. */
export interface OfflineInputReport {
  handle: number;
  decodedRate: number;
  decodedFrames: number;
  frames: number;
  nonFinite: boolean;
}

/** A finished job: the buffers `returns` named, in that order, and the file inputs' reports. */
export interface OfflineRunResult {
  buffers: OfflineBuffer[];
  inputs: OfflineInputReport[];
  /** How many times a node's guard replaced what its kernel would have computed during the run (a biquad reset, a
   *  dither restart; native's rp.host.guards/1), and the first one's reason ("" for none). The job completed either
   *  way: whether a guard's output is acceptable is the caller's call (a kit compile refuses it). */
  guardsFired: number;
  guardReason: string;
}

/** A job that failed: native's reason, and the reports of the file inputs it decoded before it stopped. */
export class OfflineRunError extends Error {
  constructor(message: string, readonly inputs: OfflineInputReport[]) {
    super(message);
    this.name = "OfflineRunError";
  }
}

/** The handle input `i` gets in a document, as a resource reference ("res:<n>"). */
export function offlineInputRef(i: number): string {
  return `res:${i + 1}`;
}

/** The handle output `j` gets in a document of `inputCount` inputs, as a resource reference. */
export function offlineOutputRef(inputCount: number, j: number): string {
  return `res:${inputCount + j + 1}`;
}

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

/** What drainOfflineJobs answers: the completions, and how many of the engine's jobs are still queued or running. */
export interface OfflineWireDrain {
  completions: OfflineWireCompletion[];
  inFlight: number;
}

/** The two native calls the jobs ride: what a backend hands OfflineJobs. */
export interface OfflinePort {
  submitOfflineJob(request: OfflineWireRequest): OfflineSubmitResult;
  drainOfflineJobs(waitMs: number): OfflineWireDrain;
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

interface Pending {
  resolve: (value: OfflineRunResult) => void;
  reject: (error: Error) => void;
}

/** The promises over one queue of offline jobs: native's, which is one per engine. */
export class OfflineJobs implements JobQueue {
  private readonly pending = new Map<number, Pending>();
  // Runs made while OFFLINE_MAX_JOBS were pending, oldest first, submitted as completions make room.
  private readonly held: { wire: OfflineWireRequest; waiter: Pending }[] = [];
  private polling = false;
  private quiet = 0;          // the poll's steps in a row that settled nothing and saw no submission
  private submitted = false;  // a submission since the poll's last step

  constructor(private readonly port: OfflinePort) {}

  /** Run a job. Rejects with native's reason when it is refused (at once, or when a held run is submitted); otherwise
   *  settles with its completion, or rejects with an OfflineRunError carrying native's reason. Never rejects because
   *  native's bound is full: the run waits here for room instead. A drain that threw (settle) is the one exception: the
   *  jobs native still holds are no longer counted here, so a run submitted before they are drained can be refused
   *  busy. */
  run(request: OfflineRunRequest): Promise<OfflineRunResult> {
    const wire = toWire(request);
    let waiter: Pending | null = null;
    const promise = new Promise<OfflineRunResult>((resolve, reject) => (waiter = { resolve, reject }));
    if (this.pending.size >= OFFLINE_MAX_JOBS) this.held.push({ wire, waiter: waiter! });
    else this.submit(wire, waiter!);
    if (this.outstanding() > 0) setQueueLive(this, true);
    this.pump();
    this.poll();
    return promise;
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

  private submit(wire: OfflineWireRequest, waiter: Pending): void {
    this.submitted = true;
    let submitted: OfflineSubmitResult;
    try {
      submitted = this.port.submitOfflineJob(wire);
    } catch (e) {
      // A request the port throws on (an RPC error, say) fails that run, never the queue: a held run reaches this from
      // settle's loop, where a throw would otherwise strand the rest of the promises.
      waiter.reject(asError(e));
      return;
    }
    if (submitted.id == null) waiter.reject(new Error(submitted.error ?? "the offline job was refused"));
    else this.pending.set(submitted.id, waiter);
  }

  private settle(waitMs: number): number {
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
    for (const c of drained.completions) {
      const p = this.pending.get(c.id);
      if (!p) continue;
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
    // A promise whose job native no longer holds (none came back, and nothing is in flight) would otherwise be polled
    // forever: it cannot complete, so it fails. One runtime binding one engine never gets here.
    if (this.pending.size > 0 && drained.inFlight === 0) {
      const lost = [...this.pending.values()];
      this.pending.clear();
      for (const p of lost) p.reject(new Error("the engine holds no record of this offline job"));
    }
    while (this.held.length && this.pending.size < OFFLINE_MAX_JOBS) {
      const next = this.held.shift()!;
      this.submit(next.wire, next.waiter);
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
      try {
        const wait = this.submitted || this.quiet < QUIET_STEPS ? 0 : offlineWaitMs;
        const before = this.pending.size;
        this.submitted = false;
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
      if (again) void Promise.resolve().then(step);
    };
    void Promise.resolve().then(step);
  }
}
