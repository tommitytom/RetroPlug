// Author an smsggdj `.rplg.zip` fixture (ROM embedded + the authored battery + a savestate holding the
// loaded, ARMED song) for the reaper SMS host-sync render: esbuild author-sms-rplg.ts (injecting the
// ROM + output path) and run it on retroplug-host, which EXPORTS a PKZIP `.rplg.zip`. Counterpart of
// tools/author-risa-rplg.js (risa) and tools/author-nes-rplg.js (n8-midi).
//
//   node tools/author-sms-rplg.js [ROM] [OUT.rplg.zip]
//     ROM   default resources/roms/smsggdj_v0_45.sms
//     OUT   default build/sms.rplg.zip
//
// Unlike the risa/LSDj ROMs this one is committed in-repo, so the usual "skip when the sibling
// resources tree is absent" case does not arise - but the guard is kept so an explicit override to a
// missing path still exits 0 rather than failing a render script.
const { buildSync } = require("esbuild");
const { execFileSync } = require("child_process");
const { resolve, dirname } = require("path");
const { mkdirSync, existsSync } = require("fs");

const REPO = resolve(__dirname, "..");
const PKG = resolve(REPO, "packages/retroplug");
const HOST = process.env.RETROPLUG_HOST || resolve(REPO, "build/bin/retroplug-host");

const ROM = resolve(REPO, process.argv[2] || "resources/roms/smsggdj_v0_45.sms");
const OUT = resolve(process.argv[3] || resolve(REPO, "build/sms.rplg.zip"));

if (!existsSync(ROM)) {
  console.log(`[author-sms-rplg] SKIP: smsggdj ROM not found at ${ROM}`);
  process.exit(0);
}

mkdirSync(dirname(OUT), { recursive: true });
const bundle = resolve(REPO, "build/native/author-sms-rplg.js");
mkdirSync(dirname(bundle), { recursive: true });

buildSync({
  entryPoints: [resolve(PKG, "test-native/author-sms-rplg.ts")],
  bundle: true,
  format: "esm",
  platform: "neutral",
  mainFields: ["module", "main"],
  target: "es2020",
  outfile: bundle,
  define: {
    "process.env.NODE_ENV": '"production"',
    __SMS_ROM__: JSON.stringify(ROM),
    __RPLG_OUT__: JSON.stringify(OUT),
  },
});

execFileSync(HOST, [bundle], { stdio: "inherit" });
console.log(`authored ${OUT}`);
