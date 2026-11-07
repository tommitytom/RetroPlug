// The render baseline (docs/modular-graph-phase-d3-design.md section 6): every case of render-cases.ts rendered as an
// engine job, in continuous blocks (section 4), each WAV it writes held to test-native/golden/render-baseline.json.
// The cases are render-cases.ts's CASES; this file is the baseline's only runner, and the golden holds the target and
// each case's record.
//
// The rules the golden records under, the kit baseline's (kit-baseline.test.ts) and parity's refusal of silence. A
// record is the length the render ended at (the job's renderedMs) and, per file, its FNV-1a-64 in lowercase hex and
// its size. The length and the sizes are compared on every host; the hashes only on the host the golden was recorded on
// (`linux-x64-gnu13`, the engine's buildTarget), because float arithmetic is bit-stable only for one compiler on one
// architecture. Elsewhere a case renders twice and must agree with itself, and RP_RENDER_GOLDEN_REQUIRE_TARGET=1 makes
// a foreign host fail instead (CI's linux-x64 job sets it on its test:native step). The case names must be the
// golden's keys exactly. Every case must sound, and every file of it but the ones its case names as silent (a stem its
// song never plays), which a recording checks as well.
//
// The cores power on in a fixed state (the runner sets RETROPLUG_DETERMINISTIC for this file, which reaches the
// engine job's own Engine: the variable is the process's), so two renders of one case agree. The smsggdj and SGB
// cases need no fixture and run on CI; the LSDj and risa cases need the fixture tree (RETROPLUG_RESOURCES_DIR) and
// SKIP without it, so this baseline is held whole only on a run with the fixtures and RP_FAIL_ON_SKIP=1.
//
// RP_RENDER_GOLDEN_UPDATE=1 re-records the golden, refused anywhere but the target, for any case whose two renders
// disagree, and for a run missing any case (fixtures absent): its last case writes the file, through a sibling temp
// file. Without the variable the file is only ever read.
import { test, expect, skip } from "@retroplug/testing";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import {
  CASES,
  caseNamed,
  missingFixture,
  renderCase,
  writeScratch,
  type Fixture,
  type Rendered,
  type RenderCase,
} from "./render-cases";

declare const __RENDER_GOLDEN__: string;
declare const tjs: { env: Record<string, string> };

const be = createRealBackend();
const audio = createAudioDriver(); // the offline poll's wait: this host has no UI tick

const BASELINE_TARGET = "linux-x64-gnu13";
const UPDATE = tjs.env.RP_RENDER_GOLDEN_UPDATE === "1";
const REQUIRE_TARGET = tjs.env.RP_RENDER_GOLDEN_REQUIRE_TARGET === "1";

// This run's scratch prefix, in its own config dir, which the runner deletes.
const SCRATCH = `${be.configDir()}/render-baseline`;
writeScratch(be, SCRATCH);

interface FileGolden {
  file: string; // the suffix: "" for a mix
  hash: string;
  size: number;
}
interface CaseGolden {
  ms: number;
  files: FileGolden[];
}
interface Golden {
  target: string;
  cases: Record<string, CaseGolden>;
}

const recorded: Record<string, CaseGolden> = {};

function readGolden(): Golden {
  const bytes = be.readFile(__RENDER_GOLDEN__);
  if (!bytes)
    throw new Error(`cannot read ${__RENDER_GOLDEN__}; record it with RP_RENDER_GOLDEN_UPDATE=1 on ${BASELINE_TARGET}`);
  return JSON.parse(new TextDecoder().decode(bytes)) as Golden;
}

// The golden's format: cases in byte order, one line per file, so a recording of the same renders is the same file.
function goldenText(golden: Golden): string {
  const names = Object.keys(golden.cases).sort((a, b) => (a < b ? -1 : a > b ? 1 : 0));
  const rows = names.map((n) => {
    const c = golden.cases[n];
    const files = c.files.map((f) => `      { "file": "${f.file}", "hash": "${f.hash}", "size": ${f.size} }`);
    return `    "${n}": {\n      "ms": ${c.ms},\n      "files": [\n  ${files.join(",\n  ")}\n      ]\n    }`;
  });
  return `{\n  "target": "${golden.target}",\n  "cases": {\n${rows.join(",\n")}\n  }\n}\n`;
}

const record = (r: Rendered): CaseGolden => ({
  ms: r.result.renderedMs,
  files: r.files.map((f) => ({ file: f.suffix, hash: f.hash, size: f.size })),
});

// What every render of a case must be, golden or not: done, every file written, the case sounding, and every file
// but the ones it may leave silent.
function checkRendered(c: RenderCase, r: Rendered): void {
  expect(r.result.status, `${c.name}: ${r.result.message}`).toBe("done");
  expect(
    r.files.map((f) => f.suffix),
    `${c.name} writes its files`,
  ).toEqual(c.outputs);
  expect(
    r.files.some((f) => f.sounds),
    `${c.name} sounds`,
  ).toBe(true);
  for (const f of r.files)
    if (!(c.silent ?? []).includes(f.suffix)) expect(f.sounds, `${c.name}${f.suffix} sounds`).toBe(true);
}

/** Render `fixture`'s cases (all at once: the engine's pool runs them side by side) and hold them to the golden, or
 *  record them. */
async function holdCases(fixture: Fixture): Promise<Map<string, Rendered>> {
  const cases = CASES.filter((c) => c.fixture === fixture);
  const missing = missingFixture(be, fixture);
  if (missing) skip(`render-baseline: ${missing}`);
  const host = audio.buildTarget();
  const onTarget = host === BASELINE_TARGET;
  if (UPDATE && !onTarget)
    throw new Error(
      `a recording only happens on ${BASELINE_TARGET}, and this host is ${host}; ` +
        "a digest recorded here would be a fallback, not the baseline",
    );
  if (!UPDATE && REQUIRE_TARGET && !onTarget)
    throw new Error(
      `this host is ${host} but the baseline was recorded on ${BASELINE_TARGET}, so exact bytes would not be compared`,
    );

  const first = await Promise.all(cases.map((c) => renderCase(be, c, SCRATCH, "a")));
  const second = UPDATE || !onTarget ? await Promise.all(cases.map((c) => renderCase(be, c, SCRATCH, "b"))) : null;
  const golden = UPDATE ? null : readGolden();
  cases.forEach((c, k) => {
    const r = first[k];
    checkRendered(c, r);
    // A case whose two renders disagree depends on something outside its spec: its digest would mean nothing.
    if (second) expect(record(second[k]), `${c.name} renders twice to the same files`).toEqual(record(r));
    if (UPDATE) {
      recorded[c.name] = record(r);
      return;
    }
    const expected = golden!.cases[c.name];
    expect(expected !== undefined, `${c.name} is in the golden`).toBe(true);
    expect(r.result.renderedMs, `${c.name}'s length`).toBe(expected.ms);
    expect(
      r.files.map((f) => f.size),
      `${c.name}'s sizes`,
    ).toEqual(expected.files.map((f) => f.size));
    if (onTarget)
      expect(
        r.files.map((f) => f.hash),
        `${c.name}'s hashes`,
      ).toEqual(expected.files.map((f) => f.hash));
  });
  console.log(
    `# ${cases.length} ${fixture === "none" ? "fixture-free" : fixture} renders ` +
      (UPDATE ? "recorded" : onTarget ? "match the golden's hashes" : `agree with themselves on ${host}`),
  );
  return new Map(cases.map((c, k) => [c.name, first[k]]));
}

test("the matrix is the golden's: its names are the golden's keys", () => {
  expect(new Set(CASES.map((c) => c.name)).size).toBe(CASES.length);
  if (UPDATE) return; // a recording writes the keys this matrix has
  const golden = readGolden();
  expect(golden.target).toBe(BASELINE_TARGET);
  expect(CASES.map((c) => c.name).sort()).toEqual(Object.keys(golden.cases).sort());
});

test("the fixture-free renders (smsggdj songs, the SGB jingle) write the files the golden records", async () => {
  await holdCases("none");
});

// What the two checks below prove about a recording: a run in which either failed would bake the misalignment into the
// golden it writes, so the recording writes only when both held (as kit-baseline's sourcesProven).
let lsdjEndsProven = false;
let stemsAligned = false;

test("LSDj renders to the files the golden records", async () => {
  const rendered = await holdCases("lsdj");
  // A song detected to its HFF stop ends at one frame whether it renders as a mix or as stems. Coarse: the end is
  // quantised to the 100 ms poll, so this would rarely catch stems starting a part block late; the next case does.
  const mix = rendered.get("lsdj/telemelt/hff/name-machete/mix")!;
  const stems = rendered.get("lsdj/telemelt/hff/index-3/channels")!;
  expect(stems.frames, "the stems' HFF end").toBe(mix.frames);
  expect(stems.result.renderedMs).toBe(mix.result.renderedMs);
  lsdjEndsProven = true;
});

// The left lane of a 16-bit stereo WAV's samples.
function leftLane(wav: Uint8Array): Float64Array {
  const view = new DataView(wav.buffer, wav.byteOffset + 44, wav.length - 44);
  const out = new Float64Array(Math.floor(view.byteLength / 4));
  for (let f = 0; f < out.length; f++) out[f] = view.getInt16(f * 4, true);
  return out;
}

// The lag in [-max, max] at which `b` best matches `a` over `len` frames from `at`: the peak of their normalised
// cross-correlation.
function bestLag(a: Float64Array, b: Float64Array, at: number, len: number, max: number): number {
  let best = 0;
  let bestScore = -Infinity;
  for (let lag = -max; lag <= max; lag++) {
    let dot = 0;
    let nb = 0;
    for (let i = at; i < at + len; i++) {
      dot += a[i] * b[i + lag];
      nb += b[i + lag] * b[i + lag];
    }
    const score = nb > 0 ? dot / Math.sqrt(nb) : -Infinity;
    if (score > bestScore) {
      bestScore = score;
      best = lag;
    }
  }
  return best;
}

test("an LSDj split render's stems line up with its mix: their sum matches it at lag 0", async () => {
  // A split render settles in its stream's shape (render.ts runRenderJob), so the block its settle leaves over is
  // its own and its stems start at the frame its mix does. Settled in the mix instead, the per-channel calls would
  // drop that carry (EngineRpcService::setContinuousBlocks) and the stems would start the rest of the settle's last
  // block later: 410 frames at 44.1 kHz.
  const missing = missingFixture(be, "lsdj");
  if (missing) skip(`render-baseline: ${missing}`);
  const [mix, stems] = await Promise.all(
    ["lsdj/tripledipper/fixed-5s/mix", "lsdj/tripledipper/fixed-5s/channels"].map((n) =>
      renderCase(be, caseNamed(n), SCRATCH, "align", {}, true),
    ),
  );
  for (const r of [mix, stems]) expect(r.result.status, r.result.message).toBe("done");
  const m = leftLane(mix.files[0].bytes!);
  const sum = new Float64Array(m.length);
  for (const f of stems.files) leftLane(f.bytes!).forEach((v, i) => (sum[i] += v));
  let first = 0;
  while (first < m.length && m[first] === 0) first++;
  const at = first + 2048; // a window inside the song, with room for the lags either side
  expect(at + 8192 + 1024 < m.length, "the mix sounds early enough to compare").toBe(true);
  expect(bestLag(m, sum, at, 8192, 1024), "the summed stems' lag against the mix").toBe(0);
  stemsAligned = true;
});

test("risa renders to the files the golden records", async () => {
  await holdCases("risa");
});

if (UPDATE)
  test("record every case to the golden", () => {
    const missing = CASES.filter((c) => recorded[c.name] === undefined).map((c) => c.name);
    if (missing.length)
      throw new Error(`a recording holds every case, and these were not rendered: ${missing.join(", ")}`);
    // The case runs even after an earlier one failed, so a run whose stems or ends were not held records nothing.
    if (!lsdjEndsProven || !stemsAligned)
      throw new Error("a recording needs the LSDj stems to end where the mix does and to line up with it at lag 0");
    const text = new TextEncoder().encode(goldenText({ target: BASELINE_TARGET, cases: recorded }));
    expect(be.writeFileAtomic(__RENDER_GOLDEN__, text), `write ${__RENDER_GOLDEN__}`).toBe(true);
    console.log(`# recorded ${CASES.length} renders to ${__RENDER_GOLDEN__}`);
  });
