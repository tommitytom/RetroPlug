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
import { runPool, spawnBuffered, resolveJobs, stripRunnerFlags, flush, parseTap } from "./lib/testPool.mjs";
import { checkSkipBaseline } from "./lib/skipBaseline.mjs";

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
const filter = stripRunnerFlags()[0];

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
  const tap = parseTap(run.output);
  if (run.status === 0 && !tap.ok) console.error(`# ${slug}: ${tap.problem}`);
  return { ok: run.status === 0 && tap.ok, tap };
}

const results = await runPool(tests, runOne, {
  jobs,
  timings: { file: join(PKG, ".test-timings/ts.json"), key: (t) => t.slug },
});
const failures = tests.filter((_, i) => results[i]?.ok !== true).map((t) => t.slug);

if (failures.length) {
  console.error(`\n# ${failures.length}/${tests.length} test file(s) FAILED: ${failures.join(", ")}`);
  process.exit(1);
}
const skipped = tests
  .map((t, i) => ({ slug: t.slug, n: results[i]?.tap.skip ?? 0 }))
  .filter((x) => x.n > 0);
const skipTotal = skipped.reduce((a, x) => a + x.n, 0);
console.error(`\n# ${tests.length} test file(s) passed (jobs=${jobs})`);
if (skipped.length)
  console.error(
    `#   ${skipTotal} case(s) SKIPPED in ${skipped.length} file(s): ` +
      skipped.map((x) => `${x.slug}(${x.n})`).join(", "),
  );

// The ratchet. A filtered run has no reading for the files it did not execute, so it cannot judge the
// baseline and does not try.
const baseline = checkSkipBaseline(
  "ts",
  tests.map((t, i) => ({ slug: t.slug, skip: results[i]?.tap.skip ?? 0, total: results[i]?.tap.plan ?? 0 })),
  {
    filtered: filter !== undefined,
    update: process.argv.slice(2).includes("--update-skip-baseline"),
    strict: process.env.RP_FAIL_ON_SKIP === "1",
  },
);
for (const line of baseline.lines) console.error(line);
if (!baseline.ok) process.exit(1);
