// Author an LSDj `.rplg.zip` fixture for the DAW-timing reaper renders: esbuild
// author-lsdj-rplg.ts (injecting the scenario + ROM + output path) and run it on retroplug-host. It
// EXPORTS (PKZIP, with the LSDj sav blob) → `.rplg.zip`; a plain `.rplg` is thin JSON and never a zip.
//
//   node tools/author-lsdj-rplg.js <scenario> [ROM] [OUT.rplg.zip]
//     scenario  midi-metro | arduinoboy-metro | midi-drift
//     ROM       default per scenario, under $RETROPLUG_RESOURCES_DIR/roms/lsdj/ when that is set and
//               <repo>/../resources/roms/lsdj/ otherwise (the same variable the native test runner
//               reads, so a worktree nested inside the main checkout finds the tree it was given)
//     OUT       default build/lsdj_<scenario>.rplg.zip
const { buildSync } = require("esbuild");
const { execFileSync } = require("child_process");
const { resolve, dirname } = require("path");
const { mkdirSync } = require("fs");

const REPO = resolve(__dirname, "..");
const PKG = resolve(REPO, "packages/retroplug");
const HOST = process.env.RETROPLUG_HOST || resolve(REPO, "build/bin/retroplug-host");

const scenario = process.argv[2];
const DEFAULT_ROM = {
  "midi-metro": "lsdj9_4_2.gb",
  "midi-drift": "lsdj9_4_2.gb",
  "arduinoboy-metro": "lsdj9_3_3-arduinoboy.gb",
};
if (!DEFAULT_ROM[scenario]) {
  console.error(`usage: node tools/author-lsdj-rplg.js <midi-metro|arduinoboy-metro|midi-drift> [ROM] [OUT.rplg]`);
  process.exit(2);
}

// The sibling resources tree, or the one RETROPLUG_RESOURCES_DIR names: every other fixture consumer reads that
// variable, and a worktree nested inside the main checkout has no ../resources of its own. An explicit ROM argument is
// still resolved against the repo, as it always was.
const RESOURCES = process.env.RETROPLUG_RESOURCES_DIR
  ? resolve(process.env.RETROPLUG_RESOURCES_DIR)
  : resolve(REPO, "..", "resources");
const ROM = process.argv[3]
  ? resolve(REPO, process.argv[3])
  : resolve(RESOURCES, "roms/lsdj", DEFAULT_ROM[scenario]);
const OUT = resolve(process.argv[4] || resolve(REPO, `build/lsdj_${scenario}.rplg.zip`));

mkdirSync(dirname(OUT), { recursive: true });
const bundle = resolve(REPO, `build/native/author-lsdj-rplg-${scenario}.js`);
mkdirSync(dirname(bundle), { recursive: true });

// The generated package registries first (docs/modular-graph-phase-f1-design.md section 3): the author script
// reaches them through src/appHost, so a standalone run in a tree nothing has built yet has none to bundle.
execFileSync(process.execPath, [resolve(PKG, "scripts/lib/packages.mjs")], { stdio: "inherit" });
buildSync({
  entryPoints: [resolve(PKG, "test-native/author-lsdj-rplg.ts")],
  bundle: true,
  format: "esm",
  platform: "neutral",
  mainFields: ["module", "main"],
  target: "es2020",
  outfile: bundle,
  define: {
    "process.env.NODE_ENV": '"production"',
    __SCENARIO__: JSON.stringify(scenario),
    __LSDJ_ROM__: JSON.stringify(ROM),
    __RPLG_OUT__: JSON.stringify(OUT),
  },
});

execFileSync(HOST, [bundle], { stdio: "inherit" });
console.log(`authored ${OUT}`);
