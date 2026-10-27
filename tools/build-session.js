// Bundle a CLI session (TypeScript) to a self-contained ES-module .js that the standalone
// retroplug-cli binary evals on txiki. Mirrors build-controlplane.js. (The script nodes are a bundle
// every Engine has embedded, so a session carries none of its own.)
// This is the ONLY Node step — it runs at author/build time; the resulting .js runs with no Node.
//
//   node tools/build-session.js <entry.ts> <out.js> [format]
//
// format defaults to "esm" (the CLI evals sessions as ES modules via evalModuleBuffer). Pass "iife" to emit
// a self-contained global-code script — used by the background render worker, which the native RenderHost
// loads on a BARE QuickJS runtime (no module loader) via JS_ReadObject + JS_EvalFunction, and by the
// script-node bundle (packages/native/cmake/bundles.cmake).
//
// Required as a module, it builds nothing and exports buildOptions, the options it bundles with, and
// bundleInputs, which bundles in memory and lists the modules that went in:
// packages/retroplug/test/script/bundle-no-zod.test.ts holds the script-node bundle to a list without zod.
const { buildSync } = require("esbuild");
const { resolve } = require("path");

// The session, bundled (esm for the CLI's module eval, or iife global-code for the render worker). Config-
// dir / resource paths are NOT baked in: a session resolves paths at runtime through the backend.
function buildOptions(entry, outFile, format = "esm") {
  return {
    entryPoints: [resolve(entry)],
    bundle: true,
    format,
    platform: "neutral",
    mainFields: ["module", "main"],
    target: "es2020",
    outfile: resolve(outFile),
    define: {
      "process.env.NODE_ENV": '"production"',
    },
  };
}

// `entry` bundled as buildOptions bundles it, in memory: every module that went in (esbuild's metafile inputs, paths
// relative to the working directory) and the output's size in bytes.
function bundleInputs(entry, format = "esm") {
  const result = buildSync({ ...buildOptions(entry, "bundle.js", format), write: false, metafile: true });
  return { inputs: Object.keys(result.metafile.inputs), bytes: result.outputFiles[0].contents.length };
}

module.exports = { buildOptions, bundleInputs };

if (require.main === module) {
  const entry = process.argv[2];
  const outFile = process.argv[3];
  const format = process.argv[4] || "esm";
  if (!entry || !outFile) {
    console.error("usage: node tools/build-session.js <entry.ts> <out.js> [format]");
    process.exit(2);
  }
  if (format !== "esm" && format !== "iife") {
    console.error(`build-session: format must be esm|iife (got ${format})`);
    process.exit(2);
  }
  buildSync(buildOptions(entry, outFile, format));
  console.log(`wrote ${outFile}`);
}
