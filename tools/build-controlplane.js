// Build the plugin's control-plane bundle (pluginControlPlane.ts) for embedding as
// bytecode: esbuild the control plane to an ES module CMake compiles with tjsc. (The DSP kernel is a
// script node every Engine has embedded, so the control plane carries no kernel of its own.)
//
//   node tools/build-controlplane.js OUT.js
const { buildSync } = require("esbuild");
const { resolve } = require("path");

const PKG = resolve(__dirname, "../packages/retroplug");
const outFile = process.argv[2] || resolve(__dirname, "../build/native/cp-bundle.js");

// The control plane as an ES module (tjsc consumes ESM).
buildSync({
  entryPoints: [resolve(PKG, "src/pluginControlPlane.ts")],
  bundle: true,
  format: "esm",
  platform: "neutral",
  mainFields: ["module", "main"],
  target: "es2020",
  outfile: outFile,
  define: {
    "process.env.NODE_ENV": '"production"',
  },
});

console.log(`wrote ${outFile}`);
