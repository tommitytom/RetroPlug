# 01 — Runtime architecture

This is the canonical description of how RetroPlug runs: the threads, the
seams between them, and who owns what. Every other spec doc references this one for the
command ring, the graph the engine runs on, the snapshot registry, the release ring, and the
threading model; it defines those concepts once, here.

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
[`TjsHostRuntime`](../packages/native/plugin/PluginDSP.cpp) with `__rpcSend`
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
**directly** from C++ (`activate`/`deactivate`/`run`, [`PluginDSP.cpp`](../packages/native/plugin/PluginDSP.cpp)).

---

## 2. Control plane vs audio thread

Two worlds, and they **never touch each other's memory directly**. All control→audio change
goes through the command ring; all audio→control observation goes through the snapshot registry
(reads) and the release ring (ownership handback).

| | **Control plane** (main / UI thread) | **Audio thread** (DPF `run()` / spawned `audioThread_`) |
|---|---|---|
| QuickJS runtime | control-plane txiki host ([`TjsHostRuntime`](../packages/native/plugin/PluginDSP.cpp)) | bare DSP context ([`DspRuntime`](../packages/native/src/host/dsp/DspRuntime.hpp), owned by `Engine::dsp_`) |
| Owns | the RPC services + their facets, the TS stores, the editor, the translator's model | the live [`Engine`](../packages/native/src/host/engine/Engine.hpp): the running plan (its system nodes, each owning its core) + the DSP kernel, **while running** |
| Does | issues mutation *requests*; plans structure into graph documents; reads snapshots by id; frees replaced plans and the cores only they held | drains the command ring, adopting plans in order; runs the plan each block; publishes snapshots |
| Never | dereferences a live `SystemBase` while audio runs; frees a core a running plan holds | builds or frees a core or a plan, or blocks |

The `Engine` itself is **single-threaded and thread-unaware**: it holds no locks and knows
nothing about who is calling it. Thread-safety is entirely a property of the two rings (both
inside the Engine's [`GraphHost`](../packages/native/src/graph/GraphHost.hpp)) and the registry
that surround it. One control-thread-written bit,
[`QueuedInvoker::audioThreadOwns_`](../packages/native/src/host/engine/EngineInvoker.hpp),
forwarded to `GraphHost::setAudioThreadOwns`, decides which world currently owns the `Engine`,
and therefore who drains the command ring.

### The seam map

```
                         CONTROL PLANE  (main / UI thread)
            txiki QuickJS · RPC facets · TS stores · editor · translator
                         │                    ▲                    ▲
   push plan / command   │                    │ read by id         │ free the replaced plan
                         ▼                    │ (owned copy)       │ (+ cores only it held)
        ┌────────────────────────┐  ┌──────────────────────┐  ┌──────────────────────┐
        │  command ring          │  │  SnapshotRegistry    │  │  release ring        │
        │  GraphHost: plan       │  │  frame/state/SRAM    │  │  GraphHost: the      │
        │  swaps + DspCommands,  │  │  (owned triple-bufs) │  │  replaced plans      │
        │  in push order         │  │                      │  │  (PlanStorage*)      │
        └────────────────────────┘  └──────────────────────┘  └──────────────────────┘
                         │                    ▲                    ▲
             drain block │                    │ publish per block  │ hand back the replaced plan
                         ▼                    │                    │
                         AUDIO THREAD  (DPF run() / spawned audioThread_)
          bare DSP QuickJS · Engine · GraphHost plan: kernel · system nodes · mixing
```

Three primitives, three directions:

- **command ring**: control → audio, the ONE mutation path; it carries plans and commands in push order (§3).
- **SnapshotRegistry** — audio → control, the ONE read door (§4).
- **release ring**: audio → control, a replaced plan handed back so the audio thread never frees (§5).

All three are built on the shared-core transport primitives:
[`SpscRing`](../packages/native/src/transport/SpscRing.hpp),
[`FrameBufferTriple`](../packages/native/src/transport/FrameBufferTriple.hpp), and
[`MemorySnapshotTriple`](../packages/native/src/transport/MemorySnapshotTriple.hpp).

---

## 3. The command ring — the ONE mutation path

Every control-plane edit goes through the one
[`QueuedInvoker`](../packages/native/src/host/engine/EngineInvoker.hpp) and lands on ONE ring: the
`SpscRing<Command, 256>` inside the Engine's [`GraphHost`](../packages/native/src/graph/GraphHost.hpp)
([modular-graph-phase-b1-design.md](../docs/modular-graph-phase-b1-design.md) section 5). The
invoker sends an edit one of two ways:

- **Structure** (add / replace / remove a system, the routing mode, a Game Boy's link group, the
  host's output shape) goes to the Engine's
  [`Translator`](../packages/native/src/host/graph/Translator.hpp) on the control thread. The
  translator updates its model (the ordered system list, the link groups, the routing mode, the host
  shape), builds a graph document and its `SystemTable`, and calls `GraphHost::setGraph`, which plans
  the document on the control thread (every layout checked before any instance is created, every new
  instance created and activated) and pushes the plan onto the ring as a plan swap.
- **Everything else** (load kernel bytecode, set the kernel's systems JSON, stage MIDI, controller
  MIDI or one system's MIDI, set bpm / transport / ppq, apply a config field, press a button, write
  RAM) is packed into a POD [`DspCommand`](../packages/native/src/host/engine/DspCommand.hpp) and
  pushed onto the same ring as an opaque host command (`GraphHost::pushHostCommand`).

At drain the ring is applied **in push order**: a plan swap adopts its plan, and a host command goes
to the Engine's handler, which applies it against the `SystemTable` of the plan current at that
point. A command pushed after a structural edit therefore sees the new systems and one pushed before
it sees the old, even when both plans are adopted in the same drain. `setGraph` waits only while a
published plan still carries a settings commit, which none of the engine's nodes produce, so it
never blocks: several plans can sit in the ring at once and adopt in order in one drain.

**There is no Direct/Queued fork.** An earlier design had two invoker classes (one for the
quiescent case, one for the running case); they were collapsed into this single path.

The only variable is **who drains the ring**, decided by `audioThreadOwns_`:

- **Running (audio thread owns the Engine):** the producer only pushes. The block driver drains
  the ring at the top of every block (`QueuedInvoker::drainInto` → `Engine::drainCommands` →
  `GraphHost::drain`) and applies each plan and command before rendering.
- **Quiescent (control thread owns the Engine):** every push flushes **inline**: the GraphHost
  drains and reclaims right after the push, so a plan is adopted and a command applied before the
  call returns. The same thread is both producer and consumer, so the SPSC invariant holds
  trivially, and a quiescent `removeSystem` frees the core synchronously (the plan it replaced is
  reclaimed in the same flush), exactly as a direct call would.

The key consequence: because the quiescent path flushes on **every** push, **the ring is empty
at the moment ownership is handed to the audio thread**. That is what makes the handoff clean:
the plugin's `activate()` / the test host's `startAudio` set the host shape (the plugin its block
size too) and then just set the bit ([`PluginDSP.cpp`](../packages/native/plugin/PluginDSP.cpp),
[`AudioDriverRpcService.cpp`](../packages/native/src/host/rpc/AudioDriverRpcService.cpp)), with no
ring to flush.

### Producers and the drain

Producer methods pack a command and push. Lightweight commands are pure POD: `stageMidi` (a message
of up to 4 bytes rides inline), `stageControllerMidi`, `setBpm`, `setTransport`, `setPpq`,
`applyConfigField`, `pressButton`; if the ring is full they are simply dropped (a lost edge, never a
leak). Heavy commands carry an **owning heap payload**: `setSystems` / `loadKernel` / `writeRam`, and
a `stageMidi` / `stageSystemMidi` longer than 4 bytes, box a `new`-ed buffer (deleted after apply or,
if the push fails, immediately).

Structural calls never ride the ring as commands. `adoptSystem` / `replaceSystem` / `removeSystem` /
`setAudioRouting` go to the translator, and the plan's swap command carries a `SyncTap` trailer
(dispatched in the same pop, right after the plan is adopted), which arms or restores the NES channel tap
against the plan just adopted
([10-multichannel-audio-out.md](10-multichannel-audio-out.md) section 4). The `LinkGroup` config field
goes both ways: the field reaches the core as a command, and the translator rewires the cable.
`setHostShape` goes to the translator alone. A change that does not plan (a refused document, or a
full ring) leaves the translator's model as it was, and a core that was handed in is freed with its
registry slot.

The handler (`Engine::onHostCommand` → `applyCommand`) switches on `DspCommand::Kind`, applies each
command and deletes its owning payload. The block drivers (the plugin's `run()`, the test host's
`audioLoop`, and the harness's `renderBlocks`) share a shape: `drainInto` the ring, then
`Engine::processBlock`. They differ only in how transport arrives: `run()` sets bpm/playing directly
from the DAW's `TimePosition` after the drain (so the host's values win over queued ones), while
`audioLoop` receives them as drained `SetBpm`/`SetTransport` commands.

---

## 4. The SnapshotRegistry — the ONE read door

[`SnapshotRegistry`](../packages/native/src/host/engine/SnapshotRegistry.hpp) is how the control
plane observes a system's video frame, savestate, and SRAM **without ever touching a live core**.
It is an owned, id-keyed store of tear-free triple-buffers. The audio thread copies each live
core's already-published snapshot into a **registry-owned** buffer at the end of every block
(`publishSystem`, for each of the running plan's systems in slot order); the control plane reads
those owned copies by id (`readFrame`/`readState`/`readSram`) and never walks the plan's systems
or dereferences a `SystemBase`. That severance is the whole
point — reads are decoupled from the DSP structure and are safe while the audio thread renders.

This closes a historical bug class where reads were guarded by an `audioRunning_` flag and went
silently dead in the running plugin; the registry is the fix: reads route through
[`Engine::readState`/`readSram`/`screenshot`/`getFrame`](../packages/native/src/host/engine/Engine.cpp),
each of which reads the registry's owned copy, never the live core.

**Structure.** A fixed-address `std::array<Slot, 64> slots_`. Each `Slot` holds an **atomic `id`**
(0 = free, so the block thread scans by id with no rehash), a `FrameBufferTriple`, and two
`MemorySnapshotTriple`s (savestate + SRAM), plus the SRAM slice offset and a sample accumulator.

**Threading contract** (the header comment of
[`SnapshotRegistry.hpp`](../packages/native/src/host/engine/SnapshotRegistry.hpp)):
`claim`/`readFrame`/`readState`/`readSram`/`release` run on the **control thread**; `publishSystem`
runs on **whichever thread drives the block** (audio thread while running, control thread on the
pull-path render). Buffers are allocated at `claim` (control thread, before handoff) and freed at
`release` (control thread, once no running plan holds the system): **never on the audio
thread**. Two ordering rules make this race-free:

- **Publish the slot LAST.** `claim` sizes the frame buffer, seeds state+SRAM from the live
  savestate, and only then does `id.store(..., release)` — so the block thread can't match a
  half-built slot.
  Seeding at claim means a read *right after construct*, before any block renders, returns real
  bytes.
- **Clear the id FIRST.** `release` does `id.store(0, release)` before freeing the buffers, so a
  stray block-thread scan can never match a slot that is being torn down. `release` runs only
  when a system node destroys its core at reclaim, once no running plan holds the system, so it
  can't race an in-flight `publishSystem`.

**Publish cadence.** `publishSystem` copies a system's frame **every block** (cheap), and copies
its savestate+SRAM on a **coarse 0.5 s interval** (`kStateIntervalSec`, matching the core's own
snapshot cadence) via a per-slot sample accumulator. `processBlock` calls it only for the systems
of the running plan's `SystemTable`, which is exactly the window before each system's release.

> The registry currently double-copies: from the core's own tear-free triple into the registry's
> owned buffer. That second copy exists only because the shared `SystemBase` cannot yet publish
> straight into the registry; collapsing it is a pending refactor
> (the header comment of [`SnapshotRegistry.hpp`](../packages/native/src/host/engine/SnapshotRegistry.hpp)). It is
> a documented redundancy, not a bug — see [07-remaining-work.md](07-remaining-work.md).

---

## 5. The release ring — ownership handback

The audio thread **cannot free a core**, or anything else a plan holds: `delete` is not
real-time-safe. Ownership therefore runs through the plan. The GraphHost holds the live node
instances, each running plan holds the instances it runs, and each **system node owns its core**
(`SystemNode<T>` in [SystemNodes.cpp](../packages/native/src/host/graph/SystemNodes.cpp)). When the
audio thread adopts a plan it hands the one it replaced back to the control thread through the
GraphHost's second SPSC ring, `SpscRing<PlanStorage*, 512> released_` (twice the command ring; the last plan a drain replaces
only once that whole drain is applied). `GraphHost::reclaim()` destroys it there, and a system node
that nothing else holds goes with it. There is no separate ring for cores.

A core's life, end to end:

| Direction | Trigger | Mechanism |
|---|---|---|
| **build → park** | control builds and activates a core (`factory.build`), claims its registry slot, hands it to the invoker | `Translator::add` / `replace` parks it under a fresh token and puts `{ "core": <token> }` in its node's settings |
| **plan → take** | `setGraph` plans the document on the control thread | the new node's first `activate` takes the core through the host service `rp.host.cores/1` and owns it from then on |
| **adopt → hand back** | the audio thread drains the plan swap | the replaced plan is pushed onto the release ring, the last one of a drain only after every command of that drain is applied |
| **reclaim → free** | the control thread reclaims | `reclaim()` deletes each released plan; a node only that plan held destroys its core: `registry.release(id)` first, then `delete` |

A token never repeats, so even a reused system id with a new core plans as a new instance. A core
no node ever took (a refused plan, or a replace whose target has gone) is freed by the translator
itself, releasing its slot the same way.

`reclaim` runs at every `setGraph`, in every inline flush (quiescent), and at the hosts' stop and
deactivate points; it returns the number of plans freed and is exposed on the wire as
`drainReleased` (`QueuedInvoker::reclaimReleased`) for the test host to pump while running. A
removed core is freed at the first of those after the audio thread has moved past it.

**Leak, don't block.** If the release ring is full (512 unreclaimed plans; `setGraph` reclaims before every publish, so a drain cannot outrun it), the audio thread
**leaks the plan rather than block or free in the render loop**: it only counts the leak (it must
not log), and the next `reclaim()` reports it. In practice any host reclaims far more often than
that. Never trading real-time safety for a free is the deliberate invariant.

On teardown, `freePending()` (`GraphHost::discardPending`) runs once the audio thread is joined
(single accessor again): it adopts every queued plan and hands every queued command back
**discarded**, freeing its payload (config/bytecode blobs, a RAM write's bytes, a long MIDI message)
without applying it. This is the teardown counterpart to `drainInto`, which *applies* pending
commands.

---

## 6. The two QuickJS runtimes

The host runs **two** QuickJS runtimes that are never shared:

| | **Control-plane runtime** | **DSP context** |
|---|---|---|
| Type | txiki full host ([`TjsHostRuntime`](../packages/native/plugin/PluginDSP.cpp)) | bare QuickJS, no txiki ([`DspRuntime`](../packages/native/src/host/dsp/DspRuntime.hpp)) |
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

`Engine::processBlock` wires it in as the plan's first step. The `rp.dsp.kernel` node
([KernelNode.cpp](../packages/native/src/host/graph/KernelNode.cpp)) runs the Engine's kernel
stage: if a kernel is loaded it builds a `BlockInfo` at the block-start ppq, calls
`dsp_.processBlock(...)`, and fans the kernel's sinks (`serialIn_`, `coreMidi_`, `coreBytes_`,
`buttonOut_`) to the addressed cores by id through the running plan's `SystemTable`. The plan then
runs the system nodes, the mixing nodes and `rp.host.audio-out`, and `processBlock` finishes with
the MI.OUT collection and `registry_.publishSystem(...)` for each system: the one place every
driver funnels the block ([`Engine.cpp`](../packages/native/src/host/engine/Engine.cpp); the full
order is in [02-native-host.md](02-native-host.md)). The kernel's host MIDI-out is drained by the
plugin after the block and written to the DAW.

---

## 7. Threading / ownership invariants

The load-bearing rules. Breaking any one of them reintroduces a data race, a use-after-free, or a
lost mutation.

1. **The control plane and the audio thread never touch each other's memory directly.** All
   control→audio change is a plan or a `DspCommand` on the one command ring, applied in push order;
   a command is applied against the plan current at its position in the ring. All audio→control
   observation is a registry read or a release-ring handback. Nothing else crosses.

2. **`audioThreadOwns_` is the single source of truth for who owns the `Engine`.** It is written
   only on the control thread: set true **before** the audio thread starts, cleared **after** it
   is joined. The invoker forwards it to the GraphHost, which reads it for the flush decision. The
   test host's audio loop runs on its own `running_` flag, and the bit is cleared only once that
   thread has joined, because clearing it hands the GraphHost back and applies whatever is queued.
   The `Engine` holds no locks; this bit is the entire concurrency contract.

3. **The command ring is empty at every ownership handoff.** The quiescent path flushes on every
   push, so activation/`startAudio` never has a backlog to drain; it sets the host shape and flips
   the bit. Deactivation flips the bit back, which flushes at once: whatever the last block left
   undrained is applied (no lost mutation), and the plans replaced just before stop are reclaimed
   with the cores only they held. The hosts' `drainInto` + `reclaimReleased` after it find nothing
   left.

4. **The audio thread never builds or frees a core or a plan.** Cores are built, activated and
   seeded on the control thread before handoff, and every node of a plan is created and activated
   there by the planner. A replaced plan goes back through the release ring and is destroyed, with
   any core only it held, on the control thread. Adopting a plan inside a block is a pointer swap.

5. **Leak before you block or free in the render loop.** A full release ring leaks a plan (and the
   cores only it held) rather than free it on the audio thread: real-time safety is never traded for
   a deallocation.

6. **The SnapshotRegistry is the only read path.** Reads never walk the plan's systems or
   dereference a live `SystemBase`. Slot buffers are allocated at `claim` and freed at `release`,
   both on the control thread, both outside the window in which the audio thread publishes to that
   slot. `claim` publishes the slot id last; `release` clears it first.

7. **A JSValue never crosses out of the DSP context.** The DSP kernel communicates only through
   system-addressed byte sinks. Its runtime is separate from the control-plane runtime and is
   re-anchored to the calling thread on every entry.

8. **TypeScript owns identity and orchestration; native never mints or decides.** System ids are
   allocated by TS and passed into `constructSystem`; native returns "did it build", never an id
   ([`EngineRpcService.cpp`](../packages/native/src/host/rpc/EngineRpcService.cpp)).
   Duplicate and reload are TS orchestration over `constructSystem`-with-state plus registry reads,
   not native methods.
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
| [`EngineInvoker.hpp`](../packages/native/src/host/engine/EngineInvoker.hpp) / [`.cpp`](../packages/native/src/host/engine/EngineInvoker.cpp) | `QueuedInvoker`: the one mutation path (structure to the translator, every other edit onto the ring), the `SyncTap` push, `audioThreadOwns_` |
| [`DspCommand.hpp`](../packages/native/src/host/engine/DspCommand.hpp) | the POD command the ring carries as an opaque host command |
| [`GraphHost.hpp`](../packages/native/src/graph/GraphHost.hpp) / [`.cpp`](../packages/native/src/graph/GraphHost.cpp) | the one command ring, the release ring of plans, `setGraph` / `drain` / `run` / `reclaim` |
| [`Translator.hpp`](../packages/native/src/host/graph/Translator.hpp) / [`.cpp`](../packages/native/src/host/graph/Translator.cpp) | the control-side model: every structural edit as a graph document + its `SystemTable` |
| [`SystemNodes.cpp`](../packages/native/src/host/graph/SystemNodes.cpp) / [`KernelNode.cpp`](../packages/native/src/host/graph/KernelNode.cpp) / [`HostServices.hpp`](../packages/native/src/host/graph/HostServices.hpp) | the cores as nodes (each owning its core), the kernel stage as a node, and the host services they ask for |
| [`SnapshotRegistry.hpp`](../packages/native/src/host/engine/SnapshotRegistry.hpp) / [`.cpp`](../packages/native/src/host/engine/SnapshotRegistry.cpp) | the id-keyed read door; claim/publishSystem/read/release |
| [`Engine.hpp`](../packages/native/src/host/engine/Engine.hpp) / [`.cpp`](../packages/native/src/host/engine/Engine.cpp) | the single-threaded owner of the graph, the DSP kernel and the registry; `processBlock`, the command handler, `SyncTap` |
| [`DspRuntime.hpp`](../packages/native/src/host/dsp/DspRuntime.hpp) / [`.cpp`](../packages/native/src/host/dsp/DspRuntime.cpp) | the bare DSP QuickJS context + byte-sink thunks |
| [`PluginDSP.cpp`](../packages/native/plugin/PluginDSP.cpp) | plugin control-plane bring-up + the `run()` block driver |
| [`PluginShared.hpp`](../packages/native/plugin/PluginShared.hpp) | the in-process editor↔control-plane handoff |
| [`main.cpp`](../packages/native/src/main.cpp) | the standalone / native test host |
| [`AudioDriverRpcService.cpp`](../packages/native/src/host/rpc/AudioDriverRpcService.cpp) | the test host's background audio thread + `audioLoop` |
