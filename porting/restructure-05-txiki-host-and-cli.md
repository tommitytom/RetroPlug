# restructure-05 — Generic txiki host + TypeScript CLI

**Status:** Pending.

## Goal

Realize the txiki-hosted TypeScript CLI from the overview — a single self-contained end-user `retroplug` executable (no Node), with all CLI logic in TypeScript over the unified packages/retroplug client. Retire the C++ `--script` path (cli/Script.hpp, cli/TestHarness.cpp, JSON event dispatch) and consolidate on a single generic "embed txiki + register native rpc service + run a TS bundle" launcher that both the plugin and CLI reuse.

## Depends on

- [Step 04](./restructure-04-unify-rpc-surface.md) (unified rpcpp service + generated client in packages/retroplug).

## Architecture introduced

**Generic txiki host:** A reusable C++ launcher (initially in-repo, moves to dpf.js at step 07) that:
- Initializes a TJSRuntime (txiki/QuickJS).
- Registers a native RPC service and establishes the in-process FFI transport.
- Loads a TS bundle (either from bytecode compiled by tjsc, or from source for dev).
- Provides environment-variable config (bundle path, sample rate, display dimensions, etc.) so the same launcher serves both the plugin (with LVGL display) and the CLI (headless).

The capability is: **"embed txiki runtime + register a native rpc service + run a TS bundle"**. Both LvglJsEngine (plugin) and TestHarness (CLI) have overlapping code (TJSRuntime init, bundle loading, bridge registration). Factor that overlap into a shared host.

**TS CLI (packages/cli):** A TypeScript app consuming packages/retroplug that maps today's C++ CliArgs (screenshotDir, finalScreenshot, perSystemWav, eventLogDir, saveRplgPath, saveSavPath) and Script/ScriptEvent JSON events to TypeScript commands + flags. Pick a tiny txiki-compatible argument parser (e.g., [minimist](https://www.npmjs.com/package/minimist) or write a bespoke one that works in txiki's stdlib — **not** a Node-only parser like yargs). The TS CLI orchestrates rendering, event dispatch, audio/screenshot capture using the unified retroplug client (not native trampolines).

**Bundling:** Esbuild bundles packages/cli TS → JavaScript; tjsc compiles the bundle to bytecode (`ui_bundle` + `ui_bundle_size` extern pattern, same as the plugin). A dev path (via `RETROPLUG_CLI_BUNDLE_PATH` env var, mirrors `LVGL_PLUGIN_BUNDLE_PATH`) loads source from disk for iteration.

**Feature parity checklist:**
- render-to-wav (out, duration, sample_rate, block_size — all scripted in TS)
- screenshot dump (at_ms, per-system, final; dir resolution logic in TS)
- per-system WAV capture (mirrors main.cpp lines 406–428; compile-time opt per-system loop)
- event-log capture (MIDI + serial-out logs for test verification)
- save .rplg snapshot (emu.saveRplg in TS, not C++ --save-rplg)
- save .sav cartridge state (emu.saveSramBytes in TS)
- multi-system + link groups (orchestrated in TS via unified client)
- LSDj kit-patch compilation (via emu.compileKit in unified client)
- MIDI routing modes (routed through project.dispatchMidi, same as --script)
- host transport simulation (set_bpm, set_transport events → AudioBlockInfo)

**Retirement:** After parity is confirmed, delete cli/Script.hpp, cli/TestHarness.cpp, and the `--script` dispatch branch from main.cpp (lines 203–212). The CLI becomes a single executable that runs TS bundles; --test is retired because tests now use `make -C build cli-ts-test` (the unified client, not the old C++ emu harness).

## Tasks

1. **Factor the generic txiki host** — Extract the common initialization logic from LvglJsEngine and TestHarness into a shared library or a header-only template (e.g., `TjikHostRuntime`) that provides:
   - TJSRuntime setup (memory limit, event loop binding, libuv integration).
   - In-process RPC bridge registration (same pattern as PluginJsBridge, injected by the caller).
   - Bundle loading: bytecode (evalModuleBytecode) or source (evalModuleBuffer) based on an env var.
   - A tick/pump loop for the event loop (used by the CLI; the plugin's LVGL tick handles this).
   - Lifecycle: init → load bundle → run → shutdown.

2. **Build packages/cli as a TS app** — Create a new TypeScript entry point (packages/cli/index.ts or main.ts) that:
   - Imports the unified retroplug client (packages/retroplug, which has loadRom, onProcess, project methods, emu.saveRplg, etc.).
   - Parses CLI args (import or write a txiki-compatible parser; avoid Node-only deps). Map to a command object (e.g., `{ command: 'render', rom: '...', outWav: '...', duration: ... }`).
   - Implements the render loop: Project setup → AudioBlockInfo per block → event drain + onProcess → audio write + screenshot/log capture.
   - Matches the parity checklist above (per-system wav, MIDI logs, event scheduling, kit patching, transport state).
   - Throws or exits with meaningful error codes (missing ROM → 2, JSON parse error → 1, success → 0).

3. **Bundle and bytecode the CLI** — Wire the esbuild + tjsc pipeline:
   - Add a `packages/cli:bundle` script (esbuild) in pnpm workspace.
   - Wire it into the CMake build (`ui-regenerate` DAG or a new `cli-regenerate` target).
   - Embed the bytecode into the retroplug-cli binary (same pattern as PluginUI.cpp: extern ui_bundle, ui_bundle_size).
   - Support dev mode: if `RETROPLUG_CLI_BUNDLE_PATH` is set, load source from that directory instead of bytecode (parallel to LVGL_PLUGIN_BUNDLE_PATH).

4. **Reach parity and verify** — Update test/ts harness fixtures (mgb.test.ts, lsdj/sync_pattern.test.ts, etc.) to use `emu.saveRplg` instead of C++ --save-rplg if needed; ensure all existing test invocations (`make -C build cli-ts-test`) still pass. Run the reaper-author targets (reaper-mgb-author, etc.) to regenerate .rplg fixtures and confirm retroplug-cli --test still works.

5. **Delete the --script path** — Remove cli/Script.hpp, cli/TestHarness.cpp. Remove the `--test` dispatch from main.cpp (lines 203–212). The CLI binary is now purely a launcher; the TS code is the real logic. Update cli/CMakeLists.txt: remove TestHarness.cpp, remove tjs linkage if the new launcher doesn't embed a runtime (or keep it if we want a headless txiki host for other tools).

## Verification

- **Headless render:** `retroplug render foo.rplg -o out.wav --duration 10000` works end-to-end (or the equivalent args your TS parser accepts).
- **Tests green:** `make -C build cli-ts-test` (TAP output; now invokes TS CLI bundle + unified retroplug client, no C++ emu harness).
- **Feature parity:** All test fixtures still regenerate via reaper-author targets and matches the old CLI output (WAV content, event logs, screenshots).
- **Plugin still works:** `make -C build screenshot` still renders the plugin UI to PNG (the plugin's LvglJsEngine path is unchanged).
- **Per-system WAV:** A multi-system SameBoy script with `--per-system-wav` produces N+1 WAVs (mix + per-sys).
- **Event logs:** A kit-patch script with `--event-logs DIR` produces MIDI + serial logs matching the old format.

## Risks / open questions

- **txiki stdlib coverage:** File I/O, stdout, exit codes, and argument parsing must work in txiki's runtime (not Node). Confirm that:
  - `fs.writeFileSync` / `fs.readFileSync` work (miniaudio + lodepng require file reads; WAV writing needs writes).
  - `process.exit(code)` works.
  - `console.log` routes to stderr (for diagnostic output like the old `[screenshot]` lines).
  - A parser (minimist-style or custom) parses argv correctly (no unicode edge cases, no shell-expansion surprises).
  - `path` module or equivalent resolves directory logic (screenshot dir, WAV stem extraction).
- **Per-system WAV for Mesen:** Today the C++ CLI only supports `--per-system-wav` with SameBoy (main.cpp ~542–546). TS CLI should document this limitation or extend it to Mesen if the unified client exposes per-system step APIs. No silent zeros.
- **runMsPerSystem coverage:** The old --per-system-wav uses a dual-loop (SameBoy stepIfBelowTarget + finishBlock; main.cpp ~550–567). Ensure the TS CLI replicates this exactly or refactors it into the unified client (e.g., a `runMsPerSystem` method on Project).
- **Flag day:** Keeping retroplug-cli buildable until parity is critical — if an intermediate commit breaks the old --script path, tests fail and it blocks CI. Consider keeping both paths (--test → new txiki host, --script → old path) during dev, then delete the old path in a final cleanup commit.
- **Bytecode size:** tjsc bytecode is typically 30–50 % of source size, but confirm the final retroplug-cli binary size is reasonable (should be smaller than today because TestHarness.cpp.o is gone).
