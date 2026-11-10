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

// A product's list may be named (its manifest name, as the root package.json declares it): the driver generates that
// product's tree into a temporary build directory and builds the four bundles a product has from it, exactly as the
// build does (tools/product-generated.js through each build script). No name is the desktop.
const REPO = resolve(process.cwd(), "../..");
const productName = process.argv[2];
// The desktop's shape is exactly what it was (one key per bundle); a product's adds its generated modules' text, which
// the tests read to hold the script entry's registrations and span labels.
const out = productName ? { product: productName, generated: {} } : {};
let productDir;
let work;
if (productName) {
    const { mkdtempSync } = await import("node:fs");
    const { tmpdir } = await import("node:os");
    const { join } = await import("node:path");
    const { generate } = await import("../../../../packages/retroplug/scripts/lib/packages.mjs");
    work = mkdtempSync(join(tmpdir(), "rp-bundles-"));
    generate({ root: REPO, product: productName, buildDir: work });
    productDir = join(work, "products", productName, "packages/retroplug");
    const { readFileSync } = await import("node:fs");
    out.generated.scriptNodes = readFileSync(join(productDir, "src/generated/scriptNodes.ts"), "utf8");
    out.generated.packages = readFileSync(join(productDir, "src/generated/packages.ts"), "utf8");
}
try {
    const entries = productName
        ? [
              ["control-plane", "src/pluginControlPlane.ts", "esm"],
              ["render-worker", "src/render/worker.ts", "iife"],
              ["script-nodes", `${productDir}/src/generated/scriptNodes.ts`, "iife"],
          ]
        : BUNDLES;
    for (const [name, entry, format] of entries) {
        const built = await bundleInputs(entry, format, productDir);
        out[name] = built.inputs.map((input) => resolve(process.cwd(), input));
    }
    // The UI bundle's own inputs are relative to the repository root (its absWorkingDir).
    out.ui = (await uiBundleInputs(resolve(process.cwd(), "ui/main.tsx"), undefined, productDir)).inputs;
} finally {
    // A product's tree is this run's alone; a leaked temp directory per case is what the suite had before.
    if (work) (await import("node:fs")).rmSync(work, { recursive: true, force: true });
}
process.stdout.write(JSON.stringify(out));
