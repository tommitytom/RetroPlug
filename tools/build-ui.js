const path = require("path");
const fs = require("fs");
const {
    esbuild,
    REPO_ROOT,
    reactNodePath,
    uiAliases,
    commonDefine,
    bundleMainFields,
    quickjsTarget,
    writeDepfile,
} = require("./esbuild-shared");

// Args from CMake (or default to writing into ../build/ui/ for ad-hoc runs).
//   node build-ui.js <bundle.js> [bundle.d]
// Bytecode compilation (bundle.js → bundle_data.c) is a separate step
// driven by CMake via txiki's `tjsc` binary.
const bundleArg = process.argv[2];
const depArg    = process.argv[3];

const bundlePath = bundleArg
    ? path.resolve(bundleArg)
    : path.resolve(REPO_ROOT, "build/ui/bundle.js");

fs.mkdirSync(path.dirname(bundlePath), { recursive: true });

esbuild
    .build({
        entryPoints: [path.resolve(REPO_ROOT, "packages/ui/src/PluginUI.tsx")],
        bundle: true,
        platform: "neutral",          // default format is ESM, which tjsc consumes
        mainFields: bundleMainFields, // neutral has none by default; see esbuild-shared
        target: quickjsTarget,
        absWorkingDir: REPO_ROOT,     // native alias values resolve vs the working dir
        external: ["tjs:path"],
        jsx: "automatic",
        outfile: bundlePath,
        // react / react-reconciler / scheduler still resolve from the submodule
        // (framework; leaves for dpf.js at restructure-07). @msgpack/zod resolve
        // by normal walk-up to the workspace root node_modules.
        nodePaths: [reactNodePath],
        alias: uiAliases,
        define: commonDefine,
        metafile: true,
    })
    .then((result) => {
        console.log(`UI bundle built: ${bundlePath}`);

        if (depArg) {
            const depPath = path.resolve(depArg);
            fs.mkdirSync(path.dirname(depPath), { recursive: true });
            writeDepfile(depPath, bundlePath, result.metafile);
        }
    })
    .catch((e) => {
        console.error(e);
        process.exit(1);
    });
