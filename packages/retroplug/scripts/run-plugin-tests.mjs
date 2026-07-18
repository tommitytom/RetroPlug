#!/usr/bin/env node
// Plugin unit-test runner. Runs the Catch2 C++ test binaries in a bounded parallel
// pool (default half the logical threads; --jobs N / -j N / TEST_JOBS, =1 for serial):
// the per-context window-hook routing (retroplug-plugin-test), the class-id
// counter sync that keeps the DAW-hosted editor from rendering blank
// (retroplug-classid-test), the per-channel audio taps (retroplug-audio-test),
// the native file watcher (retroplug-watcher-test), and the ThorVG-backed
// Lottie rasterization behind the <Lottie> component (retroplug-lottie-test).
//
// A tiny runner (rather than chaining `build/bin/foo && …` in package.json) so
// the suite is cross-platform: it appends `.exe` on Windows and spawns each
// binary directly, instead of relying on a POSIX shell to resolve the paths.
//
//   node scripts/run-plugin-tests.mjs [nameFilter]

import { existsSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join, resolve } from "node:path";
import { runPool, spawnBuffered, resolveJobs, stripJobsArgs, flush } from "./lib/testPool.mjs";

const PKG = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const REPO = resolve(PKG, "../..");
const BIN_DIR = process.env.RETROPLUG_BIN_DIR || join(REPO, "build", "bin");
const EXE = process.platform === "win32" ? ".exe" : "";

const BINARIES = ["retroplug-plugin-test", "retroplug-classid-test", "retroplug-audio-test", "retroplug-watcher-test", "retroplug-lottie-test"];

const jobs = resolveJobs();
const filter = stripJobsArgs()[0];
const selected = filter ? BINARIES.filter((b) => b.includes(filter)) : BINARIES;

if (!selected.length) {
  console.error(`no plugin test binaries match "${filter}"`);
  process.exit(1);
}

async function runOne(name) {
  const bin = join(BIN_DIR, name + EXE);
  if (!existsSync(bin)) {
    flush(name, `${name} not found: ${bin}\nbuild it once:  cmake --build build --target ${name} -j`);
    return false;
  }
  const run = await spawnBuffered(bin, [], { cwd: REPO });
  flush(name, run.output);
  return run.status === 0;
}

const results = await runPool(selected, runOne, { jobs });
const failures = selected.filter((_, i) => results[i] === false);

if (failures.length) {
  console.error(`\n# ${failures.length}/${selected.length} plugin test binary(ies) FAILED: ${failures.join(", ")}`);
  process.exit(1);
}
console.error(`\n# ${selected.length} plugin test binary(ies) passed (jobs=${jobs})`);
