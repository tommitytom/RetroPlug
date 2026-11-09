# 02 — The native host (`packages/native/`)

This is the C++ deep dive for the native half. It implements the thesis's
first clause — **native owns bytes and cores** — behind one narrow RPC surface. TypeScript
drives everything here over resolved paths, opaque byte buffers, and the graph documents it builds;
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
> - `HostServices.hpp` - the service graph itself (Engine, which builds a core through the build function its system
>   node's request carries, the one QueuedInvoker, `HostRpcService`, `EngineRpcService`). Codec-agnostic, which
>   is what lets the Node addon share it.
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
[`Engine`](../packages/native/src/host/engine/Engine.hpp) (which owns no console and no backend registry: a core is
built by the build function the node's request carries, in the [`system`](../packages/system/native/CMakeLists.txt)
package's [`SystemBuild.hpp`](../packages/system/native/src/system/SystemBuild.hpp)) and the ONE
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
| `syncFile` | `(string) → bool` | `rp::fsyncPath`: a streamed file's data to the device before it is renamed into place (a render's `.part`) |
| `listDir` | `(string dir) → vector<string>` | filenames only (on Windows, one the ANSI code page cannot hold is left out: no path in that code page names it) |
| `drainChangedPaths` | `→ vector<string>` | pull-drain of the efsw watcher (`NativeFileWatcher`); empty until a host calls `enableWatching` (the plugin does; this test host / CLI don't) ([`:210`](../packages/native/src/host/rpc/HostRpcService.cpp#L210)) |
| `setWatchedRoms` | `(vector<string>) → bool` | register the ROM files to watch (parent dirs); recomputed by TS on every systems-list change. Config dir + `bindings/` are always watched |
| `canonicalize` | `(string) → string` | `weakly_canonical`, falls back to input |
| `configDir` | `→ string` | per-OS config path, computed in this TU (no config-persistence link) ([`:23`](../packages/native/src/host/rpc/HostRpcService.cpp#L23)) |
| `zip` | `(vector<BackendZipInput>) → Bytestring` | miniz, no-copy add ([`:239`](../packages/native/src/host/rpc/HostRpcService.cpp#L239)) |
| `unzip` | `(Bytestring) → vector<BackendZipEntry>` | |

`configDir` is computed per-OS directly in this service, so the host links no config-persistence
code (it is TS-owned — [05-data-persistence.md](05-data-persistence.md)). The `.rplg` PKZIP and
base64 composition also happen in TS; native exposes only the stateless `zip`/`unzip` primitives.
The LSDj sav codec is likewise pure TS now — it used to live here as `savFromJson` and no longer
does.

### `EngineRpcService`: emulator / MIDI / transport

Every mutation here routes through the `QueuedInvoker` except these, which call the `Engine`
directly on the control thread: `putResource`, `releaseResource`, `setGraph`, `loadScriptPack` and
`setParam` (`Engine::putResource` / `Engine::releaseResource`, whose table only the control thread touches,
`Engine::setGraph` / `Engine::loadScriptPack` / `Engine::setParam`, which pushes onto the
GraphHost's ring itself),
`setSampleRate` (`Engine::setSampleRate`, so it refuses while the audio thread owns the engine or the
running plan holds a system), the render calls, and the profile calls `dspResetAllocStats`,
`dspRunGc` and `dspTraceReset` (which the script host refuses while the audio thread owns its
runtime). The render calls drive `processBlock` on the calling thread themselves and refuse while the
audio thread owns the engine; `renderBlocks` also stages each block's MIDI on the `Engine` itself and its
controller traffic into the harness surface link before it drains the ring, and sets the block's bpm,
transport and ppq after. Every
read of a core's state routes through the `SnapshotRegistry`. No method walks a live core.

| Method | Signature | Behaviour |
|---|---|---|
| `putResource` | `(BackendResourceSpec) → RpcPutResourceResult` | an immutable byte resource for the engine's `ResourceTable` (`Engine::putResource`): `type` is `rom`, `sram` or `state` and the payload is `bytes`, `path` (read here, so a 32 MB ROM or a `.sav` never crosses the bridge) or `embedded` (a baked-in ROM's marker, `mgb`). Answers `{id}`; `{error}` for a type a node would not read, no source or more than one, a file that is absent or unreadable, an empty payload for a `rom` (an empty `sram` is a blank battery, whether TS hands the bytes or the file it read is zero bytes), or an unknown marker |
| `releaseResource` | `(uint32 id) → bool` | the owner's drop, idempotent. A resource a plan or a live node still pins stays readable and is freed when the last pin goes |
| `setGraph` | `(RpcSetGraphRequest) → RpcSetGraphResult` | a graph document TS built, its slots and its tap re-arm → `Engine::setGraph` (below) |
| `loadScriptPack` | `(RpcLoadScriptPackRequest) → RpcLoadScriptPackResult` | a pack of script node types, by its id, its source and the memory limit each of its runtimes is held to (`memoryLimitBytes`, a double, absent or 0 for 64 MiB, refused unless a whole number of bytes) → `Engine::loadScriptPack`, which loads it or, for an id loaded already, reloads it (the script host, below); returns `ok`, `error` (empty on success), the planner's `errors` when a reload's replan refused (empty for a refusal before any replan) and the pack's `types`, each with its descriptor JSON |
| `setParam` | `(string nodeId, string paramId, double value) → bool` | a live change to a param of a node the last published plan places (`Engine::setParam` → `GraphHost::setParam`): clamped to the param's range (rounded for an int, enum or bool) and applied at the start of the next block, on the command ring. The store sends a system's `gain` and a NES's `expansionVolume` to its node, `sys-<id>`, this way, with no publish. False for a node or param that plan does not have (a node the next document places takes its value from that document), a NaN, a param a wire drives, or a full ring |
| `readState` | `(uint32 id) → optional<Bytestring>` | published savestate from the registry |
| `readSram` | `(uint32 id) → optional<Bytestring>` | SRAM sliced from the published savestate |
| `screenshot` | `(uint32 id, string path) → bool` | encodes the registry frame to PNG |
| `getFrame` | `(uint32 id) → RpcFrame` | registry frame, raw XRGB8888 |
| `pressButton` | `(uint32 id, uint32 button, bool down) → bool` | joypad transition → the addressed core |
| `renderAudio` | `(double ms) → Bytestring` | synchronous pull render, interleaved f32 L/R, into a graph TS built for 2 host channels; empty while the audio thread owns the engine or the running graph writes another width (as are `renderAudioPerSystem` / `renderAudioPerChannel`, for 2 channels per system or stream) |
| `setTransport` / `setBpm` | `→ bool` | queued transport ops |
| `stageMidiIn` | `(vector<uint8> bytes, uint32 frame) → bool` | any non-empty length: a channel message, a SysEx, or several messages as one run, at intra-block `frame` (TS defaults it to 0, and refuses one that is not an integer from 0 to 1023 before sending, since rpcpp would truncate it); false for a frame past `renderAudio`'s 1024-frame block |
| `stageNodeMidi` | `(string nodeId, vector<uint8> bytes) → bool` | the UI's staged message, at the node the running plan places under `nodeId` (`rp.stage/1`): a system node hands it to its core's ingress, the cart node pushes it at its link. False for an empty message; the id resolves at the drain against the plan that runs, and a message no node there takes (an id the plan does not place, a node that stages nothing) is dropped there |
| `submitJob` | `(RpcJobRequest) → RpcJobSubmitResult` | a capability job for a node of the last published plan: `{id}` when it was taken, `{error}` when it was refused (a capability the node does not present, an operation it does not have, a device's "busy" or "not connected", the engine's 64-outstanding bound, an emulated restore). A read or a ROM header completes before it returns; a write at the next block boundary, or at once while no audio thread runs (the CLI between renders); a device's job on its own thread, after a cart submission has waited on the calling thread (the DAW's main thread in the plugin) up to the probe's 1 s bound for the cart's menu check, usually one USB round trip |
| `drainJobs` | `→ vector<RpcJobCompletion>` | what finished since the last drain (`done`, `ok`, the result's bytes or the error) and every running job whose progress changed, with `progress` of `total` |
| `submitOfflineJob` | `(RpcOfflineRequest) → RpcOfflineSubmitResult` | a graph document run to completion on a host of the engine's offline pool (`Engine::submitOfflineJob`): `{id}`, or `{error}` for a rate a `GraphHost` refuses, more than 64 inputs and outputs, an input that is neither samples nor a file, an output of no frames, a return naming no handle, or 64 jobs outstanding. A document may name only the built-in C++ packages. What fails once it runs (a file that will not decode, a planner refusal, an activate error, a node fault, a cancel) is the completion's error |
| `submitEngineJob` | `(RpcEngineJobRequest) → RpcOfflineSubmitResult` | an engine job (D3): `{kind, spec}` with `kind` exactly `"render"` and `spec` the render job JSON, run on a pool worker by a `RenderHost` and an `Engine` of its own (`Engine::submitEngineJob`). `{id}`, or `{error}` for an unknown kind (naming it) or 64 jobs outstanding; a bad spec is the job's own error. Ids are shared with graph jobs |
| `drainOfflineJobs` | `(double waitMs) → RpcOfflineDrain` | the completions finished since the last drain, oldest first (each buffer's samples, rate, channels and `nonFinite`, the file inputs' reports, `guardsFired` / `guardReason`), plus `inFlight`, the jobs still queued or running. A drain also fills `engineCompletions` (`{id, status "done"/"cancelled"/"error", message, outputs, renderedMs, lines, droppedLines}`) and `engineProgress` (every running engine job's rendered milliseconds and its console lines since the last drain, each a stream and text). With `waitMs` it blocks up to that (at most 1000 ms) when nothing has finished, and returns at once when no job is queued or running |
| `cancelOfflineJob` | `(uint32 id) → bool` | stop job `id` of either kind: a queued one completes now, unrun, a running graph job at its next block boundary (or as it finishes), a running render at its next boot step or 100 ms chunk (a script that has not seen it within two seconds is interrupted, `RenderHost::kCancelGrace`), both with the reason `cancelled` (a render's cancel wins over its finishing first). False for an id neither queued nor running |
| `drainMidiOut` | `→ vector<RpcMidiOut>` | the host MIDI out (`Engine::midiOut`, which `rp.host.midi-out` fills each block) gathered over the `renderAudio*` renders since the last drain, each message with its system and frame (`renderBlocks` returns its own) |
| `droppedMessages` | `→ uint32` | messages dropped so far (`Engine::droppedMessages`) by the plan's full output queues; never reset, and it wraps, so a test compares two reads |

The table is a selection. The facets
([`BackendRpcRegistration.hpp`](../packages/native/src/host/rpc/BackendRpcRegistration.hpp)) hold 20
`host` methods, 19 `emulator` (the resources, the graph and script packs, the snapshot reads and writes, the
capability jobs, the offline and engine jobs and the node staging), 20 `harness` (the renders, transport
and sample rate, staged
MIDI in and MIDI out, `droppedMessages`, `buildTarget`, and the audio runtimes' profiling calls),
27 `debug` and
6 `driver`: 92 in the union. There is no call for what the roles do on the audio thread: their protocol
nodes are script node types every Engine embeds, and each node's settings ride the documents `setGraph`
plans. Nor is there one for a system's knobs: they are its system node's settings, in the same documents,
and its params, whose live edits are `setParam`
([modular-graph-phase-c2-design.md](../docs/modular-graph-phase-c2-design.md)).

`duplicate`, `reload` and `remove` are **not** native methods: they are TS orchestration over
resources, the registry reads and the documents the stores publish
([`EngineRpcService.cpp`](../packages/native/src/host/rpc/EngineRpcService.cpp)):
duplicate pulls a source savestate and puts it as a `state` seed for a new system node whose ROM is put
again from its source; reload puts the ROM again and seeds the battery from `readSram`; remove publishes a
document without the system and releases its resources once that document has planned. Routing is not a
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

### `BackendResourceSpec` (the resource DTO)

[`BackendResourceSpec`](../packages/native/src/host/rpc/BackendTypes.hpp) is the one
DTO worth spelling out. `putResource` takes exactly one source of the bytes, and answers
`RpcPutResourceResult` (`{ id }` or `{ error }`, exactly one of them):

| Field | Type | Meaning |
|---|---|---|
| `type` | `string` | `"rom"`, `"sram"` or `"state"`: what a node reads a resource as, and the type a `rp::resource_ref<Type>` field demands |
| `bytes` | `optional<Bytestring>` | the image TS holds: a patched ROM, a seed read back from a live core, a project's blob |
| `path` | `optional<string>` | a file the engine reads itself, so an unpatched ROM or a `.sav` never crosses the bridge (a GBA ROM can be 32 MB) |
| `embedded` | `optional<string>` | a baked-in ROM's marker: an id a package's manifest embeds (`retroplug.embed`), which the product's `rp_embedded_resources()` table holds while it lists that package, so `mgb` is the mGB synth's ROM ([mgb](../packages/mgb/package.json)) |

A type a node would not read, no source or more than one, a file that is absent or unreadable, an empty payload
for a `rom` (an empty `sram` is a blank battery, and so is a `.sav` file that opens and holds nothing) and an
unknown marker are each answered here, before any document is written. Ids count up from 1 and are never reused, so a
stale `"res:N"` is refused as unknown rather than binding whatever took its place. A resource may be empty (a blank
battery is one), and a node that reads one gets a valid data pointer at size 0, since a consumer forms a range from
`(data, data + size)`.

## `Engine` + `QueuedInvoker`: the mutation path

The command-ring model, the `audioThreadOwns_` bit, and the release-ring ownership handoff
are defined canonically in [01-architecture.md](01-architecture.md). Here is the C++. The design
these follow is [modular-graph-phase-b1-design.md](../docs/modular-graph-phase-b1-design.md).

### `Engine`

[`Engine`](../packages/native/src/host/engine/Engine.hpp) is single-threaded and thread-unaware.
It owns the staged host MIDI (`pendingMidi_`: a flat arena, each message a frame and a place in one
byte buffer, reserved at construction for what `rp.host.midi-in` carries in a block, so staging
allocates nothing until a block stages more), the host MIDI out the plan fills (`midiOut_`, which still
allocates one vector per message on the block thread), the capability jobs and their completions
(`host/engine/CapabilityJobs.hpp`), the device links its host installed, the snapshot registry, the
transport fields, and the graph the systems run on: the `ScriptHost` its script
nodes (the MIDI routing, the controller session and each role's protocol node) run in, a `NodeRegistry`
holding the node packages (`rp.core`,
`rp.host`, `rp.sameboy`, `rp.mesen`, `rp.host.io`, `rp.n8`, `rp.script`, and each script node pack
`loadScriptPack` loaded), a `ResourceTable`, the category a system node is recognised by (its descriptor's), the
[`GraphHost`](../packages/graph-runtime/native/src/graph/GraphHost.hpp) running the plan, and every live core by id. Every op
runs "now, on the calling thread"; it holds **no locks**: transport and the ppq clock are plain members mutated
only by whichever thread owns it (the pull path or the audio loop, never concurrently). The Engine's own
atomics are block-thread tallies the control thread reads (blocks too long to run and the last one's
length); the GraphHost and script host it owns add their fault, drop and
throw counts, their audio-ownership flags, the in-flight commit count and the call deadline. A removed
or displaced core is never freed on a hot path: it leaves with the plan that last ran it.

Its per-block driver `Engine::processBlock(frames, outputs, n)` is the one place every host funnels a
block. The host has already drained the ring (`drainInto` → `Engine::drainCommands` →
`GraphHost::drain`: plan swaps and commands applied in push order, a staged host message landing in the
staging and a staged node message handed to the node the running plan places under its id). Then, in order:

1. `ScriptHost::beginBlock()`: release the script instances whose shims the control thread destroyed
   since the last block (up to 16 a block), before any of this block's JS runs. An empty block stops
   here; so does one longer than the plan's buffers, which renders silence, drops what was staged for
   it and is counted, for `reclaim` to report (a host avoids it by calling `setMaxBlockFrames` with its
   buffer size, as the plugin's `activate()` does).
2. `syncTrace_.beginBlock(...)` (inert unless `RETROPLUG_SYNC_TRACE` is set), and `midiOut_` cleared
   for the plan's host output node to fill.
3. `GraphHost::run(frames, transport, outputs, n)`: silence every host output, then run the plan, in
   the planner's order (topological, ties broken by document order):
   1. **The host inputs and the script nodes that feed the systems.** `rp.host.midi-in` takes what the
      hosts staged (stable-sorted by frame), and `rp.launchpad.surface-in` drains the surface link's
      receive ring (every message at frame 0, the device's traffic not being timed against the block).
      Then the script nodes, run by the [script host](../packages/native/src/host/script/ScriptHost.hpp)
      ([04-roles-protocol-nodes.md](04-roles-protocol-nodes.md)): the MIDI routing (`rp.midi.route`) fans
      the host MIDI out to the systems; the controller session reads `surface-in`; and each
      system's protocol nodes read what they need (mGB, N8 and LSDj their routed MIDI; risa and smsggdj only the
      transport) at the **block-start** `ppq_`, and send that system node's `serialIn`, `midi`, `bytes` and
      `buttons` inputs and the session its host MIDI out to `rp.host.midi-out`.
   2. **The system nodes** ([SameBoyNode.cpp](../packages/sameboy/native/src/system/sameboy/SameBoyNode.cpp):
      `rp.sameboy.gb`, `rp.mesen.nes`, `rp.mesen.gba`, `rp.mesen.sms`), each a `SystemNode<T>` over a
      core's `prepareForBlock` / `stepIfBelowTarget` / `finishBlock`. Each applies its inputs first
      (below), then runs its core. An unlinked system is one `process()` call. Linked Game Boys run as
      one coupled group over `rp.step/1`, round-robin: prepare every member in order, then passes of
      one step per member until none is below the block's target, then finish each, so their serial
      cable stays in instruction lockstep. The node zeroes the buffers it hands `finishBlock` (which
      adds into them) and sets the lane count from which of its outputs are connected
      ([10-multichannel-audio-out.md](10-multichannel-audio-out.md) section 1), telling the core that count before
      it steps (`SystemBase::setBlockLanes`): a core whose step gates on a ring of its own (the NES, which collects
      its audio in a mix ring and, with the tap armed, tap rings too) fills the one the block drains, so a block
      the wiring sends to the mix steps on the mix's clock; a Game Boy then writes its `serialOut`.
   3. **The script nodes that read the systems.** An LSDj's serial-out decoder (`rp.lsdj.serial-out`)
      reads its Game Boy's `serialOut` of this block, over a forward wire, and sends its host MIDI out to
      `rp.host.midi-out`, each message at its byte's frame.
   4. **The cart and the host outputs.** `rp.n8.nes` pushes what its `midi` and `bytes` inputs carry (the
      fan-out of its system's protocol nodes) at the cart link, each message at its frame and the block's
      rate, and hands the link the expansion volume it owes; `rp.host.midi-out` fills `midiOut_`, its inputs
      merged by frame, which the plugin drains to the DAW after the block, each message at its frame (the SDL
      standalone to its MIDI out port at once, without the frame); `rp.launchpad.surface-out` hands the
      session's LED and page messages to the surface link's send ring, in queue order.
   5. **The mixing nodes**, `rp.core.split` and `rp.core.add`: the routing, as wiring (below).
   6. **`rp.host.audio-out`**, which copies its buses into the host's buffers (it reaches them
      through `rp_host.get_interface("rp.host.audio-buffers/1")`).
4. `ScriptHost::endBlock()`: in a profile build, the allocation window's block counts (the script
   nodes' promise jobs ran at each call's checkpoint, inside the plan's run); a profile build closes the
   block's `apu-render` span, the plan's whole run with the script nodes' spans inside it, just before
   ([08-profiling.md](08-profiling.md)).
   What was staged and no node of the plan took (a plan with no `rp.host.midi-in`) is dropped, not
   carried into the next block.
5. `registry_.publishSystem(...)` for each system of the plan, in slot order: copy its
   freshly-published frame/state/SRAM into the owned snapshot store.
6. `syncTrace_.endBlock()`, then advance `ppq_` only if transport is running (so the protocol nodes'
   `walkTicks` and the cores see the same block-start ppq).

A system's live knobs never ride a host command: they are its system node's settings and params (below),
which the node gives its core at the start of a block. `Engine::setSampleRate` re-rates the running plan's
cores in slot order, then re-activates the graph's buffers at the new rate;
`Engine::setMaxBlockFrames` grows the plan's block size to a host's maximum (never below 8192, the
largest block the parity matrix holds) and re-activates the graph while it is quiescent, where each core's
`reserveAudio` moves its audio ring and its per-block scratches with no block in flight; the plugin and the SDL
standalone call it before handing the engine over.

### Host I/O, the system nodes' inputs and queue capacities

What the script nodes read and send are wires the planner sees
([modular-graph-phase-b3a-design.md](../docs/modular-graph-phase-b3a-design.md) section 3,
[modular-graph-phase-b3b-design.md](../docs/modular-graph-phase-b3b-design.md) section 5). The host
I/O nodes ([HostIoNodes.hpp](../packages/host-io/native/src/host/graph/HostIoNodes.hpp)) reach the engine
through its host service `rp.engine.host-io/3`
([HostServices.hpp](../packages/host-io/native/src/host/graph/HostServices.hpp)) and do not activate without
it:

| Node | Ports | Does |
|---|---|---|
| `rp.host.midi-in` | out `midi` (events: 2048 messages, 64 KiB) | takes the staged host MIDI, stable-sorted by frame without allocating: the plugin stages two runs in frame order (a block's DAW events, then each moved automation slot's CC at frame 0), which it merges onto its queue, a tie to the earlier run; more runs are sorted in place first, by insertion |
| `rp.host.midi-out` | in `in1` to `inN` (events), an input per entry of its settings `{"systems": [<id>, ...]}`, at most 128 | each input's messages land in `Engine::midiOut` tagged with that entry's system, the inputs merged by frame: each step hands out the head with the earliest frame, the lower input on a tie, so with every frame 0 it is the inputs' concatenation, input by input, each in queue order; each message goes at the later of its frame and the last handed out in the block, and at the block's last frame at most, a guard that keeps the list frame-sorted and inside the block whatever an input's queue holds (a node on the C ABI writes its own, and a script node's `send` carries a frame past the block as given), since the plugin hands each frame to the DAW unchecked. The merge allocates nothing, a head index per input, and costs messages times inputs; the sink it hands each message to, `Engine::midiOut`, still takes a `std::vector` per message ([07-remaining-work.md](07-remaining-work.md)). Two entries may name one system, and an empty list is a node with no inputs, which sends nothing. A settings change that keeps the inputs is a swap, one that moves them a rebuild |
| `rp.launchpad.surface-in` | out `in` (events: 1024 messages, 16 KiB), out `connected` (control), out `drops` (control) | drains the surface link's receive ring at the top of its block (16-message batches until the link answers short), every message at frame 0; `connected` carries the claim's `generation` (0 while nothing is attached, a new number on every claim, so a pair swapped inside one block still reads as a new device) and `drops` its lost-message count, each from frame 0 of the block, both wrapped at 2^24 (`connected` into 1..2^24 - 1, so a wrap never reads as nothing attached, and a reader compares each with the previous block, so a wrap only reads as a change). Its first block discards what the link held when it bound it, so a press from before the plan is not replayed; every drop the link sees (an oversized message, a full receive ring, a paint sent with nothing attached) is in that one count, and one the node's queue cannot hold is dropped and counted by the queue. `unique`, category `device` |
| `rp.launchpad.surface-out` | in `out` (events) | hands each message to the surface link's send ring in queue order; a detached link drops and counts them. `unique`, category `device` |
| `rp.n8.nes` | in `midi` (events), in `bytes`, param `expansionVolume`, out `connected` (control) | pushes each message at the cart link at its frame and the block's rate, the `bytes` port's flush flag carried; hands the link the expansion volume at its first block, on a change and on every reconnect; `connected` is the link's state from frame 0; leaving the plan, hands the link the notes the cart still holds for their note-offs. No audio output. `unique`, category `device` (no `threads`, below) |

An input node takes everything staged whether or not its output is wired, so `Engine::setGraph` refuses
(`midi-in`) a document with a second `rp.host.midi-in`. It refuses (`midi-out`) a second
`rp.host.midi-out`: the node frame-sorts only what it merges, so a second would append its own run behind
the first's, and `Engine::midiOut`, which the plugin writes to a DAW as it is, would step back in frame.
The device nodes' uniqueness is their descriptors' `"unique":true` instead, which the planner checks for
every host: a plan holds one `rp.n8.nes`, one `rp.launchpad.surface-in` and one `rp.launchpad.surface-out` at
most, and a second is refused at plan time with code `unique`. The device nodes reach their devices through
`rp.host.devices/2` ([rp_device.h](../packages/node-sdk/native/src/graph/rp_device.h)), not the host I/O
service: a node asks for `"n8"` or `"launchpad"` by name in its activate and is handed a link the host owns,
or a never-connected one in a host that owns no such device, so a document plans and runs everywhere. A
message the UI stages at a node rides `rp.stage/1`, the third seam beside these two: a system node hands it to
its core's ingress, the cart node pushes it at its link.

A node's `threads` flag declares what the node itself needs (a thread of its own), not what its device link runs:
whether a link has a thread is the link's business, so `rp.n8.nes` and the Launchpad's two declare none (the planner's
Pico target plans them; `rp.n8.nes` relies only on `CartLink::push()` never blocking, never allocating and taking no
lock, whoever drains the ring, and the surface nodes on a `SurfaceLink` that a provider's RtMidi callback thread and
writer thread move).

Every system node has four inputs, mapped onto core calls `SystemBase` already has. It applies them
at the top of its `prepare` (where `process` starts too, and which a linked group runs for every member
before any of them steps), each in queue order, the inputs in this order:

| Input | Kind | Core call |
|---|---|---|
| `serialIn` | bytes | `pushSerialIn(frame, byte)`, one call per byte, at its message's frame (a protocol node sends a message's bytes, or a run of same-frame bytes, as one message) |
| `midi` | events | `onMidi`; a message longer than a `MidiEvent` holds goes down `pushCoreBytes`, unframed |
| `bytes` | bytes | `pushCoreBytes(frame, data, size, flush)`, the flush flag riding `RP_MESSAGE_FLUSH` (and each message handed to the sync trace) |
| `buttons` | events | `pressButton(button, down)`, from a 2-byte payload `[button, down]` |

The order shows only on the NES, where MIDI and bytes land in the same FIFO: a flushed byte message
discards the MIDI queued ahead of it. A Game Boy's `midi` input has no effect (`SystemBase::onMidi`'s
default is a no-op, as it is for the other consoles that take MIDI through a protocol node); mGB and LSDj
hear MIDI through `serialIn`. The Game Boy also has a
`serialOut` output (bytes): after `finishBlock`, while it is connected, the node copies the bytes its
core captured on the serial port this block, each a message at the frame the core completed it, a byte
completed past the block's end at its last frame (the core clears its log at its next
`prepareForBlock`). Capture runs only while armed, by the node's `serialOutCapture` setting (below; TS
sets it for LSDj's two MIDI-out modes and `off` otherwise), in one of two modes
(`SerialOutCapture`): `listen` takes the bytes the Game Boy clocks out itself (LSDj as master sync's
master), and `drive` also clocks a transfer waiting on an external clock, a bit an audio frame, taking a
byte every 8 frames while the Game Boy holds the port for one, idle bytes included (the Arduinoboy's part
in MI.OUT). Only MI.OUT drives: a stopped LSDj master waits on an external clock for another master, and
clocked, it turns slave and ignores START. An LSDj's serial-out decoder, `rp.lsdj.serial-out`, placed
after the systems, reads the port over a forward wire in the same block. The port's capacity is fixed at
8256 messages and 8256 bytes, a byte every 8 frames over the largest block a host can run (65536 frames)
with slack, rather than sized from the plan's block size, so a feedback wire from it would keep its front
across every swap; a CGB clocking the port itself on its fast clock can outrun it in a very long block, and
what does not fit is dropped and counted.

The core side is bounded too, and fixed: each core sizes its ingress queues when it is built, from what one
block's input can deliver ([InputCapacity.hpp](../packages/system/native/src/system/InputCapacity.hpp), the
capacities the sources' outputs above declare: `serialIn` and `midi` 4096 messages / 72 KiB, `bytes` 256 /
1 KiB, `buttons` 64, a Game Boy's `serialOut` 8256 / 8256), so a block that carries host MIDI, raw bytes,
serial-in bytes or presses allocates nothing in the core. The queues are a Mesen console's presses (320: a
block's 64 plus `GraphHost::commandCapacity()`'s 256, a `static_assert` holding the two together; one entry
a button, because the block applies the whole queue before the emulator polls its controller, so a
transition for a button already queued replaces it and the button range is the real bound: no burst fills
it, and a release is never dropped behind one; because that coalescing keys on the wire byte, each
console's map names at most one button per byte, and a byte the console has no button for - a Master
System's Select, a byte past any console's table - drives nothing), a Game
Boy's presses (1024, sixteen blocks of a saturated buttons input, because presses are scheduled ahead; a Mesen
console's presses coalesce, but these cannot, since each is scheduled and meant to be seen, so a quiescent session that
pushes more than 1024 transitions between two blocks drops the tail, release included, which
[07-remaining-work.md](07-remaining-work.md) records) and its serial-in (a run a message over a byte ring of two
blocks' `serialIn` or one `serialOut`), a NES's N8
runs (4352) and their bytes (74752), and a Master System's levels (74752, in bytes from both doors). A push
past a bound is dropped rather than grown, and counted (a Mesen press queue has none to reach), and the
sort a block's queued bytes or levels take is a merge through a scratch buffer sized with the queue (a second
copy of its entries, 598016 bytes for a Master System role and 52224 for a NES role, held for the core's life; the NES
role keeps a second, 17408-byte scratch for the run indices its rebase orders), so it
allocates nothing and costs at most n * (ceil(log2 n) + 1) moves: the core reports `inputAccepted()` and
`inputDropped()`, and the N8 FIFO's `coreTransportStats()` adds `droppedBytes`, the same vocabulary as a
graph queue's overflow. The reliable link refuses nothing: a full 48-frame window gives up the window (its
unacked frames) and re-syncs. The FIFO's permissive profile (`_rxCapacity == 0`) means no modelled depth, not no
storage bound: fifo_a still refuses a reply past `_rxBound` (a block's input plus `FILE_READ_MAX`), its vector reserved
to twice that because `pop` compacts the consumed prefix lazily. A reply that cannot fit answers
`EDIO_INVALID_PARAMETER` (0x13) up front rather than being cut short mid-record, and that status is delivered even when
the ROM's own backlog has filled fifo_a, one byte past the bound (one byte only, so a second refusal while the first
is unread drops); a read stops at the chunk whose pushes start dropping; a modelled depth still truncates, as the
device's fifo_a does. A `CMD_F_FRD_MEM` validates the range the ROM asked for, not the bytes the file held, and
answers 0x13 when the cart cannot back it.

An events or bytes output may declare its queue's capacity (`rp::bytes_out::sized<rp::capacity{...}>` in C++,
`events.out(capacity)` / `bytes.out(capacity)` in TS), by default 256 messages and 4096 bytes, at most 1 Mi messages
and 16 MiB. A wire carries its source's queue, so an input declares none (both parsers refuse one, and an input has no
`sized`): the outputs that do are the host inputs above, the Game Boy's `serialOut`, and every script node's output
([nodes/ports.ts](../packages/core/ts/ports.ts): `serialIn` 4096 messages / 72 KiB, `midi` 4096 /
72 KiB, `bytes` 256 / 1 KiB, `buttons` 64 / 128 B, `midiOut` 1024 / 8 KiB, `controllerOut` 256 / 4 KiB, a routing
output 2048 / 64 KiB, `launches` 64 / 256 B) and the box's `rp.device.midi-in` (64 / 4 KiB, which the `bliptoaster`
pack's message pools follow). The two that pass host MIDI on, `serialIn` and `midi`, hold what `rp.host.midi-in`'s
output holds and room for a role's own bytes besides, so the one place host MIDI is dropped is that edge; the rest are
sized from what the suites send through them in one block, with margin. Instead of a size an output may declare
`{"match": "<input>"}` (`rp::events_out::matching<"in">`, `defineNode`'s `{ match }`): it names an input of the
output's own kind and is sized, per plan, as the queue that feeds that input (the planner's `capacities()` pass; an
unwired input gives the default, a loop of matches is refused, `capacity-match`), which is how
`rp.bliptoaster.midi-policy` serves the box's 64 / 4 KiB and the desktop's 2048 / 64 KiB with one declaration (as
`rp.core.midi-filter` would). Queues are allocated per plan, a plan's events and bytes queues held to a budget of 64
MiB (`kPlanQueueBudget`; a document past it is refused, `queue-budget`), and an unwired output writes a shared scratch
queue of the default size, whose drops go uncounted. A push that does not fit is dropped and counted on its queue;
after each block the executor adds the counts to the producing instance's atomic and the GraphHost's,
`GraphHost::reclaim` names a node the first time its outputs drop, and `Engine::droppedMessages()` (the harness RPC
`droppedMessages`) reads the total, which the parity rig and the native goldens run hold at zero.

### Device nodes, their links and capability jobs

`rp.n8.nes` and `rp.launchpad.surface-in` / `-out` are the hardware nodes
([N8Node.cpp](../packages/n8/native/src/n8/N8Node.cpp),
[SurfaceNodes.cpp](../packages/launchpad/native/src/launchpad/SurfaceNodes.cpp)). Each binds the host-owned device it
drives through `rp.host.devices/2` ([rp_device.h](../packages/node-sdk/native/src/graph/rp_device.h)), a link its host
installed by name and interface id: an `rp::n8::CartLink` ([CartLink.hpp](../packages/n8/native/src/n8/CartLink.hpp),
`n8` / `rp.n8.cart-link/1`, the N8 pack's own) for the cart, with a push per message (frame, bytes, the flush flag,
the block's rate) that answers whether all of it reached the device, the link's connected state and drop count, the
expansion-volume register and `submitJob` / `abandonJobs`; an `rp::launchpad::SurfaceLink`
([SurfaceLink.hpp](../packages/launchpad/native/src/launchpad/SurfaceLink.hpp), `launchpad` / `rp.launchpad.surface-link/2`) for
the pad, with a drain into the node's own array, a push, the claim's generation and its drop count. `ControllerLink`
([ControllerLink.hpp](../packages/launchpad/native/src/launchpad/desktop/ControllerLink.hpp)) numbers every claim and tags
every receive slot with it, so the audio thread, the ring's one consumer, drops a slot that belongs to an earlier claim
where it sits rather than any thread rewinding it; `dropped()` counts what the link lost in both directions (a push to a
device that is not there, an oversized message, a full ring) and `messagesSent()` what it wrote, and both are what the
facet's status line reads. A host installs its links before its first plan with `Engine::installDevice` / `installLink`,
into a fixed table of 8 entries (a name up to 31 characters, an interface id up to 63): the N8 pack's provider installs
the `N8CartLink` in every product that starts providers (the plugin, the SDL standalone, the CLI, where it is never
connected, and the test host, over a fake cart), the Launchpad pack's provider installs its `ControllerLink` in the
products that name it and install links (the SDL standalone and the CLI, over real ports; the test host starts it for
its facet but hands the graph a `HarnessSurfaceLink`, so a test stages a press where no port could); and the Node addon
and the render host install neither. A name it has no device for answers
null, and the node runs on a stub of its own that is never connected and that drops and counts everything, so a
document plans and runs in every host. A node keeps its handle across a reactivate and gives it back once in destroy,
which runs at reclaim on the control thread while the links are still installed: a host tears its plans down first
(`Engine::clearGraph`), then uninstalls its links.

A capability job ([CapabilityJobs.hpp](../packages/native/src/host/engine/CapabilityJobs.hpp)) is what `submitJob`
takes: a request for a node of the last published plan, answered with an id or a refusal. A system node's job goes to
the `memory()` / `sram()` / `rom()` accessor its core presents: a memory read is the published copy and a ROM header
the core's immutable image, both at once, a write rides the command ring and completes once the block after its apply
has published the region, and an emulated `sram.restore` is refused (a battery is a seed on a rebuild). A device
node's job goes to its link, on the device's own thread: one at a time ("busy"), none while the device is not attached
("not connected"), progress reported per drain, and a node leaving the plan abandons the job it still runs. The
request and the report are C ([rp_jobs.h](../packages/node-sdk/native/src/graph/rp_jobs.h)): a request's strings are C
strings (a device job whose strings hold a NUL byte is refused, as a C string would cut it), and a link reports
progress, success, a bounded UTF-8 failure reason or an abandon through the engine's `rp.host.jobs/1` over an opaque
`rp_job`. The cart reads its FPGA config block at submission (on that thread, between MIDI chunks, as the expansion
volume reaches it, and only on a turn quiet for the read: nothing outstanding on the reliable link, or a cart silent
for 250 ms since its oldest outstanding frame first went out, as one at its menu is), so a transfer a running game
will refuse is refused without pausing the stream and dropping the block's notes; the submitting thread waits for that
read, up to its 1 s bound. The cart node keeps books on the notes the cart holds: it re-sends at frame 0 the note-offs
a pause dropped, and its destroy hands what is still sounding to the link (`CartLink::releaseNotes`), whose serial
thread writes their note-offs behind every byte pushed before the call (a disconnect writes them before the port
closes; a link that is down drops them), so a node leaving the plan mid-note leaves no tone on the console once its
plan is reclaimed: within a frame of the swap where the host has an idle tick (the plugin with its editor open, the
SDL standalone), and at the next publish or deactivate in a plugin whose editor is closed. The engine keeps at most
`kMaxJobs` (64) outstanding, engine-wide back-pressure that leaves room for every completion, and a request names a
capability from the one reserved id space (`rp.system.memory/1`, `rp.system.sram/1`, `rp.system.rom/1`, in
`rp_jobs.h`), so the engine asks a node only by an id it knows and never casts an answer.

### The system nodes' settings and params

A system's live knobs are its node's ([MesenNode.cpp](../packages/mesen/native/src/system/mesen/MesenNode.cpp),
[modular-graph-phase-c2-design.md](../docs/modular-graph-phase-c2-design.md)): each is either a **setting**, in
the document, which an edit publishes, or a **param**, its value in the document and its live edits through
`setParam` (above). A param is what is worth playing, automating or modulating, so it is also a control input a
wire could drive; everything else, display knobs, accuracy switches and latencies among them, is a setting.

| Node | Settings | Params |
|---|---|---|
| `rp.sameboy.gb` | `system`, `rom`, `sram`, `state`, `romName`, `model`, `fastBoot`, `highpass`, `colorCorrection`, `dmgPalette`, `lightTemperature`, `serialOutCapture` | `gain` |
| `rp.mesen.nes` | `system`, `rom`, `sram`, `state`, `romName`, `region`, `removeSpriteLimit`, `apuLatencyMs`, `s5bNoise`, `mmc5PhaseReset`, `sdRoot`, `fifo`, `fifoDepth`, `fifoBytesPerSecond`, `n8Link`, `n8LinkRttUs`, `channelExportMode` | `gain`, `expansionVolume` |
| `rp.mesen.gba` | `system`, `rom`, `sram`, `state`, `romName` | `gain` |
| `rp.mesen.sms` | `system`, `rom`, `sram`, `state`, `romName`, `enableFm`, `gameGear` | `gain` |

- **Values.** `system` is the TS system id whose core the node builds, and the node id must be `sys-<id>`; `rom`
  names a `rom` resource (`"res:N"`) and is required, `sram` and `state` optionally name their seeds (given both, the
  savestate wins), and `romName` is the ROM's file stem, which Mesen stages a Master System or Game Gear ROM under
  for its battery (a node that sends none stages under `system-<id>`). Every setting is optional: absent, the node
  leaves the core as it was built, or as the last document that carried the field left it. A param is optional too:
  absent, the stream is left where it is (its default, at a new node's first block), so a node's gain starts at
  0 dB and a NES's expansion volume at 100%. An enum is a label as the role configs spell it (the label tuples in
  each package's `ts/enums.ts`: [sameboy](../packages/sameboy/ts/enums.ts), [mesen](../packages/mesen/ts/enums.ts),
  [lsdj](../packages/lsdj/ts/enums.ts), and the app's own [settingsEnums.ts](../packages/retroplug/src/settingsEnums.ts)
  for routing and layout), which is its C++ enumerator's name with the first
  letter lowercased (`SameBoyModel::CgbC` is `"cgbC"`, `SameBoyHighpass::RemoveDcOffset` is `"removeDcOffset"`):
  `knobs::Label<E>`, an `rfl::Literal` generated at compile time from reflect-cpp's enumerator list, reads exactly
  those strings, lists them in the descriptor's schema and maps one back to its enumerator by position, so native has
  one spelling of each label and a document and a saved `.rplg` carry the same word. An ordinal, another spelling, or
  a number out of its range (`lightTemperature` -1 to 1, `apuLatencyMs` 0.25 to 6, NaN included) refuses the settings,
  and with them the document, never clamped. A field the type does not know is ignored (the settings reader takes
  extra fields), which is why `BuildGraphGoldens.test.cpp` holds every golden document's system nodes to the fields
  their types' schemas list. `serialOutCapture` is `off`, `listen` (master sync) or `drive` (MI.OUT). `gain` is dB
  from -90 to 12, default 0; `expansionVolume` an int param, whole percent of unity from 0 to 200, default 100, as the
  core takes it. The construct-time fields are the ones the core reads as it boots and has no setter for: NES `sdRoot`
  (a string), `fifo`, `n8Link` and `channelExportMode` (labels), `fifoDepth`, `fifoBytesPerSecond` and `n8LinkRttUs`
  (ints in the range the store clamps them to), SMS `enableFm` and `gameGear` (flags). `buildGraph` writes every field
  the core-config role carries, the node's default where the stored config holds no value the node takes; `gameGear`
  is the platform's, written `true` for a Game Gear and left out otherwise, and a platform never changes under a live
  id. So a document never flips a field between absent and present under a system id (the key tells the two apart).
- **Only a new core rebuilds.** The node keys its rebuild on `system`, `romName` and its construct-time fields
  (node.hpp's `rebuild_key`: prepare compares the new settings' key with the key the instance was created with, which
  no commit writes), declares `stateless_prepare`, so every other settings change is a commit that never makes
  `setGraph` wait for the audio thread, and reads its `rom` / `sram` / `state` fields itself: a ref that moved is a
  rebuild whatever the key says, and one that stayed keeps the pins the instance took at create.
- **When a setting reaches the core.** `on_settings` only points the node at the committed settings (it never
  allocates). At its first activation the node builds its core from its resources and the config its settings made,
  gives it what the build did not already apply, and runs it; after that it gives it what moved (present, and absent
  from or different in what it gave last) at the start of its next block, in `prepare`, before its inputs and
  `prepareForBlock`, which a linked Game Boy's coupled group runs for every member before any of them steps. What it
  gave last is its `Settings::Applied`, the live knobs alone and trivially copyable: the fields that carry a string
  (`romName`, `sdRoot`, the resource refs) rebuild the node or move a resource, so the copy the node makes when it
  applies a commit copies no string. A setting therefore lands at the node's next `prepare`, after any host command
  queued behind its plan (`GraphHost` drains its ring before a block runs), so a caller that must act on the restarted
  core renders a block first: a `writeRam` or staged core bytes sent after a model change reach the core it is about
  to replace. Each knob is guarded twice: `moved` compares it with what the node applied last, so a document's
  unchanged field is not applied again even where the core was changed by another path, and the core's own setter
  compares it with what the core holds where it has such a guard (`setFastBoot` and a NES's `setRemoveSpriteLimit`
  write unconditionally). So a palette change reapplies the display group and restarts nothing, a model change
  restarts the core (`restartEmulator`; the cable needs no rewiring because `linkPeers_` holds the systems, which
  survive it) and a region change resets it. A Game Boy gives `fastBoot` first, which waits for the next restart and
  so is what a model's restart boots with, then the rest in the role config's order, the capture last. The capture
  lands at the start of the first block of the plan that carries it, the block `rp.lsdj.serial-out` first runs in its
  new mode, so no byte captured in one mode is decoded in the other. A construct-time field is read once, at the
  build, so a change to one is a new system id and a new core.
- **When a param reaches the core.** Each param's latest value in the block is given after the settings and
  before the inputs, value-guarded against the value the core was given last (a live `setParam` included, whose
  queued value the host records), so a publish carrying the value the core already has calls nothing, and one
  carrying a value a live edit moved the core away from sends it again. A value a wire drives is clamped to the
  param's range by the node, from the same constants the declaration takes its bounds from; a change anywhere in a
  block lands at its start. A node reads its params only in a block (the host binds its control streams there,
  landing the document's values in them as it adopts the plan), so they reach the core at the node's first block,
  before its first `prepareForBlock`, not in `activate`: a core its node has taken runs nothing before that. That
  first value is snapped into the core (whose gain smoother starts at 0 dB), so its first sample plays at the
  document's level; every later gain change is smoothed, as it always is. A document without a param leaves its
  stream where it was: at its default, on a new node.
- **Allocation.** A warmed core's whole block allocates and frees nothing, emulation included: its audio ring and its
  per-block scratches are sized at activation (the block size arrives in the build request, `maxBlockFrames`, and a
  later host growth re-activates the graph quiescently, where `reserveAudio` moves the sizes with no block in flight),
  and Mesen's per-frame plumbing reuses its buffers. That holds with a block's host input staged into it too: every
  ingress queue a press, a MIDI run, a serial byte or a raw message lands in is bounded and sized when the core is
  built, and a push past its bound is dropped and counted (above), so the counted window may carry what a node applies
  before the core's `prepareForBlock`. The cases that prove it `SKIP` the GBA, counted, where its sibling fixture ROM
  is not present, as on CI, and cover it where the resources tree is. What still allocates on the block thread is work
  that makes something: a Game Boy's model change restarts the core (`GB_alloc` / `GB_init`), which allocates, as it
  did on the command ring; a NES's region change resets it, which allocates too; the 0.5 s state-snapshot capture
  allocates inside Mesen's streaming serializer; and the N8 firmware's SD path allocates in its command handlers, in
  a bigger directory listing and in an unbounded TX queue ([07-remaining-work.md](07-remaining-work.md)). A Game
  Boy's knobs, the ones it applies in place, a NES's setters but its region, and every param allocate nothing at all.
- **A freshly built core's first block is the warmed block with one exception.** A Mesen core's controller snapshot, the
  four persistent frame input copies and the video filter's output buffer are sized at activation, off the block thread,
  so a core built directly has a first block that allocates and frees nothing. A core the Engine builds also arms its
  boot savestate capture for the first block (`enableStateSnapshot`). A Mesen console's capture is a whole savestate
  through Mesen's serializer, which allocates by nature, and it cannot move to activation without moving
  `NesApu::EndFrame` with it; a Game Boy has none of that plumbing, and its capture is one `resize` of the scratch it
  reuses, so its first block pays that one allocation and its periodic captures of the same model are free (a model
  switch grows that scratch by the state delta, inside the restart's own allocations). Either way the first-block
  case pins that block to exactly one capture and nothing else.
- **A refused document** can still have given knobs to a node it reuses, since activation runs while it plans: a
  reused node's core keeps them, and the next document in the app carries the same. A core a refused plan's new node
  built is destroyed as that plan's instances unwind, with its snapshot slot and its resource pins.

### Graph requests

Structure arrives as a graph document TS builds
([`buildGraph.ts`](../packages/retroplug/src/graph/buildGraph.ts),
[modular-graph-phase-b2-design.md](../docs/modular-graph-phase-b2-design.md)), after the resources its system nodes
name:

- **`putResource(spec)` / `releaseResource(id)`** put an immutable byte block in the engine's `ResourceTable` and drop
  the owner's hold on one (the DTO above).
- **`setGraph({ document, slots, tap? })`** (`Engine::setGraph`) first reclaims what the audio thread
  handed back (the GraphHost's plans, the script host's released instances), then checks that the
  document's system nodes and the slots name the same systems, each node `sys-<id>` with settings naming
  its system (`"system": <id>`) and its ROM (`"rom": "res:N"`), that a tap arm names one of the slots and a mode
  from 1 to 3 (the NES's own numbering, which the engine validates rather than the capability: the arm travels as a
  byte TS numbers, and a NES's tap is the only one today), and that there is at most one `rp.host.midi-in`
  (a second refuses the document before anything is built; the device nodes' `unique` flag is the planner's);
  builds the plan's `SystemTable` (a slot-ordered list of `{ id, SystemBase*, ChannelTap* }`, the tap filled
  from `SystemBase::channelTap()` and null for a core that offers none,
  [SystemTable.hpp](../packages/native/src/host/graph/SystemTable.hpp)) from the slots and the live cores, a slot
  whose node is reused from the previous plan already filled;
  turns `tap` into the swap's `SyncTap` trailer; and hands both to `GraphHost::setGraph`, which computes
  every node's layout from the document (`layout_for` on each node type), runs every layout check, and
  only then creates and activates the new instances. A system node builds its core as it activates
  (`rp.host.systems/2`) and the engine fills that slot's table entry, so the table is complete before the plan is
  published. It rides with the plan, so the audio thread's
  lookups always match the plan that is running. A refused document changes nothing: the cores its new nodes built
  are destroyed as those instances unwind, releasing their slots and their pins.

The document `buildGraph` writes:

- **Order.** `rp.host.midi-in`, then `rp.launchpad.surface-in` when a controller is enabled, then the script
  nodes: the MIDI routing (when the model has a routing mode), the controller session (when one is enabled) and
  each system's protocol nodes, in slot order and then role order; then the unlinked systems by slot, then the
  link groups in order of first appearance (members by slot, one `link` wire chaining each member to the next),
  then the script nodes that read a system, in slot order (an LSDj's `rp.lsdj.serial-out`, on a Game Boy), then
  `rp.n8.nes` when a NES drives one, `rp.host.midi-out` and `rp.launchpad.surface-out`, then the mixing nodes,
  then `rp.host.audio-out`. A protocol node runs before its system because the system reads its
  outputs, and LSDj's decoder after it because it reads the system's `serialOut` in the same block: every
  node reads only nodes placed before it (but over a feedback wire, which orders nothing, and which no document
  `buildGraph` builds holds), and the planner breaks ties by document order (`Planner.cpp` `order()`), so
  this is also the order the plan runs in. Node ids are `midi-in`, `surface-in`, `routing`, `controller`,
  `cart-<id>`, `midi-out`, `surface-out`, `out`, `sys-<id>`, a system's protocol nodes `sys-<id>/<role-kind>`
  (one placed after its system `sys-<id>/<role-kind>/out`), and names derived from a system id
  (`split-<id>`, `stem-<id>-<k>`) or a host channel (`lane-<c>`), never from position, so a system and its
  nodes keep their instances across edits. Link groups are Game Boys only, bucketed by nonzero link-group id
  in slot order, a group of fewer than two dissolved.
- **The script nodes** ([04-roles-protocol-nodes.md](04-roles-protocol-nodes.md)). `routing`
  (`rp.midi.route`, settings the routing mode and the system ids in slot order, which positional MIDI
  routing reads) takes `midi-in.midi` and has an output per system, `sys-<id>`, into each of that system's
  protocol nodes that reads MIDI. `controller` (`rp.controller.session`) takes `surface-in.in` and
  `.connected`, its `controllerOut` to `surface-out.out`, its `launches` into the target system's
  `rp.lsdj.sync` when its target is a system. Each protocol node's outputs go to its system node's inputs of
  the same name; each LSDj's Game Boy's `serialOut` goes forward into its `rp.lsdj.serial-out`, which reads
  the bytes MI.OUT and master sync decode in the block the core captured them; the driving NES's protocol
  nodes' `midi` and `bytes` go to its `rp.n8.nes` (`cart-<id>`) as well, the fan-out that makes emulated, real
  or both plain wiring; and `midi-out` takes the controller's `midiOut`
  and then each `rp.lsdj.serial-out`'s, in slot order, each input under its system: the controller's first, so
  its message wins a tie at a frame. Every script node reshapes, so a structure change keeps each surviving
  instance (the routing node's ports follow the systems) and its wires stay bound.
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
- **Cores.** A system node builds its core at first activation through the host service
  `rp.host.systems/2` ([HostSystems.hpp](../packages/system/native/src/system/HostSystems.hpp)), from the ROM and seed
  bytes its resources hold, the config its settings made and the build function its own package carries
  (`Engine::buildSystem`), and owns it until the node is destroyed (`Engine::destroySystem`).
- **Resources.** A node that reads a resource its settings name (a system node's ROM, its battery, a savestate seed,
  each written `"res:N"`) pins it at create through `rp.host.resources/1` (GraphHost's own, over the engine's
  `ResourceTable`), and the pin drops when the instance is destroyed, both on the control thread: the audio thread
  never takes or drops one. A resource field that moves rebuilds the node (node.hpp decides that itself, resolving each
  field's handle before `needs_rebuild` runs and skipping that hook on a moved ref), so a running node's bytes never
  change under it, and a prepare that keeps the refs copies the instance's pins into the new settings object. The bytes
  arrive as `putResource`'s, so a node never touches the filesystem
  ([modular-graph-phase-c3-design.md](../docs/modular-graph-phase-c3-design.md) section 4).
- **The system nodes' knobs** (`systemKnobs` in `buildGraph.ts`, the one table of which core-config field is a
  setting and which a param, which the store reads too). A system node's settings carry `system`, its `rom` and
  seeds, `romName`, the setting fields of its system's core-config role (the role whose kind is its core, `sameboy`
  or `mesen`) as that config spells them, its type's construct-time fields, and on a Game Boy `serialOutCapture`,
  for the mode its `lsdj-sync` node actually runs, after the MI.MAP controller's override (`serialOutCaptureFor`,
  [graph/controller.ts](../packages/retroplug/src/graph/controller.ts)): `drive` for MI.OUT, `listen` for master
  sync, and `off` otherwise, a Game Boy with no `lsdj-sync` placed included, since a capture left out would leave an
  armed one armed. Its params carry the system's `gain` and a NES's `expansionVolume`. A setting value the node
  would refuse (another label, a number out of range, another type) is left out rather than sent, the core keeping
  what it has: the planner refuses a whole document for one node's settings, and a loaded project keeps its config
  as written. A param passes any finite number, which the planner clamps. A system with no core config (a
  `DirectGraph`'s until `setCoreConfig`, the B1 translator goldens' models) carries its system, its resources, its
  type's construct-time fields at their defaults and a Game Boy's capture alone.

### `QueuedInvoker`

[`QueuedInvoker`](../packages/native/src/host/engine/EngineInvoker.hpp) (in the files
`EngineInvoker.{hpp,cpp}`) is **the one command path; there is no Direct/Queued fork**. Structure
arrives as graph requests (above), and a param's value through `setParam`. Every other edit packs into a
POD [`DspCommand`](../packages/native/src/host/engine/DspCommand.hpp) and pushes onto the GraphHost's
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
| `StageNodeMidi` | `id`, `StagedNodeMessage* ext` | owning; applied at drain to the node the running plan places under `id`, through `rp.stage/1`, which hands the bytes to a system's core ingress or to a device |
| `SetBpm` / `SetTransport` / `SetPpq` | `double` / `bool` / `double` | inline |
| `PressButton` | `id`, `button`, `down` | inline |
| `CapabilityWrite` | `Job*` | a non-owning pointer to a job record the control thread owns; the block thread reads its bytes and marks it applied, and the engine frees the record once it is done |
| `SyncTap` | `arm`, `mode`, `id` | inline; rides in the plan's swap command as a trailer for every `setGraph` request carrying a `tap` |

Heavy, rare payloads (RAM writes, long MIDI) cross as raw **owning** pointers: the accepted
non-RT-on-rare-op pattern. At drain the GraphHost hands each host command to `Engine::onHostCommand`,
which applies it (`applyCommand`) against the `SystemTable` of the plan current at that point, and
`delete`s owning payloads after apply. `SyncTap` is the channel-tap arming: restore whatever `armedTap_` armed,
then, when the request names an arm, set that core's tap to its export mode (either pin split 1, `ChannelSplit` 3, as
`buildGraph` decides for a split routing over a single NES), recording what it replaced. A core that offers no tap
(`channelTap()` null) has nothing to arm, so the arm reaches nothing. TS sends it on every edit that builds or drops a
core and on every routing change, even when the wanted mode is unchanged, and each run resets the capture streams;
it rides the plan's swap, so no block runs between the two. `armedTap_` holds the tap's address as well as the id, so a
duplicate id does not restore onto another core; a NES's tap is the core object, so the address differs from the core
pointer's by a per-class constant and, within one class, tap equality is core equality. A core allocated where the
armed tap was freed still matches, which TS cannot reach (design section 12). A param's value from `setParam`
rides the same ring as a command of the GraphHost's own, not a `DspCommand`, into its param's control stream.

**Ownership handoff.** A displaced or removed core leaves with the plan that last ran it, and is
freed on the control thread when that plan is reclaimed (`reclaimReleased` → `GraphHost::reclaim`), which a
host with an idle tick does every frame (the plugin's editor idle, the SDL standalone's frame loop) and every host
does in `setGraph`, `loadScriptPack` and as its audio stops:
the node destroying it releases the snapshot slot first, then deletes the core
([01-architecture.md](01-architecture.md) section 5). Producer edge cases are handled without
blocking or freeing in render: a **full ring** on an inline POD command drops a lost edge, not a
leak; on an owning payload the producer deletes it; a plan the ring cannot take is freed
unpublished (each core its new nodes built destroyed as the plan unwinds) and `setGraph` answers busy, so
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
  a core is built returns real bytes with no block yet rendered. `claim` requires the core to
  already have `enableStateSnapshot()`'d (for `stateRegions()`).
- **`publishSystem(system, frames, sampleRate)`**, which `processBlock` calls for each of the
  running plan's systems in slot order: `find(id)`; copy the framebuffer **every block** (cheap); on
  the coarse interval (`kStateIntervalSec = 0.5`, matching the core's own snapshot cadence via
  `sampleAccum`) `readStateSnapshot` the core's published slot straight into `state`'s next write
  slot and slice `sram` out of it, publishing both. The two triples match in size and in their
  `[len:4][savestate]` layout (`claim` sizes `state` from the core's snapshot capacity), so that read is
  the one copy and needs no scratch: a warmed publish only copies, and a claim, which writes only the
  slot it claims, shares no buffer with a running plan's publish.
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

## The build seam: `SystemBuildRequest` and each console's build function

The engine keeps the bookkeeping of a build and none of its knowledge: a system node's request carries
[`SystemBuildFn`](../packages/system/native/src/system/SystemBuild.hpp), a plain function pointer its own package
exports, and an opaque `const void* config` pointing at the console config its settings made, which only that function
reads. So the host builds what the request names
([modular-graph-phase-f2-design.md](../docs/modular-graph-phase-f2-design.md) section 3), and a NES config cannot reach a Game
Boy node: the node fills both fields of the one request, so the two cannot disagree.

Every request carries the ROM bytes, the `sram`/`savestate` seed bytes, the ROM's `romName` and the largest block the
plan will run (`maxBlockFrames`, 0 for a build outside a plan). The block size is what a core sizes its audio ring and
its per-block scratches to *before* it activates, so a 64-frame plan's core never holds an 8192-frame plan's buffers; no
Engine host plans below 8192 today (`setMaxBlockFrames` only grows), so only a `GraphHost` built directly reaches that.
A core built outside a plan keeps `SystemBase::kDefaultBlockFrames` (8192, held to
`Engine::kDefaultMaxBlockFrames` by a test; Mesen cores size from `MesenAudioDevice::kDefaultBlockFrames`, held to the
same constant, so two names stand for one contract until they collapse).

`Engine::buildSystem` runs on the control thread (heavy, non-RT) and refuses, in this order, a build outside the
`setGraph` that activates the node, 0, an id above a system id, an id that is live, a request that names no build
function, and a build that returns null (with its reason); only then does the snapshot slot get claimed, after the
build has paid for its boot. The function it calls returns an **already-`onActivate`'d** system. The bytes come from the
node's resources (its `build` copies them out of the pins its instance holds into the request, and the core takes them
from there), so nothing on this path reads a file or decodes a role config.

A system node is recognised by its descriptor: the type's `category` is `"system"`, read from the `NodeRegistry` the
engine already holds for the planner, so `publishGraph` lines a document's `sys-<id>` nodes up against the plan's slots
without naming a package. The category is a promise the engine holds the type to, since a slot's core is what the audio
thread reads: a document whose plan leaves a slot without a core is refused before it is published (unwinding what its
nodes' activations built, as a refused activation does), and a script pack's type may not declare the category at all,
because a script node cannot build a core.

### The Game Boy's build: `rp::sameboy::buildCore`

[`rp::sameboy::buildCore`](../packages/sameboy/native/src/system/sameboy/SameBoyBackend.cpp) takes the
`SameBoyConfig` the node made from its settings (out of the request's `config`), gates the ROM on
`rp::sameboy::isGameBoyRom` (an empty ROM is refused with it; the embedded mGB's bytes pass the same gate, since it
carries a cart's logo), and builds the core:

```cpp
auto sys = std::make_unique<SameBoySystem>(id, std::move(typed), request.rom);
if (request.maxBlockFrames) sys->reserveAudio(request.maxBlockFrames);  // the plan's block, before any block runs
sys->onActivate(sampleRate);       // boots gb_ + restores sram then savestate
sys->enableStateSnapshot();        // tear-free savestate each block for the registry / Duplicate
```

The config's model, fast boot, highpass and display group are applied inside `onActivate`, before the ROM and
the seeds load, so a loaded non-default model never triggers a post-build restart that would nuke the
just-restored savestate. The capture is not part of `SameBoyConfig` (it is a setter), so the node's own
first application gives it, after the build.

The cores are built **bare**: no feature behaviour is baked into C++. The DSP-thread behaviours
(LSDj sync, mGB, Arduinoboy) run as TS protocol script nodes; kit-patch instead is `lsdj-assets`' `onConstruct`
folding the overrides into the ROM in memory at construct, with no audio-thread part. See 04. The role
model is 04's topic; a live knob reaches the core as its system node's settings (above).

### The Mesen consoles' builds: `rp::mesen::build{Nes,Gba,Sms}Core`

[`MesenBackend.cpp`](../packages/mesen/native/src/system/mesen/MesenBackend.cpp) holds the Mesen core's build functions, one
per console it runs, each reading its own config out of the request: `rp::mesen::buildNesCore` gates
`rp::mesen::isNesRom` and builds a `MesenNesSystem`, `rp::mesen::buildGbaCore` gates `rp::mesen::isGbaRom` and builds a
`MesenGbaSystem` (empty `biosPath` → HLE boot ROM), and `rp::mesen::buildSmsCore` refuses a ROM that is positively
another console's (the NES magic, the GBA logo or the Game Boy logo) but accepts a headerless one (TS admits homebrew by
its extension). Each gate checks its own console's signature alone, so a ROM carrying two consoles' magic is accepted by
whichever node the document names; the TS side picks the node. The NES and GBA cores take their seeds through the config;
the SMS takes the `romName` it stages its file under. All three run the same tail, `reserveAudio` then `onActivate` then
`enableStateSnapshot`, and refuse a boot Mesen's `LoadRom` rejected after the magic gate ("Mesen refused the
ROM"), so NES/GBA/SMS cores publish a tear-free savestate for the registry / Duplicate just like SameBoy.

Their audio arrives through [`MesenAudioDevice`](../packages/mesen/native/src/system/mesen/MesenAudioDevice.hpp), a
fixed-capacity ring of interleaved stereo `int16` sized `maxBlockFrames + kMaxPlayBufferFrames` (32768: the most one
Mesen `PlayBuffer` can carry, the whole of the mixer's own output buffer, `_sampleBuffer`'s 0x10000 `int16` stereo,
which `PlayAudioBuffer` is given exactly as its output cap). `availableFrames()` is its fill, which is
the block clock the step loops and the N8 FIFO's scheduling gate on; `PlayBuffer` writes at the tail, `drain` reads from
the head, and a write past capacity drops the newest frames and counts them rather than moving the queued ones (the
bound makes it unreachable; a debug build asserts). The ring grows only, keeping what it holds, so a host that raises
its block size through `setMaxBlockFrames` never loses a queued frame. The NES's per-channel tap rings
(`NesSoundMixer::_streamBuf`, one a stream) and its blips are sized and made with the same reserve.

Mesen is vendored in-tree (`deps/mesen`, not a submodule), so RetroPlug's edits to it are plain source changes, each
marked
`// RetroPlug:` where they are small. One matters to parallel cores: `Utilities/SimpleLock.{h,cpp}` keeps its holder
thread id and re-entrancy count as relaxed atomics, because `MessageManager`'s process-wide lock is taken by every
core build and a render builds cores on pool workers beside the live engine's; ThreadSanitizer found the plain-integer
read racing (a count changes by a relaxed load and store, never a locked read-modify-write, and the ids are asserted
lock-free). The other batch is the per-console compile-out (`deps/mesen/CMakeLists.txt`,
[docs/modular-graph-phase-f2-design.md](../docs/modular-graph-phase-f2-design.md) section 4): `MESEN_ENABLE_SNES` (the
Super Nintendo and the Game Boy, which Super Game Boy couples), `-PCE` and `-WS` are ON as upstream builds them and OFF
in the root's cache defaults, each dropping its `Core/<Dir>/` units and its CPUs' evaluators and guarding the call sites
into them; NES, GBA and SMS are always built, and 288 of the 426 translation units remain.

## `ScriptHost` + `ScriptCompiler`: the script-node runtime

Native runs the TypeScript script nodes (the MIDI routing, the controller session and each role's
protocol node) but understands nothing about roles. The nodes and what they do are 04's topic
([04-roles-protocol-nodes.md](04-roles-protocol-nodes.md)); this doc covers the C++ runtime under them
(docs/modular-graph-phase-b3a-design.md section 2).

[`ScriptHost`](../packages/native/src/host/script/ScriptHost.hpp) holds **packs**, the unit it
isolates, loads and replaces ([modular-graph-phase-c1-design.md](../docs/modular-graph-phase-c1-design.md)).
A pack has an id, a bundle that registers its node definitions, a package of its own named by its id, and
**two bare QuickJS runtimes** of its own (no txiki; see [01-architecture.md](01-architecture.md)), sharing
no JS value with each other or with another pack's: a control-side one that only checks settings and lays
out a node's ports, and an audio-side one that holds every instance of the pack's types and runs `process`.

- **The built-in pack.** The first pack, `rp.script`, runs the script-node bundle (the generated
  `src/generated/scriptNodes.ts`, the entry every package's `ts/protocol.ts` feeds; `script/bundle.ts` is its fixed
  name for the build's own checks), esbuilt to a global-code IIFE and
  compiled to bytecode at build time by the `retroplug-script-bundle` target
  ([bundles.cmake](../packages/native/cmake/bundles.cmake)), whose derived C array is compiled into
  `retroplug-backend`: every Engine loads it at construction, so every host has the same node types
  with no boot call. A bundle appends its `defineNode` results to the global `__rp_scriptNodes`
  ([register.ts](../packages/core/ts/register.ts)); the host reads that list in both of the
  pack's runtimes, requires the same id and descriptor at each index, and registers one `rp_node_type` per
  definition in the pack's package, all served by the same functions, each type telling itself apart by
  its `ctx`. A bundle that does not load (the promise jobs its load queues among it: they must settle in the
  256 a load runs, below), or one definition that cannot be read, registers no type, logged, and every
  document naming one is refused (`unknown-type`); the built-in pack is loaded once, and never reloaded.
  The native tests build an Engine with toy bundles of their own instead.
- **Loading a pack** (`Engine::loadScriptPack`, the `loadScriptPack` RPC above) runs on the control thread,
  while an audio thread runs blocks or not. The id is checked first: a plain id, and none of the app's (`rp`
  and every id under `rp.`, its built-in pack's and its native packages'). The source, global code as the
  built-in bundle is, is compiled by `ScriptCompiler` (below) and run in two runtimes made for the pack, under
  the checks the built-in pack's load makes and one more: every type id is the pack's id, a dot and a name
  (`user.synth.osc` in `user.synth`). Then the pack's package joins the registry, which refuses a type id
  another package holds (one pack's id can run on into another's, `a` and `a.b`), a refusal that frees the
  pack, its frees counted apart as every freed pack's are (below). A document can place its nodes from the
  next `setGraph`. The block thread first meets a pack through an instance in a plan it adopts, so the plan
  swap's ring carries the whole built pack across; a pack's audio runtime joins what packs share (the call
  deadline's interrupt handler, the allocation counters) only once its load is done, so what a loaded pack's
  load allocates is counted apart from `audioAllocations`.
- **Reloading one.** The same id loaded again builds the pack's next generation beside the current one
  (numbered by the id's count of loads, refused ones included, so a number names one source), and the
  registry swaps the package's entries for the new generation's, keeping the old ones valid for the
  instances that hold them. The Engine plans the request `setGraph` last published again, with no tap
  re-arm (the running plan's tap stays as it is, neither restored nor armed again); the planner rebuilds every
  instance whose registry entry changed, so each of the pack's
  nodes gets a fresh instance of the new code under its node id, its JS state not carried over, and every
  other node keeps its instance. A document that places none of the pack's nodes is not planned again. All
  or nothing: a replan the planner refuses (a node's settings the new code refuses, a type the document
  places gone, or `busy`) puts the old entries back and discards the new generation, and the result carries
  the planner's errors, the old code running on.
- **Freeing a generation.** A pack counts the records of its types' instances alive (made and deleted on the
  control thread, so a plain count). A generation a reload retired, or one a refused replan discarded, is
  freed by the control-thread call that finds its count 0 (`retirePack`, `discardPack`, `reclaim`,
  `setAudioThreadOwns(false)`): the registry drops a retired one's entries first (the pack-freed callback),
  then its audio runtime leaves what packs share, so its frees are counted apart, then its runtimes go. So a
  whole generation's heap is freed off the audio thread, while it runs blocks in other packs: the block
  thread holds nothing of a pack none of whose records is alive. With no audio thread running, a reload frees
  the old generation within the call. Under one, the old generation goes at the first reclaim after its
  records come back, which they do once a reclaim has freed the plan that held its nodes and a block has
  released them: the Engine reclaims first in `setGraph` and in every `loadScriptPack`, and the plugin (while its
  editor is open) and the SDL standalone reclaim at every frame of their idle tick too, so there the generation goes
  within a few frames of the replan; a host with no tick while audio runs (a plugin whose editor is closed, the harness
  hosts) frees it only at those calls, a stop or `drainReleased`
  ([07-remaining-work.md](07-remaining-work.md) section 5). A record the release ring back could not take,
  leaked rather than freed on the audio thread, keeps its pack for the host's life.
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
  What released instances, and the built-in bundle's load, leave in cycles is collected while no block
  runs, by a GC of each pack's audio runtime that has some: at the reclaim that ends a quiescent operation
  (the Engine's after every `setGraph`) and when the audio thread hands the runtimes back. A loaded pack's
  load collects its own before it returns.
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
  out**. The **drift-exact PPQ tick clock lives entirely in JS** (`walkTicks`, which each clocking
  protocol node runs); native passes only the block's transport in.
- **Promise jobs** run at a checkpoint right after each call the block thread makes into a node (its
  `onSettings`, `portsChanged`, `activate` or `process`, after the audio outputs are copied out, and the
  `create` a fresh instance's first call makes before it, whose jobs run with that call's, or at a
  checkpoint of their own when the call has no hook to run), at most 256 (`kJobsPerCheckpoint`), inside
  the call's deadline, and are charged to that node: a job that fails (a `queueMicrotask` callback that
  threw, or the platform's allocator out of memory) is its throw, and faults it under `faultOnThrow`;
  one the deadline stops counts as its call stopped; and a chain still pending after the bound is cut
  short, its throw (`jobsCutShort` counts it), which faults a `faultOnThrow` node at once and any other
  on the second in a row. QuickJS cannot drop a pending job, so a chain cut short stays queued, and its
  pack remembers whose it is until the queue empties: its later checkpoints (the next calls into any of
  the pack's nodes) run the rest charged to that node, never to a pack-mate, and once that node is
  released the rest is no node's. Every other job those checkpoints run meanwhile is charged to that
  node too, a pack-mate's own among them: a pack-mate that starts a chain of its own that never settles
  while another node's leftovers are pending has its cut shorts charged to that node, which faults for
  them while the pack-mate runs on, and once that node is released the chain is no node's, the pack-mate
  that started it never charged or bypassed, until the pack is reloaded. What is queued where no call
  into a node ran runs there and then, charged to the remembered owner, else to no node: what a release
  freeing a node's JS queues, and what a quiescent collection queues by finding a `FinalizationRegistry`
  callback's target when the queue was empty before it (queued behind another node's leftovers, those
  jobs wait with them and are charged to that node); what a reshape's freeing queues runs at a
  checkpoint after its commit, charged to its node. Only a reload, which frees the pack's runtime, ends
  a faulted node's chain; it runs, bounded, in its own pack. A load runs the jobs it queues as it loads,
  at most 256 each stretch of it (its top level in each runtime, each descriptor, what reading the
  definitions after the last one queues, what a loaded pack's collection queues), and one that leaves
  any is refused, as every other load failure is: QuickJS cannot drop a pending job, and one a load left
  would be charged to whichever of its nodes' calls came first, or, in the control runtime, would run at
  its first settings check.
- **Memory.** Each of a pack's two runtimes is held to a memory limit (64 MiB unless its load says
  otherwise), each on its own, by the host's allocator, which refuses an allocation past it, so QuickJS
  throws out of memory in the call that asked, and notes the refusal on the pack. In the audio runtime, a
  refusal during a call, or its jobs, faults the node they are charged to whatever its `faultOnThrow` (a
  refusal JS caught counted as its throw): at once in `process`, at its next block after a hook or for its
  leftovers run at a pack-mate's checkpoint or where no call ran (a release's run), since a pack at its
  limit is one none of its nodes can rely on. Only an out-of-memory error the limit did not cause (the
  platform's allocator ran out) is a throw like any other, faulting under `faultOnThrow`. A call is charged
  only what its pack refuses after it begins (the note is cleared as each call arms its deadline), so a
  refusal while a release frees a node's JS, in a quiescent collection or during a load faults no node. In
  the control runtime the limit holds the pack's top level, `parseSettings` and `layout`: a load or a
  settings check that fails on an allocation it refused is refused, saying so (a settings check's refusing
  the document, the running plan standing), and one that catches the error runs on, with no node to fault.
  The limit holds each runtime whole, its intrinsics (about 90 KB) and its code included, so a limit too
  small for those refuses the load, which says so. Another pack is untouched. "Past the limit" means
  reachable and uncollected bytes together: a refusal comes at once, with no collection first, and QuickJS's
  own cycle GC runs only as an object is made, once the heap has grown half again since the last one, so a
  pack whose reachable heap is above about two thirds of its limit can be refused for garbage, and a profile
  window that pins the auto-GC off (`resetAllocStats(true)`) gathers garbage until the limit faults a node.
- **A throw** in `process` is caught and counted, the block's output up to it stands, and the node runs
  again next block; a definition that says `faultOnThrow`, or a `process` that returns `"fault"`, faults
  instead (bypassed from then on), and a `faultOnThrow` node whose `onSettings`, `portsChanged` or
  `activate` threw faults at its next block. Throws in `create`, `onSettings`, `portsChanged` and
  `activate`, and a job that fails (a `queueMicrotask` callback that threw, or a job stopped by the
  deadline or out of memory), are counted too (`Engine::scriptThrows()`; a promise reaction's own throw
  only rejects its promise); the first one's text is kept on the block thread in a fixed buffer and logged
  once by `reclaim`. A write the block path makes onto what a node holds that fails (a setter the node hung
  there threw or ran past the deadline, or it froze the object) is the node's throw too: `messages()` and
  `control()` throw it, and on its audio views, the `Block` or its transport, written before `process` is
  called, it is a throw in `process` whose block does not run. Every node of a pack shares its realm, so
  the host keeps its writes, and its reads of a node's methods, off the prototypes they share: each field
  it fills is the object's own data property from when it makes the object (a define, which never walks the
  prototype chain), and each array slot is set when it is the array's own at that write and defined when it
  is not (never made, or taken back by a shift, a splice or a shorter length). So only the node's own
  setters run there, never an accessor another node (or the bundle) hung on `Object.prototype` or
  `Array.prototype`, with one gap: a field the node deletes is set again, which walks the chain, as does
  the read of a message's `data` it deleted (asking at every field write would cost each script call about
  12 ns a block, 14% of an empty pass-through's). `process` and the hooks are read from what `create`
  returned and the prototypes it inherits them through, short of `Object.prototype`, so none is run or
  adopted from there. A Proxy on the way answers as JS reads one (its `get` trap, or its target's chain),
  so a wrapper or a test double works; a chain longer than `ScriptHost::kMethodHops` (1024) plain objects,
  or one that cycles back through a Proxy, fails the `create` rather than holding the block thread. The
  `Block` prototype every node of a pack reaches is frozen with its methods, so no node replaces one for
  another: assigning one on a node's own `Block` (`block.send = f`) throws in the strict bundle, where
  `Object.defineProperty` on it still works. That is the host's part only, and it guards against the nodes
  of one pack: what they can do to each other through the realm they share (an accessor on a prototype
  another's own code reads through, a global it calls) is left as it is, since one author wrote them all.
  Nodes of different packs share no realm.
- **The call deadline.** Every call the block thread makes into a node's JS, with the `create` a fresh
  instance's first call makes and the promise jobs its checkpoint runs, is held to one deadline of the
  calling thread's CPU time (2 s by default, `setCallDeadline`), so a stall that is not the node's own
  never trips it. A call that runs past it is stopped and counted as a throw: a `faultOnThrow` node faults,
  any other runs again and faults only if its next call is stopped too. The control thread's calls into a
  pack are held to the same length of its own CPU time, on a clock of its own: a load is one call (its
  chunks in both runtimes, the jobs they queue, its definitions read), and so is a settings check
  (`parseSettings` and `layout` with their jobs). A load the deadline stops refuses the pack; a settings
  check it stops refuses the settings, so the planner refuses the document and the running plan stands. A
  settings check runs at most 256 of the promise jobs pending in its pack's control runtime, and one that
  leaves any refuses the settings too, saying so; QuickJS cannot drop them, so they wait there for the
  pack's next settings check, which runs them with its own, and a chain that never settles refuses every
  settings check of the pack, and so every document placing one of its nodes, until a reload frees that
  runtime. The planner checks every placed node's settings at every plan, so a node whose settings check
  fails (its `parseSettings` or `layout` throws, runs past the deadline, refuses the settings or reaches
  its pack's memory limit, or its pack's control-side jobs do not settle) makes every document placing it
  refused, as settings it refuses always have: while it is placed no other edit to the graph lands either,
  the running document published again among them, and neither does a reload of another pack whose nodes
  the running document places beside it, refused with that node's error after "the running document does
  not plan with the reloaded pack: ". Removing it (every node of its pack, for a chain that never settles)
  or reloading its pack clears it. Every runtime's JS stack is held to 256 KiB, under a macOS secondary
  thread's 512 KiB.

Every entry into a runtime re-anchors with `JS_UpdateStackTop` because the driving thread differs
(the control thread on the pull path, the audio thread while running), and QuickJS's stack-overflow
guard is calibrated against the thread that last entered it. A profile build profiles the audio
runtimes there, every pack's summed ([08-profiling.md](08-profiling.md)).

[`ScriptCompiler`](../packages/native/src/host/dsp/ScriptCompiler.hpp#L26) (`dsp::compileToBytecode`)
compiles a script to QuickJS bytecode on a **transient, bare context**: `Engine::loadScriptPack` compiles a
pack's source with it, and the engine-graph tests their toy script nodes. A compile error comes back as its
text and line (`the source does not compile: line 3: SyntaxError: ...`; QuickJS gives no finer position),
and the parser is held to the script host's 256 KiB stack, since the source is a user's and the thread the
Engine's control thread. The bytecode keeps its debug information (the built-in bundle's is stripped at
build time) and is version-locked to this exact qjs-ng build, which is fine because the compiler and the
script host are the same `qjs`.

## The console packages' classes, as the host wraps them

The host links no console: a product links the console packages it lists
([`packages/sameboy`](../packages/sameboy), [`packages/mesen`](../packages/mesen)), and every one of them builds on the
contract in [`packages/system`](../packages/system) (F2,
[modular-graph-phase-f2-design.md](../docs/modular-graph-phase-f2-design.md) section 2). What the engine and its nodes
wrap:

| Shared class | Host use |
|---|---|
| [`SystemBase`](../packages/system/native/src/system/SystemBase.hpp) | the abstract core; owned by its system node in the running plan (`SystemNode<T>`, [SystemNode.hpp](../packages/system/native/src/system/SystemNode.hpp)); drives `onActivate`/`onProcess`/`framebuffer`/`saveStateBytes`/`readStateSnapshot`/`stateRegions`/`enableStateSnapshot`/`pressButton`/`pushSerialIn`/`reserveAudio`. Its surface is what the engine and the nodes call: no battery-path accessors, no `fastBoot()` getter, no `clearSram`, no sub-block MIDI out, and no memory-snapshot arming (C4). The capabilities beside `debugTarget()` are `channelTap()` and, since C5, the nullable `memory()` / `sram()` / `rom()` accessors a system node presents to the capability job model (`system/SystemCapabilities.hpp`), and the per-block contract is `reserveAudio` plus `kDefaultBlockFrames`; its ingress queues report `inputAccepted()` / `inputDropped()` |
| [`SameBoySystem`](../packages/sameboy/native/src/system/sameboy/SameBoySystem.hpp) | the GB core; its node gives it its knobs through value-guarded setters (`setModel`, `setHighpass`, the display group, `setSerialOutCapture`); its `linkPeers_` are the serial cable, set by its node's `rp.link/1` `set_peers` (the other members of its coupled group, in member order) |
| [`MesenNesSystem` / `MesenGbaSystem` / `MesenSmsSystem`](../packages/mesen/native/src/system/mesen/) | the NES, GBA and SMS/GG cores; the NES is the one core with a [`ChannelTap`](../packages/system/native/src/system/ChannelTap.hpp) today (it implements the interface itself, `channelTap()` returning `this`), so it is the `SyncTap` target. The engine's tap names no console: it forward-declares `ChannelTap` and includes no core header, and with C5's cart node it has no console check at all |
| `SampleCache`, `util/MinizZip` | the offline seam's file decode (miniaudio, mono float at the file's own rate, the decode a kit compile reads its sources through) and `zip`/`unzip`. A baked-in ROM is not the shell's: a package embeds it (its manifest's `retroplug.embed`), and the product's generated `rp_embedded_resources()` answers `putResource`'s marker (both in [graph/Packages.hpp](../packages/graph-runtime/native/src/graph/Packages.hpp)). `lsdj/Effects` is the kit path's reference arithmetic now, compiled into the two test binaries that hold the DSP nodes to it, not into the core |
| `FrameBufferTriple` / `MemorySnapshotTriple` | the transport **primitives** the snapshot registry is built on (the command and release rings use `SpscRing`, which lives in `packages/node-sdk`) |

The graph the engine runs on is its own packages too: the executor, planner and
[`GraphHost`](../packages/graph-runtime/native/src/graph/GraphHost.hpp), with the `rp.host` package's
`rp.host.audio-out`, are the `retroplug-graph` / `retroplug-graph-exec` libs (`packages/graph-runtime/`, over the node
SDK in `packages/node-sdk/`); the `rp.core` mixing nodes are `retroplug-core-nodes` (`packages/core/`); the console
nodes are their own packages' (`system/sameboy/SameBoyNode.cpp`, `system/mesen/MesenNode.cpp`), the host MIDI I/O the
host-io package's, the cart's node the N8 pack's and the control surface's two the Launchpad pack's; and the script host
(`src/host/script/`), the engine and the RPC services are `retroplug-backend`, the shell.

ROM classification (the legacy build's C++ `RomSniffer`) is TS-owned, and since F4a it is the CONSOLE PACKAGES' data:
each console package contributes a descriptor saying how its ROMs are recognised (its detectors in rank order and the
extensions it falls back to), which node type places a system, the settings and construct fields that node reads, its
split modes and its link cable, so `packages/retroplug/src` and `packages/retroplug/cli` have no platform union or
table keyed by one and reach a console or a cart only through its package (the boundary check's rule 9 over both; the
UI's menu contributions and the CLI's tools arrive through the generated `ui/generated/menus.ts` and
`cli/generated/tools.ts`). The LSDj sav codec is TS-owned too - neither is in the
C++ tree.

### The DSP targets and the `rp.dsp` packages

The kit path's DSP and the graph's DSP nodes are the same kernels, in `packages/dsp/native/src/dsp/`, split across
five targets: `retroplug-dsp-kernels` (gain and peak, the eight RBJ biquads, dither and quantise, the DMC encode; the
C++ standard library alone), `retroplug-dsp-resample` (the r8brain resampler, which brings the heap, pthreads and
RTTI), `retroplug-r8brain-base` (r8brain's static members, one vendored translation unit compiled on an allocator that
throws, with no target declared for the vendored library; kept off RetroPlug's own warning level), and the two node
packages `retroplug-dsp-nodes` (`rp.dsp.gain`, `.biquad`, `.dither`, `.normalize`, `.dpcm`) and
`retroplug-dsp-resample-nodes` (`rp.dsp.resample`), in the `dsp` package's
[CMakeLists.txt](../packages/dsp/native/CMakeLists.txt). `retroplug-backend` links neither (nothing of the shell calls a
kernel: the one header it reads, `util/Hash.hpp`, is a header), and `retroplug-graph` links neither; `retroplug-backend`'s products link both packages and `Engine.cpp`
registers them, beside the other static packages, through the product's generated `rp_register_packages()`, the
offline job pool registering them in its own registry too. The split is load-bearing: `rp.dsp` never reaches r8brain,
so a Pico build links `retroplug-dsp-nodes` and `retroplug-dsp-kernels` alone, leaves `dspResamplePackage()` out of
its registration, and has neither the resampler nor r8brain in its binary, where the planner's `heap` refusal is only
the second line (`rp.dsp.dpcm` declares `heap` and stays in the package, so the split is r8brain's link-time cost, not
allocation). `rp.dsp.resample` reads an `audio-buffer` resource named by its settings at that buffer's rate and
outputs at the plan's; a recorder and a resampler on one buffer in one plan are unsupported (the planner refuses a
resource bound to a port reader when the same resource has a port writer, and a settings ref is not a binding it
sees), so a driver fills a buffer before the plan that plays it. `rp.dsp.normalize` and `rp.dsp.dpcm` read a buffer
the same way, and the dither's `seedFrom` names one too, so a kit-shaped document can name one buffer twice (the
player's port and the settings ref). A rate change resumes where the output had reached, to within one frame, so a
fresh r8brain run-in is heard from there rather than a gap. `rp.dsp.normalize` measures its peak over the channels it
plays (the loudest of each played channel's peak), and `rp.dsp.dpcm` refuses a multi-channel buffer rather than mixing
one.

Two of the nodes guard their state, because nothing between a plan and the host output rejects a non-finite sample:
`rp.dsp.biquad` resets a channel's filter and writes that block as silence when the filter comes out of it non-finite
(a frequency above the plan rate's Nyquist makes the RBJ poles unstable, and the kernel's output clamp passes a NaN
straight through), and `rp.dsp.dither` restarts a channel whose state came out non-finite (a non-finite input sample
would latch its error terms for the rest of the stream). The guards are on state, not on a document's values: a node
hands its kernel the values it reads, because a clamp would be a different kit byte (the planner and `setParam` clamp
a document's value into a param's range with no diagnostic, and a wired value is not clamped at all). The param ranges
cover any sane kit value (gain -100..100, frequency 1 Hz to 96 kHz, Q 0.001 to 1000, -96..96 dB), so a kit's values
reach the kernels through them whole; the planner cannot see the plan's rate, so a kit compile refuses a frequency at
or above its source rate's Nyquist itself. Each guard reports into `rp.host.guards/1` when the host offers it: the
offline job pool does, and counts the reports into its job's completion. Both guards differ from the kernel run
directly in a way a kit could see, so the compiler refuses the regimes rather than choosing between them: a filter at
or above its source's Nyquist, a source with a non-finite sample, a pass that records a non-finite sample and a pass a
guard stepped into each refuse that slot ([modular-graph-phase-d2-design.md](../docs/modular-graph-phase-d2-design.md)
section 11). A dither node's channels each draw their own noise, channel `c` from the setting's seed plus `c` times a
golden-ratio step, so a stereo patch's dither is decorrelated and channel 0 is the setting's own seed (a mono patch is
unchanged), unless `seedFrom` names a buffer, whose first channel's hash seeds the stream instead; a kit compile is
byte-identical because it refuses rather than recovers. No kit byte moves either way: the nodes are held to those
kernels by their own tests.

### Packages, generated registration and device mode

The node SDK, the graph runtime and the `rp.core` and `rp.dsp` nodes are workspace packages since E1
([modular-graph-phase-e1-design.md](../docs/modular-graph-phase-e1-design.md));
[cmake/packages.cmake](../cmake/packages.cmake) reads each `packages/<name>/package.json`'s `retroplug.native`
manifest (an object or a list, one per `rp_package`, with a per-entry `device` flag) with `string(JSON)`, and its
`retroplug.script` member (E4: `entry`, `device: true`, `heapBytes`) for a TypeScript pack.
`retroplug_product(<target> PACKAGES ...)` generates that product's `rp_register_packages()` (declared in
[`Packages.hpp`](../packages/graph-runtime/native/src/graph/Packages.hpp)), which `Engine.cpp` iterates in order; the
order is load-bearing (`rp.host` after `rp.dsp.resample`) and the `[packages]` golden holds it. The plugin is one
product whose targets are every binary DPF builds from the plugin's libraries; `retroplug_check_products` fails the
configure, at the end of the root's CMakeLists, for a binary that links a library carrying the engine (computed outward
from `retroplug-backend`) and is none, since a Linux module would only fail at load. The offline
pool keeps its own fixed subset (`core` and `dsp`).

`node.hpp` is the guide a node author reads and is built two ways. The portable half (`node_core.hpp` and
`node_device.hpp`, with `fields.hpp`, `enums.hpp`, `settings_types.hpp`, `settings_msgpack.hpp` and
`msgpack_reader.hpp`) compiles with `RP_DEVICE` and no exceptions or RTTI; the desktop half (`node_desc.hpp`,
`settings_json.hpp`) holds the descriptor, `layout_for`, the JSON parse and the heap-settings instance. A device
instance owns its `Settings` in the bytes `query_memory` counted, with the resource pins, and reads a msgpack blob
(`rp_settings.encoding` is `RP_SETTINGS_MSGPACK`) with a reader of our own that matches fields by name, defaults a
missing one and skips an unknown one; the desktop reads the same blob for a type whose settings are portable, and
`settingsToMsgpack` converts a node's JSON settings. Settings use `rp::text<N>`, `rp::list<T, N>`, `rp::choice<...>`,
numbers, bools, numeric `rp::resource_ref`s and enums with a fixed underlying type, no `operator|` and enumerators
within -256 to 256 (the same limits hold for `enum_param`). A device-capable node source wraps `validate`,
`adjust_layout` and the helpers only they use in one `#ifndef RP_DEVICE`; the desktop build compiles a
`<target>-device` object library from every `device: true` target (the `RP_DEVICE` flags, nothing linked) so a missed
guard, a pointer or non-portable `Settings`, or a `heap` node fails the desktop build. `rp.sdk.settings-json/1` is an
SDK interface every `node.hpp` type answers, writing an instance's settings as JSON. The device image and the link
trap are in [06-build-test.md](06-build-test.md).

**Script packs and package memory (E4).** A package with a `retroplug.script` member is a TypeScript pack whose nodes
run on MicroQuickJS, in the device-mode script host (`MqjsHost`, `packages/graph-runtime/native/src/script/`), on the
desktop and on the box alike ([modular-graph-phase-e4-design.md](../docs/modular-graph-phase-e4-design.md));
desktop-only packs stay on QuickJS (the script host above). The engine is `deps/mquickjs`, vendored, behind
`rp_mqjs_engine.c`, which wraps the fixes (`rp_mqjs_names.h`, force-included, renames its symbols).
`build-device-pack.mjs` bundles the entry, lowers it to ES5, scans it for library calls the engine lacks, compiles the
bytecode the generated `pack64.c` / `pack32.c` carry, and runs that bytecode on `retroplug-mqjs-run` (a transcript and
a heap check), with the type ids a device registers from without running any JavaScript. A pack's memory is one block
`[host state][a private copy of its bytecode][the context, heapBytes]`, declared by `rp_package`'s `memory` hook
(`query`, `query_target`, `setup`, `teardown`, with `ctx`), which the loader sizes once at the target's width, places
before the nodes and sets up before the first is created; a node asks for its package's block through
`rp.host.packages/1`. Each node's context loads the pack's device runtime bundle (no settings parse, no descriptor),
the desktop's control context the whole pack. A node's `process` runs under a deadline read from `rp.host.clock/1` (10
ms and 250 us a message on the box, 500 ms of thread CPU time on the desktop with no clock), a throw, an overrun or a
dead heap faults the node (`rp.script.fault/1`) and a dead heap every node of its pack, and a definition's `idle:
"skip"` keeps a block with nothing to do off the interpreter. The block API gains `forward(port, message)`.
`rp.host.packages/1`, `rp.script.fault/1`, `rp.script.heap/1`, `rp.host.clock/1` and `rp.host.script-log/1` are
declared in [rp_script.h](../packages/node-sdk/native/src/graph/rp_script.h) and `rp_node.h`.

**Providers.** A pack's desktop entry may name a `provider` (a function returning an
[`rp_provider`](../packages/node-sdk/native/src/graph/rp_provider.h): `create`, `restore`, `rpc_service`, `destroy` and,
last, `wake`, led by an ABI version) and an `rpc` registration, which is how the N8 pack ([packages/n8](../packages/n8),
[modular-graph-phase-e2-design.md](../docs/modular-graph-phase-e2-design.md)) and the Launchpad pack
([packages/launchpad](../packages/launchpad), [modular-graph-phase-f3-design.md](../docs/modular-graph-phase-f3-design.md))
start their device services in a product that names no class of theirs. `retroplug_product` generates, per desktop
product, a header-only `rp_product_providers.hpp` (`rp_start_providers`, `rp_restore_providers`, `rp_stop_providers`,
`rp_wake_providers` and `rp_register_package_rpc<Server>`, over an `rp::Providers` the product holds per instance); a
product starts its providers after its engine and before its first graph, mounts their facets where it mounts its own,
and in its destructor reclaims its plans (`Engine::clearGraph`) and then stops them. The SDL standalone and the headless
host restore them where they restore their own state and wake them once per frame; the CLI runs no frame loop and has
nothing of its own to restore, so it neither wakes nor restores them. A product starts only the providers it NAMES in CMake
(`PROVIDERS <package>...`: no default, started in the order given, duplicates refused, and a manifest item may not be
both an `entry` and a `provider`), so listing a pack is not starting it, and a pack's provider target is linked only
where it is named: the render host, the Node addon and most test binaries carry none of it. A provider installs its
links into the engine's device registry through `rp_device_host` (`host/devices/ProviderHost.hpp`), and the ABI at /2
carries the calls a provider needs from the product that owns the shared MIDI stream (`reserve_midi`,
`suspend_hardware_inputs`) and the product's callback for the startup rendezvous (`restored`). The configure check
refuses a provider or an `rpc` on a `device: true` entry.

**A pack's facet is mounted whole, in every product that lists it (decided).** `rp_register_package_rpc` makes one
`registerRpc` call per provider and the manifest's `rpc` is one header and one function, so the N8 facet's 34 methods
are all mounted wherever it is: the plugin's and the SDL standalone's channels carry the session half (`n8Open` on any
port name, `n8MemWR`, `n8FifoWR`, `n8ReadData`, which neither uses) and the CLI the link half (which persists `n8.cfg`),
and only a convention keeps the CLI's scripts on sessions. That is wider than the capability facets above, which
drop debug, harness and driver from the plugin's channel; it is accepted because no untrusted code reaches a product's
channel today (its callers are the product's own UI and the user's own scripts, which can drive the hardware through
the link half anyway). A product can be scoped later, by an `rpc` list with a scope per entry and a facet split into a
link and a session registration, if packs become downloadable. A pack prefixes its method names (the N8's are `n8*`):
the server's names are global and a repeat replaces without a word. The N8 port itself is exclusive on POSIX
(`SerialTransport` takes `TIOCEXCL` after it opens), so a second instance, a CLI session or minicom is refused with
"busy" rather than interleaving frames on one cart; N8Rpc.hpp has the rest.

### Kit reproducibility

A kit compiles to the same bytes every time. The compiler is TypeScript (each cart's `ts/kitFormat.ts` over
`@retroplug/dsp/kitJobs`): it builds each pass of a slot's chain as a graph document and packs the bank, and native does
every float the bytes depend on,
through the `rp.dsp` nodes in offline jobs (the seam below). The two TPDF dither algorithms seed their `std::mt19937`
from `rp::dsp::inputSeed` over the resampled, pre-trim sample the dither receives, salted with a per-algorithm constant,
and the other three algorithms are deterministic already. The kit baseline
([kit-baseline.test.ts](../packages/retroplug/test-native/kit-baseline.test.ts)) compiles a fixed matrix of synthesised
sources through that compiler and holds each bank's FNV-1a-64, size and slot length to
[golden/kit-baseline.json](../packages/native/test/audio/golden/kit-baseline.json): exact bytes on `linux-x64-gnu13`,
self-agreement and the layout facts elsewhere, with `RP_KIT_GOLDEN_REQUIRE_TARGET=1` on CI's linux-x64 job and
`RP_KIT_GOLDEN_UPDATE=1` re-recording on that target only. A second golden,
[kit-edges.json](../packages/native/test/audio/golden/kit-edges.json), holds the edge banks (`kit-compile.test.ts`)
under the same rules. [06-build-test.md](06-build-test.md) has both suites' rules. A pass that records a non-finite
sample, or one a node's guard stepped into, refuses its slot, which is how a compiled bank stays the kernels' bytes
when a finite source overflows somewhere in the chain.

### The offline job pool

[`OfflineJobs`](../packages/native/src/host/engine/OfflineJobs.hpp) runs two kinds of job. A **graph job** is the
document run described below. An **engine job** (D3,
[modular-graph-phase-d3-design.md](../docs/modular-graph-phase-d3-design.md))
is a render: the worker pins the default floating-point environment, builds a
[`RenderHost`](../packages/native/src/host/render/RenderHost.hpp) (a bare QuickJS runtime) whose `run()` builds the
job's own `Engine`, in-process RPC services and server, evaluates the embedded render bundle synchronously and returns,
and everything is destroyed on that worker before the completion is published. The job's engine runs continuous blocks
(`EngineRpcService::setContinuousBlocks`, C++ only): `renderAudio`, `renderAudioPerSystem` and `renderAudioPerChannel`
run whole 1024-frame blocks and carry the surplus to the next call of the same shape, system, stream count and rate.
Progress (rendered milliseconds, a running maximum) and the script's console lines go into a per-job
`EngineJobRecord`, bounded to 256 lines and 64 KiB between drains. Engine jobs run on at most `engineBudget()` workers,
all but one of the pool's size (one for a pool of one, whose `maxWorkers()` of 2 lets a graph job start beside a
render, or a render beside a graph job); a worker takes the oldest job its kind's limit allows. The pool is the
`Engine`'s, made at the first submission and reset before the engine's other members go. A graph job runs on one
worker thread from start to end, which is the job host's control thread and block thread at once (quiescent: no audio
thread, so `setGraph` flushes inline and `GraphHost::processBlock` is the block driver, draining then running), in
blocks of 1024 frames, the last partial, checking for a cancel between blocks (an engine job's blocks are continuous
and its cancel is polled between its boot steps and per chunk, its runtime's interrupt handler stopping a script
that has not seen one within two seconds). The pool starts workers on demand while a runnable job is queued and every
started one is busy, at most `maxWorkers()`: the pool's size, clamp(hardware_concurrency, 1, 8), or 2 for a pool of
one. A render held back by the engine budget starts no thread. A worker is never retired: an engine that ran a kit
keeps up to eight parked threads for the rest of its life (a parked wait, costing no CPU). At most 64 jobs are
outstanding at once (queued, running, or finished and not yet drained), so every taken job has room for its
completion. Handles are known before a job runs: input i is `res:(i + 1)`, output j is `res:(inputs + j + 1)`. An
input of no frames is accepted and reported but names no resource, and an output of no frames is refused. A file input
is decoded through a `SampleCache` of the job's own (so a path rewritten between two jobs decodes as it is now) and
windowed by [`offlineWindow`](../packages/native/src/host/engine/OfflineJobs.cpp), which reproduces the two kit
windows exactly: an offset clamped to the end, a zero length meaning "to the end", and a frame cap scaled to the
decoded rate in the window's own arithmetic (`float-ratio` for LSDj, `double-scale` for risa). A cap whose scaled
frame count is not finite or is past `size_t`'s range fails the job, naming the cap, rather than being cast.

A job's node registry is the pool's own, filled in its constructor with rp.core, rp.dsp and rp.dsp.resample before any
worker exists and never changed, so the workers share it read-only; a script, system or host I/O node in a document is
`unknown-type`, a node fault fails the job, and a guard report does not: the completion carries `guardsFired` and the
first `guardReason`, each buffer carries `nonFinite`, and a caller decides (a kit compile refuses the pass). The whole
run is held in the default floating-point environment, with the caller's environment saved before the default is
pinned and put back after (rounding mode and the flush bits included), so a worker that inherited its submitter's
flush-to-zero mode still computes what the kernels do. Cancelling completes a queued job unrun and stops a running one
at its next block boundary, or as it finishes when it is past its last (a job decoding a file finishes that decode
and stops before its next input; one planning finishes the plan and stops before its first block); tearing the engine
down abandons every job in flight, joins the workers and discards undrained completions, so no resampler outlives the
engine (D1's r8brain teardown rule). The TypeScript side
([`src/offline.ts`](../packages/retroplug/src/offline.ts)) turns submit-and-drain into a promise: the queue joins the
capability jobs' live set so one pump settles both, the native wait is turned on by `createAudioDriver` for hosts with
no UI tick, and runs past the 64-job bound wait in a FIFO rather than being refused busy. A port call that throws fails
the runs it belongs to, never the queue: a submit throw rejects that run, a drain throw rejects every run the queue was
holding, and the poll always clears its flag. The jobs native still holds after a drain threw are no longer counted by
the queue, so a fan-out past the bound submitted before they are drained is refused busy rather than held.

The txiki host `TjsHostRuntime` comes from
[`deps/dpf.js`](../deps/dpf.js), not the shell's own sources, and the shell compiles it into a library of its own
(`retroplug-tjs-runtime`, [packages/native/CMakeLists.txt](../packages/native/CMakeLists.txt)) that only the hosts
which instantiate one link ([modular-graph-phase-f2-design.md](../docs/modular-graph-phase-f2-design.md) section 6): the
plugin, the SDL standalone, the test host, the CLI and the class-id test. The backend itself is bare QuickJS
(`qjs`), so a binary that only needs the engine links no txiki and no libuv. The configure holds that
(`retroplug_check_runtime_libraries`, [cmake/packages.cmake](../cmake/packages.cmake)): a target that is no host of
either library may not reach `tjs`, `uv_a` or `efsw-static` by any link path, and the refusal names the whole chain.

## Build & identity

The [`retroplug-backend`](../packages/native/CMakeLists.txt) static lib is the shell: the `Engine` (and
`retroplug-graph`, the graph it runs on), the RPC services, the script host with its embedded script-node
bundle, the render host on bare QuickJS, the offline job pool and the format-neutral helpers
(`SampleCache`, `FileSync`, lodepng), and is PIC (it links into the shared plugin module). The txiki host and
the file watcher are two libraries beside it, linked by the hosts that use them. It links the packages' libraries it
names (`system`, `host-io`, `graph-runtime`, `core`, `dsp`, `launchpad`); the consoles it does not link, because
the product does: `retroplug_product` links every package the product lists. The
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

- **Live memory-region subscription.** There is no live "watch RAM" streaming path. Reads are one-shot through the
  registry read door; if live memory streaming to the UI is ever needed, the arming seam has to be built: the old
  `enableMemorySnapshot(type)` / `publishMemorySnapshots` mechanism had no caller and published nothing, and C4 deleted
  it outright, so nothing is left to enable.
- **Per-block host buttons/keys.** No node has an input for them (the DSP kernel carried empty `buttons`
  and `keys` arrays until B3b) and no host feeds them yet: this blocks the raw LSDj Keyboard mode, which
  needs `keys` on `rp.lsdj.sync` ([07-remaining-work.md](07-remaining-work.md)).
- **The snapshot double-copy collapse** (§`SnapshotRegistry`) is a documented redundancy, not a
  bug — a pending refactor, see [07-remaining-work.md](07-remaining-work.md).

## Key files

- [`BackendRpcRegistration.hpp`](../packages/native/src/host/rpc/BackendRpcRegistration.hpp) — mounts the capability facets onto a server; the single source of truth for the wire surface.
- [`Engine.hpp`](../packages/native/src/host/engine/Engine.hpp) / [`.cpp`](../packages/native/src/host/engine/Engine.cpp), [`OfflineJobs.hpp`](../packages/native/src/host/engine/OfflineJobs.hpp) / [`.cpp`](../packages/native/src/host/engine/OfflineJobs.cpp): the single-threaded owner of the graph (with its script host), the staged host MIDI, the host outputs, the registry and the offline job pool; `processBlock`, `setGraph`, `setParam`, `loadScriptPack`, `submitOfflineJob`, `drainOfflineJobs`, `cancelOfflineJob`, the command handler, `SyncTap`.
- [`EngineInvoker.hpp`](../packages/native/src/host/engine/EngineInvoker.hpp) / [`.cpp`](../packages/native/src/host/engine/EngineInvoker.cpp): `QueuedInvoker`, the one command path; [`DspCommand.hpp`](../packages/native/src/host/engine/DspCommand.hpp).
- [`host/graph/SystemTable.hpp`](../packages/native/src/host/graph/SystemTable.hpp): the plan's slots, by system id.
  The node types are their packages': [`MesenNode.cpp`](../packages/mesen/native/src/system/mesen/MesenNode.cpp) (the
  three Mesen consoles as nodes, with their settings and params), the Game Boy's beside it
  ([`SameBoyNode.cpp`](../packages/sameboy/native/src/system/sameboy/SameBoyNode.cpp)), the host MIDI I/O nodes'
  ([`HostIoNodes.cpp`](../packages/host-io/native/src/host/graph/HostIoNodes.cpp) and the service they read,
  [`HostServices.hpp`](../packages/host-io/native/src/host/graph/HostServices.hpp)), and the control surface's two
  ([`SurfaceNodes.cpp`](../packages/launchpad/native/src/launchpad/SurfaceNodes.cpp)).
- [`GraphHost.hpp`](../packages/graph-runtime/native/src/graph/GraphHost.hpp) /
  [`.cpp`](../packages/graph-runtime/native/src/graph/GraphHost.cpp),
  [`rp_node.h`](../packages/node-sdk/native/src/graph/rp_node.h): the one ring, plan swap and reclaim; the node ABI
  (`layout_for` per type).
- [`SnapshotRegistry.hpp`](../packages/native/src/host/engine/SnapshotRegistry.hpp) / [`.cpp`](../packages/native/src/host/engine/SnapshotRegistry.cpp) — the owned read door.
- [`SystemBuild.hpp`](../packages/system/native/src/system/SystemBuild.hpp) - the build request and the build function its node's package carries; [`SameBoyBackend.cpp`](../packages/sameboy/native/src/system/sameboy/SameBoyBackend.cpp), [`MesenBackend.cpp`](../packages/mesen/native/src/system/mesen/MesenBackend.cpp) - the consoles' build functions.
- [`ScriptHost.hpp`](../packages/native/src/host/script/ScriptHost.hpp) / [`.cpp`](../packages/native/src/host/script/ScriptHost.cpp), [`ScriptCompiler.hpp`](../packages/native/src/host/dsp/ScriptCompiler.hpp): the script-node runtime and its packs, + the compiler a pack's source and the tests' toys go through; the generated `src/generated/scriptNodes.ts` with [`script/bundle.ts`](../packages/retroplug/src/script/bundle.ts) and [`@retroplug/core/register`](../packages/core/ts/register.ts): the bundle every Engine embeds and how it registers its nodes; and each package's `ts/protocol.ts` (with the controller's [`packages/launchpad/ts/nodes.ts`](../packages/launchpad/ts/nodes.ts)) for the script nodes themselves.
- [`HostRpcService.cpp`](../packages/native/src/host/rpc/HostRpcService.cpp), [`EngineRpcService.cpp`](../packages/native/src/host/rpc/EngineRpcService.cpp), [`DebugRpcService.cpp`](../packages/native/src/host/rpc/DebugRpcService.cpp), [`AudioDriverRpcService.cpp`](../packages/native/src/host/rpc/AudioDriverRpcService.cpp) — the RPC services behind the facets.
- [`plugin/PluginDSP.cpp`](../packages/native/plugin/PluginDSP.cpp), [`plugin/PluginShared.hpp`](../packages/native/plugin/PluginShared.hpp), [`src/main.cpp`](../packages/native/src/main.cpp), [`test/ui/UiHarness.hpp`](../packages/native/test/ui/UiHarness.hpp) — the three hosts.
- [`CMakeLists.txt`](../packages/native/CMakeLists.txt), [`plugin/DistrhoPluginInfo.h`](../packages/native/plugin/DistrhoPluginInfo.h) — build wiring + plugin identity.
