// Build the retroplug-cli test SDK: the reusable authoring layer, pre-bundled so a consumer repo (e.g.
// evermidi) can write + run CLI test sessions with only esbuild (bundle a test) and tsc (typecheck it) —
// no copy of this package's src/ tree. Mirrors tools/build-session.js, but bundles the cli/sdk.ts BARREL
// once (into a reusable module) instead of a single session, and bakes the DSP kernel into it.
//
//   node tools/build-cli-sdk.mjs [outDir]      (default outDir: build/cli-sdk)
//
// Emits, in outDir:
//   retroplug-cli.js    — the authoring layer as a self-contained ESM (zod + DSP kernel inlined). A test
//                         imports named symbols from it; esbuild re-bundles it into the test's session.js,
//                         which retroplug-cli evals. Node-free at runtime; expects only the CLI host
//                         globals (globalThis[Symbol.for("plugin")].__rpcSend, tjs.exit).
//   retroplug-cli.d.ts  — the curated public interface (cli/sdk-types.d.ts, copied verbatim).
import { buildSync } from "esbuild";
import { mkdirSync, copyFileSync } from "node:fs";
import { resolve, dirname } from "node:path";
import { fileURLToPath } from "node:url";

const HERE = dirname(fileURLToPath(import.meta.url));
const REPO = resolve(HERE, "..");
const PKG = resolve(REPO, "packages/retroplug");
const outDir = resolve(process.argv[2] ?? resolve(REPO, "build/cli-sdk"));

mkdirSync(outDir, { recursive: true });

// The DSP role kernel as a self-contained IIFE — bootSession() loadKernel()s it so audio renders. Baked
// into the SDK bundle as __DSP_KERNEL_BUNDLE__, so downstream test bundles need no define of their own.
const DSP_KERNEL_BUNDLE = buildSync({
  entryPoints: [resolve(PKG, "src/dspKernelBundle.ts")],
  bundle: true,
  format: "iife",
  platform: "neutral",
  mainFields: ["module", "main"],
  target: "es2020",
  write: false,
  define: { "process.env.NODE_ENV": '"production"' },
}).outputFiles[0].text;

// The authoring layer barrel (cli/sdk.ts) as a reusable ES module. zod and every internal src/ dep are
// inlined; the only runtime externals are the CLI host globals (resolved by the retroplug-cli binary).
const jsOut = resolve(outDir, "retroplug-cli.js");
buildSync({
  entryPoints: [resolve(PKG, "cli/sdk.ts")],
  bundle: true,
  format: "esm",
  platform: "neutral",
  mainFields: ["module", "main"],
  target: "es2020",
  outfile: jsOut,
  define: {
    "process.env.NODE_ENV": '"production"',
    __DSP_KERNEL_BUNDLE__: JSON.stringify(DSP_KERNEL_BUNDLE),
  },
});

// The public interface: the curated hand-maintained declaration, shipped verbatim next to the .js.
const dtsOut = resolve(outDir, "retroplug-cli.d.ts");
copyFileSync(resolve(PKG, "cli/sdk-types.d.ts"), dtsOut);

console.log(`wrote ${jsOut}`);
console.log(`wrote ${dtsOut}`);
