// build-test.js — transpile + bundle one TypeScript test file into a single
// ESM .js the embedded QuickJS harness (`retroplug-cli --test`) can eval.
//
//   node build-test.js <entry.test.ts> <out.js> [out.d]
//
// Mirrors tools/build-ui.js (same vendored esbuild + alias plugin), but the
// only alias is "harness" -> test/harness/index.ts, so test files can do
// `import { test, expect, emu, Button, Mem } from "harness"`.

const fs = require("fs");
const path = require("path");

const REPO_ROOT      = path.resolve(__dirname, "..");
const LV_BINDING_DIR = path.join(REPO_ROOT, "deps/lv_binding_js");
const HARNESS_TS     = path.join(REPO_ROOT, "test/harness/index.ts");

const esbuild     = require(path.join(LV_BINDING_DIR, "node_modules/esbuild"));
const aliasPlugin = require(path.join(LV_BINDING_DIR, "node_modules/esbuild-plugin-alias"));

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
        outfile: outPath,
        nodePaths: [path.join(LV_BINDING_DIR, "node_modules")],
        plugins: [aliasPlugin({ harness: HARNESS_TS })],
        define: { "process.env.NODE_ENV": '"production"' },
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

function writeDepfile(depPath, target, metafile) {
    const inputs = Object.keys(metafile.inputs)
        .map((p) => path.resolve(p))
        .map(escapeMake);
    const body = `${escapeMake(target)}: ${inputs.join(" \\\n  ")}\n`;
    fs.writeFileSync(depPath, body);
}

function escapeMake(p) {
    return p.replace(/\\/g, "\\\\").replace(/ /g, "\\ ");
}
