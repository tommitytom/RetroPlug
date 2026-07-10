// Author a greenfield mGB `.rplg` fixture: esbuild author-mgb-rplg.ts (injecting the output path) and
// run it on retroplug-host, which composes a store + exports the project.
//
//   node tools/author-greenfield-rplg.js [OUT.rplg]   (default build/mgb_greenfield.rplg)
const { buildSync } = require("esbuild");
const { execFileSync } = require("child_process");
const { resolve, dirname } = require("path");
const { mkdirSync } = require("fs");

const REPO = resolve(__dirname, "..");
const PKG = resolve(REPO, "packages/retroplug");
const OUT = resolve(process.argv[2] || resolve(REPO, "build/mgb_greenfield.rplg"));
const HOST = process.env.RETROPLUG_GREENFIELD_HOST || resolve(REPO, "build/bin/retroplug-host");

mkdirSync(dirname(OUT), { recursive: true });
const bundle = resolve(REPO, "build/native-greenfield/author-mgb-rplg.js");
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
