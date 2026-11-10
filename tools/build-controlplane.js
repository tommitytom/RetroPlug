// Build the plugin's control-plane bundle (pluginControlPlane.ts) for embedding as
// bytecode: esbuild the control plane to an ES module CMake compiles with tjsc. (The protocol nodes are
// script nodes in a bundle every Engine has embedded, so the control plane carries none of them.)
//
//   node tools/build-controlplane.js OUT.js [--product-dir <dir>]
//
// --product-dir names a product's generated tree laid out as the shell's is (build/products/<name>/packages/retroplug),
// whose generated modules the shell's sources read instead of the desktop's (tools/product-generated.js). No flag is
// the desktop: the shell's own tree. The three build scripts take the flag in this one form (build-ui.js and
// build-session.js too), and CMake passes it only for a product that has a tree.
const { build, buildSync } = require("esbuild");
const { resolve } = require("path");
const { tsOptions } = require("./ts-options");
const { productGenerated } = require("./product-generated");

const PKG = resolve(__dirname, "../packages/retroplug");
// A product's tree is not under the shell, so its bare @retroplug/* specifiers resolve from the shell's own
// node_modules (pnpm's links to the workspace packages), which esbuild does not walk to on its own.
const SHELL_NODE_MODULES = resolve(PKG, "node_modules");
const argv = process.argv.slice(2);
const productAt = argv.indexOf("--product-dir");
const productDir = productAt === -1 ? undefined : argv.splice(productAt, 2)[1];
if (productAt !== -1 && !productDir) {
  console.error("usage: node tools/build-controlplane.js OUT.js [--product-dir <dir>]");
  process.exit(2);
}
const outFile = argv[0] || resolve(__dirname, "../build/native/cp-bundle.js");

// The control plane as an ES module (tjsc consumes ESM). Asynchronous with a product (a plugin, which esbuild refuses
// in buildSync), synchronous for the desktop.
const options = {
  entryPoints: [resolve(PKG, "src/pluginControlPlane.ts")],
  bundle: true,
  format: "esm",
  platform: "neutral",
  mainFields: ["module", "main"],
  target: "es2020",
  // Pinned rather than discovered (tools/ts-options.js): the entry is the shell's, whose tsconfig implies alwaysStrict.
  tsconfigRaw: tsOptions({ alwaysStrict: true }),
  outfile: outFile,
  define: {
    "process.env.NODE_ENV": '"production"',
  },
  nodePaths: productDir ? [SHELL_NODE_MODULES] : undefined,
  plugins: [productGenerated({ dir: productDir })].filter(Boolean),
};
(productDir ? build(options) : Promise.resolve(buildSync(options)))
  .then(() => console.log(`wrote ${outFile}`))
  .catch((e) => {
    console.error(`build-controlplane: ${e.message}`);
    process.exit(1);
  });
