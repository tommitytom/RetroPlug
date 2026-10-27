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
([dspRoles.ts](../packages/retroplug/src/dspRoles.ts)). Raw Keyboard emits nothing because it needs
the per-block **`keys` feed**, which nothing supplies yet: the kernel node hands the kernel empty
`buttons` and `keys` arrays every block
([kernelNode.ts](../packages/retroplug/src/script/kernelNode.ts), `blockInput`), so the kernel's
`buttons`/`keys` inputs are present but unfed. Feeding host keys/buttons per block (a port on the kernel
node, or on the protocol node that replaces it) unblocks this mode. (See
[04-roles-dsp-kernel.md](04-roles-dsp-kernel.md).)

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
| [systemRoles.ts:13,59](../packages/retroplug/src/systemRoles.ts#L13) | names kit-patch as the motivating example for the deferred `ui` descriptor. The descriptor is still unbuilt, but kit patching shipped without it - `lsdj-assets` is a no-DSP role that compiles on the control plane through the `compileKit` RPC - so the example needs replacing, not the entry |

Not cleanup (intentional domain terms that read like TODOs): `deferredProject` in
[systemsStore.ts](../packages/retroplug/src/systemsStore.ts) and `kind: "deferred"` in
`fileSelection.ts` are the sibling-`.rplg` load-handoff concept, not incomplete work.

---

## 5. The node-graph engine: after B3a

Phase B of [modular-graph-design.md](../docs/modular-graph-design.md) moves the engine onto the graph in
steps, each held bit for bit to the parity baseline ([06-build-test.md](06-build-test.md)). B3a
([modular-graph-phase-b3a-design.md](../docs/modular-graph-phase-b3a-design.md)) is built: the kernel's
input and output are real wires (host I/O nodes, `serialIn` / `midi` / `bytes` / `buttons` inputs on
every system node, the Game Boy's `serialOut`, `rp.n8.cart-mirror`), the kernel runs unchanged as one TS
node, `rp.dsp.kernel`, in the script host every Engine embeds, and the sealed native kernel node, its
fan-out, the `dspSetSystems` / `dspLoadKernel` / `compileScript` RPCs and the boot-time kernel load are
gone. Staging host MIDI allocates nothing within the room reserved at construction (one flat arena per
staging). After it, LSDj master sync was fixed on its own
([lsdj-master-sync-fix-design.md](../docs/lsdj-master-sync-fix-design.md)): a Game Boy's serial-out capture is
a mode, `listen` for master sync and `drive` for MI.OUT, and the decoder sends a clock per byte and a stop after a
200 ms silence timed on the bytes' frames, which fixed B0's sixth and eighth recorded defects and let a stopped LSDj
start again. What is left, in order:

- **B3b.** One protocol node per role (`mgb.midi`, `lsdj.sync`, `risa.sync`, `sms.sync`,
  `n8.midi-to-fifo`, the MIDI routing, the Launchpad controller), each with its own ports;
  `kernelProjection`'s special cases (the synthesized `midi-routing` and Launchpad stages, the forced
  `lsdj-sync` mode for MI.MAP) moved into `buildGraph`; a script-node bundle with no zod in it (the
  kernel's settings schema and `dspRoles.ts` carry it today); then the kernel node and `DspKernel`
  deleted. How several stages' outputs merge into one system input is B3b's to decide; a system input
  must keep "all MIDI, then all bytes" per core, and `rp.core.merge`'s output queue is the default size,
  so merging large wires needs a merge whose output capacity its settings give. A port per protocol also
  ends the private 4-byte system prefix on host MIDI out (`rp.host.midi-out` takes the id from its
  wiring instead), and with it the one port's reliance on every role sending its host MIDI out at frame 0.
  A Game Boy's `serialOut` is to be wired only into a node that decodes MI.OUT or master sync, so one
  without pays nothing for a driving capture's idle bytes. Once each protocol node has its own `midiOut`
  port, the frame-0 rule goes (B3a design section 4.4) and master sync can send each clock at its byte's
  frame, one block late, so clocks keep their spacing instead of bunching at frame 0 in large blocks.
  Carrying the capture mode as a Game Boy node setting in the published document, rather than through
  `ConfigField::SerialOutCapture` plus a publish, would make a mode switch atomic and retire the
  master-sync design's first accepted limit (its section 10). And a profile build's `apu-render` span
  starts where the last script node's `process` ended, which is right only while every script node runs
  before the systems: a protocol node placed after a system needs it revisited.
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
  nodes users write (Phase C, the Reaktor-scale aim).
- **Found while building B3a, left as they are:**
  - `rp.host.midi-out` and `rp.host.controller-out` append one `std::vector` per message to the engine's
    host outputs (`Engine::midiOut_` / `controllerOut_`, the `rp.engine.host-io/1` service in
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
  - The kernel node's `midiOut` is sized for a decoded MI.OUT stream at its densest up to 8192-frame
    blocks (1024 messages, a captured byte every 8 frames), while `serialOut` is sized for 65536: in a
    larger block the decoded stream can pass 1024 messages, and the rest is dropped and counted
    ([kernelNode.ts](../packages/retroplug/src/script/kernelNode.ts) `KERNEL_MIDI_OUT`).
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
