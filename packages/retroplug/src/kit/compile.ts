// The kit compilers (docs/modular-graph-phase-d2-design.md section 5): an LSDj kit or a risa DMC kit compiled from
// its spec as chains of offline graph jobs, one chain per slot, every slot's first job submitted at once so the
// engine's offline pool runs them in parallel. Native decodes, windows and does every float the bytes depend on (the
// effects, the resample, the dither, the DMC encode, each a node in a job's document, chain.ts); TypeScript builds the
// documents, cuts the buffers to their lengths, packs the codes and bits (pack.ts) and assembles the bank
// (lsdj/rom/buildKit.ts, risa/rom/kit.ts). A kit compiled here is the bank those goldens record, byte for byte:
// test-native/kit-baseline.test.ts holds it to kit-baseline.json's 39 banks and test-native/kit-compile.test.ts to
// kit-edges.json's banks past them, both under the baseline's rules (the exact bytes only on the host that recorded
// them, a foreign host demanding each case agree with itself).
//
// A slot that fails is left empty and the kit still compiles, never refusing the kit as a whole: a source that cannot
// be read or decoded, a window with a sample that is not finite, a spec the nodes cannot carry (chain.ts's refusals),
// a pass that makes a sample that is not finite, or a pass a node's guard stepped into (`recorded` says why that last
// one keeps every compiled kit byte-identical). Empty means no bytes: LSDj's assembler still names the slot, with a
// length of zero; risa's leaves its index entry empty.
//
// What a slot is when a chain comes out empty is settled here rather than run, since a job can neither take an input
// of no frames nor record one: a window that is empty (an offset at or past the source's end) or resamples to fewer
// frames than one wave frame is an LSDj slot of no bytes, which is also what trimming to whole 32-sample frames leaves
// of it; and a window that resamples to no frames at all is risa's one-byte `{0}`, the encode of nothing padded to its
// shortest legal length.
//
// Every slot the bank does not hold whole has a reason in the result, a failure or not: the empty LSDj slots above,
// an LSDj sample the bank's sample space ran out on (clipped, or with no room at all), and a risa sample that does not
// fit the sample region after the slots before it. The placements are the assemblers' own (buildKitBankPlaced,
// assembleKitBankPlaced), so the reasons cannot drift from the fit rules. A risa `{0}` is a placed sample, not an
// empty slot, and has none.

import type { KitCompileSpec, KitSampleSpec, RisaKitCompileSpec, RisaDmcSampleSpec } from "../audioDriver";
import { OfflineRunError, type OfflineCap, type OfflineRunRequest } from "../offline";
import type { KitCompileResult, KitFormat, KitJobRunner } from "@retroplug/dsp/kitContract";
import { buildKitBankPlaced } from "../lsdj/rom/buildKit";
import { KIT_MAX_SAMPLES, KIT_MAX_SAMPLE_SPACE } from "../lsdj/rom/constants";
import { assembleKitBankPlaced, type AssembleSlot } from "../risa/rom/kit";
import { KIT_SAMPLE_REGION, KIT_SLOT_COUNT } from "../risa/rom/constants";
import {
  KitSpecError,
  dpcmJob,
  effectsJob,
  loadJob,
  quantiseJob,
  resampleJob,
  resolveEffects,
  windowFrames,
  type SlotChain,
} from "@retroplug/dsp/chain";
import {
  DMC_MAX_BYTES,
  LSDJ_KIT_BITS,
  LSDJ_KIT_RATE,
  LSDJ_MAX_SAMPLE_FRAMES,
  packDmc,
  packNibbles,
  palDmcRate,
  resampledLength,
  trimToFrames,
} from "@retroplug/dsp/pack";

// The kit compiler contract (a format, what it runs on and what it produces) lives in dsp with the chain and pack:
// packages/dsp/ts/kitContract.ts. The compilers stay here, the shell's, because they assemble the carts' banks
// (lsdj/rom/buildKit.ts, risa/rom/kit.ts), which are the carts' own code.
export type { KitCompileResult, KitJobRunner } from "@retroplug/dsp/kitContract";

/** What a slot the compiler left empty with no reason is: the compile result's contract makes that a compiler bug, not
 *  a full bank, so a caller's message says so instead of guessing at a cause. */
export const NO_SLOT_REASON = "the compiler left the slot empty without saying why";

// The window caps: at most a kit's worth of frames at the kit rate, scaled to the decoded rate in each compiler's own
// arithmetic (LSDj in float with the ratio first, risa in double with the product first: OfflineJobs' offlineWindow).
const LSDJ_CAP: OfflineCap = { frames: LSDJ_MAX_SAMPLE_FRAMES, rate: LSDJ_KIT_RATE, arithmetic: "float-ratio" };
const dmcCap = (rate: number): OfflineCap => ({ frames: DMC_MAX_BYTES * 8, rate, arithmetic: "double-scale" });

interface Source {
  samples: Float32Array;
  rate: number;
  decodedFrames: number;
}

// The slot's source, decoded and windowed natively. A window holding a sample that is not finite is refused: a NaN or
// an infinity is where a node's guard and the kit kernel part ways, so no compile could promise the kit path's bytes.
// A file that cannot be read or decoded is the job's failure, said without the job's own input numbering.
async function load(runner: KitJobRunner, spec: KitSampleSpec, cap: OfflineCap): Promise<Source> {
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
async function recorded(runner: KitJobRunner, request: OfflineRunRequest, what: string): Promise<Float32Array> {
  const result = await runner.offlineRun(request);
  if (result.guardsFired > 0) throw new KitSpecError(`${what} ${guardWords(result.guardReason, result.guardsFired)}`);
  const [buffer] = result.buffers;
  if (buffer.nonFinite) throw new KitSpecError(`${what} made a sample that is not finite`);
  return buffer.samples;
}

// The effects passes in order, each recording the whole buffer for the next.
async function runEffects(runner: KitJobRunner, source: Source, chain: SlotChain): Promise<Float32Array> {
  let buffer = source.samples;
  for (const segment of chain.segments) {
    const what = `the pass running effect${segment.effects.length === 1 ? "" : "s"} ${segment.effects.join(", ")}`;
    buffer = await recorded(runner, effectsJob(buffer, source.rate, segment), what);
  }
  return buffer;
}

// A slot's compiled bytes, and why they are empty when they are without a failure.
interface Compiled {
  bytes: Uint8Array;
  empty: string | null;
}

async function lsdjSlot(runner: KitJobRunner, spec: KitSampleSpec, rotate: boolean): Promise<Compiled> {
  const source = await load(runner, spec, LSDJ_CAP);
  const chain = resolveEffects(spec.effects ?? [], source.rate, true);
  if (source.samples.length === 0) {
    const where = `offset ${spec.offset ?? 0}, at or past the source's ${source.decodedFrames} frames`;
    const empty = `the window holds no frames (${where})`;
    return { bytes: new Uint8Array(0), empty };
  }
  const length = resampledLength(source.samples.length, source.rate, LSDJ_KIT_RATE);
  if (trimToFrames(length) === 0) {
    const empty = `the window resamples to ${length} frames at ${LSDJ_KIT_RATE} Hz, under one 32-sample wave frame`;
    return { bytes: new Uint8Array(0), empty };
  }
  const effected = await runEffects(runner, source, chain);
  const resampled = await recorded(runner, resampleJob(effected, source.rate, LSDJ_KIT_RATE, length), "the resample");
  const quantise = quantiseJob(resampled, LSDJ_KIT_RATE, chain.dither ?? "none", LSDJ_KIT_BITS);
  const levels = await recorded(runner, quantise, "the quantise");
  return { bytes: packNibbles(levels, rotate), empty: null };
}

async function dmcSlot(runner: KitJobRunner, spec: RisaDmcSampleSpec): Promise<Compiled> {
  const rate = palDmcRate(spec.rate ?? 12);
  const source = await load(runner, spec, dmcCap(rate));
  const chain = resolveEffects(spec.effects ?? [], source.rate, false);
  const length = resampledLength(source.samples.length, source.rate, rate);
  if (length === 0) return { bytes: packDmc([]), empty: null };
  const effected = await runEffects(runner, source, chain);
  const resampled = await recorded(runner, resampleJob(effected, source.rate, rate, length), "the resample");
  const levels = await recorded(runner, dpcmJob(resampled, rate, spec.normalize ?? true), "the DMC encode");
  return { bytes: packDmc(levels), empty: null };
}

interface SlotResult {
  bytes: Uint8Array;
  error: string | null;
}

async function settle(compile: Promise<Compiled>): Promise<SlotResult> {
  try {
    const { bytes, empty } = await compile;
    return { bytes, error: empty };
  } catch (e) {
    return { bytes: new Uint8Array(0), error: e instanceof Error ? e.message : String(e) };
  }
}

// Each honoured slot's chain started at once; a sample past the bank's `capacity` is not compiled (the bank layout
// holds that many), and its reason says so.
async function compileSlots<S>(samples: readonly S[], capacity: number, compile: (s: S) => Promise<Compiled>,
                               bankName: string): Promise<{ results: SlotResult[]; dropped: string[] }> {
  const results = await Promise.all(samples.slice(0, capacity).map((s) => settle(compile(s))));
  const why = `the ${bankName} bank holds ${capacity} samples, so this one is dropped`;
  return { results, dropped: samples.slice(capacity).map(() => why) };
}

/** An LSDj kit: the 16 KB bank (its 15 slots, the rest dropped), and each sample's reason. */
export async function compileKit(runner: KitJobRunner, spec: KitCompileSpec): Promise<KitCompileResult> {
  const rotate = spec.rotate ?? true;
  const compile = (s: KitSampleSpec): Promise<Compiled> => lsdjSlot(runner, s, rotate);
  const { results, dropped } = await compileSlots(spec.samples, KIT_MAX_SAMPLES, compile, "LSDj kit");
  const samples = results.map((r, i) => ({ name: spec.samples[i].name, bytes: r.bytes }));
  const { bank, written } = buildKitBankPlaced(spec.name, samples);
  const errors = results.map((r, i): string | null => {
    if (r.error !== null) return r.error;
    const n = r.bytes.length;
    const landed = written[i];
    if (landed === null)
      return `the kit's ${KIT_MAX_SAMPLE_SPACE} bytes of sample space are full, so none of this sample's ${n} ` +
        "bytes fit";
    if (landed < n) return `the kit's sample space ran out, so ${landed} of this sample's ${n} bytes fit`;
    return null;
  });
  return { bytes: bank, errors: [...errors, ...dropped] };
}

/** A risa DMC kit: the 8 KB bank (its 16 slots, the rest dropped), and each sample's reason. */
export async function compileDmc(runner: KitJobRunner, spec: RisaKitCompileSpec): Promise<KitCompileResult> {
  const compile = (s: RisaDmcSampleSpec): Promise<Compiled> => dmcSlot(runner, s);
  const { results, dropped } = await compileSlots(spec.samples, KIT_SLOT_COUNT, compile, "risa kit");
  const slots = results.map((r, i): AssembleSlot | null => {
    const s = spec.samples[i];
    return r.bytes.length ? { dpcm: r.bytes, rate: (s.rate ?? 12) & 0x0f, loop: s.loop ?? false, name: s.name } : null;
  });
  const { bank, placed } = assembleKitBankPlaced(spec.name, slots);
  const errors = results.map((r, i): string | null => {
    if (r.error !== null) return r.error;
    if (placed[i]) return null;
    return `this sample's ${r.bytes.length} bytes do not fit the ${KIT_SAMPLE_REGION}-byte sample region after the ` +
      "slots before it";
  });
  return { bytes: bank, errors: [...errors, ...dropped] };
}

export type { KitFormat } from "@retroplug/dsp/kitContract";

/** The LSDj kit: a 16 KB bank of 15 rotated 4-bit samples. */
export const LSDJ_KIT_FORMAT: KitFormat<KitCompileSpec> = { id: "lsdj", compile: compileKit };

/** The risa DMC kit: an 8 KB bank of 16 DPCM samples, which BlipToaster's kits are too. */
export const RISA_KIT_FORMAT: KitFormat<RisaKitCompileSpec> = { id: "risa", compile: compileDmc };
