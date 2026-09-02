// The QuickJS side of the emulator parity test: boots the real control plane on retroplug-cli and
// runs the shared matrix. esbuild bundles this (inlining the same built SDK the Node side imports)
// into a self-contained session; the CLI evals it.
//
//   retroplug-cli <bundle.js> <nesRom>

import * as sdk from "../../../../build/cli-sdk/retroplug-cli.js";
import { runEmu } from "./emu-ops.mjs";

const ns = globalThis[Symbol.for("plugin")];
const nesRom = ns.args[0];

try {
    const results = runEmu(sdk, nesRom);
    console.log("__EMU_PARITY__" + JSON.stringify(results));
    tjs.exit(0);
} catch (e) {
    console.log("__EMU_PARITY_ERROR__" + (e && e.message ? e.message : String(e)));
    tjs.exit(1);
}
