#!/usr/bin/env node
// Build CMake target(s) in the configured `build/` dir, always in parallel.
//
//   node scripts/cmake-build.js [target...]
//
// No targets => a full build (bare `cmake --build`, what AGENTS.md recommends
// for UI changes). Assumes `build/` is already configured (run `pnpm configure`
// once). The Catch2 test binaries are EXCLUDE_FROM_ALL and built by NAME - `pnpm
// test:plugin` names all eight - so they need no BUILD_TESTING configure.

const os = require("os");
const path = require("path");
const { spawnSync } = require("child_process");

const REPO_ROOT = path.resolve(__dirname, "..");
const BUILD_DIR = path.join(REPO_ROOT, "build");

const targets = process.argv.slice(2);
const args = ["--build", BUILD_DIR, "-j", String(os.cpus().length)];
if (targets.length) args.push("--target", ...targets);

const r = spawnSync("cmake", args, { stdio: "inherit", cwd: REPO_ROOT });
if (r.error) {
    console.error(r.error.message);
    process.exit(1);
}
process.exit(r.status === null ? 1 : r.status);
