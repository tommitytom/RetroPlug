// `retroplug-cli test <dir>` - strip and run a directory of TypeScript test files.
//
// This is what lets a consumer repo (BlipToaster) use the CLI as its whole test harness with nothing
// installed: no Node, no npm, no esbuild, no node_modules. The kit is the binary, the SDK `.js`, the
// `.d.ts`, and the tests.
//
// Two things make it work, both pre-existing:
//   * The CLI's txiki runtime resolves `import` off disk at runtime, so tests need no bundling; they
//     just have to be JavaScript.
//   * Consumer tests already use explicit `.js` specifiers ("./helper.js", "../sdk/retroplug-cli.js"),
//     so stripping `X.ts` -> `X.js` at the same directory DEPTH needs no specifier rewriting. See
//     buildDirFor in ../tsStrip.
//
// Each test file runs in its OWN PROCESS, which is required rather than tidy: the TAP harness calls
// tjs.exit when a file finishes, its module-level case list is shared once the SDK resolves to a single
// module, and the native Engine is per-process, so systems loaded by one file would leak into the next.
//
// That isolation is also what makes the suite embarrassingly parallel, so files run in a bounded pool
// (default half the logical cores; --jobs N / -j N, or RP_TEST_JOBS). A consumer's suite is emulator
// work - each file boots a core and renders seconds of audio - so this is the difference between a
// coffee break and a few seconds. Under concurrency each child's output is BUFFERED and flushed as one
// labelled block on completion; several children writing a terminal at once would shred the TAP. At
// --jobs 1 the children inherit stdio exactly as they always did, so a single file being debugged still
// streams live.

import { keepAlive, exitProcess } from "../session";
import type { Session } from "../session";
import type { CliTool } from "../tools";
import { buildTsDir, resolveBuildDir } from "../tsStrip";
import { spawnSession, spawnSessionCaptured } from "../childSession";
import { ensureSdk, sdkDirFor } from "../sdkAssets";

declare const tjs: { tmpDir: string; env: Record<string, string>; system: { cpus: unknown[] } };

export interface TestArgs {
  dir: string;
  /** Substring match on the test file name; empty runs everything. */
  filter: string;
  /** Forwarded to every test file as its first session argument (BlipToaster tests read a ROM path). */
  rom: string | null;
  /** Override the stripped-output directory (default: the source dir's sibling .rp-test-build). */
  out: string | null;
  /** Extra session arguments, after `--`, appended to every test file's argv. */
  passthrough: string[];
  /** Files to run at once. Null = not given on the argv; resolveJobs then falls back to env/default. */
  jobs: number | null;
}

/** Parse `test`'s arguments. Pure, so the unit tests can cover it without a binary. */
export function parseTestArgs(args: string[]): TestArgs {
  const out: TestArgs = { dir: "", filter: "", rom: null, out: null, passthrough: [], jobs: null };
  const dashDash = args.indexOf("--");
  const head = dashDash < 0 ? args : args.slice(0, dashDash);
  out.passthrough = dashDash < 0 ? [] : args.slice(dashDash + 1);

  const positional: string[] = [];
  for (let i = 0; i < head.length; i++) {
    const a = head[i];
    if (a === "--rom") out.rom = head[++i] ?? null;
    else if (a === "--out") out.out = head[++i] ?? null;
    else if (a === "--jobs" || a === "-j") out.jobs = parseJobs(head[++i]);
    else if (a.startsWith("--jobs=")) out.jobs = parseJobs(a.slice("--jobs=".length));
    else if (a.startsWith("-j") && a.length > 2) out.jobs = parseJobs(a.slice(2));
    else positional.push(a);
  }
  out.dir = positional[0] ?? "";
  out.filter = positional[1] ?? "";
  return out;
}

/** A positive integer, or null for anything else - a bad value falls back rather than running 0 files
 *  (`--jobs 0` would otherwise be a suite that silently passes by doing nothing). */
function parseJobs(raw: string | undefined): number | null {
  if (raw === undefined) return null;
  const n = Number.parseInt(raw, 10);
  return Number.isFinite(n) && n > 0 ? n : null;
}

/** Concurrency for this run: --jobs/-j > RP_TEST_JOBS > half the logical cores (min 1). Half rather
 *  than all, to match RetroPlug's own runners and because each child is a whole emulator, not a
 *  thread. Never more than the number of files. */
export function resolveJobs(fromArgs: number | null, env: Record<string, string>, cpus: number, fileCount: number): number {
  const jobs = fromArgs ?? parseJobs(env.RP_TEST_JOBS) ?? Math.max(1, Math.floor((cpus || 1) / 2));
  return Math.max(1, Math.min(jobs, fileCount || 1));
}

/** The emitted files that are test files, in the order they should run. */
export function selectTests(emitted: string[], filter: string): string[] {
  return emitted
    .filter((f) => f.endsWith(".test.js"))
    .filter((f) => !filter || f.includes(filter))
    .sort();
}

/** Dispatch order: slowest first, from the durations the previous run recorded.
 *
 *  Alphabetical order is uncorrelated with cost, so the pool can pick up the longest file last and then
 *  run it alone while every other worker idles - the wall clock ends up bounded by WHEN the big file was
 *  reached rather than by the file itself. Slowest-first makes the floor the slowest file, which is as
 *  good as a pool of independent processes gets.
 *
 *  A file with no recorded time sorts FIRST: it is new, renamed, or this is a first run, and an
 *  unmeasured file may well be the expensive one. Ties keep alphabetical order so a run stays
 *  reproducible. Pure, so the unit tests cover it without a binary. */
export function orderLongestFirst(tests: string[], timings: Record<string, number>): string[] {
  const cost = (f: string): number => {
    const v = timings[f];
    return typeof v === "number" && Number.isFinite(v) ? v : Infinity;
  };
  return [...tests].sort((a, b) => cost(b) - cost(a) || (a < b ? -1 : a > b ? 1 : 0));
}

const help = `usage: retroplug-cli test <dir> [name-filter] [options] [-- session-args...]

Strip and run every *.test.ts in <dir>. Each file runs in its own process (a fresh Engine and a fresh
config dir), and the exit code is nonzero if any file fails, so this is a real pass/fail gate.

  <dir>             directory of *.test.ts / *.test.js files
  [name-filter]     only run files whose name contains this substring

options:
  --rom <path>      passed to every test file as its first argument
  --out <dir>       where to write stripped output (default: <dir>'s sibling .rp-test-build, or a
                    directory under the temp dir when that sibling is not writable)
  --jobs N, -j N    run N files at once (default: half the logical cores; also RP_TEST_JOBS).
                    -j1 runs them one at a time and streams each child's output live.
  -- <args...>      extra arguments appended to every test file's argv

TypeScript is stripped, not compiled: only erasable syntax is supported. enum, namespace and
constructor parameter properties emit runtime code and are rejected with a file:line:col error.

example:
  retroplug-cli test tests --rom rom/build/bliptoaster.nes
  retroplug-cli test tests pulse --rom rom/build/bliptoaster.nes`;

export const testTool: CliTool = {
  name: "test",
  summary: "Strip and run a directory of TypeScript test files",
  help,
  // Spawning children and awaiting them is async, so the dispatcher must not auto-exit us; we report
  // the exit code ourselves once every file has been waited on.
  longRunning: true,
  run(s: Session, args: string[]): void {
    const opts = parseTestArgs(args);
    if (!opts.dir) {
      console.error("retroplug-cli test: missing <dir>\n\n" + help);
      exitProcess(2);
      return;
    }

    const { outDir, fellBack } = resolveBuildDir(s.backend, opts.dir, opts.out, tjs.tmpDir);
    if (fellBack) console.error(`note: the sibling build dir is not writable; stripping into ${outDir} (override with --out)`);
    const { emitted, needsSdk } = buildTsDir(s.backend, opts.dir, outDir);

    // The binary owns the SDK: refresh it next to the tests if it is missing or stale, so the copy a
    // test imports can never lag the binary running it. Only when something actually imports it.
    if (needsSdk) ensureSdk(s.backend, sdkDirFor(opts.dir));

    const tests = selectTests(emitted, opts.filter);

    if (tests.length === 0) {
      console.error(`no tests matched in ${opts.dir}${opts.filter ? ` (filter: ${opts.filter})` : ""}`);
      exitProcess(2);
      return;
    }

    const sessionArgs = [...(opts.rom ? [opts.rom] : []), ...opts.passthrough];

    const jobs = resolveJobs(opts.jobs, tjs.env, tjs.system.cpus.length, tests.length);

    keepAlive(); // opt into the run-until-exit pump while the children run
    void runAll(s, tests, outDir, sessionArgs, jobs);
  },
};

// Per-file durations from the last run, kept inside the stripped-output dir - a derived directory this
// tool already owns and creates, so a consumer gains no tracked file. Losing it costs one unordered run.
const timingsPathFor = (outDir: string): string => `${outDir}/.timings.json`;

function loadTimings(backend: Session["backend"], outDir: string): Record<string, number> {
  const path = timingsPathFor(outDir);
  if (!backend.fileExists(path)) return {};
  try {
    const raw = backend.readFile(path);
    if (!raw) return {};
    const data: unknown = JSON.parse(new TextDecoder().decode(raw));
    return data && typeof data === "object" ? (data as Record<string, number>) : {};
  } catch {
    return {}; // a truncated or hand-edited file is a scheduling hint, not a reason to fail a suite
  }
}

/** Merge rather than replace: a filtered run measures a handful of files, and overwriting would discard
 *  every other file's time and blind the next full run's ordering. Best-effort - a read-only tree must
 *  not fail a suite that otherwise passed. */
function saveTimings(backend: Session["backend"], outDir: string, merged: Record<string, number>): void {
  try {
    backend.writeFile(timingsPathFor(outDir), new TextEncoder().encode(JSON.stringify(merged) + "\n"));
  } catch {
    /* ignore */
  }
}

/** Run one file. A fresh config dir per file so runs never cross-contaminate (mirrors what the Node
 *  runner did with mkdtemp). The child creates it on first write - writeFile makes parent dirs on
 *  demand. Serial runs inherit stdio so output streams live; parallel runs buffer and flush as a block. */
async function runOne(file: string, outDir: string, sessionArgs: string[], live: boolean): Promise<number> {
  const name = file.replace(/\.test\.js$/, "");
  const env = { RETROPLUG_USER_CONFIG_DIR: `${outDir}/.cfg/${name}` };

  if (live) {
    console.log(`\n# ${file}`);
    return await spawnSession(`${outDir}/${file}`, sessionArgs, env);
  }

  const { code, output } = await spawnSessionCaptured(`${outDir}/${file}`, sessionArgs, env);
  // Header and body printed together, so a block cannot be attributed to the wrong file.
  console.log(`\n# ${file}\n${output.endsWith("\n") ? output.slice(0, -1) : output}`);
  return code;
}

async function runAll(s: Session, tests: string[], outDir: string, sessionArgs: string[], jobs: number): Promise<void> {
  const live = jobs === 1;
  const failures: string[] = [];
  const measured: Record<string, number> = {};
  const past = loadTimings(s.backend, outDir);
  // Serial runs keep the listed order: there is nothing to pack, and a predictable order is easier to
  // follow when you are watching one file at a time.
  const queue = live ? tests : orderLongestFirst(tests, past);
  let next = 0;

  // Bounded pool: `jobs` workers each pulling the next file until the list is exhausted. Files are
  // reported as they FINISH, not in list order, so a long file does not hold up everything behind it.
  async function worker(): Promise<void> {
    for (;;) {
      const i = next++;
      if (i >= queue.length) return;
      const file = queue[i];
      const started = Date.now();
      const code = await runOne(file, outDir, sessionArgs, live);
      measured[file] = Date.now() - started;
      if (code !== 0) failures.push(file);
    }
  }
  await Promise.all(Array.from({ length: jobs }, () => worker()));
  saveTimings(s.backend, outDir, { ...past, ...measured });

  const total = tests.length;
  console.log(`\n${failures.length === 0 ? "PASS" : "FAIL"}: ${total - failures.length}/${total} test file(s) ok`);
  // Name them: with files reported in completion order, a failure is easy to scroll past.
  if (failures.length) console.log(`failed: ${failures.sort().join(", ")}`);
  exitProcess(failures.length === 0 ? 0 : 1);
}
