# 00 — Overview

RetroPlug is a multi-instance chiptune plugin: it hosts Game Boy (SameBoy) and
NES/GBA (Mesen) emulator cores inside a DAW or as a standalone app, wires their
serial/MIDI/audio together, and drives music-tracker workflows (LSDj sync, mGB,
Arduinoboy, kit patching) on top. This `spec/` describes it — the C++ host in
[`packages/native/`](../packages/native) and the TypeScript + React/LVGL control
plane/UI in [`packages/retroplug/`](../packages/retroplug). It is written in the
present tense: it documents what exists today, and calls out what does not yet
exist in explicit "Not yet built" subsections.

> **The port is complete.** RetroPlug is a single build. The older **legacy**
> build (the twin `Plugin*.cpp` DSP/UI, `packages/ui/`, `packages/cli/`, and the
> old generated RPC client) has been deleted; this is the sole build, with the
> canonical `RetroPlug` plugin identity. The remaining feature gaps live in
> [07-remaining-work.md](07-remaining-work.md).

## The thesis: native owns bytes and cores; TypeScript owns meaning

Every design decision serves one split:

- **Native (C++)** owns the emulator **cores** and their **raw bytes** — ROM,
  SRAM, savestate, framebuffer, zip archives, the LSDj sav codec — plus the
  real-time **audio thread** and the lock-free seams between threads. Native
  makes no policy decisions; it is a role-agnostic runner of bytes.
- **TypeScript** owns **all meaning** — system identity and ids, ROM
  classification, path/sibling/suffix resolution, the project model and its
  serialization, config schemas, role/DSP behaviour, routing decisions, and the
  UI. TS drives native through one narrow **`Backend`** RPC surface over resolved
  paths, opaque byte buffers, and opaque config blobs.

Two consequences show up everywhere and are worth stating once here:

- **Cores are built bare.** Each core is constructed with
  `setSniffDefaultRoles(false)`, so no feature behaviour is baked into C++. LSDj
  sync, mGB and Arduinoboy behaviour live in a TypeScript **DSP role kernel**
  ([dspKernel.ts](../packages/retroplug/src/dspKernel.ts)) that native
  runs as bytecode. (Kit-patch is the exception: it runs on the **UI thread**,
  writing already-patched memory regions into the core — it has no DSP-kernel
  component.) See [04-roles-dsp-kernel.md](04-roles-dsp-kernel.md).
- **TS owns the system-id counter and orchestration.** Native never mints an id;
  `duplicate`/`reload` are TS orchestration over a `constructSystem`-with-state
  call plus snapshot reads, not native methods. See [03-ts-layer.md](03-ts-layer.md).

## The three hosts

Three C++ entry points compose the **same** Backend RPC surface — the capability
facets ([01-architecture.md](01-architecture.md)), each host mounting the subset it
is allowed to expose. Each binds `globalThis[Symbol.for("plugin")].__rpcSend`, and one
TypeScript adapter ([realBackend.ts](../packages/retroplug/src/realBackend.ts))
targets exactly that namespace — so the control-plane code is identical across all
three. The threading model, command ring, snapshot registry and release ring that
these hosts share are defined canonically in [01-architecture.md](01-architecture.md).

| Host | Entry point | Role |
|---|---|---|
| DPF plugin | [`PluginDSP.cpp`](../packages/native/plugin/PluginDSP.cpp) + [`PluginUI.cpp`](../packages/native/plugin/PluginUI.cpp) | The shipping plugin. DSP + UI link in one binary; the editor runs in-process on the DSP's control-plane context. |
| Native test host | [`src/main.cpp`](../packages/native/src/main.cpp) (`retroplug-host`) | Headless txiki host that evals a TS bundle over the real `Backend`. The `test:native` runner. |
| Headless UI-test host | [`test/ui/UiHarness.cpp`](../packages/native/test/ui/UiHarness.cpp) | Boots the real React UI on a software LVGL display, no audio thread. The `test:ui` runner. |

## The artifact set

The DPF plugin builds in four plugin formats plus the JACK standalone
(`dpf_add_plugin(retroplug TARGETS clap vst3 vst2 au jack)`); the two test hosts are
separate executables. Targets are verified against the configured build; see
[06-build-test.md](06-build-test.md) for how they build and how to run each.

| Artifact | CMake target | Output |
|---|---|---|
| CLAP plugin | `retroplug-clap` | `bin/retroplug.clap` |
| VST3 plugin | `retroplug-vst3` | `bin/retroplug.vst3` |
| VST2 plugin | `retroplug-vst2` | `bin/retroplug-vst2.so` |
| AU plugin (macOS only) | `retroplug-au` | `bin/retroplug.component` |
| Standalone (GUI) app | `retroplug-jack` | `bin/retroplug` |
| Native test host | `retroplug-host` | `bin/retroplug-host` |
| Headless UI-test host | `retroplug-ui-test` | `bin/retroplug-ui-test` |

**LV2** is the only format not built (its out-of-process DSP/UI split doesn't fit
RetroPlug); AU is gated by DPF to macOS. The DPF identity is name `RetroPlug`, URI
`https://retroplug.io`, unique id `RPlg`
([DistrhoPluginInfo.h](../packages/native/plugin/DistrhoPluginInfo.h)).
The plugin exposes 0 inputs / **8 outputs = four stereo pairs** (`out_1..4`), each
system routed to one pair by its audio routing.

## Package layout

| Package | Contents | Lifetime |
|---|---|---|
| [`packages/native/`](../packages/native) | The C++ host: the RPC services + their facets, `Engine`, `SystemFactory`/backends, the command/snapshot/release seams, the DSP-kernel runner, the DPF plugin, and both test hosts. | The sole native tree. |
| [`packages/retroplug/`](../packages/retroplug) | The TypeScript control plane and React/LVGL UI: the `Backend` interface, the stores (Systems/Project/Recent/UserConfig/Bindings), roles + the DSP kernel, and the UI. | The sole control-plane + UI tree. |
| [`packages/native/src/`](../packages/native/src) | The **shared core**, compiled into the `retroplug-core` static lib and linked by the host: `SystemBase`, `SameBoySystem`, `Mesen{Nes,Gba}System`, `Project`, `BlockRunner`, `LinkGroup`, the LSDj kit/sample pipeline, and the transport *primitives* (`SpscRing`, `FrameBufferTriple`, `MemorySnapshotTriple`). ROM classification and the LSDj sav codec are TS-owned. | Shared across the host + CLI + test binaries. |

The host uses `Project`'s runtime methods only (adopt/remove/swap/rebuild link
groups); the LSDj-persistence path is TS-owned. `retroplug-core` is a neutral static
lib in the native tree; `retroplug-backend` wraps it with the `Engine` + RPC surface,
and every host (plugin, `retroplug-host`, `retroplug-cli`) links that.

## Component map

```
  React / LVGL UI  (App · SystemGrid · tiles · Menu)
        │  events + __rp_* window/file-dialog hooks
        ▼
 ┌───────────────────────────────────────────────────────────────┐
 │ control plane  (main / UI thread)                             │
 │   control-plane QuickJS runtime (txiki)                       │
 │   TS stores ─► Backend (backend.ts) ─► __rpcSend              │
 └──────────────────────────────┬────────────────────────────────┘
                                │  in-process RPC (QuickJSCodec)
                                ▼
 ┌───────────────────────────────────────────────────────────────┐
 │ Backend RPC facets  (C++ services, per RPC server)            │
 │   HostRpcService · EngineRpcService · AudioDriverRpcService · DebugRpcService │
 └──────┬───────────────────────────────────────────▲────────────┘
        │ command ring (QueuedInvoker,               │ SnapshotRegistry (reads)
        │ control → audio, one mutation path)        │ + release ring (ownership handback)
        ▼                                            │
 ┌───────────────────────────────────────────────────────────────┐
 │ audio thread  (DPF run() / spawned audioThread_)              │
 │   Engine ─► Project of cores  (SameBoy · MesenNes · MesenGba) │
 │   DSP role kernel  (bare QuickJS, no txiki, dspKernel.ts)     │
 └───────────────────────────────────────────────────────────────┘
```

The control plane and the audio thread **never touch each other's memory
directly**: every control→audio mutation goes through the command ring, and every
audio→control observation goes through the snapshot registry (reads) plus the
release ring (ownership handback). Native runs **two QuickJS runtimes** — the
control-plane one (txiki, full host) and the bare DSP-kernel one (byte-only, on
the audio thread) — never shared. All of this is specified in
[01-architecture.md](01-architecture.md).

## Where to go next

| Doc | Scope |
|---|---|
| [01-architecture.md](01-architecture.md) | The canonical runtime architecture: three hosts over one Backend RPC surface (the capability facets); control plane vs audio thread; the command ring, snapshot registry, release ring; the two QuickJS runtimes; threading/ownership invariants. |
| [02-native-host.md](02-native-host.md) | `packages/native/`: the full RPC surface, `Engine`/`EngineInvoker`, `SnapshotRegistry` internals, `SystemFactory` + backends, the DSP runtime and script compiler. |
| [03-ts-layer.md](03-ts-layer.md) | `packages/retroplug/`: the `Backend` interface, the stores, the control-plane composition, the `__rp_*` UI↔native hooks, and the React/LVGL UI. |
| [04-roles-dsp-kernel.md](04-roles-dsp-kernel.md) | The role model: system-role vs feature-role, the role registry, and the DSP kernel's per-system pipelines and byte-sink ABI. |
| [05-data-persistence.md](05-data-persistence.md) | The project model + `.rplg`, DPF get/setState + autoload, config models, forward-tolerant reads + version stamps, the LSDj sav codec, SRAM auto-save. |
| [06-build-test.md](06-build-test.md) | How the project builds, the pnpm scripts, the dpf.js seam, and the headless verification loop. |
| [07-remaining-work.md](07-remaining-work.md) | The remaining feature gaps now the port is complete: the file-watcher half, the raw LSDj Keyboard mode, NES per-mapper expansion sub-channels, the kit-patch UI rework, and the deferred items. |

## Key files

- [`packages/native/src/host/rpc/BackendRpcRegistration.hpp`](../packages/native/src/host/rpc/BackendRpcRegistration.hpp) — where the RPC facets are mounted onto a server; the C++ surface the three hosts share.
- [`packages/retroplug/src/backend.ts`](../packages/retroplug/src/backend.ts) — the TypeScript side of that contract.
- [`packages/retroplug/src/realBackend.ts`](../packages/retroplug/src/realBackend.ts) — the adapter that binds `Symbol.for("plugin").__rpcSend`.
- [`packages/native/src/host/engine/Engine.hpp`](../packages/native/src/host/engine/Engine.hpp) — the single-threaded owner of the live `Project` of cores.
- [`packages/retroplug/src/dspKernel.ts`](../packages/retroplug/src/dspKernel.ts) — the TS DSP role kernel run as bytecode on the audio thread.
