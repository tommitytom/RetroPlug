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
import { runPool, spawnBuffered, resolveJobs, stripJobsArgs, flush, parseTap } from "./lib/testPool.mjs";

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
  // A NES cart too, for the tests that need a non-GB console (console-dependent menu rows). Committed
  // like mGB, so unconditional — unlike the two best-effort stages below.
  copyFileSync(join(REPO, "resources/roms/bliptoaster.nes"), join(romsDir, "bliptoaster.nes"));
  // The smsggdj tracker (committed), for the Recent-list test that needs a cart whose working song lives
  // in work RAM and takes seconds to boot. The test writes the `.sav` beside it itself (the SMDJ4 codec is
  // TS, and the test runs in the app's own context), so only the ROM is staged.
  copyFileSync(join(REPO, "resources/roms/smsggdj_v0_45.sms"), join(romsDir, "smsggdj_v0_45.sms"));
  // Stage an LSDj ROM too when one is present (local or the sibling resources tree) — the LSDj-overlay
  // test drops it; absent, that test SKIPs. It's a large external asset, so this is best-effort.
  for (const src of [join(REPO, "resources/roms/lsdj/lsdj9_4_2.gb"), join(REPO, "../resources/roms/lsdj/lsdj9_4_2.gb")]) {
    if (existsSync(src)) { copyFileSync(src, join(romsDir, "lsdj9_4_2.gb")); break; }
  }
  // Stage a built risa ROM too when present (RISA_ROM env, resources, or the sibling risa source tree) —
  // the risa-overlay test drops it; absent, that test SKIPs.
  // RISA_SRC matches the native runner's override and scripts/gen-risa-symbols.mjs, so the sibling
  // checkout is named in one place per runner rather than baked into a path literal here.
  const risaSrc = process.env.RISA_SRC || "/workspaces/risa-v2.2.1-source";
  for (const src of [process.env.RISA_ROM, join(REPO, "resources/roms/risa/risa.nes"), join(risaSrc, "build/risa-pal.nes")]) {
    if (src && existsSync(src)) { copyFileSync(src, join(romsDir, "risa.nes")); break; }
  }

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
  const tap = parseTap(run.output);
  if (run.status === 0 && !tap.ok) console.error(`# ${slug}: ${tap.problem}`);
  return { ok: run.status === 0 && tap.ok, tap };
}

const results = await runPool(tests, runOne, {
  jobs,
  timings: { file: join(PKG, ".test-timings/ui.json"), key: (t) => t.slug },
});
const failures = tests.filter((_, i) => results[i]?.ok !== true).map((t) => t.slug);

if (failures.length) {
  console.error(`\n# ${failures.length}/${tests.length} UI test file(s) FAILED: ${failures.join(", ")}`);
  process.exit(1);
}
const skipped = tests
  .map((t, i) => ({ slug: t.slug, n: results[i]?.tap.skip ?? 0 }))
  .filter((x) => x.n > 0);
const skipTotal = skipped.reduce((a, x) => a + x.n, 0);
console.error(`\n# ${tests.length} UI test file(s) passed (jobs=${jobs})`);
if (skipped.length)
  console.error(
    `#   ${skipTotal} case(s) SKIPPED in ${skipped.length} file(s): ` +
      skipped.map((x) => `${x.slug}(${x.n})`).join(", "),
  );
