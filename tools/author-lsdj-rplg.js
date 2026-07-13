// Author an LSDj `.rplg.zip` fixture for the DAW-timing reaper renders: esbuild
// author-lsdj-rplg.ts (injecting the scenario + ROM + output path) and run it on retroplug-host. It
// EXPORTS (PKZIP, with the LSDj sav blob) → `.rplg.zip`; a plain `.rplg` is thin JSON and never a zip.
//
//   node tools/author-lsdj-rplg.js <scenario> [ROM] [OUT.rplg.zip]
//     scenario  midi-metro | arduinoboy-metro | midi-drift
//     ROM       default per scenario (../resources/roms/lsdj/…)
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
  "midi-metro": "../resources/roms/lsdj/lsdj9_4_2.gb",
  "midi-drift": "../resources/roms/lsdj/lsdj9_4_2.gb",
  "arduinoboy-metro": "../resources/roms/lsdj/lsdj9_3_3-arduinoboy.gb",
};
if (!DEFAULT_ROM[scenario]) {
  console.error(`usage: node tools/author-lsdj-rplg.js <midi-metro|arduinoboy-metro|midi-drift> [ROM] [OUT.rplg]`);
  process.exit(2);
}

const ROM = resolve(REPO, process.argv[3] || DEFAULT_ROM[scenario]);
const OUT = resolve(process.argv[4] || resolve(REPO, `build/lsdj_${scenario}.rplg.zip`));

mkdirSync(dirname(OUT), { recursive: true });
const bundle = resolve(REPO, `build/native/author-lsdj-rplg-${scenario}.js`);
mkdirSync(dirname(bundle), { recursive: true });

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
