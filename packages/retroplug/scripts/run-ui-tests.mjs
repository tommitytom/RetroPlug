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

import { readdirSync, mkdirSync, existsSync, mkdtempSync, rmSync, copyFileSync, readFileSync } from "node:fs";
import { createRequire } from "node:module";
import { fileURLToPath } from "node:url";
import { dirname, join, resolve, relative, sep } from "node:path";
import { tmpdir } from "node:os";
import { build } from "esbuild";
import { runPool, spawnBuffered, resolveJobs, stripRunnerFlags, flush, parseTap } from "./lib/testPool.mjs";
import { checkSkipBaseline } from "./lib/skipBaseline.mjs";
import { generate } from "./lib/packages.mjs";
import { setContracts } from "../../../tools/build-shared-modules.mjs";

const { productGenerated } = createRequire(import.meta.url)("../../../tools/product-generated.js");

const PKG = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const REPO = resolve(PKG, "../..");
const TEST_DIR = join(PKG, "test-ui");

// The pack mode (docs/downloadable-packs-m5b-plan.md): every file again, on retroplug-ui-test-packs, the UI harness on
// packs-desktop (the desktop's list without mesen), which loads Mesen as a pack. A bundle reads that product's
// generated registries, leaves the harness's shared contract modules (the app set's names, zod among them) external,
// and starts with a prelude that loads the packs' halves.
const PACKS = process.argv.slice(2).includes("--packs");
const PACK_PRODUCT = "packs-desktop";
const MESEN_VERSION = JSON.parse(readFileSync(join(REPO, "packages/mesen/pack.json"), "utf8")).version;
const MESEN_PACK = join(REPO, "build/packs/mesen", MESEN_VERSION);
const PRODUCT_TREE = join(REPO, "build/products", PACK_PRODUCT, "packages/retroplug");
const PRELUDE = join(TEST_DIR, "_packsPrelude.ts");
const OUT_DIR = join(PKG, PACKS ? ".ui-build-packs" : ".ui-build");

// The generated package registries first (src/generated, docs/modular-graph-phase-f1-design.md section 3): a test
// bundles src, which imports them, so they must exist before the first esbuild run. A fresh checkout has none.
generate(PACKS ? { product: PACK_PRODUCT } : {});

const HOST_TARGET = PACKS ? "retroplug-ui-test-packs" : "retroplug-ui-test";
const HOST =
  (PACKS ? process.env.RETROPLUG_UI_TEST_PACKS : process.env.RETROPLUG_UI_TEST) ||
  join(REPO, "build/bin", HOST_TARGET + (process.platform === "win32" ? ".exe" : ""));

if (!existsSync(HOST)) {
  console.error(
    `UI test binary not found: ${HOST}\n` +
      `build it once:  cmake --build build --target ${HOST_TARGET} -j$(nproc)\n` +
      `or set ${PACKS ? "RETROPLUG_UI_TEST_PACKS" : "RETROPLUG_UI_TEST"} to a binary.`,
  );
  process.exit(1);
}
if (PACKS && !existsSync(join(MESEN_PACK, "pack.json"))) {
  console.error(
    `the Mesen pack is not built: ${MESEN_PACK}\n` +
      `build it:  cmake --build build --target retroplug-mesen-pack -j$(nproc)`,
  );
  process.exit(1);
}

// The UI front door (test/expect over the harness + the `ui` facade over
// Symbol.for("retroplug-ui")). Self-contained — no legacy emu-harness graph. Aliased so a test can
// `import ... from "ui-harness"`.
const UI_HARNESS = join(PKG, "test-ui/uiHarness.ts");

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
  console.error(filter ? `no tests match "${filter}"` : "no UI tests found");
  process.exit(1);
}

// A test of pack loading names the development packs it loads (docs/downloadable-packs-m2c-plan.md); every other test
// loads none. The fixture packs are staged under build/packs by retroplug-ts-fixture-packs.
const PACK_TESTS = {
  "packs-ts-fixture": ["tsfix", "tsfix-throws", "tsfix-guard", "tsfix-kind"],
};
// A fixture's staged folder is build/packs/<id>/<version>, the one version folder there.
const fixtureFolder = (id) => {
  const dir = join(REPO, "build/packs", id);
  const versions = existsSync(dir) ? readdirSync(dir, { withFileTypes: true }).filter((e) => e.isDirectory()) : [];
  return join(dir, versions.length === 1 ? versions[0].name : "1.0.0");
};
const SEP = process.platform === "win32" ? ";" : ":";
const packPath = (slug) =>
  [...(PACKS ? [MESEN_PACK] : []), ...(PACK_TESTS[slug] ?? []).map(fixtureFolder)].join(SEP);

const SHELL_SRC = join(PKG, "src");

/** Whether `file`'s pack-mode bundle carries any module of the shell's own src/: a metafile-only build (nothing
 *  written) with the pack mode's externals and product tree, and no prelude. */
async function reachesShellSrc(file) {
  const result = await build({
    entryPoints: [file],
    bundle: true,
    write: false,
    metafile: true,
    format: "esm",
    platform: "neutral",
    mainFields: ["module", "main"],
    target: "es2022",
    outfile: join(OUT_DIR, "metafile-probe.js"),
    alias: { "ui-harness": UI_HARNESS },
    define: { "process.env.NODE_ENV": '"production"' },
    external: setContracts("app"),
    plugins: [productGenerated({ dir: PRODUCT_TREE })],
    logLevel: "silent",
  });
  return Object.keys(result.metafile.inputs).some((input) => {
    const abs = resolve(process.cwd(), input);  // a metafile key is relative to the working dir (esbuild's default)
    return abs === SHELL_SRC || abs.startsWith(SHELL_SRC + sep);
  });
}

async function runOne({ file, slug }) {
  const outFile = join(OUT_DIR, `${slug}.js`);
  mkdirSync(dirname(outFile), { recursive: true });
  const cfgDir = mkdtempSync(join(tmpdir(), "rp-ui-"));
  // Stage a real ROM in a WRITABLE temp dir so a file-drop test can load it by path and any sibling
  // <rom>.rplg / .sav the load writes lands here (cleaned up with cfgDir), never polluting resources/.
  const romsDir = join(cfgDir, "roms");
  mkdirSync(romsDir, { recursive: true });
  copyFileSync(join(REPO, "packages/mgb/rom/mGB.gb"), join(romsDir, "mGB.gb"));
  // A NES cart too, for the tests that need a non-GB console (console-dependent menu rows). Committed
  // like mGB, so unconditional — unlike the two best-effort stages below.
  copyFileSync(join(REPO, "resources/roms/bliptoaster.nes"), join(romsDir, "bliptoaster.nes"));
  // The smsggdj tracker (committed), for the Recent-list test that needs a cart whose working song lives
  // in work RAM and takes seconds to boot. The test writes the `.sav` beside it itself (the SMDJ4 codec is
  // TS, and the test runs in the app's own context), so only the ROM is staged.
  copyFileSync(join(REPO, "resources/roms/smsggdj_v0_45.sms"), join(romsDir, "smsggdj_v0_45.sms"));
  // Stage an LSDj ROM too when one is present (local or the sibling resources tree) — the LSDj-overlay
  // test drops it; absent, that test SKIPs. It's a large external asset, so this is best-effort.
  // The sibling resources tree, via the same override the native runner uses — so a fixture-less run
  // (which is how the skip baseline is generated) can actually be simulated here too.
  const resourcesDir = process.env.RETROPLUG_RESOURCES_DIR || join(REPO, "../resources");
  for (const src of [join(REPO, "resources/roms/lsdj/lsdj9_4_2.gb"), join(resourcesDir, "roms/lsdj/lsdj9_4_2.gb")]) {
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
    // Only a file whose bundle reaches the shell's own src/ (its stores, and so a copy of the registries of its own)
    // needs the prelude: the UI bundle loads the packs into the UI's registries itself (ui/main.tsx's loadUiPacks). For
    // any other file the prelude would bring a second copy of the shell and its packages' modules into the UI's context,
    // whose global hooks then stand in for the fakes a device test installs (audio-driver, midi-settings and game-input
    // failed so). A metafile pass without it says which.
    const needsPrelude = PACKS && (await reachesShellSrc(file));
    // esbuild refuses entryPoints beside stdin, so the pack mode's options are built apart from the desktop's.
    const entry = PACKS
      ? {
          stdin: {
            contents: `${needsPrelude ? `import ${JSON.stringify(PRELUDE.replaceAll("\\", "/"))};\n` : ""}import ${JSON.stringify(file.replaceAll("\\", "/"))};\n`,
            resolveDir: dirname(file),
            sourcefile: `${slug}.packs-entry.js`,
            loader: "js",
          },
          external: setContracts("app"),
          plugins: [productGenerated({ dir: PRODUCT_TREE })],
        }
      : { entryPoints: [file] };
    await build({
      ...entry,
      bundle: true,
      format: "esm",
      platform: "neutral",
      mainFields: ["module", "main"],
      // The pack mode's prelude awaits at top level, which es2020 cannot express.
      target: PACKS ? "es2022" : "es2020",
      outfile: outFile,
      alias: { "ui-harness": UI_HARNESS },
      define: { "process.env.NODE_ENV": '"production"' },
    });
  } catch (e) {
    flush(`BUILD FAILED: ${slug}`, `${e?.message ?? e}`);
    rmSync(cfgDir, { recursive: true, force: true });
    return false;
  }

  // The host reads the launch packs: an empty folder it owns, and no development path, so no test meets the user's.
  const { RETROPLUG_PACK_PATH: _ignored, ...inherited } = process.env;
  const run = await spawnBuffered(HOST, ["--test", outFile], {
    cwd: PKG,
    env: {
      ...inherited,
      RETROPLUG_USER_CONFIG_DIR: cfgDir,
      RETROPLUG_UI_TEST_ROMS: romsDir,
      RETROPLUG_PACKS_DIR: join(cfgDir, "packs"),
      RETROPLUG_PACKS_CACHE_DIR: join(cfgDir, "packs-cache"),
      ...(PACKS || PACK_TESTS[slug] ? { RETROPLUG_PACK_PATH: packPath(slug) } : {}),
    },
  });
  flush(slug, run.output);
  rmSync(cfgDir, { recursive: true, force: true });
  const tap = parseTap(run.output);
  if (run.status === 0 && !tap.ok) console.error(`# ${slug}: ${tap.problem}`);
  return { ok: run.status === 0 && tap.ok, tap };
}

const results = await runPool(tests, runOne, {
  jobs,
  timings: { file: join(PKG, `.test-timings/${PACKS ? "ui-packs" : "ui"}.json`), key: (t) => t.slug },
});
const failures = tests.filter((_, i) => results[i]?.ok !== true).map((t) => t.slug);

if (failures.length) {
  console.error(`\n# ${failures.length}/${tests.length} UI${PACKS ? " (packs)" : ""} test file(s) FAILED: ${failures.join(", ")}`);
  process.exit(1);
}
const skipped = tests
  .map((t, i) => ({ slug: t.slug, n: results[i]?.tap.skip ?? 0 }))
  .filter((x) => x.n > 0);
const skipTotal = skipped.reduce((a, x) => a + x.n, 0);
console.error(`\n# ${tests.length} UI${PACKS ? " (packs)" : ""} test file(s) passed (jobs=${jobs})`);
if (skipped.length)
  console.error(
    `#   ${skipTotal} case(s) SKIPPED in ${skipped.length} file(s): ` +
      skipped.map((x) => `${x.slug}(${x.n})`).join(", "),
  );

// The ratchet. A filtered run has no reading for the files it did not execute, so it cannot judge the
// baseline and does not try.
const baseline = checkSkipBaseline(
  PACKS ? "ui-packs" : "ui",
  tests.map((t, i) => ({ slug: t.slug, skip: results[i]?.tap.skip ?? 0, total: results[i]?.tap.plan ?? 0 })),
  {
    filtered: filter !== undefined,
    update: process.argv.slice(2).includes("--update-skip-baseline"),
    strict: process.env.RP_FAIL_ON_SKIP === "1",
  },
);
for (const line of baseline.lines) console.error(line);
if (!baseline.ok) process.exit(1);
