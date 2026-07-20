#!/usr/bin/env node
// Test runner. For each test/**/*.test.ts: bundle it with esbuild
// (QuickJS/es2020 target, types stripped), then run the bundle on the
// standalone txiki.js runtime (`tjs run`). Aggregates TAP; exits nonzero on any
// failure. One tjs process per file = per-file isolation; files run in a bounded
// parallel pool (default half the logical threads; --jobs N / -j N / TEST_JOBS, =1 serial).
//
// Decoupled from the C++/plugin build: needs only the `tjs` binary (built once
// from the vendored txiki) + esbuild from the workspace. No retroplug-cli, no
// plugin, no emulator.
//
//   node scripts/run-tests.mjs [slugFilter]
//
// slugFilter: a path under test/ with the .test.ts suffix stripped, in slash
// ("recent/store") or dash ("recent-store") form; a directory prefix runs all
// tests under it.

import { readdirSync, mkdirSync, existsSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join, resolve, relative } from "node:path";
import { build } from "esbuild";
import { runPool, spawnBuffered, resolveJobs, stripJobsArgs, flush } from "./lib/testPool.mjs";

const PKG = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const REPO = resolve(PKG, "../..");
const TEST_DIR = join(PKG, "test");
const OUT_DIR = join(PKG, ".test-build");

const TJS =
  process.env.RETROPLUG_TJS ||
  join(
    REPO,
    "build/dpfjs/deps/lv_binding_js/deps/txiki/tjs" +
      (process.platform === "win32" ? ".exe" : ""),
  );

if (!existsSync(TJS)) {
  console.error(
    `txiki runtime not found: ${TJS}\n` +
      `build it once:  cmake --build build --target tjs-cli -j$(nproc)\n` +
      `or set RETROPLUG_TJS to a tjs binary.`,
  );
  process.exit(1);
}

const jobs = resolveJobs();
const filter = stripJobsArgs()[0];

function walk(dir, out = []) {
  if (!existsSync(dir)) return out;
  for (const ent of readdirSync(dir, { withFileTypes: true })) {
    const p = join(dir, ent.name);
    if (ent.isDirectory()) walk(p, out);
    else if (ent.name.endsWith(".test.ts")) out.push(p);
  }
  return out;
}

function matches(slug) {
  if (!filter) return true;
  const dash = slug.replace(/\//g, "-");
  return slug === filter || dash === filter || slug.startsWith(filter + "/") || dash.startsWith(filter + "-");
}

const tests = walk(TEST_DIR)
  .map((file) => ({ file, slug: relative(TEST_DIR, file).replace(/\.test\.ts$/, "").split(/[\\/]/).join("/") }))
  .filter((t) => matches(t.slug))
  .sort((a, b) => a.slug.localeCompare(b.slug));

if (!tests.length) {
  console.error(filter ? `no tests match "${filter}"` : "no tests found");
  process.exit(1);
}

async function runOne({ file, slug }) {
  const outFile = join(OUT_DIR, `${slug}.js`);
  mkdirSync(dirname(outFile), { recursive: true });

  try {
    await build({
      entryPoints: [file],
      bundle: true,
      format: "esm",
      platform: "neutral",
      mainFields: ["module", "main"],
      target: "es2020",
      outfile: outFile,
      define: { "process.env.NODE_ENV": '"production"' },
    });
  } catch (e) {
    flush(`BUILD FAILED: ${slug}`, `${e?.message ?? e}`);
    return false;
  }

  const run = await spawnBuffered(TJS, ["run", outFile], { cwd: PKG });
  flush(slug, run.output);
  return run.status === 0;
}

const results = await runPool(tests, runOne, { jobs });
const failures = tests.filter((_, i) => results[i] === false).map((t) => t.slug);

if (failures.length) {
  console.error(`\n# ${failures.length}/${tests.length} test file(s) FAILED: ${failures.join(", ")}`);
  process.exit(1);
}
console.error(`\n# ${tests.length} test file(s) passed (jobs=${jobs})`);
