// build-test.js — transpile + bundle one TypeScript test file into a single
// ESM .js the embedded QuickJS harness (`retroplug-cli --test`) can eval.
//
//   node build-test.js <entry.test.ts> <out.js> [out.d]
//
// Two aliases: "harness" -> test/harness/index.ts (emu tests: `import { emu }
// from "harness"`) and "ui-harness" -> test/harness/ui.ts (UI tests). Both are
// applied unconditionally; a test imports whichever it needs. The harness graph
// is self-contained, so no nodePaths are needed.

const path = require("path");
const fs = require("fs");
const {
    esbuild,
    REPO_ROOT,
    commonDefine,
    bundleMainFields,
    quickjsTarget,
    writeDepfile,
} = require("./esbuild-shared");

const HARNESS_TS    = path.join(REPO_ROOT, "test/harness/index.ts");
const UI_HARNESS_TS = path.join(REPO_ROOT, "test/harness/ui.ts");

const entryArg = process.argv[2];
const outArg   = process.argv[3];
const depArg   = process.argv[4];

if (!entryArg || !outArg) {
    console.error("usage: node build-test.js <entry.test.ts> <out.js> [out.d]");
    process.exit(2);
}

const entryPath = path.resolve(entryArg);
const outPath   = path.resolve(outArg);
fs.mkdirSync(path.dirname(outPath), { recursive: true });

esbuild
    .build({
        entryPoints: [entryPath],
        bundle: true,
        platform: "neutral",
        format: "esm",
        mainFields: bundleMainFields,
        target: quickjsTarget,
        absWorkingDir: REPO_ROOT,
        outfile: outPath,
        alias: { harness: HARNESS_TS, "ui-harness": UI_HARNESS_TS },
        define: commonDefine,
        metafile: true,
    })
    .then((result) => {
        console.log(`test bundle built: ${outPath}`);
        if (depArg) {
            const depPath = path.resolve(depArg);
            fs.mkdirSync(path.dirname(depPath), { recursive: true });
            writeDepfile(depPath, outPath, result.metafile);
        }
    })
    .catch((e) => {
        console.error(e);
        process.exit(1);
    });
