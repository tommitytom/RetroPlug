// Shared esbuild plumbing for the three Node build scripts (build-ui.js,
// build-test.js, gen-rpc-ts.js). One esbuild (the workspace devDependency, not
// the copy vendored in deps/lv_binding_js), one alias map, one set of depfile
// helpers — so the bundler config lives in a single place.
//
// esbuild has a native `alias` option, so esbuild-plugin-alias is gone.
//
// Resolution notes (load-bearing):
//   - CMake runs these scripts with cwd = the build dir, so every consumer sets
//     `absWorkingDir: REPO_ROOT` and the alias values below are ABSOLUTE.
//   - `bundleMainFields` is required because esbuild's `platform: "neutral"`
//     has no default mainFields; without it bare deps with no "exports" map
//     (e.g. @msgpack/msgpack) don't resolve. "module" first picks their ESM.
//   - `quickjsTarget` keeps output within what QuickJS-ng (the txiki runtime
//     that evals the bundle, via tjsc bytecode) can parse.

const fs = require("fs");
const path = require("path");

const esbuild = require("esbuild");

const REPO_ROOT = path.resolve(__dirname, "..");

// The generic framework (lvgljs runtime, the lv_binding_js submodule with its
// React reconciler, and the rpcpp TS client) lives in the dpf.js package,
// resolved from node_modules. require.resolve returns the realpath through the
// pnpm link, so this is the on-disk dpf.js checkout.
const DPFJS_DIR      = path.dirname(require.resolve("dpf.js/package.json"));
const LV_BINDING_DIR = path.join(DPFJS_DIR, "deps/lv_binding_js");
const RPCPP_TS_DIR   = path.join(DPFJS_DIR, "deps/rpcpp/clients/typescript");

// The consumer's own node_modules — a nodePaths fallback so bare deps imported
// by the (now dpf.js-resident) rpcpp client (e.g. @msgpack/msgpack) still
// resolve against the consumer's install.
const WORKSPACE_NODE_MODULES = path.join(REPO_ROOT, "node_modules");

// The generated typed RPC clients (derived; produced by gen-rpc-ts.js).
const GENERATED_RPC_TS     = path.join(REPO_ROOT, "build/ui/generated/PluginService.ts");
const GENERATED_HARNESS_TS = path.join(REPO_ROOT, "build/generated/HarnessService.ts");

// The @retroplug/retroplug workspace package entry (the TS layer over native).
const RETROPLUG_TS = path.join(REPO_ROOT, "packages/retroplug/src/index.ts");

// react / react-reconciler / scheduler live in the dpf.js lv_binding_js
// submodule's node_modules, so the UI bundle keeps one nodePath into it.
const reactNodePath = path.join(LV_BINDING_DIR, "node_modules");

// Framework + generated aliases for the UI bundle: lvgljs* is the dpf.js
// renderer/runtime (bundled from source), @rpcpp/* points at the client's src
// files because the package index re-exports a Node-only Stdio transport
// (node:child_process) that trips a neutral build, and plugin-service is the
// generated client. All ABSOLUTE (see header).
const uiAliases = {
    "lvgljs-ui":           path.join(LV_BINDING_DIR, "src/render/react/index.ts"),
    "lvgljs":              path.join(DPFJS_DIR, "runtime/lvgljs/index.ts"),
    "@rpcpp/createClient": path.join(RPCPP_TS_DIR, "client/src/createClient.ts"),
    "@rpcpp/codec":        path.join(RPCPP_TS_DIR, "client/src/codec.ts"),
    "@rpcpp/transport":    path.join(RPCPP_TS_DIR, "client/src/transport.ts"),
    "plugin-service":      GENERATED_RPC_TS,
};

const commonDefine     = { "process.env.NODE_ENV": '"production"' };
const bundleMainFields = ["module", "main"];
const quickjsTarget    = "es2020";

// Make-style depfile: `<target>: <input1> <input2> ...`, with backslash-newline
// continuations and escaped spaces, as CMake's DEPFILE expects.
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

module.exports = {
    esbuild,
    REPO_ROOT,
    DPFJS_DIR,
    LV_BINDING_DIR,
    RPCPP_TS_DIR,
    GENERATED_RPC_TS,
    GENERATED_HARNESS_TS,
    RETROPLUG_TS,
    reactNodePath,
    WORKSPACE_NODE_MODULES,
    uiAliases,
    commonDefine,
    bundleMainFields,
    quickjsTarget,
    writeDepfile,
    escapeMake,
};
