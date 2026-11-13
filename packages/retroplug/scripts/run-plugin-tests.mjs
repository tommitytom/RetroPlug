#!/usr/bin/env node
// Plugin unit-test runner. Runs the Catch2 C++ test binaries in a bounded parallel
// pool (default half the logical threads; --jobs N / -j N / TEST_JOBS, =1 for serial):
// the per-context window-hook routing (retroplug-plugin-test), the class-id
// counter sync that keeps the DAW-hosted editor from rendering blank
// (retroplug-classid-test), the per-channel audio taps (retroplug-audio-test), the two console
// packages' own audio paths (retroplug-sameboy-test, retroplug-mesen-test), the Mega Drive package's BlastEm core,
// whose cases build the core directly and boot a minimal cart (retroplug-blastem-test),
// the native file watcher (retroplug-watcher-test), the ThorVG-backed
// Lottie rasterization behind the <Lottie> component (retroplug-lottie-test),
// the MIDI device-selection policy, the stream over a fake backend (its output-set swap,
// its multi-producer input ring and its delivery thread) (retroplug-midi-test), the audio host over a
// fake backend (its reconfigure order, the muted fallback, the watchdog, render-ahead) (retroplug-audio-host-test), the
// control-surface pack's transport, provider and facet (retroplug-launchpad-test), the
// node-graph engine's foundations
// (retroplug-graph-test), the engine on the graph: its system nodes, the
// documents TS sends it, and buildGraph's goldens planned on it
// (retroplug-engine-graph-test), and the node SDK's device build, run against
// the desktop's (retroplug-device-mode-test), and the Everdrive N8 pack's protocol over fakes
// (retroplug-n8-test) and its serial port's timeouts and exclusivity over a pseudo-terminal
// (retroplug-serial-test, Linux only: its cases are, and an empty Catch2 binary exits non-zero).
//
// A tiny runner (rather than chaining `build/bin/foo && …` in package.json) so
// the suite is cross-platform: it appends `.exe` on Windows and spawns each
// binary directly, instead of relying on a POSIX shell to resolve the paths.
//
//   node scripts/run-plugin-tests.mjs [nameFilter...]    # a binary runs if its name holds any filter

import { existsSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join, resolve } from "node:path";
import { runPool, spawnBuffered, resolveJobs, stripRunnerFlags, flush } from "./lib/testPool.mjs";

const PKG = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const REPO = resolve(PKG, "../..");
const BIN_DIR = process.env.RETROPLUG_BIN_DIR || join(REPO, "build", "bin");
const EXE = process.platform === "win32" ? ".exe" : "";

const BINARIES = [
  "retroplug-plugin-test", "retroplug-dynparams-test", "retroplug-classid-test", "retroplug-audio-test",
  "retroplug-sameboy-test", "retroplug-mesen-test", "retroplug-blastem-test", "retroplug-watcher-test", "retroplug-lottie-test",
  "retroplug-midi-test", "retroplug-audio-host-test", "retroplug-launchpad-test", "retroplug-graph-test",
  "retroplug-engine-graph-test", "retroplug-device-mode-test", "retroplug-n8-test",
  ...(process.platform === "linux" ? ["retroplug-serial-test"] : []),
];

const jobs = resolveJobs();
const filters = stripRunnerFlags();
const selected = filters.length ? BINARIES.filter((b) => filters.some((f) => b.includes(f))) : BINARIES;

if (!selected.length) {
  console.error(`no plugin test binaries match "${filters.join(" ")}"`);
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

const results = await runPool(selected, runOne, {
  jobs,
  timings: { file: join(PKG, ".test-timings/plugin.json"), key: (name) => name },
});
// `!== true`, not `=== false`: runPool leaves `undefined` for a worker that threw, and `=== false`
// counted that as a pass. These binaries emit no TAP, so the verdict stays the exit code.
const failures = selected.filter((_, i) => results[i] !== true);

if (failures.length) {
  console.error(`\n# ${failures.length}/${selected.length} plugin test binary(ies) FAILED: ${failures.join(", ")}`);
  process.exit(1);
}
console.error(`\n# ${selected.length} plugin test binary(ies) passed (jobs=${jobs})`);
