---
name: feature-dev
description: >-
  Orient in RetroPlug's subsystems before implementing a feature that spans RetroPlug and a consumer
  (e.g. the BlipToaster ROM driven through retroplug-cli, or the plugin). Maps the feature to the subsystems
  it touches — emulator cores/systems, audio/DSP + roles, MIDI, the debug/introspection RPCs, the CLI SDK,
  persistence, assets — so you reuse what already exists, extend the right layer, and pick the right
  tooling, then build/test/sync and verify on the consumer. Use when adding or designing a feature (for a
  reported CLI/emulator bug, use cli-debug instead).
---

# Implementing a feature with RetroPlug subsystem context

You're building a feature that involves a RetroPlug **consumer** — most often the BlipToaster NES ROM driven
through the `retroplug-cli` harness (sibling checkout, sync default `../evermidi`), sometimes the plugin
or another dpf.js consumer. Such features usually span **both sides**: behavior in the consumer's own
artifact (e.g. ROM code) *plus* a RetroPlug capability that supports or observes it (a new emulator
behavior, a debug/introspection RPC, an SDK method, a DSP role, a persistence change). The value of
working from this repo is the full subsystem context — use it to reuse what exists and extend the right
layer instead of reinventing or working around.

## Orient first — don't guess the architecture

RetroPlug's design is documented per concern in `spec/`. **Read the ones your feature touches before
designing:**

- [spec/00-overview.md](../../../spec/00-overview.md) + [01-architecture.md](../../../spec/01-architecture.md) — the whole picture (native host + TS layer + cores).
- [spec/02-native-host.md](../../../spec/02-native-host.md) — the C++ host (Engine, systems, RPC).
- [spec/03-ts-layer.md](../../../spec/03-ts-layer.md) — the TS layer (stores, backend, SDK).
- [spec/04-roles-dsp-kernel.md](../../../spec/04-roles-dsp-kernel.md) — roles + the DSP kernel (per-system audio/MIDI behavior).
- [spec/05-data-persistence.md](../../../spec/05-data-persistence.md) — versioned JSON persistence, migrations, role-config.
- [spec/06-build-test.md](../../../spec/06-build-test.md) — build + the headless verify loop.
- [spec/08-profiling.md](../../../spec/08-profiling.md) / [09-cli-debugging.md](../../../spec/09-cli-debugging.md) — profiler + CLI debug surface.
- [spec/10-multichannel-audio-out.md](../../../spec/10-multichannel-audio-out.md) / [11-ui-rendering.md](../../../spec/11-ui-rendering.md) — per-channel stems + UI/background render.

Also [AGENTS.md](../../../AGENTS.md) (repo guide) and [docs/lsdj.md](../../../docs/lsdj.md) — the LSDj work
is a worked, end-to-end reference for a full console-feature subsystem (ROM assets, sample import, sav
codec, persistence, tests) and a good template to pattern-match against.

## Method

1. **Scope the feature to subsystems** — what does it touch? Map it against the list below.
2. **Decide the split** — consumer-side vs RetroPlug-side. The consumer owns its artifact and its tests;
   RetroPlug owns the emulator, host, DSP, the RPC surface, and the SDK. Name each piece and where it lives.
3. **Inventory before you build** — check what RetroPlug *already* exposes (a debug RPC, a render mode, an
   SDK method, a role, an asset path). Reuse beats reinventing; the SDK surface + debug RPCs are the
   consumer's window into the emulator.
4. **Pick the layer to extend** — see the extension map. Match the need to the layer instead of forcing it
   into the consumer.
5. **Build → test → sync → verify on the consumer.**

## Subsystem map (where things live, what they own)

- **Emulator cores + systems** — `deps/mesen` (NES), `deps/sameboy` (GB); wrapped by
  `packages/native/src/system/{mesen,sameboy}/` (`SystemBase`, per-console system + roles). New emulator
  behavior for a console feature lives here.
- **Audio / DSP** — `packages/native/src/host/{engine,dsp}/` (`Engine::processBlock`, per-system /
  per-channel mixing, `MultiOutRouter`); the DSP kernel + roles are TS
  (`packages/retroplug/src/{dspKernel,dspRoles,coreRoles}.ts`). Per-channel stems: spec/10. Offline render:
  `src/host/render` + `packages/retroplug/src/render/`.
- **MIDI + transport** — host MIDI → `Engine::stageMidi` → the system's ingress (SameBoy serial; NES N8
  FIFO role in `system/mesen/roles/`); routing in `packages/retroplug/src/midiRouting.ts`.
- **Debug / introspection** (the consumer's observability) — `packages/native/src/system/DebugTarget.hpp`
  + `system/mesen/MesenNesDebugSession` + `host/rpc/DebugRpcService` (`getApuState`,
  `getExpansionAudioState`, `drainEvents`, `readCpu`, the debugger/profiler). See spec/09.
- **RPC + SDK surface** (what a consumer/CLI can call) — `packages/native/src/host/rpc/` (scoped capability
  facets) ↔ `packages/retroplug/src/{backend,realBackend}.ts` ↔ the CLI SDK
  `packages/retroplug/cli/{sdk-types.d.ts,sdk.ts}`. This seam is what a consumer feature most often extends.
- **Roles + per-system config** — `coreRoles.ts` / `dspRoles.ts` compose per-system audio/MIDI behavior;
  role-config crosses to native (reflect-cpp `DefaultIfMissing`-tolerant). spec/04.
- **Persistence / config** — TS-owned, versioned raw-JSON; `migrate.ts` (one idempotent raw step per
  breaking bump), zod schemas, `projectConfig`/`projectStore`, the `.rplg` project file. spec/05.
  **Never embed opaque binary in `.rplg` — link assets by path** (like `romPath`/`savPath`).
- **Assets / file interop + watching** — `fileWatcher.ts` / `NativeFileWatcher`; the LSDj ROM-assets path
  (`packages/retroplug/src/lsdj/`, `lsdjAssetsRole.ts`) is the reference for injecting/overriding console
  assets in-memory at construct without rewriting the on-disk ROM.
- **Harness + build/sync** — `retroplug-cli` (`cli/session`, the SDK), `run-native-tests.mjs`, C++ Catch2
  (`pnpm test:plugin`); `build.sh` / `node scripts/cmake-build.js <target>`;
  `tools/sync-cli-to-bliptoaster.sh` copies the built CLI + SDK into the consumer. spec/06.

## Extension recipes (match the need to the layer)

- **New introspection / readout a consumer test needs** → add a debug RPC: an `rp::` struct + defaulted
  virtual in `DebugTarget.hpp` → the read in `MesenNesDebugSession.{hpp,cpp}` → the method in
  `DebugRpcService.{hpp,cpp}` → register in `BackendRpcRegistration.hpp` → TS
  `backend.ts`/`realBackend.ts`/`mockBackend.ts` → the CLI SDK `cli/{sdk-types.d.ts,sdk.ts}` → a
  `test-native` guard. reflect-cpp auto-serializes; C++ field names must equal the TS keys.
- **Expose an existing backend method to consumers** → declare it in `cli/sdk-types.d.ts` + re-export in
  `cli/sdk.ts` (`realBackend` may already implement it — the SDK surface is hand-maintained and can lag).
- **New per-system audio/MIDI behavior** → a role (`coreRoles`/`dspRoles`) + any role-config, wired through
  the DSP kernel. spec/04.
- **New emulator behavior** → the vendored core (edit `deps/mesen` directly; `deps/sameboy` via its tracked
  patch) + the wrapping system. Revert temporary instrumentation.
- **New persisted state** → the TS persistence layer + a `migrate.ts` step if the change is breaking. spec/05.
- **New render / output mode** → `packages/retroplug/src/render/` + the `RenderHost`. spec/10 / spec/11.

## Verify (both sides)

Back a "done" claim with an exit-zero, per spec/06. Build (`build.sh` / `cmake-build.js <target>`); run the
headless suites (`pnpm test` / `test:native` / `test:ui` / `test:plugin`, `run-native-tests.mjs`, the
sanitizers) and add a regression test. If the feature adds a consumer-facing capability,
`tools/sync-cli-to-bliptoaster.sh` and run the consumer's suite — its existing tests must stay green.

## Gotchas

- **NEVER use Python, under any circumstances** - no `python`/`python3`, no numpy, no venv, no inline
  `-c` snippets, for analysis or scratch work or "just a quick check". This project's tooling is
  TS/C++ behind `retroplug-cli` plus ordinary shell tools. If something seems to need Python, it needs
  a `retroplug-cli` subcommand instead: write it in `packages/retroplug/cli/sessions/*.ts`, register
  it in `cli/tools.ts` (the only registration point), and rebuild with
  `cmake --build build --target retroplug-cli -j$(nproc)`. A throwaway script proves nothing twice;
  a subcommand is reviewable, reusable and shares the repo's DSP helpers with the tests.

- `deps/sameboy`'s dirty tree is expected (patched at configure); `deps/mesen` is vendored — edit directly
  and revert instrumentation.
- The CLI SDK surface (`cli/sdk-types.d.ts`) is hand-maintained and can lag `realBackend` — add missing
  methods when a consumer needs them.
- No opaque binary in `.rplg`; link assets by path.
- A consumer runs a synced binary that lags this repo — rebuild + sync when the feature touches native/SDK.
- Don't commit derived artifacts (embedded `*bundle_data.c`); don't push or open PRs without an explicit ask.
