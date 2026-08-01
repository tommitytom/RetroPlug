// Author a NES `.rplg.zip` fixture (ROM embedded) for the reaper NES MIDI-timing render: esbuild
// author-nes-rplg.ts (injecting the ROM + output path) and run it on retroplug-host, which EXPORTS a
// PKZIP `.rplg.zip`. Counterpart of tools/author-rplg.js (mGB) / author-lsdj-rplg.js.
//
//   node tools/author-nes-rplg.js [ROM] [OUT.rplg.zip]
//     ROM   default resources/roms/n8-midi.nes
//     OUT   default build/nes.rplg.zip
const { buildSync } = require("esbuild");
const { execFileSync } = require("child_process");
const { resolve, dirname } = require("path");
const { mkdirSync } = require("fs");

const REPO = resolve(__dirname, "..");
const PKG = resolve(REPO, "packages/retroplug");
const HOST = process.env.RETROPLUG_HOST || resolve(REPO, "build/bin/retroplug-host");

const ROM = resolve(REPO, process.argv[2] || "resources/roms/n8-midi.nes");
const OUT = resolve(process.argv[3] || resolve(REPO, "build/nes.rplg.zip"));

mkdirSync(dirname(OUT), { recursive: true });
const bundle = resolve(REPO, "build/native/author-nes-rplg.js");
mkdirSync(dirname(bundle), { recursive: true });

buildSync({
  entryPoints: [resolve(PKG, "test-native/author-nes-rplg.ts")],
  bundle: true,
  format: "esm",
  platform: "neutral",
  mainFields: ["module", "main"],
  target: "es2020",
  outfile: bundle,
  define: {
    "process.env.NODE_ENV": '"production"',
    __NES_ROM__: JSON.stringify(ROM),
    __RPLG_OUT__: JSON.stringify(OUT),
  },
});

execFileSync(HOST, [bundle], { stdio: "inherit" });
console.log(`authored ${OUT}`);
