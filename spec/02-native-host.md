# 02 — The native host (`packages/native/`)

This is the C++ deep dive for the native half. It implements the thesis's
first clause — **native owns bytes and cores** — behind one narrow RPC surface. TypeScript
drives everything here over resolved paths, opaque byte buffers, and opaque config blobs;
native makes no policy decisions.

[01-architecture.md](01-architecture.md) is the canonical reference for the cross-cutting
runtime concepts this doc leans on — the three hosts over one Backend RPC surface (its
capability facets), the control-plane / audio-thread split, the command ring, the snapshot read
door, and the release ring. This doc references those by name and goes deeper on the C++
mechanics: the method tables, the `QueuedInvoker` internals, the `SnapshotRegistry` slot
machinery, the core backends, and the bare DSP runtime.

Everything below lives under [`packages/native/`](../packages/native).
The `Engine` + RPC surface compiles into one static library,
[`retroplug-backend`](../packages/native/CMakeLists.txt#L77), which both the DPF plugin and the
two test hosts link — so all three drive the *same* `Engine` over the *same* RPC surface.

## The composition root: the RPC services + facets

There is **no single facade object**. A host composes a shared
[`Engine`](../packages/native/src/host/engine/Engine.hpp#L35), a
[`SystemFactory`](../packages/native/src/system/SystemFactory.hpp) (populated by
[`registerCoreBackends`](../packages/native/src/system/CoreBackends.cpp#L9)), and the ONE
[`QueuedInvoker`](../packages/native/src/host/engine/EngineInvoker.hpp#L28), plus a handful of
stateless service instances that hold references to them:

| Service | Concern | Header |
|---|---|---|
| `HostRpcService` | filesystem / config / zip (pure, stateless) | [`HostRpcService.hpp`](../packages/native/src/host/rpc/HostRpcService.hpp) |
| `EngineRpcService` | emulator lifecycle / reads / kernel / MIDI / transport / render + profile | [`EngineRpcService.hpp`](../packages/native/src/host/rpc/EngineRpcService.hpp) |
| `DebugRpcService` | live-core inspection / stepping / breakpoints (CLI only — [09-cli-debugging.md](09-cli-debugging.md)) | [`DebugRpcService.hpp`](../packages/native/src/host/rpc/DebugRpcService.hpp) |
| `AudioDriverRpcService` | background audio thread + observation atomics (test host only) | [`AudioDriverRpcService.hpp`](../packages/native/src/host/rpc/AudioDriverRpcService.hpp#L17) |

Each service's methods are mounted onto the rpcpp server as one or more **capability facets** by
the `register*Rpc` functions in
[`BackendRpcRegistration.hpp`](../packages/native/src/host/rpc/BackendRpcRegistration.hpp) (host,
emulator, dsp-kernel, harness, debug, driver — `registerAllBackendRpc` mounts the full union).
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

- **DPF plugin** — [`PluginDSP::bootControlPlane`](../packages/native/plugin/PluginDSP.cpp#L163)
  brings up a plugin-lifetime txiki runtime, binds `__rpcSend`
  ([`:171-185`](../packages/native/plugin/PluginDSP.cpp#L171)), sets the
  sample rate **before** the bundle composes (systems bake SR at construct), evals the embedded
  control-plane bytecode, then pumps until `__rp_ready`. The editor
  ([`PluginUI.cpp`](../packages/native/plugin/PluginUI.cpp))
  attaches its LVGL display to that *same* context via the in-process handoff
  ([`SharedDSP`](../packages/native/plugin/PluginShared.hpp#L19)),
  so the UI reaches the backend through the already-bound `__rpcSend` — no second RPC bridge.
  The UI and window-geometry seams are 03's topic.
- **Native test host** — [`main.cpp:84-97`](../packages/native/src/main.cpp#L84) binds
  the same namespace, installs a `globalThis.tjs.exit` hook that records the exit code, evals a TS
  bundle, and pumps the job loop until the harness reports TAP. This is the
  `test:native` runner (`retroplug-host`).
- **Headless UI-test host** — [`UiHarness`](../packages/native/test/ui/UiHarness.hpp#L26)
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

Every mutation here routes through the `QueuedInvoker`; every read routes through the
`SnapshotRegistry`. No method walks a live core.

| Method | Signature | Behaviour |
|---|---|---|
| `constructSystem` | `(BackendConstructSpec) → bool` | TS-owned `spec.id`; `factory_.build` → `registry().claim` → `invoker_.replaceSystem`/`adoptSystem`. Returns "did it build" ([`:71`](../packages/native/src/host/rpc/EngineRpcService.cpp#L71)) |
| `removeSystem` | `(uint32 id) → bool` | `invoker_.removeSystem`; no threading branch ([`:92`](../packages/native/src/host/rpc/EngineRpcService.cpp#L92)) |
| `applySystemSetting` | `(uint32 id, string key, double value) → bool` | `gainDb` / `reloadOnRomChange` → a `ConfigField` ([`:99`](../packages/native/src/host/rpc/EngineRpcService.cpp#L99)) |
| `applyRoleConfig` | `(uint32 id, string kind, string config) → bool` | `kind=="sameboy"` only; decodes the whole role config, pushes Model/Highpass/LinkGroup/FastBoot ([`:111`](../packages/native/src/host/rpc/EngineRpcService.cpp#L111)) |
| `readState` | `(uint32 id) → optional<Bytestring>` | published savestate from the registry ([`:126`](../packages/native/src/host/rpc/EngineRpcService.cpp#L126)) |
| `readSram` | `(uint32 id) → optional<Bytestring>` | SRAM sliced from the published savestate |
| `screenshot` | `(uint32 id, string path) → bool` | encodes the registry frame to PNG |
| `getFrame` | `(uint32 id) → RpcFrame` | registry frame, raw XRGB8888 ([`:145`](../packages/native/src/host/rpc/EngineRpcService.cpp#L145)) |
| `compileScript` | `(string source) → optional<Bytestring>` | `dsp::compileToBytecode` on a throwaway context ([`:158`](../packages/native/src/host/rpc/EngineRpcService.cpp#L158)) |
| `dspLoadKernel` | `(vector<uint8> bytecode) → bool` | `invoker_.loadKernel` (hot-swap) |
| `dspSetSystems` | `(string json) → bool` | `invoker_.setSystems` |
| `pressButton` | `(uint32 id, uint32 button, bool down) → bool` | joypad transition → the addressed core |
| `renderAudio` | `(double ms) → Bytestring` | synchronous pull render, interleaved f32 L/R ([`:182`](../packages/native/src/host/rpc/EngineRpcService.cpp#L182)) |
| `setTransport` / `setBpm` | `→ bool` | queued transport ops |
| `setAudioRouting` | `(uint32 mode) → bool` | gated `mode ≤ 3` (Stereo / TwoPerInstance / OnePerInstance / ChannelSplit) ([`:321`](../packages/native/src/host/rpc/EngineRpcService.cpp#L321)) |
| `stageMidiIn` | `(vector<uint8> bytes) → bool` | ≤ 4 bytes (one MIDI message) |

`duplicate` and `reload` are **not** native methods — they are TS orchestration over
`constructSystem`-with-state plus the registry reads
([`EngineRpcService.cpp:87`](../packages/native/src/host/rpc/EngineRpcService.cpp#L87)):
duplicate pulls a source savestate and builds a seeded core; reload pulls SRAM and cold-boots
the ROM with `replaceId`. This is the thesis in miniature — native offers the primitives, TS
composes the meaning.

### `AudioDriverRpcService` — background audio thread (test host only)

The plugin drives the audio thread through DPF's `run()`; only the threaded test host mounts
this service, which spins a background thread that OWNS the `Engine` while it runs
([`AudioDriverRpcService.hpp:17`](../packages/native/src/host/rpc/AudioDriverRpcService.hpp#L17)).

| Method | Signature | Behaviour |
|---|---|---|
| `startAudio` | `→ bool` | sets `audioThreadOwns(true)` **before** spawning `audioThread_` ([`:55`](../packages/native/src/host/rpc/AudioDriverRpcService.cpp#L55)) |
| `stopAudio` | `→ bool` | clears the bit, joins, then `drainInto` + `reclaimReleased` ([`:67`](../packages/native/src/host/rpc/AudioDriverRpcService.cpp#L67)) |
| `audioCaptured` | `→ AudioCaptured{energy, frames}` | monotonic atomics for a windowed RMS ([`:78`](../packages/native/src/host/rpc/AudioDriverRpcService.cpp#L78)) |
| `sleepMs` | `(double) → bool` | |
| `systemCount` | `→ uint32` | republished atomic while running; live read when quiescent ([`:89`](../packages/native/src/host/rpc/AudioDriverRpcService.cpp#L89)) |
| `drainReleased` | `→ uint32` | control-thread reclaim of released cores ([`:98`](../packages/native/src/host/rpc/AudioDriverRpcService.cpp#L98)) |

The loop ([`audioLoop`](../packages/native/src/host/rpc/AudioDriverRpcService.cpp#L32)) runs
`while (audioThreadOwns())`: `drainInto` → republish the live count → `processBlock(1024,…)` →
accumulate energy/frames → sleep 200 µs (faster than real time, so a short `sleepMs` window
still yields plenty of audio).

### `BackendConstructSpec` (the construct DTO)

[`BackendConstructSpec`](../packages/native/src/host/rpc/BackendTypes.hpp#L29) is the one
DTO worth spelling out — it mirrors the TS `ConstructSpec`:

| Field | Type | Meaning |
|---|---|---|
| `id` | `uint32` | **TS owns the id counter**; native never allocates one |
| `romPath` | `string` | `""` when embedded |
| `embeddedRom` | `string` | marker, e.g. `"mgb"` (`""` when file-backed) |
| `platform` | `optional<string>` | `"gb"` / `"nes"` / `"gba"` — which system a multi-platform core builds |
| `core` | `optional<string>` | `"sameboy"` / `"mesen"` — the factory registry key |
| `savPath` / `statePath` | `optional<string>` | on-disk seed paths |
| `replaceId` | `optional<uint32>` | swap in place of an existing system (reload) |
| `sramBytes` / `stateBytes` | `optional<Bytestring>` | zip-import seed bytes (win over the on-disk paths) |
| `settings` | `optional<string>` | the backend "system"-role config JSON, decoded only by the matching backend |

## `Engine` + `QueuedInvoker`: the mutation path

The command-ring model, the `audioThreadOwns_` bit, and the release-ring ownership handoff
are defined canonically in [01-architecture.md](01-architecture.md). Here is the C++.

### `Engine`

[`Engine`](../packages/native/src/host/engine/Engine.hpp#L35) is the single-threaded,
thread-unaware owner of the live [`Project`](../packages/native/src/project/Project.hpp) of
cores plus the DSP kernel. Every op runs "now, on the calling thread"; it holds **no
atomics** — transport and the ppq clock are plain members mutated only by whichever thread
owns it (the pull path or the audio loop, never concurrently). Removed or displaced cores are
**returned** to the caller, never freed on a hot path.

Its per-block driver [`Engine::processBlock`](../packages/native/src/host/engine/Engine.cpp#L74)
is the one place every host funnels a block. In order:

1. If a kernel is active, build a `BlockInfo` at the **block-start** `ppq_`, run
   `dsp_.processBlock(...)`, and fan the kernel's system-addressed sinks to the cores
   **before** `onProcess`: `serialIn_` → `pushSerialIn`, `buttonOut_` → `pressButton`.
2. Zero every output channel (systems *sum* into their router-assigned bus).
3. `runBlock(info, project_, MultiOutRouter(outputs, numOutputs, audioRouting_))` — the shared
   [`BlockRunner`](../packages/native/src/system/BlockRunner.hpp) advances every core one block
   and the `MultiOutRouter` places each system's audio into its output pair.
4. [`registry_.publishAll(...)`](../packages/native/src/host/engine/Engine.cpp#L97) — copy every
   core's freshly-published frame/state/SRAM into the owned snapshot store.
5. Advance `ppq_` only if transport is running (so the kernel's `walkTicks` and the cores see
   the same block-start ppq).

Live config edits land in
[`Engine::applyConfigField`](../packages/native/src/host/engine/Engine.cpp#L152) — a SameBoy-only
`dynamic_cast` today, **value-guarded per field** so a whole-config re-send only acts on what
changed (a `Model` change triggers `restartEmulator()` + a link-group rebuild; an unchanged one
no-ops, avoiding a spurious restart that would nuke a restored savestate). NES/GBA gain/reload
generalise to a base virtual when those backends grow live knobs.

### `QueuedInvoker`

[`QueuedInvoker`](../packages/native/src/host/engine/EngineInvoker.hpp#L28) (in the files
`EngineInvoker.{hpp,cpp}`) is **the one mutation path — there is no Direct/Queued fork**
([`:19`](../packages/native/src/host/engine/EngineInvoker.hpp#L19)). Every control-plane edit
packs into a POD [`DspCommand`](../packages/native/src/host/engine/DspCommand.hpp#L19) and pushes
onto an `SpscRing<DspCommand, 256>`. The single control-thread bit `audioThreadOwns_` decides
who drains: while the audio thread owns the `Engine` it drains each block and the producer just
pushes; otherwise `maybeFlush()` flushes **inline** right after each push (same thread is
producer and consumer). Because the quiescent path flushes every push, the ring is empty at the
ownership handoff, so the plugin's `activate()` / the test host's `startAudio` take over cleanly.

`DspCommand` is a `Kind` tag plus a union of POD payloads
([`DspCommand.hpp:19-43`](../packages/native/src/host/engine/DspCommand.hpp#L19)):

| Kind | Payload | Ownership |
|---|---|---|
| `SetSystems` | `string* json` | owning — audio thread `delete`s after apply |
| `LoadKernel` | `vector<uint8>* bytecode` | owning — audio thread `delete`s after apply |
| `StageMidi` | `uint8 data[4]`, `len` | inline (a MIDI message fits in 4 bytes) |
| `AddSystem` | `SystemBase* sys` | owning — adopted into the pre-reserved `Project` |
| `ReplaceSystem` | `SystemBase* sys`, `id` | owning — swapped for `id`; displaced core handed back |
| `RemoveSystem` | `id` | inline — removed core handed back |
| `SetBpm` / `SetTransport` | `double` / `bool` | inline |
| `SetConfigField` | `id`, `field`, `value` | inline |
| `PressButton` | `id`, `button`, `down` | inline |
| `SetAudioRouting` | `mode` | inline |

Heavy, rare payloads (systems, kernel bytecode, systems-JSON) cross as raw **owning**
pointers — the accepted non-RT-on-rare-op pattern. The consumer
[`drainInto`](../packages/native/src/host/engine/EngineInvoker.cpp#L106) switches over the kind,
applies each into the `Engine`, and `delete`s owning payloads after apply. Lifecycle kinds are
alloc-free pointer swaps into the pre-reserved `Project`: `AddSystem` → `adoptSystem`;
`ReplaceSystem` / `RemoveSystem` → `handBackReleased(...release())`.

**Ownership handoff.** The audio thread cannot `delete` (non-RT). Displaced/removed cores go
back through a second ring, `SpscRing<DspEvent, 256> released_`
([`DspEvent.hpp`](../packages/native/src/host/engine/DspEvent.hpp)); the control thread drains it
in [`reclaimReleased`](../packages/native/src/host/engine/EngineInvoker.cpp#L162):
`popReleased()` → `registry_->release(id)` (free the snapshot slot) → the `unique_ptr` deletes
at scope end. Producer edge cases are handled without blocking or freeing in render: a **full
command ring** on an `adopt`/`replace` lets the `unique_ptr` delete the build and frees its
just-claimed slot ([`:18`](../packages/native/src/host/engine/EngineInvoker.cpp#L18)); a **full
release ring** logs and **leaks rather than block/free** in the render loop
([`:203`](../packages/native/src/host/engine/EngineInvoker.cpp#L203)); a full ring on an inline
POD command drops a lost edge, not a leak.
[`freePending`](../packages/native/src/host/engine/EngineInvoker.cpp#L178) (teardown only, ring
single-accessor) discards un-applied payloads, freeing built-but-unadopted systems and their
claimed slots.

The two block drivers are the same shape: the plugin's
[`run()`](../packages/native/plugin/PluginDSP.cpp#L152) (`drainInto` → set bpm/transport direct
→ `processBlock`), and `audioLoop` for the test host. The plugin's
[`activate` / `deactivate`](../packages/native/plugin/PluginDSP.cpp#L134) flip the
ownership bit around DPF's lifecycle, draining and reclaiming on the way out.

## `SnapshotRegistry`: the read door

The read-door doctrine — the control plane observes cores only through owned copies, never a
live `SystemBase` — is in [01-architecture.md](01-architecture.md). This is the store's
internals.

[`SnapshotRegistry`](../packages/native/src/host/engine/SnapshotRegistry.hpp#L35) is a
fixed-address `std::array<Slot, 64>`
([`:79`](../packages/native/src/host/engine/SnapshotRegistry.hpp#L79)). Each
[`Slot`](../packages/native/src/host/engine/SnapshotRegistry.hpp#L66) carries an **atomic `id`**
(0 = free, so the block thread scans by id with no rehash), `width`/`height`, three owned
triple-buffers (`FrameBufferTriple frame`, `MemorySnapshotTriple state`, `MemorySnapshotTriple
sram`), a `sramOffset`, and a `sampleAccum`. Buffers are allocated at claim and freed at
release — both on the **control thread**, never the audio thread. `publishAll` only ever writes
a slot whose system is still in `project.systems()`, which is exactly the window before its
release, so a published-to slot is never concurrently freed.

The ordering guarantees are the load-bearing detail:

- **`claim(id, sys)`** ([`:22`](../packages/native/src/host/engine/SnapshotRegistry.cpp#L22)) —
  grab a free slot (a full pool fails the construct, logged), reset it, size `frame` from the
  core's framebuffer (left **unpublished** — no frame rendered yet), and **seed** `state` from
  `sys.saveStateBytes()` + slice `sram` from `stateRegions()[Sram]`, publishing both. Then
  `id.store(..., release)` **LAST** ([`:64`](../packages/native/src/host/engine/SnapshotRegistry.cpp#L64))
  — the slot becomes visible only after its buffers are ready. Seeding is why a read right after
  `constructSystem` returns real bytes with no block yet rendered. `claim` requires the core to
  already have `enableStateSnapshot()`'d (for `stateRegions()`).
- **`publishAll(project, frames, sampleRate)`**
  ([`:68`](../packages/native/src/host/engine/SnapshotRegistry.cpp#L68)) — for each system,
  `find(id)`; copy the framebuffer **every block** (cheap); on the coarse interval
  (`kStateIntervalSec = 0.5`, matching the core's own snapshot cadence via `sampleAccum`)
  `readStateSnapshot` into `state` and slice `sram` at `sramOffset`, publishing both.
- **`release(id)`** ([`:140`](../packages/native/src/host/engine/SnapshotRegistry.cpp#L140)) —
  clear `id` **FIRST** ([`:147`](../packages/native/src/host/engine/SnapshotRegistry.cpp#L147)) so
  a stray block-thread scan can't match, THEN free the buffers. Idempotent; only runs once the
  system is out of `project.systems()`.

Reads (`readFrame` / `readState` / `readSram`) copy out of the published triple by id and
return `published: false` for an unknown, non-video, or not-yet-rendered slot. The
`Engine`'s read methods
([`readState`/`readSram`/`screenshot`/`getFrame`](../packages/native/src/host/engine/Engine.cpp#L108))
route straight here — they never walk `Project` or dereference a live core, which is the fix
for the historical live-read bug class where reads were guarded off in the running plugin.

The registry deliberately **double-copies** from each core's own tear-free triple, because the
shared `SystemBase` can't yet publish straight into the registry
([`SnapshotRegistry.hpp:23`](../packages/native/src/host/engine/SnapshotRegistry.hpp#L23));
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
[`toBuildSpec`](../packages/native/src/host/rpc/EngineRpcService.cpp#L52): `core` defaults
from `platform` via
[`defaultCoreFor`](../packages/native/src/host/rpc/EngineRpcService.cpp#L43) (nes/gba → mesen,
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

## `DspRuntime` + `ScriptCompiler`: the bare DSP kernel runner

Native runs the TypeScript DSP role kernel but understands nothing about roles — it is "a
dumb, role-agnostic runner … fed only by bytes." The kernel itself
([`dspKernel.ts`](../packages/retroplug/src/dspKernel.ts)) and its behaviour are
04's topic; this doc covers the C++ runner.

[`DspRuntime`](../packages/native/src/host/dsp/DspRuntime.hpp#L28) owns a **second, bare
QuickJS context** (no txiki — one of the two runtimes; see
[01-architecture.md](01-architecture.md)), held by `Engine::dsp_`. The kernel bundle defines two
globals when evaluated — `setSystems(json)` (the rarely-changing system structure, parsed once)
and `processBlock(input)` (run one block) — and calls three bound C sink thunks as it runs, all
**system-addressed** so one context drives every system
([`DspRuntime.cpp`](../packages/native/src/host/dsp/DspRuntime.cpp)):

| Thunk | Fills | Fanned to |
|---|---|---|
| [`pushSerialIn(system, frame, byte)`](../packages/native/src/host/dsp/DspRuntime.cpp#L17) | `serialIn_` | the addressed core's serial FIFO |
| [`emitMidiOut(system, frame, [bytes])`](../packages/native/src/host/dsp/DspRuntime.cpp#L32) | `midiOut_` | the host (DAW) after the block |
| [`pressButton(system, frame, button, down)`](../packages/native/src/host/dsp/DspRuntime.cpp#L60) | `buttonOut_` | the addressed core's joypad |

**A `JSValue` never crosses out** — the runner reads back only these byte-addressed vectors,
cleared at the top of each `processBlock`. The `DspRuntime` is set as the context opaque, so
each thunk reaches its collector via `JS_GetContextOpaque`. The **drift-exact PPQ tick clock
lives entirely in the JS kernel** (`walkTicks`) — native owns no `nextTick` / `eachTick`
primitive; it only passes `BlockInfo { frames, sampleRate, tempo, ppqStart, transport }` in.

`loadKernel` is `JS_ReadObject` + `JS_EvalFunction` on the bytecode, which runs the top-level
code that defines the globals; re-loading hot-swaps the kernel. Every entry point re-anchors
with [`JS_UpdateStackTop(rt_)`](../packages/native/src/host/dsp/DspRuntime.cpp#L102) because
the driving thread differs (the control thread on the pull path, the audio thread while
running), and QuickJS's stack-overflow guard is calibrated against the thread that created the
runtime — without the re-anchor a call from a different stack throws a spurious overflow.

[`ScriptCompiler`](../packages/native/src/host/dsp/ScriptCompiler.hpp#L17) (`dsp::compileToBytecode`)
compiles a script to QuickJS bytecode on a **transient, bare context — deliberately not the
`DspRuntime`'s** — so the DSP side is proven never to re-parse source: bytecode is the only
thing that crosses into the DSP heap. It backs the `compileScript` RPC. The bytecode is
version-locked to this exact qjs-ng build, which is fine because the compiler and DSP contexts
are the same `qjs`.

## Wrapped shared-core classes

The host links the shared core through
[`retroplug-core`](../packages/native/CMakeLists.txt#L23) (the static lib that
compiles the shared sources under [`packages/native/src`](../packages/native/src) once) and wraps
its **runtime** classes:

| Shared class | Host use |
|---|---|
| [`SystemBase`](../packages/native/src/system/SystemBase.hpp) | the abstract core; owned as `unique_ptr<SystemBase>` in the `Project`; drives `onActivate`/`onProcess`/`framebuffer`/`saveStateBytes`/`readStateSnapshot`/`stateRegions`/`enableStateSnapshot`/`pressButton`/`pushSerialIn` |
| [`SameBoySystem`](../packages/native/src/system/sameboy/SameBoySystem.hpp) | the GB core; the live-apply `dynamic_cast` target for `applyConfigField` |
| [`MesenNesSystem` / `MesenGbaSystem`](../packages/native/src/system/mesen) | the NES + GBA cores |
| [`Project`](../packages/native/src/project/Project.hpp) | the live-systems container — **runtime methods only** (`reserve`/`adoptSystem`/`removeSystemAndRelease`/`swapSystem`/`findSystem`/`systems`/`rebuildLinkGroups`); the LSDj-persistence path is TS-owned |
| [`BlockRunner`](../packages/native/src/system/BlockRunner.hpp) | `runBlock` + `MultiOutRouter` — the per-block audio driver |
| [`LinkGroup`](../packages/native/src/system/sameboy/LinkGroup.cpp) | serial link lockstep, rebuilt on every adopt/replace |
| the LSDj kit/sample pipeline (`KitCompiler` / `KitUtil` / `SampleCache` / `Effects`), `util/MinizZip`, `EmbeddedRoms` | backing kit patching, `zip`/`unzip`, and embedded-ROM resolution |
| `SpscRing` / `FrameBufferTriple` / `MemorySnapshotTriple` | the transport **primitives** the command/release rings and the snapshot registry are built on |

ROM classification (the old C++ `RomSniffer`) and the LSDj sav codec are TS-owned — neither is in
the C++ tree.

The txiki host `TjsHostRuntime` comes from
[`deps/dpf.js`](../deps/dpf.js), not the shared core.

## Build & identity

The [`retroplug-backend`](../packages/native/CMakeLists.txt#L77) static lib wraps
[`retroplug-core`](../packages/native/CMakeLists.txt#L23) with the `Engine`, the RPC services,
the DSP runtime, and the txiki host, and is PIC (it links into the shared plugin module). The
test host `retroplug-host` is that lib + `main.cpp`; the DPF plugin is
[`dpf_add_plugin(retroplug TARGETS clap vst3 vst2 au jack)`](../packages/native/CMakeLists.txt#L232)
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
([`PluginDSP.cpp:154-158`](../packages/native/plugin/PluginDSP.cpp#L154)).

## Not yet built / deferred

- **Live memory-region subscription.** There is no live "watch RAM" streaming path
  (`enableMemorySnapshot(type)`). Reads are one-shot through the registry read door; if live
  memory streaming to the UI is ever needed, that arming seam must be built.
- **Live `applyConfigField` for Mesen.** NES/GBA role knobs (Region / Remove Sprite Limit) apply
  **at construct**, not live — the `dynamic_cast` in
  [`Engine::applyConfigField`](../packages/native/src/host/engine/Engine.cpp#L152) is SameBoy-only,
  so changing a Mesen knob rebuilds the system rather than nudging the running core.
- **Per-block buttons/keys into the kernel.** `Engine::processBlock` passes `kNoButtons` /
  `kNoKeys` today ([`Engine.cpp:20`](../packages/native/src/host/engine/Engine.cpp#L20)); the
  kernel's button/key input arrays are wired but not yet fed (this blocks the raw LSDj Keyboard
  mode — [07-remaining-work.md](07-remaining-work.md)).
- **The snapshot double-copy collapse** (§`SnapshotRegistry`) is a documented redundancy, not a
  bug — a pending refactor, see [07-remaining-work.md](07-remaining-work.md).

## Key files

- [`BackendRpcRegistration.hpp`](../packages/native/src/host/rpc/BackendRpcRegistration.hpp) — mounts the capability facets onto a server; the single source of truth for the wire surface.
- [`CoreBackends.cpp`](../packages/native/src/system/CoreBackends.cpp) — registers the `sameboy` / `mesen` backends into the `SystemFactory`.
- [`Engine.hpp`](../packages/native/src/host/engine/Engine.hpp) / [`.cpp`](../packages/native/src/host/engine/Engine.cpp) — the single-threaded owner of the `Project` + DSP kernel.
- [`EngineInvoker.hpp`](../packages/native/src/host/engine/EngineInvoker.hpp) / [`.cpp`](../packages/native/src/host/engine/EngineInvoker.cpp) — `QueuedInvoker`, the one mutation path; [`DspCommand.hpp`](../packages/native/src/host/engine/DspCommand.hpp) / [`DspEvent.hpp`](../packages/native/src/host/engine/DspEvent.hpp).
- [`SnapshotRegistry.hpp`](../packages/native/src/host/engine/SnapshotRegistry.hpp) / [`.cpp`](../packages/native/src/host/engine/SnapshotRegistry.cpp) — the owned read door.
- [`SystemFactory.hpp`](../packages/native/src/system/SystemFactory.hpp), [`SameBoyBackend.cpp`](../packages/native/src/system/sameboy/SameBoyBackend.cpp), [`MesenBackend.cpp`](../packages/native/src/system/mesen/MesenBackend.cpp) — the build path.
- [`DspRuntime.hpp`](../packages/native/src/host/dsp/DspRuntime.hpp) / [`.cpp`](../packages/native/src/host/dsp/DspRuntime.cpp), [`ScriptCompiler.hpp`](../packages/native/src/host/dsp/ScriptCompiler.hpp) — the bare DSP context runner + compiler.
- [`HostRpcService.cpp`](../packages/native/src/host/rpc/HostRpcService.cpp), [`EngineRpcService.cpp`](../packages/native/src/host/rpc/EngineRpcService.cpp), [`DebugRpcService.cpp`](../packages/native/src/host/rpc/DebugRpcService.cpp), [`AudioDriverRpcService.cpp`](../packages/native/src/host/rpc/AudioDriverRpcService.cpp) — the RPC services behind the facets.
- [`plugin/PluginDSP.cpp`](../packages/native/plugin/PluginDSP.cpp), [`plugin/PluginShared.hpp`](../packages/native/plugin/PluginShared.hpp), [`src/main.cpp`](../packages/native/src/main.cpp), [`test/ui/UiHarness.hpp`](../packages/native/test/ui/UiHarness.hpp) — the three hosts.
- [`CMakeLists.txt`](../packages/native/CMakeLists.txt), [`plugin/DistrhoPluginInfo.h`](../packages/native/plugin/DistrhoPluginInfo.h) — build wiring + plugin identity.
