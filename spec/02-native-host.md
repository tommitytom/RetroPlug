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
`QueuedInvoker` internals, the `SnapshotRegistry` slot machinery, the core backends, and the script
host the TS nodes run in.

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
| `EngineRpcService` | emulator lifecycle / reads / MIDI / transport / render + profile | [`EngineRpcService.hpp`](../packages/native/src/host/rpc/EngineRpcService.hpp) |
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
  ([`SharedDSP`](../packages/native/plugin/PluginShared.hpp#L21)),
  so the UI reaches the backend through the already-bound `__rpcSend` — no second RPC bridge.
  The UI and window-geometry seams are 03's topic.
- **Native test host**: [`main.cpp:107-151`](../packages/native/src/main.cpp#L107-L151) binds
  the same namespace, installs a `globalThis.tjs.exit` hook that records the exit code, evals a TS
  bundle, and pumps the job loop until the harness reports TAP. This is the
  `test:native` runner (`retroplug-host`).
- **Headless UI-test host**: [`UiHarness`](../packages/native/test/ui/UiHarness.hpp#L30)
  is the plugin's control-plane bring-up **minus the audio thread, plus a display**. Because
  there is no audio thread, its
  [`advance(ms)`](../packages/native/test/ui/UiHarness.hpp#L48) explicitly
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
| `writeFile` | `(string, Bytestring) → bool` | creates parent dirs on demand ([`:89`](../packages/native/src/host/rpc/HostRpcService.cpp#L89)) |
| `writeFileAtomic` | `(string, Bytestring) → bool` | tmp + rename ([`:106`](../packages/native/src/host/rpc/HostRpcService.cpp#L106)) |
| `fileExists` / `rename` / `deleteFile` | `→ bool` | |
| `listDir` | `(string dir) → vector<string>` | filenames only |
| `drainChangedPaths` | `→ vector<string>` | pull-drain of the efsw watcher (`NativeFileWatcher`); empty until a host calls `enableWatching` (the plugin does; this test host / CLI don't) ([`:197`](../packages/native/src/host/rpc/HostRpcService.cpp#L197)) |
| `setWatchedRoms` | `(vector<string>) → bool` | register the ROM files to watch (parent dirs); recomputed by TS on every systems-list change. Config dir + `bindings/` are always watched |
| `canonicalize` | `(string) → string` | `weakly_canonical`, falls back to input |
| `configDir` | `→ string` | per-OS config path, computed in this TU (no config-persistence link) ([`:23`](../packages/native/src/host/rpc/HostRpcService.cpp#L23)) |
| `zip` | `(vector<BackendZipInput>) → Bytestring` | miniz, no-copy add ([`:226`](../packages/native/src/host/rpc/HostRpcService.cpp#L226)) |
| `unzip` | `(Bytestring) → vector<BackendZipEntry>` | |

`configDir` is computed per-OS directly in this service, so the host links no config-persistence
code (it is TS-owned — [05-data-persistence.md](05-data-persistence.md)). The `.rplg` PKZIP and
base64 composition also happen in TS; native exposes only the stateless `zip`/`unzip` primitives.
The LSDj sav codec is likewise pure TS now — it used to live here as `savFromJson` and no longer
does.

### `EngineRpcService`: emulator / MIDI / transport

Every mutation here routes through the `QueuedInvoker` except these, which call the `Engine`
directly on the control thread: `buildCore` and `setGraph` (`Engine::parkCore` / `Engine::setGraph`),
`setSampleRate` (`Engine::setSampleRate`, so it refuses while the audio thread owns the engine or the
running plan holds a system), the render calls, and the profile calls `dspResetAllocStats`,
`dspRunGc` and `dspTraceReset` (which the script host refuses while the audio thread owns its
runtime). The render calls drive `processBlock` on the calling thread themselves and refuse while the
audio thread owns the engine; `renderBlocks` also stages each block's MIDI and controller traffic on
the `Engine` itself before it drains the ring, and sets the block's bpm, transport and ppq after. Every
read of a core's state routes through the `SnapshotRegistry`. No method walks a live core.

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
| `stageMidiIn` | `(vector<uint8> bytes, uint32 frame) → bool` | any non-empty length: a channel message, a SysEx, or several messages as one run, at intra-block `frame` (TS defaults it to 0, and refuses one that is not an integer from 0 to 1023 before sending, since rpcpp would truncate it); false for a frame past `renderAudio`'s 1024-frame block |
| `drainMidiOut` | `→ vector<RpcMidiOut>` | the host MIDI out (`Engine::midiOut`, which `rp.host.midi-out` fills each block) gathered over the `renderAudio*` renders since the last drain, each message with its system and frame (`renderBlocks` returns its own) |
| `droppedMessages` | `→ uint32` | messages dropped so far (`Engine::droppedMessages`): by the plan's full output queues, and by `rp.host.midi-out` for a message too short to name its system; never reset, and it wraps, so a test compares two reads |

The table is a selection. The facets
([`BackendRpcRegistration.hpp`](../packages/native/src/host/rpc/BackendRpcRegistration.hpp)) hold 19
`host` methods, 13 `emulator`, 22 `harness` (the renders, transport and sample rate, staged MIDI in and
MIDI out, `droppedMessages`, `buildTarget`, the kit compiles and the audio runtime's profiling calls),
27 `debug` and
6 `driver`: 87 in the union. There is no kernel call: the DSP kernel is a script node every Engine
embeds, and its structure is its settings in the documents `setGraph` plans.

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
| `serialOutCapture` | `optional<string>` | arm a Game Boy's serial-out capture before its first block: `"drive"` (LSDj MI.OUT) or `"listen"` (master sync); any other name refuses the build; ignored by every other core |

## `Engine` + `QueuedInvoker`: the mutation path

The command-ring model, the `audioThreadOwns_` bit, and the release-ring ownership handoff
are defined canonically in [01-architecture.md](01-architecture.md). Here is the C++. The design
these follow is [modular-graph-phase-b1-design.md](../docs/modular-graph-phase-b1-design.md).

### `Engine`

[`Engine`](../packages/native/src/host/engine/Engine.hpp) is single-threaded and thread-unaware.
It owns the staged host MIDI (`pendingMidi_`, `pendingControllerMidi_`: flat arenas, each message a
frame and a place in one byte buffer, reserved at construction for what the input nodes carry in a
block, so staging allocates nothing until a block stages more), the host outputs the plan fills
(`midiOut_`, `controllerOut_`, which still allocate one vector per message on the block thread), the
snapshot registry, the transport fields, and the graph the systems run on: the `ScriptHost` its script
nodes (the DSP kernel among them) run in, a `NodeRegistry` holding the node packages (`rp.core`,
`rp.host`, `rp.sameboy`, `rp.mesen`, `rp.host.io`, `rp.n8`, `rp.script`), a `ResourceTable`, the
[`GraphHost`](../packages/native/src/graph/GraphHost.hpp) running the plan, the cores `buildCore`
parked until a document places them, and every live core by id. Every op runs "now, on the calling
thread"; it holds **no locks**: transport and the ppq clock are plain members mutated only by
whichever thread owns it (the pull path or the audio loop, never concurrently). The Engine's own
atomics are block-thread tallies the control thread reads (blocks too long to run and the last one's
length, `rp.host.midi-out`'s drops); the GraphHost and script host it owns add their fault, drop and
throw counts, their audio-ownership flags, the in-flight commit count and the call deadline. A removed
or displaced core is never freed on a hot path: it leaves with the plan that last ran it.

Its per-block driver `Engine::processBlock(frames, outputs, n)` is the one place every host funnels a
block. The host has already drained the ring (`drainInto` → `Engine::drainCommands` →
`GraphHost::drain`: plan swaps and commands applied in push order, a staged message landing in the
staging). Then, in order:

1. `ScriptHost::beginBlock()`: release the script instances whose shims the control thread destroyed
   since the last block (up to 16 a block), before any of this block's JS runs. An empty block stops
   here; so does one longer than the plan's buffers, which renders silence, drops what was staged for
   it and is counted, for `reclaim` to report (a host avoids it by calling `setMaxBlockFrames` with its
   buffer size, as the plugin's `activate()` does).
2. `syncTrace_.beginBlock(...)` (inert unless `RETROPLUG_SYNC_TRACE` is set), and `midiOut_` /
   `controllerOut_` cleared for the plan's output nodes to fill.
3. `GraphHost::run(frames, transport, outputs, n)`: silence every host output, then run the plan, in
   the planner's order (topological, ties broken by document order):
   1. **The host inputs and the kernel.** `rp.host.midi-in` and `rp.host.controller-in` take what the
      hosts staged (the host MIDI stable-sorted by frame). The DSP kernel, the script node
      `rp.dsp.kernel` ([kernelNode.ts](../packages/retroplug/src/script/kernelNode.ts), run by the
      [script host](../packages/native/src/host/script/ScriptHost.hpp)), reads them, the transport at
      the **block-start** `ppq_` and each Game Boy's `serialOut` of last block (a feedback wire), and
      sends each system's `serialIn`, `midi`, `bytes` and `buttons` on wires to that system node's
      inputs, its host MIDI out to `rp.host.midi-out` and its control-surface out to
      `rp.host.controller-out`.
   2. **The system nodes** ([SystemNodes.cpp](../packages/native/src/host/graph/SystemNodes.cpp):
      `rp.sameboy.gb`, `rp.mesen.nes`, `rp.mesen.gba`, `rp.mesen.sms`), each a `SystemNode<T>` over a
      core's `prepareForBlock` / `stepIfBelowTarget` / `finishBlock`. Each applies its inputs first
      (below), then runs its core. An unlinked system is one `process()` call. Linked Game Boys run as
      one coupled group over `rp.step/1`, round-robin: prepare every member in order, then passes of
      one step per member until none is below the block's target, then finish each, so their serial
      cable stays in instruction lockstep. The node zeroes the buffers it hands `finishBlock` (which
      adds into them) and sets the lane count from which of its outputs are connected
      ([10-multichannel-audio-out.md](10-multichannel-audio-out.md) section 1); a Game Boy then writes
      its `serialOut`.
   3. **The kernel's other outputs.** `rp.n8.cart-mirror`, wired from the first NES in slot order,
      hands what that system was sent to `coreByteSink_` for a physical N8 (a message staged straight
      at it, `StageSystemMidi`, reaches the sink too, at drain); `rp.host.midi-out` appends to
      `midiOut_`, which the plugin drains to the DAW after the block (the SDL standalone to its MIDI out
      port); `rp.host.controller-out` appends
      to `controllerOut_`, which the SDL standalone drains to the Launchpad link.
   4. **The mixing nodes**, `rp.core.split` and `rp.core.add`: the routing, as wiring (below).
   5. **`rp.host.audio-out`**, which copies its buses into the host's buffers (it reaches them
      through `rp_host.get_interface("rp.host.audio-buffers/1")`).
4. `ScriptHost::endBlock()`: the promise jobs the block's script nodes queued, at most 256 (the rest
   wait for the next block's end), then, in a profile build, the allocation window's block counts; a
   profile build closes the block's `apu-render` span just before ([08-profiling.md](08-profiling.md)).
   What was staged and no node of the plan took (a plan with no `rp.host.midi-in` or `controller-in`)
   is dropped, not carried into the next block.
5. `registry_.publishSystem(...)` for each system of the plan, in slot order: copy its
   freshly-published frame/state/SRAM into the owned snapshot store.
6. `syncTrace_.endBlock()`, then advance `ppq_` only if transport is running (so the kernel's
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

### Host I/O, the system nodes' inputs and queue capacities

The kernel's input and output are wires the planner sees
([modular-graph-phase-b3a-design.md](../docs/modular-graph-phase-b3a-design.md) section 3). The host
I/O nodes ([HostIoNodes.hpp](../packages/native/src/host/graph/HostIoNodes.hpp)) reach the engine
through its host service `rp.engine.host-io/1`
([HostServices.hpp](../packages/native/src/host/graph/HostServices.hpp)) and do not activate without
it:

| Node | Ports | Does |
|---|---|---|
| `rp.host.midi-in` | out `midi` (events: 2048 messages, 64 KiB) | takes the staged host MIDI, stable-sorted by frame without allocating: the plugin stages two runs in frame order (a block's DAW events, then each moved automation slot's CC at frame 0), which it merges onto its queue, a tie to the earlier run; more runs are sorted in place first, by insertion |
| `rp.host.midi-out` | in `midi` | each message is the emitting system's id, 4 bytes little-endian, then the MIDI; it lands in `Engine::midiOut` tagged with that system (a message shorter than the prefix is dropped, and counted in `Engine::droppedMessages`) |
| `rp.host.controller-in` | out `midi` (events: 1024 messages, 16 KiB), out `connected` (control) | takes the staged control-surface traffic, all at frame 0; `connected` is 1 while a surface is attached, from frame 0 of the block (a change at frame 0 on the block the flag flips, since a control output's start is the last block's end) |
| `rp.host.controller-out` | in `midi` | appends each message to `Engine::controllerOut` |
| `rp.n8.cart-mirror` | in `midi` (events), in `bytes` | settings `{"system": <id>}`; hands the core-byte sink every MIDI message, then every byte message with its flush flag, in queue order, skipping empty ones; nothing while no sink is set |

An input node takes everything staged whether or not its output is wired, so `Engine::setGraph`
refuses (`midi-in`, `controller-in`) a document with a second of either. It refuses (`cart-mirror`) a
second cart mirror too, or one whose `system` is not one of the slots' NES systems, reading the settings
with the node's own reader (`cartMirrorSystem`). The system it names goes into the plan's `SystemTable`, and
`Engine::cartMirrorSystemId()` reads it off the running plan: while it is non-zero the plugin and the
SDL standalone stop forwarding their raw host MIDI (the DAW's, or the standalone's MIDI input) to the
cart (it would arrive twice, the second copy unrouted), and a `StageSystemMidi` aimed at that system
reaches the cart too. The node's settings
change nothing it does, so it prepares statelessly and a change is a swap.

Every system node has four inputs, mapped onto core calls `SystemBase` already has. It applies them
at the top of its `prepare` (where `process` starts too, and which a linked group runs for every member
before any of them steps), each in queue order, the inputs in this order:

| Input | Kind | Core call |
|---|---|---|
| `serialIn` | bytes | `pushSerialIn(frame, byte)`, one call per byte, at its message's frame (the kernel node sends a run of same-frame bytes as one message) |
| `midi` | events | `onMidi`; a message longer than a `MidiEvent` holds goes down `pushCoreBytes`, unframed |
| `bytes` | bytes | `pushCoreBytes(frame, data, size, flush)`, the flush flag riding `RP_MESSAGE_FLUSH` (and each message handed to the sync trace) |
| `buttons` | events | `pressButton(button, down)`, from a 2-byte payload `[button, down]` |

The order shows only on the NES, where MIDI and bytes land in the same FIFO: a flushed byte message
discards the MIDI queued ahead of it. A Game Boy's `midi` input has no effect (SameBoy's `onMidi`
queues into a list nothing reads); mGB and LSDj hear MIDI through `serialIn`. The Game Boy also has a
`serialOut` output (bytes): after `finishBlock`, while it is connected, the node copies the bytes its
core captured on the serial port this block, each a message at the frame the core completed it, a byte
completed past the block's end at its last frame (the core clears its log at its next
`prepareForBlock`). Capture runs only while armed (the build-time `serialOutCapture`, or
`setSerialOutCapture`; TS arms it for LSDj's two MIDI-out modes), in one of two modes
(`SerialOutCapture`): `listen` takes the bytes the Game Boy clocks out itself (LSDj as master sync's
master), and `drive` also clocks a transfer waiting on an external clock, a bit an audio frame, taking a
byte every 8 frames while the Game Boy holds the port for one, idle bytes included (the Arduinoboy's part
in MI.OUT). Only MI.OUT drives: a stopped LSDj master waits on an external clock for another master, and
clocked, it turns slave and ignores START. The port's capacity is
fixed at 8256 messages and 8256 bytes, a byte every 8 frames over the largest block a host can run
(65536 frames) with slack, so the feedback wire to the kernel keeps its front across every swap; a CGB
clocking the port itself on its fast clock can outrun it in a very long block, and what does not fit is
dropped and counted.

An events or bytes output may declare its queue's capacity (`rp::bytes_out::sized<rp::capacity{...}>` in
C++, `events.out(capacity)` / `bytes.out(capacity)` in TS), by default 256 messages and 4096 bytes, at
most 1 Mi messages and 16 MiB. A wire carries its source's queue, so an input declares none (both parsers
refuse one, and an input has no `sized`): the outputs that do are the host inputs above, the Game Boy's
`serialOut`, and each of the kernel's outputs
([kernelNode.ts](../packages/retroplug/src/script/kernelNode.ts): per system `serialIn` 4096 messages /
72 KiB, `midi` 4096 / 72 KiB, `bytes` 256 / 1 KiB, `buttons` 64 / 128 B; `midiOut` 1024 / 8 KiB,
`controllerOut` 256 / 4 KiB). The two that pass host MIDI on, `serialIn` and `midi`, hold what
`rp.host.midi-in`'s output holds and room for a role's own bytes besides, so the one place host MIDI is
dropped is that edge; the rest are sized from what the suites send through them in one block, with
margin. Queues are allocated per plan, a plan's events and bytes queues held to a budget of 64 MiB
(`kPlanQueueBudget`; a document past it is refused, `queue-budget`), and an unwired output writes a
shared scratch queue of the default size, whose drops go uncounted. A push that does not fit is dropped
and counted on its queue; after each block the executor adds the counts to the producing instance's
atomic and the GraphHost's, `GraphHost::reclaim` names a node the first time its outputs drop, and
`Engine::droppedMessages()` (the harness RPC `droppedMessages`) reads the total, which the parity rig and
the native goldens run hold at zero.

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
- **`setGraph({ document, slots, tap? })`** (`Engine::setGraph`) first reclaims what the audio thread
  handed back (the GraphHost's plans, the script host's released instances), then checks that the
  document's system nodes and the slots name the same systems, each node `sys-<id>` with settings
  `{ "core": <id> }`, of the node type its core is placed as, that a tap arm names one of the slots and
  a mode from 1 to 3, that there is at most one `rp.host.midi-in` and one `rp.host.controller-in`, and at
  most one `rp.n8.cart-mirror`, naming one of the slots' NES systems (any of these refuses the document
  before anything is built); builds the plan's `SystemTable` (a slot-ordered list of
  `{ id, SystemBase*, MesenNesSystem* }`, the NES pointer for the channel tap, plus the system the cart mirrors,
  [SystemTable.hpp](../packages/native/src/host/graph/SystemTable.hpp)) from the slots and the live cores;
  turns `tap` into the swap's `SyncTap` trailer; and hands both to `GraphHost::setGraph`, which computes
  every node's layout from the document (`layout_for` on each node type), runs every layout check, and
  only then creates and activates the new instances. The table rides with the plan, so the audio
  thread's lookups always match the plan that is running. Once a document has published, a core still
  parked is one no document placed, and is freed with its snapshot slot. A refused document changes
  nothing: a core a node of the refused plan had taken goes back to parked, for a corrected document to
  place.

The document `buildGraph` writes:

- **Order.** `rp.host.midi-in`, `rp.host.controller-in` and the kernel node, then the unlinked systems
  by slot, then the link groups in order of first appearance (members by slot, one `link` wire chaining
  each member to the next), then `rp.n8.cart-mirror` when there is a NES, `rp.host.midi-out` and
  `rp.host.controller-out`, then the mixing nodes, then `rp.host.audio-out`. The kernel runs before the
  systems because they read its outputs, not because of where it is listed (a Game Boy's `serialOut`
  reaches it over a feedback wire, which orders nothing). The planner breaks ties by document order, so
  this is also the order the plan runs in. Node ids are `midi-in`, `controller-in`, `kernel`, `cart`,
  `midi-out`, `controller-out`, `out`, `sys-<id>` and names derived from a system id (`split-<id>`,
  `stem-<id>-<k>`) or a host channel (`lane-<c>`), never from position, so a system keeps its instance
  across edits. Link groups are Game Boys only, bucketed by nonzero link-group id in slot order, a
  group of fewer than two dissolved.
- **The kernel.** Its settings are the structure: `{ project, systems: [{ id, pipeline, serialOut }] }`,
  the systems in slot order (positional MIDI routing reads it), each pipeline the roles the kernel runs
  for that system, `serialOut` true for a Game Boy. Its wires: `midi-in.midi`, `controller-in.midi` and
  `controller-in.connected` to its `midiIn`, `controllerIn` and `controllerConnected`; its
  `sys-<id>.serialIn` / `.midi` / `.bytes` / `.buttons` to the system node's input of the same name; each
  Game Boy's `serialOut` back to its `sys-<id>.serialOut` as a feedback wire (MI.OUT's one block of
  latency); the first NES's `sys-<id>.midi` and `.bytes` to the cart mirror as well; its `midiOut` and
  `controllerOut` to `midi-out` and `controller-out`. Its ports are named by system id, so a structure
  change reshapes it: the instance is kept and a surviving system's wires stay bound
  ([04-roles-dsp-kernel.md](04-roles-dsp-kernel.md)).
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
| `StageMidi` | `uint8 data[4]`, `len`, or `vector<uint8>* ext`, `frame` | inline for a message of up to 4 bytes; a longer one (a SysEx) owning; applied into the engine's staging, which `rp.host.midi-in` takes |
| `StageControllerMidi` | `uint8 data[4]`, `len` | inline only; a message over 4 bytes is refused before it is pushed |
| `StageSystemMidi` | `id`, `data[4]` / `len` or `ext` | as `StageMidi`, delivered at drain to one system's ingress (and to the cart when the plan's cart mirror names that system), never through the kernel |
| `SetBpm` / `SetTransport` / `SetPpq` | `double` / `bool` / `double` | inline |
| `SetConfigField` | `id`, `field`, `value` | inline |
| `PressButton` | `id`, `button`, `down` | inline |
| `WriteRam` | `id`, `offset`, `vector<uint8>* bytes` | owning |
| `SyncTap` | `arm`, `mode`, `id` | inline; rides in the plan's swap command as a trailer for every `setGraph` request carrying a `tap` |

Heavy, rare payloads (RAM writes, long MIDI) cross as raw **owning** pointers: the accepted
non-RT-on-rare-op pattern. At drain the GraphHost hands each host command to `Engine::onHostCommand`,
which applies it (`applyCommand`) against the `SystemTable` of the plan current at that point, and
`delete`s owning payloads after apply. `SyncTap` is the NES channel-tap
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
[`SystemBackend::build`](../packages/native/src/system/SystemFactory.hpp#L33) runs on the
control thread (heavy, non-RT) and returns an **already-`onActivate`'d** system or nullptr. The
wire→build mapping is
[`toBuildSpec`](../packages/native/src/host/rpc/EngineRpcService.cpp): `core` defaults
from `platform` via
[`defaultCoreFor`](../packages/native/src/host/rpc/EngineRpcService.cpp) (nes/gba → mesen,
else sameboy); zip-import seed bytes win over on-disk paths; `settings` passes through unchanged.

### `SameBoyBackend`

[`SameBoyBackend::build`](../packages/native/src/system/sameboy/SameBoyBackend.cpp#L35) resolves the
ROM (an embedded marker via `rp::embeddedRom` is SameBoy by fiat; a file-backed ROM is slurped
and gated on `detectRomFormat == RomFormat::Gb`, so a non-GB file is rejected). It builds a
`SameBoyConfig`, then decodes the opaque `settings` blob into a
[`SameBoyRoleConfig`](../packages/native/src/system/sameboy/SameBoyBackend.hpp#L18) and applies
model / highpass / linkGroupId / fastBoot **at construct**
([`:69-72`](../packages/native/src/system/sameboy/SameBoyBackend.cpp#L69)): before `onActivate`, so a
loaded non-default model doesn't trigger a post-build restart that would nuke the just-restored
savestate. The shared build sequence
[`buildSameBoy`](../packages/native/src/system/sameboy/SameBoyBackend.cpp#L24) is the critical seam:

```cpp
auto sys = std::make_unique<SameBoySystem>(id, std::move(cfg), std::move(romBytes));
sys->onActivate(sampleRate);       // boots gb_ + restores sram then savestate
sys->enableStateSnapshot();        // tear-free savestate each block for the registry / Duplicate
```

The cores are built **bare** — no feature behaviour is baked into C++. The DSP-thread behaviours
(LSDj sync, mGB, Arduinoboy) run in the TS DSP kernel; kit-patch instead runs on the UI thread
(writing patched memory regions into the core), not in the kernel. See 04. The system-role config
schema (`SameBoyRoleConfig` field names) mirrors TS's
`coreRoles.ts`; [`decodeSameBoyRoleConfig`](../packages/native/src/system/sameboy/SameBoyBackend.cpp#L19)
reads with `rfl::DefaultIfMissing` (forward-tolerant) and is the single JSON decode point,
shared by live `applyRoleConfig` and the construct blob. The role model is 04's topic.

### `MesenBackend`

[`MesenBackend::build`](../packages/native/src/system/mesen/MesenBackend.cpp#L43) is one backend
serving two platforms. It slurps the ROM and dispatches on `spec.platform`: `"nes"` gates
`RomFormat::Nes` and builds a `MesenNesSystem`; `"gba"` gates `RomFormat::Gba` and builds a
`MesenGbaSystem` (empty `biosPath` → HLE boot ROM). A mislabelled ROM or an unserved platform
returns nullptr. Both call `onActivate` then `enableStateSnapshot`
([`MesenBackend.cpp:25-27`](../packages/native/src/system/mesen/MesenBackend.cpp#L25)), so NES/GBA
cores publish a tear-free savestate for the registry / Duplicate just like SameBoy. The `settings`
blob carries the NES role-config (Region / Remove Sprite Limit / channel-export mode).

## `ScriptHost` + `ScriptCompiler`: the script-node runtime

Native runs the TypeScript DSP role kernel but understands nothing about roles. The kernel itself
([`dspKernel.ts`](../packages/retroplug/src/dspKernel.ts)) and its behaviour are 04's topic; it
runs as a script node ([`kernelNode.ts`](../packages/retroplug/src/script/kernelNode.ts)), and this
doc covers the C++ runtime under it (docs/modular-graph-phase-b3a-design.md section 2).

[`ScriptHost`](../packages/native/src/host/script/ScriptHost.hpp) owns **two bare QuickJS
runtimes** (no txiki; see [01-architecture.md](01-architecture.md)), one pair per Engine, sharing no
JS value: a control-side one that only checks settings and lays out a node's ports, and the
audio-side one that holds every instance and runs `process`.

- **The bundle.** Both run the script-node bundle
  ([`bundle.ts`](../packages/retroplug/src/script/bundle.ts)), esbuilt to a global-code IIFE and
  compiled to bytecode at build time by the `retroplug-script-bundle` target
  ([bundles.cmake](../packages/native/cmake/bundles.cmake)), whose derived C array is compiled into
  `retroplug-backend`: every Engine loads it at construction, so every host has the same node types
  with no boot call. The bundle appends its `defineNode` results to the global `__rp_scriptNodes`
  ([register.ts](../packages/retroplug/src/script/register.ts)); the host reads that list in both
  runtimes, requires the same id and descriptor at each index, and registers one `rp_node_type` per
  definition in the package `rp.script`, all served by the same functions, each type telling itself
  apart by its `ctx`. A bundle that does not load, or one definition that cannot be read, registers no
  type, logged, and every document naming one is refused (`unknown-type`); a second load is refused.
  The native tests build an Engine with toy bundles of their own instead.
- **Settings and layout** (control side, the planning thread). `layout_for` and `prepare_settings` run
  the definition's `parseSettings` and its `layout` on the settings alone, so the planner can check a
  whole document before it creates anything. Every script type declares `RP_TYPE_STATELESS_PREPARE`,
  since that prepare reads nothing a commit writes, so `setGraph` never waits on a script node's
  commit. A definition that `reshapes` answers `RP_PREPARE_RESHAPE` for every change it accepts (the
  planner keeps the instance, binds the new plan's ports to it by id, and commits the settings at the
  swap); one that does not answers `RP_PREPARE_SWAP` while its layout is the one it was created with,
  and `RP_PREPARE_REBUILD` once it moves.
- **Instances.** A shim's `create` and `activate` run on the control thread and only allocate the shim
  and its record. Its JS instance is made on the block thread at the first call the shim gets there,
  which is the `ports_changed` the planner sends every fresh instance when its plan is adopted, so
  before that plan's first `process`; the definition's `create` gets `{ settings, sampleRate,
  maxFrames }`, the settings in the JSON form the control side's `parseSettings` returned (the audio
  side never runs the schema). A commit hands the
  instance its new settings through its `onSettings` (and, for a reshape, its new layout). A live
  instance is re-activated only while quiescent. The shim's `destroy` (control thread) hands the record
  to the script host: with no audio thread running it is released there and then; otherwise it is
  queued on a 256-slot SPSC ring (a control-side backlog behind it) that `beginBlock` drains at the
  top of later blocks, 16 a block, freeing the JS state on the block thread, and the record comes back
  on a second ring for `reclaim` (control thread) to delete, so the audio thread frees nothing native.
  What released instances, and the bundle's load, leave in cycles is collected while no block runs: at
  the reclaim that ends a quiescent operation (the Engine's after every `setGraph`) and when the audio
  thread hands the runtime back.
- **The block.** A node's `process` gets one `Block` object, the same every block: `messages(port)` a
  reused array whose payloads are copied into one JS-owned buffer per port, `control(id)` a reused
  `{ start, changes }`, `audio(port)` a reused `Float32Array[]` over the instance's own JS-owned copy of
  each channel (an input's samples copied in before `process`, an output's copied out after it, one
  copy per channel per block; the buffers made when the port first runs and again only for a larger
  block or one JS gave away), and `send` / `emit` writing the plan's output queues directly, a send past
  a queue's capacity refused and counted. No plan memory reaches JS, so nothing a node keeps, transfers
  or writes can reach a buffer another node reads or a plan the control thread frees. Ports are found
  through atoms made when the instance or its new layout is bound, so a warm block allocates nothing
  native, and a node written with indexed loops nothing in JS either. So **a `JSValue` never crosses
  out**. The promise jobs a block's JS queues run at its end (`endBlock`), at most 256. The
  **drift-exact PPQ tick clock lives entirely in the JS kernel** (`walkTicks`); native passes only the
  block's transport in.
- **A throw** in `process` is caught and counted, the block's output up to it stands, and the node runs
  again next block; a definition that says `faultOnThrow`, or a `process` that returns `"fault"`,
  faults instead (bypassed from then on), and a `faultOnThrow` node whose `onSettings`,
  `portsChanged` or `activate` threw faults at its next block. Throws in `create`, `onSettings`,
  `portsChanged` and `activate`, and a job that fails (a `queueMicrotask` callback that threw, or a job
  stopped by the deadline or out of memory), are counted too (`Engine::scriptThrows()`; a promise
  reaction's own throw only rejects its promise); the first one's text is kept on the block thread in a
  fixed buffer and logged once by `reclaim`. A write the block path makes onto what a node holds that
  fails (a setter the node hung there threw or ran past the deadline, or it froze the object) is the
  node's throw too: `messages()` and `control()` throw it, and on its audio views, the `Block` or its
  transport, written before `process` is called, it is a throw in `process` whose block does not run.
  Every node shares one realm, so the host keeps its writes, and its reads of a node's methods, off the
  shared prototypes: each field it fills is the object's own data property from when it makes the object
  (a define, which never walks the prototype chain), and each array slot is set when it is the array's
  own at that write and defined when it is not (never made, or taken back by a shift, a splice or a
  shorter length). So only the node's own setters run there, never an accessor another node (or the
  bundle) hung on `Object.prototype` or `Array.prototype`, with one gap: a field the node deletes is set
  again, which walks the chain, as does the read of a message's `data` it deleted (asking at every field
  write would cost each script call about 12 ns a block, 14% of an empty pass-through's). `process` and
  the hooks are read from what `create` returned and the prototypes it inherits them through, short of
  `Object.prototype`, so none is run or adopted from there. A Proxy on the way answers as JS reads one
  (its `get` trap, or its target's chain), so a wrapper or a test double works; a chain longer than
  `ScriptHost::kMethodHops` (1024) plain objects, or one that cycles back through a Proxy, fails the
  `create` rather than holding the block thread. The `Block` prototype every node reaches is frozen with
  its methods (a profile build binds the trace globals read-only and frozen too), so no node replaces one
  for another: assigning one on a node's own `Block` (`block.send = f`) throws in the strict bundle,
  where `Object.defineProperty` on it still works. That is the host's part only: what nodes can do to
  each other through the shared realm (an accessor on a prototype another's own code reads through, a
  global it calls) is left to one context per node ([07-remaining-work.md](07-remaining-work.md)).
- **The call deadline.** Every call the block thread makes into a node's JS (and `endBlock`'s promise
  jobs, together) is held to a deadline of the calling thread's CPU time (2 s by default,
  `setCallDeadline`), so a stall that is not the node's own never trips it. A call that runs past it is
  stopped and counted as a throw: a `faultOnThrow` node faults, any other runs again and faults only if
  its next call is stopped too. Both runtimes' JS stacks are held to 256 KiB, under a macOS secondary
  thread's 512 KiB.

Every entry into a runtime re-anchors with `JS_UpdateStackTop` because the driving thread differs
(the control thread on the pull path, the audio thread while running), and QuickJS's stack-overflow
guard is calibrated against the thread that last entered it. A profile build profiles the audio
runtime there ([08-profiling.md](08-profiling.md)).

[`ScriptCompiler`](../packages/native/src/host/dsp/ScriptCompiler.hpp#L18) (`dsp::compileToBytecode`)
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
`rp.host` package's `rp.host.audio-out`, are the `retroplug-graph` / `retroplug-graph-exec` libs
(`src/graph/`), and the engine's own nodes (`src/host/graph/`: the system nodes, the host MIDI and
control-surface I/O, the cart mirror) and the script host (`src/host/script/`) compile into
`retroplug-backend`.

ROM classification (the old C++ `RomSniffer`) and the LSDj sav codec are TS-owned — neither is in
the C++ tree.

The txiki host `TjsHostRuntime` comes from
[`deps/dpf.js`](../deps/dpf.js), not the shared core.

## Build & identity

The [`retroplug-backend`](../packages/native/CMakeLists.txt) static lib wraps
[`retroplug-core`](../packages/native/CMakeLists.txt) with the `Engine` (and links
`retroplug-graph`, the graph it runs on), the RPC services, the script host with its embedded
script-node bundle, and the txiki host, and is PIC (it links into the shared plugin module). The
test host `retroplug-host` is that lib + `main.cpp`; the DPF plugin is
[`dpf_add_plugin(retroplug TARGETS clap vst3 vst2 au jack)`](../packages/native/CMakeLists.txt)
(AU is macOS-only). The control-plane and UI bytecode bundles are built by
`tools/build-controlplane.js` + `tools/build-ui.js` piped through `tjsc`, and the script-node bundle
by `tools/build-session.js` (`retroplug-script-bundle`); all are **derived, never committed**.
[06-build-test.md](06-build-test.md) is the full build/verify story.

Plugin identity: name `RetroPlug`, CLAP id
`net.tommitytom.retroplug`, `UNIQUE_ID RPlg` / `BRAND Tmtt`,
**0 inputs / 8 outputs = four stereo pairs `out_1..4`**
([`DistrhoPluginInfo.h:10-11`](../packages/native/plugin/DistrhoPluginInfo.h#L10)),
IS_SYNTH, HAS_UI (the React/LVGL editor, 480×432, user-resizable), FILE_BROWSER, MIDI in+out,
TIMEPOS, STATE + FULL_STATE, and DIRECT_ACCESS (the in-process editor↔DSP handoff). Master gain
is one automatable parameter applied post-render across all 8 channels
([`PluginDSP.cpp`](../packages/native/plugin/PluginDSP.cpp)).

## Not yet built / deferred

- **Live memory-region subscription.** There is no live "watch RAM" streaming path
  (`enableMemorySnapshot(type)`). Reads are one-shot through the registry read door; if live
  memory streaming to the UI is ever needed, that arming seam must be built.
- **Per-block buttons/keys into the kernel.** The kernel node hands its kernel empty `buttons` and
  `keys` arrays every block ([`kernelNode.ts`](../packages/retroplug/src/script/kernelNode.ts)): it has
  no input port for them, and no host feeds them yet (this blocks the raw LSDj Keyboard mode,
  [07-remaining-work.md](07-remaining-work.md)).
- **The snapshot double-copy collapse** (§`SnapshotRegistry`) is a documented redundancy, not a
  bug — a pending refactor, see [07-remaining-work.md](07-remaining-work.md).

## Key files

- [`BackendRpcRegistration.hpp`](../packages/native/src/host/rpc/BackendRpcRegistration.hpp) — mounts the capability facets onto a server; the single source of truth for the wire surface.
- [`CoreBackends.cpp`](../packages/native/src/system/CoreBackends.cpp) — registers the `sameboy` / `mesen` backends into the `SystemFactory`.
- [`Engine.hpp`](../packages/native/src/host/engine/Engine.hpp) / [`.cpp`](../packages/native/src/host/engine/Engine.cpp): the single-threaded owner of the graph (with its script host), the staged host MIDI, the host outputs and the registry; `processBlock`, `setGraph`, the command handler, `SyncTap`.
- [`EngineInvoker.hpp`](../packages/native/src/host/engine/EngineInvoker.hpp) / [`.cpp`](../packages/native/src/host/engine/EngineInvoker.cpp): `QueuedInvoker`, the one command path; [`DspCommand.hpp`](../packages/native/src/host/engine/DspCommand.hpp).
- [`host/graph/`](../packages/native/src/host/graph): [`SystemNodes.cpp`](../packages/native/src/host/graph/SystemNodes.cpp) (the cores as nodes), [`HostIoNodes.cpp`](../packages/native/src/host/graph/HostIoNodes.cpp) (the host I/O nodes and the cart mirror), [`SystemTable.hpp`](../packages/native/src/host/graph/SystemTable.hpp), [`HostServices.hpp`](../packages/native/src/host/graph/HostServices.hpp).
- [`GraphHost.hpp`](../packages/native/src/graph/GraphHost.hpp) / [`.cpp`](../packages/native/src/graph/GraphHost.cpp), [`rp_node.h`](../packages/native/src/graph/rp_node.h): the one ring, plan swap and reclaim; the node ABI (`layout_for` per type).
- [`SnapshotRegistry.hpp`](../packages/native/src/host/engine/SnapshotRegistry.hpp) / [`.cpp`](../packages/native/src/host/engine/SnapshotRegistry.cpp) — the owned read door.
- [`SystemFactory.hpp`](../packages/native/src/system/SystemFactory.hpp), [`SameBoyBackend.cpp`](../packages/native/src/system/sameboy/SameBoyBackend.cpp), [`MesenBackend.cpp`](../packages/native/src/system/mesen/MesenBackend.cpp) — the build path.
- [`ScriptHost.hpp`](../packages/native/src/host/script/ScriptHost.hpp) / [`.cpp`](../packages/native/src/host/script/ScriptHost.cpp), [`ScriptCompiler.hpp`](../packages/native/src/host/dsp/ScriptCompiler.hpp): the script-node runtime + the tests' compiler; [`src/script/`](../packages/retroplug/src/script) (`bundle.ts`, `register.ts`, `kernelNode.ts`): the bundle every Engine embeds, how it registers its nodes, and the DSP kernel as a script node.
- [`HostRpcService.cpp`](../packages/native/src/host/rpc/HostRpcService.cpp), [`EngineRpcService.cpp`](../packages/native/src/host/rpc/EngineRpcService.cpp), [`DebugRpcService.cpp`](../packages/native/src/host/rpc/DebugRpcService.cpp), [`AudioDriverRpcService.cpp`](../packages/native/src/host/rpc/AudioDriverRpcService.cpp) — the RPC services behind the facets.
- [`plugin/PluginDSP.cpp`](../packages/native/plugin/PluginDSP.cpp), [`plugin/PluginShared.hpp`](../packages/native/plugin/PluginShared.hpp), [`src/main.cpp`](../packages/native/src/main.cpp), [`test/ui/UiHarness.hpp`](../packages/native/test/ui/UiHarness.hpp) — the three hosts.
- [`CMakeLists.txt`](../packages/native/CMakeLists.txt), [`plugin/DistrhoPluginInfo.h`](../packages/native/plugin/DistrhoPluginInfo.h) — build wiring + plugin identity.
