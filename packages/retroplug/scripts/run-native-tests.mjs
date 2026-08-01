#!/usr/bin/env node
// Native-backend test runner. For each test-native/**/*.test.ts: bundle it with esbuild
// (same config as run-tests.mjs), then run the bundle on the retroplug-host binary
// — which exposes a REAL Backend (fs/config/codec) over globalThis[Symbol.for("plugin")].
// Each file gets a fresh temp dir as RETROPLUG_USER_CONFIG_DIR (isolated real disk), also
// injected into the bundle as __CONFIG_DIR__ so tests can assert against it. Pass/fail from
// exit code; one host process per file, run in a bounded parallel pool (default half the
// logical threads; override with --jobs N / -j N / TEST_JOBS, =1 for serial).
//
//   node scripts/run-native-tests.mjs [slugFilter] [--jobs N]

import { readdirSync, mkdirSync, existsSync, mkdtempSync, rmSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join, resolve, relative } from "node:path";
import { tmpdir } from "node:os";
import { build, buildSync } from "esbuild";
import { runPool, spawnBuffered, resolveJobs, stripJobsArgs, flush } from "./lib/testPool.mjs";

const PKG = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const REPO = resolve(PKG, "../..");
const TEST_DIR = join(PKG, "test-native");
const OUT_DIR = join(PKG, ".native-build");

// The sibling resources dir (ROMs/manuals/…), overridable — mirrors the repo-wide convention.
// Injected into the bundle so a test can locate real ROMs by absolute path (native slurps them).
const RESOURCES_DIR = process.env.RETROPLUG_RESOURCES_DIR || resolve(REPO, "../resources");
// The in-repo resources dir — a few small ROMs are committed here (e.g. resources/roms/n8-midi.nes)
// rather than in the sibling tree, so a test can reach them without the sibling being populated.
const REPO_RESOURCES_DIR = join(REPO, "resources");

const HOST =
  process.env.RETROPLUG_HOST ||
  join(REPO, "build/bin/retroplug-host" + (process.platform === "win32" ? ".exe" : ""));

if (!existsSync(HOST)) {
  console.error(
    `retroplug-host not found: ${HOST}\n` +
      `build it once:  cmake --build build --target retroplug-host -j$(nproc)\n` +
      `or set RETROPLUG_HOST to a host binary.`,
  );
  process.exit(1);
}

const jobs = resolveJobs();
const filter = stripJobsArgs()[0];

// Build the DSP role kernel once as a self-contained IIFE and inject its SOURCE into every test
// (like __RESOURCES_DIR__ below). A test compiles+loads it into the native DSP runtime — the real
// per-block program — instead of authoring an ad-hoc translator string.
const DSP_KERNEL_BUNDLE = buildSync({
  entryPoints: [join(PKG, "src/dspKernelBundle.ts")],
  bundle: true,
  format: "iife",
  platform: "neutral",
  mainFields: ["module", "main"],
  target: "es2020",
  write: false,
  define: { "process.env.NODE_ENV": '"production"' },
}).outputFiles[0].text;

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
  console.error(filter ? `no tests match "${filter}"` : "no native tests found");
  process.exit(1);
}

async function runOne({ file, slug }) {
  const outFile = join(OUT_DIR, `${slug}.js`);
  mkdirSync(dirname(outFile), { recursive: true });
  // Forward slashes so the injected __CONFIG_DIR__ matches the native backend's
  // path convention (it canonicalizes stored paths to '/'). On Windows mkdtempSync
  // yields backslashes; both node fs and the native host accept '/' there. No-op
  // elsewhere. Without this, path round-trip assertions mismatch on Windows.
  const cfgDir = mkdtempSync(join(tmpdir(), "rp-")).replaceAll("\\", "/");

  try {
    await build({
      entryPoints: [file],
      bundle: true,
      format: "esm",
      platform: "neutral",
      mainFields: ["module", "main"],
      target: "es2020",
      outfile: outFile,
      define: {
        "process.env.NODE_ENV": '"production"',
        __CONFIG_DIR__: JSON.stringify(cfgDir),
        __RESOURCES_DIR__: JSON.stringify(RESOURCES_DIR),
        __REPO_RESOURCES_DIR__: JSON.stringify(REPO_RESOURCES_DIR),
        __DSP_KERNEL_BUNDLE__: JSON.stringify(DSP_KERNEL_BUNDLE),
      },
    });
  } catch (e) {
    flush(`BUILD FAILED: ${slug}`, `${e?.message ?? e}`);
    rmSync(cfgDir, { recursive: true, force: true });
    return false;
  }

  const run = await spawnBuffered(HOST, [outFile], {
    cwd: PKG,
    env: { ...process.env, RETROPLUG_USER_CONFIG_DIR: cfgDir },
  });
  flush(slug, run.output);
  rmSync(cfgDir, { recursive: true, force: true });
  return run.status === 0;
}

const results = await runPool(tests, runOne, { jobs });
const failures = tests.filter((_, i) => results[i] === false).map((t) => t.slug);

if (failures.length) {
  console.error(`\n# ${failures.length}/${tests.length} native test file(s) FAILED: ${failures.join(", ")}`);
  process.exit(1);
}
console.error(`\n# ${tests.length} native test file(s) passed (jobs=${jobs})`);
