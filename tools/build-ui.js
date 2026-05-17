const fs = require("fs");
const path = require("path");

// Resolve esbuild from lv_binding_js's node_modules
const REPO_ROOT       = path.resolve(__dirname, "..");
const LV_BINDING_DIR  = path.join(REPO_ROOT, "deps/lv_binding_js");
const RPCPP_TS_DIR    = path.join(REPO_ROOT, "deps/rpcpp/clients/typescript");
const GENERATED_RPC_TS = path.join(REPO_ROOT, "build/ui/generated/PluginService.ts");
const esbuild = require(path.join(LV_BINDING_DIR, "node_modules/esbuild"));
const aliasPlugin = require(path.join(LV_BINDING_DIR, "node_modules/esbuild-plugin-alias"));

// Args from CMake (or default to writing into ../build/ui/ for ad-hoc runs).
//   node build-ui.js <bundle.js> [bundle.d]
// Bytecode compilation (bundle.js → bundle_data.c) is a separate step
// driven by CMake via txiki's `tjsc` binary.
const bundleArg = process.argv[2];
const depArg    = process.argv[3];

const bundlePath = bundleArg
    ? path.resolve(bundleArg)
    : path.resolve(__dirname, "../build/ui/bundle.js");

fs.mkdirSync(path.dirname(bundlePath), { recursive: true });

esbuild
    .build({
        entryPoints: [path.resolve(__dirname, "../ui/PluginUI.tsx")],
        bundle: true,
        platform: "neutral",
        external: ["tjs:path"],
        jsx: "automatic",
        outfile: bundlePath,
        nodePaths: [
            path.join(LV_BINDING_DIR, "node_modules"),
            path.join(REPO_ROOT, "node_modules"),  // for @msgpack/msgpack
        ],
        // Read the renderer source directly, bypassing pnpm's file:-dep copy.
        plugins: [
            aliasPlugin({
                "lvgljs-ui":      path.join(LV_BINDING_DIR, "src/render/react/index.ts"),
                "lvgljs":         path.resolve(__dirname, "../runtime/lvgljs/index.ts"),
                // Pull rpcpp client pieces directly from the package's src
                // (not its index, which re-exports the Node-only Stdio
                // transport family and trips esbuild on node:child_process).
                "@rpcpp/createClient":  path.join(RPCPP_TS_DIR, "client/src/createClient.ts"),
                "@rpcpp/MsgpackCodec":  path.join(RPCPP_TS_DIR, "client/src/codecs/MsgpackCodec.ts"),
                "@rpcpp/transport":     path.join(RPCPP_TS_DIR, "client/src/transport.ts"),
                // esbuild 0.14 vendored in lv_binding_js doesn't pick up
                // @msgpack/msgpack's .cjs/.mjs main fields under
                // platform: "neutral". Point straight at the ESM bundle.
                "@msgpack/msgpack":     path.join(REPO_ROOT, "node_modules/@msgpack/msgpack/dist.esm/index.mjs"),
                "plugin-service": GENERATED_RPC_TS,
            }),
        ],
        define: {
            "process.env.NODE_ENV": '"production"',
        },
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

function writeDepfile(depPath, target, metafile) {
    // Make-style depfile: `<target>: <input1> <input2> ...`
    // CMake's DEPFILE expects POSIX-ish target paths and space-separated deps,
    // with backslash-newline continuations and escaped spaces.
    const inputs = Object.keys(metafile.inputs)
        .map((p) => path.resolve(p))
        .map(escapeMake);
    const escapedTarget = escapeMake(target);
    const body = `${escapedTarget}: ${inputs.join(" \\\n  ")}\n`;
    fs.writeFileSync(depPath, body);
}

function escapeMake(p) {
    // Escape spaces and backslashes for Make
    return p.replace(/\\/g, "\\\\").replace(/ /g, "\\ ");
}
