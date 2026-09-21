# @retroplug/retroplug

RetroPlug's **control plane** and **UI**: everything above the emulator cores and the realtime
audio thread. Policy over plain data, the filesystem and the RPC seam - not DSP.

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
| `src/` | The application logic. Pure TS over the `Backend` interface; no React, no DSP. |
| `src/backend.ts` | The **single** native contract - fs bytes, atomic writes, path canonicalisation, config dir, dialogs, emulator construction, live reads. Grow it only when a feature genuinely needs the OS or a core. |
| `src/dspRoles.ts` + `src/dspKernel.ts` | The per-block role kernel. Authored in TS, compiled to bytecode, executed on the audio thread ([spec/04](../../spec/04-roles-dsp-kernel.md)). |
| `ui/` | The React/LVGL UI, bundled by `tools/build-ui.js` and embedded in the plugin. |
| `cli/` | The `retroplug-cli` session SDK, its `.d.ts`, and the TS stripper the CLI embeds ([spec/09](../../spec/09-cli-debugging.md)). |
| `testing/` | The TAP harness (`test`, `expect`, `skip`) and `MockBackend`. |
| `test/` | Mock-backend tests - `pnpm test`. No native build, no emulator. |
| `test-native/` | Real-host tests over live cores - `pnpm test:native`. |
| `test-ui/` | Real-UI tests on a headless software LVGL display - `pnpm test:ui`. |
| `scripts/` | The three TS test runners, the shared pool, and the skip-baseline ratchet. |

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
