// LSDj's kit bank format (docs/modular-graph-phase-d2-design.md section 5): a 16 KB bank of up to 15 rotated 4-bit
// samples, each compiled from its source as a chain of offline graph jobs. The shared half of that compile - the window,
// the effects passes, the guard and non-finite refusals, every slot started at once - is @retroplug/dsp/kitJobs'; what
// is here is the slot: an LSDj sample resampled, optionally rotated, quantised to 4 bits and packed as nibbles, then the
// bank assembled by the ROM codec (./rom/buildKit). A kit compiled here is the bank the goldens record, byte for byte.
//
// What a slot is when a chain comes out empty is settled here rather than run, since a job can neither take an input of
// no frames nor record one: a window that is empty (an offset at or past the source's end) is an LSDj slot of no bytes,
// which is also what trimming to whole 32-sample frames leaves of it.
import type { KitCompileResult, KitFormat, KitJobRunner, KitSampleSpec } from "@retroplug/dsp/kitContract";
import { compileSlots, load, recorded, runEffects, type Compiled } from "@retroplug/dsp/kitJobs";
import { quantiseJob, resampleJob, resolveEffects } from "@retroplug/dsp/chain";
import {
  LSDJ_KIT_BITS,
  LSDJ_KIT_RATE,
  LSDJ_MAX_SAMPLE_FRAMES,
  packNibbles,
  resampledLength,
  trimToFrames,
} from "@retroplug/dsp/pack";
import type { OfflineCap } from "@retroplug/core/offlineJob";
import { buildKitBankPlaced } from "./rom/buildKit";
import { KIT_MAX_SAMPLES, KIT_MAX_SAMPLE_SPACE } from "./rom/constants";

/** One sample of an LSDj kit: a source file, a 3-character slot name, an optional source-frame window and an effect
 *  chain. */
export type { KitSampleSpec } from "@retroplug/dsp/kitContract";

/** A whole-kit compile: a 6-char kit name + up to 15 samples. `rotate` = the LSDj 9.2.0+ frame rotation, set from the
 *  target ROM's version (omitted -> true / 9.2.0+). */
export interface KitCompileSpec {
  name: string;
  samples: KitSampleSpec[];
  rotate?: boolean;
}

// The window cap: at most a kit's worth of frames at the kit rate, scaled to the decoded rate in LSDj's own arithmetic
// (float with the ratio first: OfflineJobs' offlineWindow).
const LSDJ_CAP: OfflineCap = { frames: LSDJ_MAX_SAMPLE_FRAMES, rate: LSDJ_KIT_RATE, arithmetic: "float-ratio" };

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

/** An LSDj kit: the 16 KB bank (its 15 slots, the rest dropped), and each sample's reason. */
export async function compileLsdjKit(runner: KitJobRunner, spec: KitCompileSpec): Promise<KitCompileResult> {
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

/** The LSDj kit: a 16 KB bank of 15 rotated 4-bit samples. */
export const LSDJ_KIT_FORMAT: KitFormat<KitCompileSpec> = { id: "lsdj", compile: compileLsdjKit };
