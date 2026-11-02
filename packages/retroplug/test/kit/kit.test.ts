// The TypeScript kit compiler's pure parts (src/kit/, docs/modular-graph-phase-d2-design.md section 5), with no host:
// the 4-bit level-to-code inversion over every level the dither node produces, the trim, the nibble pack with and
// without rotate against an independent transcription of the LSDj nibble format, the DMC bit pack, pad and truncation,
// the spec's refusals and their messages, the segment split at normalising gains and the documents each pass runs, the
// range constants held to the two descriptor goldens, the bank assemblers' names byte for byte as the bank layouts
// store them, and a slot's chain of jobs over a scripted runner (what it submits, where it stops short, what it does
// with a failure). The banks themselves are held by test-native/kit-baseline (all 39 golden cases) and
// test-native/kit-compile.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import {
  LSDJ_MAX_SAMPLE_FRAMES,
  PAL_DMC_RATES,
  levelToCode,
  packDmc,
  packNibbles,
  palDmcRate,
  resampledLength,
  trimToFrames,
} from "../../src/kit/pack";
import {
  BIQUAD_FREQUENCY_RANGE,
  BIQUAD_GAIN_DB_RANGE,
  BIQUAD_Q_RANGE,
  GAIN_RANGE,
  dpcmDocument,
  effectsDocument,
  quantiseDocument,
  resampleDocument,
  resolveEffects,
  type ParamRange,
} from "../../src/kit/chain";
import { compileDmc, compileKit, type KitJobRunner } from "../../src/kit";
import { buildKitBank } from "../../src/lsdj/rom/buildKit";
import { assembleKitBank } from "../../src/risa/rom/kit";
import type { KitEffect, KitSampleSpec } from "../../src/audioDriver";
import { OfflineRunError, type OfflineRunRequest, type OfflineRunResult } from "../../src/offline";
import gainDescriptor from "../../../native/test/graph/golden/rp.dsp.gain.descriptor.json";
import biquadDescriptor from "../../../native/test/graph/golden/rp.dsp.biquad.descriptor.json";

const F = Math.fround;

// rp::dsp::codeToLevel at 4 bits, in binary32 as the node computes it: (float(code) / 15.0f) * 2.0f - 1.0f. A float
// quotient rounded once from the double quotient is the binary32 quotient, and so for the multiply and the subtract.
const levelOf = (code: number): number => F(F(F(code / 15) * 2) - 1);

// The LSDj nibble pack, transcribed independently of src/kit: stored = 0xF - code, and each frame rotated right by
// one for 9.2.0+.
function referenceNibbles(codes: number[], rotate: boolean): number[] {
  const out: number[] = [];
  for (let chunk = 0; chunk + 32 <= codes.length; chunk += 32) {
    const s = new Array<number>(32);
    if (rotate) {
      s[0] = 0xf - codes[chunk + 31];
      for (let i = 1; i < 32; i++) s[i] = 0xf - codes[chunk + i - 1];
    } else {
      for (let i = 0; i < 32; i++) s[i] = 0xf - codes[chunk + i];
    }
    for (let i = 0; i < 16; i++) out.push((s[i * 2] << 4) | s[i * 2 + 1]);
  }
  return out;
}

function codesOf(n: number, seed: number): number[] {
  let x = seed;
  return Array.from({ length: n }, () => {
    x = (Math.imul(x, 1103515245) + 12345) >>> 0;
    return (x >>> 16) & 0xf;
  });
}

test("every 4-bit level the dither node outputs inverts to its own code, with room to spare", () => {
  for (let code = 0; code < 16; code++) {
    const level = levelOf(code);
    expect(levelToCode(level), `code ${code}`).toBe(code);
    // The inversion's error is float rounding, about 1e-6 of a step: nowhere near the half step a neighbour needs.
    expect(Math.abs((level + 1) * 0.5 * 15 - code), `code ${code}'s error`).toBeLessThan(1e-5);
  }
  expect(levelOf(0)).toBe(-1);
  expect(levelOf(15)).toBe(1);
});

test("the trim keeps whole 32-sample frames, and the nibble pack matches the independent transcription", () => {
  expect([0, 31, 32, 33, 63, 64, 1000].map(trimToFrames)).toEqual([0, 0, 32, 32, 32, 64, 992]);
  for (const n of [0, 5, 31, 32, 33, 64, 96 + 17, 1056 * 2]) {
    const codes = codesOf(n, n + 1);
    const levels = codes.map(levelOf);
    for (const rotate of [true, false]) {
      const packed = packNibbles(levels, rotate);
      expect(packed.length, `${n} codes`).toBe(trimToFrames(n) / 2);
      expect([...packed], `${n} codes, rotate ${rotate}`).toEqual(referenceNibbles(codes, rotate));
    }
  }
  // Rotate: a frame's last sample opens it, inverted (stored = 15 - code).
  const ramp = Array.from({ length: 32 }, (_, i) => i % 16);
  expect(packNibbles(ramp.map(levelOf), true)[0]).toBe(((0xf - 15) << 4) | (0xf - 0));
  expect(packNibbles(ramp.map(levelOf), false)[0]).toBe(((0xf - 0) << 4) | (0xf - 1));
});

test("the resampled length is the kit path's floor(frames * (out / in)), and the DMC rates wav2dmc.py's", () => {
  expect(resampledLength(8192, 44100, 11468)).toBe(Math.floor(8192 * (11468 / 44100)));
  expect(resampledLength(600, 44100, 11468)).toBe(156);
  expect(resampledLength(0, 22050, 11468)).toBe(0);
  expect(resampledLength(3, 48000, 4177.4)).toBe(0);
  expect(PAL_DMC_RATES.length).toBe(16);
  expect(palDmcRate(12)).toBe(16965.4); // the compilers' default index
  expect(palDmcRate(17)).toBe(4696.63); // the index is masked to its low four bits
  expect(LSDJ_MAX_SAMPLE_FRAMES).toBe(32576);
});

test("the DMC pack is LSB first, padded to 16k + 1 bytes and cut at 4081, as the risa bank packs it", () => {
  const bitsOf = (bits: number[]): number[] => bits.map((b) => (b ? 1 : -1));
  expect([...packDmc([])]).toEqual([0]); // the encode of nothing: the one-byte legal stream
  expect([...packDmc(bitsOf([1, 0, 0, 0, 0, 0, 0, 0]))]).toEqual([0x01]);
  expect([...packDmc(bitsOf([0, 1, 1, 0, 0, 0, 0, 1]))]).toEqual([0x86]);
  const nine = packDmc(bitsOf([1, 1, 1, 1, 1, 1, 1, 1, 1]));
  expect(nine.length).toBe(17); // two bytes padded to 16 * 1 + 1
  expect([...nine.subarray(0, 2)]).toEqual([0xff, 0x01]);
  expect(nine.subarray(2).every((b) => b === 0)).toBe(true);
  expect(packDmc(new Array(17 * 8).fill(1)).length).toBe(17); // already legal: no pad
  expect(packDmc(new Array(18 * 8).fill(1)).length).toBe(33);
  // Past the hardware's 4081 bytes the stream is cut there, still a legal length, whether it was padded or not.
  expect(packDmc(new Array(4082 * 8).fill(1)).length).toBe(4081);
  const cut = packDmc(new Array(4081 * 8 + 3).fill(1));
  expect(cut.length).toBe(4081);
  expect(cut[4080]).toBe(0xff);
  // A level is a bit by its sign: the node writes exactly +1 or -1.
  expect([...packDmc([1, -1, 1, -1, 1, -1, 1, -1])]).toEqual([0x55]);
});

test("the range constants are the two nodes' descriptors", () => {
  const param = (d: { params: { id: string; min: number; max: number }[] }, id: string): ParamRange => {
    const p = d.params.find((x) => x.id === id)!;
    return { min: p.min, max: p.max };
  };
  expect(GAIN_RANGE).toEqual(param(gainDescriptor, "gain"));
  expect(BIQUAD_FREQUENCY_RANGE).toEqual(param(biquadDescriptor, "frequency"));
  expect(BIQUAD_Q_RANGE).toEqual(param(biquadDescriptor, "q"));
  expect(BIQUAD_GAIN_DB_RANGE).toEqual(param(biquadDescriptor, "gainDb"));
});

function refusal(fn: () => unknown): string {
  try {
    fn();
  } catch (e) {
    return (e as Error).message;
  }
  throw new Error("expected a refusal");
}

test("a spec the nodes cannot carry is refused with the reason, checked on its binary32 value", () => {
  const at = (effects: KitEffect[], rate = 44100) => () => resolveEffects(effects, rate, true);
  expect(refusal(at([{ type: "gain", normalize: false, gain: 150 }]))).toBe(
    "effect 0 (gain): gain 150 is outside rp.dsp.gain's range -100 .. 100",
  );
  expect(refusal(at([{ type: "gain", normalize: false, gain: NaN }]))).toBe(
    "effect 0 (gain): gain NaN is outside rp.dsp.gain's range -100 .. 100",
  );
  expect(refusal(at([{ type: "filter", filterType: "LowPass", frequency: 22050, q: 1 }]))).toBe(
    "effect 0 (filter): frequency 22050 Hz is at or above the source's Nyquist frequency (22050 Hz at 44100 Hz)",
  );
  expect(refusal(at([{ type: "filter", frequency: 6000 }], 11025))).toBe(
    "effect 0 (filter): frequency 6000 Hz is at or above the source's Nyquist frequency (5512.5 Hz at 11025 Hz)",
  );
  // The default frequency (5734 Hz, the kit rate's Nyquist) is above an 11025 Hz source's.
  expect(refusal(at([{ type: "filter" }], 11025))).toBe(
    "effect 0 (filter): frequency 5734 Hz is at or above the source's Nyquist frequency (5512.5 Hz at 11025 Hz)",
  );
  expect(refusal(at([{ type: "gain", normalize: true }, { type: "filter", filterType: "Peak", q: 2000 }]))).toBe(
    "effect 1 (filter): q 2000 is outside rp.dsp.biquad's range 0.001 .. 1000",
  );
  expect(refusal(at([{ type: "filter", q: 0.0005 }]))).toBe(
    "effect 0 (filter): q 0.0005 is outside rp.dsp.biquad's range 0.001 .. 1000",
  );
  expect(refusal(at([{ type: "filter", filterType: "HighShelf", gain: -97 }]))).toBe(
    "effect 0 (filter): gain -97 is outside rp.dsp.biquad's range -96 .. 96",
  );
  expect(refusal(at([{ type: "filter", frequency: 0.5 }]))).toBe(
    "effect 0 (filter): frequency 0.5 is outside rp.dsp.biquad's range 1 .. 96000",
  );
  // A normalising gain is a setting, refused only when no document can carry it, and the reason says which way.
  expect(refusal(at([{ type: "gain", normalize: true, gain: Infinity }]))).toBe(
    "effect 0 (gain): a normalising gain of Infinity is not finite",
  );
  expect(refusal(at([{ type: "gain", normalize: true, gain: NaN }]))).toBe(
    "effect 0 (gain): a normalising gain of NaN is not a number",
  );
  expect(refusal(at([{ type: "gain", normalize: true, gain: 1e39 }]))).toBe(
    "effect 0 (gain): a normalising gain of 1e+39 is past binary32's largest value (about 3.4e38)",
  );
  expect(refusal(at([{ type: "reverb" } as unknown as KitEffect]))).toBe('effect 0: unknown effect type "reverb"');
  expect(refusal(at([{ type: "filter", filterType: "Comb" } as unknown as KitEffect]))).toBe(
    'effect 0 (filter): unknown filter type "Comb"',
  );
  expect(refusal(at([{ type: "dither", ditherType: "Floyd" } as unknown as KitEffect]))).toBe(
    'effect 0 (dither): unknown dither type "Floyd"',
  );
  const stringGain = { type: "gain", gain: "1" } as unknown as KitEffect;
  expect(refusal(at([stringGain]))).toBe("effect 0 (gain): gain is not a number");
  // Inside the ranges, the bounds included; just under Nyquist; a normalising gain far past the plain gain's range.
  const bounds = resolveEffects(
    [
      { type: "gain", normalize: false, gain: -100 },
      { type: "gain", normalize: false, gain: 100 },
      { type: "filter", frequency: 22049, q: 1000, gain: 96 },
      { type: "filter", frequency: 1, q: 0.001, gain: -96 },
    ],
    44100,
    true,
  );
  expect(bounds.segments[0].nodes.length).toBe(4);
  expect(resolveEffects([{ type: "gain", normalize: true, gain: 150 }], 44100, true).segments[0].normalize).toBe(150);
  // risa skips a dither unread: DMC is 1-bit, so a 4-bit dither has nothing to apply to.
  const unknownDither = { type: "dither", ditherType: "Floyd" } as unknown as KitEffect;
  expect(resolveEffects([unknownDither], 44100, false).dither).toBe(null);
});

test("the effect list is cut at each normalising gain, the last dither winning, and values go in as binary32", () => {
  const chain = resolveEffects(
    [
      { type: "filter", filterType: "HighPass", frequency: 10, q: 20 },
      { type: "dither", ditherType: "JJN" },
      { type: "gain", gain: 0.37 }, // normalise defaults on, as the native struct's
      { type: "gain", normalize: false, gain: -1 },
      { type: "gain", normalize: true, gain: 0.5 },
      { type: "filter", filterType: "Peak", frequency: 1000, q: 0.7, gain: 6 },
      { type: "dither" }, // HighPassTPDF by default, and the last one wins
    ],
    44100,
    true,
  );
  expect(chain.dither).toBe("high-pass-tpdf");
  expect(chain.segments).toEqual([
    { normalize: null, nodes: [{ kind: "biquad", type: "high-pass", frequency: 10, q: 20, gainDb: 0 }], effects: [0] },
    { normalize: F(0.37), nodes: [{ kind: "gain", gain: -1 }], effects: [2, 3] },
    {
      normalize: 0.5,
      nodes: [{ kind: "biquad", type: "peak", frequency: 1000, q: F(0.7), gainDb: 6 }],
      effects: [4, 5],
    },
  ]);
  // A list with nothing to stream has no pass at all: no job.
  expect(resolveEffects([], 44100, true)).toEqual({ segments: [], dither: null });
  expect(resolveEffects([{ type: "dither", ditherType: "SierraLite" }], 44100, true)).toEqual({
    segments: [],
    dither: "sierra-lite",
  });
  // A normalising gain alone is a pass of its own: the normalise is the work.
  expect(resolveEffects([{ type: "gain", gain: 2 }], 44100, true).segments).toEqual([
    { normalize: 2, nodes: [], effects: [0] },
  ]);
});

type Doc = {
  nodes: {
    id: string;
    type: string;
    settings?: Record<string, unknown>;
    params?: Record<string, number>;
    resources?: Record<string, string>;
  }[];
  wires: { from: string; to: string }[];
};

test("each pass's document: its buffer in at res:1, a mono chain of nodes, recorded to res:2", () => {
  const plain = JSON.parse(
    effectsDocument({
      normalize: null,
      nodes: [
        { kind: "gain", gain: F(0.37) },
        { kind: "biquad", type: "low-pass", frequency: 2000, q: F(0.9), gainDb: 0 },
      ],
      effects: [0, 1],
    }),
  ) as Doc;
  expect(plain.nodes.map((n) => n.type)).toEqual([
    "rp.core.buffer-player",
    "rp.dsp.gain",
    "rp.dsp.biquad",
    "rp.core.buffer-recorder",
  ]);
  expect(plain.nodes[0].resources).toEqual({ sample: "res:1" });
  expect(plain.nodes[0].settings).toEqual({ channels: 1, layout: "mono", loop: false });
  expect(plain.nodes[1].params).toEqual({ gain: F(0.37) });
  expect(plain.nodes[2].settings).toEqual({ channels: 1, type: "low-pass" });
  expect(plain.nodes[2].params).toEqual({ frequency: 2000, q: F(0.9), gainDb: 0 });
  expect(plain.nodes[3]).toEqual({
    id: "rec",
    type: "rp.core.buffer-recorder",
    settings: { channels: 1 },
    resources: { target: "res:2" },
  });
  expect(plain.wires).toEqual([
    { from: "src.out", to: "n0.in" },
    { from: "n0.out", to: "n1.in" },
    { from: "n1.out", to: "rec.in" },
  ]);

  const normalised = JSON.parse(effectsDocument({ normalize: F(0.37), nodes: [], effects: [0] })) as Doc;
  expect(normalised.nodes[0]).toEqual({
    id: "src",
    type: "rp.dsp.normalize",
    settings: { buffer: "res:1", gain: F(0.37), channels: 1 },
  });
  expect(normalised.wires).toEqual([{ from: "src.out", to: "rec.in" }]);

  const resample = JSON.parse(resampleDocument()) as Doc;
  expect(resample.nodes[0].settings).toEqual({ buffer: "res:1", channels: 1, layout: "mono", loop: false });

  const quantise = JSON.parse(quantiseDocument("jjn", 4)) as Doc;
  const quantiseTypes = quantise.nodes.map((n) => n.type);
  expect(quantiseTypes).toEqual(["rp.core.buffer-player", "rp.dsp.dither", "rp.core.buffer-recorder"]);
  // The seed is the hash of the very buffer the dither plays, whole: res:1 named twice.
  expect(quantise.nodes[1].settings).toEqual({ channels: 1, algorithm: "jjn", bits: 4, seedFrom: "res:1" });

  const dpcm = JSON.parse(dpcmDocument(false)) as Doc;
  expect(dpcm.nodes[0]).toEqual({ id: "src", type: "rp.dsp.dpcm", settings: { buffer: "res:1", normalize: false } });
});

test("the LSDj assembler writes names by the bank layout's byte rule: the UTF-8 bytes, a-z uppercased", () => {
  const name = (bank: Uint8Array, off: number, n: number): number[] => [...bank.subarray(off, off + n)];
  const bank = buildKitBank("baseline-kit", [
    { name: "kik", bytes: new Uint8Array(16) },
    { name: "é", bytes: new Uint8Array(0) },
  ]);
  expect(name(bank, 0x52, 6)).toEqual([..."BASELI"].map((c) => c.charCodeAt(0)));
  expect(name(bank, 0x22, 3)).toEqual([..."KIK"].map((c) => c.charCodeAt(0)));
  // A non-ASCII name is its UTF-8 bytes (0xC3 0xA9), then the fill; the rule uppercases a-z only, so bytes past ASCII
  // are left alone. The slot is present with no bytes: a named, zero-length entry, not the empty-slot marker.
  expect(name(bank, 0x25, 3)).toEqual([0xc3, 0xa9, 0x2d]);
  expect(bank[2] | (bank[3] << 8)).toBe(0x4060 + 16);
  expect(bank[4] | (bank[5] << 8)).toBe(0x4060 + 16);
  expect(bank[6] | (bank[7] << 8)).toBe(0); // slot 2: the empty-slot marker
  // A short kit name is space-padded.
  expect(name(buildKitBank("ab", []), 0x52, 6)).toEqual([0x41, 0x42, 0x20, 0x20, 0x20, 0x20]);
});

test("the risa assembler filters names byte by byte by the bank layout's rule", () => {
  const bank = assembleKitBank("straße", [{ dpcm: new Uint8Array(17), rate: 12, loop: false, name: "ßk" }]);
  // The rule keeps A-Z, 0-9 and '-' of the name's uppercased bytes, so the two bytes of the eszett are dropped whole
  // (JavaScript's toUpperCase would have made it "SS").
  expect([...bank.subarray(0x1ec0, 0x1ec0 + 7)]).toEqual([..."STRAE"].map((c) => c.charCodeAt(0)).concat([0, 0]));
  // A sample name is its first three bytes, a-z uppercased, others as they are.
  expect([...bank.subarray(0x1ed0, 0x1ed3)]).toEqual([0xc3, 0x9f, 0x4b]);
  const short = assembleKitBank("drums", [{ dpcm: new Uint8Array(17), rate: 12, loop: false, name: "sn" }]);
  expect([...short.subarray(0x1ed0, 0x1ed3)]).toEqual([0x53, 0x4e, 0x20]);
});

// ---- a slot's chain of jobs, over a scripted runner ---------------------------------------------------------------

interface Script {
  // What a load job's file decodes to, by path: its samples, rate and whether the window holds a non-finite sample,
  // or a string to fail with.
  files: Record<string, { samples: Float32Array; rate: number; nonFinite?: boolean } | string>;
}

// A runner that records each request and answers it: a load with the scripted window (the whole file: these tests
// pass no offset or length), a pass by echoing its input resized to the output's frames (an effects pass keeps the
// samples; a quantise pass hands back levels, which the echo keeps as they are, so its input is levels already).
function scriptedRunner(script: Script): KitJobRunner & { requests: OfflineRunRequest[] } {
  const requests: OfflineRunRequest[] = [];
  return {
    requests,
    async offlineRun(request: OfflineRunRequest): Promise<OfflineRunResult> {
      requests.push(request);
      const input = request.inputs![0];
      if ("path" in input) {
        const file = script.files[input.path];
        if (typeof file === "string") throw new OfflineRunError(file, []);
        return {
          buffers: [
            {
              handle: 1,
              channels: 1,
              frames: file.samples.length,
              rate: file.rate,
              samples: file.samples,
              nonFinite: false,
            },
          ],
          inputs: [
            {
              handle: 1,
              decodedRate: file.rate,
              decodedFrames: file.samples.length,
              frames: file.samples.length,
              nonFinite: file.nonFinite ?? false,
            },
          ],
          guardsFired: 0,
          guardReason: "",
        };
      }
      const out = request.outputs![0];
      const samples = new Float32Array(out.frames);
      samples.set(input.samples.subarray(0, out.frames));
      const nonFinite = samples.some((v) => !Number.isFinite(v));
      const buffer = { handle: 2, channels: 1, frames: out.frames, rate: out.rate, samples, nonFinite };
      return { buffers: [buffer], inputs: [], guardsFired: 0, guardReason: "" };
    },
  };
}

// A request's node types, short ("load" for a job with no document).
const typesOf = (r: OfflineRunRequest): string[] =>
  r.document === undefined
    ? ["load"]
    : (JSON.parse(r.document) as Doc).nodes.map((n) => n.type.replace(/^rp\.(core|dsp)\./, ""));

test("an LSDj slot is a load, its effects passes, the resample cut at its length, the quantise, the pack", async () => {
  const levels = Float32Array.from({ length: 4410 }, (_, i) => levelOf(i % 16));
  const runner = scriptedRunner({ files: { "/a.wav": { samples: levels, rate: 44100 } } });
  const effects: KitEffect[] = [
    { type: "filter", frequency: 3000 },
    { type: "gain", gain: 0.5 },
    { type: "dither", ditherType: "ShapedTPDF" },
  ];
  const kit = await compileKit(runner, { name: "K", samples: [{ path: "/a.wav", name: "A", effects }] });
  expect(kit.errors).toEqual([null]);
  expect(runner.requests.map(typesOf)).toEqual([
    ["load"],
    ["buffer-player", "biquad", "buffer-recorder"],
    ["normalize", "buffer-recorder"],
    ["resample", "buffer-recorder"],
    ["buffer-player", "dither", "buffer-recorder"],
  ]);
  const [load, filter, normalize, resample, quantise] = runner.requests;
  const lsdjCap = { frames: 32576, rate: 11468, arithmetic: "float-ratio" };
  expect(load.inputs).toEqual([{ path: "/a.wav", offset: 0, length: 0, cap: lsdjCap }]);
  expect([filter.rate, filter.frames, normalize.rate, normalize.frames]).toEqual([44100, 4410, 44100, 4410]);
  const length = resampledLength(4410, 44100, 11468);
  expect([resample.rate, resample.frames, resample.outputs![0].frames]).toEqual([11468, length, length]);
  expect([quantise.rate, quantise.frames]).toEqual([11468, length]);
  expect((JSON.parse(quantise.document!) as Doc).nodes[1].settings!.algorithm).toBe("shaped-tpdf");
  // The echo's levels (the first `length` of the source's ramp) packed and assembled.
  const packed = packNibbles(levels.subarray(0, length), true);
  expect(kit.bytes[2] | (kit.bytes[3] << 8)).toBe(0x4060 + packed.length);
  expect([...kit.bytes.subarray(0x60, 0x60 + packed.length)]).toEqual([...packed]);
});

test("an LSDj slot that resamples to under one wave frame, or is empty, submits nothing after its load and says why",
  async () => {
  const runner = scriptedRunner({
    files: {
      "/short.wav": { samples: new Float32Array(100), rate: 44100 },
      "/empty.wav": { samples: new Float32Array(0), rate: 44100 },
    },
  });
  const kit = await compileKit(runner, {
    name: "K",
    samples: [
      { path: "/short.wav", name: "S", effects: [{ type: "gain", gain: 1 }] },
      { path: "/empty.wav", name: "E", effects: [] },
    ],
  });
  expect(kit.errors).toEqual([
    "the window resamples to 26 frames at 11468 Hz, under one 32-sample wave frame",
    "the window holds no frames (offset 0, at or past the source's 0 frames)",
  ]);
  expect(runner.requests.map(typesOf)).toEqual([["load"], ["load"]]);
  // Both are named, zero-length slots: the bank keeps a slot's name with no bytes.
  expect([kit.bytes[2] | (kit.bytes[3] << 8), kit.bytes[4] | (kit.bytes[5] << 8)]).toEqual([0x4060, 0x4060]);
  expect(String.fromCharCode(kit.bytes[0x22], kit.bytes[0x25])).toBe("SE");
});

test("a risa slot that resamples to no frames is the one-byte {0}, and one that does takes the DMC pass", async () => {
  const levels = Float32Array.from({ length: 2000 }, (_, i) => (i % 3 ? 1 : -1));
  const runner = scriptedRunner({
    files: { "/tiny.wav": { samples: new Float32Array(3), rate: 48000 }, "/b.wav": { samples: levels, rate: 22050 } },
  });
  const kit = await compileDmc(runner, {
    name: "R",
    samples: [
      { path: "/tiny.wav", name: "T", effects: [], rate: 0 },
      { path: "/b.wav", name: "B", effects: [{ type: "dither" }], rate: 15, normalize: false, loop: true },
    ],
  });
  expect(kit.errors).toEqual([null, null]);
  expect(runner.requests.map(typesOf)).toEqual([
    ["load"],
    ["load"],
    ["resample", "buffer-recorder"],
    ["dpcm", "buffer-recorder"],
  ]);
  const loadB = runner.requests[1];
  const dmcCap = { frames: 4081 * 8, rate: 33252.09, arithmetic: "double-scale" };
  expect(loadB.inputs![0]).toEqual({ path: "/b.wav", offset: 0, length: 0, cap: dmcCap });
  const dpcm = (JSON.parse(runner.requests[3].document!) as Doc).nodes[0];
  expect(dpcm.settings).toEqual({ buffer: "res:1", normalize: false });
  // Slot 0: one zero byte at address 0, length register 0, rate index 0.
  expect([kit.bytes[0x1f00], kit.bytes[0x1f01], kit.bytes[0x1f02], kit.bytes[0]]).toEqual([0, 0, 0, 0]);
  // Slot 1: 64-byte aligned after it, its rate index and loop flag.
  // The echo pads the 2000 levels with zeros to the resampled length, and a zero is a 0 bit.
  const padded = new Float32Array(resampledLength(2000, 22050, 33252.09));
  padded.set(levels);
  const expected = packDmc(padded);
  const index1 = [...kit.bytes.subarray(0x1f04, 0x1f08)];
  expect(index1).toEqual([1, (expected.length - 1) / 16, 15, 1]);
  expect([...kit.bytes.subarray(64, 64 + expected.length)]).toEqual([...expected]);
});

test("a failed slot is left empty with its reason, and the rest of the kit compiles", async () => {
  const ok = Float32Array.from({ length: 4410 }, (_, i) => levelOf(i % 16));
  const runner = scriptedRunner({
    files: {
      "/ok.wav": { samples: ok, rate: 44100 },
      "/nan.wav": { samples: ok, rate: 44100, nonFinite: true },
      "/missing.wav": "input 0: /missing.wav could not be read or decoded",
    },
  });
  const samples = [
    { path: "/missing.wav", name: "M", effects: [] },
    { path: "/nan.wav", name: "N", effects: [] },
    { path: "/ok.wav", name: "F", effects: [{ type: "filter", frequency: 30000 } as KitEffect] },
    { path: "/ok.wav", name: "OK", effects: [] },
  ];
  const kit = await compileKit(runner, { name: "K", samples });
  expect(kit.errors).toEqual([
    "/missing.wav could not be read or decoded", // native's job error, without the job's input numbering
    "/nan.wav has a sample that is not finite in the window",
    "effect 0 (filter): frequency 30000 Hz is at or above the source's Nyquist frequency (22050 Hz at 44100 Hz)",
    null,
  ]);
  // The failed slots are named with no bytes; the good one carries its sample.
  const entry = (i: number): number => kit.bytes[i * 2] | (kit.bytes[i * 2 + 1] << 8);
  expect([entry(1), entry(2), entry(3)]).toEqual([0x4060, 0x4060, 0x4060]);
  expect(entry(4)).toBeGreaterThan(0x4060);
  // risa leaves a failed slot's index entry empty instead.
  const dmc = await compileDmc(runner, { name: "R", samples: [samples[0], samples[3]] });
  expect(dmc.errors).toEqual(["/missing.wav could not be read or decoded", null]);
  expect([dmc.bytes[0x1f00], dmc.bytes[0x1f04]]).toEqual([0xff, 0]);
});

test("a spec's window is a whole frame count, refused as its slot's reason before any job is submitted", async () => {
  const ok = Float32Array.from({ length: 4410 }, (_, i) => levelOf(i % 16));
  const runner = scriptedRunner({ files: { "/ok.wav": { samples: ok, rate: 44100 } } });
  const kit = await compileKit(runner, {
    name: "K",
    samples: [
      { path: "/ok.wav", name: "N", offset: -1, effects: [] },
      { path: "/ok.wav", name: "F", length: 2.5, effects: [] },
      { path: "/ok.wav", name: "H", offset: 0x1_0000_0000, effects: [] },
      { path: "/ok.wav", name: "OK", effects: [] },
    ],
  });
  expect(kit.errors).toEqual([
    "offset -1 is not a frame count (a whole number from 0 to 4294967295)",
    "length 2.5 is not a frame count (a whole number from 0 to 4294967295)",
    "offset 4294967296 is not a frame count (a whole number from 0 to 4294967295)",
    null,
  ]);
  // Only the good slot reached the runner: a value no request can carry is refused here, in the spec's own words,
  // rather than reaching the RPC for reflect-cpp's decode message to explain.
  expect(runner.requests.length).toBe(3);  // load, resample, quantise
});

test("a null window field is absent, which native's optional reads as zero", async () => {
  const ok = Float32Array.from({ length: 4410 }, (_, i) => levelOf(i % 16));
  const runner = scriptedRunner({ files: { "/ok.wav": { samples: ok, rate: 44100 } } });
  // A kit spec read from a user's JSON (lsdj-rom's build-kit takes one straight from the file) can carry an explicit
  // null, which the spec's type does not. Native's optional offset and length read null as absent, so the window is
  // the whole file, as it is for a spec that leaves them out.
  const kit = await compileKit(runner, {
    name: "K",
    samples: [
      { path: "/ok.wav", name: "N", offset: null, length: null, effects: [] } as unknown as KitSampleSpec,
    ],
  });
  expect(kit.errors).toEqual([null]);
  expect(runner.requests[0].inputs).toEqual([
    {
      path: "/ok.wav",
      offset: 0,
      length: 0,
      cap: { frames: LSDJ_MAX_SAMPLE_FRAMES, rate: 11468, arithmetic: "float-ratio" },
    },
  ]);
});

test("a pass that records a non-finite sample, or that a node's guard stepped into, refuses its slot", async () => {
  const ok = Float32Array.from({ length: 4410 }, (_, i) => levelOf(i % 16));
  const base = scriptedRunner({ files: { "/ok.wav": { samples: ok, rate: 44100 } } });
  // The effects pass's recording carries an infinity (a normalise over a subnormal peak, say), or its job completes
  // carrying the count and first reason the offline host gives when guards fire; every other job is the scripted one.
  const runner = (fault: "infinity" | "guard"): KitJobRunner => ({
    async offlineRun(request: OfflineRunRequest): Promise<OfflineRunResult> {
      const result = await base.offlineRun(request);
      const types = typesOf(request);
      if (types[0] === "normalize" && fault === "infinity") {
        result.buffers[0].samples[100] = Infinity;
        result.buffers[0].nonFinite = true; // as native reports it
      }
      if (types[0] === "normalize" && fault === "guard") {
        result.guardsFired = 2;
        result.guardReason = "rp.dsp.biquad reset a filter whose state went non-finite";
      }
      return result;
    },
  });
  const samples = [
    { path: "/ok.wav", name: "A", effects: [{ type: "gain", gain: 0.5 }, { type: "filter" }] as KitEffect[] },
    { path: "/ok.wav", name: "B", effects: [] },
  ];
  const infinity = await compileKit(runner("infinity"), { name: "K", samples });
  expect(infinity.errors).toEqual(["the pass running effects 0, 1 made a sample that is not finite", null]);
  const guard = await compileKit(runner("guard"), { name: "K", samples });
  expect(guard.errors[0]).toBe(
    "the pass running effects 0, 1 overflowed a filter, which rp.dsp.biquad reset (2 times)",
  );
  expect(guard.errors[1]).toBe(null);
});

test("a sample the bank does not hold whole says so: clipped, no room, or not fitting the DMC region", async () => {
  // The scripted runner applies no cap, so these sources pack past the space a bank has.
  const long = Float32Array.from({ length: 130000 }, (_, i) => levelOf(i % 16));
  const runner = scriptedRunner({
    files: { "/long.wav": { samples: long, rate: 44100 }, "/dmc.wav": { samples: long.map(() => 1), rate: 22050 } },
  });
  const kit = await compileKit(runner, {
    name: "K",
    samples: [
      { path: "/long.wav", name: "A", effects: [] },
      { path: "/long.wav", name: "B", effects: [] },
    ],
  });
  const packed = packNibbles(long.subarray(0, resampledLength(130000, 44100, 11468)), true).length;
  expect(kit.errors).toEqual([
    `the kit's sample space ran out, so 16288 of this sample's ${packed} bytes fit`,
    `the kit's 16288 bytes of sample space are full, so none of this sample's ${packed} bytes fit`,
  ]);
  // risa: the encode at the slowest rate is cut at 4081 bytes, and a second one finds no room after it.
  const dmc = await compileDmc(runner, {
    name: "R",
    samples: [
      { path: "/dmc.wav", name: "A", effects: [], rate: 15 },
      { path: "/dmc.wav", name: "B", effects: [], rate: 15 },
    ],
  });
  expect(dmc.errors).toEqual([
    null,
    "this sample's 4081 bytes do not fit the 7872-byte sample region after the slots before it",
  ]);
});

test("samples past the bank's slot count are not compiled, and say so", async () => {
  const runner = scriptedRunner({ files: { "/e.wav": { samples: new Float32Array(0), rate: 44100 } } });
  const sixteen = Array.from({ length: 16 }, (_, i) => ({ path: "/e.wav", name: `S${i}`, effects: [] }));
  const kit = await compileKit(runner, { name: "K", samples: sixteen });
  expect(kit.errors.length).toBe(16);
  expect(kit.errors[14]).toBe("the window holds no frames (offset 0, at or past the source's 0 frames)");
  expect(kit.errors[15]).toBe("the LSDj kit bank holds 15 samples, so this one is dropped");
  expect(runner.requests.length).toBe(15);
  const seventeen = [...sixteen, { path: "/e.wav", name: "X", effects: [] }];
  const dmc = await compileDmc(runner, { name: "R", samples: seventeen });
  expect(dmc.errors[16]).toBe("the risa kit bank holds 16 samples, so this one is dropped");
});

test("on the mock backend every slot fails with its reason, since only a native host runs graphs", async () => {
  const kit = await compileKit(new MockBackend(), { name: "K", samples: [{ path: "/a.wav", name: "A", effects: [] }] });
  expect(kit.errors).toEqual(["offline jobs need a native host: the mock backend cannot run graphs"]);
  expect(kit.bytes.length).toBe(0x4000);
});
