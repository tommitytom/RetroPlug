// The shape of an offline graph job (docs/modular-graph-phase-d2-design.md section 5): a graph document run to
// completion on a host of its own. It is the engine's job API (the backend's offlineRun, native's OfflineJobs), so it
// lives here beside the graph document, which a package that runs a job has anyway; the queue that runs them, with its
// limits and its wait hooks, stays the shell's (src/offline.ts re-exports these names), and dsp's kit compilers take it
// as what they run their chains on. Nothing here imports anything: a job is data.

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

/** The handle input `i` gets in a document, as a resource reference ("res:<n>"). */
export function offlineInputRef(i: number): string {
  return `res:${i + 1}`;
}

/** The handle output `j` gets in a document of `inputCount` inputs, as a resource reference. */
export function offlineOutputRef(inputCount: number, j: number): string {
  return `res:${inputCount + j + 1}`;
}
