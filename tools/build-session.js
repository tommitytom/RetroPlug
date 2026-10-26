// Bundle a CLI session (TypeScript) to a self-contained ES-module .js that the standalone
// retroplug-cli binary evals on txiki. Mirrors build-controlplane.js. (The DSP kernel is a script node
// every Engine has embedded, so a session carries no kernel of its own.)
// This is the ONLY Node step — it runs at author/build time; the resulting .js runs with no Node.
//
//   node tools/build-session.js <entry.ts> <out.js> [format]
//
// format defaults to "esm" (the CLI evals sessions as ES modules via evalModuleBuffer). Pass "iife" to emit
// a self-contained global-code script — used by the background render worker, which the native RenderHost
// loads on a BARE QuickJS runtime (no module loader) via JS_ReadObject + JS_EvalFunction.
const { buildSync } = require("esbuild");
const { resolve } = require("path");

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

// The session, bundled (esm for the CLI's module eval, or iife global-code for the render worker). Config-
// dir / resource paths are NOT baked in — a session resolves paths at runtime through the backend.
buildSync({
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
});

console.log(`wrote ${outFile}`);
