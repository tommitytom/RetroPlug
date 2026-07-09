// Bundle a greenfield CLI session (TypeScript) to a self-contained ES-module .js that the standalone
// retroplug-greenfield-cli binary evals on txiki. Mirrors build-greenfield-controlplane.js: esbuild the
// DSP role kernel to an IIFE and inject it as __DSP_KERNEL_BUNDLE__, then esbuild the session entry.
// This is the ONLY Node step — it runs at author/build time; the resulting .js runs with no Node.
//
//   node tools/build-greenfield-session.js <entry.ts> <out.js>
const { buildSync } = require("esbuild");
const { resolve } = require("path");

const PKG = resolve(__dirname, "../packages/retroplug-greenfield");

const entry = process.argv[2];
const outFile = process.argv[3];
if (!entry || !outFile) {
  console.error("usage: node tools/build-greenfield-session.js <entry.ts> <out.js>");
  process.exit(2);
}

// The DSP role kernel as a self-contained IIFE — the session's bootSession() loadKernel()s it so audio
// renders. Baked into the bundle as __DSP_KERNEL_BUNDLE__.
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

// The session as an ES module (the host evals it via evalModuleBuffer). Config-dir / resource paths are
// NOT baked in — a session resolves paths at runtime through the backend.
buildSync({
  entryPoints: [resolve(entry)],
  bundle: true,
  format: "esm",
  platform: "neutral",
  mainFields: ["module", "main"],
  target: "es2020",
  outfile: resolve(outFile),
  define: {
    "process.env.NODE_ENV": '"production"',
    __DSP_KERNEL_BUNDLE__: JSON.stringify(DSP_KERNEL_BUNDLE),
  },
});

console.log(`wrote ${outFile}`);
