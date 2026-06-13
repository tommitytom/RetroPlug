// build-cli.js — transpile + bundle the end-user CLI (packages/cli) into a
// single ESM .js for tjsc to compile to bytecode and embed in retroplug-cli.
//
//   node build-cli.js <out.js> [out.d]
//
// Entry: packages/cli/src/main.ts. Aliases mirror build-test.js: the workspace
// TS layer (@retroplug/retroplug) and the generated harness client
// (harness-service, pulled in transitively by createEmu). The graph is
// self-contained, so no nodePaths are needed.

const path = require("path");
const fs = require("fs");
const {
    esbuild,
    REPO_ROOT,
    GENERATED_HARNESS_TS,
    RETROPLUG_TS,
    commonDefine,
    bundleMainFields,
    quickjsTarget,
    writeDepfile,
} = require("./esbuild-shared");

const ENTRY = path.join(REPO_ROOT, "packages/cli/src/main.ts");

const outArg = process.argv[2];
const depArg = process.argv[3];

if (!outArg) {
    console.error("usage: node build-cli.js <out.js> [out.d]");
    process.exit(2);
}

const outPath = path.resolve(outArg);
fs.mkdirSync(path.dirname(outPath), { recursive: true });

esbuild
    .build({
        entryPoints: [ENTRY],
        bundle: true,
        platform: "neutral",
        format: "esm",
        mainFields: bundleMainFields,
        target: quickjsTarget,
        absWorkingDir: REPO_ROOT,
        outfile: outPath,
        alias: {
            "harness-service": GENERATED_HARNESS_TS,
            "@retroplug/retroplug": RETROPLUG_TS,
        },
        define: commonDefine,
        metafile: true,
    })
    .then((result) => {
        console.log(`cli bundle built: ${outPath}`);
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
