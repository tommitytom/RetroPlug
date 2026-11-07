// `retroplug-cli render`'s tool (cli/sessions/render.ts) held to the render baseline: its arguments become an engine
// job (docs/modular-graph-phase-d3-design.md section 5), so the files it writes are the files the baseline's case of
// the same spec records, hash for hash on the baseline's host (the sizes anywhere), and the tool's console output is
// the render library's own lines. A render that fails ends the tool with the job's message, and --list-songs stays
// in-process.
//
// The tool runs here against the test host's session, not the launcher: the host fires no timers, so the tool's
// setTimeout, which it hands the offline poll to yield to a launcher's loop, is given a stand-in that resumes at once.
// The launcher's own half (an unbounded pump, a Ctrl-C between steps, exit codes, the output streams) is held against
// the real binary by tools/run-cli-ts-tests.sh (pnpm test:cli-ts).
import { test, expect, skip } from "@retroplug/testing";
import { bootSession } from "../src/bootSession";
import { renderTool } from "../cli/sessions/render";
import { caseNamed, missingFixture, writeScratch } from "./render-cases";
import { fnv1a64Hex } from "./kit/sources";

declare const __RENDER_GOLDEN__: string;
declare const __REPO_RESOURCES_DIR__: string;

interface Golden {
  cases: Record<string, { ms: number; files: { file: string; hash: string; size: number }[] }>;
}

const session = bootSession();
const be = session.backend;
const SCRATCH = `${be.configDir()}/render-cli`;
writeScratch(be, SCRATCH);

const BASELINE_TARGET = "linux-x64-gnu13";
const golden = (): Golden => JSON.parse(new TextDecoder().decode(be.readFile(__RENDER_GOLDEN__)!)) as Golden;

/** Run the render tool with `args`, with the timer the launcher would give it and its console captured. */
async function runTool(args: string[]): Promise<string[]> {
  const g = globalThis as { setTimeout?: unknown };
  const saved = g.setTimeout;
  g.setTimeout = (fn: () => void) => void Promise.resolve().then(fn);
  const lines: string[] = [];
  const log = console.log;
  const err = console.error;
  console.log = (s: string) => void lines.push(s);
  console.error = (s: string) => void lines.push(`stderr: ${s}`);
  try {
    await renderTool.run(session, args);
  } finally {
    console.log = log;
    console.error = err;
    g.setTimeout = saved;
  }
  return lines;
}

/** Render baseline case `name` through the tool, with the flags its spec maps to, and hold its files to the golden. */
async function holdThroughTool(
  name: string,
  flags: string[],
  outs: string[],
): Promise<{ lines: string[]; rom: string; out: string }> {
  const c = caseNamed(name);
  const missing = missingFixture(be, c.fixture);
  if (missing) skip(`render-cli: ${missing}`);
  const spec = c.spec(SCRATCH);
  const base = `${SCRATCH}-${name.split("/").join("_")}`;
  const split = outs.length !== 1 || outs[0] !== "";
  const args = [
    spec.rom as string,
    ...(spec.sav ? ["--sav", spec.sav as string] : []),
    ...flags,
    "--out",
    split ? base : `${base}.wav`,
  ];
  const lines = await runTool(args);
  const expected = golden().cases[name];
  const onTarget = session.audio.buildTarget() === BASELINE_TARGET;
  expect(outs.length, `${name}: files in the golden`).toBe(expected.files.length);
  outs.forEach((suffix, k) => {
    const path = split ? `${base}${suffix}.wav` : `${base}.wav`;
    const bytes = be.readFile(path);
    expect(bytes !== null, `${name}${suffix} was written`).toBe(true);
    be.deleteFile(path);
    expect(bytes!.length, `${name}${suffix}'s size`).toBe(expected.files[k].size);
    if (onTarget) expect(fnv1a64Hex(bytes!), `${name}${suffix}'s hash`).toBe(expected.files[k].hash);
  });
  return { lines, rom: spec.rom as string, out: split ? base : `${base}.wav` };
}

test("a smsggdj song rendered by the tool is the baseline's file", async () => {
  const { lines, rom, out } = await holdThroughTool(
    "smsggdj/sms/index-0/mix",
    ["--song-index", "0", "--duration", "4s"],
    [""],
  );
  // The library's own lines, in order, on the stream it logs them to.
  expect(lines).toEqual([
    'song "METRO" (slot 0) \u2192 work RAM',
    `rendering ${rom} \u2192 ${out} (4000ms)\u2026`,
    out,
    `rendered ${rom} \u2192 ${out} (@44100Hz)`,
  ]);
});

test("an LSDj render by the tool, mix and stems, is the baseline's files", async () => {
  await holdThroughTool("lsdj/tripledipper/fixed-5s/mix", ["--duration", "5s"], [""]);
  await holdThroughTool("lsdj/tripledipper/fixed-5s/channels", ["--duration", "5s", "--split", "channels"], [
    "_pulse1",
    "_pulse2",
    "_wave",
    "_noise",
  ]);
});

test("a render that fails ends the tool with the job's message", async () => {
  let message = "";
  try {
    await runTool([`${SCRATCH}-no-such.gb`, "--duration", "1s"]);
  } catch (e) {
    message = (e as Error).message;
  }
  expect(message.length > 0, "the tool rejected").toBe(true);
  expect(message.includes("no-such.gb"), `the message names the ROM: ${message}`).toBe(true);
});

test("a usage error is thrown before any job is submitted", async () => {
  let message = "";
  try {
    await runTool(["--split", "sideways", "x.gb"]);
  } catch (e) {
    message = (e as Error).message;
  }
  expect(message.startsWith("render: --split must be one of")).toBe(true);
});

test("a cancelled render ends cancelled and leaves no WAV behind (its header would claim no audio)", async () => {
  const c = caseNamed("smsggdj/sms/index-0/mix");
  const out = `${SCRATCH}-cancelled.wav`;
  let started = false;
  const handle = be.engineRun("render", { ...c.spec(SCRATCH), durationMs: 120000, out }, {
    onProgress: () => {
      // The first chunk is rendered, so the render's `<out>.<token>.part` is open with its placeholder header.
      if (!started) {
        started = true;
        handle.cancel();
      }
    },
  });
  const result = await handle;
  expect(result.status).toBe("cancelled");
  expect(be.fileExists(out), "no WAV was moved into place").toBe(false);
  const slash = out.lastIndexOf("/");
  const name = out.slice(slash + 1);
  const left = be.listDir(out.slice(0, slash)).filter((n) => n.startsWith(`${name}.`) && n.endsWith(".part"));
  expect(left, "the partial WAV was removed").toEqual([]);
});

test("tools/fixtures/render-smsggdj.sav, which test:cli-ts renders, is the baseline's smsggdj cases' battery", () => {
  const committed = be.readFile(`${__REPO_RESOURCES_DIR__}/../tools/fixtures/render-smsggdj.sav`);
  const authored = be.readFile(`${SCRATCH}-smsggdj.sav`);
  expect(committed !== null && authored !== null).toBe(true);
  expect(fnv1a64Hex(committed!)).toBe(fnv1a64Hex(authored!));
  expect(committed!.length).toBe(authored!.length);
});
