#!/usr/bin/env node
// UI test runner. For each test-ui/**/*.test.ts: bundle it with esbuild (aliasing
// "ui-harness" → the root test/harness/ui.ts, the same front door the legacy UI tests use), then run
// the bundle on the retroplug-ui-test binary — which boots the React UI on a
// headless software LVGL display (RenderCore) driven by the BackendFacade RPC (UiHarness).
// The runner installs the `retroplug` (TAP) + `retroplug-ui` (ui.*) globals and reports the exit code.
// One binary process per file, run in a bounded parallel pool (each process has its own in-process
// software LVGL display, so no display contention; default half the logical threads, --jobs/TEST_JOBS, =1 serial).
//
//   node scripts/run-ui-tests.mjs [slugFilter]

import { readdirSync, mkdirSync, existsSync, mkdtempSync, rmSync, copyFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join, resolve, relative } from "node:path";
import { tmpdir } from "node:os";
import { build } from "esbuild";
import { runPool, spawnBuffered, resolveJobs, stripJobsArgs, flush } from "./lib/testPool.mjs";

const PKG = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const REPO = resolve(PKG, "../..");
const TEST_DIR = join(PKG, "test-ui");
const OUT_DIR = join(PKG, ".ui-build");

const HOST =
  process.env.RETROPLUG_UI_TEST ||
  join(REPO, "build/bin/retroplug-ui-test" + (process.platform === "win32" ? ".exe" : ""));

if (!existsSync(HOST)) {
  console.error(
    `UI test binary not found: ${HOST}\n` +
      `build it once:  cmake --build build --target retroplug-ui-test -j$(nproc)\n` +
      `or set RETROPLUG_UI_TEST to a binary.`,
  );
  process.exit(1);
}

// The UI front door (test/expect over the harness + the `ui` facade over
// Symbol.for("retroplug-ui")). Self-contained — no legacy emu-harness graph. Aliased so a test can
// `import ... from "ui-harness"`.
const UI_HARNESS = join(PKG, "test-ui/uiHarness.ts");

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
  console.error(filter ? `no tests match "${filter}"` : "no UI tests found");
  process.exit(1);
}

async function runOne({ file, slug }) {
  const outFile = join(OUT_DIR, `${slug}.js`);
  mkdirSync(dirname(outFile), { recursive: true });
  const cfgDir = mkdtempSync(join(tmpdir(), "rp-ui-"));
  // Stage a real ROM in a WRITABLE temp dir so a file-drop test can load it by path and any sibling
  // <rom>.rplg / .sav the load writes lands here (cleaned up with cfgDir), never polluting resources/.
  const romsDir = join(cfgDir, "roms");
  mkdirSync(romsDir, { recursive: true });
  copyFileSync(join(REPO, "resources/roms/mGB.gb"), join(romsDir, "mGB.gb"));

  try {
    await build({
      entryPoints: [file],
      bundle: true,
      format: "esm",
      platform: "neutral",
      mainFields: ["module", "main"],
      target: "es2020",
      outfile: outFile,
      alias: { "ui-harness": UI_HARNESS },
      define: { "process.env.NODE_ENV": '"production"' },
    });
  } catch (e) {
    flush(`BUILD FAILED: ${slug}`, `${e?.message ?? e}`);
    rmSync(cfgDir, { recursive: true, force: true });
    return false;
  }

  const run = await spawnBuffered(HOST, ["--test", outFile], {
    cwd: PKG,
    env: { ...process.env, RETROPLUG_USER_CONFIG_DIR: cfgDir, RETROPLUG_UI_TEST_ROMS: romsDir },
  });
  flush(slug, run.output);
  rmSync(cfgDir, { recursive: true, force: true });
  return run.status === 0;
}

const results = await runPool(tests, runOne, { jobs });
const failures = tests.filter((_, i) => results[i] === false).map((t) => t.slug);

if (failures.length) {
  console.error(`\n# ${failures.length}/${tests.length} UI test file(s) FAILED: ${failures.join(", ")}`);
  process.exit(1);
}
console.error(`\n# ${tests.length} UI test file(s) passed (jobs=${jobs})`);
