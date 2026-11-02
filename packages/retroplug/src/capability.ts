// Capability jobs (docs/modular-graph-phase-c5-design.md section 6): ask a node of the running graph to read, write or
// dump something of the system behind it, and get the answer as a promise. Native takes or refuses a job at once
// (submitJob) and hands its completion back later (drainJobs, from a bounded queue): a read of an emulated system
// completes before submitJob returns, a write once the block after it has published the region (or at once while no
// audio thread runs, the CLI's case), and a device's job (an N8 cart's) on the device's own thread, reporting progress.
// This is the promise side of that, shared by every backend: the real one over the RPC and the mock over its model.
//
// The poll runs where the control plane already looks at native state, never on a timer of its own: once right after
// each submission (so everything that completed at once settles there and then), from the UI's frame tick
// (ui/app/useJobPump.ts) while anything is pending, and, in a host with no UI tick that runs its own audio thread (the
// CLI, the Node addon), after each audio-driver call that lets time pass, with a submission waiting through a native
// sleep while that thread runs (setJobWait), so `await capability(...)` settles there with no timer at all. A closed
// project rejects what is still pending (SystemsStore.clear, which a new project and every load go through): the node
// it named is gone with the graph, and a caller holding the promise learns so rather than waiting for a completion
// that belongs to the old project.

/** The operations, as `<capability>.<op>`; each names its versioned native interface below. */
export type CapabilityOp =
  | "memory.regions"
  | "memory.read"
  | "memory.write"
  | "sram.dump"
  | "sram.restore"
  | "rom.header"
  | "rom.title"
  | "rom.upload";

/** The arguments an operation reads; the rest are ignored. memory: `region` by name, `offset`, and `length` for a read
 *  (absent or 0: to the region's end) or `bytes` for a write; sram: `bytes` for a restore; rom: `length` for a header
 *  (absent or 0: the whole image), and `bytes` and `name` (the file it is stored under) for an upload, which only a
 *  device's node (rp.n8.nes) performs. On that node `name` also names the ROM whose battery file (EDN8/gamedata/<rom>/
 *  bram.srm on its SD card) sram.dump and sram.restore reach, defaulting to the ROM this host last booted. */
export interface CapabilityArgs {
  region?: string;
  offset?: number;
  length?: number;
  bytes?: Uint8Array;
  name?: string;
}

/** What a cart's `sram.restore` wrote: the battery file, and whether the ROM's folder had to be made for it (a ROM that
 *  never saved on the card, or a mistyped name). Only a device's node restores in place; an emulated system refuses. */
export interface SramRestore {
  path: string;
  created: boolean;
}

/** What a cart's `rom.upload` booted: where the image went, the mapper core the cart's menu chose, and that streaming
 *  stopped (a running game has no menu, so the link is reconnected to the new ROM). */
export interface RomUpload {
  path: string;
  mapIndex: number;
  streaming: "stopped";
}

/** A region `memory.regions` lists: its name and its published size in bytes. */
export interface MemoryRegion {
  name: string;
  size: number;
}

/** What each operation's promise resolves with. */
export interface CapabilityResults {
  "memory.regions": MemoryRegion[];
  "memory.read": Uint8Array;
  "memory.write": void;
  "sram.dump": Uint8Array;
  "sram.restore": SramRestore | undefined;
  "rom.header": Uint8Array;
  "rom.title": string;
  "rom.upload": RomUpload;
}

/** A long job's progress: `progress` of `total` (units the job chooses: bytes for a transfer). */
export interface CapabilityProgress {
  progress: number;
  total: number;
}

// The wire shapes (native BackendTypes.hpp: RpcJobRequest, RpcJobSubmitResult, RpcJobCompletion).
export interface JobRequest {
  nodeId: string;
  capability: string;
  op: string;
  region?: string;
  offset?: number;
  length?: number;
  bytes?: Uint8Array;
  name?: string;
}
export interface JobSubmitResult {
  id?: number | null;
  error?: string | null;
}
export interface JobCompletion {
  id: number;
  done: boolean;
  ok: boolean;
  error: string;
  bytes: Uint8Array;
  progress: number;
  total: number;
}

/** The two native calls the jobs ride: what a backend hands CapabilityJobs. */
export interface JobPort {
  submitJob(request: JobRequest): JobSubmitResult;
  drainJobs(): JobCompletion[];
}

const INTERFACES: Record<string, string> = {
  memory: "rp.system.memory/1",
  sram: "rp.system.sram/1",
  rom: "rp.system.rom/1",
};

function decode<O extends CapabilityOp>(op: O, bytes: Uint8Array): CapabilityResults[O] {
  // A JSON result carries the file names a device answered with, which are UTF-8 (the card's own encoding), while a
  // title is a raw byte field of the image, so one byte is one character there.
  if (op === "memory.regions" || op === "rom.upload") return JSON.parse(utf8(bytes)) as CapabilityResults[O];
  if (op === "rom.title") return ascii(bytes) as CapabilityResults[O];
  if (op === "sram.restore") return (bytes.length ? JSON.parse(utf8(bytes)) : undefined) as CapabilityResults[O];
  if (op === "memory.write") return undefined as CapabilityResults[O];
  return bytes as CapabilityResults[O];
}

function utf8(bytes: Uint8Array): string {
  return new TextDecoder().decode(bytes);
}

function ascii(bytes: Uint8Array): string {
  let s = "";
  for (const b of bytes) s += String.fromCharCode(b);
  return s;
}

interface Pending {
  op: CapabilityOp;
  resolve: (value: unknown) => void;
  reject: (error: Error) => void;
  onProgress?: (p: CapabilityProgress) => void;
}

/** A queue of jobs a time-advancing pump visits: the capability jobs here, and the offline jobs (src/offline.ts), so
 *  one pump settles both kinds. `pump` drains what has completed and answers how many are still pending. */
export interface JobQueue {
  pump(): number;
}

// Every queue a time-advancing pump should visit: the ones with a promise waiting, or a completion a close left owed.
// A queue that owes nothing leaves the set, so a runtime that makes backends repeatedly bounds it; one real client's
// queue serves every client of a runtime, since native's completion queue is one per engine and a runtime binds one
// engine (realBackend.ts), while the mock has one per mock, over its own model.
const liveJobs = new Set<JobQueue>();

/** Put a queue in, or take it out of, the set pumpAllJobs visits: a queue joins while it has a promise waiting. */
export function setQueueLive(queue: JobQueue, live: boolean): void {
  if (live) liveJobs.add(queue);
  else liveJobs.delete(queue);
}

// How to let time pass while a submitted job waits, or null to return its promise pending: the audio driver installs
// a native sleep while its audio thread runs (startAudio to stopAudio), where nothing else would pump, and clears it on
// stop. Only there: a host whose blocks the DAW drives pumps from its UI tick, and must never block that thread.
let jobWait: ((ms: number) => void) | null = null;

/** Install (or clear, with null) the wait a submission blocks on while its job is pending; the audio driver's. */
export function setJobWait(wait: ((ms: number) => void) | null): void {
  jobWait = wait;
}

/** Pump every live job queue, capability and offline: the audio driver calls it after the calls that advance time. A
 *  real client's queue is its engine's, so one visit can settle more than one backend's promises. */
export function pumpAllJobs(): void {
  for (const jobs of liveJobs) jobs.pump();
}

/** How many queues a time-advancing pump visits: those with a promise waiting or a completion owed, since a queue that
 *  owes nothing leaves the set. */
export function liveJobCount(): number {
  return liveJobs.size;
}

// How long a submission waits through jobWait before it rejects. A write completes within a block, but a device job can
// run for seconds by design, so the bound catches a job that has not settled rather than one that will not: its bytes
// may still land after the promise is gone.
const JOB_WAIT_LIMIT_MS = 10_000;

/** The promises over one queue of jobs: native's, which is one per engine, or the mock's over its own model. */
export class CapabilityJobs {
  private readonly pending = new Map<number, Pending>();
  // Jobs whose promise is gone while native still owes the completion (a close rejected it, or the wait's bound
  // withdrew it): drained and dropped as they come, since each holds one of native's outstanding slots until a drain
  // collects it.
  private readonly orphans = new Set<number>();

  constructor(private readonly port: JobPort) {}

  /** Run `op` on node `nodeId`. Rejects at once with native's reason when the job is refused; otherwise settles with
   *  its completion, and calls `onProgress` for each progress a long job reports first. */
  run<O extends CapabilityOp>(
    nodeId: string,
    op: O,
    args: CapabilityArgs = {},
    onProgress?: (p: CapabilityProgress) => void,
  ): Promise<CapabilityResults[O]> {
    const dot = op.indexOf(".");
    const capability = INTERFACES[op.slice(0, dot)];
    if (!capability || dot < 0) return Promise.reject(new Error(`unknown capability operation ${op}`));
    const request: JobRequest = { nodeId, capability, op: op.slice(dot + 1) };
    if (args.region !== undefined) request.region = args.region;
    if (args.offset !== undefined) request.offset = args.offset;
    if (args.length !== undefined) request.length = args.length;
    if (args.bytes !== undefined) request.bytes = args.bytes;
    if (args.name !== undefined) request.name = args.name;
    // What a gone promise left owed first, so its completion cannot hold native's bound full against this submission.
    if (this.orphans.size) this.pump();
    const submitted = this.port.submitJob(request);
    if (submitted.id == null) {
      // A refusal drains too: whatever native has finished for this backend settles now, not at some later tick.
      this.pump();
      return Promise.reject(new Error(submitted.error ?? "the job was refused"));
    }
    const id = submitted.id;
    let fail: (error: Error) => void = () => {};
    const promise = new Promise<CapabilityResults[O]>((resolve, reject) => {
      fail = reject;
      this.pending.set(id, { op, resolve: resolve as (value: unknown) => void, reject, onProgress });
    });
    liveJobs.add(this);
    this.pump();
    // With an audio thread no frame tick pumps for (the CLI's), wait here: the job settles before this returns.
    for (let waited = 0; jobWait && this.pending.has(id); waited++) {
      if (waited === JOB_WAIT_LIMIT_MS) {
        // Nothing completed within the bound: reject rather than hand back a promise no pump may ever settle. Native
        // still owes the completion, so its record stays owed (a drain drops it) instead of holding a slot forever.
        this.pending.delete(id);
        this.orphans.add(id);
        fail(new Error(`the job did not complete within ${JOB_WAIT_LIMIT_MS / 1000} s`));
        break;
      }
      jobWait(1);
      this.pump();
    }
    return promise;
  }

  /** Drain native's completions and settle their promises; nothing (no native call) while none is pending and a
   *  close left none owed. A completion whose promise is gone (rejected by a close) is dropped. Returns how many are
   *  still pending. */
  pump(): number {
    if (this.pending.size === 0 && this.orphans.size === 0) {
      liveJobs.delete(this);
      return 0;
    }
    for (const c of this.port.drainJobs()) {
      if (c.done) this.orphans.delete(c.id);
      const p = this.pending.get(c.id);
      if (!p) continue;
      if (!c.done) {
        p.onProgress?.({ progress: c.progress, total: c.total });
        continue;
      }
      this.pending.delete(c.id);
      if (c.ok) p.resolve(decode(p.op, c.bytes));
      else p.reject(new Error(c.error));
    }
    if (this.pending.size === 0 && this.orphans.size === 0) liveJobs.delete(this);
    return this.pending.size;
  }

  /** Reject every pending promise with `reason` (a project closing), so none waits on a node that is gone. */
  rejectAll(reason: string): void {
    const all = [...this.pending.values()];
    if (all.length) liveJobs.add(this);
    for (const id of this.pending.keys()) this.orphans.add(id);
    this.pending.clear();
    for (const p of all) p.reject(new Error(reason));
  }

  /** How many promises are waiting on a completion. */
  pendingCount(): number {
    return this.pending.size;
  }
}
