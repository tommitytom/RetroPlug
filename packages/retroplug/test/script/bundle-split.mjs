#!/usr/bin/env node
// The driver behind test/script/bundle-ui-split.test.ts: bundles each surface's entry the way the build does and prints
// one JSON line - per bundle, the absolute paths of the modules that went in. The control plane, the UI and the CLI
// dispatcher are the hosts of the shared module sets (tools/build-shared-modules.mjs), read from the closures that
// build reports; the other surfaces are not shared sets and go through tools/build-session.js, in memory. The split is
// then a fact about the metafile rather than a claim about the imports: the control plane, the render worker, the script nodes, the SDK, the CLI dispatcher and the planner carry no UI code, and
// the UI bundle carries no CLI one.
//
// The build is Node's (esbuild is not a txiki module), so the test runs this in a node child, as
// bundle-protocol.test.ts does with its own driver.
import { createRequire } from "node:module";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";

const require = createRequire(import.meta.url);
const { bundleInputs } = require("../../../../tools/build-session.js");
const { buildSharedModules } = await import("../../../../tools/build-shared-modules.mjs");

// The entries, and the format the build gives each: the render worker and the script nodes are iifes (a bare QuickJS
// runtime evals them). The script-node bundle is every Engine's, so it is a non-UI surface like the rest. The control
// plane, the UI and the dispatcher are not here: they come from the shared sets' closures.
const BUNDLES = [
    ["render-worker", "src/render/worker.ts", "iife"],
    ["script-nodes", "src/generated/scriptNodes.ts", "iife"],
    ["cli-sdk", "cli/sdk.ts", "esm"],
    ["planner", "src/graph/protocolNodes.ts", "esm"],
];

// A product's list may be named (its manifest name, as the root package.json declares it): the driver generates that
// product's tree into a temporary build directory and builds its surfaces from it as the build does: the render worker
// and the script nodes through build-session.js, the control plane and the UI as the app set
// (tools/product-generated.js redirects the generated modules). No name is the desktop.
const REPO = resolve(process.cwd(), "../..");
const productName = process.argv[2];
// The desktop's shape is exactly what it was (one key per bundle); a product's adds its generated modules' text, which
// the tests read to hold the script entry's registrations and span labels.
const out = productName ? { product: productName, generated: {} } : {};
let productDir;
let work;
if (productName) {
    const { mkdtempSync, realpathSync } = await import("node:fs");
    const { generate } = await import("../../../../packages/retroplug/scripts/lib/packages.mjs");
    // The real path: macOS's tmpdir() is /var/folders, a symlink to /private/var/folders, and the generated tree's
    // relative paths back to the repository are counted from the path given, while esbuild resolves them from the real
    // one, one `..` deeper.
    work = realpathSync(mkdtempSync(join(tmpdir(), "rp-bundles-")));
    generate({ root: REPO, product: productName, buildDir: work });
    productDir = join(work, "products", productName, "packages/retroplug");
    const { readFileSync } = await import("node:fs");
    out.generated.scriptNodes = readFileSync(join(productDir, "src/generated/scriptNodes.ts"), "utf8");
    out.generated.packages = readFileSync(join(productDir, "src/generated/packages.ts"), "utf8");
}
try {
    const entries = productName
        ? [
              ["render-worker", "src/render/worker.ts", "iife"],
              ["script-nodes", `${productDir}/src/generated/scriptNodes.ts`, "iife"],
          ]
        : BUNDLES;
    for (const [name, entry, format] of entries) {
        const built = await bundleInputs(entry, format, productDir);
        out[name] = built.inputs.map((input) => resolve(process.cwd(), input));
    }
    // The shared sets' closures are absolute paths, as the entries above are made absolute with process.cwd().
    const unused = join(tmpdir(), "rp-split-unused");
    const app = await buildSharedModules({ set: "app", outDir: unused, productDir, write: false });
    out["control-plane"] = app.closures["rp:cp"];
    out.ui = app.closures["rp:ui"];
    if (!productName) {
        const cli = await buildSharedModules({ set: "cli", outDir: unused, write: false });
        out["cli-dispatcher"] = cli.closures["rp:cli"];
    }
} finally {
    // A product's tree is this run's alone; a leaked temp directory per case is what the suite had before.
    if (work) (await import("node:fs")).rmSync(work, { recursive: true, force: true });
}
process.stdout.write(JSON.stringify(out));
