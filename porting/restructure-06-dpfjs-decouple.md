# restructure-06 — Decouple dpf.js (in-repo)

**Status:** Pending.

## Goal

Introduce the framework/product seam in place — fix the 4 coupling points between the generic DPF + LVGL + txiki + rpcpp stack and RetroPlug domain objects — so the generic C++ subtree compiles with zero RetroPlug symbols, while the repo remains monolithic and builds as one. This prepares the seam for extraction (restructure-07) without affecting current build or verification workflows.

## Depends on

- [restructure-03 — CMake → package.json scripts](./restructure-03-cmake-scripts.md) (env-var prefixing will be a pnpm script configuration concern)
- [restructure-04 — Unify the native↔TS surface](./restructure-04-unify-rpc-surface.md) (the RPC service is already unified; injection points are now stable)

## Architecture introduced

**The 4 coupling points:**

1. **Env-var namespace** ([src/PluginUI.cpp:191–303](../src/PluginUI.cpp)): hardcoded `RETROPLUG_SCREENSHOT_PATH`, `RETROPLUG_SCREENSHOT_INTERVAL_MS`, `RETROPLUG_AUTOLOAD_PROJECT`, `RETROPLUG_AUTOLOAD_ROM`. Consumer must own the prefix; dpf.js reads a **configurable env-var PREFIX** (e.g. `DPFJS_` by default, consumer injects `RETROPLUG_` at build time).

2. **Parameter spec** ([src/PluginShared.hpp:54–60](../src/PluginShared.hpp)): `kPluginParameters[]` (RetroPlug = one "gain" param). Introduce a **PluginDescriptor** struct (name, URI, input/output counts, parameters list) so dpf.js accepts zero parameters and LvglJsEngine([src/LvglJsEngine.cpp:342](../src/LvglJsEngine.cpp), `registerParameter`) adapts to whatever the consumer provides.

3. **RPC service & context** ([src/PluginJsBridge.hpp:45–52](../src/PluginJsBridge.hpp), [cpp:50–59](../src/PluginJsBridge.cpp)): PluginJsBridge ctor takes raw pointers (Project, CommandQueue, EventQueue, UserConfig, RecentFiles). Introduce an abstract **service factory** interface and **context ownership** so the bridge accepts an opaque service + context blob; PluginRpcService remains domain-specific but the bridge itself is generic. The service wiring callbacks (file browser, window size, emit) are already in [PluginRpcService.hpp:164–169](../src/PluginRpcService.hpp); formalize them as a **PluginServiceContext** interface that the bridge uses, letting the consumer define it.

4. **Plugin identity & UI entry** ([src/DistrhoPluginInfo.h](../src/DistrhoPluginInfo.h) macros DISTRHO_PLUGIN_NAME, URI, NUM_INPUTS/OUTPUTS; [ui/PluginUI.tsx](../ui/PluginUI.tsx) hardcoded as bundle entry; [tools/build-ui.js:27](../tools/build-ui.js), [tools/gen-rpc-ts.js:63](../tools/gen-rpc-ts.js) hardcoded service name). DistrhoPluginInfo.h is a DPF compile-time requirement that lives at a fixed path — the **consumer owns it** and generates it from the descriptor. `PluginUI.tsx` entry path and generated service name become **configurable build inputs** to `build-ui.js` and `gen-rpc-ts.js`.

**Layout:**

Move the generic framework (DPF + LVGL + txiki + rpcpp generator) into a cleanly separated namespace:

```
src/dpfjs/  (or packages/native/dpfjs/)
  ├── LvglJsEngine.hpp/.cpp
  ├── TypedRpcServer.h / MsgpackCodec.h / QueueTransport.h
  ├── rpcpp generator (RpcSchemaDump.cpp)
  └── PluginJsBridge interface (abstract service factory)
  └── native/{bootstrap,core,components}
  └── transports/* + codecs/*

src/retroplug/  (RetroPlug-specific)
  ├── PluginUI.cpp (includes src/dpfjs/LvglJsEngine.hpp)
  ├── PluginRpcService.hpp/.cpp (a PluginJsBridge service implementation)
  ├── DistrhoPluginInfo.h (generated/scaffolded from descriptor)
  └── [existing domain files unchanged]
```

## Tasks

1. **Create PluginDescriptor struct** in `src/dpfjs/PluginDescriptor.hpp`: `{name, uri, numInputs, numOutputs, parameters: ParamSpec[]}`. Provide a default (empty parameters, DPFJS generic identity). Consumer (RetroPlug) instantiates with its own values.

2. **Introduce configurable env-var PREFIX** in `src/dpfjs/LvglJsEngine.hpp` (constructor or factory): store a string prefix (default `"DPFJS_"`); all env-var reads in PluginUI use it. At build time, inject RetroPlug's prefix via a CMake `-DPLUGIN_ENV_PREFIX="RETROPLUG_"` or a pnpm script env var that the PluginUI constructor receives.

3. **Refactor PluginJsBridge to accept abstract service**: introduce `PluginServiceContext` interface with the three callback setters (file browser, window size, emit) and one virtual destructor. Rewrite PluginJsBridge ctor to take `std::unique_ptr<PluginServiceContext>` instead of raw pointers. Retain `TypedRpcServer<PluginRpcService, MsgpackCodec>` in the dpf.js layer (it is generic over the service type via C++ templates).

4. **Move generic subtree into src/dpfjs/** (or `packages/native/dpfjs/` if workspace is live): LvglJsEngine, TypedRpcServer, native components, transports, codecs, and the rpcpp generator. Zero #includes of `src/project/`, `src/system/`, `src/lsdj/`. CMakeLists.txt for this subtree is self-contained; top-level CMake adds it as a subdirectory.

5. **Parametrize build-ui.js and gen-rpc-ts.js**: add command-line args (or env vars) for UI entry path and generated service name (`--ui-entry ../ui/PluginUI.tsx --service-name PluginService` or env defaults). build-ui.js entryPoint becomes `path.resolve(__dirname, process.env.UI_ENTRY || "../ui/PluginUI.tsx")`.

6. **Update gen-rpc-ts.js** to accept the output service name as an argument: `writeService(doc, 'ts', process.env.SERVICE_NAME || 'PluginService', outPath)`.

7. **Validate the seam with a compile-only example or stub**: create `examples/minimal/` with a trivial service (`Empty` or `Stub` struct with no methods) and a minimal DistrhoPluginInfo.h. Build only the dpf.js subtree against it (no link, just compile) to confirm no RetroPlug symbol leaks.

8. **Adjust CMakeLists.txt**: the top-level build still links PluginRpcService + RetroPlug domain into the plugin binary, but now it does so by explicitly wiring the descriptor and service into the generic framework. Verify the final binary is identical.

## Verification

- **Full build green**: `cmake --build build -j$(nproc)` passes; all test binaries link and run.
- **No symbol leaks in dpf.js subtree**: `nm build/src/dpfjs/CMakeFiles/dpfjs.dir/LvglJsEngine.cpp.o | grep -i retroplug` returns nothing. (Or: compile the example stub without any RetroPlug headers and confirm clean build.)
- **Existing tests unchanged**: `make -C build cli-ts-test`, `ui-ts-test`, `validate`, `retroplug-tests` all pass with identical output. (Step 03 will port these to pnpm scripts; for now they still live in CMake.)
- **DistrhoPluginInfo.h generation**: if applicable, a CMake custom command or pnpm script generates `src/DistrhoPluginInfo.h` from the descriptor at configure time; verify it matches the current static file (or make it a template if values become runtime inputs).
- **Round-trip RPC schema**: the rpc-schema-dump binary still produces the same OpenRPC JSON for PluginRpcService; `gen-rpc-ts.js` produces an identical `build/ui/generated/PluginService.ts`.

## Risks / open questions

- **Parameter-sync in LvglJsEngine.registerParameter** assumes params exist; an empty-parameter descriptor must not crash. The getter `getParameterCount()` ([runtime/lvgljs/index.ts:36](../runtime/lvgljs/index.ts)) and the loop in PluginUI ([src/PluginUI.cpp:235–236](../src/PluginUI.cpp)) must both handle zero gracefully. Verify with a zero-param stub build.

- **'rpc-message' event name is generic, but payload is service-specific.** The event emission path in PluginJsBridge and the client transport (ui/plugin/transport.ts) already decouple at the msgpack level; no change needed, but document it so a consumer understands the transport is service-agnostic (messages are opaque blobs).

- **DistrhoPluginInfo.h ownership.** DPF requires this header at a fixed include path. dpf.js cannot own it; RetroPlug must either generate it or check it in. If generated, it becomes a derived artifact (no git); if checked in, it must be consistent with the descriptor. This doc does not prescribe which — flag it in the PR and decide per project.

- **Back-compat shims forbidden** ([AGENTS.md](../AGENTS.md) pre-release rule). Do not add forwarding headers or deprecated aliases — old code using raw pointers will not compile, which is correct (we are fixing the seam, not preserving a broken API).

- **Circular subdir risk.** If dpf.js moves to `packages/native/dpfjs/` before workspace step 01, CMake's `add_subdirectory` from the top level must reach it via `require.resolve`. Test this path early.
