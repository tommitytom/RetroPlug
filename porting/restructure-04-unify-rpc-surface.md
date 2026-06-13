# restructure-04 — Unify the native↔TS surface (keystone)

**Status:** Done (2026-06-13).

## As-built (what actually landed vs. the plan below)

The plan below assumed one service with `#ifdef`-gated debug methods. Exploration
showed the harness and `PluginRpcService` are **independent thin bindings with
different execution models** (the harness is synchronous and *controls time* via
`Project::onProcess`; the plugin is command-queue/DSP-driven), already in separate
binaries. So the realization is a **separate `HarnessRpcService`** compiled only
into `retroplug-cli` — `PluginRpcService` is untouched, and schema separation is
structural, not preprocessor. What landed (staged, all 29 cli + 11 ui tests green
at each commit):

- **`cli/HarnessRpcService.{hpp}` + bodies in `TestHarness.cpp`** — a reflect-cpp
  service (~40 methods) wrapping `TestHarness::Impl`. New DTOs
  (`HarnessMidiEvent`/`HarnessSerialByte`/`HarnessFrame`/`HarnessKitSample`/
  `HarnessPerSystemAudio`); reuses `rp::CpuRegister`/`ProfiledFunction`/`DisasmLine`/
  `TraceLine`/`CallFrame`/`BreakInfo`/`BreakpointSpec`. `cli/HarnessRpcRegistration.hpp`
  + `retroplug-cli --dump-harness-schema` emit the OpenRPC schema.
- **Two reflect-cpp constraints handled:** the generic *reader* rejects
  `std::byte`, so `rfl::Bytestring` is **output-only** — binary INPUT params use
  `std::vector<std::uint8_t>`; and `std::optional` in param position mangles badly,
  so params use sentinels (empty/0 = absent).
- **`cli-regenerate`** CMake target → `build/generated/HarnessService.ts`
  (gitignored), separate from `ui-regenerate` (no LVGL dependency). `gen-rpc-ts.js`
  parameterized (service name + exe args) + 3 backward-compatible fixes
  (`Bytestring`→`Uint8Array`, `unsigned_char`→`number`, PascalCase bare `$ref`s).
- **`__rpcSend`** wired into the cli runner (mirrors `PluginJsBridge`):
  `HarnessRpcService` + `QueueTransport` + `TypedRpcServer` on
  `Symbol.for("retroplug").__rpcSend` → `processMessage` (sync; handler exceptions
  become `-32603` envelopes).
- **`packages/retroplug/src`** (committed): `createSyncClient` (a `Proxy` that
  encodes with `@msgpack/msgpack`, calls `__rpcSend`, decodes, returns/throws) +
  `Unpromisify` mapped type + `harnessRpcSend`. Same wire format as the UI's
  `@rpcpp/MsgpackCodec` (raw msgpack, no in-process framing).
- **`test/harness/index.ts` rebuilt** over the generated client behind a
  byte-for-byte-identical `emu` facade (zero test-file edits). Binary reshaping
  lives in the facade: `number[]` inputs, exact-buffer `Uint8Array`/`Float32Array`
  copies (msgpack BIN decodes as a *view* — `.buffer` must be exact),
  `getRegisters` vector→`Record`, `setBreakpoints` optional-field defaults.
  `__rpcSend` resolves **lazily** so the `ui-harness` (which re-exports
  `test`/`expect` from here) doesn't need the bridge.
- **Deleted:** the ~37 per-method JS trampolines + the hand-mirrored `NativeRp`
  interface. Only `__rpcSend` + native runner plumbing
  (`beginCase`/`report`/`done`/`log`) remain.
- **Scripts:** the cli-test pnpm scripts build `cli-regenerate` (deps
  `retroplug-cli`) so the client is fresh on a targeted run.

The drift between two hand-maintained surfaces is gone — one C++ service, one
generated client. Verified: full build, `cli-ts-test` (29), `ui-ts-test` (11),
`validate` (clap 18/0 + pluginval SUCCESS).

The original plan follows for reference.

## Goal

Collapse the two hand-maintained native↔TS surfaces (PluginRpcService + TestHarness) into a single auto-generated rpcpp client in `packages/retroplug`. Today PluginRpcService (56 public RPC methods registered via src/PluginRpcRegistration.hpp) serves the UI; cli/TestHarness.cpp exposes ~40 native trampolines bound to a Symbol.for('retroplug') namespace, hand-mirrored in test/harness/index.ts. They overlap heavily (loadRom, press, readMemory, drainMidi, screenshot, saveRplg, loadRplg, patchKit) and drift. Kill the mirror facade. One generated client consumed by UI, CLI, and tests — the keystone for restructure-05's txiki host extraction.

## Depends on

[restructure-01](./restructure-01-pnpm-workspace.md).

## Architecture introduced

- **Conditional RPC registration.** Split PluginRpcService methods into two registries: (a) **production methods** (loadRom, pressButton, drainMidi, screenshot, getFrame, getAudio, saveRplg, loadRplg, patchKit, setBpm, setTransport, etc.) registered on the plugin and CLI test/UI builds, and (b) **debug-only methods** (step, runUntilPc, breakpoints, watchpoints, disassemble, trace, profiling, loadLabels, savFromJson, savRoundtripDiff, readCpu) registered only in CLI/test/ui-test builds, never in the shipping plugin. The shipping plugin's OpenRPC schema will NOT contain debug methods. savFromJson and savRoundtripDiff stay debug-only (they are LSDj test fixtures, not emulator state). Mesen-NES-only features (breakpoints, profiling, disassemble, trace, callstack, readCpu, step) are gated to DebugRpcService, registered conditionally.

- **Txiki in-process RPC host.** When restructure-05 extracts the generic host launcher, the host wires `native rpc send` to call PluginRpcService synchronously in-process (exactly like the plugin does today via LvglJsEngine). The generated client in packages/retroplug uses an injectable transport; today it points to the plugin's `__rpcSend` or (in CLI/tests) the harness's in-process dispatch. The test harness must expose the same `processMessage(msgpack bytes) → msgpack bytes` synchronous hook that rpcpp uses internally.

- **One generated client in packages/retroplug.** The build DAG (native → OpenRPC schema dump → codegen → client) runs once, target is packages/retroplug/src/rpc.ts (gitignored), and consumed by `packages/ui`, `packages/cli`, and `test/harness`. Remove the hand-written test/harness/index.ts facade; replace it with a thin TAP layer over the generated client (or keep `emu` as a typed re-export for ergonomics).

- **Raw-buffer Bytestring path.** Ensure that FrameResponse.buffer, AuditionResponse.pcmF32, MemorySnapshotResponse.bytes, and CompileKitResult.compiledBytes all ride the rfl::Bytestring/msgpack-BIN codec path so they become Uint8Array on the TS side, not a number[]. No post-hoc base64 decode step in the client.

## Tasks

1. **Classify each harness method** (loadRom, press, drainMidi, screenshot, etc.) as (a) already exposed in PluginRpcService (getFrame, pressButton), (b) promote into PluginRpcService under the same or a compatible name, or (c) debug/dev-only (step, profiling, savFromJson, savRoundtripDiff, breakpoints, Mesen-NES-only disassemble/trace).

2. **Design conditional registration.** Introduce a preprocessor or CMake-driven build-mode switch (e.g., `-DRETRO_PLUGIN_BUILD` for the shipping plugin, absent for CLI/test builds). In PluginRpcRegistration.hpp, guard debug methods with `#ifndef RETRO_PLUGIN_BUILD`. Use the same switch in cli/TestHarness.cpp's binding table. Verify the shipping plugin's schema dump omits debug methods (assert via a CMake test that dumps the schema and greps for "step" — should find nothing).

3. **Wire txiki RPC dispatch.** In cli/TestHarness.cpp, expose a C function `testHarnessRpcSend(const uint8_t* request, size_t len, uint8_t** response_out, size_t* response_len_out)` that deserializes the msgpack envelope, dispatches via rpcpp, and serializes the result back — synchronous, no queue. The generated client calls this instead of the global emu table.

4. **Refactor test/harness/index.ts.** Remove the hand-written NativeRp interface and all the binding mirrors. Instead, import the generated client from packages/retroplug. Build a thin TAP test layer (beginCase, report, done functions) and re-export the client methods as `emu.*` for test ergonomics. Or, keep a simple facade layer that maps test idioms (press → pressButton, readMemory → getMemory + MemoryType enum) to the generated names; preserve the public API surface of test/harness/index.ts so existing tests don't break.

5. **Regenerate the single client.** Run `make -C build ui-regenerate` to trigger the native build, schema dump, and codegen. The output lands at packages/retroplug/src/rpc.ts (or a similar location in the package) and is gitignored.

6. **Verify no schema drift.** Build the shipping plugin (CLI_BUILD=0 / -DRETRO_PLUGIN_BUILD=1) and dump its schema; assert it does NOT contain step, disassemble, breakpoints, profiling, or savFromJson. Build the test CLI and dump its schema; assert it DOES contain those.

7. **Rewrite test/harness to use the generated client.** Update test/harness/index.ts to import from packages/retroplug, drop the hand-mirror, and test the TAP layer. Existing test files should not break (preserve the `emu.*` API and Button/Mem enums).

## Verification

- `make -C build cli-ts-test` and `make -C build ui-ts-test` pass with the generated client (tests use emu.loadRom, emu.press, etc., which now route to the auto-generated methods).
- The shipping plugin's OpenRPC schema (dumped with `-DRETRO_PLUGIN_BUILD`) does **not** list step, disassemble, breakpoints, profiling, loadLabels, readCpu, setTrace, readTrace, getCallStack, setBreakpoints, runUntilBreak, beginProfile, readProfile, savFromJson, savRoundtripDiff, readCpu, setRegister, getRegisters.
- The CLI test schema **does** list all of the above (when built without `-DRETRO_PLUGIN_BUILD`).
- `test/harness/index.ts` exports the same emu object shape and Button/Mem enums as before; tests that do `import { emu, Button } from "harness"` work unchanged.
- Frame, audio, and memory buffers returned by the client are Uint8Array, not number[]; no base64 decode step in the client.
- `tools/run-sanitizers.sh` passes (no new thread races in the harness's rpcpp dispatch loop).

## Risks / open questions

- **Schema dump timing.** The rpc-schema-dump binary must be rebuilt before codegen, adding a full native compile to the build DAG. This is already true today (CMake builds rpc-schema-dump as part of `ui-regenerate`); no new risk.
- **Debug methods as conditional exports.** If debug methods are conditionally registered, the TS client's interface must also be conditional (or union-typed for the debug methods). The codegen may need a post-pass to mark them optional or to emit a separate DebugRpcService interface. This is manageable with conditional interface merging in TS.
- **Test harness TAP layer.** The beginCase, report, done functions are test-runner plumbing, not RPC. They stay in test/harness and are not exported over RPC. Keep them synchronous and tied to the TAP state machine in cli/TestHarness.cpp.
- **Mesen-NES-only features.** Methods like step, disassemble, profiling are only available when a Mesen NES system is loaded. Tests calling them on a SameBoy system will error. Document the limitation; test fixtures are already Mesen-NES-aware.
- **Generated client gitignore.** packages/retroplug/src/rpc.ts must be in .gitignore so it is rebuilt on every clone. Ensure the build fails legibly if the codegen step is skipped.
- **No back-compat shims.** The TS client method names (pressButton vs press, getMemory vs readMemory) are load-bearing. Tests must be updated to use the generated names. Keep a thin emu facade in test/harness/index.ts to smooth the migration (emu.press = client.pressButton, etc.), or rewrite tests to use the generated names directly.
