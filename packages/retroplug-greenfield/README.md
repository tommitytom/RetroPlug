# @retroplug/greenfield

A **TS-first, test-driven** reimplementation of RetroPlug's orchestration layer —
the parts of the app that are policy over plain data and the filesystem, not
realtime DSP. Built clean-room here, proven with unit tests, and only wired to
the real plugin at the end.

## Why

Most of what lives in `PluginRpcService` and `config/*` today is native only
because of a data-locality accident (the config was a C++ struct, JS was
window-gated), not because it needs C++. This package rebuilds that logic in
TypeScript, looking at the app **through a TS lens with a C++ backend for the
parts that actually need it** (emulator cores, realtime queues, byte codecs, OS
paths/dialogs).

## The shape

- **`src/`** — the application logic (project/systems/paths/recent/config/SRAM/
  kits). Pure TS, depends only on the `Backend` interface.
- **`src/backend.ts`** — the **single** native-backend contract. Everything the
  app can't do in pure TS (fs bytes, atomic writes, path canonicalize, config
  dir; later: dialogs, emulator construction, live reads) is one synchronous
  interface. Grow it only when a feature genuinely needs the OS/emulator.
- **`testing/`** — the test harness + `MockBackend` (an in-memory `Backend`).
- **`test/`** — `*.test.ts`, one behaviour per test, run against the mock.

## Running the tests

Tests run on the standalone **txiki.js** runtime (`tjs`), the same QuickJS the
plugin embeds — no Node runtime semantics, no C++/plugin build, no emulator.

```sh
# one-time: build the tjs runtime binary from the vendored txiki
cmake --build build --target tjs-cli -j$(nproc)

# run all greenfield tests (bundles each with esbuild, runs via `tjs run`)
node packages/retroplug-greenfield/scripts/run-tests.mjs
# or a subset:
node packages/retroplug-greenfield/scripts/run-tests.mjs recent
```

The runner locates `tjs` at `build/dpfjs/.../txiki/tjs` (override with
`RETROPLUG_TJS`). Output is TAP; a nonzero exit means a failure.
