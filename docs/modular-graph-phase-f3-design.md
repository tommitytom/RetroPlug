# Phase F3: the Launchpad pack

**Date:** 2026-11-07 · **Branch:** `feature/modularize` · **Status:** built on `feature/modularize` (five commits; it was designed and prototyped on `proto/f3-launchpad`); sections 1 to 10 are corrected in place to what the build found,
and section 12 ("As built") records it

F3 is the second phase of the package split ([package-split.md](package-split.md)), after F1 ([modular-graph-phase-f1-design.md](modular-graph-phase-f1-design.md)).
It is a **port, not a merge**, of `feature/launchpad`. That branch forked at `cfeb80e00` (2026-09-29), before the graph
existed, and rebuilt the control surface on the old DSP kernel's controller channel; this tree has since replaced that
channel with the graph (`rp.launchpad.surface-in` / `-out` over `rp.host.devices/2`, the `rp.controller.session` script
node). Merging would fight three phases of rework to land fixes that are small once they are expressed in the graph's
terms. So F3 reads the branch as a specification: every fix it found is carried over, expressed in this tree's
structures, in a package, `packages/launchpad`, with the LSDj app in a bridge package beside it, `packages/lsdj-launchpad`.
Everything the user approved in the brainstorm is binding here; what the brainstorm left to the prototype was a question in
section 10, answered there.

The shape is the E2 shape (the N8 pack, [modular-graph-phase-e2-design.md](modular-graph-phase-e2-design.md)): a portable
native target with the nodes, a desktop target with the host's half, a provider the product starts without naming a class,
an RPC facet the TS reaches the hardware through. What is new is a TS half whose portable core is **proved on
MicroQuickJS** (as the branch proved it can run there) by the toolchain's scenario mode, and a new contribution kind,
`controllerApps`.

## 1. Before F3

Facts, checked against `10ab1e71c` and, for the branch, `git show feature/launchpad:<path>` (7b9d85459).

- **The branch** has 17 commits (2026-10-18 to 2026-10-20) over the merge-base, 84 files, +5082 / -2332. Its fixes, none of
  which is on this tree: a **relocate** (a tick jump larger than a block can cover is a seek, not elapsed time; the
  predictor must not walk through it; `controllerRole.ts` on the branch has `relocate` and `lastTick`); a **link
  generation** (a pair swapped faster than a block still reads as a new device, and every ring slot is tagged with the
  claim it belongs to); **drop repaint** (the kernel is told how many messages the link dropped, so a lost LED update
  triggers a repaint); an `isTickCount` guard and a grid **revision** (the app skips painting while page, follow, cue,
  every playhead and the grid's revision are unchanged: an idle block costs about 1.6 us); **`config.json`
  `controlSurface`** with merge-on-write (every write re-reads the file and applies only its own change); **pair
  reservation** (the claimed pair, input AND output, stays out of `MidiIo`'s shared stream); **`PortClaim`** (one claim
  per port across processes: `flock` under `<configDir>/locks`, an unshared `CreateFile` on Windows); **model
  confirmation** (the Mini MK3 and the Pro MK3 both answer the inquiry with family `13 01`; the scan follows up on the
  pair that answered with the model's own select-layout question, and the reply must have the manual's shape); **`PortScan`**
  (a deterministic state machine in TS, `controller/portScan.ts` 219 lines: `GAP_MS` 50, `MAX_REPLIES` 64, `followUp`,
  `cancel`); and the **portable core** (`controller/portable.ts`, no Map / Set / Symbol, no ES2015 library methods).
  Its design is `docs/launchpad-plan.md` section 8.6 on that branch (`git show feature/launchpad:docs/launchpad-plan.md`);
  this tree's `docs/launchpad-plan.md` has no section 8.6.
- **The branch's native transport** is `src/host/controller/` (13 files), protocol-free: `MidiPort.hpp` (the port and its
  opener), `RtMidiPort` (the only RtMidi user, SysEx on), `ControllerLink` (the claimed pair; a device-to-audio ring of
  generation-tagged vectors; an audio-to-device POD `SpscRing` with `kMaxOut` 560 and `kMaxIn` 1024; a **writer thread**,
  because WinMM blocks ~170 ms on a bulk SysEx; **close bytes** replayed on disconnect and destruct), `MidiPortPool` (loose
  ports for TS scans, inputs opened as one batch, with one held edge that suspends the shared stream's hardware inputs),
  `PortClaim`, `ControllerHost` (a `ReserveFn`, a `ClaimFn`, `restored()`) and `ControllerHooks` (11 `__rp_*` hooks). The SDL
  wiring pumps the JS loop at startup until TS calls `__rp_controllerReady` (at most 500 ms) before `MidiIo` and audio start.
  `Controller.test.cpp` has 34 cases. The plugin has nothing.
- **What this tree has instead.** `packages/native/src/host/launchpad/` (11 files): `LaunchpadLink` (one claimed pair),
  `LaunchpadHost` (persistence to `<configDir>/launchpad.cfg`, `restore()`), `LaunchpadScanner` (a native scan thread, so
  the inquiry's bytes and meaning live in C++ and TS), `LaunchpadHooks` (7 `__rp_*` hooks, `bindLaunchpadHooks`),
  `RtMidiPort` and `LaunchpadSurfaceLink`, the adapter that is the `rp::launchpad::SurfaceLink` a node acquires.
  `packages/native/src/launchpad/SurfaceLink.hpp` defines the node-facing contract: `kDeviceName` `"launchpad"`,
  `kSurfaceLinkInterface` `"rp.launchpad.surface-link/1"`, `kMaxDeviceMessage` 560, `DeviceMessage` (a fixed 560-byte slot),
  `SurfaceLink` (`drainInput`, `pushOutput`, `connected`, `dropped`) and `NoSurface`, the stub a node runs on when the host
  answers its acquire with null. The nodes are `packages/native/src/host/graph/LaunchpadNodes.cpp` (135 lines),
  `rp.launchpad.surface-in` and `-out`, registered by `rp_launchpad_package`, one of the four entries of
  `packages/native/package.json`. Both declare `threads = true` and `unique = true`; neither is `device: true`. The
  surface-in node reports `connected` as a 0 / 1 control stream and discards what the link holds on its first block after
  it binds; it has no generation and no drop count of its own.
- **The legacy host and its UI-drain race.** `LaunchpadLink::disconnect()` (`LaunchpadLink.cpp`) runs on the UI thread and
  drains the input ring there: `inHead_.store(inTail_...)`, and it pops `outRing_` itself. The audio thread is the ring's
  consumer through `drainInput`, so the UI thread is a second consumer. The slots are POD, so there is no double free
  here, but it is a data race, the same shape the branch's `c60a3cf86` fixed (there the slots were `std::vector`, and it
  was a heap double free). The fix is to never rewind from the control thread: slots carry the claim's generation and the
  consumer drops an earlier claim's.
- **Where the SDL standalone wires it** (`packages/native/sdl/main.cpp`): a `LaunchpadHost` member (line 248) constructed
  with `rtMidiPortFactory`, a `LaunchpadSurfaceLink` over its link (260), `engine.installLink<rp::launchpad::SurfaceLink>`
  (1577), `launchpadHost.restore()` after `rp_restore_providers` (1625), `app.midi.setReservedInput(...)` (1626) and a
  `setOnLinkChanged` callback (1651), and `bindLaunchpadHooks(ctx, ...)` on the UI context (1312). The CLI
  (`cli/main.cpp`) and the plugin have no Launchpad; `packages/native/src/main.cpp` (the headless host) installs a
  `HarnessSurfaceLink` under the same name so a document with surface nodes plans and runs there. The DAW plugin's MIDI seam
  caps a message at 4 bytes, so no SysEx fits through it.
- **The shell's TS** (`packages/retroplug`): `src/controller/` (`index.ts`, `registry.ts` 82 lines with the zod app registry,
  `session.ts` 189, `trackerTarget.ts` 69, `apps/index.ts`, `apps/lsdjMidiMap.ts`), `src/launchpad/` (`decode`, `profile`,
  `protocol`, `surface`, `index`; about 585 lines), `src/graph/controller.ts` (119 lines: `controllerSyncOverride`, which
  hard-codes `controller.app !== "lsdj-midimap"`, `serialOutCaptureFor`, `controllerSettings`),
  `src/script/nodes/controllerSession.ts` (167 lines; the node, label 22, declared by the shell's own manifest,
  `packages/retroplug/package.json`, with `roles: {}`), `ui/screens/menu/launchpadDevices.ts` (235 lines over the 7 legacy
  hooks), `cli/sessions/launchpad-probe.ts`, the controller song refresh, start anchor and SYNC poll in `projectStore.ts`
  (`controllerSongSig`, `controllerSync`, `controllerAnchor`), and `settingsEnums.ts`'s `CONTROLLER_TARGET_VALUES`
  (`["system", "midiOut"]`), which `projectConfig.ts` imports. The node imports `@retroplug/lsdj/playback/predict`
  directly. `userConfig.ts` has no `controlSurface`; `USER_CONFIG_SCHEMA` is 3. `CONTROLLER_OUT` (256 messages, 4096 bytes)
  and `LAUNCHES` are in `packages/core/ts/ports.ts`.
- **Tests that exist and are the oracles.** `packages/native/test/engine-graph/Surface.test.cpp` (10 cases),
  `packages/native/test/launchpad` (`LaunchpadLink.test.cpp`, `LaunchpadSurfaceLink.test.cpp`: 29 cases, the target
  `retroplug-launchpad-test`), the parity baseline `test-native/parity/baseline/controller.json`,
  `test/script/nodes/controller-session.test.ts`, `test-native/lsdj-launchpad*.test.ts` (and `lsdj-launchpad-role`), the
  pure-TS suites `test/controller` (6 files) and `test/launchpad` (3), and `test/menu/launchpad.test.ts` (22 cases here; the
  branch's version has 47).
- **The provider ABI cannot carry a Launchpad.** `rp_device_host` (`packages/node-sdk/native/src/graph/rp_provider.h`,
  `RP_PROVIDER_ABI_VERSION` 1) has `config_dir`, `install` and `clear` and nothing else. A provider that holds a port pair
  has to keep it out of the product's shared `MidiIo` stream, and on Windows (where an input is exclusive) suspend that
  stream's hardware inputs for a scan; taking or releasing a port can need an audio stop and restart. Only the product
  can do that, so the host table needs a capability for it.
- **The Pico has no controller path.** `pico/host` has `UartMidi` (input only), the one root port is taken by the N8 (host
  CDC), and the controller's core measured about 120 KB of heap against the box's 64 KiB arena. There is also no Game Boy on
  the box for MI.MAP to drive.
- **The branch's portability proof** is `scripts/check-portable-core.mjs` (249 lines) and `tools-src/portable-scenario.ts`
  (267), `pnpm check:portable`: an allowlist bundle, an ES5 lowering, a scan for library calls MicroQuickJS lacks, a Node
  versus `mqjs` byte-identical transcript, in a heap of about 120 KB. This tree's `scripts/build-device-pack.mjs` already
  does the bundle, the lowering, the scan, the transcript and the heap check, for a pack's nodes; what it lacks is a way to
  run a scenario that is not a node.

## 2. The packages

| Package | Gets | Native |
| --- | --- | --- |
| `launchpad` | **TS** (a library and a protocol-node pack, with **no `script` member, no `app` member and no zod**): the controller session (`ControllerSession`, a zod-free runtime `registry`, `trackerTarget`'s contract, `playbackModel` use through `tracker`), the Launchpad protocol (`decode`, `profile`, `protocol`, `surface`), `PortScan`, the profiles, the `controllerApps` contract (section 4), `CONTROLLER_TARGET_VALUES` (`@retroplug/launchpad/enums`), `CONTROLLER_OUT`, the facet's TS client, and the `rp.controller.session` node (label 22, moved from the shell) with the branch's fixes (section 7). | `retroplug-launchpad` (`device: true`, entry `rp_launchpad_package`): `SurfaceLink` with link generations, `rp.launchpad.surface-in` / `-out` (`threads` off). `retroplug-launchpad-desktop` (`device: false`): `ControllerLink`, `MidiPortPool`, `PortClaim`, `RtMidiPort`, `MidiPort.hpp`, `rp_launchpad_provider`, `LaunchpadRpc` (the facet, 12 methods) |
| `lsdj-launchpad` | The bridge: the MI.MAP app (`lsdjMidiMap`), `trackerTarget` (the launches into LSDj's sync node, or a real Game Boy's Arduinoboy), the predictor wiring (`PredictedLsdjModel`), and the sync override, which is **manifest data** (`protocol.apps[].syncOverride`: the cart sync node type, the mode the app's launches need, the polled value that counts as the cart listening, and the mode "not listening" means), read by the shell's generic code. Its `app` member is the control plane's half (the zod config schema and the label), its `protocol.apps` entry names the zod-free runtime module; it has no nodes, so its `protocol.module` is absent | none |

**The dependency direction** is the reason for the bridge. The brainstorm's choice (C): neither `lsdj` nor `launchpad`
knows the other. `lsdj-launchpad` depends on `core`, `launchpad`, `lsdj` and `tracker` and contributes through
`controllerApps`; `launchpad` depends on `core`, `node-sdk` and `tracker` (and `testing` for its tests), with no zod; `lsdj` is unchanged. F1's list
([modular-graph-phase-f1-design.md](modular-graph-phase-f1-design.md) section 10) had `lsdj` contributing the app; that line
is superseded here, and `lsdj` stays free of the controller. A tracker with a different app (risa has none) would add its
own bridge.

**What stays in the shell:** the menu (`menuDefs.ts`'s controller rows, about lines 116 to 122, 307 to 347 and 388 to 510)
and `launchpadDevices.ts`, rewritten on a facet client (section 3) and persisting through `config.json`'s `controlSurface`
(section 5); `useLaunchpadScanWatch`; the `cli` probe session; `buildGraph`'s surface placement (the two surface nodes and
the session in the document, `graph/controller.ts`'s `controllerSettings` and the wiring); the store's controller song poll,
anchor and SYNC poll (`projectStore.ts`), which is cart orchestration through the stores and waits for F4.

**What goes:** `packages/native/src/host/launchpad/*` (the whole legacy host, including `LaunchpadHost`, `LaunchpadScanner`,
`LaunchpadHooks` and `bindLaunchpadHooks`), `launchpad.cfg` (not imported: a user re-scans once; section 5), the shell's
`src/launchpad` and every part of `src/controller` but its registry (`registry.ts` stays: it fills the launchpad
package's registry from the generated list and parses a saved app config with the app's own schema), the shell's
protocol member for `rp.controller.session`, and `check:portable`
(`scripts/check-portable-core.mjs`, `tools-src/portable-scenario.ts`, the `build-portable/` mqjs, the CI step), replaced by
section 6. `LaunchpadNodes.*` and `SurfaceLink.hpp` move into `retroplug-launchpad`.

**Products.** The SDL standalone, the headless host and the CLI start the provider; the plugin does not (section 11), and
refuses at configure to link the pack's desktop target, so it names no RtMidi and no system MIDI framework for it. Which
providers a product starts is its own `PROVIDERS` list in `retroplug_product` (section 3): listing a package gives a
product its node types and nothing else. The
SDL's provider drives real ports; the headless host's facet is a **fake** (section 8) so `test:native` and CI exercise the
facet-driven TS without a device; the CLI's provider uses the real ports for the `launchpad-probe` session and for scripted
sessions. A product that lists the package and does not name its provider still plans the surface nodes: they run on `NoSurface`.

**The product list** becomes `core sameboy mesen dsp mgb lsdj graph-runtime n8 smsggdj risa native bliptoaster` with
`launchpad` after `core` and `lsdj-launchpad` after `lsdj`: `core launchpad sameboy mesen dsp mgb lsdj lsdj-launchpad
graph-runtime n8 smsggdj risa native bliptoaster`. The native subsequence gains `launchpad` after `core`, so
the `[packages]` golden is an intended re-record (section 8). The shell declares both
packages in `packages/retroplug/package.json`'s `dependencies` (spec/06 "Adding a package", item 3), and the protocol node
labels stay 16 to 23 with the session at 22: its manifest entry moves from the shell to `launchpad`.

## 3. Startup, threads and the provider interface

**The provider.** `launchpad`'s desktop target is a manifest native entry with `provider: "rp_launchpad_provider"` and
`rpc: { header, register }`, as `retroplug-n8-desktop` is (`packages/n8/package.json`). `retroplug_product` generates its
start, restore, stop and wake calls (`rp_start_providers`, `rp_restore_providers`, `rp_stop_providers`,
`rp_wake_providers`) and mounts the facet whole in every product that starts it. **Which providers a product starts is
its `PROVIDERS <package>...` list**, which has no default: a package in `PACKAGES` and not in `PROVIDERS` contributes its
node types and neither its provider entry nor its desktop target (a manifest item that carries both an `entry` and a
`provider` is refused at configure, because the two would be dropped together). The SDL, the headless host and the CLI
name `launchpad`; the plugin never does and its `plugin.cmake` refuses a plugin target that links the desktop half. The provider creates the `ControllerLink`, the `MidiPortPool` and the `PortClaim`, and
installs the link under `"launchpad"` / `rp.launchpad.surface-link/2` through `rp_device_host::install`, as the N8 provider
installs its cart link. It owns no console, no file and no profile: TS owns every decision (section 5), native is a
protocol-free transport.

**The ABI bump.** `RP_PROVIDER_ABI_VERSION` is **2**, and a host and a provider refuse each other across a version in both
directions (there is no optional read of the new members). `rp_device_host` gains three calls and `rp_provider` one:

- `reserve_midi(ctx, in_name, out_name)`: keep this pair out of the product's shared MIDI stream, input and output, and
  undo it with empty names. The product reopens only what actually moves and does the audio stop and restart if one is
  needed.
- `suspend_hardware_inputs(ctx, bool)`: the held edge a scan raises while loose inputs are open. The call is made on every
  platform, but only Windows acts on it (an input there is exclusive); elsewhere it is a true no-op, so a scan costs no
  audio restart.
- `restored(ctx)`: the control plane's half of the startup rendezvous is done (below).
- on `rp_provider`, `wake(self)`: the per-frame pump from the product's frame loop that wakes the writer thread, so the
  frame the session painted is the frame the device hears.

The SDL implements them over `MidiIo` (`setReserved`, `setHardwareInputsSuspended`) with the audio stop and restart, one
restart for a swap; the headless host records the calls (a test reads the record through the engine facet's harness half)
and does nothing else; the CLI passes no-ops, because it owns no shared stream to reserve from, has no inputs to give up
and ends the rendezvous at once, and it has no reader for a record; the N8 provider ignores the table's new members.

**The startup rendezvous.** Today the SDL restores the link after `rp_restore_providers` and hands `MidiIo` the reserved
input, with audio running. F3 inverts it so a claimed pair is never opened by the shared stream first:

1. the product starts the providers (`rp_start_providers`), and **holds** MIDI and audio (does not start either);
2. TS reads `config.json` (`controlSurface`: `input`, `output`, `device`, `enabled`), claims the pair through the facet, and
   calls `restored()` on it;
3. the provider's claim reserves the pair through `reserve_midi`; the product then **starts** MIDI and audio.

The product pumps its JS loop while it holds (the provider holds no precomputed state: the facet's `restored()` calls the
product's `restored` and the product starts on it), and starts anyway after **500 ms** of wall time (the branch's bound) so a missing or
broken `config.json`, or a hung port, never keeps the app silent. A claim that arrives after that is a **mid-session
reserve**: the product stops audio, reopens the stream without the pair, restarts audio. `restored()` is a call on the facet
(a no-op to a product that holds nothing).

**Fixed slots and generations.** `ControllerLink`'s device-to-audio ring is fixed `DeviceMessage[kMaxIn]` slots (the
node-facing `DeviceMessage` is 560 bytes; the branch took 1024-byte messages from the device, so an inbound message wider
than the slot is dropped and counted in the link's one `dropped_` (as an oversized message, a full receive ring and a
paint sent with nothing attached all are), single consumer: the audio thread. Each slot carries the claim it was written under; whoever consumes a slot drops it if it belongs to an earlier
claim. `disconnect` and `connect` bump the generation and rewind nothing, which removes the UI-drain race of section 1. A claim
whose input and output carry one name (an interface's two ports) is **one** cross-process claim, not two locks that
refuse each other. The facet's claim runs in the branch's order: the old pair is disconnected (its close bytes out) and
rejoins the shared stream; an empty port means none chosen; the pair's **presence** is checked against the lister before
anything is reserved; the cross-process claims are taken; the pair is reserved; the ports open. A refusal before the
reservation hands nothing back to hold, one at the claims keeps the reservation (the other instance's pads must not reach
this stream). The
audio-to-device ring stays a POD `SpscRing` (the packages already ship one under `node-sdk`'s `transport/SpscRing.hpp`),
`kMaxOut` 560, drained by the link's **writer thread** (WinMM blocks ~170 ms on a bulk SysEx, so it never runs on the audio
thread or the UI frame). The UI thread's `send()` goes through a small queue of its own (16 messages, counted when full)
that the writer drains, so a scan's sends never wait for the write in flight. **Close bytes**, the opaque blob TS gave `connect`, are replayed on a disconnect and on a destruct,
so a Launchpad in Programmer mode is let go even when the JS runtimes are gone.

**The nodes.** `rp.launchpad.surface-in` and `-out` lose `threads = true` (the writer thread and the RtMidi callback are the
provider's, not the node's) and become `device: true`, so they plan on the Pico and the `device-mode` test builds them
(`-fno-exceptions`, `-fno-rtti`; their settings are empty). `SurfaceLink` gains `generation()`. The `connected` stream carries
the generation, not a flag: 0 when nothing is attached, otherwise the claim's number, so a pair swapped inside one block
still reads as a new device. The number wraps within `1..2^24-1` (a wrap reads as a claim and never aliases 0, "nothing
attached"). Surface-in also gains **drops**, a second control
stream with the same wrap: the link's loss count, so the session can repaint. `buildGraph` gained one wire for it.
`NoSurface` answers generation 0.

**The facet.** `LaunchpadRpc`, mounted whole in every product that starts the provider (the user's decision for N8's facet,
carried over). It is TS's only door to ports: `ports()` (the loose enumeration, the input and output names), `status` (the
claim's state: connected, generation, sent, dropped, the pair's names and the last error), `claim`, `release` (with the
close bytes), `send` (a message to the claimed output), `restored` (the rendezvous's end), and the `MidiPortPool` calls a
scan needs (`openInputs`, `openOutput`, `portSend`, `portRead`, `portClose` and `portError`): twelve synchronous methods in
all. Scans run in TS (`PortScan`) as a state machine stepped from the UI frame poll
with a clock it is told, over these calls, so no native thread scans and no wall-clock stall can wedge it. The facet is
synchronous, like the N8's facet in the CLI; each call is a few vector copies, and the pool's batch open is the one
expensive call (section 10 question 4).

**Threads.** Audio thread: surface nodes only, no allocation, nothing blocked. The RtMidi callback thread writes the
device-to-audio ring. The writer thread reads the audio-to-device ring. The UI/control thread calls the facet. The Pico runs
no host code of this package, only the nodes.

## 4. Controller apps: the `controllerApps` contribution kind

An app is the thing a session runs (MI.MAP is the only one). It has two halves because the two runtimes differ: the
control plane (the UI, the project, the store) has zod and the config's schema, and the script node's bundle has no zod
(`test/script/bundle-no-zod.test.ts` holds it). Today `controllerSession.ts` imports `controller/apps/index.ts`, which is
the shell's, and `src/controller/registry.ts` holds the zod side; the node never imports the registry, so the app's config
arrives **already parsed** in the node's `appConfig` setting.

**The control-plane half** is a member of the contributing package's `app` module (the bridge's; `launchpad` itself has no
`app` member): `definePackage({ controllerApps: [...] })`, each entry `{ id, label, config: <zod schema> }`: `id` is the
stable string stored in the project (`lsdj-midimap`), `label` the menu's text and `config` the app's own knobs' schema
(the project's `.rplg` still stores them). The menu's rows are built from the app's schema, the cart-sync label from the
app's own answer, and the project's default app id is the registry's first app, so the shell names no app.

**The sync override is manifest data,** not a function and not a fourth generated module: the runtime entry
`protocol.apps[].syncOverride` is `{ node, mode, listening, off }`, rendered into `generated/protocol.ts` as
`CONTROLLER_SYNC` (the generator validates it: `node` and `mode` required, `listening` and `off` together, an unknown key
refused, the node type one a package declares). `graph/controller.ts`'s `controllerSyncOverride` reads that table by the
stored app id, resolves the system with the same `controllerTarget` the launches are tagged with, and `buildGraph` applies
it to every cart sync node of that type (to the document, never to the project), holding no node type and no app id.

**The runtime half** is a member of the package's `protocol` member: `protocol: { module?, nodes?, apps }`, where `apps`
lists `{ id, module, syncOverride? }` and `module` names a zod-free module that registers the app's runtime (its factory,
its playback model, its launch target). `protocol.module` is **optional** for a package that declares no nodes (the
bridge); a module named with no nodes to export is refused. The generated script entry registers the runtimes **before**
the session node and imports only these modules, so the node's bundle stays zod-free and the node's TS never imports an
app by name. The two halves join by `id`.

**The generator** (`packages/retroplug/scripts/lib/packages.mjs`, spec/06's "Adding a package" item 7: a new contribution
kind is not data) gains `controllerApps` in `renderPackages`, a `declare module "@retroplug/core/definePackage"`
augmentation in `launchpad` (the owner, as `tracker` and `dsp` own theirs), the type-only import in `lsdj-launchpad`'s app
module, and `expected/packages.ts` in the fixtures. It refuses a **duplicate id** (control plane or runtime, with both
packages named) and a colliding generated identifier; the generated module refuses an `app` entry with no runtime half and
a runtime half with no control-plane half **at load**, with a thrown error naming the id. The `scriptNodes.ts` cross-check
at load ("the registered set is exactly the manifests' declarations") is extended to the apps, import by import.

**Dependency direction** as section 2: `lsdj-launchpad` imports `launchpad` (the contract) and `lsdj`
(`@retroplug/lsdj/playback/predict`, the leaf, not the barrel that pulls the WRAM reader and the sav codec into the bundle;
`bundle-no-zod` pins both and asserts no `lsdj/ts/sav` module arrives; the bridge exports no bare `"."`, so the bare
specifier cannot load zod);
`launchpad` imports neither the bridge nor `lsdj`. The shell's registry reads the generated list. The migration of a
saved project is nil: the app id and the config are the same strings and the same schema.

## 5. Persistence

TS owns the selection (the brainstorm's choice A; the branch's section 8.6). `config.json` (`userConfig.ts`, `USER_CONFIG_SCHEMA`
3) gains an additive `controlSurface: { input, output, device, enabled }`: additive, so a zod default fills it and no
migration step is needed (the rule in AGENTS.md; the branch's schema has it as `controlSurfaceSchema`, `.preprocess`ed, with
`// additive → no migration`). The write **merges**: the store re-reads the file, applies only its own change and writes,
because several processes can share `config.json` (two standalones; and a write by a newer build is refused read-only by the
existing versioning). A failed write is not "saved" (the branch's `1c1c89f1b`, ported to this tree's
`userConfigStore.ts`, a change in the shell and in its tests). The schema version stays 3.

`launchpad.cfg` is **not imported**: it is the legacy host's file, under native, with the pair's port names; a user with it
re-scans once (the menu says so when nothing is selected and a Launchpad is present), and the file is left where it is. Nothing
else reads it. Persistence lives in `launchpadDevices.ts` (rewritten against the facet): the port-stem re-match (a backend
that renames a port on reconnect is matched by its stem), the close bytes (`exitToLiveMode`), the scan's confirmation and the
**forget** action, as the branch's file (431 lines) owns them. When the project stops running the controller the UI sends the
claim's close bytes through the facet a couple of polls later, because the node cannot say goodbye itself.

## 6. Portability

The branch's `check:portable` is retired and replaced by the E4 toolchain, which does steps it did by hand (bundle, ES5,
the library scan, a transcript on Node and on `retroplug-mqjs-run`, the heap check, bytecode at 64 and 32 bits). `launchpad`
has **no `script` member** in F3: nothing is built into a pack, because no box image carries the controller, so a
`heapBytes` would be a claim nobody holds it to. The scenario mode below is the portability proof, and the node keeps to
the pack rules ([modular-graph-phase-e4-design.md](modular-graph-phase-e4-design.md)) so that a `script` member is a
manifest line when a target exists.

What `build-device-pack.mjs` cannot do is run a scenario that is not a node: the controller's session, `PortScan` and the
Launchpad protocol are libraries a node uses, and a missing library call only fails on the path that runs it. So the script
gains a **scenario mode**: `--scenario <file.ts>` bundles the scenario instead of a node entry, under the same allowlist
(it **may import a package's declared dependencies**, which the node mode does not, because a scenario is how a bridge
package proves its half: `lsdj-launchpad` imports `launchpad` and `lsdj`), lowers it, scans the scenario and the **whole portable core of the package** (every module under its `ts/`, bar the one the manifest
names as its `app`, which is the control plane's and may use what the engine lacks) for library calls the engine lacks, runs it under Node and under the runner, requires byte-identical transcripts, and checks
the heap against `--heap-bytes`. It writes no pack. There are two scenarios, both run by `pnpm test` (not by the C++ suites), and each has refusal cases (a package whose portable core calls a
missing builtin, or whose app module is imported by a core module, fails naming the line):

- **the launchpad core:** every LED form the protocol writes, the decoder's message kinds, each quantise mode, a session
  update across a relocate and a generation change, and `PortScan` through its follow-up, cancel, failure and quiet paths;
- **`lsdj-launchpad`:** the MI.MAP app and the predictor: an edit, an anchor, a song change, the launches the target sends
  to both `launches` and `midiOut`.

**What it proves:** the code runs on the engine the box has and says the same bytes as on Node, in a heap measured here:
the launchpad core passes at 61440 bytes and fails at 61400 (budget 76800), and the bridge with MI.MAP passes at 144960
and fails at 144896 (budget 184320), on the runner's 64-bit context. The branch's "about 120 KB" was a source-compiling
measurement. The budget is the scenario's own, not the box's 64 KiB arena, and the number is recorded, not claimed to fit. **What it does not:** that the node fits a 64 KiB arena
beside the N8's pack and the bliptoaster node (there is no Pico controller path, section 11); that a real device answers the
way the fake does; or the facet's behaviour (a native seam, section 8).

## 7. What changes for a user, and what must not

What changes (each is a fix on the branch; each gets its own test):

- **Relocate.** A tick jump larger than the previous block's projected end (plus the walk's one tick of slack) is a seek,
  not elapsed time: the node counts the ticks the host **clocked** in each block with the same walk the cart's protocol
  node runs (`TickClock` / `walkTicks`, whose forward resync is the rule) and hands the session the previous block's count,
  so the predictor advances by exactly what the cart was sent. This is better than the branch's, which inferred a relocate
  from the tick difference and dropped the block at a loop wrap, a seek made while stopped and a device taken again.
- **Generation re-anchor.** The session re-takes the device on a new generation, so a pair swapped faster than a block still
  gets the hello bytes and a full repaint (and keeps the block it lands in); today only the 0-to-1 edge of `connected` does.
- **Drop repaint.** The session repaints when the surface-in node reports more dropped messages than it saw
  (`controllerDrops`, wired by `buildGraph`).
- **Model confirmation.** The scan asks the Mini MK3 / Pro MK3 family question on the pair that answered, so a Mini is not
  taken for a Pro MK3; an interface that echoes SysEx back confirms nothing.
- **One claim per machine.** Two processes restoring one `config.json` no longer both take one Launchpad; a refused claim
  still keeps the pair out of that process's shared stream.
- **Scan in TS.** The scan is `PortScan`, stepped from the UI poll, cancellable, quiet-gapped (`GAP_MS` 50), with no native
  thread.
- **Idle cost.** The surface skips its diff and the app skips painting while nothing changed: an idle block costs the audio
  thread a measured ~1.6 us on the branch (QuickJS, 128 frames).
- **The reconnect race** is gone (section 3); the upgrade needs **one re-scan** (section 5).

What must not change: the audio (the parity baseline, including the controller group), every kit and render byte, the
`buildGraph` goldens, the project file (`.rplg` stores the same app id and config), the LED bytes the protocol writes (the
branch's protocol and the shell's agree; the tests pin them), and the MI.MAP behaviour of an emulated cart and of a real
Game Boy. A pair on a plain build is never claimed unless the user selected it.

## 8. Proof

F3 completes on the **headless proof**; the real-device checklist below is **owed**, run by the user, and findings become
follow-ups (the brainstorm's decision).

**Intended re-records**, each in its own commit with the reason in the message, and nothing else moves:

- the `[packages]` golden (two packages in the list; `launchpad` in the native subsequence);
- the role goldens' **drops wire** (`golden/build-graph-roles.json`: surface-in's drops into the session, and nothing else
  in that file);
- the surface descriptor goldens, as **new files** (the nodes' `connected` generation, the drops output, `device: true` and
  no `threads`);
- the device-check image hash (`device:check` builds `retroplug-launchpad` into the image's node set);
- **one `ProtocolNodes.test.cpp` digest**, the controller-out digest, which moved with the **tick-count fix** (the session
  now paints the block a loop wrap or a swap used to lose), not with the surface nodes: measured, the stage tree with the old
  literal fails and the pack's nodes change no byte of it. That is a departure from "extended, not re-recorded" below.

**What must hold, unmodified:** strict parity (`pnpm test:native parity`, with the controller group), the kit and render
baselines, the `buildGraph` and role goldens, `ProtocolNodes.test.cpp`, the `bliptoaster` pack's bytecode, the box image
hash (`device:box`), and these tests **extended, not re-recorded**: `Surface.test.cpp`, `controller-session.test.ts`
(one anchor case's stimulus was changed, deliberately), the `lsdj-launchpad*` native tests. The new `controller` parity cases, if any, are fixture-only and added only if a case
would otherwise be unpinned.

**New proof:**

- the transport's C++ cases, ported from the branch's `Controller.test.cpp` (34 cases): claim, generation, the writer thread,
  close bytes, `PortClaim`, the pool's held edge, a swap mid-drain with no race (a TSan run in `build-tsan`);
- the `PortScan` cases (follow-up, cancel, failure, quiet, an echo that confirms nothing) and the profiles;
- the two scenarios of section 6 in `pnpm test`;
- `controllerApps` generator fixtures: a refused duplicate id and an unpaired half each, an `expected/packages.ts`;
- the facet's tests through the **headless fake** behind `packages/native/src/main.cpp` (the pack's real facet over a scripted
  `MidiPortOpener` that answers like a device: ports, a claim, a scan reply, a swap): these run in `test:native` and CI, so the TS that drives the facet is
  exercised without hardware;
- `sdl:controller`, 5 legs on Linux through `tools/fixtures/fake-launchpad.c` (an ALSA sequencer fake Pro MK3, client name
  `RPFakeLPProMK3`) and `tools/run-sdl-controller.sh` (not in CI; ported from the branch). It runs only with
  `RP_MIDI_PORT_ALLOW` set to the two fakes' **exact full port names** (built from the fakes' ready lines; a caller who
  presets it is refused), after a listing-only pass that fails the run if anything else is listed or anything named like a
  Launchpad is not a fake. Its five legs: nothing selected, restore with the
  pair kept out of the shared MIDI stream, the lifecycle order (Programmer mode first, the LEDs, the release last), a real
  scan claimed mid-session beside a second device, and a second process refused the claim;
- the menu test, 47 cases ported from the branch's `test/menu/launchpad.test.ts` onto the facet (this tree had 22);
- the session node: a swap in one block re-anchors (the hello bytes and a full repaint), a drop repaints, a loop wrap and a
  device taken again keep the block (red against the code before the tick-count fix);
- **hardware safety seams.** A test or a harness must not touch a machine's real MIDI ports: `RP_MIDI_PORT_ALLOW` (a
  comma-separated list of whole port names) is honoured by the lister, the opener, the pool, the link and `MidiIo` alike
  (and `MidiIo` checks the name on the opening object, since a hot-plug shifts indices), `RETROPLUG_LAUNCHPAD_CLAIM_DIR`
  names the headless host's claim directory (the host removes only a directory it created), and
  `RETROPLUG_SDL_TEST_SCAN` drives the SDL's scan from a harness. `sdl:smoke` and `sdl:pipewire` run under a scratch config
  dir with an allow list that matches nothing.

**The owed checklist** (about 10 minutes; the Launchpad Pro MK3 and the SDL standalone on a real desk, LSDj in a ROM):

1. Build and start `retroplug-sdl` with nothing connected; the audio starts without a pause (the 500 ms bound is not hit).
2. Plug the Pro MK3 in; open the Launchpad submenu; the scan finds exactly one device and confirms the model.
3. Enable the controller; the grid enters Programmer mode and paints; a pad press launches a row in LSDj.
4. Quit; the pad's own Settings menu is unlocked (the close bytes were sent).
5. Restart: the pair is claimed before audio starts, with no MIDI input from the Launchpad in the shared stream (play a pad:
   no note reaches a monitored synth).
6. Unplug and replug mid-session; the surface re-takes and repaints; no glitch or crash on the audio.
7. Start a second `retroplug-sdl` with the same `config.json`: the second refuses the claim and says so; the first keeps its
   pair.
8. Seek the host transport far and watch the playhead: it jumps, it does not run.
9. A cart in LSDJ master or MI.OUT mode with the controller on: the controller sends it nothing.
10. Upgrade check: a `launchpad.cfg` and no `controlSurface`: the menu offers a scan; nothing crashes.

**Windows items** (owed; built and reasoned about, never run): exclusivity of an input (the held edge, which acts on Windows
only, suspends the shared stream's inputs for a scan), the unshared `CreateFile` claim (a second process is refused), the
writer thread under WinMM (a bulk repaint does not freeze the UI frame; `send()` goes through the UI queue, but
`ControllerLink::disconnect()` still runs on the UI thread, joins a writer that may be mid-drain and writes the close bytes
itself, so a swap or release with a full repaint queued could block it), and `/W4 /WX` on the new targets.

## 9. How it is built

A prototype first, as F1: branch `proto/f3-launchpad`, five stages (five commits, each reviewed and amended until its
review passed):

1. the TS core: the `launchpad` package, the protocol, `PortScan`, the profiles, the session and its fixes, the portable
   core proved by the scenario mode and the launchpad scenario (`config.json`'s `controlSurface` went to stage 4);
2. `controllerApps` and the bridge: the contribution kind (generator, augmentation, fixture, the zod-free route), the
   `lsdj-launchpad` package, the second scenario;
3. native: `retroplug-launchpad`, `retroplug-launchpad-desktop`, the link on fixed slots with generations, the writer thread,
   `PortClaim`, the pool, the provider and the facet, the ABI bump, the transport's tests;
4. products and the shell: the provider in the SDL, the headless fake, the CLI, the facet client in `launchpadDevices.ts`,
   the menu on it, the shell's deletions, `sdl:controller` (with its port filter);
5. docs: `AGENTS.md`'s package rules, `spec/06-build-test.md`, `spec/07-remaining-work.md`, this document's as-built.

Routing is as F1 (the plan's implementers, the acceptance run and the follow-ups on the models the user set for it; Opus for
the stage reviews and the final review; the controller coordinates and reviews the risky code). Work directories (gitignored,
under `.superpowers/`): `f3pro`, `f3plan`, `f3exec`. The prototype is thrown away; the plan is written against what it found;
execution starts from `feature/modularize`.

## 10. Prototype questions

Each was answered by the prototype; the section keeps the question and the answer.

1. **The session node's relocate and generation logic.** The threshold is the **previous block's projected end**,
   `nextTick = ppq*24 + frames/sampleRate*(tempo/60)*24`, and a relocate is `ppq*24 > nextTick + 1`: the walk's own forward
   resync (`nextTick < ppqRes - 1`), with its one tick of slack. The first answer ("as `walkTicks` does", measuring the
   previous block against the current one's length) drifted on ragged blocks, and the review caught it. The fix pass then
   went further: the node counts the ticks the host clocked with the cart's own walk and hands the session the previous
   block's count (section 7), which removed `relocate`, the `lastTick` diff and the `started` gate.
2. **How surface-in reports the generation.** A control stream carrying the claim's number, wrapped within `1..2^24-1` so
   that it never aliases 0; `drops` is a second control stream with the plain 2^24 wrap. The descriptor change is
   `connected`'s doc, `drops` and no `threads`; `buildGraph` gained one wire. A reader that compares sees no wrap.
3. **`restored()` before the product's RPC loop runs.** The product pumps its loop while it holds and starts on
   `rp_device_host::restored` or at 500 ms of wall time (a pump count would expire in a millisecond); the facet calls
   the product's `restored`, so the product names no class of the pack. Nothing is precomputed.
4. **The facet's synchronous RPC from the UI frame poll.** Measured in passing, not as a case: `sdl:controller`'s scan leg
   runs the whole scan (two outputs, an inquiry each, a model query on the pair that answered) from the frame poll in
   about 5 s of wall clock at that host's frame rate, with every call a few vector copies. No batch call beyond the pool's
   one held-edge open was needed.
5. **The headless fake's shape.** The pack's real facet over a scripted `MidiPortOpener` (`FakePorts.hpp`), not a
   facet-level fake: the claim logic, the pool and the link are the product's code. It lives in
   `packages/launchpad/native/test`, linked into `retroplug-host` alone, like `FakeN8.hpp`. The host keeps its harness
   surface link (parity stages pad presses into it), so the provider's install gets a seat and no engine link there.
6. **Whether parity's controller group moves.** It did not: 30 files, 250 cases, identical at every stage.
7. **`MidiIo`'s reserve on SDL.** The full stop, apply, start, and only when a port the stream holds moves; no Windows
   special case in the code path: the same calls are made everywhere and the held edge acts on Windows only.
8. **The scenario's heap budget.** Separate from any pack's, as the scenario writes no pack: the launchpad core 61440
   bytes least, budget 76800; the bridge 144960 least, budget 184320 (section 6). No `script` member claims a `heapBytes`.
9. **`CONTROLLER_TARGET_VALUES` and `projectConfig.ts`.** The shell imports it from `@retroplug/launchpad/enums`; the one
   literal left was the default app id, now the registry's first app.
10. **`rtmidi` ownership.** The pack's desktop target links `rtmidi` directly, defined at root scope and built with PIC; the
    `RETROPLUG_WERROR` and Windows rules are unchanged (the dep keeps its `-w`, our sources `/W4 /WX`), and the plugin
    proves the selection by not linking it.
11. **Anything found while verifying a survey fact.** The tree still had the legacy native host, which the pack replaced
    in stage 4 (the SDL's own install was refused by the provider's until `PROVIDERS` existed); `docs/launchpad-plan.md`
    got a path-only link update so `check:docs` passes.

## 11. Left beyond F3

- **The plugin Launchpad.** The DAW plugin has no control surface (the host's MIDI seam caps a message at 4 bytes, so no
  SysEx fits), and whether it should is its own question.
- **The Pico controller.** A USB-MIDI host on the box (the one root port is the N8's) or a DIN output (a UART TX node;
  `UartMidi` is input only), a heap that fits 64 KiB (the core measured about 120 KB), and a target that matches the box
  (the N8 FIFO, not a Game Boy). The surface nodes are `device: true` so the box can plan them later.
- **Third-party apps.** `controllerApps` registers from the product's package list; loading one from outside the tree is the
  downloadable-pack road ([modular-graph-design.md](modular-graph-design.md) section 11).
- **Multiple links.** One controller link per process and per `rp.host.devices/2` name; more than one is a registry change.
- **The store's song poll** (the controller song refresh, the anchor and the SYNC poll in `projectStore.ts`) stays in the shell
  until F4.
- **F2** (the native carve) and **F4** (the UI onto extensions) are unchanged by this phase; F4 takes the menu rows and the
  poll.

## 12. As built

The build, on `feature/modularize` (it was designed and prototyped on `proto/f3-launchpad` over the design commit
`fcdb47361`), is five commits, one per stage, each reviewed (by Opus) and amended until its review passed; the last fix
pass folded every outstanding fix into the commit it belongs to, and each of the five trees was checked green on the
`graph` suite and the `[protocol-nodes]` cases on its own. The sections above were corrected in place to what these
found; this section records what was built, where it departs from the design as first committed, and what it verified.

**The stages**, by commit subject:

1. `feat(launchpad): the package's TS core, its session node and a scenario`: `packages/launchpad` (protocol, `PortScan`,
   profiles, session, the zod-free runtime registry, the `rp.controller.session` node moved from the shell), the branch's
   fixes expressed in the graph's terms, and the scenario mode of `build-device-pack.mjs` with the launchpad scenario.
2. `feat(packages): the controllerApps kind, and the LSDj bridge that contributes MI.MAP`: the contribution kind
   (generator, augmentation, fixtures, the zod-free route), `packages/lsdj-launchpad`, the second scenario, the sync
   override as manifest data and the clocked-tick fix.
3. `feat(launchpad): the pack's native half, its provider and its facet`: `retroplug-launchpad` and
   `retroplug-launchpad-desktop`, the link on fixed slots with generations, the writer thread, `PortClaim`, the pool, the
   provider and the 12-method facet, ABI 2, `PROVIDERS`, the transport's tests.
4. `feat(launchpad): the products on the pack's provider, and the shell on its facet`: the provider in the SDL, the headless
   fake and the CLI, the facet client in `launchpadDevices.ts`, the menu on it, `controlSurface` in `config.json`, the
   shell's deletions (the legacy host, `src/launchpad`, `src/controller`'s app modules and session, `check:portable`),
   `sdl:controller` and the
   port filter.
5. `docs: the Launchpad pack in the living docs`: AGENTS.md, spec/02, spec/06, spec/07, package-split.

**Departures from the design as committed, and why.**

- **No `script` member for `launchpad`.** The scenario mode is the portability proof, and no box image carries the
  controller, so nothing would build or hold a pack's heap. `launchpad` has no `app` member and no zod either: the app's
  control-plane half is the bridge's.
- **The scenario mode may import declared dependencies and scans the whole core.** The first version scanned only the
  scenario's own graph (a `Number.isInteger` was left in the core) and could not run a bridge's scenario. The measured
  heaps are in section 6; the first stage recorded 262144 as a budget, which the fix passes replaced with measured ones.
- **The sync override is manifest data in `protocol.ts`**, not a function in a fourth generated module (the first stage 2
  had a `controllerSync.ts`; the review found the override declared twice). `protocol.module` became optional for a
  package that declares only apps. The menu rows, the cart-sync label and the default app id come from the registry, so
  the shell names no app.
- **The relocate rule is against the previous block's projected end, and the session counts the host's clocked ticks**
  with the cart's own walk. The first answer measured the previous block against the current one's length and drifted on
  ragged blocks; the branch's own relocate dropped the block at a loop wrap, a seek while stopped and a device taken
  again, which the clocked-tick count fixes (red first, two cases).
- **`PROVIDERS` per product, with no default.** Stage 3 first started every listed provider in every product, which made
  the provider's surface link refuse the SDL's own install (the SDL's Launchpad was dead) and made the plugin start a
  control surface it cannot use. Now the SDL, the headless host and the CLI name `launchpad`, the plugin never does and
  refuses its desktop half at configure, and a manifest item with both an `entry` and a `provider` is refused with a
  message.
- **ABI 2 carries more than the design said:** `reserve_midi`, `suspend_hardware_inputs`, `restored` and the provider's
  `wake`, with strict equality across a version both ways. `suspend_hardware_inputs` acts on Windows only.
- **A pair whose input and output share a name is one claim** (two locks refused each other, so the ordinary Launchpad on
  ALSA, CoreMIDI and WinMM could never be claimed), and the claim runs in the branch's order with its presence check.
- **`send()` goes through a UI queue the writer drains** (16 messages), so the UI thread never waits for the write in flight.
- **The generation wraps within `1..2^24-1`** so it never reads as "nothing attached".
- **The headless fake is the real facet over a scripted opener,** and the host keeps its harness surface link, because the
  parity baseline stages pad presses into a link.
- **`ProtocolNodes.test.cpp`'s controller-out digest moved,** which the design said would not: the cause is the tick-count
  fix, not the surface nodes (the pack's nodes change no byte of it), and it was re-recorded once, in the commit that
  made the fix.
- **The re-records as they landed:** the `[packages]` golden, the role goldens' drops wire, the surface descriptor goldens
  as new files, the device-check image hash (`8cc7ae7d` to `38665e2c`, the surface nodes joining its node set), and that
  one digest. The box images and the `bliptoaster` pack's bytes were not touched.
- **The test seams are larger than the design said:** `RP_MIDI_PORT_ALLOW` (a comma-separated list of SUBSTRINGS of a
  port's name; the harness writes its fakes' whole names, which no other device's name can contain),
  `RETROPLUG_LAUNCHPAD_CLAIM_DIR`, `RETROPLUG_SDL_TEST_SCAN`, a fake named `RPFakeLPProMK3`, and `sdl:smoke` and
  `sdl:pipewire` under a scratch config dir with an allow list that matches nothing.

**The hardware-safety episode.** The first `sdl:controller` harness run (stage 4, and the tip-verify leg that ran it)
listed and opened every system MIDI port, and its scan sent a Device Inquiry to every output, the Zoom L6 and a CH345
included, before any port filter existed. The fix pass added the
filter (honoured by the lister, the opener, the pool, the link and `MidiIo`), made the harness refuse to run without it,
and added a listing-only proof run before anything is opened. A review then found the first allow list (a substring of the
Pro MK3's interface name) would also have admitted a real Pro MK3; the fake was renamed so that no real device has its
client name, the list is the two fakes' exact full names built at run time, a preset list is refused, and the run fails
if a listed port that is not a fake contains `LPProMK3` or `Launchpad`. A table-driven case holds the shell's copy and
the pack's copy of the filter rule to each other. The harness leg was disabled until the filter existed and was turned back on
with it.

**Verification.** A baseline run at the design commit (40 legs, every one exit 0, the same tip-verify as E4's) was compared
with a run at the final commit (42 legs, the baseline's plus `tsanlp` for the pack's transport under ThreadSanitizer and
`sdlctl` for `sdl:controller`): every leg exit 0. The legs:

- the device images, hashed: box PIO `4b4f9abc` and native `af36f223`, identical to the baseline; the device check moved
  from `8cc7ae7d` to `38665e2c` as intended; the `bliptoaster` pack's bytes (three hashes) identical;
- strict parity unchanged, the controller group included (30 files, 250 cases, no re-record), and the kit and render
  baselines, `buildGraph`'s goldens and the CLI suites unchanged;
- `pnpm test`: 190 files and 1991 cases, zero skips (baseline 183 and 1906); `pnpm test:ui` 42 files and 46 cases;
  `pnpm test:plugin` 11 binaries and 932 cases, the per-binary sum (the engine-graph binary's 338 among them, the pack's
  `retroplug-launchpad-test` 52 and `retroplug-midi-test` 23 at that run; 902 at the baseline); `test:native` 172 files and
  629 cases with resources, and the CI picture 144 skips in the same
  65 files; the sanitizer legs (thread and address) over the engine graph, the graph library, the N8 pack, the device mode,
  the pack's transport and the kit; `device:check`, `device:box`, `sdl:smoke`, `sdl:controller` (five legs), `check:docs`
  and `check:boundaries` (no exception, no warning).

Windows, macOS and arm64 builds of F3 were never run, and CI was not triggered.

**Owed.**

- the real-device checklist of section 8 (the Pro MK3 and LSDj on a desk), run by the user, with its findings as
  follow-ups;
- the Windows items of section 8: the unshared `CreateFile` claim, the input exclusivity and the held edge (which acts on
  Windows only), the writer under WinMM, `ControllerLink::disconnect()` on the UI thread (it joins a writer that may be
  mid-drain and writes the close bytes itself; measure it with a full repaint queued and, if too long, hand the farewell
  to the writer, which needs an asynchronous claim path through the facet), and `/W4 /WX` on the new targets;
- the first macOS and arm64 builds, and the first CI run.

**Left for later.** The plugin's control surface (the host's MIDI seam caps a message at 4 bytes, so no SysEx fits); the
Pico controller (a USB-MIDI host or a UART TX node, and a target that matches the box; the measured heaps above are far
over its 64 KiB arena); third-party apps (`controllerApps` registers from the product's package list, so an app from
outside the tree is the downloadable-pack road); one controller link per process and per `rp.host.devices/2` name; and the
store's song poll, anchor and SYNC poll in `projectStore.ts`, which stay in the shell until F4.
