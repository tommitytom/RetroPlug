# @retroplug/retroplug

RetroPlug's **control plane** and **UI**: everything above the emulator cores and the realtime
audio thread. Policy over plain data, the filesystem and the RPC seam - not DSP.

This package is the **shell**: the stores, the backend, the graph and the controller that tie the
consoles and carts together, and the UI and CLI over them. A console, a cart, a device and the TS
libraries are their own packages under `packages/`, and the app's registrations are generated from
their manifests (the map is below; the shell's boundary rule is in [AGENTS.md](../../AGENTS.md), the mechanism in
[spec/06-build-test.md](../../spec/06-build-test.md#the-packages)).

The architecture is in [spec/03-ts-layer.md](../../spec/03-ts-layer.md); this README is the map of
the directory.

## Why it is TypeScript

Most of this was native once, because the config happened to be a C++ struct and JS was
window-gated - a data-locality accident, not a requirement. It lives here now, with C++ kept for
the parts that genuinely need it: the emulator cores, the realtime queues, the OS paths and
dialogs. The payoff is that the whole application layer (project / systems / paths / recent /
config / SRAM / kits) is testable with no emulator in the loop.

## The shape

| Dir | What |
|---|---|
| `src/` | The application logic. Pure TS over the `Backend` interface; no React. |
| `src/backend.ts` | The **single** native contract - fs bytes, atomic writes, path canonicalisation, config dir, dialogs, emulator construction, live reads. Grow it only when a feature genuinely needs the OS or a core. |
| `src/script/` | The one script node still the shell's, the controller session (`nodes/`); every cart's, the NES's and the MIDI routing's are their packages' `protocol` modules. The generated bundle entry (`src/generated/scriptNodes.ts`) collects them all, so the bundle's order is the span labels', not this tree's ([spec/04](../../spec/04-roles-protocol-nodes.md)). |
| `src/graph/` | The graph TS builds for the engine: `buildGraph`, which role is placed as which node, and the controller's target. The node contract (`defineNode`) and its zod-free settings schema are `@retroplug/core`'s. |
| `ui/` | The React/LVGL UI, bundled by `tools/build-ui.js` and embedded in the plugin. |
| `cli/` | The `retroplug-cli` session SDK, its `.d.ts`, and the TS stripper the CLI embeds ([spec/09](../../spec/09-cli-debugging.md)). |
| `test/` | Mock-backend tests - `pnpm test`. No native build, no emulator. The harness itself is `@retroplug/testing`. |
| `testing/` | The shell's own test doubles - `mockBackend.ts`, the `Backend` a mock-backend test drives. The harness, the node rig and the shared helpers are `@retroplug/testing`'s. |
| `test-native/` | Real-host tests over live cores - `pnpm test:native`. |
| `test-ui/` | Real-UI tests on a headless software LVGL display - `pnpm test:ui`. |
| `scripts/` | The three TS test runners, the shared pool, and the skip-baseline ratchet. |
| `scripts/lib/packages.mjs` | The generator behind `src/generated/`: it reads the product list and every manifest and writes the app's registrations, the protocol table and the bundle's entry. |
| `src/app.ts` | The shell's own `app` contribution. It is empty: every console and cart contributes its roles, providers, integrations and kit formats from its own package, and what is left here moves with the controller in F3. |

## Where the rest lives

| Package | What |
|---|---|
| `@retroplug/core`, `@retroplug/dsp`, `@retroplug/tracker` | The TS libraries: the node contract and role contract, the DSP kernels and kit chain, the tracker layer's catalogs and neutral console helpers. |
| `@retroplug/sameboy`, `@retroplug/mesen` | The consoles' roles and their settings enums (their native halves are still `packages/native`'s until F2). |
| `@retroplug/lsdj`, `@retroplug/mgb`, `@retroplug/risa`, `@retroplug/smsggdj`, `@retroplug/bliptoaster` | The carts: their codecs, catalogs, roles, ROM providers, kit formats and protocol nodes. |
| `@retroplug/n8` | The Everdrive N8 Pro's node types, its Edio and link, and the NES host-MIDI role. |
| `@retroplug/graph-runtime`, `@retroplug/node-sdk`, `@retroplug/device-io`, `@retroplug/devtest` | The engine and node SDK, the box's device nodes, and the device check's test nodes. |
| `@retroplug/testing` | The TAP harness, the script-node rig and the shared byte/deep-equal helpers. |

A cart's or console's tests live in its own `ts/test/`, so the runner's slug for one is
`<package>/<path>`; the store, menu, graph, engine and UI tests stay in this tree.

## Running the tests

Each `pnpm test*` script builds the binary it needs first, so it is self-contained. From the repo
root:

```sh
pnpm test                 # mock backend, on the tjs runtime - fast, no emulator
pnpm test recent          # one file, or a directory prefix
pnpm test:native          # the real Backend RPC surface with live cores
pnpm test:ui              # the real React UI on a software LVGL display
```

Output is TAP; a nonzero exit means a failure. A case that cannot run calls `skip(reason)` and is
counted as a skip, not a pass - which files may skip is pinned by `scripts/skip-baseline.json`.
The full picture, including which command proves which kind of change, is in
[spec/06-build-test.md](../../spec/06-build-test.md).
