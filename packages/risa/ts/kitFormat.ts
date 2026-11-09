// risa's kit bank format (docs/modular-graph-phase-d2-design.md section 5): an 8 KB bank of 16 PAL DPCM samples, each
// compiled from its source as a chain of offline graph jobs. The shared half of that compile - the window, the effects
// passes, the guard and non-finite refusals, every slot started at once - is @retroplug/dsp/kitJobs'; what is here is
// the slot: a sample resampled to the PAL DPCM rate, encoded to 7-bit levels (optionally peak-normalised) and packed
// with the DPCM header, then the bank assembled by the ROM codec (@retroplug/tracker/nes/kitBank). BlipToaster's kits
// are the same 8 KB format, which its package assembles from the same helpers.
//
// What a slot is when its window comes out empty is settled here rather than run, since a job can neither take an
// input of no frames nor record one.
import type { KitCompileResult, KitFormat, KitJobRunner } from "@retroplug/dsp/kitContract";
import { compileSlots, load, recorded, runEffects, type Compiled } from "@retroplug/dsp/kitJobs";
import { dpcmJob, resampleJob, resolveEffects } from "@retroplug/dsp/chain";
import { DMC_MAX_BYTES, packDmc, palDmcRate, resampledLength } from "@retroplug/dsp/pack";
import type { OfflineCap } from "@retroplug/core/offlineJob";
import { assembleKitBankPlaced, type AssembleSlot } from "@retroplug/tracker/nes/kitBank";
import type { NesDmcKitSpec, NesDmcSampleSpec } from "@retroplug/tracker/nes/kitSpec";
import { KIT_SAMPLE_REGION, KIT_SLOT_COUNT } from "@retroplug/tracker/nes/constants";

/** One source sample for the risa DMC compiler. The shape is shared with BlipToaster's kits, so it is tracker's
 *  (docs/modular-graph-phase-f4b-design.md section 5.2); risa keeps its own name as an alias. */
export type RisaDmcSampleSpec = NesDmcSampleSpec;

/** A whole risa DMC kit compile: a 6-char kit name + up to 16 samples. */
export type RisaKitCompileSpec = NesDmcKitSpec;

// The DMC window cap: at most a kit's worth of frames at the rate, scaled to the decoded rate in double with the
// product first (OfflineJobs' offlineWindow).
const dmcCap = (rate: number): OfflineCap => ({ frames: DMC_MAX_BYTES * 8, rate, arithmetic: "double-scale" });

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

/** A risa DMC kit: the 8 KB bank (its 16 slots, the rest dropped), and each sample's reason. */
export async function compileRisaKit(runner: KitJobRunner, spec: RisaKitCompileSpec): Promise<KitCompileResult> {
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

/** The risa DMC kit: an 8 KB bank of 16 DPCM samples, which BlipToaster's kits are too. */
export const RISA_KIT_FORMAT: KitFormat<RisaKitCompileSpec> = { id: "risa", compile: compileRisaKit };
