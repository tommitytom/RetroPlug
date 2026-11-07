// What a kit compiler does around a format's own slot, apart from the slot itself
// (docs/modular-graph-phase-d2-design.md section 5): decode and window a source natively, run its effect chain and each
// pass, hold every pass and every slot to "the bytes are the kernels'", and start every honoured slot's chain at once
// so the engine's offline pool runs them in parallel. A cart's format (its package's kitFormat.ts) supplies the slot:
// which chain to resolve, what to quantise and how to pack it, then assembles the bank.
//
// A slot that fails is left empty and the kit still compiles, never refusing the kit as a whole: a source that cannot be
// read or decoded, a window with a sample that is not finite, a spec the nodes cannot carry (chain.ts's refusals), a
// pass that makes a sample that is not finite, or a pass a node's guard stepped into (`recorded` says why that last one
// keeps every compiled kit byte-identical). Empty means no bytes: LSDj's assembler still names the slot, with a length
// of zero; risa's leaves its index entry empty.
import { OfflineRunError, type OfflineCap, type OfflineRunRequest } from "@retroplug/core/offlineJob";
import { KitSpecError, effectsJob, loadJob, windowFrames, type SlotChain } from "./chain";
import type { KitJobRunner, KitSampleSpec } from "./kitContract";

/** What a slot the compiler left empty with no reason is: the compile result's contract makes that a compiler bug, not
 *  a full bank, so a caller's message says so instead of guessing at a cause. */
export const NO_SLOT_REASON = "the compiler left the slot empty without saying why";

/** A slot's source, decoded and windowed natively. */
export interface Source {
  samples: Float32Array;
  rate: number;
  decodedFrames: number;
}

// The slot's source, decoded and windowed natively. A window holding a sample that is not finite is refused: a NaN or
// an infinity is where a node's guard and the kit kernel part ways, so no compile could promise the kit path's bytes.
// A file that cannot be read or decoded is the job's failure, said without the job's own input numbering.
export async function load(runner: KitJobRunner, spec: KitSampleSpec, cap: OfflineCap): Promise<Source> {
  let result;
  try {
    result = await runner.offlineRun(
      loadJob(spec.path, windowFrames(spec, "offset"), windowFrames(spec, "length"), cap),
    );
  } catch (e) {
    if (e instanceof OfflineRunError) throw new Error(e.message.replace(/^input 0: /, ""));
    throw e;
  }
  const [report] = result.inputs;
  if (report.nonFinite) throw new KitSpecError(`${spec.path} has a sample that is not finite in the window`);
  return { samples: result.buffers[0].samples, rate: report.decodedRate, decodedFrames: report.decodedFrames };
}

// What a guard's report says, in a slot error's words: which node stepped in, and what that means for the pass.
function guardWords(reason: string, times: number): string {
  const count = `${times} time${times === 1 ? "" : "s"}`;
  if (reason.startsWith("rp.dsp.biquad")) return `overflowed a filter, which rp.dsp.biquad reset (${count})`;
  if (reason.startsWith("rp.dsp.dither"))
    return `overflowed the dither's state, which rp.dsp.dither restarted (${count})`;
  return `needed a node's guard (${count}: ${reason})`;
}

// A pass run, and the one buffer it recorded; refused (naming the pass, `what`) when a node's guard stepped in during
// the run or the recording holds a sample that is not finite.
//
// The two refusals keep a compiled kit byte-identical to the kit kernels when a finite source overflows somewhere in
// the chain. Every pass's input is finite: the window's, checked at the load, or an earlier pass's recording, checked
// here. In an effects pass, a gain or a normalise has neither a guard nor a clamp, so a non-finite value it makes goes
// on to the recording or to a biquad; the biquad clamps its output, the one place an effects pass could hide such a
// value, but a non-finite input or output leaves its state non-finite, which its guard resets and reports. The
// resample has no guard or clamp either, so what it makes is in its recording. The quantise and the DMC encode only
// ever get checked inputs: the dither reports any restart, and on finite input the dither's quantiser and the DPCM
// encoder are their kernels. So a node departs from its kernel only where a guard reports it (rp.host.guards/1, counted
// in the completion), and every other non-finite value is in a recording; refusing both refuses every way a kit could
// part from the kernels, as a source with a non-finite sample is refused. Native reads the non-finite flag as it copies
// the buffer out; a scan here would cost several times the job in a script runtime (380 ms of a 450 ms compile of
// fifteen capped 96 kHz slots).
export async function recorded(runner: KitJobRunner, request: OfflineRunRequest, what: string): Promise<Float32Array> {
  const result = await runner.offlineRun(request);
  if (result.guardsFired > 0) throw new KitSpecError(`${what} ${guardWords(result.guardReason, result.guardsFired)}`);
  const [buffer] = result.buffers;
  if (buffer.nonFinite) throw new KitSpecError(`${what} made a sample that is not finite`);
  return buffer.samples;
}

// The effects passes in order, each recording the whole buffer for the next.
export async function runEffects(runner: KitJobRunner, source: Source, chain: SlotChain): Promise<Float32Array> {
  let buffer = source.samples;
  for (const segment of chain.segments) {
    const what = `the pass running effect${segment.effects.length === 1 ? "" : "s"} ${segment.effects.join(", ")}`;
    buffer = await recorded(runner, effectsJob(buffer, source.rate, segment), what);
  }
  return buffer;
}

/** A slot's compiled bytes, and why they are empty when they are without a failure. */
export interface Compiled {
  bytes: Uint8Array;
  empty: string | null;
}

/** One slot's outcome: its bytes (none on a failure) and the reason it has none or is short, if it has one. */
export interface SlotResult {
  bytes: Uint8Array;
  error: string | null;
}

/** A slot compiled to its bytes or to the reason it could not be: a failure never fails the kit. */
export async function settle(compile: Promise<Compiled>): Promise<SlotResult> {
  try {
    const { bytes, empty } = await compile;
    return { bytes, error: empty };
  } catch (e) {
    return { bytes: new Uint8Array(0), error: e instanceof Error ? e.message : String(e) };
  }
}

/** Each honoured slot's chain started at once; a sample past the bank's `capacity` is not compiled (the bank layout
 *  holds that many), and its reason says so. */
export async function compileSlots<S>(
  samples: readonly S[],
  capacity: number,
  compile: (s: S) => Promise<Compiled>,
  bankName: string,
): Promise<{ results: SlotResult[]; dropped: string[] }> {
  const results = await Promise.all(samples.slice(0, capacity).map((s) => settle(compile(s))));
  const why = `the ${bankName} bank holds ${capacity} samples, so this one is dropped`;
  return { results, dropped: samples.slice(capacity).map(() => why) };
}
