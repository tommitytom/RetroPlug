// Shared parallelism helper for the test runners (run-tests / run-native-tests /
// run-ui-tests / run-plugin-tests). Each runner's unit of work is an isolated child
// process, so the suites are embarrassingly parallel — this turns the old serial
// `for … spawnSync` loop into a bounded worker pool without changing what the tests
// assert. Output is buffered per child and flushed as a labelled block on completion
// (live `stdio: "inherit"` would interleave illegibly under concurrency).
//
// Concurrency defaults to half the logical threads; override with `--jobs N` / `-j N`
// on the runner argv or the `TEST_JOBS` env. `TEST_JOBS=1` restores serial behaviour.
//
// Work is dispatched LONGEST-FIRST, from durations recorded by the previous run (see
// `timings` on runPool). These suites are extremely skewed — a handful of files that
// boot a real core and render tens of seconds of audio, against a tail of sub-second
// ones — and a long file picked up late runs alone at the end while every worker but
// one sits idle. Longest-first makes the wall clock bounded by the SLOWEST FILE rather
// than by when the pool happened to reach it. Files with no recorded time (new, renamed,
// or a first-ever run) sort first, since an unmeasured file may well be the expensive one.

import { spawn } from "node:child_process";
import { availableParallelism, cpus } from "node:os";
import { readFileSync, writeFileSync, mkdirSync } from "node:fs";
import { dirname } from "node:path";

// Resolve the concurrency for this run. Precedence: --jobs/-j argv > TEST_JOBS env >
// default (half the logical threads, min 1). Returns a positive integer.
export function resolveJobs(argv = process.argv.slice(2)) {
  let jobs;

  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    let v;
    if (a === "--jobs" || a === "-j") v = argv[i + 1];
    else if (a.startsWith("--jobs=")) v = a.slice("--jobs=".length);
    else if (a.startsWith("-j") && a.length > 2) v = a.slice(2);
    if (v !== undefined) {
      const n = Number.parseInt(v, 10);
      if (Number.isFinite(n) && n > 0) jobs = n;
    }
  }

  if (jobs === undefined && process.env.TEST_JOBS) {
    const n = Number.parseInt(process.env.TEST_JOBS, 10);
    if (Number.isFinite(n) && n > 0) jobs = n;
  }

  if (jobs === undefined) {
    const threads = (availableParallelism?.() ?? cpus().length) || 1;
    jobs = Math.max(1, Math.floor(threads / 2));
  }

  return jobs;
}

// Strip the runner's own flags from argv so the remaining positional slug filter is unaffected (the
// runners read argv[0] as their filter). Anything added here must also be listed, or a new flag silently
// becomes a filter that matches no test.
const VALUELESS_FLAGS = new Set(["--update-skip-baseline"]);

export function stripRunnerFlags(argv = process.argv.slice(2)) {
  const out = [];
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (a === "--jobs" || a === "-j") { i++; continue; } // also drop its value
    if (a.startsWith("--jobs=") || (a.startsWith("-j") && a.length > 2)) continue;
    if (VALUELESS_FLAGS.has(a)) continue;
    out.push(a);
  }
  return out;
}

// Promise wrapper over child_process.spawn that captures stdout+stderr into one buffer
// (so parallel children don't interleave on the TTY). Resolves { status, output };
// a spawn error resolves as status 1 with the error text in the buffer (never rejects).
export function spawnBuffered(cmd, args, opts = {}) {
  return new Promise((resolvePromise) => {
    const child = spawn(cmd, args, { ...opts, stdio: ["ignore", "pipe", "pipe"] });
    const chunks = [];
    child.stdout.on("data", (d) => chunks.push(d));
    child.stderr.on("data", (d) => chunks.push(d));
    child.on("error", (err) => {
      chunks.push(Buffer.from(`\n[spawn error] ${err?.message ?? err}\n`));
      resolvePromise({ status: 1, output: Buffer.concat(chunks).toString("utf8") });
    });
    child.on("close", (code) => {
      resolvePromise({ status: code ?? 1, output: Buffer.concat(chunks).toString("utf8") });
    });
  });
}

// Read a runner's recorded per-item durations. Missing/corrupt file => no timings, which
// just means this run dispatches in input order and records times for the next one.
function loadTimings(file) {
  try {
    const data = JSON.parse(readFileSync(file, "utf8"));
    return data && typeof data === "object" ? data : {};
  } catch {
    return {};
  }
}

// Persist durations for the next run. Best-effort: a read-only or racing checkout must not
// fail a suite that otherwise passed, so every error here is swallowed.
function saveTimings(file, timings) {
  try {
    mkdirSync(dirname(file), { recursive: true });
    writeFileSync(file, JSON.stringify(timings, null, 1) + "\n");
  } catch {
    /* scheduling hint only — never worth failing a run over */
  }
}

// Bounded-concurrency async pool. Runs `worker(item, index)` for every item with at
// most `jobs` in flight; returns results in input order. A worker that throws yields
// its thrown value's absence as `undefined` in the results — runners treat a thrown
// worker as a failure explicitly, so we let it propagate via the returned value shape.
//
// `timings: { file, key }` opts into longest-first dispatch: durations are read from
// `file`, used to order the work, then rewritten with this run's measurements. `key(item)`
// names an item stably across runs (a slug, not an index — indices shift when a filter is
// applied or a file is added). DISPATCH order changes; the returned results stay in INPUT
// order, because the runners pair them with their own `items` array by index.
export async function runPool(items, worker, { jobs = 1, timings } = {}) {
  const results = new Array(items.length);

  // Indices in the order they'll be picked up. Unknown durations sort first (Infinity),
  // then longest to shortest; ties keep input order so a run stays reproducible.
  const past = timings ? loadTimings(timings.file) : {};
  const cost = (i) => {
    const v = past[timings.key(items[i])];
    return typeof v === "number" && Number.isFinite(v) ? v : Infinity;
  };
  const order = items.map((_, i) => i);
  if (timings) order.sort((a, b) => cost(b) - cost(a) || a - b);

  const measured = {};
  let next = 0;

  async function run() {
    while (true) {
      const slot = next++;
      if (slot >= order.length) return;
      const i = order[slot];
      const started = Date.now();
      results[i] = await worker(items[i], i);
      if (timings) measured[timings.key(items[i])] = Date.now() - started;
    }
  }

  const n = Math.max(1, Math.min(jobs, items.length || 1));
  await Promise.all(Array.from({ length: n }, run));
  // Merge rather than replace: a filtered run ("pnpm test recent") measures a handful of
  // files, and overwriting would throw away every other file's time and blind the next
  // full run's ordering.
  if (timings) saveTimings(timings.file, { ...past, ...measured });
  return results;
}

// Parse a child's TAP output into per-file counts.
//
// Everything is derived from the RESULT LINES, never from the harness's own trailing summary comment, so
// the two cannot disagree - the comment is for a human reading the block, this is for the runner.
//
// `ok` also answers a question the exit code cannot: did the file account for itself? The harness buffers
// its whole report and prints it in one go at the end, and schedules that print only from inside test() -
// so a file that registers no cases (an empty table-driven array, an early tjs.exit) prints NOTHING and
// exits 0. That is a vacuous pass one level up from the skip: not a case that quietly did not run, but a
// whole FILE. A missing plan line, or a plan that disagrees with the result lines, fails the file.
export function parseTap(output) {
  let plan = -1;
  let pass = 0;
  let fail = 0;
  let skip = 0;
  for (const line of (output ?? "").split("\n")) {
    const m = /^1\.\.(\d+)\s*$/.exec(line);
    if (m) { plan = Number(m[1]); continue; }
    if (/^not ok \d+/.test(line)) { fail++; continue; }
    // TAP matches a directive case-insensitively; the harness writes it uppercase.
    if (/^ok \d+/.test(line)) { if (/\s#\s*skip\b/i.test(line)) skip++; else pass++; }
  }
  const counted = pass + fail + skip;
  let problem = null;
  if (plan < 0) problem = "no TAP plan line — the file registered no cases, or exited before reporting";
  else if (counted !== plan) problem = `TAP plan says ${plan} case(s), found ${counted}`;
  return { plan, pass, fail, skip, ok: problem === null, problem };
}

// Print a child's buffered output as one labelled block (grouped, not interleaved).
export function flush(label, output) {
  process.stderr.write(`\n# ${label}\n`);
  if (output) process.stdout.write(output.endsWith("\n") ? output : output + "\n");
}
