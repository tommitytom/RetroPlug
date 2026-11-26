// The SDK the CLI binary embeds, which a session file imports as `retroplug-cli` (cli/main.cpp registers it under that
// name): the launch packs' app, consoles, protocol, kinds and placer halves commit into this context's registries
// first (a top-level await, so an importer's body waits for it), and then the authoring surface is cli/sdk.ts's. A
// session therefore sees the packs the dispatcher and the app see (docs/downloadable-packs-cli-plan.md). Built as the
// cli shared set's second host (tools/build-shared-modules.mjs), so its contract modules are the ones a pack's halves
// link to. The Node addon's SDK (tools/build-cli-sdk.mjs) bundles cli/sdk.ts alone and loads no pack.
import { loadAppPacks } from "../src/packs/loadPacks";

await loadAppPacks();

export * from "./sdk";
