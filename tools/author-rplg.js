// Author an mGB `.rplg.zip` fixture: esbuild author-mgb-rplg.ts (injecting the output path) and
// run it on retroplug-host, which composes a store + exports the project (PKZIP → `.rplg.zip`; a plain
// `.rplg` is thin JSON only and is never loaded as a zip).
//
//   node tools/author-rplg.js [OUT.rplg.zip]   (default build/mgb.rplg.zip)
const { buildSync } = require("esbuild");
const { execFileSync } = require("child_process");
const { resolve, dirname } = require("path");
const { mkdirSync } = require("fs");

const REPO = resolve(__dirname, "..");
const PKG = resolve(REPO, "packages/retroplug");
const OUT = resolve(process.argv[2] || resolve(REPO, "build/mgb.rplg.zip"));
const HOST = process.env.RETROPLUG_HOST || resolve(REPO, "build/bin/retroplug-host");

mkdirSync(dirname(OUT), { recursive: true });
const bundle = resolve(REPO, "build/native/author-mgb-rplg.js");
mkdirSync(dirname(bundle), { recursive: true });

buildSync({
  entryPoints: [resolve(PKG, "test-native/author-mgb-rplg.ts")],
  bundle: true,
  format: "esm",
  platform: "neutral",
  mainFields: ["module", "main"],
  target: "es2020",
  outfile: bundle,
  define: {
    "process.env.NODE_ENV": '"production"',
    __RPLG_OUT__: JSON.stringify(OUT),
  },
});

execFileSync(HOST, [bundle], { stdio: "inherit" });
console.log(`authored ${OUT}`);
