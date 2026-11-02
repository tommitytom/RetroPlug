// The kit chain's documents (docs/modular-graph-phase-d2-design.md section 5): a slot's effect list resolved and
// checked against the nodes that will run it, then each pass of the chain as one offline job. A pass ends wherever the
// next step needs the whole of what came before: a normalising gain measures its input's peak, the resample cuts a
// whole buffer to its length, and the dither seeds from the whole resampled buffer and the DPCM encode normalises over
// it. Each of those is a node that reads its buffer at activate (rp.dsp.normalize, rp.dsp.resample, rp.dsp.dither's
// `seedFrom`, rp.dsp.dpcm), so each pass records into a buffer the next pass is handed as its input.
//
// The order is the kit formats' (and the golden's), because the order is the bytes: the effects at the source rate in
// list order, the resample, then the quantise (LSDj) or the DMC encode (risa).
//
// Every spec value goes into a document as its binary32 value (`Math.fround`), which is what each node casts its param
// or setting to, so the node computes on the kit path's float. The checks are on that same value, as a double against
// the descriptor's double bounds, the comparison the planner clamps with: anything this file accepts reaches its node
// unclamped, and anything the planner would clamp is refused here with a reason instead of compiling a different spec.

import type { KitEffect } from "../audioDriver";
import { offlineInputRef, offlineOutputRef, type OfflineCap, type OfflineRunRequest } from "../offline";

/** A param's bounds, as its node's descriptor declares them. */
export interface ParamRange {
  min: number;
  max: number;
}

// The param ranges of the two nodes a spec's values reach (DspNodes.cpp's float_params, recorded in
// packages/native/test/graph/golden/rp.dsp.{gain,biquad}.descriptor.json, which a pnpm test case holds these to).
export const GAIN_RANGE: ParamRange = { min: -100, max: 100 };
export const BIQUAD_FREQUENCY_RANGE: ParamRange = { min: 1, max: 96000 };
export const BIQUAD_Q_RANGE: ParamRange = { min: 0.001, max: 1000 };
export const BIQUAD_GAIN_DB_RANGE: ParamRange = { min: -96, max: 96 };

/** A spec the kit compiler refuses: the reason is the slot's error. */
export class KitSpecError extends Error {
  constructor(message: string) {
    super(message);
    this.name = "KitSpecError";
  }
}

// The effect's enums as the nodes label them (the kebab-case of the kernel enumerator, rp.dsp.biquad's `type` and
// rp.dsp.dither's `algorithm`), keyed by the spec's spelling (the rp::lsdj enum names reflect-cpp decodes).
const FILTER_TYPES: Readonly<Record<string, string>> = {
  LowPass: "low-pass",
  HighPass: "high-pass",
  BandPass: "band-pass",
  BandStop: "band-stop",
  Peak: "peak",
  LowShelf: "low-shelf",
  HighShelf: "high-shelf",
  AllPass: "all-pass",
};
const DITHER_TYPES: Readonly<Record<string, string>> = {
  HighPassTPDF: "high-pass-tpdf",
  ShapedTPDF: "shaped-tpdf",
  ErrorDiffusion: "error-diffusion",
  JJN: "jjn",
  SierraLite: "sierra-lite",
};

/** One streaming step of an effects pass: a plain gain, or a biquad. Values are binary32. */
export type ChainNode =
  | { kind: "gain"; gain: number }
  | { kind: "biquad"; type: string; frequency: number; q: number; gainDb: number };

/** One effects pass: its buffer played (through rp.dsp.normalize at `normalize`'s gain when the pass starts at a
 *  normalising gain, else plainly), then its nodes in list order. `effects` are the spec indices of the effects it
 *  runs, the normalising gain's included, for a reason that names them. */
export interface ChainSegment {
  normalize: number | null;
  nodes: ChainNode[];
  effects: number[];
}

/** A slot's effect list, resolved: the effects passes in order (none for a list with nothing to run), and the dither
 *  the quantise runs, the last dither effect winning, or null for plain rounding. */
export interface SlotChain {
  segments: ChainSegment[];
  dither: string | null;
}

// A spec field's value: its binary32 value, with the value as the spec wrote it for a message, or the effect's
// documented default when the field is absent (audioDriver.ts's KitEffect declares every field optional). A non-number
// is refused: no document can carry it.
interface Field {
  value: number;
  written: number;
}

function field(effect: KitEffect, key: string, fallback: number, where: string): Field {
  const v = (effect as unknown as Record<string, unknown>)[key];
  if (v === undefined) return { value: Math.fround(fallback), written: fallback };
  if (typeof v !== "number") throw new KitSpecError(`${where}: ${key} is not a number`);
  return { value: Math.fround(v), written: v };
}

function flag(effect: KitEffect, key: string, fallback: boolean, where: string): boolean {
  const v = (effect as unknown as Record<string, unknown>)[key];
  if (v === undefined) return fallback;
  if (typeof v !== "boolean") throw new KitSpecError(`${where}: ${key} is not a boolean`);
  return v;
}

function enumLabel(table: Readonly<Record<string, string>>, value: unknown, fallback: string, what: string,
                   where: string): string {
  const key = value === undefined ? fallback : value;
  const label = typeof key === "string" && Object.prototype.hasOwnProperty.call(table, key) ? table[key] : undefined;
  if (label === undefined) throw new KitSpecError(`${where}: unknown ${what} ${JSON.stringify(value)}`);
  return label;
}

// The field's binary32 value, if the node's range holds it; refused otherwise. The comparison is the planner's (a
// double against the double bounds), on the value the document will carry. A NaN is inside no range.
function inRange(f: Field, range: ParamRange, what: string, node: string, where: string): number {
  if (!(f.value >= range.min && f.value <= range.max))
    throw new KitSpecError(`${where}: ${what} ${f.written} is outside ${node}'s range ${range.min} .. ${range.max}`);
  return f.value;
}

/**
 * A slot's effect list resolved against the nodes that run it, at the source's `sourceRate`: refused (a KitSpecError
 * naming the effect) for an effect type, filter type or dither type the spec cannot name, a plain gain outside
 * rp.dsp.gain's range, a filter value outside rp.dsp.biquad's, or a filter at or above the source's Nyquist frequency
 * (the planner cannot see the plan's rate, and a biquad there is unstable, where the node's guard would part from the
 * kernel). A normalising gain is not range-checked: rp.dsp.normalize folds the peak into it as a setting, which no
 * range clamps, so it is refused only when a document cannot carry it (not finite). `withDither` false (risa) skips a
 * dither effect unread: DMC is 1-bit, so a 4-bit dither has nothing to apply to.
 *
 * The list is cut at each normalising gain; a pass with nothing to run (the stretch before the first normalising gain,
 * when it holds no node) is dropped, so a list of nothing but dithers has no passes at all.
 */
export function resolveEffects(effects: readonly KitEffect[], sourceRate: number, withDither: boolean): SlotChain {
  const segments: ChainSegment[] = [{ normalize: null, nodes: [], effects: [] }];
  const last = (): ChainSegment => segments[segments.length - 1];
  let dither: string | null = null;
  effects.forEach((effect, i) => {
    const type = (effect as { type?: unknown }).type;
    const where = `effect ${i} (${String(type)})`;
    if (type === "gain") {
      const gain = field(effect, "gain", 1, where);
      if (flag(effect, "normalize", true, where)) {
        // A setting carries any binary32 value but a non-finite one, which JSON cannot: refused, saying which kind.
        if (Number.isNaN(gain.written)) throw new KitSpecError(`${where}: a normalising gain of NaN is not a number`);
        if (!Number.isFinite(gain.written))
          throw new KitSpecError(`${where}: a normalising gain of ${gain.written} is not finite`);
        if (!Number.isFinite(gain.value))
          throw new KitSpecError(
            `${where}: a normalising gain of ${gain.written} is past binary32's largest value (about 3.4e38)`,
          );
        segments.push({ normalize: gain.value, nodes: [], effects: [i] });
      } else {
        const linear = inRange(gain, GAIN_RANGE, "gain", "rp.dsp.gain", where);
        last().nodes.push({ kind: "gain", gain: linear });
        last().effects.push(i);
      }
    } else if (type === "filter") {
      const kind = (effect as { filterType?: unknown }).filterType;
      const filterType = enumLabel(FILTER_TYPES, kind, "LowPass", "filter type", where);
      const freq = field(effect, "frequency", 5734, where);
      const frequency = inRange(freq, BIQUAD_FREQUENCY_RANGE, "frequency", "rp.dsp.biquad", where);
      const q = inRange(field(effect, "q", 1, where), BIQUAD_Q_RANGE, "q", "rp.dsp.biquad", where);
      const gainDb = inRange(field(effect, "gain", 0, where), BIQUAD_GAIN_DB_RANGE, "gain", "rp.dsp.biquad", where);
      const nyquist = sourceRate / 2;
      if (frequency >= nyquist)
        throw new KitSpecError(
          `${where}: frequency ${freq.written} Hz is at or above the source's Nyquist frequency ` +
            `(${nyquist} Hz at ${sourceRate} Hz)`,
        );
      last().nodes.push({ kind: "biquad", type: filterType, frequency, q, gainDb });
      last().effects.push(i);
    } else if (type === "dither") {
      const kind = (effect as { ditherType?: unknown }).ditherType;
      if (withDither) dither = enumLabel(DITHER_TYPES, kind, "HighPassTPDF", "dither type", where);
    } else {
      throw new KitSpecError(`effect ${i}: unknown effect type ${JSON.stringify(type)}`);
    }
  });
  return { segments: segments.filter((s) => s.normalize !== null || s.nodes.length > 0), dither };
}

// ---- the documents ------------------------------------------------------------------------------------------------

// Every document here is mono: each node with a `channels` setting says 1, since several default to 2. A pass's input
// is res:1 and its recording res:2 (offlineInputRef, offlineOutputRef).
const IN = offlineInputRef(0);
const OUT = offlineOutputRef(1, 0);

interface DocNode {
  id: string;
  type: string;
  settings?: Record<string, unknown>;
  params?: Record<string, number>;
  resources?: Record<string, string>;
}

// A chain of nodes, each one's `out` wired to the next one's `in`, the last into a recorder of res:2.
function chainDocument(nodes: DocNode[]): string {
  const recorder: DocNode = {
    id: "rec",
    type: "rp.core.buffer-recorder",
    settings: { channels: 1 },
    resources: { target: OUT },
  };
  const all = [...nodes, recorder];
  const wires = all.slice(1).map((n, i) => ({ from: `${all[i].id}.out`, to: `${n.id}.in` }));
  return JSON.stringify({ nodes: all, wires });
}

// res:1 played from its first frame, once.
function player(): DocNode {
  return {
    id: "src",
    type: "rp.core.buffer-player",
    settings: { channels: 1, layout: "mono", loop: false },
    resources: { sample: IN },
  };
}

/** An effects pass's document: res:1 played (normalised, when the pass starts at a normalising gain), its nodes in
 *  order, recorded to res:2. */
export function effectsDocument(segment: ChainSegment): string {
  const source: DocNode =
    segment.normalize === null
      ? player()
      : { id: "src", type: "rp.dsp.normalize", settings: { buffer: IN, gain: segment.normalize, channels: 1 } };
  const nodes = segment.nodes.map((n, i): DocNode =>
    n.kind === "gain"
      ? { id: `n${i}`, type: "rp.dsp.gain", settings: { channels: 1 }, params: { gain: n.gain } }
      : {
          id: `n${i}`,
          type: "rp.dsp.biquad",
          settings: { channels: 1, type: n.type },
          params: { frequency: n.frequency, q: n.q, gainDb: n.gainDb },
        },
  );
  return chainDocument([source, ...nodes]);
}

/** The resample pass's document: res:1 through rp.dsp.resample to the plan's rate, recorded to res:2. */
export function resampleDocument(): string {
  const settings = { buffer: IN, channels: 1, layout: "mono", loop: false };
  return chainDocument([{ id: "src", type: "rp.dsp.resample", settings }]);
}

/** The quantise pass's document: res:1 played into rp.dsp.dither at 4 bits, seeded from res:1 itself (the whole
 *  resampled buffer, before any trim: the kit path's seed), its levels recorded to res:2. `algorithm` is the
 *  remembered dither's, or "none" for plain rounding (which reads no seed, as the kit path's rounding does not). */
export function quantiseDocument(algorithm: string, bits: number): string {
  const settings = { channels: 1, algorithm, bits, seedFrom: IN };
  return chainDocument([player(), { id: "q", type: "rp.dsp.dither", settings }]);
}

/** The DMC pass's document: res:1 encoded whole by rp.dsp.dpcm at activate, its delta bits recorded to res:2 as
 *  levels of plus and minus one. */
export function dpcmDocument(normalize: boolean): string {
  return chainDocument([{ id: "src", type: "rp.dsp.dpcm", settings: { buffer: IN, normalize } }]);
}

// ---- the jobs -----------------------------------------------------------------------------------------------------

/** A slot's source decoded and windowed natively, returned with no document run: the window's buffer comes back with
 *  its decoded rate, and the input's report says how long the window is and whether it holds a non-finite sample. */
export function loadJob(path: string, offset: number, length: number, cap: OfflineCap): OfflineRunRequest {
  return { inputs: [{ path, offset, length, cap }], returns: [1], frames: 0, rate: cap.rate };
}

// A mono pass over `samples` at `inRate`, recording `frames` frames at the plan's `rate`.
function pass(document: string, samples: Float32Array, inRate: number, frames: number,
              rate: number): OfflineRunRequest {
  return {
    document,
    inputs: [{ samples, channels: 1, rate: inRate }],
    outputs: [{ channels: 1, frames, rate }],
    frames,
    rate,
  };
}

/** An effects pass over the whole buffer, at the source rate (a biquad's coefficients are the plan rate's). */
export function effectsJob(samples: Float32Array, rate: number, segment: ChainSegment): OfflineRunRequest {
  return pass(effectsDocument(segment), samples, rate, samples.length, rate);
}

/** The resample from `inRate` to `outRate`, run for exactly `frames` frames: the kit path's whole-buffer cut,
 *  resampledLength. */
export function resampleJob(samples: Float32Array, inRate: number, outRate: number, frames: number): OfflineRunRequest {
  return pass(resampleDocument(), samples, inRate, frames, outRate);
}

/** The quantise of the whole resampled buffer at the kit rate. */
export function quantiseJob(samples: Float32Array, rate: number, algorithm: string, bits: number): OfflineRunRequest {
  return pass(quantiseDocument(algorithm, bits), samples, rate, samples.length, rate);
}

/** The DMC encode of the whole resampled buffer, one bit a frame. */
export function dpcmJob(samples: Float32Array, rate: number, normalize: boolean): OfflineRunRequest {
  return pass(dpcmDocument(normalize), samples, rate, samples.length, rate);
}
