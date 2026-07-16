# 01 — Runtime architecture

This is the canonical description of how RetroPlug runs: the threads, the
seams between them, and who owns what. Every other spec doc references this one for the
command ring, the snapshot registry, the release ring, and the threading model — it defines
those concepts once, here.

The whole architecture serves one thesis: **native owns bytes and cores; TypeScript owns
meaning.** Native (C++) owns the emulator cores, their raw bytes, the real-time audio thread,
and the lock-free seams between threads. TypeScript owns all meaning — identity, classification,
project model, roles, routing, UI — and drives native through one narrow RPC surface.

---

## 1. The three hosts, one Backend RPC surface, one `__rpcSend`

There are three C++ entry points. They all compose the **same** Backend RPC surface —
a set of capability **facets** mounted onto one rpcpp server
([`BackendRpcRegistration.hpp`](../packages/native/src/host/rpc/BackendRpcRegistration.hpp)) —
and they all publish it to JavaScript at exactly one place:

```
globalThis[Symbol.for("plugin")].__rpcSend
```

That symbol is the invariant seam. On the C++ side it is bound to an rpcpp server over the
`QuickJSCodec` whose dispatch is `server.processMessage(req)->materialize(ctx)` — a synchronous,
in-process call marshalling live JS objects against the context (nothing is serialized to a
string). The server has **no primary object**: each facet is mounted cross-object over a shared
service instance, so a host mounts exactly the facets it is allowed to expose. On the TypeScript
side, `realBackend.ts` targets that one namespace. Because the bind is identical in all three
hosts, **one TS adapter serves all three** — a store or the UI cannot tell which host it is
running in.

| Host | Entry point | Audio thread | Display | Purpose |
|---|---|---|---|---|
| **DPF plugin** | [`PluginDSP`](../packages/native/plugin/PluginDSP.cpp) (DSP) + [`PluginUI`](../packages/native/plugin/PluginUI.cpp) (editor) | DPF's `run()` | LVGL editor window | The shipped `clap`/`vst3`/`vst2`/`au`/`jack` artifacts |
| **Native test host** (headless) | [`retroplug-host`](../packages/native/src/main.cpp) | spawned `audioThread_` (via `AudioDriverRpcService`) | none | Headless `test:native`; runs a TS bundle to completion. (The GUI standalone is the `jack` variant of the DPF plugin above.) |
| **Headless UI-test host** | [`UiHarness`](../packages/native/test/ui/UiHarness.cpp) | none — `advance(ms)` calls `renderAudio` inline | software LVGL display | `test:ui`; boots the real React bundle |

The bind is the same shape in each — build the RPC server, mount the facets it exposes, define
`Symbol.for("plugin")` on the global: see [`PluginDSP.cpp`](../packages/native/plugin/PluginDSP.cpp),
[`main.cpp`](../packages/native/src/main.cpp) (`registerAllBackendRpc` — the full union), and the
UI-harness copy.

### The plugin's in-process editor handoff

The DPF plugin is a special case worth calling out: **its editor runs on the same QuickJS
context as its control plane**, not a separate RPC bridge. The DSP side owns a plugin-lifetime
[`TjsHostRuntime`](../packages/native/plugin/PluginDSP.cpp#L38) with `__rpcSend`
already bound (in `bootControlPlane`). The UI side reaches it through
[`SharedDSP`](../packages/native/plugin/PluginShared.hpp) —
`getPluginInstancePointer()` → `getSharedDSP()` → `LvglJsEngine::useExternalHost()` —
and attaches its LVGL display to that context. So the React UI reaches the backend through the
existing `Symbol.for("plugin").__rpcSend`; there is no second server. Window-owning seams that
must touch the editor's window (resize, file browser) are hung as direct `__rp_*` C-functions on
that shared context rather than routed through RPC (defined in [03-ts-layer.md](03-ts-layer.md)).

### The RPC services and their facets

There is **no single facade object**. A host composes a shared `Engine` + `SystemFactory` +
the ONE `QueuedInvoker`, plus a handful of stateless service instances, and mounts each service's
methods as one or more **facets** onto the rpcpp server
([`BackendRpcRegistration.hpp`](../packages/native/src/host/rpc/BackendRpcRegistration.hpp)).
Registration is **cross-object** — `addMethod<&Service::m>(instance)` — and **the method name IS
the wire name** (rpcpp derives it), so the C++ names must match the TypeScript `Backend` surface
exactly. The full method tables live in [02-native-host.md](02-native-host.md); the services at a
glance:

| Service | Facet(s) | Concern | Thread |
|---|---|---|---|
| [`HostRpcService`](../packages/native/src/host/rpc/HostRpcService.hpp) | `host` | filesystem, config dir, zip/unzip — stateless, pure | control |
| [`EngineRpcService`](../packages/native/src/host/rpc/EngineRpcService.hpp) | `emulator` + `dsp-kernel` + `harness` | construct/remove systems, reads, kernel load, MIDI, transport, render/profile | control (pushes mutations) |
| [`DebugRpcService`](../packages/native/src/host/rpc/DebugRpcService.hpp) | `debug` | live-core inspection / stepping / breakpoints (CLI only — [09-cli-debugging.md](09-cli-debugging.md)) | control |
| [`AudioDriverRpcService`](../packages/native/src/host/rpc/AudioDriverRpcService.hpp) | `driver` | background audio thread (`startAudio`/`stopAudio`/`audioCaptured`/`drainReleased`) | control; owns `audioThread_` (**test host only**) |

Each host mounts only the facets it should expose (the plugin channel omits `debug`/`harness`/
`driver`; the CLI + test host mount the full union via `registerAllBackendRpc`). The plugin does
not use the `driver` facet — DPF owns its audio thread and drives `engine_` + `invoker_`
**directly** from C++ (`activate`/`deactivate`/`run`, [`PluginDSP.cpp`](../packages/native/plugin/PluginDSP.cpp#L134)).

---

## 2. Control plane vs audio thread

Two worlds, and they **never touch each other's memory directly**. All control→audio mutation
goes through the command ring; all audio→control observation goes through the snapshot registry
(reads) and the release ring (ownership handback).

| | **Control plane** (main / UI thread) | **Audio thread** (DPF `run()` / spawned `audioThread_`) |
|---|---|---|
| QuickJS runtime | control-plane txiki host ([`TjsHostRuntime`](../packages/native/plugin/PluginDSP.cpp#L38)) | bare DSP context ([`DspRuntime`](../packages/native/src/host/dsp/DspRuntime.hpp), owned by `Engine::dsp_`) |
| Owns | the RPC services + their facets, the TS stores, the editor | the live [`Engine`](../packages/native/src/host/engine/Engine.hpp) + its `Project` of cores + the DSP kernel, **while running** |
| Does | issues mutation *requests*; reads snapshots by id; frees released cores | drains the command ring; renders each block; publishes snapshots |
| Never | dereferences a live `SystemBase`, walks `Project`, frees an audio-owned core | allocates, frees a core, or blocks |

The `Engine` itself is **single-threaded and thread-unaware** — it holds no locks and knows
nothing about who is calling it. Thread-safety is entirely a property of the two rings and the
registry that surround it. One control-thread-written bit,
[`QueuedInvoker::audioThreadOwns_`](../packages/native/src/host/engine/EngineInvoker.hpp#L73),
decides which world currently owns the `Engine`, and therefore who drains the command ring.

### The seam map

```
                         CONTROL PLANE  (main / UI thread)
                    txiki QuickJS · RPC facets · TS stores · editor
                         │                    ▲                    ▲
          push mutation  │                    │ read by id         │ free released core
                         ▼                    │ (owned copy)       │
        ┌────────────────────────┐  ┌──────────────────────┐  ┌──────────────────────┐
        │  command ring          │  │  SnapshotRegistry    │  │  release ring        │
        │  SpscRing<DspCommand>   │  │  frame/state/SRAM    │  │  SpscRing<DspEvent>   │
        │  QueuedInvoker          │  │  (owned triple-bufs) │  │  (raw SystemBase*)    │
        └────────────────────────┘  └──────────────────────┘  └──────────────────────┘
                         │                    ▲                    ▲
             drain block │       publish end-of-block │           │ hand back displaced/removed
                         ▼                    │                    │
                         AUDIO THREAD  (DPF run() / spawned audioThread_)
                    bare DSP QuickJS · Engine · Project cores · BlockRunner
```

Three primitives, three directions:

- **command ring** — control → audio, the ONE mutation path (§3).
- **SnapshotRegistry** — audio → control, the ONE read door (§4).
- **release ring** — audio → control, ownership handback so the audio thread never frees (§5).

All three are built on the shared-core transport primitives:
[`SpscRing`](../packages/native/src/transport/SpscRing.hpp),
[`FrameBufferTriple`](../packages/native/src/transport/FrameBufferTriple.hpp), and
[`MemorySnapshotTriple`](../packages/native/src/transport/MemorySnapshotTriple.hpp).

---

## 3. The command ring — the ONE mutation path

Every control-plane edit — add/remove/replace a system, load kernel bytecode, stage MIDI, set
bpm/transport/routing, apply a config field, press a button — is packed into a POD
[`DspCommand`](../packages/native/src/host/engine/DspCommand.hpp) and pushed onto a single
`SpscRing<DspCommand, 256>` inside [`QueuedInvoker`](../packages/native/src/host/engine/EngineInvoker.hpp).

**There is no Direct/Queued fork.** An earlier design had two invoker classes (one for the
quiescent case, one for the running case); they were collapsed into this single path. The header
states it plainly: *"The ONE mutation path to the Engine — there is no Direct/Queued fork"*
([`EngineInvoker.hpp:19-27`](../packages/native/src/host/engine/EngineInvoker.hpp#L19)).

The only variable is **who drains the ring**, decided by `audioThreadOwns_`:

- **Running (audio thread owns the Engine):** the producer only pushes. The block driver drains
  the ring at the top of every block ([`drainInto`](../packages/native/src/host/engine/EngineInvoker.cpp#L106))
  and applies each command into the `Engine` before rendering.
- **Quiescent (control thread owns the Engine):** every push flushes **inline** — `maybeFlush()`
  runs `flush()` = `drainInto(engine_) + reclaimReleased()` immediately after the push
  ([`EngineInvoker.hpp:67`](../packages/native/src/host/engine/EngineInvoker.hpp#L67),
  [`.cpp:154-160`](../packages/native/src/host/engine/EngineInvoker.cpp#L154)). The same thread is
  both producer and consumer, so the SPSC invariant holds trivially, and a quiescent
  `removeSystem` deletes the core synchronously — exactly as a direct call would.

The key consequence: because the quiescent path flushes on **every** push, **the ring is empty
at the moment ownership is handed to the audio thread**. That is what makes the handoff clean —
the plugin's `activate()` / the test host's `startAudio` just set the bit
([`PluginDSP.cpp:134`](../packages/native/plugin/PluginDSP.cpp#L134),
[`AudioDriverRpcService.cpp`](../packages/native/src/host/rpc/AudioDriverRpcService.cpp)),
with no ring to flush.

### Producers and the drain

Producer methods pack a command and push (then `maybeFlush`). Lightweight commands are pure POD:
`stageMidi`, `setBpm`, `setTransport`, `setAudioRouting`, `applyConfigField`, `pressButton` —
if the ring is full they are simply dropped (a lost edge, never a leak). Heavy commands carry an
**owning heap payload**: `setSystems`/`loadKernel` box a `new`-ed buffer (deleted on drain or, if
the push fails, immediately); `adoptSystem`/`replaceSystem` push a raw owning `SystemBase*` and
`release()` the `unique_ptr` on success, or free the build and its registry slot on a full ring
([`EngineInvoker.cpp:13-102`](../packages/native/src/host/engine/EngineInvoker.cpp#L13)).

The consumer `drainInto(Engine&)` switches on `DspCommand::Kind` and applies each command. System
lifecycle is done as **alloc-free pointer swaps into the pre-reserved `Project`**: `AddSystem` →
`adoptSystem`; `ReplaceSystem`/`RemoveSystem` → swap/erase, then hand the displaced core back
through the release ring ([`.cpp:136-152`](../packages/native/src/host/engine/EngineInvoker.cpp#L136)).
The two block drivers ([the plugin's `run()`](../packages/native/plugin/PluginDSP.cpp#L152)
and [`audioLoop`](../packages/native/src/host/rpc/AudioDriverRpcService.cpp#L32)) share a shape —
`drainInto` the command ring, then `Engine::processBlock` — differing only in how transport arrives:
`run()` sets bpm/playing directly from the DAW's `TimePosition`, while `audioLoop`
receives them as drained `SetBpm`/`SetTransport` ring commands.

---

## 4. The SnapshotRegistry — the ONE read door

[`SnapshotRegistry`](../packages/native/src/host/engine/SnapshotRegistry.hpp) is how the control
plane observes a system's video frame, savestate, and SRAM **without ever touching a live core**.
It is an owned, id-keyed store of tear-free triple-buffers. The audio thread copies each live
core's already-published snapshot into a **registry-owned** buffer at the end of every block
(`publishAll`); the control plane reads those owned copies by id (`readFrame`/`readState`/
`readSram`) and never walks `Project` or dereferences a `SystemBase`. That severance is the whole
point — reads are decoupled from the DSP structure and are safe while the audio thread renders.

This closes a historical bug class where reads were guarded by an `audioRunning_` flag and went
silently dead in the running plugin; the registry is the fix — reads route through
[`Engine::readState`/`readSram`/`screenshot`/`getFrame`](../packages/native/src/host/engine/Engine.cpp#L108),
each of which "never walks Project / the live core".

**Structure.** A fixed-address `std::array<Slot, 64> slots_`. Each `Slot` holds an **atomic `id`**
(0 = free, so the block thread scans by id with no rehash), a `FrameBufferTriple`, and two
`MemorySnapshotTriple`s (savestate + SRAM), plus the SRAM slice offset and a sample accumulator.

**Threading contract** ([`SnapshotRegistry.hpp:27-34`](../packages/native/src/host/engine/SnapshotRegistry.hpp#L27)):
`claim`/`readFrame`/`readState`/`readSram`/`release` run on the **control thread**; `publishAll`
runs on **whichever thread drives the block** (audio thread while running, control thread on the
pull-path render). Buffers are allocated at `claim` (control thread, before handoff) and freed at
`release` (control thread, after the audio thread dropped the system) — **never on the audio
thread**. Two ordering rules make this race-free:

- **Publish the slot LAST.** `claim` sizes the frame buffer, seeds state+SRAM from the live
  savestate, and only then does `id.store(..., release)` — so the block thread can't match a
  half-built slot ([`SnapshotRegistry.cpp:64`](../packages/native/src/host/engine/SnapshotRegistry.cpp#L64)).
  Seeding at claim means a read *right after construct*, before any block renders, returns real
  bytes.
- **Clear the id FIRST.** `release` does `id.store(0, release)` before freeing the buffers, so a
  stray block-thread scan can never match a slot that is being torn down
  ([`SnapshotRegistry.cpp:140-147`](../packages/native/src/host/engine/SnapshotRegistry.cpp#L140)).
  `release` only runs once the system is already out of `project.systems()`, so it can't race an
  in-flight `publishAll`.

**Publish cadence.** `publishAll` copies each system's frame **every block** (cheap), and copies
its savestate+SRAM on a **coarse 0.5 s interval** (`kStateIntervalSec`, matching the core's own
snapshot cadence) via a per-slot sample accumulator ([`.cpp:68-106`](../packages/native/src/host/engine/SnapshotRegistry.cpp#L68)).
It only writes a slot whose system is still in `project.systems()` — exactly the window before
that system's release.

> The registry currently double-copies: from the core's own tear-free triple into the registry's
> owned buffer. That second copy exists only because the shared `SystemBase` cannot yet publish
> straight into the registry; collapsing it is a pending refactor
> ([`SnapshotRegistry.hpp:22-25`](../packages/native/src/host/engine/SnapshotRegistry.hpp#L22)). It is
> a documented redundancy, not a bug — see [07-remaining-work.md](07-remaining-work.md).

---

## 5. The release ring — ownership handback

The audio thread **cannot free a core** — `delete` is not real-time-safe. So when a lifecycle
command displaces or removes a system, the audio thread hands the raw `SystemBase*` back to the
control thread through a second SPSC ring, `SpscRing<DspEvent, 256> released_`
([`DspEvent`](../packages/native/src/host/engine/DspEvent.hpp) carries a single
`Kind::SystemReleased{ SystemBase* }`).

The protocol is symmetric with construction:

| Direction | Trigger | Mechanism |
|---|---|---|
| **add → adopt** | control builds a core, seeds its registry slot, pushes it | `drainInto` → `Engine::adoptSystem` swaps it into the pre-reserved `Project` |
| **remove/replace → release** | audio thread erases/swaps the core | `handBackReleased(sys.release())` → push onto the release ring |
| **drain → free** | control thread reclaims | `reclaimReleased()`: `popReleased()` → `registry_->release(id)` → `unique_ptr` deletes at scope end |

`reclaimReleased` ([`EngineInvoker.cpp:162-169`](../packages/native/src/host/engine/EngineInvoker.cpp#L162))
frees each released core's snapshot slot **before** the core is deleted, and returns the count
freed. It runs as part of the inline flush (quiescent) and is exposed on the wire as
`drainReleased` for the test host to pump while running.

**Leak, don't block.** If the release ring is full (256 undrained releases), the audio thread
**leaks the core rather than block or free in the render loop** — it logs and moves on
([`EngineInvoker.cpp:198-208`](../packages/native/src/host/engine/EngineInvoker.cpp#L198)). In
practice any host drains far more often than that. Never trading real-time safety for a free is
the deliberate invariant.

On teardown, `freePending()` ([`.cpp:178-196`](../packages/native/src/host/engine/EngineInvoker.cpp#L178))
runs once the audio thread is joined (single accessor again) and **discards** un-applied command
payloads — built-but-never-adopted cores (and their claimed slots), plus config/bytecode blobs.
This is the teardown counterpart to `drainInto`, which *applies* pending commands.

---

## 6. The two QuickJS runtimes

The host runs **two** QuickJS runtimes that are never shared:

| | **Control-plane runtime** | **DSP context** |
|---|---|---|
| Type | txiki full host ([`TjsHostRuntime`](../packages/native/plugin/PluginDSP.cpp#L38)) | bare QuickJS, no txiki ([`DspRuntime`](../packages/native/src/host/dsp/DspRuntime.hpp)) |
| Owner | the host (plugin / test host / UI harness) | `Engine::dsp_` |
| Runs | the TS control-plane bundle: stores, `Backend` adapter, `__rp_*` globals, editor | the DSP role kernel [`dspKernel.ts`](../packages/retroplug/src/dspKernel.ts), compiled to bytecode |
| Thread | control plane only | whichever thread drives the block |
| Data | full JS objects over `__rpcSend` | **bytes only** — a JSValue never crosses out |

The **DSP context** is a deliberately minimal runner. Native loads the kernel as bytecode
(`JS_ReadObject` + `JS_EvalFunction`; re-loading hot-swaps it), pushes the system structure once
via a global `setSystems(json)`, and calls a global `processBlock(input)` per block. The kernel
drives three bound C-function sink thunks, all **system-addressed** so one context serves every
system: `pushSerialIn(system, frame, byte)`, `emitMidiOut(system, frame, [bytes])`,
`pressButton(system, frame, button, down)`. Native is "a dumb, role-agnostic runner … fed only
by bytes" — it owns no role logic. In particular, **the drift-exact PPQ tick clock lives entirely
in JS** (`walkTicks`); native no longer owns any `nextTick`/`eachTick` primitive. The kernel, its
byte-sink ABI, and the role model are specified in [04-roles-dsp-kernel.md](04-roles-dsp-kernel.md).

The DSP context runs on **both** threads over its lifetime — the control thread on the pull-path
`renderAudio`, the audio thread once running — so every entry re-anchors QuickJS's stack-overflow
guard to the current thread with `JS_UpdateStackTop(rt_)` before entering JS, or a call from a
different stack throws a spurious overflow
([`DspRuntime.cpp:97-102`](../packages/native/src/host/dsp/DspRuntime.cpp#L97)).

`Engine::processBlock` wires it in: if the kernel is active it builds a `BlockInfo` at the
block-start ppq, calls `dsp_.processBlock(...)`, fans the `serialIn_`/`buttonOut_` sinks to the
addressed cores, then zeroes the outputs, runs `runBlock` (the shared
[`BlockRunner`](../packages/native/src/system/BlockRunner.hpp) + `MultiOutRouter`), and calls
`registry_.publishAll(...)` — the one place every driver funnels the block
([`Engine.cpp:74-100`](../packages/native/src/host/engine/Engine.cpp#L74)). The kernel's host MIDI-out
is drained by the plugin after the block and written to the DAW.

---

## 7. Threading / ownership invariants

The load-bearing rules. Breaking any one of them reintroduces a data race, a use-after-free, or a
lost mutation.

1. **The control plane and the audio thread never touch each other's memory directly.** All
   control→audio mutation is a `DspCommand` on the command ring; all audio→control observation is
   a registry read or a release-ring handback. Nothing else crosses.

2. **`audioThreadOwns_` is the single source of truth for who owns the `Engine`.** It is written
   only on the control thread — set true **before** the audio thread starts, cleared **after** it
   is joined. Read by the control thread (the flush decision) and the audio loop (its run
   condition). The `Engine` holds no locks; this bit is the entire concurrency contract.

3. **The command ring is empty at every ownership handoff.** The quiescent path flushes on every
   push, so activation/`startAudio` never has a backlog to drain — it just flips the bit.
   Deactivation flips the bit back, then `drainInto` applies any commands the last block left
   undrained (no lost mutation) and `reclaimReleased` frees any cores released just before stop.

4. **The audio thread never allocates and never frees.** Cores are built and seeded on the
   control thread before handoff; displaced/removed cores go back through the release ring and are
   deleted on the control thread. Lifecycle inside a block is alloc-free pointer swaps into the
   pre-reserved `Project`.

5. **Leak before you block or free in the render loop.** A full release ring leaks a core rather
   than free it on the audio thread — real-time safety is never traded for a deallocation.

6. **The SnapshotRegistry is the only read path.** Reads never walk `Project` or dereference a
   live `SystemBase`. Slot buffers are allocated at `claim` and freed at `release`, both on the
   control thread, both outside the window in which the audio thread publishes to that slot.
   `claim` publishes the slot id last; `release` clears it first.

7. **A JSValue never crosses out of the DSP context.** The DSP kernel communicates only through
   system-addressed byte sinks. Its runtime is separate from the control-plane runtime and is
   re-anchored to the calling thread on every entry.

8. **TypeScript owns identity and orchestration; native never mints or decides.** System ids are
   allocated by TS and passed into `constructSystem` — native returns "did it build", never an id
   ([`EngineRpcService.cpp:71-85`](../packages/native/src/host/rpc/EngineRpcService.cpp#L71)).
   Duplicate and reload are TS orchestration over `constructSystem`-with-state plus registry reads,
   not native methods ([`EngineRpcService.cpp:87-90`](../packages/native/src/host/rpc/EngineRpcService.cpp#L87)).
   Cores are built bare (no default roles baked into C++); feature behaviour lives in the TS kernel.

### Not yet built / deferred

- **The registry double-copy** (§4) stands until the shared `SystemBase` can publish directly into
  the registry — a pending refactor, see [07-remaining-work.md](07-remaining-work.md). (State
  snapshots are now armed for **both** cores — SameBoy in
  [`SameBoyBackend`](../packages/native/src/system/sameboy/SameBoyBackend.cpp) and Mesen in
  [`MesenBackend`](../packages/native/src/system/mesen/MesenBackend.cpp) — so `readSram`/state-based
  duplicate work on NES/GBA too.)

---

## Key files

| File | Role |
|---|---|
| [`BackendRpcRegistration.hpp`](../packages/native/src/host/rpc/BackendRpcRegistration.hpp) | mounts the capability facets onto an RPC server over the shared service instances |
| [`EngineInvoker.hpp`](../packages/native/src/host/engine/EngineInvoker.hpp) / [`.cpp`](../packages/native/src/host/engine/EngineInvoker.cpp) | `QueuedInvoker` — the command ring + release-ring drain, `audioThreadOwns_` |
| [`SnapshotRegistry.hpp`](../packages/native/src/host/engine/SnapshotRegistry.hpp) / [`.cpp`](../packages/native/src/host/engine/SnapshotRegistry.cpp) | the id-keyed read door; claim/publishAll/read/release |
| [`DspEvent.hpp`](../packages/native/src/host/engine/DspEvent.hpp) | the release-ring event (`SystemReleased`) |
| [`Engine.hpp`](../packages/native/src/host/engine/Engine.hpp) / [`.cpp`](../packages/native/src/host/engine/Engine.cpp) | the single-threaded live-project owner; `processBlock` wiring |
| [`DspRuntime.hpp`](../packages/native/src/host/dsp/DspRuntime.hpp) / [`.cpp`](../packages/native/src/host/dsp/DspRuntime.cpp) | the bare DSP QuickJS context + byte-sink thunks |
| [`PluginDSP.cpp`](../packages/native/plugin/PluginDSP.cpp) | plugin control-plane bring-up + the `run()` block driver |
| [`PluginShared.hpp`](../packages/native/plugin/PluginShared.hpp) | the in-process editor↔control-plane handoff |
| [`main.cpp`](../packages/native/src/main.cpp) | the standalone / native test host |
| [`AudioDriverRpcService.cpp`](../packages/native/src/host/rpc/AudioDriverRpcService.cpp) | the test host's background audio thread + `audioLoop` |
