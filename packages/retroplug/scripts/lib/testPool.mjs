// Shared parallelism helper for the test runners (run-tests / run-native-tests /
// run-ui-tests / run-plugin-tests). Each runner's unit of work is an isolated child
// process, so the suites are embarrassingly parallel — this turns the old serial
// `for … spawnSync` loop into a bounded worker pool without changing what the tests
// assert. Output is buffered per child and flushed as a labelled block on completion
// (live `stdio: "inherit"` would interleave illegibly under concurrency).
//
// Concurrency defaults to half the logical threads; override with `--jobs N` / `-j N`
// on the runner argv or the `TEST_JOBS` env. `TEST_JOBS=1` restores serial behaviour.

import { spawn } from "node:child_process";
import { availableParallelism, cpus } from "node:os";

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

// Strip the jobs flags from a runner's argv so the remaining positional slug filter is
// unaffected (the runners read argv[0] as their filter).
export function stripJobsArgs(argv = process.argv.slice(2)) {
  const out = [];
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (a === "--jobs" || a === "-j") { i++; continue; } // also drop its value
    if (a.startsWith("--jobs=") || (a.startsWith("-j") && a.length > 2)) continue;
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

// Bounded-concurrency async pool. Runs `worker(item, index)` for every item with at
// most `jobs` in flight; returns results in input order. A worker that throws yields
// its thrown value's absence as `undefined` in the results — runners treat a thrown
// worker as a failure explicitly, so we let it propagate via the returned value shape.
export async function runPool(items, worker, { jobs = 1 } = {}) {
  const results = new Array(items.length);
  let next = 0;

  async function run() {
    while (true) {
      const i = next++;
      if (i >= items.length) return;
      results[i] = await worker(items[i], i);
    }
  }

  const n = Math.max(1, Math.min(jobs, items.length || 1));
  await Promise.all(Array.from({ length: n }, run));
  return results;
}

// Print a child's buffered output as one labelled block (grouped, not interleaved).
export function flush(label, output) {
  process.stderr.write(`\n# ${label}\n`);
  if (output) process.stdout.write(output.endsWith("\n") ? output : output + "\n");
}
