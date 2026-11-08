// Author a risa `.rplg.zip` fixture (ROM embedded + the authored .srm battery) for the reaper risa
// host-sync render: esbuild author-risa-rplg.ts (injecting the ROM + output path) and run it on
// retroplug-host, which EXPORTS a PKZIP `.rplg.zip`. Counterpart of tools/author-nes-rplg.js (n8-midi)
// and tools/author-lsdj-rplg.js.
//
//   node tools/author-risa-rplg.js [ROM] [OUT.rplg.zip]
//     ROM   default under $RETROPLUG_RESOURCES_DIR/roms/risa/ when that is set,
//           <repo>/../resources/roms/risa/ otherwise
//     OUT   default build/risa.rplg.zip
//
// The ROM lives in the sibling resources tree (like the LSDj ones), so this exits 0 with a notice when
// it isn't present - the render script skips rather than failing a resource-less checkout.
const { buildSync } = require("esbuild");
const { execFileSync } = require("child_process");
const { resolve, dirname } = require("path");
const { mkdirSync, existsSync } = require("fs");

const REPO = resolve(__dirname, "..");
const PKG = resolve(REPO, "packages/retroplug");
const HOST = process.env.RETROPLUG_HOST || resolve(REPO, "build/bin/retroplug-host");

// The sibling resources tree, or the one RETROPLUG_RESOURCES_DIR names (as the native test runner reads it); an
// explicit ROM argument is still resolved against the repo.
const RESOURCES = process.env.RETROPLUG_RESOURCES_DIR
  ? resolve(process.env.RETROPLUG_RESOURCES_DIR)
  : resolve(REPO, "..", "resources");
const ROM = process.argv[2]
  ? resolve(REPO, process.argv[2])
  : resolve(RESOURCES, "roms/risa/risa-v2.3.0/risa-2.3.0-pal.nes");
const OUT = resolve(process.argv[3] || resolve(REPO, "build/risa.rplg.zip"));

if (!existsSync(ROM)) {
  console.log(`[author-risa-rplg] SKIP: risa ROM not found at ${ROM}`);
  process.exit(0);
}

mkdirSync(dirname(OUT), { recursive: true });
const bundle = resolve(REPO, "build/native/author-risa-rplg.js");
mkdirSync(dirname(bundle), { recursive: true });

// The generated package registries first (docs/modular-graph-phase-f1-design.md section 3): the author script
// reaches them through src/appHost, so a standalone run in a tree nothing has built yet has none to bundle.
execFileSync(process.execPath, [resolve(PKG, "scripts/lib/packages.mjs")], { stdio: "inherit" });
buildSync({
  entryPoints: [resolve(PKG, "test-native/author-risa-rplg.ts")],
  bundle: true,
  format: "esm",
  platform: "neutral",
  mainFields: ["module", "main"],
  target: "es2020",
  outfile: bundle,
  define: {
    "process.env.NODE_ENV": '"production"',
    __RISA_ROM__: JSON.stringify(ROM),
    __RPLG_OUT__: JSON.stringify(OUT),
  },
});

execFileSync(HOST, [bundle], { stdio: "inherit" });
console.log(`authored ${OUT}`);
