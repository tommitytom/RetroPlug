// Build the greenfield plugin's control-plane bundle (pluginControlPlane.ts) for embedding as
// bytecode. Mirrors run-native-tests.mjs: esbuild the DSP role kernel to an IIFE and inject it as
// __DSP_KERNEL_BUNDLE__, then esbuild the control plane to an ES module CMake compiles with tjsc.
//
//   node tools/build-greenfield-controlplane.js OUT.js
const { buildSync } = require("esbuild");
const { resolve } = require("path");

const PKG = resolve(__dirname, "../packages/retroplug");
const outFile = process.argv[2] || resolve(__dirname, "../build/native-greenfield/cp-bundle.js");

// The DSP role kernel as a self-contained IIFE (the bare DSP QuickJS context evals this per structure
// change). Injected into the control plane so it can loadKernel() at boot.
const DSP_KERNEL_BUNDLE = buildSync({
  entryPoints: [resolve(PKG, "src/dspKernelBundle.ts")],
  bundle: true,
  format: "iife",
  platform: "neutral",
  write: false,
  define: { "process.env.NODE_ENV": '"production"' },
}).outputFiles[0].text;

// The control plane as an ES module (tjsc consumes ESM). __DSP_KERNEL_BUNDLE__ is baked in.
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
    __DSP_KERNEL_BUNDLE__: JSON.stringify(DSP_KERNEL_BUNDLE),
  },
});

console.log(`wrote ${outFile}`);
