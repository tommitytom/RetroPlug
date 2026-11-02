// The kit compilers' byte baseline through the TypeScript compiler (src/kit/, docs/modular-graph-phase-d2-design.md
// sections 5 and 6): every case of packages/native/test/audio/golden/kit-baseline.json compiled as chains of offline
// graph jobs on this host's real engine, its bank held to the golden's record. The matrix is this file's CASES and the
// sources are test-native/kit/sources.ts; this file is the baseline's only runner, and the golden holds the target and
// each case's record.
//
// The sources are synthesised (test-native/kit/sources.ts), written as 32-bit float WAVs into this run's config dir and
// decoded natively by the offline jobs' file input, which is SampleCache's decode (the seam's own), so nothing here
// reads a fixture and CI runs it. Before any case compiles, each source is decoded back through a job to the samples
// written, and on the target held to SOURCE_HASHES (each source's FNV-1a-64 over its float bytes, recorded from the
// retired C++ suite), so a bank that matches is the recorded input compiled, not a coincidence of two different inputs.
// Only on the target: the hashes were recorded from glibc's sin, and Math.sin is the host's own libm (Apple's on macOS,
// the UCRT on Windows), whose last bit may differ now and then; off the target a case only has to agree with itself, so
// a source one ulp away there changes no verdict.
//
// The rules the golden records under. A record is {hash, size, slot}: the bank's FNV-1a-64 in lowercase hex, its
// size, and slot 0's length as the bank records it. The size and the slot length are compared on every host; the hash
// only on the host the golden was recorded on (`linux-x64-gnu13`, the engine's buildTarget), because float arithmetic
// is bit-stable only for one compiler on one architecture. Elsewhere a case compiles twice and must agree with itself,
// and RP_KIT_GOLDEN_REQUIRE_TARGET=1 makes a foreign host fail instead (CI's linux-x64 job sets it on its test:native
// step). The case names must be the golden's keys exactly, none missing and none extra.
//
// RP_KIT_GOLDEN_UPDATE=1 re-records the golden from this compiler, refused anywhere but the target and for any case
// whose two compiles disagree, and written through a sibling temp file. Without the variable the file is only ever
// read.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver, type KitEffect } from "../src/audioDriver";
import { compileKit, compileDmc, type KitCompileResult } from "../src/kit";
import {
  SOURCE_HASHES,
  fnv1a64Hex,
  floatBytes,
  floatWav,
  signalFrames,
  signalSamples,
  sourceKey,
  type Signal,
} from "./kit/sources";

declare const __KIT_GOLDEN__: string;
declare const tjs: { env: Record<string, string> };

const be = createRealBackend();
const audio = createAudioDriver(); // the offline poll's wait: this host has no UI tick

const BASELINE_TARGET = "linux-x64-gnu13";
const env = tjs.env;
const UPDATE = env.RP_KIT_GOLDEN_UPDATE === "1";
const REQUIRE_TARGET = env.RP_KIT_GOLDEN_REQUIRE_TARGET === "1";

// ---- the matrix (the golden's cases, in its order) ------------------------------------------------------------------

type Codec = "lsdj" | "risa";

interface ExtraSlot {
  name: string;
  effects: KitEffect[];
  dmcRate?: number;
}

interface KitCase {
  name: string;
  codec: Codec;
  signal: Signal;
  rate: number;
  effects: KitEffect[];
  rotate?: boolean;
  dmcRate?: number;
  normalize?: boolean;
  loop?: boolean;
  extraSlots?: ExtraSlot[];
  kitName?: string;
}

const gain = (g: number, normalize: boolean): KitEffect => ({ type: "gain", normalize, gain: g });
type FilterType = NonNullable<Extract<KitEffect, { type: "filter" }>["filterType"]>;
const filter = (filterType: FilterType, frequency: number, q: number, gainDb = 0): KitEffect => ({
  type: "filter",
  filterType,
  frequency,
  q,
  gain: gainDb,
});
type DitherType = NonNullable<Extract<KitEffect, { type: "dither" }>["ditherType"]>;
const dither = (ditherType: DitherType): KitEffect => ({ type: "dither", ditherType });

const CASES: KitCase[] = [
  { name: "lsdj/no-effect", codec: "lsdj", signal: "sine440", rate: 44100, effects: [] },
  { name: "lsdj/gain", codec: "lsdj", signal: "sine440", rate: 44100, effects: [gain(0.37, false)] },
  { name: "lsdj/normalize", codec: "lsdj", signal: "sine440", rate: 44100, effects: [gain(0.37, true)] },
  { name: "lsdj/normalize-silence", codec: "lsdj", signal: "silence", rate: 44100, effects: [gain(0.37, true)] },
  { name: "lsdj/normalize-quiet", codec: "lsdj", signal: "quiet", rate: 22050, effects: [gain(0.37, true)] },
  {
    name: "lsdj/normalize-quiet-dither",
    codec: "lsdj",
    signal: "quiet",
    rate: 44100,
    effects: [gain(0.37, true), dither("HighPassTPDF")],
  },
  { name: "lsdj/normalize-quiet-48000", codec: "lsdj", signal: "quiet", rate: 48000, effects: [gain(0.25, true)] },
  { name: "lsdj/gain-negative", codec: "lsdj", signal: "sine440", rate: 44100, effects: [gain(-1.0, false)] },
  {
    name: "lsdj/filter/low-pass",
    codec: "lsdj",
    signal: "sine440",
    rate: 44100,
    effects: [filter("LowPass", 2000, 0.9)],
  },
  {
    name: "lsdj/filter/high-pass",
    codec: "lsdj",
    signal: "sine440",
    rate: 22050,
    effects: [filter("HighPass", 800, 1.4)],
  },
  {
    name: "lsdj/filter/band-pass",
    codec: "lsdj",
    signal: "sine1200",
    rate: 48000,
    effects: [filter("BandPass", 1200, 2.0)],
  },
  {
    name: "lsdj/filter/band-stop",
    codec: "lsdj",
    signal: "chirp",
    rate: 48000,
    effects: [filter("BandStop", 4000, 0.7)],
  },
  {
    name: "lsdj/filter/peak",
    codec: "lsdj",
    signal: "sine1200",
    rate: 44100,
    effects: [filter("Peak", 1000, 1.0, 6.0)],
  },
  {
    name: "lsdj/filter/low-shelf",
    codec: "lsdj",
    signal: "noise",
    rate: 22050,
    effects: [filter("LowShelf", 800, 0.7, -6.0)],
  },
  {
    name: "lsdj/filter/high-shelf",
    codec: "lsdj",
    signal: "chirp",
    rate: 22050,
    effects: [filter("HighShelf", 3000, 0.7, 4.0)],
  },
  {
    name: "lsdj/filter/all-pass",
    codec: "lsdj",
    signal: "noise",
    rate: 48000,
    effects: [filter("AllPass", 1500, 1.2)],
  },
  {
    name: "lsdj/filter/high-pass-outside-range",
    codec: "lsdj",
    signal: "sine440",
    rate: 44100,
    effects: [filter("HighPass", 10, 20)],
  },
  {
    name: "lsdj/filter/high-shelf-outside-range",
    codec: "lsdj",
    signal: "noise",
    rate: 22050,
    effects: [filter("HighShelf", 3000, 0.7, 30.0)],
  },
  {
    name: "lsdj/dither/high-pass-tpdf",
    codec: "lsdj",
    signal: "sine1200",
    rate: 22050,
    effects: [dither("HighPassTPDF")],
  },
  { name: "lsdj/dither/shaped-tpdf", codec: "lsdj", signal: "noise", rate: 44100, effects: [dither("ShapedTPDF")] },
  {
    name: "lsdj/dither/error-diffusion",
    codec: "lsdj",
    signal: "chirp",
    rate: 44100,
    effects: [dither("ErrorDiffusion")],
  },
  { name: "lsdj/dither/jjn", codec: "lsdj", signal: "sine440", rate: 48000, effects: [dither("JJN")] },
  { name: "lsdj/dither/sierra-lite", codec: "lsdj", signal: "sine1200", rate: 44100, effects: [dither("SierraLite")] },
  { name: "lsdj/rotate-off", codec: "lsdj", signal: "chirp", rate: 44100, effects: [], rotate: false },
  { name: "lsdj/rotate-on", codec: "lsdj", signal: "chirp", rate: 44100, effects: [], rotate: true },
  {
    name: "lsdj/chain/filter-gain-dither",
    codec: "lsdj",
    signal: "noise",
    rate: 22050,
    effects: [filter("LowPass", 3000, 1.0), gain(0.5, false), dither("HighPassTPDF")],
  },
  { name: "lsdj/short-source", codec: "lsdj", signal: "short", rate: 44100, effects: [] },
  {
    name: "lsdj/two-slots",
    codec: "lsdj",
    signal: "sine440",
    rate: 44100,
    effects: [],
    extraSlots: [{ name: "SNR", effects: [gain(0.5, false)] }],
    kitName: "baseline-kit",
  },

  { name: "risa/no-effect-rate-4", codec: "risa", signal: "sine440", rate: 44100, effects: [], dmcRate: 4 },
  { name: "risa/no-effect-rate-12", codec: "risa", signal: "chirp", rate: 48000, effects: [], dmcRate: 12 },
  { name: "risa/no-effect-rate-15", codec: "risa", signal: "noise", rate: 22050, effects: [], dmcRate: 15 },
  {
    name: "risa/gain-rate-10",
    codec: "risa",
    signal: "sine1200",
    rate: 44100,
    effects: [gain(0.4, false)],
    dmcRate: 10,
  },
  {
    name: "risa/filter-rate-8",
    codec: "risa",
    signal: "noise",
    rate: 48000,
    effects: [filter("LowPass", 3000, 1.0)],
    dmcRate: 8,
  },
  { name: "risa/short-source-rate-12", codec: "risa", signal: "short", rate: 44100, effects: [], dmcRate: 12 },
  {
    name: "risa/no-normalize-rate-12",
    codec: "risa",
    signal: "noise",
    rate: 44100,
    effects: [],
    dmcRate: 12,
    normalize: false,
  },
  { name: "risa/loop-rate-4", codec: "risa", signal: "sine440", rate: 44100, effects: [], dmcRate: 4, loop: true },
  { name: "risa/normalize-silence-rate-12", codec: "risa", signal: "silence", rate: 22050, effects: [], dmcRate: 12 },
  { name: "risa/normalize-silence-rate-4", codec: "risa", signal: "silence", rate: 48000, effects: [], dmcRate: 4 },
  {
    name: "risa/two-slots-rate-12",
    codec: "risa",
    signal: "chirp",
    rate: 48000,
    effects: [],
    dmcRate: 12,
    extraSlots: [{ name: "SNR", effects: [gain(0.4, false)], dmcRate: 4 }],
  },
];

// ---- the sources ----------------------------------------------------------------------------------------------------

const SIGNALS: Signal[] = ["sine440", "sine1200", "quiet", "chirp", "noise", "silence", "short"];
const RATES = [22050, 44100, 48000];

interface SourceFile {
  path: string;
  samples: Float32Array;
}

const sources = new Map<string, SourceFile>();
for (const c of CASES) {
  const key = sourceKey(c.signal, c.rate);
  if (sources.has(key)) continue;
  const samples = signalSamples(c.signal, c.rate);
  const path = `${be.configDir()}/kit-baseline-${key}.wav`;
  if (!be.writeFile(path, floatWav(samples, c.rate))) throw new Error(`cannot write ${path}`);
  sources.set(key, { path, samples });
}

// ---- compiling a case, and reading its bank ---------------------------------------------------------------------

// A case's kit as its RPC spec, compiled by the TypeScript compiler: slot 0 is KIK with the case's effects, each extra
// slot the same sample under its own name and effects (and, on risa, its own rate index when it has one).
function compileCase(c: KitCase): Promise<KitCompileResult> {
  const path = sources.get(sourceKey(c.signal, c.rate))!.path;
  const kitName = c.kitName ?? (c.codec === "lsdj" ? "BASELINE" : "RISA");
  if (c.codec === "lsdj") {
    const slot = { path, name: "KIK", effects: c.effects };
    const extra = (c.extraSlots ?? []).map((x) => ({ ...slot, name: x.name, effects: x.effects }));
    return compileKit(be, { name: kitName, samples: [slot, ...extra], rotate: c.rotate ?? true });
  }
  const slot = {
    path,
    name: "KIK",
    effects: c.effects,
    rate: c.dmcRate ?? 12,
    normalize: c.normalize ?? true,
    loop: c.loop ?? false,
  };
  const extra = (c.extraSlots ?? []).map((x) => ({
    ...slot,
    name: x.name,
    effects: x.effects,
    rate: x.dmcRate ?? slot.rate,
  }));
  return compileDmc(be, { name: kitName, samples: [slot, ...extra] });
}

const LSDJ_BANK = 0x4000;
const RISA_BANK = 0x2000;
const RISA_INDEX = 0x1f00;

// Whether the bank carries every slot the case compiles: without it a case whose slots all came out empty would pin a
// stable empty bank and pass. LSDj: the offset table only moves forward, so slot i is present when entry i + 1 is past
// entry i. risa: a slot's index address byte is 0xFF when it is empty.
function bankHasSlots(c: KitCase, bank: Uint8Array): boolean {
  const slots = 1 + (c.extraSlots?.length ?? 0);
  if (c.codec === "lsdj") {
    if (bank.length !== LSDJ_BANK) return false;
    const entry = (i: number): number => bank[i * 2] | (bank[i * 2 + 1] << 8);
    if (entry(0) !== 0x4060) return false;
    for (let i = 0; i < slots; i++) if (entry(i + 1) <= entry(i)) return false;
    return true;
  }
  if (bank.length !== RISA_BANK) return false;
  for (let i = 0; i < slots; i++) if (bank[RISA_INDEX + i * 4] === 0xff) return false;
  return true;
}

// Slot 0's length as the bank records it: LSDj's offset table gives the packed bytes exactly; risa's length register
// gives (reg + 1) * 16, the $4013 playback span.
function slotLength(c: KitCase, bank: Uint8Array): number {
  if (c.codec === "lsdj") return (bank[2] | (bank[3] << 8)) - (bank[0] | (bank[1] << 8));
  return (bank[RISA_INDEX + 1] + 1) * 16;
}

interface BankGolden {
  hash: string;
  size: number;
  slot: number;
}
interface Golden {
  target: string;
  cases: Record<string, BankGolden>;
}

function readGolden(): Golden {
  const bytes = be.readFile(__KIT_GOLDEN__);
  if (!bytes)
    throw new Error(`cannot read ${__KIT_GOLDEN__}; record it with RP_KIT_GOLDEN_UPDATE=1 on ${BASELINE_TARGET}`);
  return JSON.parse(new TextDecoder().decode(bytes)) as Golden;
}

// The golden's format: keys in byte order, so a recording of the same banks is the same file.
function goldenText(golden: Golden): string {
  const names = Object.keys(golden.cases).sort((a, b) => (a < b ? -1 : a > b ? 1 : 0));
  const rows = names.map((n) => {
    const b = golden.cases[n];
    return `    "${n}": { "hash": "${b.hash}", "size": ${b.size}, "slot": ${b.slot} }`;
  });
  return `{\n  "target": "${golden.target}",\n  "cases": {\n${rows.join(",\n")}\n  }\n}\n`;
}

function firstDifference(a: Uint8Array, b: Uint8Array): number {
  const n = Math.min(a.length, b.length);
  for (let i = 0; i < n; i++) if (a[i] !== b[i]) return i;
  return a.length === b.length ? -1 : n;
}

// ---- the cases --------------------------------------------------------------------------------------------------

test("the matrix is the golden's: sources for every signal at every rate, and its names are the golden's keys", () => {
  for (const signal of SIGNALS)
    for (const rate of RATES) {
      if (signal === "short" && rate !== 44100) continue;
      expect(sources.has(sourceKey(signal, rate)), `${signal} at ${rate} Hz`).toBe(true);
    }
  expect(new Set(CASES.map((c) => c.name)).size).toBe(CASES.length);
  if (UPDATE) return; // a recording writes the keys this matrix has
  const golden = readGolden();
  expect(golden.target).toBe(BASELINE_TARGET);
  expect(CASES.map((c) => c.name).sort()).toEqual(Object.keys(golden.cases).sort());
});

test("every source decodes through a job to the samples written, and on the target matches SOURCE_HASHES", async () => {
  expect(sources.size).toBe(Object.keys(SOURCE_HASHES).length);
  const onTarget = audio.buildTarget() === BASELINE_TARGET;
  for (const [key, source] of sources) {
    expect(source.samples.length, key).toBe(signalFrames(key.split("-")[0] as Signal));
    if (onTarget) expect(fnv1a64Hex(floatBytes(source.samples)), `${key}'s floats`).toBe(SOURCE_HASHES[key]);
  }
  const loads = [...sources].map(async ([key, source]) => {
    const r = await be.offlineRun({ inputs: [{ path: source.path }], returns: [1], frames: 0, rate: 48000 });
    return { key, source, r };
  });
  for (const { key, source, r } of await Promise.all(loads)) {
    const rate = Number(key.split("-")[1]);
    expect(r.inputs[0].decodedRate, key).toBe(rate);
    expect(r.inputs[0].nonFinite, key).toBe(false);
    expect(r.buffers[0].frames, key).toBe(source.samples.length);
    expect(firstDifference(floatBytes(r.buffers[0].samples), floatBytes(source.samples)), `${key} decoded`).toBe(-1);
  }
});

const COMPILE_CASE = UPDATE
  ? "record every case's bank to the golden"
  : "every kit case compiles to the bank the golden records";

test(COMPILE_CASE, async () => {
  const host = audio.buildTarget();
  const onTarget = host === BASELINE_TARGET;

  // Every case at once: the engine's pool runs their jobs side by side, and each chain is the case's own.
  const first = await Promise.all(CASES.map(compileCase));
  const second = UPDATE || !onTarget ? await Promise.all(CASES.map(compileCase)) : null;

  if (UPDATE) {
    if (!onTarget)
      throw new Error(
        `a recording only happens on ${BASELINE_TARGET}, and this host is ${host}; ` +
          "a digest recorded here would be a fallback, not the baseline",
      );
    const recorded: Golden = { target: BASELINE_TARGET, cases: {} };
    CASES.forEach((c, k) => {
      const kit = first[k];
      expect(kit.errors, c.name).toEqual(kit.errors.map(() => null));
      expect(bankHasSlots(c, kit.bytes), `${c.name} carries its slots`).toBe(true);
      // A case whose two compiles disagree depends on something outside its input: its digest would mean nothing.
      expect(firstDifference(second![k].bytes, kit.bytes), `${c.name} compiles twice to one bank`).toBe(-1);
      recorded.cases[c.name] = { hash: fnv1a64Hex(kit.bytes), size: kit.bytes.length, slot: slotLength(c, kit.bytes) };
    });
    expect(Object.keys(recorded.cases).length).toBe(CASES.length);
    const text = new TextEncoder().encode(goldenText(recorded));
    expect(be.writeFileAtomic(__KIT_GOLDEN__, text), `write ${__KIT_GOLDEN__}`).toBe(true);
    console.log(`# recorded ${CASES.length} banks to ${__KIT_GOLDEN__}`);
    return;
  }

  const golden = readGolden();
  expect(golden.target).toBe(BASELINE_TARGET);
  if (REQUIRE_TARGET && !onTarget)
    throw new Error(
      `this host is ${host} but the baseline was recorded on ${golden.target}, ` +
        "so exact kit bytes would not be compared",
    );

  let matched = 0;
  CASES.forEach((c, k) => {
    const kit = first[k];
    const expected = golden.cases[c.name];
    expect(expected !== undefined, `${c.name} is in the golden`).toBe(true);
    expect(kit.errors, `${c.name}'s slot errors`).toEqual(kit.errors.map(() => null));
    expect(bankHasSlots(c, kit.bytes), `${c.name} carries its slots`).toBe(true);
    expect(kit.bytes.length, `${c.name}'s size`).toBe(expected.size);
    expect(slotLength(c, kit.bytes), `${c.name}'s slot length`).toBe(expected.slot);
    if (onTarget) {
      expect(fnv1a64Hex(kit.bytes), `${c.name}'s bank hash`).toBe(expected.hash);
    } else {
      // Off the target only self-agreement is meaningful: the case must be reproducible from its input alone.
      expect(firstDifference(second![k].bytes, kit.bytes), `${c.name} compiles twice to one bank`).toBe(-1);
    }
    matched++;
  });
  expect(matched).toBe(CASES.length);
  console.log(`# ${matched} banks ${onTarget ? "match the golden's hashes" : `agree with themselves on ${host}`}`);
});
