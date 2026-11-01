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
// sleep while that thread runs (setJobWait), so `await capability(...)` settles there with no timer at all. A closed project rejects what is still pending (SystemsStore.clear,
// which a new project and every load go through): the node it named is gone with the graph, and a caller holding the
// promise learns so rather than waiting for a completion that belongs to the old project.

/** The operations, as `<capability>.<op>`; each names its versioned native interface below. */
export type CapabilityOp =
  | "memory.regions"
  | "memory.read"
  | "memory.write"
  | "sram.dump"
  | "sram.restore"
  | "rom.header"
  | "rom.title";

/** The arguments an operation reads; the rest are ignored. memory: `region` by name, `offset`, and `length` for a read
 *  (absent or 0: to the region's end) or `bytes` for a write; sram: `bytes` for a restore; rom: `length` for a header
 *  (absent or 0: the whole image). */
export interface CapabilityArgs {
  region?: string;
  offset?: number;
  length?: number;
  bytes?: Uint8Array;
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
  "sram.restore": void;
  "rom.header": Uint8Array;
  "rom.title": string;
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
  if (op === "memory.regions") return JSON.parse(ascii(bytes)) as CapabilityResults[O];
  if (op === "rom.title") return ascii(bytes) as CapabilityResults[O];
  if (op === "memory.write" || op === "sram.restore") return undefined as CapabilityResults[O];
  return bytes as CapabilityResults[O];
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

// Every CapabilityJobs alive (one per backend), for the calls that advance time to pump: a host with no UI frame tick
// (the CLI, the Node addon, the native test host) drives its own audio thread through the audio driver, which pumps
// these after each render, sleep and stop (src/audioDriver.ts).
const liveJobs = new Set<CapabilityJobs>();

// How to let time pass while a submitted job waits, or null to return its promise pending: the audio driver installs
// a native sleep while its audio thread runs (startAudio to stopAudio), where nothing else would pump, and clears it on
// stop. Only there: a host whose blocks the DAW drives pumps from its UI tick, and must never block that thread.
let jobWait: ((ms: number) => void) | null = null;

/** Install (or clear, with null) the wait a submission blocks on while its job is pending; the audio driver's. */
export function setJobWait(wait: ((ms: number) => void) | null): void {
  jobWait = wait;
}

/** Pump every backend's jobs: the audio driver calls it after the calls that advance time. */
export function pumpAllJobs(): void {
  for (const jobs of liveJobs) jobs.pump();
}

// How long a submission waits through jobWait before it returns its promise still pending: a write completes within a
// block, so this bounds only a job that is stuck (a device's) rather than slow.
const JOB_WAIT_LIMIT_MS = 10_000;

/** The promises over one backend's jobs. */
export class CapabilityJobs {
  private readonly pending = new Map<number, Pending>();
  // Jobs whose promises a close rejected while native still owed their completions: drained and dropped as they come,
  // since each holds one of native's outstanding slots until a drain collects it.
  private readonly orphans = new Set<number>();

  constructor(private readonly port: JobPort) {
    liveJobs.add(this);
  }

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
    // What a close left behind first, so its completions cannot hold native's bound full against this submission.
    if (this.orphans.size) this.pump();
    const submitted = this.port.submitJob(request);
    if (submitted.id == null) {
      // A refusal drains too: whatever native has finished for this backend settles now, not at some later tick.
      this.pump();
      return Promise.reject(new Error(submitted.error ?? "the job was refused"));
    }
    const id = submitted.id;
    const promise = new Promise<CapabilityResults[O]>((resolve, reject) => {
      this.pending.set(id, { op, resolve: resolve as (value: unknown) => void, reject, onProgress });
    });
    this.pump();
    // With an audio thread no frame tick pumps for (the CLI's), wait here: the job settles before this returns.
    for (let waited = 0; jobWait && this.pending.has(id) && waited < JOB_WAIT_LIMIT_MS; waited++) {
      jobWait(1);
      this.pump();
    }
    return promise;
  }

  /** Drain native's completions and settle their promises; nothing (no native call) while none is pending and a
   *  close left none owed. A completion whose promise is gone (rejected by a close) is dropped. Returns how many are
   *  still pending. */
  pump(): number {
    if (this.pending.size === 0 && this.orphans.size === 0) return 0;
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
    return this.pending.size;
  }

  /** Reject every pending promise with `reason` (a project closing), so none waits on a node that is gone. */
  rejectAll(reason: string): void {
    const all = [...this.pending.values()];
    for (const id of this.pending.keys()) this.orphans.add(id);
    this.pending.clear();
    for (const p of all) p.reject(new Error(reason));
  }

  /** How many promises are waiting on a completion. */
  pendingCount(): number {
    return this.pending.size;
  }
}
