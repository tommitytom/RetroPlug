#!/usr/bin/env node
// Native-backend test runner. For each test-native/**/*.test.ts: bundle it with esbuild
// (same config as run-tests.mjs), then run the bundle on the native-greenfield-host binary
// — which exposes a REAL Backend (fs/config/codec) over globalThis[Symbol.for("plugin")].
// Each file gets a fresh temp dir as RETROPLUG_USER_CONFIG_DIR (isolated real disk), also
// injected into the bundle as __CONFIG_DIR__ so tests can assert against it. Pass/fail from
// exit code; one host process per file.
//
//   node scripts/run-native-tests.mjs [slugFilter]

import { readdirSync, mkdirSync, existsSync, mkdtempSync, rmSync } from "node:fs";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { dirname, join, resolve, relative } from "node:path";
import { tmpdir } from "node:os";
import { buildSync } from "esbuild";

const PKG = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const REPO = resolve(PKG, "../..");
const TEST_DIR = join(PKG, "test-native");
const OUT_DIR = join(PKG, ".native-build");

const HOST = process.env.RETROPLUG_GREENFIELD_HOST || join(REPO, "build/bin/native-greenfield-host");

if (!existsSync(HOST)) {
  console.error(
    `native-greenfield host not found: ${HOST}\n` +
      `build it once:  cmake --build build --target native-greenfield-host -j$(nproc)\n` +
      `or set RETROPLUG_GREENFIELD_HOST to a host binary.`,
  );
  process.exit(1);
}

const filter = process.argv[2];

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

const failures = [];
for (const { file, slug } of tests) {
  const outFile = join(OUT_DIR, `${slug}.js`);
  mkdirSync(dirname(outFile), { recursive: true });
  const cfgDir = mkdtempSync(join(tmpdir(), "rp-greenfield-"));

  try {
    buildSync({
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
      },
    });
  } catch (e) {
    console.error(`# BUILD FAILED: ${slug}\n${e?.message ?? e}`);
    failures.push(slug);
    rmSync(cfgDir, { recursive: true, force: true });
    continue;
  }

  const run = spawnSync(HOST, [outFile], {
    stdio: "inherit",
    cwd: PKG,
    env: { ...process.env, RETROPLUG_USER_CONFIG_DIR: cfgDir },
  });
  if (run.status !== 0) failures.push(slug);
  rmSync(cfgDir, { recursive: true, force: true });
}

if (failures.length) {
  console.error(`\n# ${failures.length}/${tests.length} native test file(s) FAILED: ${failures.join(", ")}`);
  process.exit(1);
}
console.error(`\n# ${tests.length} native test file(s) passed`);
