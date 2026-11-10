#!/usr/bin/env node
// Type-checks every non-desktop product's generated tree (docs/modular-graph-phase-f5a-design.md sections 7.2 and 11,
// item 5): for each list the root package.json's retroplug.products declares but the desktop's, the generator writes
// the product's modules, and its own tsconfig program checks them against the shell's real sources.
//
// The tree goes under the shell's `.products/` (gitignored), NOT under build/: build.sh and build.bat configure when
// `build/CMakeCache.txt` is missing, and a typecheck before a build must not leave a directory a later build reads as
// a configured tree. A build's own product trees still land under its binary directory, where its bundle targets
// expect them.
import { readFileSync } from "node:fs";
import { createRequire } from "node:module";
import { spawnSync } from "node:child_process";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { generate } from "./lib/packages.mjs";

const HERE = dirname(fileURLToPath(import.meta.url));
const SHELL = resolve(HERE, "..");
const ROOT = resolve(SHELL, "..", "..");
const PRODUCTS = join(SHELL, ".products");
const require = createRequire(import.meta.url);
// The compiler `pnpm typecheck` runs: the workspace's `tsc` is the root's `typescript-5` (an alias of 5.7.3), not a
// package's own `typescript` dependency, so the same binary is run here rather than resolved from the shell's.
const tsc = require.resolve("typescript-5/bin/tsc");

const manifest = JSON.parse(readFileSync(join(ROOT, "package.json"), "utf8"));
const lists = Object.keys(manifest.retroplug?.products ?? {});
let failed = false;
for (const product of lists) {
  // The desktop's tree is the shell's own, which the program this script runs beside (`tsc --noEmit`) already covers.
  if (product === "desktop") continue;
  generate({ root: ROOT, product, productsDir: PRODUCTS });
  const program = join(PRODUCTS, product, "tsconfig.json");
  console.log(`typecheck: the ${product} product's tree (${program})`);
  const result = spawnSync(process.execPath, [tsc, "-p", program, "--noEmit"], { stdio: "inherit" });
  if (result.status !== 0) failed = true;
}
process.exit(failed ? 1 : 0);
