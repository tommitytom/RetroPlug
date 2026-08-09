#!/usr/bin/env node
// Bundle a test-native/*.test.ts into a SELF-CONTAINED .js that retroplug-host can eval directly on ANY
// machine (e.g. the arm64 handheld) with no Node at runtime. Mirrors run-native-tests.mjs's esbuild
// config, but bakes in a caller-chosen __CONFIG_DIR__ (a writable path ON THE TARGET) instead of a host
// mkdtemp — so the same bundle runs off-box. Used for on-device profiling (dsp-bench).
//
//   node tools/bundle-native-test.mjs <slug> <out.js> [configDir=/tmp/rp-cfg]
import { build, buildSync } from "esbuild";
import { resolve, join, dirname } from "node:path";
import { fileURLToPath } from "node:url";

const REPO = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const PKG = join(REPO, "packages/retroplug");
const [slug, out, cfgDir = "/tmp/rp-cfg"] = process.argv.slice(2);
if (!slug || !out) {
  console.error("usage: node tools/bundle-native-test.mjs <slug> <out.js> [configDir]");
  process.exit(1);
}
const RES = join(REPO, "resources"); // dsp-bench uses the EMBEDDED mGB, so this is only a placeholder

const kernel = buildSync({
  entryPoints: [join(PKG, "src/dspKernelBundle.ts")],
  bundle: true, format: "iife", platform: "neutral", mainFields: ["module", "main"],
  target: "es2020", write: false, define: { "process.env.NODE_ENV": '"production"' },
}).outputFiles[0].text;

await build({
  entryPoints: [join(PKG, "test-native", slug + ".test.ts")],
  bundle: true, format: "esm", platform: "neutral", mainFields: ["module", "main"],
  target: "es2020", outfile: out,
  define: {
    "process.env.NODE_ENV": '"production"',
    __CONFIG_DIR__: JSON.stringify(cfgDir),
    __RESOURCES_DIR__: JSON.stringify(RES),
    __REPO_RESOURCES_DIR__: JSON.stringify(RES),
    __DSP_KERNEL_BUNDLE__: JSON.stringify(kernel),
  },
});
console.log(`bundled ${slug} -> ${out} (configDir=${cfgDir})`);
