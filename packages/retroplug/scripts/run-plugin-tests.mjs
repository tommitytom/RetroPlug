#!/usr/bin/env node
// Plugin unit-test runner. Runs the Catch2 C++ test binaries in sequence:
// the per-context window-hook routing (retroplug-plugin-test), the class-id
// counter sync that keeps the DAW-hosted editor from rendering blank
// (retroplug-classid-test), the per-channel audio taps (retroplug-audio-test),
// and the native file watcher (retroplug-watcher-test).
//
// A tiny runner (rather than chaining `build/bin/foo && …` in package.json) so
// the suite is cross-platform: it appends `.exe` on Windows and spawns each
// binary directly, instead of relying on a POSIX shell to resolve the paths.
//
//   node scripts/run-plugin-tests.mjs [nameFilter]

import { existsSync } from "node:fs";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { dirname, join, resolve } from "node:path";

const PKG = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const REPO = resolve(PKG, "../..");
const BIN_DIR = process.env.RETROPLUG_BIN_DIR || join(REPO, "build", "bin");
const EXE = process.platform === "win32" ? ".exe" : "";

const BINARIES = ["retroplug-plugin-test", "retroplug-classid-test", "retroplug-audio-test", "retroplug-watcher-test"];

const filter = process.argv[2];
const selected = filter ? BINARIES.filter((b) => b.includes(filter)) : BINARIES;

if (!selected.length) {
  console.error(`no plugin test binaries match "${filter}"`);
  process.exit(1);
}

const failures = [];
for (const name of selected) {
  const bin = join(BIN_DIR, name + EXE);
  if (!existsSync(bin)) {
    console.error(
      `${name} not found: ${bin}\n` +
        `build it once:  cmake --build build --target ${name} -j`,
    );
    failures.push(name);
    continue;
  }
  console.error(`\n# ${name}`);
  const run = spawnSync(bin, [], { stdio: "inherit", cwd: REPO });
  if (run.status !== 0) failures.push(name);
}

if (failures.length) {
  console.error(`\n# ${failures.length}/${selected.length} plugin test binary(ies) FAILED: ${failures.join(", ")}`);
  process.exit(1);
}
console.error(`\n# ${selected.length} plugin test binary(ies) passed`);
