// Bundle a CLI session (TypeScript) to a self-contained ES-module .js that the standalone
// retroplug-cli binary evals on txiki. Mirrors build-controlplane.js. (The script nodes are a bundle
// every Engine has embedded, so a session carries none of its own.)
// This is the ONLY Node step — it runs at author/build time; the resulting .js runs with no Node.
//
//   node tools/build-session.js <entry.ts> <out.js> [format] [--product-dir <dir>]
//
// --product-dir names a product's generated tree (build/products/<name>/packages/retroplug), whose generated modules
// the shell's sources read instead of the desktop's (tools/product-generated.js). No flag is the desktop.
//
// format defaults to "esm" (the CLI evals sessions as ES modules via evalModuleBuffer). Pass "iife" to emit
// a self-contained global-code script — used by the background render worker, which the native RenderHost
// loads on a BARE QuickJS runtime (no module loader) via JS_ReadObject + JS_EvalFunction, and by the
// script-node bundle (packages/native/cmake/bundles.cmake).
//
// Required as a module, it builds nothing and exports buildOptions, the options it bundles with, and
// bundleInputs, which bundles in memory and lists the modules that went in:
// packages/retroplug/test/script/bundle-no-zod.test.ts holds the script-node bundle to a list without zod.
const { build, buildSync } = require("esbuild");
const { resolve } = require("path");
// A product's tree is not under the shell, so its bare @retroplug/* specifiers resolve from the shell's node_modules.
const SHELL_NODE_MODULES = resolve(__dirname, "../packages/retroplug/node_modules");
const { tsOptions } = require("./ts-options");
const { productGenerated } = require("./product-generated");

// The session, bundled (esm for the CLI's module eval, or iife global-code for the render worker). Config-
// dir / resource paths are NOT baked in: a session resolves paths at runtime through the backend.
function buildOptions(entry, outFile, format = "esm", product = undefined) {
  return {
    entryPoints: [resolve(entry)],
    bundle: true,
    format,
    platform: "neutral",
    mainFields: ["module", "main"],
    target: "es2020",
    // The entries are the shell's (cli/sessions, test/script) and the iife bundles built from them carry a
    // "use strict" directive today, so it is pinned on: tools/ts-options.js says why a bundle names its own options.
    tsconfigRaw: tsOptions({ alwaysStrict: true }),
    outfile: resolve(outFile),
    // A package's example session imports the SDK by the bare `retroplug-cli` specifier, which the binary serves from
    // its loaded-module table at run time (as it does for a test file), so it must not be bundled in: the example is a
    // few lines against the binary's own SDK. The shell's own sessions import their modules relatively and still bundle.
    external: ["retroplug-cli"],
    define: {
      "process.env.NODE_ENV": '"production"',
    },
    nodePaths: product ? [SHELL_NODE_MODULES] : undefined,
    plugins: [productGenerated({ dir: product })].filter(Boolean),
  };
}

// `entry` bundled as buildOptions bundles it, in memory: every module that went in (esbuild's metafile inputs, paths
// relative to the working directory) and the output's size in bytes.
// Asynchronous because a product's redirect is a plugin, and esbuild refuses plugins in buildSync.
async function bundleInputs(entry, format = "esm", product = undefined) {
  const result = await build({ ...buildOptions(entry, "bundle.js", format, product), write: false, metafile: true });
  return { inputs: Object.keys(result.metafile.inputs), bytes: result.outputFiles[0].contents.length };
}

module.exports = { buildOptions, bundleInputs };

if (require.main === module) {
  const rest = process.argv.slice(2);
  const productAt = rest.indexOf("--product-dir");
  const productDir = productAt === -1 ? undefined : rest.splice(productAt, 2)[1];
  const [entry, outFile, format = "esm"] = rest;
  if (!entry || !outFile) {
    console.error("usage: node tools/build-session.js <entry.ts> <out.js> [format] [--product-dir <dir>]");
    process.exit(2);
  }
  if (format !== "esm" && format !== "iife") {
    console.error(`build-session: format must be esm|iife (got ${format})`);
    process.exit(2);
  }
  const options = { ...buildOptions(entry, outFile, format, productDir) };
  (productDir ? build(options) : Promise.resolve(buildSync(options)))
    .then(() => console.log(`wrote ${outFile}`))
    .catch((e) => {
      console.error(`build-session: ${e.message}`);
      process.exit(1);
    });
}
