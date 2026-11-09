#!/usr/bin/env node
// The driver behind test/script/bundle-ui-split.test.ts: bundles each surface's entry the way the build does (through
// tools/build-session.js and tools/build-ui.js, in memory) and prints one JSON line - per bundle, the absolute paths of
// the modules that went in. The split is then a fact about the metafile rather than a claim about the imports: the
// control plane, the render worker, the script nodes, the SDK, the CLI dispatcher and the planner carry no UI code, and
// the UI bundle carries no CLI one.
//
// The build is Node's (esbuild is not a txiki module), so the test runs this in a node child, as
// bundle-protocol.test.ts does with its own driver.
import { createRequire } from "node:module";
import { resolve } from "node:path";

const require = createRequire(import.meta.url);
const { bundleInputs } = require("../../../../tools/build-session.js");
const { bundleInputs: uiBundleInputs } = require("../../../../tools/build-ui.js");

// The entries, and the format the build gives each: the render worker and the script nodes are iifes (a bare QuickJS
// runtime evals them). The script-node bundle is every Engine's, so it is a non-UI surface like the rest.
const BUNDLES = [
    ["control-plane", "src/pluginControlPlane.ts", "esm"],
    ["render-worker", "src/render/worker.ts", "iife"],
    ["script-nodes", "src/generated/scriptNodes.ts", "iife"],
    ["cli-sdk", "cli/sdk.ts", "esm"],
    ["cli-dispatcher", "cli/cli.ts", "esm"],
    ["planner", "src/graph/protocolNodes.ts", "esm"],
];

const out = {};
for (const [name, entry, format] of BUNDLES) {
    const built = bundleInputs(entry, format);
    out[name] = built.inputs.map((input) => resolve(process.cwd(), input));
}
// The UI bundle's own inputs are relative to the repository root (its absWorkingDir).
out.ui = uiBundleInputs(resolve(process.cwd(), "ui/main.tsx")).inputs;
process.stdout.write(JSON.stringify(out));
