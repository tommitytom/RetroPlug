#!/usr/bin/env node
// Drives pico/n8-host/exporter-stale.sh over a scratch tree, for test/tools/exporter-stale.test.ts. The script's whole
// input is a repository layout and a stamp file's mtime, so the test builds a small one of each rather than reading the
// checkout's, and asks the three questions the script answers: current (exit 0), stale (exit 1 and the path), and a
// usage error (exit 2).
//
//   node test/tools/exporter-stale-check.mjs
//
// Prints one JSON line: { current: { status, out }, newer: { status, out }, usage: { status, out }, missing: {
// status, out } }, each the script's exit status and its output, so the test holds what a person running it sees.
import { spawnSync } from "node:child_process";
import { existsSync, mkdirSync, mkdtempSync, rmSync, utimesSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const HERE = dirname(fileURLToPath(import.meta.url));
const REPO = resolve(HERE, "../../../..");
const SCRIPT = join(REPO, "pico/n8-host/exporter-stale.sh");

// Seconds since the epoch, which utimesSync takes. Far enough apart that a file the test makes newer is unambiguous.
const T0 = 1_000_000_000;
const T1 = 2_000_000_000;
const T2 = 3_000_000_000;

/** A path as a POSIX shell reads it: a Windows `C:\dir\file` becomes `C:/dir/file`, which Git for Windows accepts,
 *  and a path it would take for an escape sequence is not what reaches the script. */
const shellPath = (path) => path.split("\\").join("/");

/** A file of the scratch tree, made with a fixed mtime so the comparison is the test's and not the clock's. */
function put(root, path, text, at = T0) {
  const file = join(root, path);
  mkdirSync(dirname(file), { recursive: true });
  writeFileSync(file, text);
  utimesSync(file, at, at);
}

/** The bash to run the script in. On Windows a bare `bash` can be the WSL launcher (WindowsApps\bash.exe), which reads
 *  `C:/...` as a Linux path, so Git for Windows' own bash is found from git's exec path (<git>/mingw64/libexec/git-core). */
function bash() {
  if (process.env.RP_BASH) return process.env.RP_BASH;
  if (process.platform !== "win32") return "bash";
  const exec = spawnSync("git", ["--exec-path"], { encoding: "utf8" });
  const gitBash = exec.status === 0 ? resolve(exec.stdout.trim(), "../../../bin/bash.exe") : "";
  return gitBash && existsSync(gitBash) ? gitBash : "bash";
}

/** The script's answer to one invocation, as its exit status and its output. */
function stale(stamp, root) {
  const args = stamp === undefined ? [shellPath(SCRIPT)] : [shellPath(SCRIPT), shellPath(stamp), shellPath(root)];
  const child = spawnSync(bash(), args, { encoding: "utf8" });
  if (child.error) throw new Error(`the script could not be run: ${child.error.message}`);
  return { status: child.status, out: `${child.stdout ?? ""}${child.stderr ?? ""}`.trim() };
}

const root = mkdtempSync(join(tmpdir(), "retroplug-exporter-stale-"));
try {
  // The pieces the script's list names that this scratch repository has to hold: its packages, the exporter's
  // toolchain with an SDK list under packages/core/ts, and the CMake the exporter is built by.
  put(root, "packages/core/ts/clock.ts", "export const clock = 1;\n");
  put(root, "packages/retroplug/scripts/build-device-pack.mjs", 'const SDK = [\n  "packages/core/ts/defineNode.ts",\n];\n');
  put(root, "tools/ts-options.js", "export const options = {};\n");
  put(root, "cmake/packages.cmake", "# a package's CMake, in the tree the script watches as a whole\n");
  put(root, "CMakeLists.txt", "project(scratch NONE)\n");
  put(root, "deps/mquickjs/mquickjs.c", "/* the engine the image takes from the same build */\n");
  // A script pack: its manifest names one, so the script watches its own ts/ tree and the manifest beside it.
  put(
    root,
    "packages/alpha/package.json",
    `${JSON.stringify({ name: "@retroplug/alpha", retroplug: { script: { entry: "ts/pack.ts", device: true, heapBytes: 4096 } } }, null, 2)}\n`,
  );
  put(root, "packages/alpha/ts/pack.ts", "export const pack = 1;\n");

  const stamp = join(root, "exporter.current");
  put(root, "exporter.current", "", T1);
  const current = stale(stamp, root);

  // A core TS file newer than the exporter: the script names it and refuses.
  utimesSync(join(root, "packages/core/ts/clock.ts"), T2, T2);
  const newer = stale(stamp, root);
  utimesSync(join(root, "packages/core/ts/clock.ts"), T0, T0);

  const usage = stale(undefined, root);
  const missing = stale(join(root, "no-such-stamp"), root);

  console.log(JSON.stringify({ current, newer, usage, missing }));
} finally {
  rmSync(root, { recursive: true, force: true });
}
