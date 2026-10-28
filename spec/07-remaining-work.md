# 07 — Remaining work

The port is complete: the legacy build is gone, the transitional target suffix is dropped, and the
plugin identity is the canonical `RetroPlug`. What is left is **feature work, not migration** — a
short list of gaps, deferred features, one pending refactor, a code-comment cleanup backlog, and the
node-graph engine's next steps (section 5). This is the authoritative inventory; the other docs note a
gap in a line and point here.

The runtime architecture is [01-architecture.md](01-architecture.md); nothing below but section 5
changes it.

---

## 1. Feature gaps

Things a user could reasonably expect that aren't wired yet. None block shipping.

### Raw LSDj Keyboard mode (LsdjSyncMode 4)

`lsdj-sync` implements every sync mode except raw **Keyboard**: MidiSync, MidiSyncArduinoboy,
MidiMap, KeyboardMidi, MidiPassthrough, MidiOut (ArduinoboyMaster), and MasterSync all ship
([script/nodes/lsdjSync.ts](../packages/retroplug/src/script/nodes/lsdjSync.ts), the role's protocol
node). Raw Keyboard sends nothing because it needs a per-block **`keys` feed**, which nothing supplies
yet: `rp.lsdj.sync` has no input for host keys (the DSP kernel carried empty `buttons` and `keys`
arrays until B3b deleted it), and no host stages any. A `keys` input on the node, and a host I/O node
that feeds it, unblock this mode. (See [04-roles-protocol-nodes.md](04-roles-protocol-nodes.md).)

### NES per-mapper expansion sub-channels (multichannel audio)

Per-console channel output ships end-to-end ([10-multichannel-audio-out.md](10-multichannel-audio-out.md),
steps 1–6): Game Boy 4-stem output, NES stereo-mod pins, and NES 5 individual core channels. The one
open piece is the **individual expansion voices** (VRC6 pulse/saw, VRC7 6×FM, N163, MMC5, FDS, S5B),
which live inside each mapper's audio class before they sum into the chip's `AudioChannel` delta, so
they need a per-mapper audio tap deeper than the `NesSoundMixer` edit, and VRC7's emu2413 core is the
one large tap. (Test material is no longer the obstacle: `resources/roms/bliptoaster-{vrc6,vrc7,n163,
mmc5,s5b}.nes` are all committed, and `reaper:params-vrc7` already drives one of them.)

### Standalone disk-wins reopen

In a DAW the host chunk is authoritative (get/setState). The standalone starts **empty** unless
`RETROPLUG_AUTOLOAD_PROJECT` seeds it; reopening the last-saved project from disk on launch is not
built ([05-data-persistence.md](05-data-persistence.md)).

### Live memory-region subscription

There is no live "watch RAM" streaming path (`enableMemorySnapshot(type)`). Reads are one-shot through
the [`SnapshotRegistry`](../packages/native/src/host/engine/SnapshotRegistry.hpp) read door, and the
consumers that want live memory poll it: the LSDj overlay per frame, and the HD player
([useLsdjHdSession.ts](../packages/retroplug/ui/screens/hd/useLsdjHdSession.ts)) on a frame divider,
because a full 128 KiB WRAM copy plus a song decode is too expensive to do every frame. So this is a
cost question, not a capability one - an arming seam would let those two stop copying the whole image
to read a few hundred bytes of it.

### MIDI **out** is capped at 4 bytes

MIDI **in** takes every size (`PluginDSP.cpp:319` reads `dataExt` when a message outgrows the inline
`data[4]`), so a DAW can upload an N163 wave the way the CLI does. The **out** path drops anything
longer: `PluginDSP.cpp:363` skips events over `MidiEvent::kDataSize`. Lifting it is not a one-line
change - the plugin never sets `dataExt`, so simply removing the guard is a null dereference in
DPF's VST3 writer. It also buys little: VST2 drops messages over 4 bytes itself, CLAP drops over 3,
and VST3 drops SysEx regardless, so the payoff exists only on the JACK standalone.

### CLI debugger: Mesen `.mlb` labels

The debug RPC facet ([09-cli-debugging.md](09-cli-debugging.md)) is built out — APU/PPU state,
CPU/memory peek + poke, per-frame register-event capture (`drainEvents`), breakpoints, trace, step,
profiler, and cc65 `.dbg` labels. The one remaining item is Mesen native `.mlb` label files, which
need a new parser (Mesen's C# one isn't vendored). A CLI-only nicety.

---

## 2. Deferred / dropped

Intentionally not planned; listed so the intent isn't lost.

- **About panel** — dropped (offered little); the one menu item still marked deferred in
  [menuDefs.ts](../packages/retroplug/ui/screens/menu/menuDefs.ts).
- **LV2** — deferred indefinitely; its out-of-process DSP/UI split doesn't fit RetroPlug. (VST2 + AU
  now build — see [06-build-test.md](06-build-test.md).)
- **`ui?` render descriptor + third-party extension model** — a role's own settings UI, and
  registering `RoleType`s / ROM providers / behaviours from outside the built-ins, are future work.
- **Savestate slots** / **sav inspector** — never built; the state-snapshot machinery and the pure-TS
  sav codec exist, but a user-facing multi-slot feature and a React sav view do not.
- **Web / Emscripten port** — design-only.

---

## 3. Pending refactor

- **The `SnapshotRegistry` double-copy.** The read door copies from each core's own tear-free triple
  into a registry-owned buffer because the shared `SystemBase` can't yet publish straight into the
  registry ([SnapshotRegistry.hpp:23](../packages/native/src/host/engine/SnapshotRegistry.hpp#L23)).
  It is a documented redundancy, not a bug; collapsing it is a `SystemBase` refactor, not urgent.

---

## 4. Code-comment cleanup backlog (note, don't fix)

Stale scaffolding to sweep opportunistically — not work to schedule.

A handful of in-code deferral / provenance comments still describe an earlier state and should be
swept as their feature lands or when touched. Known ones:

| Location | Marker |
|---|---|
| [host/engine/Engine.cpp](../packages/native/src/host/engine/Engine.cpp) | `fastBoot` takes effect on the next restart, not live |

Not cleanup (intentional domain terms that read like TODOs): `deferredProject` in
[systemsStore.ts](../packages/retroplug/src/systemsStore.ts) and `kind: "deferred"` in
`fileSelection.ts` are the sibling-`.rplg` load-handoff concept, not incomplete work.

---

## 5. The node-graph engine: after B3b

Phase B of [modular-graph-design.md](../docs/modular-graph-design.md) moves the engine onto the graph in
steps, each held bit for bit to the parity baseline ([06-build-test.md](06-build-test.md)). B3a
([modular-graph-phase-b3a-design.md](../docs/modular-graph-phase-b3a-design.md)) made the DSP kernel's
input and output real wires (host I/O nodes, `serialIn` / `midi` / `bytes` / `buttons` inputs on every
system node, the Game Boy's `serialOut`, `rp.n8.cart-mirror`) and ran the kernel as one TS node in the
script host every Engine embeds. LSDj master sync was then fixed on its own
([lsdj-master-sync-fix-design.md](../docs/lsdj-master-sync-fix-design.md)): a Game Boy's serial-out capture is
a mode, `listen` for master sync and `drive` for MI.OUT, and the decoder sends a clock per byte and a stop after a
200 ms silence timed on the bytes' frames. B3b
([modular-graph-phase-b3b-design.md](../docs/modular-graph-phase-b3b-design.md)) split the kernel into one
protocol script node per role beside the MIDI routing and the controller session, moved `kernelProjection`
into `buildGraph`, took zod out of the script-node bundle (78 KB, from 660), gave `rp.host.midi-out` each
input's system from its settings, and deleted the kernel. What is left, in order:

- **Master-sync clocks at their byte's frame**, its own change, with its own re-record: master sync sends each
  clock at frame 0 of the block after its byte, so in a large block its clocks bunch where they could keep
  their spacing, one block late. Each LSDj node has its own `midiOut` port now, so its queue no longer orders
  another's; but this would be the first producer of host MIDI past frame 0. `rp.host.midi-out` already keeps the
  host's list frame-sorted across its inputs (each message goes out at the later of its frame and the last
  one handed out in the block), so a host-side sort is no longer the alternative. But the clamp raises a later
  input's earlier frames to an earlier input's, so a second LSDj in master sync would have its clocks bunched behind the first's: that change
  has to turn the clamp into a merge by frame, or its re-record would bake the bunching into the baseline.
- **The capture mode as a Game Boy node setting** (Phase C, with the other live knobs): carried in the
  published document rather than through `ConfigField::SerialOutCapture` plus a publish, it would make a mode
  switch atomic and retire the master-sync design's first accepted limit (its section 10).
- **One QuickJS context per script node, decided before user-written nodes.** Every script node runs in
  the audio runtime's one context, so they share a realm: the script host keeps its own writes and its
  reads of a node's methods off the shared prototypes ([02-native-host.md](02-native-host.md)), but one
  node's code can still change what another's reads through (an accessor on `Object.prototype` or
  `Array.prototype`, a global, a definition in `__rp_scriptNodes`), and a field a node deletes puts the
  host's next write of it back on the chain. Promise jobs are charged to no node either:
  `ScriptHost::runAudioJobs` runs every node's at the block's end under one deadline and counts a failed
  one as nobody's throw, so a node that queues a never-ending reaction every block burns a full deadline
  (2 s of CPU) every block and is never bypassed, as a `process` that hangs would be. A context per node
  in the same runtime (its own intrinsics and prototypes) closes all of it, at a per-context memory cost,
  and names the node a job ran in (`JS_ExecutePendingJob` reports the job's context); decide it before
  nodes users write (Phase C, the Reaktor-scale aim). B3b made the project's nodes many, not one: seven is the
  number of node types, and a project places the routing node, a controller when enabled, and one node per protocol
  role.
- **Found while building B3b, left as they are:**
  - The MI.OUT and master-sync decoders (`arduinoboyDecodeSerialOut`, `arduinoboyMasterSyncBlock` in
    [lsdjArduinoboy.ts](../packages/retroplug/src/lsdjArduinoboy.ts)) build each message they emit as a new
    array, about 3 JS allocations for every host MIDI message they decode: the one per-block JS allocation
    left on a production path (every other protocol node allocates nothing in a warm block,
    [08-profiling.md](08-profiling.md)). Emitting into a reused buffer is the decoders' own change.
  - `defineNode` keeps to ES2015's library (`Set`, `Object.entries`, `includes`), so the script-node bundle
    holds no zod but is not yet MicroQuickJS-ready; `settingsSchema.ts` already keeps to ES5's.
  - `routeBlockInto` in [midiRouting.ts](../packages/retroplug/src/midiRouting.ts) has no caller since B3b:
    the routing node routes each message with `routeTarget`, the same arithmetic, and `routeBlock`, its pure
    twin, stays as the reference the routing node's test holds every mode to. It can be deleted.
  - `DirectGraph.setPipeline` hands a node its role's config as given, where a store hands the config its
    schema parsed, and a node's settings fill their own defaults: a direct caller's `lsdj-sync` with no `mode`
    is MIDI sync, where the kernel read a missing mode as nothing.
  - **Readers that decide "is this system LSDj" (or mGB) by a role's presence, not its placement.** In the rare
    case a ROM path changed between an LSDj and an mGB build, a system holds both `mgb` and `lsdj-sync`, and the
    pipeline places one (the one the ROM calls for). The graph is right; these readers are not:
    `trackerIntegrationFor` ([trackerIntegration.ts](../packages/retroplug/src/tracker/trackerIntegration.ts), by
    `markerRole` presence), so the LSDj menu's sync cyclers
    ([menuDefs.ts](../packages/retroplug/ui/screens/menu/menuDefs.ts)) edit a left-out `lsdj-sync` on an mGB cart;
    `hasRole(..., "lsdj-sync")` in `ui/App.tsx`, `ui/screens/grid/useLsdjRuntime.ts`,
    `ui/screens/hd/useLsdjHdSession.ts` and `projectStore.ts` (which tests `roles.some((r) => r.kind ===
    "lsdj-sync")`); and `projectParameterMap`
    ([parameterMap.ts](../packages/retroplug/src/parameterMap.ts)), which still exposes mGB's CC lanes for an LSDj
    cart carrying a stale `mgb`. The fix is `placedRole(...)`
    ([graph/protocolNodes.ts](../packages/retroplug/src/graph/protocolNodes.ts)) in those readers. Not fixed.
  - **The controller's MI.MAP override changes only the document.** `controllerSyncOverride`
    ([graph/controller.ts](../packages/retroplug/src/graph/controller.ts), applied in `buildGraph`) runs an LSDj's
    node in `midiMap` for a cart reading MidiMap, but `serialOutCaptureFor` (`systemsStore.ts`) and
    `__rp_syncLatencyMs` still follow the stored `lsdj-sync` mode: an LSDj stored in MI.OUT whose cart reads MidiMap
    keeps capture "drive" while its node runs `midiMap`. Pre-existing: `kernelProjection` behaved the same.
  - A profile build's `apu-render` span begins where the last script node's `process` ended, which is right
    while every script node runs before the systems, as `buildGraph` places them; a script node placed after
    a system would need it revisited.
  - A few test-native case names still say "the kernel" (in `dsp-lsdj-midisync`, `dsp-risa-sync`,
    `dsp-sms-sync`, `lsdj-midiout` and `plugin-controlplane`), where they mean the protocol nodes. Renaming
    a case is harmless but was kept out of B3b, which changed no test's identity it did not have to (the one case
    `dsp-serial.test.ts` renamed, because its name described the deleted kernel, is the exception).
- **Found while building B3a, left as they are:**
  - `rp.host.midi-out` and `rp.host.controller-out` append one `std::vector` per message to the engine's
    host outputs (`Engine::midiOut_` / `controllerOut_`, the `rp.engine.host-io/2` service in
    [Engine.cpp](../packages/native/src/host/engine/Engine.cpp)), an allocation on the block thread for
    every message out; a flat arena, as the staging has, would remove it.
  - The script node `Block` ([defineNode.ts](../packages/retroplug/src/graph/defineNode.ts)) has no
    resource accessor and no link peers yet.
  - With commits to script nodes never making `setGraph` wait (their prepare is stateless), the
    GraphHost's command ring (256 slots, 255 usable, shared with params and the engine's commands) is what
    bounds edits while no block runs: a host that stops calling `process` while the plugin stays active
    queues one plan per edit, each holding its buffers, and once the ring is full `setGraph` answers busy.
    The publisher keeps its model, so the next publish sends its document even if unchanged and the latest
    model lands. Coalescing queued, unadopted plans is the alternative, should it bite.
  - The adoption wait itself is global: one commit to a type without `RP_TYPE_STATELESS_PREPARE` holds back
    an edit that touches only stateless nodes (a gate per instance is possible). And every `node.hpp` node
    without `needs_rebuild` already prepares statelessly, so for C++ nodes the flag could be derived rather
    than declared.
  - A Game Boy that clocks its serial port itself, a CGB on its fast clock, can send more in a block past
    about 12000 frames (6000 in double speed) than its fixed `serialOut` holds; the rest is dropped and
    counted ([SystemNodes.cpp](../packages/native/src/host/graph/SystemNodes.cpp) `kSerialOutCapacity`).
  - `rp.lsdj.sync`'s `midiOut` (the kernel node's, before B3b) is sized for a decoded MI.OUT stream at its
    densest up to 8192-frame blocks (1024 messages, a captured byte every 8 frames), while `serialOut` is
    sized for 65536: in a larger block the decoded stream can pass 1024 messages, and the rest is dropped and
    counted ([nodes/ports.ts](../packages/retroplug/src/script/nodes/ports.ts) `MIDI_OUT`).
  - `rp.host.midi-in`'s sort at the source merges two frame runs in linear time, which is what real
    plugin hosts stage (the DAW's events, then the automation CCs at frame 0); a staging of three or more
    runs falls back to an in-place insertion sort, quadratic in what was staged, on the block thread. The
    staging has no count cap either (past its reserved room it grows), so a host that stages a great many
    messages out of order pays for every one, though the input node's queue keeps 2048
    ([Engine.cpp](../packages/native/src/host/engine/Engine.cpp) `Staging::takeSortedInto`). A cap at what
    the input node carries, or a merge of k runs, would bound it.
  - A feedback source whose queue capacity changes while an audio thread runs gets a fresh, empty front:
    the block in flight is lost, and `droppedMessages` never counts it, since the carry and its count run
    only while quiescent ([Planner.cpp](../packages/native/src/graph/Planner.cpp) `front`). No port changes
    its capacity live today: the Game Boy's `serialOut`, the one feedback source, is fixed.
  - `retroplug-engine-graph-test` replaces the global `operator new` with a counting allocator
    ([CountingAllocator.cpp](../packages/native/test/graph/CountingAllocator.cpp)) that allocates through
    `_aligned_malloc` on Windows, so a core or QuickJS freeing what `new` made with `free()` would crash
    there; no Windows CI run has exercised it yet.
  - `retroplug-ts-stripper-bundle` fails in a build directory outside the repository:
    [tools/build-ts-stripper.mjs](../tools/build-ts-stripper.mjs) runs in the binary dir and sets no
    esbuild `absWorkingDir`, so its `typescript-5` alias resolves from there (unlike
    [tools/build-ui.js](../tools/build-ui.js), which sets it to the repo root).
