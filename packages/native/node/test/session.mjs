// The QuickJS side of the parity test: runs the shared operation matrix on retroplug-cli's
// __rpcSend and prints the canonicalized results as one JSON line. esbuild bundles this into a
// self-contained session (see parity.test.mjs); the CLI evals it.
//
//   retroplug-cli <bundle.js> <tmpDir>

import { runOps } from "./ops.mjs";

const ns = globalThis[Symbol.for("plugin")];
const tmp = ns.args[0];

try {
    const results = runOps(ns.__rpcSend, tmp);
    console.log("__PARITY__" + JSON.stringify(results));
    tjs.exit(0);
} catch (e) {
    console.log("__PARITY_ERROR__" + (e && e.message ? e.message : String(e)));
    tjs.exit(1);
}
