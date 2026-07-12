# 00 — Overview

RetroPlug2 is a multi-instance chiptune plugin: it hosts Game Boy (SameBoy) and
NES/GBA (Mesen) emulator cores inside a DAW or as a standalone app, wires their
serial/MIDI/audio together, and drives music-tracker workflows (LSDj sync, mGB,
Arduinoboy, kit patching) on top. This `spec/` describes the
build — the active reimplementation in [`packages/native/`](../packages/native)
(C++ host) and [`packages/retroplug/`](../packages/retroplug)
(TypeScript + React/LVGL UI). It is written in the present tense: it documents
what exists today, and calls out what does not yet exist in explicit
"Not yet built" subsections.

> **Legacy is being removed.** An older build still ships alongside this one
> ([`packages/native/src/Plugin*.cpp`](../packages/native/src) + [`packages/ui/`](../packages/ui)
> + [`packages/retroplug/`](../packages/retroplug) + [`packages/cli/`](../packages/cli)),
> only because this build has not yet reached full parity. It is on its way out.
> This `spec/` never describes legacy internals as current; the switchover and the
> remaining feature gap live in [07-migration.md](07-migration.md).

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

Three C++ entry points compose the **same** `BackendFacade` over the **same** RPC
surface. Each binds `globalThis[Symbol.for("plugin")].__rpcSend`, and one
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

The DPF plugin builds in three formats; the two test hosts are separate
executables. Targets are verified against the configured build; see
[06-build-test.md](06-build-test.md) for how they build and how to run each.

| Artifact | CMake target | Output |
|---|---|---|
| CLAP plugin | `retroplug-clap` | `bin/retroplug.clap` |
| VST3 plugin | `retroplug-vst3` | `bin/retroplug.vst3` |
| Standalone (GUI) app | `retroplug-jack` | `bin/retroplug` |
| Native test host | `retroplug-host` | `bin/retroplug-host` |
| Headless UI-test host | `retroplug-ui-test` | `bin/retroplug-ui-test` |

The build declares **no** LV2 / VST2 / AU (unlike legacy). Its DPF identity is
name `RetroPlug`, URI `urn:distrho:retroplug`, unique id `RPlg`
([DistrhoPluginInfo.h](../packages/native/plugin/DistrhoPluginInfo.h)).
The plugin exposes 0 inputs / **8 outputs = four stereo pairs** (`out_1..4`), each
system routed to one pair by its audio routing.

## Package layout

| Package | Contents | Lifetime |
|---|---|---|
| [`packages/native/`](../packages/native) | The C++ host: `BackendFacade` + the RPC services, `Engine`, `SystemFactory`/backends, the command/snapshot/release seams, the DSP-kernel runner, the DPF plugin, and both test hosts. | The sole native tree. |
| [`packages/retroplug/`](../packages/retroplug) | The TypeScript control plane and React/LVGL UI: the `Backend` interface, the stores (Systems/Project/Recent/UserConfig/Bindings), roles + the DSP kernel, and the UI. | The sole control-plane + UI tree. |
| [`packages/native/src/`](../packages/native/src) | The **shared core**, compiled once into the `retroplug-cli-core` static lib and linked by the host: `SystemBase`, `SameBoySystem`, `Mesen{Nes,Gba}System`, `Project`, `BlockRunner`, `LinkGroup`, `RomSniffer`, the LSDj sav codec + model, and the transport *primitives* (`SpscRing`, `FrameBufferTriple`, `MemorySnapshotTriple`). | **Permanent** — survives the legacy deletion. |

The host uses `Project`'s runtime methods only (adopt/remove/swap/rebuild link
groups); it never calls its legacy persistence methods. The shared core is still
housed in a target named `retroplug-cli-core` that also compiles a CLI-only RPC
surface the host does not use — a naming wart flagged for re-homing in
[07-migration.md](07-migration.md).

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
 │ BackendFacade  (C++, one per RPC server)                      │
 │   HostRpcService · EngineRpcService · AudioDriverRpcService   │
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
| [01-architecture.md](01-architecture.md) | The canonical runtime architecture: three hosts over one `BackendFacade`; control plane vs audio thread; the command ring, snapshot registry, release ring; the two QuickJS runtimes; threading/ownership invariants. |
| [02-native-host.md](02-native-host.md) | `packages/native/`: the full RPC surface, `Engine`/`EngineInvoker`, `SnapshotRegistry` internals, `SystemFactory` + backends, the DSP runtime and script compiler. |
| [03-ts-layer.md](03-ts-layer.md) | `packages/retroplug/`: the `Backend` interface, the stores, the control-plane composition, the `__rp_*` UI↔native hooks, and the React/LVGL UI. |
| [04-roles-dsp-kernel.md](04-roles-dsp-kernel.md) | The role model: system-role vs feature-role, the role registry, and the DSP kernel's per-system pipelines and byte-sink ABI. |
| [05-data-persistence.md](05-data-persistence.md) | The project model + `.rplg`, DPF get/setState + autoload, config models, forward-tolerant reads + version stamps, the LSDj sav codec, SRAM auto-save. |
| [06-build-test.md](06-build-test.md) | How the project builds, the pnpm scripts, the dpf.js seam, and the headless verification loop. |
| [07-migration.md](07-migration.md) | The switchover to a single build: the feature gap, the shared-vs-legacy C++ map, the rename/delete checklist, and deletion risks. |

## Key files

- [`packages/native/src/BackendFacade.hpp`](../packages/native/src/BackendFacade.hpp) — the C++ RPC facade the three hosts share.
- [`packages/retroplug/src/backend.ts`](../packages/retroplug/src/backend.ts) — the TypeScript side of that contract.
- [`packages/retroplug/src/realBackend.ts`](../packages/retroplug/src/realBackend.ts) — the adapter that binds `Symbol.for("plugin").__rpcSend`.
- [`packages/native/src/Engine.hpp`](../packages/native/src/Engine.hpp) — the single-threaded owner of the live `Project` of cores.
- [`packages/retroplug/src/dspKernel.ts`](../packages/retroplug/src/dspKernel.ts) — the TS DSP role kernel run as bytecode on the audio thread.
