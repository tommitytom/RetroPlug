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

There is **no single facade object**. A host composes a shared `Engine` (which owns the one `SystemFactory`, every
core backend registered on it at construction) and the ONE `QueuedInvoker`, plus a handful of stateless service
instances, and mounts each service's methods as one or more **facets** onto the rpcpp server
([`BackendRpcRegistration.hpp`](../packages/native/src/host/rpc/BackendRpcRegistration.hpp)).
Registration is **cross-object** — `addMethod<&Service::m>(instance)` — and **the method name IS
the wire name** (rpcpp derives it), so the C++ names must match the TypeScript `Backend` surface
exactly. The full method tables live in [02-native-host.md](02-native-host.md); the services at a
glance:

| Service | Facet(s) | Concern | Thread |
|---|---|---|---|
| [`HostRpcService`](../packages/native/src/host/rpc/HostRpcService.hpp) | `host` | filesystem, config dir, zip/unzip — stateless, pure | control |
| [`EngineRpcService`](../packages/native/src/host/rpc/EngineRpcService.hpp) | `emulator` + `harness` | put and drop immutable byte resources a node names (`putResource` / `releaseResource`), place systems (`setGraph`) and load script node packs (`loadScriptPack`), reads, MIDI, transport, render/profile | control (pushes mutations) |
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
| QuickJS runtime | control-plane txiki host ([`TjsHostRuntime`](../packages/native/plugin/PluginDSP.cpp)) | the script host's bare audio runtimes, one per pack ([`ScriptHost`](../packages/native/src/host/script/ScriptHost.hpp), owned by the Engine's graph) |
| Owns | the RPC services + their facets, the TS stores and the graph they publish (`EngineGraph`), the editor | the live [`Engine`](../packages/native/src/host/engine/Engine.hpp): the running plan (its system nodes, each owning its core, and its script nodes, the MIDI routing and each role's protocol node among them, whose JS instances live in the script host), **while running** |
| Does | issues mutation *requests*; plans structure into graph documents (the script nodes' settings checked in the script host's control side); reads snapshots by id; frees replaced plans and the cores only they held | drains the command ring, adopting plans in order; runs the plan each block; publishes snapshots |
| Never | dereferences a live `SystemBase` while audio runs; frees a core a running plan holds | builds or frees a core or a plan, or blocks |

The `Engine` itself is **single-threaded and thread-unaware**: it holds no locks and knows
nothing about who is calling it. Thread-safety is entirely a property of the two rings (both
inside the Engine's [`GraphHost`](../packages/native/src/graph/GraphHost.hpp)) and the registry
that surround it, plus the script host's own pair, which carries a destroyed script node's release
to the block thread and its record back. One control-thread-written bit,
[`QueuedInvoker::audioThreadOwns_`](../packages/native/src/host/engine/EngineInvoker.hpp),
forwarded through `Engine::setAudioThreadOwns` to the GraphHost and the script host, decides which
world currently owns the `Engine`, and therefore who drains the command ring.

### The seam map

```
                         CONTROL PLANE  (main / UI thread)
            txiki QuickJS · RPC facets · TS stores + EngineGraph · editor
                         │                    ▲                    ▲
   push plan / command   │                    │ read by id         │ free the replaced plan
                         ▼                    │ (owned copy)       │ (+ cores only it held)
        ┌────────────────────────┐  ┌──────────────────────┐  ┌──────────────────────┐
        │  command ring          │  │  SnapshotRegistry    │  │  release ring        │
        │  GraphHost: plan       │  │  frame/state/SRAM    │  │  GraphHost: the      │
        │  swaps + commands,     │  │  (owned triple-bufs) │  │  replaced plans      │
        │  in push order         │  │                      │  │  (PlanStorage*)      │
        └────────────────────────┘  └──────────────────────┘  └──────────────────────┘
                         │                    ▲                    ▲
             drain block │                    │ publish per block  │ hand back the replaced plan
                         ▼                    │                    │
                         AUDIO THREAD  (DPF run() / spawned audioThread_)
     script host (audio side) · Engine · GraphHost plan: host I/O · script nodes · systems · mixing
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

Every control-plane edit lands on ONE ring: the `SpscRing<Command, 256>` inside the Engine's
[`GraphHost`](../packages/native/src/graph/GraphHost.hpp)
([modular-graph-phase-b1-design.md](../docs/modular-graph-phase-b1-design.md) section 5). An edit
arrives one of three ways:

- **Structure** (which systems run, in what order and wired how, a Game Boy's link cable, each system
  node's settings, which are its core's knobs (a Game Boy's model, palette and the serial-out capture its LSDj
  needs, a NES's region), and its params' values, the host's output shape, and the script nodes, each with its
  settings: ahead of the systems the MIDI routing, the controller session and each role's protocol node, and
  after its Game Boy an LSDj's serial-out decoder) is a graph document TS builds
  ([modular-graph-phase-b2-design.md](../docs/modular-graph-phase-b2-design.md),
  [modular-graph-phase-b3b-design.md](../docs/modular-graph-phase-b3b-design.md),
  [modular-graph-phase-c2-design.md](../docs/modular-graph-phase-c2-design.md)). The stores call
  [`EngineGraph.publish`](../packages/retroplug/src/engineGraph.ts) once at the end of every edit that
  changes what the document holds (a structural edit, the config of a role with a protocol node, a setting
  of a system's core config, the MIDI routing, the controller); it builds the document with
  [`buildGraph`](../packages/retroplug/src/graph/buildGraph.ts) and sends it with the `setGraph` RPC,
  which `EngineRpcService::setGraph` hands straight to `Engine::setGraph`, not through the invoker.
  `Engine::setGraph` builds the plan's `SystemTable` from the request's slots and the live cores (a node reused from an
  earlier plan is already there), and calls `GraphHost::setGraph`, which plans
  the document on the control thread (every layout checked before any instance is created, every new
  instance created and activated, and a node that builds a core filling the plan's table entry as it activates) and
  pushes the plan onto the ring as a plan swap.
- **A param's live value** (a system's gain, a NES's expansion volume: what is worth playing, automating
  or modulating) goes through `setParam`, which `EngineRpcService::setParam` hands to `Engine::setParam`, not
  through the invoker either: `GraphHost::setParam` clamps the value to the param's range and pushes it onto
  the ring as a param command, for a node the last published plan places, to land at the start of the next
  block. The next document the stores publish carries the same value, so a live edit the node did not take is sent
  again.
- **Everything else** (stage host MIDI, controller MIDI or one system's MIDI, set bpm / transport /
  ppq, press a button, write RAM) goes through the one
  [`QueuedInvoker`](../packages/native/src/host/engine/EngineInvoker.hpp), which packs it into a POD
  [`DspCommand`](../packages/native/src/host/engine/DspCommand.hpp) and pushes it onto the same ring
  as an opaque host command (`GraphHost::pushHostCommand`).

At drain the ring is applied **in push order**: a plan swap adopts its plan, a param command lands in
its param's stream, and a host command goes to the Engine's handler, which applies it against the
`SystemTable` of the plan current at that point. A command pushed after a structural edit therefore sees
the new systems and one pushed before it sees the old, even when both plans are adopted in the same drain.

`setGraph` waits (`GraphHost::waitForCommits`, up to a 2 s timeout, then `busy`) only while a plan it
published carries a settings commit to a node type whose prepare reads the node's current settings,
one without `RP_TYPE_STATELESS_PREPARE` (`PlanStorage::awaitsAdoption`): the planner must not ask
such a node to prepare again while its last commit may still be landing. The commits the engine's
documents carry are the script nodes', whenever their settings change (a system added or removed, which
moves the routing node's outputs, a role's config, the MIDI routing, the controller), the system nodes',
whenever a knob moves (a model, a palette, an LSDj's capture), and all of them prepare
statelessly: every script type does, and a system node rebuilds only for a new system id, `romName` or a
construct-time field, which it compares against the key of the instance it was created with (node.hpp's
`rebuild_key`; a moved resource ref rebuilds it too), never against settings a commit writes.
`rp.host.midi-out` swaps or rebuilds, statelessly too, and the other C++ nodes take each settings change
they meet as a rebuild. So `setGraph` never waits on the audio thread: several plans can sit in the ring
at once, adopted in push order in one drain. What bounds edits is the ring itself. While no block runs with
the audio thread still owning the engine (a host that stops calling process while the plugin stays
active, as CLAP's `stop_processing` allows), each edit queues one plan, holding its buffers, and once
the ring is full (255 usable slots, shared with the queued commands) `setGraph` answers `busy`.
`EngineGraph` keeps the model it was asked to publish and sends the next document even if it is
unchanged, so the latest model lands with the next publish once the ring has room again.

**There is no Direct/Queued fork.** An earlier design had two invoker classes (one for the
quiescent case, one for the running case); they were collapsed into this single path.

The only variable is **who drains the ring**, decided by `audioThreadOwns_`:

- **Running (audio thread owns the Engine):** the producer only pushes. The block driver drains
  the ring at the top of every block (`QueuedInvoker::drainInto` → `Engine::drainCommands` →
  `GraphHost::drain`) and applies each plan and command before rendering.
- **Quiescent (control thread owns the Engine):** every push flushes **inline**: the GraphHost
  drains and reclaims right after the push, so a plan is adopted and a command applied before the
  call returns. The same thread is both producer and consumer, so the SPSC invariant holds
  trivially, and a quiescent remove (a document without the system) frees the core synchronously (the
  plan it replaced is reclaimed in the same flush), exactly as a direct call would.

The key consequence: because the quiescent path flushes on **every** push, **the ring is empty
at the moment ownership is handed to the audio thread**. That is what makes the handoff clean:
the plugin's `activate()` sets its block size and the test host's `startAudio` nothing at all, and
then each just sets the bit ([`PluginDSP.cpp`](../packages/native/plugin/PluginDSP.cpp),
[`AudioDriverRpcService.cpp`](../packages/native/src/host/rpc/AudioDriverRpcService.cpp)), with no
ring to flush. The host shape is the control plane's: TS built the running graph for the host's
outputs before the handoff.

### Producers and the drain

Producer methods pack a command and push. Lightweight commands are pure POD: `stageMidi` (a message
of up to 4 bytes rides inline), `setBpm`, `setTransport`, `setPpq`, `pressButton`; if the ring is full
they are simply dropped (a lost edge, never a leak), as is a `setParam`'s, which answers false then.
Heavy commands carry an **owning heap payload**: a `stageMidi` longer than 4 bytes boxes a `new`-ed
buffer (deleted after apply or, if the push fails, immediately), and `stageNodeMidi` carries the node id and
the bytes as one owning payload (`DspCommand::stageNodeMidi`). A `writeRam`, like any capability job's write,
carries a pointer to a job record the control thread owns and frees once the block thread has marked it finished
(`host/engine/CapabilityJobs.hpp`), so applying one allocates and frees nothing. A staged host MIDI message
lands in the engine's staging at drain, where the plan's `rp.host.midi-in` takes it at the top of the next block;
a staged node message is handed there to the node the running plan places under the id, through `rp.stage/1`,
which is how a UI message reaches a system's ingress or a real cart without the engine naming either.

Structure never rides the ring as a command. A `setGraph` request carries a `tap` when its edit re-arms
the NES channel tap (every edit that builds or drops a core, and every routing change, even to the mode
already set), and the plan's swap command carries it as a `SyncTap` trailer (dispatched in the same pop,
right after the plan is adopted), which restores whatever is armed and then arms the tap the request names
against the plan just adopted ([10-multichannel-audio-out.md](10-multichannel-audio-out.md) section 4). A
Game Boy's link group is wiring alone: the store publishes the graph with the cable (`buildGraph` turns its
`linkGroupId` into the `link` wires, and no core decodes it). A document that does not plan (refused,
or a full ring) changes nothing: the cores its nodes built are destroyed as its instances unwind, and the
store keeps its model. The store's next publish is sent even when its document matches the running one, so the
refused document sent again builds those cores again once it plans.

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
  stray block-thread scan can never match a slot that is being torn down. `release` runs when a
  system node destroys its core at reclaim or as a refused plan unwinds, once no running plan holds
  the system. Neither can race an in-flight `publishSystem`.

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
| **build** | `setGraph` plans the document on the control thread, and a new node's first `activate` builds its core through `rp.host.systems/1` (`Engine::buildSystem`) | the backend boots it from the ROM and seed bytes its resources hold and the config its settings made; the engine claims its registry slot under the system id, records it among the live cores, and fills the plan's `SystemTable` entry |
| **adopt → hand back** | the audio thread drains the plan swap | the replaced plan is pushed onto the release ring, the last one of a drain only after every command of that drain is applied; the system table rides with the plan |
| **reclaim → free** | the control thread reclaims | `reclaim()` deletes each released plan; a node only that plan held destroys its core through `rp.host.systems/1` (`Engine::destroySystem`: `registry.release(id)` first, then `delete`) and releases its resource pins |

The stores never reuse a system id (a `DirectGraph` caller may, once the old core is gone), and
`buildSystem` refuses an id that is still live (held by a node of any plan not yet
reclaimed), so a new core always plans as a new instance. A core a refused plan built is destroyed as
that plan's instances unwind.

`reclaim` runs at every `setGraph` and every `loadScriptPack`, in every inline flush (quiescent), at the
hosts' stop and deactivate points, and at each idle tick of a host that has one while its audio runs: the
plugin's editor idle and the SDL standalone's frame loop call `QueuedInvoker::reclaimReleased` every frame,
which with nothing handed back allocates nothing and takes no lock the audio thread takes. It returns the
number of plans freed and is exposed on the wire as `drainReleased` for the harness hosts (the test host, the
CLI, the Node addon), whose session pumps it while their background audio thread runs, since they have no
tick. A removed core, and every node and resource pin only its plan held, is freed at the first of those after
the audio thread has moved past it: within a frame where a tick runs, and at the next publish, deactivate or
`drainReleased` where none does (a plugin whose editor is closed: DPF gives a plugin no idle of its own).

**Leak, don't block.** If the release ring is full (512 unreclaimed plans; `setGraph` reclaims before every publish, so a drain cannot outrun it), the audio thread
**leaks the plan rather than block or free in the render loop**: it only counts the leak (it must
not log), and the next `reclaim()` reports it. In practice any host reclaims far more often than
that. Never trading real-time safety for a free is the deliberate invariant.

On teardown, `freePending()` (`GraphHost::discardPending`) runs once the audio thread is joined
(single accessor again): it adopts every queued plan and hands every queued command back
**discarded**, freeing its payload (a RAM write's bytes, a long MIDI message) without applying it.
This is the teardown counterpart to `drainInto`, which *applies* pending commands.

---

## 6. The QuickJS runtimes

A host runs one control-plane runtime, and every `Engine` it holds has a **script host** of its own
with two more for each **script node pack** it holds: the built-in one, and any loaded beside it
([`ScriptHost`](../packages/native/src/host/script/ScriptHost.hpp),
[modular-graph-phase-b3a-design.md](../docs/modular-graph-phase-b3a-design.md) section 2,
[modular-graph-phase-c1-design.md](../docs/modular-graph-phase-c1-design.md)). (A UI
render job adds a bare QuickJS `RenderHost` with an Engine, so a script host, of its own,
[11-ui-rendering.md](11-ui-rendering.md); the Node-API host runs its control plane in V8.) None is
shared, and no JS value passes between any two:

| | **Control-plane runtime** | **A pack's control side** | **A pack's audio side** |
|---|---|---|---|
| Type | txiki full host ([`TjsHostRuntime`](../packages/native/plugin/PluginDSP.cpp)) | bare QuickJS, no txiki | bare QuickJS, no txiki |
| Owner | the host (plugin / test host / UI harness) | the Engine's script host, one per pack | the Engine's script host, one per pack |
| Runs | the TS control-plane bundle: stores, `Backend` adapter, `__rp_*` globals, editor | its pack's types' `parseSettings` and `layout`, for the planner's `layout_for` and `prepare_settings`; it never holds an instance | every instance of its pack's types (its `create`, `process`, `onSettings`, `portsChanged`, `activate`): for the built-in pack, the MIDI routing, the controller session and each role's protocol node |
| Thread | control plane only | the thread that plans (`setGraph`) and loads packs (`loadScriptPack`) | whichever thread drives the block, once its pack is loaded (the load runs on the control thread) |
| Data | full JS objects over `__rpcSend` | settings JSON in, layout JSON out | the plan's queues and control streams, and copies of its audio buffers, through one reused `Block` object per instance; a JSValue never crosses out |

The built-in pack, `rp.script`, is the first: both its runtimes are loaded at the Engine's construction
with the script-node bundle ([`bundle.ts`](../packages/retroplug/src/script/bundle.ts)), compiled to
bytecode at build time and embedded in `retroplug-backend`, so every host's Engines have the same node
types with no boot call. Each of a pack's script types is a node type of the package named by the
pack's id, and each script node of a plan is a native shim the planner creates, activates and destroys
like any node; its JS instance is made on the block thread when its plan is adopted, before that plan's
first `process`, and released there at the start of a later block once the shim is destroyed (at once
while no audio thread runs). The production bundle's eight are the MIDI routing, a protocol node for each
role that has one (mGB, the N8 FIFO, risa, smsggdj and LSDj sync), the controller session and LSDj's
serial-out decoder: each takes its settings from the document, and its inputs and outputs are real
wires from the host I/O nodes and to each system node's inputs, or, for the decoder, from its Game Boy's
output. Native owns no role logic; in particular, **the drift-exact PPQ tick clock lives entirely in JS**
(`walkTicks`). The nodes and the role
model are specified in [04-roles-protocol-nodes.md](04-roles-protocol-nodes.md); the runtime under them in
[02-native-host.md](02-native-host.md).

More packs load at runtime, while audio runs or not: `loadScriptPack`, on the `emulator` facet every host
mounts, compiles a pack's source on the control thread and loads it into two runtimes made for it, its type
ids under its own id (never `rp` or an id under it, which are the app's), and its types join the registry for
the next document to place. Loaded again, a pack reloads: the running document is planned again, so its
nodes rebuild on the new code while every other node keeps its instance, all or nothing, and the old
generation's runtimes are freed on the control thread once the block thread holds nothing of it. A pack
isolates its nodes from every other pack's (a prototype or a global one patches, a heap it fills) but not from
each other; its promise jobs run after each call into one of its nodes, charged to that node, and each of its
runtimes is held to a memory limit (64 MiB by default). Each loaded pack costs two runtimes, about 180 KB and
twice what its code loads to. Nothing in the app loads one yet.

A pack's audio side runs on **both** threads over its lifetime (the control thread on the pull-path
`renderAudio`, the audio thread once running), so every entry re-anchors QuickJS's stack-overflow
guard to the current thread with `JS_UpdateStackTop` before entering JS, or a call from a
different stack throws a spurious overflow. The control side does the same.

`Engine::processBlock` runs the plan: `rp.host.midi-in` takes what the hosts staged; the routing node fans the host
MIDI out to the systems, the controller session reads the control surface's `rp.launchpad.surface-in`, and each
system's protocol nodes read their routed MIDI (risa and smsggdj read only the transport) and send that system node
its inputs; the system nodes give their cores what moved of their settings and params, then those inputs, and run
them; an LSDj's serial-out decoder reads what its Game Boy captured on its link port in that block and decodes it to
host MIDI, each message at its byte's frame; `rp.n8.nes`, fed by fan-out what its system's protocol nodes send, hands
a physical N8 the same MIDI and bytes; the controller session's LED and page output goes to
`rp.launchpad.surface-out`; `rp.host.midi-out` hands the host the script nodes' MIDI, merged by frame; the mixing
nodes and `rp.host.audio-out` fill the host's buffers. Then `registry_.publishSystem(...)` runs for
each system: the one place every driver funnels the block
([`Engine.cpp`](../packages/native/src/host/engine/Engine.cpp); the full order is in
[02-native-host.md](02-native-host.md)). The host MIDI out (LSDj's MI.OUT and master sync, the
controller's launches to a real Game Boy), through `rp.host.midi-out`, is drained after the block by the
plugin (to the DAW, each message at its frame) and the SDL standalone (to its MIDI out port, at once,
without the frame).

---

## 7. Threading / ownership invariants

The load-bearing rules. Breaking any one of them reintroduces a data race, a use-after-free, or a
lost mutation.

1. **The control plane and the audio thread never touch each other's memory directly.** All
   control→audio change is a plan, a param's value or a `DspCommand` on the one command ring, applied in
   push order; a command is applied against the plan current at its position in the ring. All
   audio→control observation is a registry read or a release-ring handback. Nothing else crosses.

2. **`audioThreadOwns_` is the single source of truth for who owns the `Engine`.** It is written
   only on the control thread: set true **before** the audio thread starts, cleared **after** it
   is joined. The invoker forwards it to the GraphHost, which reads it for the flush decision. The
   test host's audio loop runs on its own `running_` flag, and the bit is cleared only once that
   thread has joined, because clearing it hands the GraphHost back and applies whatever is queued.
   The `Engine` holds no locks; this bit is the entire concurrency contract.

3. **The command ring is empty at every ownership handoff.** The quiescent path flushes on every
   push, so activation/`startAudio` never has a backlog to drain: activation sets the block size
   (`startAudio` nothing) and flips the bit, and the host shape is already the graph TS published.
   Deactivation flips the bit back, which flushes at once: whatever the last block left
   undrained is applied (no lost mutation), and the plans replaced just before stop are reclaimed
   with the cores only they held. The hosts' `drainInto` + `reclaimReleased` after it find nothing
   left.

4. **The audio thread never builds or frees a core or a plan.** Cores are built, activated and
   seeded on the control thread, inside `setGraph`, as the planner creates and activates a plan's
   system nodes. A replaced plan goes back through the release ring and is destroyed, with
   any core only it held, on the control thread. Adopting a plan inside a block is a pointer swap
   and the calls its plan carries (settings commits, `ports_changed`). The block thread does allocate
   and free in the script host's audio runtimes: a script node's JS instance is created in its pack's
   when its plan is adopted (and a commit's settings are parsed into JS for its `onSettings`), and
   released at the start of a later block once its shim is destroyed (16 a block at most). The native record
   behind a released instance goes back to the control thread to be deleted (`ScriptHost::reclaim`),
   as do the settings a commit replaced, and what released instances leave in cycles is collected
   while no block runs (a quiescent reclaim, or the audio thread handing the runtimes back). A heavy
   `create` is therefore an xrun risk at a swap, accepted as the price of runtimes that only the
   block thread enters. A pack's runtimes themselves are made, loaded and freed on the control thread:
   a pack loaded, or a reload's new generation, before any plan places one of its nodes, and a
   generation a reload retired once no record of its nodes is alive.

5. **Leak before you block or free in the render loop.** A full release ring leaks a plan (and the
   cores only it held) rather than free it on the audio thread: real-time safety is never traded for
   a deallocation.

6. **The SnapshotRegistry is the only read path.** Reads never walk the plan's systems or
   dereference a live `SystemBase`. Slot buffers are allocated at `claim` and freed at `release`,
   both on the control thread, both outside the window in which the audio thread publishes to that
   slot. `claim` publishes the slot id last; `release` clears it first.

7. **A JSValue never crosses out of a pack's audio runtime, and no plan memory crosses in.**
   A script node reaches the plan only through its shim: its input messages are copied into JS-owned
   buffers, its audio ports are JS-owned copies (an input's samples copied in before `process`, an
   output's copied out after it), and what it sends is written straight into the plan's output queues.
   So nothing a node keeps, transfers or edits can reach a buffer another node reads or a plan the
   control thread has freed. A protocol node's sends are its output ports. The runtime is separate
   from the control-plane runtime, from its pack's control side and from every other pack's runtimes,
   and is re-anchored to the calling thread on every entry.

8. **TypeScript owns identity, orchestration and structure; native never mints or decides.** System
   ids are allocated by TS and named by a system node's `system` setting; native builds the core it
   names and never an id ([`EngineRpcService.cpp`](../packages/native/src/host/rpc/EngineRpcService.cpp)). Which systems
   run, in what order and wired how is the graph document TS builds and sends with `setGraph`.
   Duplicate, reload and remove are TS orchestration over a new system node whose settings name a
   state-seed resource, registry reads and the documents the stores publish, not native methods.
   Cores are built bare (no default roles baked into C++); feature behaviour lives in TS, the audio
   thread's part in the protocol script nodes.

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
| [`EngineInvoker.hpp`](../packages/native/src/host/engine/EngineInvoker.hpp) / [`.cpp`](../packages/native/src/host/engine/EngineInvoker.cpp) | `QueuedInvoker`: every edit but structure onto the ring, `audioThreadOwns_` |
| [`DspCommand.hpp`](../packages/native/src/host/engine/DspCommand.hpp) | the POD command the ring carries as an opaque host command |
| [`GraphHost.hpp`](../packages/native/src/graph/GraphHost.hpp) / [`.cpp`](../packages/native/src/graph/GraphHost.cpp) | the one command ring, the release ring of plans, `setGraph` / `drain` / `run` / `reclaim` |
| [`buildGraph.ts`](../packages/retroplug/src/graph/buildGraph.ts) / [`engineGraph.ts`](../packages/retroplug/src/engineGraph.ts) | the engine's graph as TS builds it from the stores, and its publisher (the `ProjectStore`'s, or a `DirectGraph`'s or bare `SystemsStore`'s own) |
| [`SystemNodes.cpp`](../packages/native/src/host/graph/SystemNodes.cpp) / [`HostIoNodes.cpp`](../packages/native/src/host/graph/HostIoNodes.cpp) / [`N8Nodes.cpp`](../packages/native/src/host/graph/N8Nodes.cpp) / [`LaunchpadNodes.cpp`](../packages/native/src/host/graph/LaunchpadNodes.cpp) / [`HostServices.hpp`](../packages/native/src/host/graph/HostServices.hpp) | the cores as nodes (each owning its core), the host MIDI I/O nodes, the cart's node and the control surface's two, and the host services they ask for |
| [`SnapshotRegistry.hpp`](../packages/native/src/host/engine/SnapshotRegistry.hpp) / [`.cpp`](../packages/native/src/host/engine/SnapshotRegistry.cpp) | the id-keyed read door; claim/publishSystem/read/release |
| [`Engine.hpp`](../packages/native/src/host/engine/Engine.hpp) / [`.cpp`](../packages/native/src/host/engine/Engine.cpp) | the single-threaded owner of the graph (with the script host its TS nodes run in), its one `SystemFactory`, the staged host MIDI, the host outputs and the registry; `buildSystem` / `destroySystem` / `setGraph` / `loadScriptPack`, `processBlock`, the command handler, `SyncTap` |
| [`ScriptHost.hpp`](../packages/native/src/host/script/ScriptHost.hpp) / [`.cpp`](../packages/native/src/host/script/ScriptHost.cpp) | the script-node runtime: TS nodes behind the C ABI in packs, each two bare QuickJS runtimes of its own (the built-in pack first, more loaded and reloaded at runtime), and the audio runtimes' profiling |
| [`nodes/`](../packages/retroplug/src/script/nodes) / [`bundle.ts`](../packages/retroplug/src/script/bundle.ts) | the script nodes buildGraph places (the MIDI routing, each role's protocol node, the controller session), and the script-node bundle every Engine embeds |
| [`PluginDSP.cpp`](../packages/native/plugin/PluginDSP.cpp) | plugin control-plane bring-up + the `run()` block driver |
| [`PluginShared.hpp`](../packages/native/plugin/PluginShared.hpp) | the in-process editor↔control-plane handoff |
| [`main.cpp`](../packages/native/src/main.cpp) | the standalone / native test host |
| [`AudioDriverRpcService.cpp`](../packages/native/src/host/rpc/AudioDriverRpcService.cpp) | the test host's background audio thread + `audioLoop` |
