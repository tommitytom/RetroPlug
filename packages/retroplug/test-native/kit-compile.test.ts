// The TypeScript kit compiler (src/kit/) past what the 39-case baseline covers (test-native/kit-baseline holds those),
// on this host's real engine. A slot it refuses (a source with a NaN, a filter at Nyquist, a gain or a Q outside its
// node's range, a pass that overflows a finite source to a value that is not finite, a job whose completion carries a
// node's guard count) leaves the rest of the kit compiling, says why, and leaves a slot of no bytes, the shape a slot
// whose file fails to load has. Every edge a slot has - an empty window, a window cut by offset and length, a window
// that resamples to less than one wave frame or to nothing, a silent source, a source past the frame cap, a bank whose
// space runs out, a DMC slot that does not fit, samples past the slot count, the defaults a spec leaves out - is
// recorded, in full 15- and 16-slot kits; and a slot the bank does not hold whole says why.
//
// Every bank is held to kit-edges.json (beside kit-baseline.json), recorded from this compiler on the target under the
// baseline's rules: its hash on the target (`linux-x64-gnu13`), and elsewhere a second compile agreeing with the first;
// RP_KIT_GOLDEN_REQUIRE_TARGET=1 fails a foreign host, and RP_KIT_GOLDEN_UPDATE=1 re-records it, refused off the target
// or for a bank whose two compiles disagree. Every case still compiles its bank, so the golden holds the bank each one
// describes, and the refusals' shape is held against the same kit compiled with those slots' files missing. The sources
// are synthesised, so it needs no fixtures.
import { test, expect } from "@retroplug/testing";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver, type KitEffect, type KitSampleSpec } from "../src/audioDriver";
import type { RisaDmcSampleSpec } from "../src/audioDriver";
import { compileKit, compileDmc, type KitCompileResult } from "../src/kit";
import { fnv1a64Hex, floatWav, signalSamples } from "./kit/sources";

declare const __KIT_EDGES_GOLDEN__: string;
declare const tjs: { env: Record<string, string> };

const be = createRealBackend();
const audio = createAudioDriver(); // the offline poll's wait: this host has no UI tick

const BASELINE_TARGET = "linux-x64-gnu13";
const UPDATE = tjs.env.RP_KIT_GOLDEN_UPDATE === "1";
const REQUIRE_TARGET = tjs.env.RP_KIT_GOLDEN_REQUIRE_TARGET === "1";
const HOST = audio.buildTarget();
const ON_TARGET = HOST === BASELINE_TARGET;

interface EdgeGolden {
  target: string;
  cases: Record<string, { hash: string; size: number }>;
}

function readGolden(): EdgeGolden {
  const bytes = be.readFile(__KIT_EDGES_GOLDEN__);
  if (!bytes)
    throw new Error(`cannot read ${__KIT_EDGES_GOLDEN__}; record it with RP_KIT_GOLDEN_UPDATE=1 on ${BASELINE_TARGET}`);
  return JSON.parse(new TextDecoder().decode(bytes)) as EdgeGolden;
}

const golden = UPDATE ? null : readGolden();
const recorded = new Map<string, { hash: string; size: number }>();

// The cases that ran to their end: a recording writes only when every one did, so a failing case cannot leave a
// golden recorded from a run that went wrong (or that skipped the banks after its failure).
let completed = 0;
const EDGE_CASES = 8;
function edgeCase(name: string, body: () => Promise<void>): void {
  test(name, async () => {
    await body();
    completed++;
  });
}

// A bank compiled and held to kit-edges.json under the baseline's rules (its size everywhere, its hash on the target, a
// second compile agreeing elsewhere), or recorded under RP_KIT_GOLDEN_UPDATE=1. Returns the first compile.
async function held(name: string, compile: () => Promise<KitCompileResult>): Promise<KitCompileResult> {
  if (UPDATE && !ON_TARGET)
    throw new Error(`a recording only happens on ${BASELINE_TARGET}, and this host is ${HOST}`);
  if (!UPDATE && REQUIRE_TARGET && !ON_TARGET)
    throw new Error(`this host is ${HOST} but kit-edges.json was recorded on ${BASELINE_TARGET}`);
  expect(recorded.has(name), `${name} is held once`).toBe(false);
  const kit = await compile();
  if (UPDATE || !ON_TARGET) {
    const again = await compile();
    expect(firstDifference(again.bytes, kit.bytes), `${name} compiles twice to one bank`).toBe(-1);
  }
  const entry = { hash: fnv1a64Hex(kit.bytes), size: kit.bytes.length };
  recorded.set(name, entry);
  if (golden) {
    const expected = golden.cases[name];
    expect(expected !== undefined, `${name} is in kit-edges.json`).toBe(true);
    expect(entry.size, `${name}'s size`).toBe(expected.size);
    if (ON_TARGET) expect(entry.hash, `${name}'s bank hash`).toBe(expected.hash);
  }
  return kit;
}

function write(name: string, samples: Float32Array, rate: number): string {
  const path = `${be.configDir()}/${name}.wav`;
  if (!be.writeFile(path, floatWav(samples, rate))) throw new Error(`cannot write ${path}`);
  return path;
}

function sine(hz: number, rate: number, frames: number, level = 0.8): Float32Array {
  return Float32Array.from({ length: frames }, (_, i) => level * Math.sin((2 * Math.PI * hz * i) / rate));
}

const SINE = write("sine", signalSamples("sine440", 44100), 44100);
const CHIRP = write("chirp", signalSamples("chirp", 48000), 48000);
const NOISE = write("noise", signalSamples("noise", 22050), 22050);
const SILENCE = write("silence", new Float32Array(8192), 44100);
const SHORT = write("short", sine(440, 44100, 100), 44100); // 26 frames at the kit rate: under one wave frame
const TINY = write("tiny", sine(440, 48000, 3), 48000); // no frames at all at the slowest DMC rate
// 4.5 seconds: past LSDj's cap (a kit's 32576 frames at 11468 Hz is 125271 frames at 44.1 kHz), so the window caps it.
const LONG = write("long", sine(220, 44100, 200000, 0.6), 44100);
// 8.2 seconds: past every DMC rate's cap too (4081 bytes of bits at 4177.4 Hz is 344659 frames at 44.1 kHz).
const LONGER = write("longer", sine(220, 44100, 360000, 0.6), 44100);
const NAN = write(
  "nan",
  (() => {
    const s = signalSamples("sine440", 44100);
    s[1000] = NaN;
    return s;
  })(),
  44100,
);
const MISSING = `${be.configDir()}/no-such-file.wav`;
// Finite sources the chain overflows: a subnormal peak (whose reciprocal is past binary32's range), samples near the
// top of the range (which a gain or a filter's gain carries past it), and samples a dither's scaled value overflows.
const SUBNORMAL = write("subnormal", sine(440, 44100, 8192, 1e-39), 44100);
const HUGE = write("huge", Float32Array.from(signalSamples("sine440", 44100), (v) => v * 3e38), 44100);
const LOUD = write("loud", Float32Array.from(signalSamples("sine440", 44100), (v) => v * 1e38), 44100);

// Effects with every field written, so a case says exactly what it applies; the defaults case below is the one that
// leaves fields out and holds them to the spelled-out spec.
type FilterType = NonNullable<Extract<KitEffect, { type: "filter" }>["filterType"]>;
type DitherType = NonNullable<Extract<KitEffect, { type: "dither" }>["ditherType"]>;
const gain = (g: number, normalize = true): KitEffect => ({ type: "gain", normalize, gain: g });
const filter = (filterType: FilterType, frequency: number, q: number, gainDb = 0): KitEffect => ({
  type: "filter",
  filterType,
  frequency,
  q,
  gain: gainDb,
});
const dither = (ditherType: DitherType): KitEffect => ({ type: "dither", ditherType });

function lsdj(path: string, name: string, effects: KitEffect[] = [],
              window: Partial<KitSampleSpec> = {}): KitSampleSpec {
  return { path, name, effects, ...window };
}

function firstDifference(a: Uint8Array, b: Uint8Array): number {
  const n = Math.min(a.length, b.length);
  for (let i = 0; i < n; i++) if (a[i] !== b[i]) return i;
  return a.length === b.length ? -1 : n;
}

const NONFINITE = `${NAN} has a sample that is not finite in the window`;
const NYQUIST =
  "effect 0 (filter): frequency 22050 Hz is at or above the source's Nyquist frequency (22050 Hz at 44100 Hz)";
const GAIN = "effect 0 (gain): gain 150 is outside rp.dsp.gain's range -100 .. 100";
const Q = "effect 1 (filter): q 2000 is outside rp.dsp.biquad's range 0.001 .. 1000";
const UNREADABLE = `${MISSING} could not be read or decoded`;

edgeCase("a refused LSDj slot leaves the rest compiling, says why, and empties its slot", async () => {
  const refused = [
    lsdj(NAN, "NAN"),
    lsdj(SINE, "NYQ", [filter("HighPass", 22050, 1)]),
    lsdj(NOISE, "GAN", [gain(150, false)]),
    lsdj(NOISE, "Q", [gain(0.5), filter("Peak", 1000, 2000)]),
  ];
  const samples = [
    lsdj(SINE, "A", [filter("LowPass", 2000, 0.9)]),
    refused[0],
    lsdj(CHIRP, "C", [gain(0.5), dither("JJN")]),
    refused[1],
    refused[2],
    lsdj(NOISE, "N", [filter("LowShelf", 800, 0.7, -6)]),
    refused[3],
  ];
  const kit = await held("lsdj/refused", () => compileKit(be, { name: "REFUSE", samples }));
  expect(kit.errors).toEqual([null, NONFINITE, null, NYQUIST, GAIN, null, Q]);

  // Each refused slot is empty: no bytes, its name kept, which is how a slot whose file fails to load is left. So the
  // bank is the same kit compiled with those slots' files missing, byte for byte.
  const missing = samples.map((s) => (refused.includes(s) ? { ...s, path: MISSING } : s));
  const again = await compileKit(be, { name: "REFUSE", samples: missing });
  expect(again.errors).toEqual([null, UNREADABLE, null, UNREADABLE, UNREADABLE, null, UNREADABLE]);
  expect(firstDifference(kit.bytes, again.bytes), "the bank with the refused slots' files missing").toBe(-1);
  // Not the kit without those slots, though: an LSDj slot with no bytes is named, with a zero length, where a slot the
  // spec does not have is the empty-slot marker. So a refused last slot differs from the kit one shorter in exactly
  // that entry and that name.
  const shorter = await compileKit(be, { name: "REFUSE", samples: samples.slice(0, 6) });
  const entry = (bank: Uint8Array, i: number): number => bank[i * 2] | (bank[i * 2 + 1] << 8);
  expect([entry(kit.bytes, 7), entry(shorter.bytes, 7)]).toEqual([entry(kit.bytes, 6), 0]);
  expect([kit.bytes[0x22 + 6 * 3], shorter.bytes[0x22 + 6 * 3]]).toEqual(["Q".charCodeAt(0), 0]);
});

edgeCase("a refused risa slot leaves the rest compiling, its entry empty: a missing file's, or no slot's", async () => {
  const samples: RisaDmcSampleSpec[] = [
    { path: CHIRP, name: "A", effects: [filter("LowPass", 3000, 1)], rate: 8 },
    { path: NAN, name: "NAN", effects: [], rate: 12 },
    { path: NOISE, name: "N", effects: [gain(0.4, false)], rate: 15, loop: true },
    { path: SINE, name: "NYQ", effects: [filter("HighPass", 22050, 1)], rate: 4 },
  ];
  const kit = await held("risa/refused", () => compileDmc(be, { name: "REFUSE", samples }));
  expect(kit.errors).toEqual([null, NONFINITE, null, NYQUIST]);
  expect([kit.bytes[0x1f04], kit.bytes[0x1f0c]]).toEqual([0xff, 0xff]);
  const missing = samples.map((s, i) => (i === 1 || i === 3 ? { ...s, path: MISSING } : s));
  const again = await compileDmc(be, { name: "REFUSE", samples: missing });
  expect(firstDifference(kit.bytes, again.bytes), "the bank with the refused slots' files missing").toBe(-1);
  // risa's bank leaves an empty slot where it is and moves no cursor, so a refused LAST slot is the kit without it.
  const shorter = await compileDmc(be, { name: "REFUSE", samples: samples.slice(0, 3) });
  expect(firstDifference(kit.bytes, shorter.bytes), "the kit without its refused last slot").toBe(-1);
});

edgeCase("a full LSDj kit of every slot edge is the bank kit-edges.json records", async () => {
  const dithers = ["HighPassTPDF", "ShapedTPDF", "ErrorDiffusion", "JJN", "SierraLite"] as const;
  const samples = [
    lsdj(SINE, "EMP", [gain(0.5)], { offset: 9000 }), // an offset past the end: an empty window
    lsdj(SINE, "WIN", [dither("JJN")], { offset: 1000, length: 3000 }),
    lsdj(SHORT, "SHO", [filter("LowPass", 1000, 1)]), // resamples to 26 frames: trimmed to nothing
    lsdj(SILENCE, "SIL", [gain(0.37)]), // normalising silence: no peak to divide by
    lsdj(SILENCE, "SI2", [dither("ShapedTPDF")]),
    ...dithers.map((d, i) =>
      lsdj(i % 2 ? CHIRP : NOISE, `D${i}`, [filter("BandPass", 1500, 2), dither(d)]),
    ),
    lsdj(NOISE, "CH", [
      gain(0.25),
      filter("HighShelf", 3000, 0.7, 30),
      gain(-1, false),
      gain(0.9),
      filter("HighPass", 10, 20),
    ]),
    lsdj(SINE, "LEN", [], { length: 4000 }),
    lsdj(CHIRP, "AP", [filter("AllPass", 1500, 1.2)]),
    lsdj(NOISE, "NS"),
    lsdj(LONG, "LNG", [gain(1)]), // past the cap, and after the bank's space has run out: no room at all
  ];
  expect(samples.length).toBe(15);
  for (const rotate of [true, false]) {
    const spec = { name: "edges", samples, rotate };
    const kit = await held(`lsdj/edges-rotate-${rotate ? "on" : "off"}`, () => compileKit(be, spec));
    // Every slot the bank does not hold whole says why: the empty window, the window under one wave frame, the slot
    // the bank's space ran out on part way (the noise, 2128 bytes, before the capped source), and the capped source,
    // which finds none left.
    const entry = (i: number): number => kit.bytes[i * 2] | (kit.bytes[i * 2 + 1] << 8);
    expect(kit.errors).toEqual(
      samples.map((s, i) =>
        s.name === "EMP"
          ? "the window holds no frames (offset 9000, at or past the source's 8192 frames)"
          : s.name === "SHO"
            ? "the window resamples to 26 frames at 11468 Hz, under one 32-sample wave frame"
            : s.name === "NS"
              ? `the kit's sample space ran out, so ${entry(i + 1) - entry(i)} of this sample's 2128 bytes fit`
              : s.name === "LNG"
                ? "the kit's 16288 bytes of sample space are full, so none of this sample's 16288 bytes fit"
                : null,
      ),
    );
  }
  // A source past the cap fills the whole sample space on its own, and a slot after it finds none: the empty-slot
  // marker, which the bank writes for a slot with no room as for one the spec does not have.
  const full = { name: "FULL", samples: [lsdj(LONG, "LNG"), lsdj(SINE, "AFT")] };
  const kit = await held("lsdj/full", () => compileKit(be, full));
  const noRoom = "the kit's 16288 bytes of sample space are full, so none of this sample's 1056 bytes fit";
  expect(kit.errors).toEqual([null, noRoom]);
  const entry = (i: number): number => kit.bytes[i * 2] | (kit.bytes[i * 2 + 1] << 8);
  expect([entry(1), entry(2), kit.bytes[0x25]]).toEqual([0x4060 + 0x3fa0, 0, 0]);
});

edgeCase("a full risa kit of every DMC slot edge is the bank kit-edges.json records", async () => {
  const samples: RisaDmcSampleSpec[] = [
    { path: TINY, name: "TNY", effects: [], rate: 0 }, // resamples to no frames: the one-byte {0}
    { path: SINE, name: "EMP", effects: [], offset: 9000 }, // an empty window: {0} too
    { path: LONGER, name: "L0", effects: [], rate: 0 }, // past the cap at the slowest rate: 4081 bytes
    { path: LONGER, name: "L15", effects: [gain(0.5)], rate: 15 }, // no room after L0: left empty
    { path: SILENCE, name: "SIL", effects: [], rate: 12 },
    { path: SILENCE, name: "SIN", effects: [], rate: 12, normalize: false },
    { path: NOISE, name: "N", effects: [dither("JJN")], rate: 3, normalize: false },
    { path: CHIRP, name: "C", effects: [filter("Peak", 1000, 1, 6)], rate: 7, loop: true },
    { path: SINE, name: "W", effects: [], rate: 9, offset: 500, length: 2000 },
    { path: SHORT, name: "S", effects: [], rate: 1 },
    { path: NOISE, name: "N2", effects: [gain(-0.7, false)], rate: 2 },
    { path: CHIRP, name: "C2", effects: [], rate: 21 }, // an index past 15 is masked to its low four bits
    { path: SINE, name: "S2", effects: [gain(2)], rate: 5 },
    { path: NOISE, name: "N3", effects: [], rate: 6 },
    { path: CHIRP, name: "C3", effects: [], rate: 10 },
    { path: SINE, name: "S3", effects: [], rate: 11 },
  ];
  const kit = await held("risa/edges", () => compileDmc(be, { name: "edges-kit", samples }));
  // A {0} slot is a placed sample; L15 found no room after L0, and says so.
  const noFit = "this sample's 4081 bytes do not fit the 7872-byte sample region after the slots before it";
  expect(kit.errors).toEqual(samples.map((s) => (s.name === "L15" ? noFit : null)));
  // The {0} slots are one byte at their own 64-byte boundary; L0 is the longest legal stream; L15 found no room.
  expect([kit.bytes[0x1f00], kit.bytes[0x1f01], kit.bytes[0x1f04], kit.bytes[0x1f05]]).toEqual([0, 0, 1, 0]);
  expect([kit.bytes[0x1f08], kit.bytes[0x1f09], kit.bytes[0x1f0c]]).toEqual([2, 255, 0xff]);
});

edgeCase("a finite source the chain overflows refuses its slot with the reason, the rest compiling", async () => {
  // Each route reaches a value that is not finite from a source that is: a normalise over a subnormal peak and a plain
  // gain over samples near the float range's top make one the pass records; a +96 dB peak over those samples overflows
  // the filter, whose guard resets it and whose job completes carrying the count; a dither (JJN) over loud samples
  // overflows its scaled value, whose guard restarts it and whose job completes the same way. A live plan streams on
  // through both guards; the kit compile refuses the slot, so no kit it builds carries a guard's output or a non-finite
  // intermediate.
  const samples = [
    lsdj(SUBNORMAL, "SUB", [gain(0.9)]),
    lsdj(SINE, "A", [filter("LowPass", 2000, 0.9)]),
    lsdj(HUGE, "GAN", [gain(50, false)]),
    lsdj(HUGE, "BIQ", [filter("Peak", 1000, 1, 96)]),
    lsdj(LOUD, "DIT", [dither("JJN")]),
  ];
  const kit = await held("lsdj/overflow", () => compileKit(be, { name: "OVER", samples }));
  expect(kit.errors[0]).toBe("the pass running effect 0 made a sample that is not finite");
  expect(kit.errors[1]).toBe(null);
  expect(kit.errors[2]).toBe("the pass running effect 0 made a sample that is not finite");
  // A guard's count depends on how many blocks it fired in, so the reason is held up to the count.
  const guard = (e: string | null, words: string): boolean => e !== null && e.startsWith(words) && e.endsWith(")");
  const filterWords = "the pass running effect 0 overflowed a filter, which rp.dsp.biquad reset (";
  expect(guard(kit.errors[3], filterWords), String(kit.errors[3])).toBe(true);
  const ditherWords = "the quantise overflowed the dither's state, which rp.dsp.dither restarted (";
  expect(guard(kit.errors[4], ditherWords), String(kit.errors[4])).toBe(true);
  // The refused slots are left as the slots whose files fail to load.
  const missing = samples.map((s) => (s.name === "A" ? s : { ...s, path: MISSING }));
  const bank = await compileKit(be, { name: "OVER", samples: missing });
  expect(firstDifference(kit.bytes, bank.bytes), "the bank with the refused slots' files missing").toBe(-1);
});

edgeCase("samples past the slot count are dropped, each saying so", async () => {
  const sixteen = Array.from({ length: 16 }, (_, i) => lsdj(i % 2 ? SINE : SHORT, `S${i}`, i % 3 ? [] : [gain(0.5)]));
  const kit = await held("lsdj/sixteen", () => compileKit(be, { name: "SIXTEEN", samples: sixteen }));
  expect(kit.errors[15]).toBe("the LSDj kit bank holds 15 samples, so this one is dropped");
  const seventeen: RisaDmcSampleSpec[] = Array.from({ length: 17 }, (_, i) => ({
    path: i % 2 ? SINE : SHORT,
    name: `S${i}`,
    effects: [],
    rate: i % 16,
  }));
  const dmc = await held("risa/seventeen", () => compileDmc(be, { name: "SEVENTEEN", samples: seventeen }));
  expect(dmc.errors[16]).toBe("the risa kit bank holds 16 samples, so this one is dropped");
});

edgeCase("a refusal beats an empty window: a refused slot is empty where its window alone places {0}", async () => {
  const samples: RisaDmcSampleSpec[] = [
    { path: SINE, name: "A", effects: [], rate: 12 },
    { path: SINE, name: "REF", effects: [gain(150, false)], rate: 12, offset: 9000 },
  ];
  const kit = await held("risa/refusal-over-empty-window", () => compileDmc(be, { name: "R", samples }));
  expect(kit.errors).toEqual([null, GAIN]);
  expect(kit.bytes[0x1f04]).toBe(0xff);
  // An empty window is not a refusal: the same slot without the out-of-range gain compiles to the one-byte {0}, a
  // placed slot. The refused slot is the slot left for a file that fails to load.
  const window = await compileDmc(be, {
    name: "R",
    samples: [samples[0], { ...samples[1], effects: [], offset: 9000 }],
  });
  expect(window.errors).toEqual([null, null]);
  expect(window.bytes[0x1f04] !== 0xff).toBe(true);
  const missing = await compileDmc(be, { name: "R", samples: [samples[0], { ...samples[1], path: MISSING }] });
  expect(firstDifference(kit.bytes, missing.bytes)).toBe(-1);
});

edgeCase("a field the spec leaves out takes the effect's default, the spelled-out spec's bank", async () => {
  // A spec may leave an effect's fields out, and the compiler fills the defaults: LowPass at 5734 Hz, Q 1 and 0 dB; a
  // normalising gain of 1; HighPassTPDF. The sparse spec must compile to the spelled-out one's bank.
  const sparse: KitEffect[] = [{ type: "filter" }, { type: "gain" }, { type: "dither" }];
  const spelled: KitEffect[] = [filter("LowPass", 5734, 1, 0), gain(1, true), dither("HighPassTPDF")];
  const kit = await held("lsdj/defaults", () => compileKit(be, { name: "DEF", samples: [lsdj(CHIRP, "SP", sparse)] }));
  expect(kit.errors).toEqual([null]);
  const lsdjSpelled = await compileKit(be, { name: "DEF", samples: [lsdj(CHIRP, "SP", spelled)] });
  expect(firstDifference(kit.bytes, lsdjSpelled.bytes), "the spelled-out LSDj spec's bank").toBe(-1);
  const dmcSparse = [{ path: CHIRP, name: "SP", effects: sparse.slice(0, 2) }];
  const dmc = await held("risa/defaults", () => compileDmc(be, { name: "DEF", samples: dmcSparse }));
  expect(dmc.errors).toEqual([null]);
  const dmcSpelled = await compileDmc(be, {
    name: "DEF",
    samples: [{ path: CHIRP, name: "SP", effects: spelled.slice(0, 2), rate: 12, loop: false, normalize: true }],
  });
  expect(firstDifference(dmc.bytes, dmcSpelled.bytes), "the spelled-out risa spec's bank").toBe(-1);
});

test(UPDATE ? "write kit-edges.json" : "kit-edges.json holds exactly the banks held here", () => {
  if (UPDATE) {
    expect(completed, "every case ran to its end, so the recording is whole").toBe(EDGE_CASES);
    const names = [...recorded.keys()].sort((a, b) => (a < b ? -1 : a > b ? 1 : 0));
    const rows = names.map((n) => {
      const b = recorded.get(n)!;
      return `    "${n}": { "hash": "${b.hash}", "size": ${b.size} }`;
    });
    const text = `{\n  "target": "${BASELINE_TARGET}",\n  "cases": {\n${rows.join(",\n")}\n  }\n}\n`;
    const written = be.writeFileAtomic(__KIT_EDGES_GOLDEN__, new TextEncoder().encode(text));
    expect(written, `write ${__KIT_EDGES_GOLDEN__}`).toBe(true);
    console.log(`# recorded ${names.length} banks to ${__KIT_EDGES_GOLDEN__}`);
    return;
  }
  expect(golden!.target).toBe(BASELINE_TARGET);
  expect([...recorded.keys()].sort()).toEqual(Object.keys(golden!.cases).sort());
});
