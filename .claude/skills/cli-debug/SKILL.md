---
name: cli-debug
description: >-
  Debug and fix issues that a retroplug-cli consumer (e.g. the BlipToaster NES ROM) reports against the
  tooling — a sound channel silent/wrong, MIDI/pitch/timing off, a harness test failing, missing emulator
  introspection, or an "is this a Mesen/SameBoy bug?" question. Reproduce on the CURRENT source, root-cause
  across the layers (RetroPlug native/emulator/host vs the CLI SDK vs the consumer's own ROM), fix here with
  the full source, add a regression test, then sync the rebuilt CLI back to the consumer and confirm its
  suite. Use whenever you have a consumer handoff or a CLI-tooling bug to run down from the RetroPlug side.
---

# Debugging CLI-tooling issues from the RetroPlug side

You are in the **RetroPlug** repo — the emulator cores (Mesen NES, SameBoy GB), the native host, the
plugin, the DSP, and the `retroplug-cli` test harness + SDK. Downstream **consumers** (e.g. the BlipToaster
NES ROM, a sibling checkout — the sync default is `../bliptoaster`) drive ROMs through the CLI and report
issues, often as handoff docs (`<consumer>/RETROPLUG-CLI-HANDOFF*.md`). Your advantage over the consumer:
you have the full source. Use it.

Read first: [AGENTS.md](../../../AGENTS.md) (repo guide), [spec/](../../../spec/README.md) (architecture),
and [spec/06-build-test.md](../../../spec/06-build-test.md) (the headless verification loop).

## Which layer owns the bug

1. **RetroPlug native / emulator / host** — the integration or the vendored cores. (The N8 MIDI FIFO Edio
   protocol was a real one here.)
2. **The CLI SDK / TS backend** — a method exists in `realBackend` but isn't in the typed SDK surface, or a
   readout is missing entirely.
3. **The consumer's own artifact (the ROM)** — report back with evidence; do **not** "fix" it here.

**Golden rule:** the vendored cores are almost always faithful. Before blaming Mesen/SameBoy, prove it with
a byte/register trace — most "emulator" reports are the integration or the consumer's ROM.

## Reproduce on CURRENT source first

Consumers run a **prebuilt, synced** `bin/retroplug-cli` that lags this repo. Always reproduce against
current source: `tools/sync-cli-to-bliptoaster.sh [dest]` (rebuilds `retroplug-cli` + the SDK and copies
`bin/`+`sdk/` into the consumer), then run the consumer's failing session/test. No repro on current source
⇒ already fixed. Still reproduces ⇒ you have a live repro to work from.

## Reproduce + observe

- Port the consumer's repro to whatever's cheapest to iterate on: a `retroplug-cli` session, a `render`, a
  `test-native` test, or a C++ Catch2 unit test.
- **Instrument the native side.** Env-gated `fprintf(stderr, …)` traces are the workhorse — grep for
  existing ones (e.g. `RP_FIFO_TRACE`). For a one-off, add a temporary trace to the relevant Mesen/host
  source and **revert it after** (`deps/mesen` is vendored/tracked; `deps/sameboy` is patched-at-configure,
  so its dirty tree is EXPECTED — never reset/commit it).
- **Use the debug RPCs** from a session to see decoded state: `getApuState` / `getExpansionAudioState` /
  `drainEvents` (poll during render) / `readCpu` + `loadLabels` / the 6502 debugger + profiler.

## RetroPlug source map (where to look / fix)

- **N8 USB MIDI FIFO** (host MIDI → ROM at `$40F0/$40F1`, Edio protocol):
  `packages/native/src/system/mesen/NesEverdriveFifo.hpp` + `roles/NesN8MidiRole.{hpp,cpp}`, driven from
  `MesenNesSystem.cpp` (`stepIfBelowTarget` / `finishBlock`).
- **Emulated NES expansion audio**: `deps/mesen/Core/NES/Mappers/Audio/`
  (`Vrc6Audio.h`, `Vrc7Audio.h`→`emu2413`, `Sunsoft5bAudio.h`, `Namco163Audio.h`) + the mappers under
  `deps/mesen/Core/NES/Mappers/{Konami,Sunsoft,Namco}/`.
- **Debug reads / RPCs**: `packages/native/src/system/DebugTarget.hpp` (`rp::` state structs + virtuals),
  `.../mesen/MesenNesDebugSession.{hpp,cpp}` (reads the live core),
  `.../host/rpc/DebugRpcService.{hpp,cpp}` + `.../host/rpc/BackendRpcRegistration.hpp` (registration).
- **TS backend + CLI SDK**: `packages/retroplug/src/{backend.ts,realBackend.ts}`,
  `packages/retroplug/testing/mockBackend.ts`, `packages/retroplug/cli/{sdk-types.d.ts,sdk.ts}`.
- **Cores**: `deps/mesen` (NES, vendored — edit directly), `deps/sameboy` (GB, tracked patch — see AGENTS.md).

## Fix + verify (an exit-zero, not a claim)

- **Build**: `node scripts/cmake-build.js <target>` (e.g. `retroplug-cli`, `retroplug-host`,
  `retroplug-audio-test`) or `./build.sh`. Single Catch2 target: `cmake --build build --target <t> -j$(nproc)`.
- **Test** (spec/06-build-test.md): `pnpm test` (mock), `test:native` (real host+cores), `test:ui`,
  `test:plugin` (Catch2); `node packages/retroplug/scripts/run-native-tests.mjs [slug]`;
  the sanitizers (`tools/run-sanitizer.sh`). Back a "fixed" claim with a real exit-zero.
- **Regression test**: add a Catch2 unit (`packages/native/test/...`, wired into its target's CMakeLists)
  and/or a `test-native` (`packages/retroplug/test-native/*.test.ts`, auto-discovered).
- **Sync back + confirm the consumer**: `tools/sync-cli-to-bliptoaster.sh`, then run the consumer's suite
  (`cd ../<consumer>/retroplug-cli && npm test`). Backward compat matters — the consumer's existing tests
  must stay green against the rebuilt binary.
- **Clean up**: revert temporary instrumentation; keep an env-gated trace only if broadly useful.

## Recipes

**Add a debug RPC** (new introspection a consumer needs). Mirror `getApuState`/`getExpansionAudioState`
across every layer: an `rp::` struct + defaulted virtual in `DebugTarget.hpp` → the read in
`MesenNesDebugSession.{hpp,cpp}` → the method in `DebugRpcService.{hpp,cpp}` → register in
`BackendRpcRegistration.hpp` → TS `backend.ts` (interface + method + method-name union) + `realBackend.ts`
(a `call(...)`) + `mockBackend.ts` (a stub) → the CLI SDK `cli/{sdk-types.d.ts,sdk.ts}` → a `test-native`
guard. Field names are the JSON keys (reflect-cpp auto-serializes), so the C++ struct and TS interface must
match exactly. Build + sync.

**Expose an existing backend method in the CLI SDK.** `realBackend` implements more than
`cli/sdk-types.d.ts` declares (e.g. the debugger/profiler). To make one usable + typed by consumers, add
its decl + any types to `cli/sdk-types.d.ts` and re-export in `cli/sdk.ts` — `realBackend` already calls it.
Typecheck + sync.

**Attribute ROM vs core.** Trace the exact bytes/registers the consumer's ROM writes. Wrong bytes → the
ROM (report back with the trace + a failing consumer test; don't fix here). Right bytes, mis-decoded → the
core/integration (verify against the chip spec, then fix here).

## RetroPlug gotchas

- `deps/sameboy` dirty tree is EXPECTED (patched at configure) — never reset/stash/commit it or bump its pointer.
- `deps/mesen` is vendored/tracked — edit directly; revert any temporary instrumentation before committing.
- Don't `rm -rf build` to "fix" CMake — the configured `build/` is load-bearing.
- Never commit derived artifacts (the embedded `*bundle_data.c`).
- The consumer's synced `bin/retroplug-cli` lags this repo — always reproduce on current source.
- Don't push or open PRs without an explicit ask.
