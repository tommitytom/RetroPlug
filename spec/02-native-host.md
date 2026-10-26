# 02 — The native host (`packages/native/`)

This is the C++ deep dive for the native half. It implements the thesis's
first clause — **native owns bytes and cores** — behind one narrow RPC surface. TypeScript
drives everything here over resolved paths, opaque byte buffers, and opaque config blobs;
native makes no policy decisions.

[01-architecture.md](01-architecture.md) is the canonical reference for the cross-cutting
runtime concepts this doc leans on — the three hosts over one Backend RPC surface (its
capability facets), the control-plane / audio-thread split, the command ring, the graph the
engine runs on, the snapshot read door, and the release ring. This doc references those by name and
goes deeper on the C++ mechanics: the method tables, the `Engine`'s block, its graph requests, the
`QueuedInvoker` internals, the `SnapshotRegistry` slot machinery, the core backends, and the bare
DSP runtime.

Everything below lives under [`packages/native/`](../packages/native).
The `Engine` + RPC surface compiles into one static library,
[`retroplug-backend`](../packages/native/CMakeLists.txt), which both the DPF plugin and the
two test hosts link — so all three drive the *same* `Engine` over the *same* RPC surface.

## The composition root: the RPC services + facets

> **Where the shared pieces live.** Seven hosts stand this up - the plugin, the SDL standalone, the CLI,
> the Node addon, the test host, the render host and the UI test harness - and the parts they share are
> in [`src/host/`](../packages/native/src/host/):
>
> - `HostServices.hpp` - the service graph itself (Engine, SystemFactory + core backends, the one QueuedInvoker,
>   `HostRpcService`, `EngineRpcService`). Codec-agnostic, which is what lets the Node addon share it.
>   It deliberately STOPS there: `DebugRpcService` and `AudioDriverRpcService` are declared by the hosts
>   that mount them, because `~AudioDriverRpcService` clears the invoker's `audioThreadOwns` bit and
>   calls `freePending()` + `reclaimReleased()` - so merely owning one changes teardown, and the plugin
>   and SDL standalone mount neither facet.
> - `QuickJsGlobals.hpp` - `installPluginNamespace` (the `Symbol.for("plugin")` install, six hosts),
>   `jsCallGlobal` / `jsReadReady` / `jsHasGlobalFunction`, the close guard and the file-browser
>   delivery. `jsCallGlobal` carries a `ready` flag because the plugin must refuse to call into a
>   control plane that never signalled ready (a DAW scan degrades; it cannot exit) while SDL exits 1 on
>   that path and has no analogue.
> - `input/GamepadPump.hpp`, `ui/OpenPath.hpp` - the editor/standalone input and shell-out seams.


There is **no single facade object**. A host composes a shared
[`Engine`](../packages/native/src/host/engine/Engine.hpp), a
[`SystemFactory`](../packages/native/src/system/SystemFactory.hpp) (populated by
[`registerCoreBackends`](../packages/native/src/system/CoreBackends.cpp#L9)), and the ONE
[`QueuedInvoker`](../packages/native/src/host/engine/EngineInvoker.hpp), plus a handful of
stateless service instances that hold references to them:

| Service | Concern | Header |
|---|---|---|
| `HostRpcService` | filesystem / config / zip (pure, stateless) | [`HostRpcService.hpp`](../packages/native/src/host/rpc/HostRpcService.hpp) |
| `EngineRpcService` | emulator lifecycle / reads / kernel / MIDI / transport / render + profile | [`EngineRpcService.hpp`](../packages/native/src/host/rpc/EngineRpcService.hpp) |
| `DebugRpcService` | live-core inspection / stepping / breakpoints (CLI only — [09-cli-debugging.md](09-cli-debugging.md)) | [`DebugRpcService.hpp`](../packages/native/src/host/rpc/DebugRpcService.hpp) |
| `AudioDriverRpcService` | background audio thread + observation atomics (test host only) | [`AudioDriverRpcService.hpp`](../packages/native/src/host/rpc/AudioDriverRpcService.hpp) |

Each service's methods are mounted onto the rpcpp server as one or more **capability facets** by
the `register*Rpc` functions in
[`BackendRpcRegistration.hpp`](../packages/native/src/host/rpc/BackendRpcRegistration.hpp) (host,
emulator, harness, debug, driver: `registerAllBackendRpc` mounts the full union).
Registration is **cross-object** (`addMethod<&Service::m>(instance)`) and the **method name IS
the wire name** — rpcpp derives each identifier from the member pointer — so the C++ signatures
must match the TypeScript `Backend` surface field-for-field ([03-ts-layer.md](03-ts-layer.md)
covers the TS side). A host mounts only the facets it is allowed to expose: the plugin channel
omits `debug`/`harness`/`driver`; the CLI + test host mount everything.

The DPF plugin does not use the `driver` facet — DPF owns its audio thread and drives
`engine_` + `invoker_` **directly** from C++ (`activate` / `deactivate` / `run`, the block loop),
which replaces the `AudioDriverRpcService` loop when DPF owns the audio thread.

### How the three hosts bind it

All three hosts compose the services, mount their facets on an rpcpp server over the
`QuickJSCodec`, and bind the server's dispatch onto
`globalThis[Symbol.for("plugin")].__rpcSend` — the exact namespace the TS
`realBackend.ts` adapter targets. The bind is the same shape across hosts:

- **DPF plugin**: [`PluginDSP::bootControlPlane`](../packages/native/plugin/PluginDSP.cpp)
  brings up a plugin-lifetime txiki runtime, binds `__rpcSend`, sets the
  sample rate **before** the bundle composes (systems bake SR at construct), evals the embedded
  control-plane bytecode, then pumps until `__rp_ready`. The editor
  ([`PluginUI.cpp`](../packages/native/plugin/PluginUI.cpp))
  attaches its LVGL display to that *same* context via the in-process handoff
  ([`SharedDSP`](../packages/native/plugin/PluginShared.hpp#L19)),
  so the UI reaches the backend through the already-bound `__rpcSend` — no second RPC bridge.
  The UI and window-geometry seams are 03's topic.
- **Native test host**: [`main.cpp:84-97`](../packages/native/src/main.cpp#L84) binds
  the same namespace, installs a `globalThis.tjs.exit` hook that records the exit code, evals a TS
  bundle, and pumps the job loop until the harness reports TAP. This is the
  `test:native` runner (`retroplug-host`).
- **Headless UI-test host**: [`UiHarness`](../packages/native/test/ui/UiHarness.hpp#L26)
  is the plugin's control-plane bring-up **minus the audio thread, plus a display**. Because
  there is no audio thread, its
  [`advance(ms)`](../packages/native/test/ui/UiHarness.hpp#L44) explicitly
  calls `renderAudio(ms)` to push frames into the registry the UI reads.

## The RPC surface

Binary crosses the wire as `rfl::Bytestring` (a JS `Uint8Array`) in **both** directions —
the qjs codec decodes a typed byte param straight into `rfl::Bytestring`, never a JSON
int-array. A nullable read is `std::optional` (absent → JS `null`). The shared DTOs live in
[`BackendTypes.hpp`](../packages/native/src/host/rpc/BackendTypes.hpp).

### `HostRpcService` — fs / config / zip

Pure and stateless: no `Engine`, no cores. It is `std::filesystem` + miniz (`zip`/`unzip`).
(The LSDj sav codec used to live here as `savFromJson`; it is now pure TS —
see [05-data-persistence.md](05-data-persistence.md).)

| Method | Signature | Notes |
|---|---|---|
| `readFile` | `(string path) → optional<Bytestring>` | whole file |
| `readFilePrefix` | `(string, uint32 length) → optional<Bytestring>` | first N bytes (ROM sniffing) |
| `writeFile` | `(string, Bytestring) → bool` | creates parent dirs on demand ([`:81`](../packages/native/src/host/rpc/HostRpcService.cpp#L81)) |
| `writeFileAtomic` | `(string, Bytestring) → bool` | tmp + rename ([`:94`](../packages/native/src/host/rpc/HostRpcService.cpp#L94)) |
| `fileExists` / `rename` / `deleteFile` | `→ bool` | |
| `listDir` | `(string dir) → vector<string>` | filenames only |
| `drainChangedPaths` | `→ vector<string>` | pull-drain of the efsw watcher (`NativeFileWatcher`); empty until a host calls `enableWatching` (the plugin does; this test host / CLI don't) ([`:155`](../packages/native/src/host/rpc/HostRpcService.cpp#L155)) |
| `setWatchedRoms` | `(vector<string>) → bool` | register the ROM files to watch (parent dirs); recomputed by TS on every systems-list change. Config dir + `bindings/` are always watched |
| `canonicalize` | `(string) → string` | `weakly_canonical`, falls back to input |
| `configDir` | `→ string` | per-OS config path, computed in this TU (no config-persistence link) ([`:23`](../packages/native/src/host/rpc/HostRpcService.cpp#L23)) |
| `zip` | `(vector<BackendZipInput>) → Bytestring` | miniz, no-copy add ([`:147`](../packages/native/src/host/rpc/HostRpcService.cpp#L147)) |
| `unzip` | `(Bytestring) → vector<BackendZipEntry>` | |

`configDir` is computed per-OS directly in this service, so the host links no config-persistence
code (it is TS-owned — [05-data-persistence.md](05-data-persistence.md)). The `.rplg` PKZIP and
base64 composition also happen in TS; native exposes only the stateless `zip`/`unzip` primitives.
The LSDj sav codec is likewise pure TS now — it used to live here as `savFromJson` and no longer
does.

### `EngineRpcService` — emulator / kernel / MIDI / transport

Every mutation here routes through the `QueuedInvoker` except three, which call the `Engine`
directly on the control thread: `buildCore` and `setGraph` (`Engine::parkCore` / `Engine::setGraph`),
and `setSampleRate` (`Engine::setSampleRate`, so it refuses while the audio thread owns the engine or
the running plan holds a system). The render calls drive `processBlock` on the calling thread
themselves and refuse while the audio thread owns the engine. Every read routes through the
`SnapshotRegistry`. No method walks a live core.

| Method | Signature | Behaviour |
|---|---|---|
| `buildCore` | `(BackendConstructSpec) → optional<RpcBuiltCore>` | TS-owned `spec.id`; `factory_.build` → `Engine::parkCore`, which claims the registry slot. Returns the core's node type, export mode and link group, or null (below) |
| `setGraph` | `(RpcSetGraphRequest) → RpcSetGraphResult` | a graph document TS built, its slots and its tap re-arm → `Engine::setGraph` (below) |
| `applySystemSetting` | `(uint32 id, string key, double value) → bool` | `gainDb` / `reloadOnRomChange` → a `ConfigField` |
| `applyRoleConfig` | `(uint32 id, string kind, string config) → bool` | `kind` `"sameboy"` or `"mesen"`, false for any other; decodes the whole role config and pushes each field as a `ConfigField` (sameboy: Model/Highpass/LinkGroup/FastBoot/ColorCorrection/DmgPalette/LightTemperature; mesen: NesRegion/NesRemoveSpriteLimit/NesApuLatencyMs/NesS5bNoise/NesMmc5PhaseReset/NesExpansionVolume) |
| `readState` | `(uint32 id) → optional<Bytestring>` | published savestate from the registry |
| `readSram` | `(uint32 id) → optional<Bytestring>` | SRAM sliced from the published savestate |
| `screenshot` | `(uint32 id, string path) → bool` | encodes the registry frame to PNG |
| `getFrame` | `(uint32 id) → RpcFrame` | registry frame, raw XRGB8888 |
| `pressButton` | `(uint32 id, uint32 button, bool down) → bool` | joypad transition → the addressed core |
| `renderAudio` | `(double ms) → Bytestring` | synchronous pull render, interleaved f32 L/R, into a graph TS built for 2 host channels; empty while the audio thread owns the engine or the running graph writes another width (as are `renderAudioPerSystem` / `renderAudioPerChannel`, for 2 channels per system or stream) |
| `setTransport` / `setBpm` | `→ bool` | queued transport ops |
| `stageMidiIn` | `(vector<uint8> bytes) → bool` | any non-empty length: a channel message, a SysEx, or several messages as one run |

`duplicate`, `reload` and `remove` are **not** native methods: they are TS orchestration over
`buildCore`-with-state, the registry reads and the documents the stores publish
([`EngineRpcService.cpp`](../packages/native/src/host/rpc/EngineRpcService.cpp)):
duplicate pulls a source savestate and builds a seeded core; reload pulls SRAM and cold-boots
the ROM into the same slot; remove publishes a document without the system. Routing is not a
native call either: it is the wiring of the document. This is the thesis in miniature: native
offers the primitives, TS composes the meaning.

### `AudioDriverRpcService` — background audio thread (test host only)

The plugin drives the audio thread through DPF's `run()`; only the threaded test host mounts
this service, which spins a background thread that OWNS the `Engine` while it runs
([`AudioDriverRpcService.hpp`](../packages/native/src/host/rpc/AudioDriverRpcService.hpp)).

| Method | Signature | Behaviour |
|---|---|---|
| `startAudio` | `→ bool` | sets `audioThreadOwns(true)` **before** spawning `audioThread_` (the stereo shape is TS's: the audio driver sets `multi(2)` first) |
| `stopAudio` | `→ bool` | stops the loop (`running_`), joins, THEN clears the bit (which applies whatever is queued), then `drainInto` + `reclaimReleased` |
| `audioCaptured` | `→ AudioCaptured{energy, frames}` | monotonic atomics for a windowed RMS |
| `sleepMs` | `(double) → bool` | |
| `systemCount` | `→ uint32` | republished atomic while running; live read when quiescent |
| `drainReleased` | `→ uint32` | control-thread reclaim of replaced plans, and the cores only they held |

The loop ([`audioLoop`](../packages/native/src/host/rpc/AudioDriverRpcService.cpp)) runs
`while (running_)`, its own flag rather than the ownership bit: `drainInto` → republish the live
count → `processBlock(1024,…)` → accumulate energy/frames → sleep 200 µs (faster than real time,
so a short `sleepMs` window still yields plenty of audio). The bit is cleared only once the thread
has joined, because clearing it hands the GraphHost back and applies what is queued.

### `BackendConstructSpec` (the construct DTO)

[`BackendConstructSpec`](../packages/native/src/host/rpc/BackendTypes.hpp#L96) is the one
DTO worth spelling out — it mirrors the TS `ConstructSpec`:

| Field | Type | Meaning |
|---|---|---|
| `id` | `uint32` | **TS owns the id counter**; native never allocates one |
| `romPath` | `string` | `""` when embedded |
| `embeddedRom` | `string` | marker, e.g. `"mgb"` (`""` when file-backed) |
| `platform` | `optional<string>` | `"gb"` / `"nes"` / `"gba"` / `"sms"` / `"gg"`: which system a multi-platform core builds |
| `core` | `optional<string>` | `"sameboy"` / `"mesen"` — the factory registry key |
| `savPath` / `statePath` | `optional<string>` | on-disk seed paths |
| `sramBytes` / `stateBytes` | `optional<Bytestring>` | zip-import seed bytes (win over the on-disk paths) |
| `romBytes` | `optional<Bytestring>` | ROM bytes to load instead of reading `romPath` (a patched image, e.g. LSDj asset overrides; honoured by SameBoy and by Mesen's NES, GBA and SMS / Game Gear), `romPath` still travelling for the watcher and `.sav` resolution |
| `settings` | `optional<string>` | the backend "system"-role config JSON, decoded only by the matching backend |
| `serialOutCapture` | `optional<bool>` | arm a Game Boy's serial-out capture (LSDj MI.OUT) before its first block; ignored by every other core |

## `Engine` + `QueuedInvoker`: the mutation path

The command-ring model, the `audioThreadOwns_` bit, and the release-ring ownership handoff
are defined canonically in [01-architecture.md](01-architecture.md). Here is the C++. The design
these follow is [modular-graph-phase-b1-design.md](../docs/modular-graph-phase-b1-design.md).

### `Engine`

[`Engine`](../packages/native/src/host/engine/Engine.hpp) is single-threaded and thread-unaware.
It owns the staged host MIDI (`pendingMidi_`, `pendingControllerMidi_`: flat arenas, allocation-free
while a block stages what its input nodes carry), the host outputs the plan fills (`midiOut_`,
`controllerOut_`), the snapshot registry, the transport fields, and the graph the systems run on: the
`ScriptHost` its script nodes (the DSP kernel among them) run in, a `NodeRegistry` holding the node
packages (`rp.core`, `rp.host`, `rp.sameboy`, `rp.mesen`, `rp.host.io`, `rp.n8`, `rp.script`), a
`ResourceTable`, the
[`GraphHost`](../packages/native/src/graph/GraphHost.hpp) running the plan, the cores `buildCore`
parked until a document places them, and every live core by id. Every op runs
"now, on the calling thread"; it holds **no atomics**: transport and the ppq clock are plain members
mutated only by whichever thread owns it (the pull path or the audio loop, never concurrently). A
removed or displaced core is never freed on a hot path: it leaves with the plan that last ran it.

Its per-block driver `Engine::processBlock(frames, outputs, n)` is the one place every host funnels a
block. The host has already drained the ring (`drainInto` → `Engine::drainCommands` →
`GraphHost::drain`: plan swaps and commands applied in push order). Then, in order:

1. `syncTrace_.beginBlock(...)` (inert unless `RETROPLUG_SYNC_TRACE` is set).
2. `GraphHost::run(frames, transport, outputs, n)`: silence every host output, then run the plan:
   1. **The host inputs and the kernel.** `rp.host.midi-in` and `rp.host.controller-in` take what the
      hosts staged (the host MIDI stable-sorted by frame). The DSP kernel, the script node
      `rp.dsp.kernel` ([kernelNode.ts](../packages/retroplug/src/script/kernelNode.ts), run by the
      [script host](../packages/native/src/host/script/ScriptHost.hpp)), reads them, the transport at
      the **block-start** `ppq_` and each Game Boy's `serialOut` of last block (a feedback wire), and
      sends each system's `serialIn`, `midi`, `bytes` and `buttons` on wires to that system node's
      inputs, its host MIDI out to `rp.host.midi-out` and its control-surface out to
      `rp.host.controller-out`. `rp.n8.cart-mirror`, wired from the first NES in slot order, hands what
      that system gets to `coreByteSink_` for a physical N8 (a message staged straight at it,
      `StageSystemMidi`, too).
   2. **The system nodes** ([SystemNodes.cpp](../packages/native/src/host/graph/SystemNodes.cpp):
      `rp.sameboy.gb`, `rp.mesen.nes`, `rp.mesen.gba`, `rp.mesen.sms`), each a `SystemNode<T>` over a
      core's `prepareForBlock` / `stepIfBelowTarget` / `finishBlock`. An unlinked system is one
      `process()` call. Linked Game Boys run as one coupled group over `rp.step/1`, round-robin:
      prepare every member in order, then passes of one step per member until none is below the
      block's target, then finish each, so their serial cable stays in instruction lockstep. The
      node zeroes the buffers it hands `finishBlock` (which adds into them) and sets the lane count
      from which of its outputs are connected
      ([10-multichannel-audio-out.md](10-multichannel-audio-out.md) section 1).
   3. **The mixing nodes**, `rp.core.split` and `rp.core.add`: the routing, as wiring (below).
   4. **`rp.host.audio-out`**, which copies its buses into the host's buffers (it reaches them
      through `rp_host.get_interface("rp.host.audio-buffers/1")`).
3. What was staged and no node of the plan took (a plan with no `rp.host.midi-in` or
   `controller-in`) is dropped, not carried into the next block.
4. `registry_.publishSystem(...)` for each system of the plan, in slot order: copy its
   freshly-published frame/state/SRAM into the owned snapshot store.
5. `syncTrace_.endBlock()`, then advance `ppq_` only if transport is running (so the kernel's
   `walkTicks` and the cores see the same block-start ppq).

Live config edits land in `Engine::applyConfigField`, dispatched by the core's concrete type and
**value-guarded per field** so a whole-config re-send only acts on what changed (a `Model` change
triggers `restartEmulator()` in place, and the cable needs no rewiring because `linkPeers_` holds
the systems, which survive it; an unchanged one no-ops, avoiding a spurious restart that would nuke
a restored savestate). It applies at drain against the plan current at that point, finding the core
by id in that plan's `SystemTable`. `Engine::setSampleRate` re-rates the running plan's cores in slot
order, then the parked ones, then re-activates the graph's buffers at the new rate;
`Engine::setMaxBlockFrames` grows the plan's block size to a host's maximum (never below 8192, the
largest block the parity matrix holds), and the plugin and the SDL standalone call it before handing
the engine over.

### Graph requests

Structure arrives as a graph document TS builds
([`buildGraph.ts`](../packages/retroplug/src/graph/buildGraph.ts),
[modular-graph-phase-b2-design.md](../docs/modular-graph-phase-b2-design.md)), in two calls:

- **`buildCore(spec)`** builds and activates a core on the control thread (`factory.build`), arms a Game
  Boy's serial-out capture if the spec asks, and parks it under its system id with `Engine::parkCore`,
  which claims its snapshot slot (after checking the id is not live: the registry finds a slot by id, first
  match). It answers with the node type the core is placed as and the two values the graph needs as the
  core decoded them (a NES's export mode, a Game Boy's link group), or null. It refuses id 0 (the
  registry's free-slot id) and an id that is still live: parked, or held by a node of any plan not yet
  reclaimed (it reclaims first, so with no audio thread running every removed core is already gone).
- **`setGraph({ document, slots, tap? })`** (`Engine::setGraph`) checks that the document's system nodes
  and the slots name the same systems, each node `sys-<id>` with settings `{ "core": <id> }`, of the node
  type its core is placed as, and that a tap arm names one of the slots and a mode from 1 to 3 (any of
  these refuses the document before anything is built); builds the plan's `SystemTable` (a slot-ordered
  list of `{ id, SystemBase*, SameBoySystem*, MesenNesSystem* }`,
  [SystemTable.hpp](../packages/native/src/host/graph/SystemTable.hpp)) from the slots and the live cores;
  turns `tap` into the swap's `SyncTap` trailer; and hands both to `GraphHost::setGraph`, which computes
  every node's layout from the document (`layout_for` on each node type), runs every layout check, and
  only then creates and activates the new instances. The table rides with the plan, so the audio
  thread's lookups always match the plan that is running. Once a document has published, a core still
  parked is one no document placed, and is freed with its snapshot slot. A refused document changes
  nothing: a core a node of the refused plan had taken goes back to parked, for a corrected document to
  place.

The document `buildGraph` writes:

- **Order.** `rp.dsp.kernel` first, then the unlinked systems by slot, then the link groups in order of
  first appearance (members by slot, one `link` wire chaining each member to the next), then the mixing
  nodes, then `rp.host.audio-out`. The planner breaks ties by document order, so this is also the order
  the plan runs in. Node ids are `kernel`, `sys-<id>` and names derived from them, never from position,
  so a system keeps its instance across edits. Link groups are Game Boys only, bucketed by nonzero
  link-group id in slot order, a group of fewer than two dissolved.
- **Routing as wiring.** For each host channel it lists the contributions in the order they sum:
  unlinked systems by slot, then the link groups by first appearance, L before R within a system. One
  contribution is wired straight to `audio-out`; several go through one mono `rp.core.add` in that order
  (chained past its 16 inputs, the first add's output as the next one's first input). A system's stereo
  output reaches mono lanes through `rp.core.split`. For the system at slot i on N host channels:

  | Mode | Wiring |
  |---|---|
  | Stereo, and every split that falls back | L to channel 0, R to channel 1 |
  | TwoPerInstance | L to `(2i) mod N`, R to `(2i + 1) mod N` |
  | OnePerInstance | L and R both to `i mod N` |
  | ChannelSplit / PinSplit / StereoPinSplit, valid | stream k to lane `(stride·k) mod N` and, for a stereo stream, the next; StereoPinSplit's mono stream is wired to both lanes of its pair |

  A split is valid with exactly one system, the pin splits only on a NES, and `stride × streams ≤ N`;
  an invalid split falls back to Stereo wiring
  ([10-multichannel-audio-out.md](10-multichannel-audio-out.md) section 4).
- **Host shapes.** The control plane's `EngineGraph` holds the shape and rebuilds for it. A shape change
  is a rewire: every instance is reused, so no core notices.

  | Shape | Set by | Channels |
  |---|---|---|
  | `multi(n)` | the plugin (8, through `__rp_setHostOutputs` once the control plane is up), the SDL standalone (its device's channel count, likewise), TS's audio driver for `startAudio` and `renderAudio` (2) and `renderBlocks` (its `outputs`) | the routing above |
  | `perSystem` | TS's audio driver for `renderAudioPerSystem` | slot i's mix on channels `2i`, `2i + 1` |
  | `perChannel` | TS's audio driver for `renderAudioPerChannel` (one system) | stream s on channels `2s`, `2s + 1`; a mono stream fills only `2s` |

  The render calls set their shape, render, and put the one before it back. Natively, each render RPC
  refuses while the audio thread owns the engine, and refuses buffers that do not match the running
  plan's host width (the sum of its `audio-out` buses, recorded by `setGraph`), so a caller that forgot
  the shape gets an error rather than audio wired for another output count.
- **Cores.** A node takes its core on first activation through the host service `rp.host.cores/1`
  ([HostServices.hpp](../packages/native/src/host/graph/HostServices.hpp)), by the id its settings name,
  and owns it from then on.

### `QueuedInvoker`

[`QueuedInvoker`](../packages/native/src/host/engine/EngineInvoker.hpp) (in the files
`EngineInvoker.{hpp,cpp}`) is **the one command path; there is no Direct/Queued fork**. Structure
arrives as graph requests (above). Every other edit packs into a POD
[`DspCommand`](../packages/native/src/host/engine/DspCommand.hpp) and pushes onto the GraphHost's
one `SpscRing<Command, 256>` as an opaque host command, behind any plan already there. The single
control-thread bit `audioThreadOwns_` (forwarded to `GraphHost::setAudioThreadOwns`) decides who
drains: while the audio thread owns the `Engine` it drains each block and the producer just pushes;
otherwise the GraphHost flushes **inline** right after each push (`drain` + `reclaim`; the same
thread is producer and consumer). Because the quiescent path flushes every push, the ring is empty
at the ownership handoff, so the plugin's `activate()` / the test host's `startAudio` take over
cleanly.

`DspCommand` is a `Kind` tag plus a union of POD payloads, small enough to ride the ring as a
`HostCommand`:

| Kind | Payload | Ownership |
|---|---|---|
| `SetSystems` | `string* json` | owning: the audio thread `delete`s after apply |
| `LoadKernel` | `vector<uint8>* bytecode` | owning: the audio thread `delete`s after apply |
| `StageMidi` | `uint8 data[4]`, `len`, or `vector<uint8>* ext` | inline for a message of up to 4 bytes; a longer one (a SysEx) owning |
| `StageControllerMidi` | `uint8 data[4]`, `len` | inline only; a message over 4 bytes is refused before it is pushed |
| `StageSystemMidi` | `id`, `data[4]` / `len` or `ext` | as `StageMidi`, delivered to one system's ingress, never through the kernel |
| `SetBpm` / `SetTransport` / `SetPpq` | `double` / `bool` / `double` | inline |
| `SetConfigField` | `id`, `field`, `value` | inline |
| `PressButton` | `id`, `button`, `down` | inline |
| `WriteRam` | `id`, `offset`, `vector<uint8>* bytes` | owning |
| `SyncTap` | `arm`, `mode`, `id` | inline; rides in the plan's swap command as a trailer for every `setGraph` request carrying a `tap` |

Heavy, rare payloads (systems JSON, kernel bytecode, RAM writes, long MIDI) cross as raw **owning**
pointers: the accepted non-RT-on-rare-op pattern. At drain the GraphHost hands each host command
to `Engine::onHostCommand`, which applies it (`applyCommand`) against the `SystemTable` of the plan
current at that point, and `delete`s owning payloads after apply. `SyncTap` is the NES channel-tap
arming: restore whatever `armedTap_` armed, then, when the request names an arm, set that NES to its
export mode (either pin split 1, `ChannelSplit` 3, as `buildGraph` decides for a split routing over a
single NES), recording what it replaced. TS sends it on every edit that builds or drops a core and on
every routing change, even when the wanted mode is unchanged, and each run resets the capture streams;
it rides the plan's swap, so no block runs between the two. `armedTap_` holds the core pointer as well
as the id, so a core that has gone is never restored onto another. The `LinkGroup` config field is the
one field that is also structure: the invoker pushes it to the core, and the store republishes the
graph with the cable.

**Ownership handoff.** A displaced or removed core leaves with the plan that last ran it, and is
freed on the control thread when that plan is reclaimed (`reclaimReleased` → `GraphHost::reclaim`):
the node destroying it releases the snapshot slot first, then deletes the core
([01-architecture.md](01-architecture.md) section 5). Producer edge cases are handled without
blocking or freeing in render: a **full ring** on an inline POD command drops a lost edge, not a
leak; on an owning payload the producer deletes it; a plan the ring cannot take is freed
unpublished (each core its new nodes had taken going back to parked) and `setGraph` answers busy, so
the store keeps its model, the refusal is logged, and the next document is sent even if unchanged. A
**full release ring** leaks a plan rather than block or free in the render loop. [`freePending`](../packages/native/src/host/engine/EngineInvoker.cpp) (teardown only, ring
single-accessor) discards what is still queued: it adopts the queued plans and frees the queued
commands' payloads unapplied.

The block drivers are the same shape: the plugin's
[`run()`](../packages/native/plugin/PluginDSP.cpp) (stage the DAW's MIDI → `drainInto` → set
bpm/transport direct → `processBlock`), `audioLoop` for the test host, and `renderBlocks` for the
harness. The plugin's [`activate` / `deactivate`](../packages/native/plugin/PluginDSP.cpp) flip the
ownership bit around DPF's lifecycle (`activate` first sets the block size),
draining and reclaiming on the way out.

## `SnapshotRegistry`: the read door

The read-door doctrine — the control plane observes cores only through owned copies, never a
live `SystemBase` — is in [01-architecture.md](01-architecture.md). This is the store's
internals.

[`SnapshotRegistry`](../packages/native/src/host/engine/SnapshotRegistry.hpp) is a
fixed-address `std::array<Slot, 64>`. Each `Slot` carries an **atomic `id`**
(0 = free, so the block thread scans by id with no rehash), `width`/`height`, three owned
triple-buffers (`FrameBufferTriple frame`, `MemorySnapshotTriple state`, `MemorySnapshotTriple
sram`), a `sramOffset`, and a `sampleAccum`. Buffers are allocated at claim and freed at
release, both on the **control thread**, never the audio thread. `publishSystem` is only ever
called for a system of the running plan, which is exactly the window before its release, so a
published-to slot is never concurrently freed.

The ordering guarantees are the load-bearing detail:

- **`claim(id, sys)`**: grab a free slot (a full pool fails the build, logged), reset it, size `frame` from the
  core's framebuffer (left **unpublished** — no frame rendered yet), and **seed** `state` from
  `sys.saveStateBytes()` + slice `sram` from `stateRegions()[Sram]`, publishing both. Then
  `id.store(..., release)` **LAST**: the slot becomes visible only after its buffers are ready.
  Seeding is why a read right after
  `buildCore` returns real bytes with no block yet rendered. `claim` requires the core to
  already have `enableStateSnapshot()`'d (for `stateRegions()`).
- **`publishSystem(system, frames, sampleRate)`**, which `processBlock` calls for each of the
  running plan's systems in slot order: `find(id)`; copy the framebuffer **every block** (cheap); on
  the coarse interval (`kStateIntervalSec = 0.5`, matching the core's own snapshot cadence via
  `sampleAccum`) `readStateSnapshot` into `state` and slice `sram` out of it, publishing both.
- **`release(id)`**: clear `id` **FIRST** so a stray block-thread scan can't match, THEN free the
  buffers. Idempotent; it runs when a system node destroys its core at reclaim, once no running
  plan holds the system.

Reads (`readFrame` / `readState` / `readSram`) copy out of the published triple by id and
return `published: false` for an unknown, non-video, or not-yet-rendered slot. The
`Engine`'s read methods
([`readState`/`readSram`/`screenshot`/`getFrame`](../packages/native/src/host/engine/Engine.cpp))
route straight here: they never walk the plan's systems or dereference a live core, which is the
fix for the historical live-read bug class where reads were guarded off in the running plugin.

The registry deliberately **double-copies** from each core's own tear-free triple, because the
shared `SystemBase` can't yet publish straight into the registry (the header comment of
[`SnapshotRegistry.hpp`](../packages/native/src/host/engine/SnapshotRegistry.hpp));
collapsing that second copy is a pending refactor ([07-remaining-work.md](07-remaining-work.md)).

## The core backends: `SystemFactory` + `SameBoyBackend` / `MesenBackend`

[`SystemFactory`](../packages/native/src/system/SystemFactory.hpp) is the one build
path: an `unordered_map<string core, unique_ptr<SystemBackend>>` keyed by `core`, populated by
[`registerCoreBackends`](../packages/native/src/system/CoreBackends.cpp#L9) (`"sameboy"` →
`SameBoyBackend`, `"mesen"` → `MesenBackend`). `build` dispatches on the key and returns nullptr
on an unknown core.

Every backend consumes a backend-agnostic
[`SystemBuildSpec`](../packages/native/src/system/SystemFactory.hpp) — `core`,
`platform`, `romPath`/`embeddedRom`, `sram`/`savestate` seed bytes, and an **opaque `settings`
blob** whose schema is owned by TS and decoded only by the matching backend. A
[`SystemBackend::build`](../packages/native/src/system/SystemFactory.hpp#L29) runs on the
control thread (heavy, non-RT) and returns an **already-`onActivate`'d** system or nullptr. The
wire→build mapping is
[`toBuildSpec`](../packages/native/src/host/rpc/EngineRpcService.cpp): `core` defaults
from `platform` via
[`defaultCoreFor`](../packages/native/src/host/rpc/EngineRpcService.cpp) (nes/gba → mesen,
else sameboy); zip-import seed bytes win over on-disk paths; `settings` passes through unchanged.

### `SameBoyBackend`

[`SameBoyBackend::build`](../packages/native/src/system/sameboy/SameBoyBackend.cpp#L42) resolves the
ROM (an embedded marker via `rp::embeddedRom` is SameBoy by fiat; a file-backed ROM is slurped
and gated on `detectRomFormat == RomFormat::Gb`, so a non-GB file is rejected). It builds a
`SameBoyConfig`, then decodes the opaque `settings` blob into a
[`SameBoyRoleConfig`](../packages/native/src/system/sameboy/SameBoyBackend.hpp#L18) and applies
model / highpass / linkGroupId / fastBoot **at construct**
([`:71-74`](../packages/native/src/system/sameboy/SameBoyBackend.cpp#L71)) — before `onActivate`, so a
loaded non-default model doesn't trigger a post-build restart that would nuke the just-restored
savestate. The shared build sequence
[`buildSameBoy`](../packages/native/src/system/sameboy/SameBoyBackend.cpp#L31) is the critical seam:

```cpp
auto sys = std::make_unique<SameBoySystem>(id, std::move(cfg), std::move(romBytes));
sys->onActivate(sampleRate);       // boots gb_ + restores sram then savestate
sys->enableStateSnapshot();        // tear-free savestate each block for the registry / Duplicate
```

The cores are built **bare** — no feature behaviour is baked into C++. The DSP-thread behaviours
(LSDj sync, mGB, Arduinoboy) run in the TS DSP kernel; kit-patch instead runs on the UI thread
(writing patched memory regions into the core), not in the kernel. See 04. The system-role config
schema (`SameBoyRoleConfig` field names) mirrors TS's
`coreRoles.ts`; [`decodeSameBoyRoleConfig`](../packages/native/src/system/sameboy/SameBoyBackend.cpp#L26)
reads with `rfl::DefaultIfMissing` (forward-tolerant) and is the single JSON decode point,
shared by live `applyRoleConfig` and the construct blob. The role model is 04's topic.

### `MesenBackend`

[`MesenBackend::build`](../packages/native/src/system/mesen/MesenBackend.cpp#L24) is one backend
serving two platforms. It slurps the ROM and dispatches on `spec.platform`: `"nes"` gates
`RomFormat::Nes` and builds a `MesenNesSystem`; `"gba"` gates `RomFormat::Gba` and builds a
`MesenGbaSystem` (empty `biosPath` → HLE boot ROM). A mislabelled ROM or an unserved platform
returns nullptr. Both call `onActivate` then `enableStateSnapshot`
([`MesenBackend.cpp:32`](../packages/native/src/system/mesen/MesenBackend.cpp#L32)), so NES/GBA
cores publish a tear-free savestate for the registry / Duplicate just like SameBoy. The `settings`
blob carries the NES role-config (Region / Remove Sprite Limit / channel-export mode).

## `ScriptHost` + `ScriptCompiler`: the script-node runtime

Native runs the TypeScript DSP role kernel but understands nothing about roles. The kernel itself
([`dspKernel.ts`](../packages/retroplug/src/dspKernel.ts)) and its behaviour are 04's topic; it
runs as a script node ([`kernelNode.ts`](../packages/retroplug/src/script/kernelNode.ts)), and this
doc covers the C++ runtime under it (docs/modular-graph-phase-b3a-design.md section 2).

[`ScriptHost`](../packages/native/src/host/script/ScriptHost.hpp) owns **two bare QuickJS
runtimes** (no txiki; see [01-architecture.md](01-architecture.md)), one per Engine, both loaded at
construction with the script-node bundle embedded as bytecode
([`bundle.ts`](../packages/retroplug/src/script/bundle.ts)): a control-side one that only validates
settings and gives a node's ports, and the audio-side one that holds every instance and runs
`process`. Each script type is an `rp_node_type` of the package `rp.script`, its instances native
shims the planner creates and destroys like any node's. A node reads its inputs and writes its
outputs through one reused `Block` object, straight from and into the plan's queues, so **a `JSValue`
never crosses out**. The **drift-exact PPQ tick clock lives entirely in the JS kernel**
(`walkTicks`); native passes only the block's transport in. Every entry into a runtime re-anchors
with `JS_UpdateStackTop` because the driving thread differs (the control thread on the pull path,
the audio thread while running), and QuickJS's stack-overflow guard is calibrated against the
thread that last entered it. A profile build profiles the audio runtime there
([08-profiling.md](08-profiling.md)).

[`ScriptCompiler`](../packages/native/src/host/dsp/ScriptCompiler.hpp#L17) (`dsp::compileToBytecode`)
compiles a script to QuickJS bytecode on a **transient, bare context**: the engine-graph tests use
it to build their toy script nodes. The bytecode is version-locked to this exact qjs-ng build, which
is fine because the compiler and the script host are the same `qjs`.

## Wrapped shared-core classes

The host links the shared core through
[`retroplug-core`](../packages/native/CMakeLists.txt) (the static lib that
compiles the shared sources under [`packages/native/src`](../packages/native/src) once) and wraps
its **runtime** classes:

| Shared class | Host use |
|---|---|
| [`SystemBase`](../packages/native/src/system/SystemBase.hpp) | the abstract core; owned by its system node in the running plan (`SystemNode<T>`, [SystemNodes.cpp](../packages/native/src/host/graph/SystemNodes.cpp)); drives `onActivate`/`onProcess`/`framebuffer`/`saveStateBytes`/`readStateSnapshot`/`stateRegions`/`enableStateSnapshot`/`pressButton`/`pushSerialIn` |
| [`SameBoySystem`](../packages/native/src/system/sameboy/SameBoySystem.hpp) | the GB core; the live-apply `dynamic_cast` target for `applyConfigField`; its `linkPeers_` are the serial cable, set by its node's `rp.link/1` `set_peers` (the other members of its coupled group, in member order) |
| [`MesenNesSystem` / `MesenGbaSystem` / `MesenSmsSystem`](../packages/native/src/system/mesen) | the NES, GBA and SMS/GG cores; the NES is the `SyncTap` target |
| the LSDj kit/sample pipeline (`KitCompiler` / `KitUtil` / `SampleCache` / `Effects`), `util/MinizZip`, `EmbeddedRoms` | backing kit patching, `zip`/`unzip`, and embedded-ROM resolution |
| `SpscRing` / `FrameBufferTriple` / `MemorySnapshotTriple` | the transport **primitives** the command/release rings and the snapshot registry are built on |

The graph the engine runs on is not in `retroplug-core`: the executor, planner and
[`GraphHost`](../packages/native/src/graph/GraphHost.hpp), with the `rp.core` mixing nodes and the
`rp.host` I/O nodes, are the `retroplug-graph` / `retroplug-graph-exec` libs (`src/graph/`), and
the engine's own nodes (`src/host/graph/`) compile into `retroplug-backend`.

ROM classification (the old C++ `RomSniffer`) and the LSDj sav codec are TS-owned — neither is in
the C++ tree.

The txiki host `TjsHostRuntime` comes from
[`deps/dpf.js`](../deps/dpf.js), not the shared core.

## Build & identity

The [`retroplug-backend`](../packages/native/CMakeLists.txt) static lib wraps
[`retroplug-core`](../packages/native/CMakeLists.txt) with the `Engine` (and links
`retroplug-graph`, the graph it runs on), the RPC services, the DSP runtime, and the txiki host,
and is PIC (it links into the shared plugin module). The test host `retroplug-host` is that lib +
`main.cpp`; the DPF plugin is
[`dpf_add_plugin(retroplug TARGETS clap vst3 vst2 au jack)`](../packages/native/CMakeLists.txt)
(AU is macOS-only). The control-plane and UI bytecode bundles are built by
`tools/build-controlplane.js` + `tools/build-ui.js` piped through `tjsc`, and are
**derived — never committed**. [06-build-test.md](06-build-test.md) is the full build/verify
story.

Plugin identity: name `RetroPlug`, CLAP id
`net.tommitytom.retroplug`, `UNIQUE_ID RPlg` / `BRAND Tmtt`,
**0 inputs / 8 outputs = four stereo pairs `out_1..4`**
([`DistrhoPluginInfo.h:13`](../packages/native/plugin/DistrhoPluginInfo.h#L13)),
IS_SYNTH, HAS_UI (the React/LVGL editor, 480×432, user-resizable), FILE_BROWSER, MIDI in+out,
TIMEPOS, STATE + FULL_STATE, and DIRECT_ACCESS (the in-process editor↔DSP handoff). Master gain
is one automatable parameter applied post-render across all 8 channels
([`PluginDSP.cpp`](../packages/native/plugin/PluginDSP.cpp)).

## Not yet built / deferred

- **Live memory-region subscription.** There is no live "watch RAM" streaming path
  (`enableMemorySnapshot(type)`). Reads are one-shot through the registry read door; if live
  memory streaming to the UI is ever needed, that arming seam must be built.
- **Live `applyConfigField` for Mesen.** NES/GBA role knobs (Region / Remove Sprite Limit) apply
  **at construct**, not live — the `dynamic_cast` in
  [`Engine::applyConfigField`](../packages/native/src/host/engine/Engine.cpp) is SameBoy-only,
  so changing a Mesen knob rebuilds the system rather than nudging the running core.
- **Per-block buttons/keys into the kernel.** The kernel stage passes `kNoButtons` /
  `kNoKeys` today ([`Engine.cpp`](../packages/native/src/host/engine/Engine.cpp)); the
  kernel's button/key input arrays are wired but not yet fed (this blocks the raw LSDj Keyboard
  mode — [07-remaining-work.md](07-remaining-work.md)).
- **The snapshot double-copy collapse** (§`SnapshotRegistry`) is a documented redundancy, not a
  bug — a pending refactor, see [07-remaining-work.md](07-remaining-work.md).

## Key files

- [`BackendRpcRegistration.hpp`](../packages/native/src/host/rpc/BackendRpcRegistration.hpp) — mounts the capability facets onto a server; the single source of truth for the wire surface.
- [`CoreBackends.cpp`](../packages/native/src/system/CoreBackends.cpp) — registers the `sameboy` / `mesen` backends into the `SystemFactory`.
- [`Engine.hpp`](../packages/native/src/host/engine/Engine.hpp) / [`.cpp`](../packages/native/src/host/engine/Engine.cpp): the single-threaded owner of the graph, the DSP kernel and the registry; `processBlock`, the command handler, `SyncTap`.
- [`EngineInvoker.hpp`](../packages/native/src/host/engine/EngineInvoker.hpp) / [`.cpp`](../packages/native/src/host/engine/EngineInvoker.cpp): `QueuedInvoker`, the one command path; [`DspCommand.hpp`](../packages/native/src/host/engine/DspCommand.hpp).
- [`host/graph/`](../packages/native/src/host/graph): [`SystemNodes.cpp`](../packages/native/src/host/graph/SystemNodes.cpp) (the cores as nodes), [`HostIoNodes.cpp`](../packages/native/src/host/graph/HostIoNodes.cpp) (the host I/O nodes and the cart mirror), [`SystemTable.hpp`](../packages/native/src/host/graph/SystemTable.hpp), [`HostServices.hpp`](../packages/native/src/host/graph/HostServices.hpp).
- [`GraphHost.hpp`](../packages/native/src/graph/GraphHost.hpp) / [`.cpp`](../packages/native/src/graph/GraphHost.cpp), [`rp_node.h`](../packages/native/src/graph/rp_node.h): the one ring, plan swap and reclaim; the node ABI (`layout_for` per type).
- [`SnapshotRegistry.hpp`](../packages/native/src/host/engine/SnapshotRegistry.hpp) / [`.cpp`](../packages/native/src/host/engine/SnapshotRegistry.cpp) — the owned read door.
- [`SystemFactory.hpp`](../packages/native/src/system/SystemFactory.hpp), [`SameBoyBackend.cpp`](../packages/native/src/system/sameboy/SameBoyBackend.cpp), [`MesenBackend.cpp`](../packages/native/src/system/mesen/MesenBackend.cpp) — the build path.
- [`ScriptHost.hpp`](../packages/native/src/host/script/ScriptHost.hpp) / [`.cpp`](../packages/native/src/host/script/ScriptHost.cpp), [`ScriptCompiler.hpp`](../packages/native/src/host/dsp/ScriptCompiler.hpp): the script-node runtime + the tests' compiler.
- [`HostRpcService.cpp`](../packages/native/src/host/rpc/HostRpcService.cpp), [`EngineRpcService.cpp`](../packages/native/src/host/rpc/EngineRpcService.cpp), [`DebugRpcService.cpp`](../packages/native/src/host/rpc/DebugRpcService.cpp), [`AudioDriverRpcService.cpp`](../packages/native/src/host/rpc/AudioDriverRpcService.cpp) — the RPC services behind the facets.
- [`plugin/PluginDSP.cpp`](../packages/native/plugin/PluginDSP.cpp), [`plugin/PluginShared.hpp`](../packages/native/plugin/PluginShared.hpp), [`src/main.cpp`](../packages/native/src/main.cpp), [`test/ui/UiHarness.hpp`](../packages/native/test/ui/UiHarness.hpp) — the three hosts.
- [`CMakeLists.txt`](../packages/native/CMakeLists.txt), [`plugin/DistrhoPluginInfo.h`](../packages/native/plugin/DistrhoPluginInfo.h) — build wiring + plugin identity.
