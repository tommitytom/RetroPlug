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
    WORKSPACE_NODE_MODULES,
} = require("./esbuild-shared");
const { productGenerated } = require("./product-generated");

// The options this bundle builds with. No tsconfigRaw, unlike every other embedded bundle (tools/ts-options.js says why
// they name one): esbuild reads a tsconfig per MODULE, and this bundle's modules come from two trees with different
// ones - the shell's (target ES2020, so a class field is assigned) and dpf.js's lv_binding_js (no target, so esbuild's
// default is define semantics, with its __publicField helper). So this bundle's BYTES do follow those two tsconfigs: a
// `target` or `useDefineForClassFields` edit in either moves them, which no other embedded bundle here allows. Pinning
// one set would flatten a real difference in how every dpf.js component class initialises, so the exception is kept
// rather than the bytes being made tsconfig-independent.
function uiBuildOptions(entryPath, outfile, product = undefined) {
    return {
        entryPoints: [entryPath],
        bundle: true,
        platform: "neutral",          // default format is ESM, which tjsc consumes
        mainFields: bundleMainFields, // neutral has none by default; see esbuild-shared
        target: quickjsTarget,
        absWorkingDir: REPO_ROOT,     // native alias values resolve vs the working dir
        external: ["tjs:path"],
        jsx: "automatic",
        outfile,
        // react / react-reconciler / scheduler resolve from the dpf.js
        // lv_binding_js node_modules; @msgpack (imported by the dpf.js-resident
        // rpcpp client) and zod resolve from the consumer's node_modules.
        nodePaths: product
            // A product's tree is not under the shell: its @retroplug/* specifiers resolve from the shell's
            // node_modules.
            ? [reactNodePath, WORKSPACE_NODE_MODULES, path.join(REPO_ROOT, "packages/retroplug/node_modules")]
            : [reactNodePath, WORKSPACE_NODE_MODULES],
        alias: uiAliases,
        define: commonDefine,
        metafile: true,
        // A product whose package list differs from the desktop's has its own generated tree, which the shell's
        // sources read through this resolver (tools/product-generated.js). Undefined is the desktop.
        plugins: [productGenerated({ dir: product })].filter(Boolean),
    };
}

/** The modules `entry` bundled to `outfile` carries, as absolute paths: test/script/bundle-ui-split.test.ts holds the
 *  UI bundle to carrying no CLI code, and the non-UI bundles to carrying no UI. Built in memory, so nothing is
 *  written. */
async function bundleInputs(entry, outfile = path.resolve(REPO_ROOT, "build/ui/bundle.js"), product = undefined) {
    // Asynchronous because a product's redirect is a plugin, and esbuild refuses plugins in buildSync.
    const result = await esbuild.build({ ...uiBuildOptions(entry, outfile, product), write: false, metafile: true });
    return {
        inputs: Object.keys(result.metafile.inputs).map((input) => path.resolve(REPO_ROOT, input)),
        bytes: result.outputFiles[0].contents.length,
    };
}

module.exports = { uiBuildOptions, bundleInputs };

if (require.main === module) {
    // Args from CMake (or default to writing into ../build/ui/ for ad-hoc runs).
    //   node build-ui.js <bundle.js> [bundle.d] [ui-entry.tsx] [--product-dir <dir>]
    // The UI entry is a consumer-owned build input (the framework doesn't hardcode
    // it); CMake passes the RetroPlug entry, ad-hoc runs default to it.
    // Bytecode compilation (bundle.js → bundle_data.c) is a separate step
    // driven by CMake via txiki's `tjsc` binary.
    const argv = process.argv.slice(2);
    // The product flag, in the form the other two build scripts take it (tools/product-generated.js), and only when
    // there is a product tree: no flag is the desktop, the shell's own tree.
    const productAt = argv.indexOf("--product-dir");
    const productArg = productAt === -1 ? undefined : argv.splice(productAt, 2)[1];
    if (productAt !== -1 && !productArg) {
        console.error("usage: node tools/build-ui.js <bundle.js> [bundle.d] [ui-entry.tsx] [--product-dir <dir>]");
        process.exit(2);
    }
    const bundleArg = argv[0];
    const depArg    = argv[1];
    const entryArg  = argv[2];

    const bundlePath = bundleArg
        ? path.resolve(bundleArg)
        : path.resolve(REPO_ROOT, "build/ui/bundle.js");

    const entryPath = entryArg
        ? path.resolve(entryArg)
        : path.resolve(REPO_ROOT, "packages/ui/src/PluginUI.tsx");

    fs.mkdirSync(path.dirname(bundlePath), { recursive: true });

    Promise.resolve(esbuild.build(uiBuildOptions(entryPath, bundlePath, productArg)))
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
}
