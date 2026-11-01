# Phase C5: hardware as nodes

**Date:** 2026-10-31 · **Branch:** `feature/modularize` · **Status:** built: section 8's seven commits, each green and
strict parity bit for bit; what building them changed is in the sections it changed (3 to 6), and what they settled and
found is section 10

RetroPlug drives two pieces of real hardware: an Everdrive N8 Pro cartridge in a real NES, over USB serial, and a
Novation Launchpad as a control surface. Both reach the graph through special cases in the engine and the hosts: the
cart mirrors one emulated NES through a plan-level field and a sink function the engine calls, a UI message is mirrored
to the cart by a flag on a staging command, and the Launchpad's MIDI rides two engine staging fields, a command kind
and the controller halves of two host I/O nodes. This phase makes each device a node: `rp.n8.nes` takes the same inputs
as an emulated NES, so emulated, real or both is plain fan-out wiring; `rp.launchpad.surface-in` / `-out` have event
ports to and from the device; and the operations both an emulated core and a real cart can answer (memory, battery,
ROM) become capability interfaces with an asynchronous job model, so the N8's seconds-long USB transfers and an
emulated core's block-boundary writes share one API. After C5 the engine names no console and no device. It is Phase C's
last piece ([modular-graph-design.md](modular-graph-design.md) sections 6.4, 6.6 and 8), after C4
([modular-graph-phase-c4-design.md](modular-graph-phase-c4-design.md)), which left the cart mirror's NES check as the
engine's last console name and deferred the capability split (P7) to the hardware nodes that need it.

## 1. Before C5

- **The cart mirror.** `Engine::coreByteSink_` is a function the hosts install (the plugin and the SDL standalone) and
  the engine calls on the block thread; `SystemTable::cartMirror` makes the mirrored system a property of the plan,
  filled from the `rp.n8.cart-mirror` node (the only type in the `rp.n8` package), which `buildGraph` wires from the
  `midi` and `bytes` outputs feeding the first NES in slot order. `Engine::setGraph` refuses a second mirror, one naming
  a non-slot, and one whose system is not an `rp.mesen.nes` node: C4's one remaining console name in the engine.
  `Engine::cartMirrorSystemId()` answers which system is mirrored, and the hosts fall back to raw MIDI to the cart when
  none is (`PluginDSP.cpp`, `sdl/main.cpp`). `stageSystemMidi`'s `toCart` branch mirrors a UI message to the cart at
  frame 0.
- **The N8 link.** `N8Host` (one per host instance) owns the serial port (exclusive), `N8Link` streams bytes to the cart
  from a lock-free ring on its own serial thread with a lookahead, timed-release scheduler and an optional reliable link
  (framing, acks, resend), and `postControl` / `setOnConnected` carry control writes such as the expansion audio's
  master volume. SRAM dump and restore (64 KiB in 4 KiB reads), ROM upload and boot (8 KiB chunks over seconds), and the
  menu commands they need run on `N8Host`'s SD worker, reached from the UI's cart menu through the `__rp_n8LoadRom`,
  `__rp_n8DumpSram`, `__rp_n8RestoreSram` and `__rp_getN8SdStatus` hooks, and each disconnects the streaming link while
  it runs. The expansion volume reaches the cart through `__rp_setN8ExpVol`. The plugin and the SDL standalone own an
  `N8Host` each; the CLI owns none: its n8-bridge / n8-load / n8-sync / n8-play tools drive the cart from TypeScript (a
  TS twin of the Edio protocol) over the CLI-only serial facet.
- **The Launchpad.** The native transport is protocol-free (a host / controller split: scanning, INQUIRY pairing, the
  MIDI ports); the protocol, config, pages and the MI.MAP override are TypeScript; the controller session script node
  reads the device's MIDI from `rp.host.controller-in` and a connected flag, and writes LEDs to `rp.host.controller-out`.
  Behind those nodes the engine keeps `pendingControllerMidi_`, `controllerOut_`, `controllerConnected_` and the
  `StageControllerMidi` command kind; the SDL standalone stages and drains them per block; `EngineRpcService`'s
  `stageControllerIn` and `renderBlocks`' controller field are test seams.
- **Capabilities.** `SystemBase` answers memory regions, SRAM, savestates, ROM header, video, buttons and debug through
  its own virtuals; the control plane reads them through the snapshot registry and writes through the command ring
  (`readSram`, `readState`, `writeRam`), by system id. The main design's section 6.4 split (`rp.system.memory/1`,
  `.sram/1`, `.rom/1`, ...) is unbuilt.
- **Tests.** The N8's link, Edio, host and SD worker run against fakes (`FakeSerialPort`); the Launchpad's link against a
  fake MIDI port and a loopback script over virtual ALSA ports; the cart mirror in `HostIo.test.cpp` and
  `EngineCartMirror.test.cpp`. Real-hardware timing, the reliable link's recovery on a console and every device
  behaviour need the hardware, which this devcontainer has (a real NES with an N8 Pro, through the hardware-lab skill).

## 2. Decisions taken

The user's, on 2026-10-31:

- **Everything in one phase**: both device nodes and the capability interfaces, with the N8's long operations moved
  behind them. Not the streaming nodes alone, and not the Launchpad first.
- **An `rp.n8.nes` is placed by a setting**: a system's "drive the real cart" setting places it; the node always
  activates, and whether the cart is attached is runtime state. Not placed only while connected (every plug, reset and
  SD operation a graph edit), and not placed for every NES.
- **Capabilities are asynchronous jobs**: a request completes later, at a block boundary on an emulated core, on the
  device's thread on the cart. Not capabilities for the quick operations only, and not synchronous calls that would
  freeze the control thread for seconds.

## 3. Who owns what

**Hosts keep owning the devices.** Opening the serial port, scanning MIDI ports, pairing, reconnecting after a reset or
a job that stops the link, and one port per process all stay in `N8Host` / `N8Link` and the Launchpad's host /
controller transport. **Nodes bind to a device through a host interface, `rp.host.devices/1`**: a node asks for a
device link by name (`"n8"`, `"launchpad"`, through a typed acquire call per device, so its handle is concrete) and
gets a handle, or a handle that is never connected in an instance whose host does not own the device. A handle offers
the device's streaming rings, its connected state and, for a cart, its capability job runner.

**Placement is by setting, not device state**: a system's "drive the real cart" setting places `rp.n8.nes`, the
controller setting places the two surface nodes. All of them always activate; a `connected` control output says whether
the device is attached, and pushes go nowhere while it is not. A plug, unplug, reset or device job never edits the
graph.

**The engine loses** the cart mirror (`coreByteSink_`, `SystemTable::cartMirror`, `cartMirrorSystemId`, its NES check,
the `rp.n8.cart-mirror` type), `stageSystemMidi`'s `toCart`, the four controller fields and their command kind, the
controller halves of `rp.host.controller-in` / `-out`, and the hosts' raw-MIDI fallbacks. `stageSystemMidi` itself goes
too: with TS staging at nodes, nothing calls a system-addressed form any more (section 10).

## 4. `rp.n8.nes`

**Ports**: the same `midi` and `bytes` inputs as `rp.mesen.nes`; in its block it pushes each message onto the link's
ring with its frame offset, so N8Link's timed-release scheduler is unchanged. A `connected` control output, no audio
output, and an `expansionVolume` param like the emulated NES's, handed to the link on a change (the link keeps the
standing value and its serial thread applies it, so the block makes no call into the device) and again on every
reconnect. It declares `threads`, and `unique`: one cart, one link, one node.

**Wiring is fan-out**: `buildGraph` wires the protocol node outputs that feed a system's `rp.mesen.nes` into its
`rp.n8.nes` too. The emulated node always stays (a "real cart only" system is later work).

**UI-staged MIDI**: the staging command addresses a node of the running plan instead of a system, and the node decides
which door the bytes enter (`rp.stage/1`: a system node hands them to its core's ingress, the cart node pushes them at
the link); TS stages to the system node, and to its cart node when one is placed, so a UI message still reaches the real
cart and the engine stays neutral.

**Lifetime**: disconnected, pushes go nowhere without allocating and `connected` reads false; reconnecting is the
link's job on its own thread; removing the node releases its handle at reclaim, on the control thread, a job it still
runs failing there rather than at the next publish. That reclaim, which frees the plan the swap replaced and every node
only it held, comes at the host's next idle tick after the audio thread adopts the swap (the plugin's editor idle, the
SDL standalone's frame loop: within a frame or so), at once while no audio thread runs, and otherwise at the next graph
publish, deactivate or teardown (a plugin whose editor is closed has no idle of its own; the harness hosts reclaim when
their session calls `drainReleased` or `stopAudio`). A pause a job makes drops and counts the block's pushes, and the
node re-sends the note-offs a pause dropped for notes the cart holds, at frame 0 of the first block the link reads
connected again, so no note is left sounding. Leaving the plan with notes held (the setting turned off, the NES removed
or replaced, the drive moving, a project loaded or closed while the DAW plays) is the same hazard with no later block
to pay it in, so the node's destroy, at that reclaim, hands what it holds to the link (`CartLink::releaseNotes`), whose
serial thread writes their note-offs behind every byte pushed before the call, on a streaming turn (never inside a
job's pause), and before the port closes on a disconnect; a link that is down drops them. The destroy runs on the
control thread after the swap and may not push into the ring itself, as the audio thread, or the node taking over, may
be pushing then. A handover releases the old node's notes and starts the new node's books empty, so a key held across
it sounds again at its next note-on. Each plugin instance keeps its own `N8Host` and the port stays exclusive, so with
two instances the first to connect has the cart and the other's handle stays disconnected with the open error in its
`lastError`.

## 5. `rp.launchpad.surface-in` and `rp.launchpad.surface-out`

**Two nodes, one each way.** One node that both fed a session and followed it is a node-level cycle, and the only wire a
cycle breaks on is a feedback wire, which delivers the previous block. So the device's two directions are two nodes over
the one link, each `unique` and each `threads`:

- **`rp.launchpad.surface-in`**: `in`, an events output (the device's MIDI drained from the link's receive ring at the
  start of the node's block, at frame 0: the device's traffic is not timed against the block); `connected`, a control
  output. Its first block discards what the link held when it bound it, so traffic the device sent before the plan
  existed is not replayed into a session.
- **`rp.launchpad.surface-out`**: `out`, an events input written to the link's send ring.

**Wiring**: the controller session script node reads `surface-in`'s `in` and `connected` and sends its LED and page
output to `surface-out`'s `out`; its handshake (Programmer mode and a repaint on the block the link first reports
connected) is unchanged. `buildGraph` places both whenever the controller setting is on, beside the session, and
neither when it is off (a surface-in no session reads would drain the device's traffic nowhere).

**They replace** the engine's controller staging, output buffer, connected flag and command kind, the controller halves
of the two host I/O nodes, and the SDL standalone's per-block controller staging and drain. The host keeps scanning,
pairing and the farewell; the config, pages and MI.MAP override stay TypeScript's. The session's recorded input and
output digests and its per-block LED stream are identical through the two nodes, block for block.

## 6. Capabilities as asynchronous jobs

**The interfaces** (P7's split, restricted to what C5 needs; video, buttons and debug stay as they are):
`rp.system.memory/1` (named regions, read and write), `rp.system.sram/1` (dump, restore), `rp.system.rom/1` (header and
title; upload-and-boot on the N8 node only, an emulated system's new ROM staying C3's rebuild). A system node presents
each through a nullable accessor on `SystemBase` (the `channelTap()` pattern), and an emulated system refuses
`sram.restore`: its battery is restored by a rebuild with the bytes as the seed. `rp.n8.nes` presents `sram/1` and
`rom/1` (the main design's section 6.6), not `memory/1`: a cart-memory read is a DMA that can wedge a running game, so
on this hardware the battery is reached only through the SD card's own save file at the menu
(`EDN8/gamedata/<rom>/bram.srm`), a dump reading it in 512-byte blocks and a restore writing it in 4 KB ones, the game
loading it at its next boot.

**The job model**: the control thread submits a job (node id, capability, operation, arguments) through one engine RPC
and gets a job id; completions drain from a queue TS polls. On an emulated core a read is served from the snapshot
registry and completes at once, and a write goes through the command ring and completes at the next block boundary, as
`readSram` / `writeRam` do today (those system-id RPCs stay as thin wrappers). On the N8 node a submission first reads
the cart's config block on the link's worker thread, between MIDI chunks, the expansion volume's path, and only on a
turn quiet for the read (an ack landing inside it would be taken for the reply's first bytes): one with nothing
outstanding on the reliable link, or once the cart has said nothing for 250 ms since its oldest outstanding frame first
went out, as one at its menu does. The three transfers run at the menu only, so a job a running game will refuse (or a
cart whose block does not answer, or whose link keeps acknowledging for the probe's whole second) is refused there, and
the stream never pauses for it. A job that is taken then runs on that thread: streaming pauses, `connected` reads false
for the duration and pushes are dropped and counted, the transfer runs, streaming resumes, the completion is posted. The
probe waits on the submitting thread, up to its 1 s bound (the DAW's main thread in the plugin, the UI loop in the SDL
standalone): usually one USB round trip, the whole second for a reliable link that keeps acknowledging before its
refusal. One job at a time per device (a second is refused busy); a job while disconnected fails "not connected". A
cart's `rom.header` and `rom.title` answer at once from the image and name this host last booted, with no transfer. A
`rom.upload` is the one job that does not resume streaming: it writes the image, installs and boots it through the
cart's menu, and closes the port, so the link reads disconnected and its result says streaming stopped. A node removed
while its job runs abandons it at reclaim, and the device stops at its next safe point.

**TS**: `backend.capability(nodeId, op, args)`, a promise over submit and poll, the same for emulated and real. The UI's
cart menu (back up and restore the cart's SRAM, load a ROM to the cart) moves onto it, and the `__rp_n8LoadRom` /
`__rp_n8DumpSram` / `__rp_n8RestoreSram` / `__rp_getN8SdStatus` hooks and `N8Host`'s SD worker go, the SD and menu
commands those operations use running as the node's jobs; `__rp_setN8ExpVol` goes too, the node's param replacing it.
The CLI's n8 tools keep their raw serial path: they run with no engine and no `N8Host`, and need the raw link (peek,
poke, the sniffer, FIFO reads) that the capabilities deliberately do not offer, the battery dump included, which only
they still take off the cart's own memory.

## 7. Tests

- **Parity** holds bit for bit at every commit, nothing re-recorded: the emulated audio path does not change.
- **`retroplug-engine-graph-test`** with fake device links: the same bytes reach the emulated NES and the cart node, in
  order and at their frames; a missing device faults nothing; `connected` flips; a job pauses and resumes streaming;
  busy and not-connected refusals; a warmed block with a placed cart node and the surface's two nodes allocates nothing;
  Launchpad session's recorded digests identical through the surface; the emulated capabilities against today's reads
  and writes.
- **`pnpm test`**: placement by setting, the fan-out wiring, stage-to-a-node, the capability API and its promise
  lifecycle; the role goldens and `ProtocolNodes.test.cpp`'s session documents re-recorded (a wiring change, the
  digests unchanged).
- **ThreadSanitizer and AddressSanitizer** on every case touched.
- **Real hardware at acceptance** (the hardware-lab skill: the NES and N8 Pro wired into this container): load a ROM
  through the new capability, drive it with MIDI through `rp.n8.nes` and record and check the console's audio, and
  round-trip the cart's SRAM. The Launchpad by loopback, unless one is attached.

## 8. Landing

Seven commits, each green:

1. **`rp.host.devices/1`**: device handles over N8Link and the Launchpad link; fake links for tests.
2. **Capabilities**: the interfaces, the job model on the emulated cores, the engine RPC, the completion queue, the TS
   `capability()` API.
3. **`rp.n8.nes`**: streaming, `connected`, `expansionVolume`, jobs on the link thread, beside today's cart mirror.
4. **`rp.launchpad.surface-in` and `-out`.**
5. **The TS switch-over**: placement by setting (additive: no migration), fan-out, stage-to-a-node, the session wired to
   the surface, the cart menu on capabilities, the goldens re-recorded.
6. **The old paths deleted**: the cart mirror, `coreByteSink_`, `toCart`, the controller fields, command and host I/O
   halves, the raw-MIDI fallbacks, the `__rp_n8*` operation hooks and `N8Host`'s SD worker.
7. **Docs.**

Then the acceptance run, including the hardware checks. As C4, without Flash-Next: a prototype whose stages 2 (the job
model) and 3 (`rp.n8.nes` and pausing the link for a job, smoke-tested on the real cart) are built by Opus and reviewed
by DeepSeek, the other stages built by DeepSeek and reviewed by DeepSeek, each fixed after its review; the plan generated
and replayed, then an Opus plan review; DeepSeek implementers and follow-ups, Opus code reviews of tasks 2, 3, 5 and 6,
a DeepSeek acceptance run, an Opus final review.

## 9. Prototype questions

Section 10 answers these one by one, and records what else building it settled.

- How does a node reach a host-owned device across hosts (the plugin, the SDL standalone, the CLI, the Node addon, the
  render host, the UI harness), and what does a host with no device give it?
- Does pausing N8Link's streaming for a job need a change in the link (today an SD operation disconnects it), and what
  happens to bytes the graph pushes meanwhile?
- Where does today's UI-staged MIDI enter (who calls `stageSystemMidi` and with what), and does stage-to-a-node cover
  every caller?
- What does the UI's cart menu need from the capability API (progress, the busy state it shows today), and does the
  ROM load's "reconnect off, enabled cleared" lifecycle survive as a job?
- Does the session's message digest stay identical through the surface, block for block?

## 10. As built

Section 8's seven commits landed in order, each green, strict parity bit for bit at every one and nothing re-recorded:
nothing on the emulated audio path moved, so the baseline never did. This section records what the stages decided where
the design was silent, what section 9 turned out to be, what building it changed in sections 3 to 6 (each correction is
in place there), and the findings worth naming; each stage's own report is the full record.

### What each stage settled

- **1, `rp.host.devices/1`.** Two typed acquires (`acquireCart(ctx, "n8")`, `acquireSurface(ctx, "launchpad")`) rather
  than one name-keyed union, so a node's handle is concrete and no kind tag or downcast exists; the name is still the
  registry key. An unknown name and a device a host does not own answer the same never-connected handle, never null, so
  a document plans and runs in every host and a node needs no null check; both fallbacks count what they drop, which the
  surface's stage added to its interface. The sample rate is the push's argument (the
  block's), so a host rate change needs no notification, and `flush` is accepted and dropped by the cart adapter (the
  arm byte is the barrier it stands for). `setExpansionVolume` takes the cart's register value (0..255, or -1 to leave
  it to the cart), not a percent. The surface link's one message bound is 560 bytes, the protocol's worst case, both
  ways. `acquire` / `release` are optional no-op virtuals, so a fake counts the balance and a node keeps a handle
  across a reactivate (`GraphHost::reactivate`); a release is forwarded only while its link is still installed, so a
  late one dereferences nothing. `Engine::addNodePackage` is a TEST / HARNESS seam, the analogue of the script-bundle
  constructor. A host that owns a device tears its plans down first (`Engine::clearGraph`) and uninstalls its links
  second, while every device member is alive: member order could not put them before the engine, which builds cores
  through the host's services.
- **2, the capabilities and the job model.** The interfaces are restricted to what the control plane reads:
  `memory/1` lists one region, the published work RAM; `sram/1`'s dump is the battery bytes; `rom/1`'s header is a
  prefix of the image and its title the raw field up to its first byte that is not printable ASCII (so a Game Boy
  title that fills its 15 bytes stops before the CGB flag at $0143); `state/1` stays the system-id RPC, which this
  design does not list. An emulated `sram.restore` is refused at submission: a battery is restored by a rebuild with
  the bytes as the seed, and SameBoy's published battery is a savestate slice refreshed every 0.5 s, so a live restore
  could not honour "visible to the next read". A write completes when it is readable, not when it is applied: it is
  applied at the drain and marked done after the next block's publish, so `await write; read` sees the bytes. A job
  whose system left the plan before it ran fails `Removed`, one the engine discards at teardown fails `Discarded`, and
  the 64-outstanding bound is back-pressure that keeps room for every completion (a dropped completion is a promise
  that never settles). The quiescent path completes a write before `submitJob` returns, which is the CLI between
  renders. The old system-id RPCs are thin wrappers over the same reads and writes; the one behaviour that changed is
  `writeRam`'s full-ring answer (false, where it answered true and dropped the write). TS adds `backend.capability`,
  `pumpJobs` and `rejectPendingJobs`: the promises are one queue per engine (a runtime binds one engine), the pump runs
  right after each submission, on the UI's frame tick, and, in a host with no UI tick that runs its own audio thread,
  after each audio-driver call, with a submission waiting through a native sleep and, past a 10 s bound, rejecting
  rather than handing back a promise no pump would settle; closing a project rejects what is pending, and a queue that
  owes nothing leaves the set the time-advancing pump walks.
- **3, `rp.n8.nes`.** Pausing is a real pause inside the serial thread, not a disconnect and reopen (section 9's
  second answer): the thread sets `connected` false, writes what the ring already holds at its release times, waits
  (bounded) for the reliable link's outstanding frames to be acknowledged, runs the job on its own Edio, and resumes
  with the port still open and the sequence state untouched. A throw that escapes a job is the port's, so the link
  records it (`lastError`), has a job that never started report the link went down (one whose own `run` threw did run,
  and is not run again), and stops streaming as a failed write stops it, closing the port when the job had already
  decided to stop so an upload still frees it. The node abandons a job it still runs in its destructor rather than the
  engine failing device jobs centrally, so every touch of the record is under the slot's lock and the record cannot
  outlive the device's use of it; the failure then lands at the plan's reclaim. `rom.header` and `rom.title` answer
  from what this host last booted, at once; the cart has no query for the image it runs, and a PRG read is a DMA.
  `rom.upload` takes the image's bytes and a plain file name, writes it to the card in 8 KB chunks, installs and boots
  it through the menu, and stops streaming so the CLI's tools can open the port. The first battery dump was one 64 KB
  read, not sixteen 4 KB ones: measured on the cart, a memory read answers about a second after it is asked whatever its
  size (one 64 KB read 1150 ms, 16 x 4 KB 16174 ms, 64 x 1 KB 60979 ms). The node keeps the notes the cart holds and
  the ones whose note-off a pause dropped, and re-sends them. The type's category is `device`, not `system`, and the
  package moved to `N8Nodes.cpp`.
- **3, reworked: the battery through the SD file.** The first battery jobs read the cart's own memory and the console
  proved that wrong: a 64 KB read with a game running froze it, and a read at the menu left the menu deaf to its
  commands (the CLI's own `--screenshot` timed out). So `sram.dump` and `sram.restore` reach the card's save file
  instead, at the menu only: a dump lists `EDN8/gamedata/<rom>` and reads `bram.srm` in 512-byte blocks (62 s for
  66,560 bytes on the hardware, about 1.08 KB/s whatever the block size), and a restore writes it in 4 KB ones (716 ms),
  creating the folder and saying so in its result, with no readback (the writes are acknowledged per kilobyte). The
  game loads it at its next boot, so a restore goes before a `rom.upload`. The ROM is the job's `name`, else the file
  name this host last booted; a running game refuses both jobs, and the direct-battery paths, the liveness probe and
  the test-only override that allowed them went. The old SD worker's entry points threw at once with the reason until
  stage 6 deleted them.
- **4, the surface.** One node could not feed the session and follow it without a node-level cycle, so the surface is
  two nodes, one each way, and no feedback wire (section 5 and section 9's last answer). Every drained message is
  placed at frame 0 (the device's traffic is not timed against the block), the drain works in 16-message batches
  reused every block (the receive ring holds 255 usable slots, ~140 KiB, which a node need not pay for), and a message
  the `in` queue cannot hold is dropped and counted by the queue, never allocated around. Uniqueness is a descriptor
  flag (`"unique":true`) the planner checks, not a type name in the engine: a plan holds one node of a unique type,
  and the engine's own refusals stay for `rp.host.midi-in` / `-out`. `surface-in` discards what the link held when it
  bound it, so a press from before the plan is not replayed (a reactivate keeps the handle and does not discard again),
  which is also why the test host's harness link needs no per-run clearing: a press it holds against no surface node is
  dropped by the first block of the plan that places one, and one staged while the surface is bound is delivered.
- **5, the TS switch-over.** "Drive the real cart" is a NES role setting defaulting ON (additive, so a saved project
  keeps its behaviour and no migration exists) and places at most one `rp.n8.nes`, for the first NES in slot order that
  drives one, its id `cart-<system>` so the UI can derive it from the document; the fan-out wires what feeds that
  system's `midi` and `bytes`. Both surface nodes are placed only when the controller setting is on, and neither when
  it is off (the old document held the host controller pair unconditionally, the biggest single move in
  `build-graph.json`). Staging is node-addressed with no port argument (`backend.stageNodeMidi(nodeId, bytes)`,
  `rp.stage/1`, resolved at the drain against the plan that runs), because the two node types take different doors and
  the engine learns no type. The harness's controller seam became a `HarnessSurfaceLink` the test host installs: a
  harness-facet host with no surface link refuses `stageControllerIn` and controller traffic in `renderBlocks`. The
  cart menu's rows submit capability jobs against the driven cart's node and show the job's progress, result and
  refusal; the start menu's card rows are gone (a card job addresses a node, and a project with no system places
  none); a live Expansion Volume edit reaches both the system node and the cart node. The goldens moved with the
  documents (the role goldens, and `build-graph.json` rebuilt from its own cases), with the protocol digests unchanged.
- **6, the deletions.** The engine's host I/O service is `rp.engine.host-io/3`, its comment recording what left;
  `stageSystemMidi` went with `toCart`, since with TS staging at nodes nothing could call it (the brief asked for its
  cart half only, and its two behaviours live in `rp.stage/1`'s cases); the `DspCommand` kind is a hole at 13 and 15
  with nothing else moved, since kinds are never serialised. The package versions stay `1.0.0`: a version is a
  descriptor marker that never gates, and a document naming a removed type is refused `unknown-type` by the planner.
  The manual hardware harness (`n8-sd-hwtest`) was re-pointed through `submitCartJob` rather than deleted, so the
  battery and ROM operations can still be driven by hand through the path the node uses; its review fix connects the
  link before submitting, because a job is refused while the link is down and the first version never noticed.

### Section 9's questions, answered

- **How does a node reach a host-owned device across hosts, and what does a host with no device give it?** It binds
  `rp.host.devices/1` once in `bind_host` and acquires by name in `activate` [control], releasing in its destroy. The
  engine holds the pointers its host installed with `Engine::setDeviceLinks` before its first graph: the plugin passes
  its `N8CartLink` and no surface (its DAW MIDI seam cannot carry SysEx, so it still owns no Launchpad), the SDL
  standalone passes both, and the CLI, the Node addon, the render host and the UI harness install nothing. A name with
  no device answers the never-connected handle above, so the same document plans and runs everywhere.
- **Does pausing N8Link's streaming for a job need a change in the link, and what happens to bytes the graph pushes
  meanwhile?** Yes, and the pause is inside the serial thread (stage 3 above). A disconnect loses the last lookahead's
  chunks (note-offs among them), destroys the reliable sender's state, and lets another process take the port; the
  pause writes what the ring holds, waits for the acks, keeps the port and the sequence, and runs the job on its own
  Edio. Pushes during the pause are dropped and counted, and the node re-sends the note-offs it lost.
- **Where does the UI-staged MIDI enter, and does stage-to-a-node cover every caller?** The BlipToaster rig SysEx from
  the cart menu was the one production caller; it now stages at `sys-<id>` and at `cart-<id>` for the system that
  drives the cart, and the harness's system-addressed callers moved to `stageNodeMidi`. The device-facing calls in the
  SDL standalone and the harness's controller staging were not system-addressed staging at all and moved to the
  surface nodes and the harness link. So yes: every caller is covered, and the system-addressed RPC could go.
- **What does the UI's cart menu need from the capability API, and does the ROM load's lifecycle survive as a job?**
  Progress (`onProgress`), a busy state (every row disabled while a job runs, Connect included), the refusal reason
  verbatim in the row, and the result's own account of what happened: a restore reports the file it wrote and, when the
  ROM's folder had to be made for it, says so as a warning (a mistyped name is as likely as a game that never saved, so
  the row does not claim the bytes landed where the game reads), an upload reports the card path, the mapper index and
  "streaming: stopped", with `enabled` cleared. A battery job names the ROM this runtime (one a plugin instance) booted
  on the cart, else the driven system's ROM file: the booted ROM is the physical cart's state, not the node's, so it
  survives the drive moving to another NES, an editor close and reopen, and a project load (a fresh UI realm starts with
  none and falls back to the driven system's ROM file). Both battery jobs run only at the N8 menu, and a running game
  refuses them; a restore goes before the `rom.upload` that boots the game, whose rows say the game picks the battery
  file up at its next boot.
- **Does the session's message digest stay identical through the surface, block for block?** Yes, with the two nodes;
  the one-node version was refused as a cycle, and its only way through (a feedback wire) delivered the LEDs a block
  late. Through `surface-in` / `surface-out` the session reads the same input queues, and its per-block LED stream and
  every digest are the recorded ones; no digest was re-recorded for the rewiring (the role goldens' documents moved,
  their digests did not).

### What building it changed in sections 3 to 6

Each correction is in place; this is the list.

- **Section 3**: "reconnecting after a reset or an SD operation" is a reset, or a job that stops the link (an upload);
  the engine's losses gained `stageSystemMidi` itself, and the two devices bind through a typed acquire each.
- **Section 4**: the staging command addresses a node, not its input (`rp.stage/1` chooses the door); the node declares
  `unique` as well as `threads`; a removed node abandons its job at reclaim; a pause never leaves a note sounding, and
  neither does the node leaving the plan (its held notes go to the link at the plan's reclaim, which a host's idle tick
  makes within a frame of the swap, and the link writes their note-offs); the expansion volume is a standing value the
  link's serial thread applies, and the last node to release the link puts it back to AUTO, so a project that stops
  driving the cart leaves the register to the cart rather than at the level it last played at (a handover never reaches
  zero: the new node acquires before the old one is reclaimed).
- **Section 5**: one `rp.launchpad.surface` became `rp.launchpad.surface-in` / `-out` (the cycle), each unique, with
  the first-block discard.
- **Section 6**: `sram/1` on the cart reads and writes the SD card's save file at the menu, not the cart's memory (the
  DMA wedge); an emulated `sram.restore` is refused; `rom.header` / `rom.title` answer from what this host booted;
  `rom.upload` ends with streaming stopped; the CLI's battery dump is the one cart-memory read left, and the menu's
  battery jobs go through the SD card's save file.

### Found while building

The findings that changed a section are in it: the two surface nodes in section 5; the SD file's battery and the
upload's stopped streaming in section 6. The ones worth naming on their own:

- **One surface node is a cycle, and the session's digests are the cost of getting it wrong** (section 5). A surface
  that feeds a session and follows it cannot be one node; the only wire a cycle breaks on is a feedback wire, so the
  LEDs would have gone one block late. Two nodes cost the one-block latch nothing and no digest moved, which the
  protocol cases hold block for block.
- **A cart's battery cannot be read over USB on this hardware** (section 6). The direct cart-memory path this phase
  tried first froze a running game, and at the menu a read left the menu deaf to its commands until a power-cycle: the
  game's ROM is mid-DMA and nothing reinitialises the cart's file layer. Chunking did not help (16 x 4 KB reads took
  16 s and wedged it too), so the SD card's own save file is the only path, and it exists only at the menu: a dump
  takes about a minute.
- **`postControl` was not needed, and neither is any call from the block into the link's thread** (section 4). The
  expansion volume is a value the host holds with a generation counter: the block only clamps, stores and bumps, and
  the serial thread applies the standing value on its next turn, so a failed write retries and no lock or
  `std::function` is on the audio path. A write that keeps failing is retried once a second rather than every turn, so
  a wedged-but-open port cannot take the stream's turns for a config register; the menu probe is behind no such wait,
  since a submission asks the link for a quiet turn of its own (`N8Link::wakeControl`, a wake that stands through busy
  turns and is spent by the first quiet one, so a busy turn's failed write cannot re-arm the wait in front of it), with
  its own 1 s deadline as the bound.
- **A project with no NES no longer forwards DAW MIDI to the cart** (section 4). The fallback existed for "no emulated
  NES to mirror"; the cart is placed by a setting now, a raw copy beside the fan-out would arrive twice, and a user who
  drove a MIDI cart with no NES system in the project (a MENU or debug pattern) loses that. Restoring it would be a rule
  of the same shape as the one this phase removed, so it is noted rather than invented.
- **The harness's controller seam only works where a host installs a surface link** (section 6, stage 5).
  `stageControllerIn` and `renderBlocks`' controller field now queue into `HarnessSurfaceLink`, which only the test host
  installs, and a harness-facet host without one refuses them rather than staging into nothing: a consumer suite built
  on the old seam is a trap.
- **`unique` is planner-wide** (section 5). A document with two surface-in, surface-out or cart nodes is refused with
  code `unique` at plan time in every host, and a TS node type can declare it too.
- **The engine's host I/O service moved to `/3`** (stage 6). A node built against `/2` reads a struct three members past
  its end; every node is in tree, so nothing serialises the id and no shim was written.
- **A package's version never gates** (stage 6). `rp.n8` keeps `1.0.0` after losing `rp.n8.cart-mirror`, and
  `rp.host.io` keeps `1.0.0` after losing two node types: the planner refuses a removed type as `unknown-type` at any
  version.
- **A job that will be refused must not pause the stream** (sections 4 and 6). The three transfers run at the menu only,
  so for a running game every pause was spent delivering the drain, waiting out the ack bound and then refusing, while
  the block's note-ons during it were dropped (note-offs are owed and re-sent, note-ons are not). The config block read
  needs no pause at all, and reading it at submission is what makes the refusal free; the volume's own config writes
  already ran there, between chunks.
- **A config read on a live reliable link waits for a quiet turn** (sections 4 and 6). Moving the probe out of the pause
  took away the two things that kept it clear of a playing ROM's acks (the drain that waited for them, and the flush
  before a job's own reply), and a game whose link is up owes an ack for every frame it has not answered: one arriving
  inside the read is taken for the reply's first bytes, and the reply's tail is then parsed as link traffic, which can
  mark a frame delivered that was not. The serial thread services the link before a control turn and tells the read
  whether the turn is quiet: nothing outstanding, or a cart that has said nothing for the pause's 250 ms ack bound
  since the oldest outstanding frame first went out. The second half is not optional: the sender never gives a frame
  up by time, so a cart that stops acknowledging with a frame outstanding (one a console reset returned to its menu
  with USB still up, which acknowledges nothing) would otherwise never give the read a turn, and every job would be
  refused until a reconnect; a cart that silent is the risk the pause's drain already takes. A probe whose second
  passes on a link that kept acknowledging is refused for that, not as a block that did not answer. The volume's Auto
  read has the same exposure and takes the same guard. One residue is left: an ack for no outstanding frame, which the
  ROM writes for each copy of a frame a lossy link made the host send twice, the second after the first has emptied
  the window. It costs a misread (an Auto that sees no expansion mapper writes nothing, a probe that sees no menu
  refuses) and the reply's last bytes parsed as an ack only if they spell one; closing it would need a bound on when
  the ROM writes that ack, which follows its FIFO running empty, so it is described here rather than guessed.
- **The hardware smoke, on the real NES and N8 Pro.** Recorded on the prototype, before the probe moved to submission:
  then a job paused a playing ROM, which a submission now refuses instead of pausing for. A pause over a playing ROM
  kept the reliable link continuous (resends 0, outstanding 0, no re-sync) and the note before and after it sounded (A4
  at 440.30 Hz, E5 at 657.66 Hz, silent after the note-off); a battery dump, restore and dump round-tripped byte for
  byte through the card's file; a 66,560-byte save restored through the job loaded at the game's next boot, its song
  table read off the screen against the save; and a ROM uploaded through the job booted in 2.8 s with the header
  matching the file. The rig was left at a fresh menu with every scratch file deleted. The probe's quiet-turn read has
  not been run on a cart.
- **A cart node leaving the plan left its notes sounding** (section 4, the final review). The node paid owed note-offs
  after a pause, but its destructor only abandoned and released, and with the no-NES raw fallback gone nothing else
  could carry the DAW's eventual note-off to the cart: a note held when the setting went off, the NES was removed or
  replaced, the drive moved or a project was loaded or closed mid-note sounded on the console until a power-cycle. The
  node now hands its `sounding` books to the link, which keeps them under the job mutex beside the push count they wait
  behind (the audio thread counts each chunk it puts in the ring, the serial thread each one it writes), so a note-on
  still waiting for its release time cannot follow its own note-off. The serial thread writes them on its next streaming
  turn once that count is reached, framed when the link is reliable; a job's pause holds them until streaming resumes, a
  disconnect writes them before the port closes (so a plugin closed while the cart plays leaves nothing held) without
  waiting for the ring it is throwing away, and a link with no serial thread, or one whose stream stopped by itself (a
  failed write, an upload's boot), drops them.
- **A plan a running host replaced waited for the next publish** (section 4, the review of the held-notes fix). A cart
  node's note release, its handle's, its job's abandon and every pin of a replaced plan happen when that plan is
  reclaimed, and with an audio thread running blocks `GraphHost` reclaimed only at `setGraph`, `loadScriptPack`, a
  deactivate or a stop: an idle reclaim was owed by the hosts since Phase A (the main design's section 8). So turning
  "Drive Real Cart" off left the note sounding until the user's next graph edit, and a handover's cut reached every key
  struck until then. The plugin's editor idle and the SDL standalone's frame loop now call
  `QueuedInvoker::reclaimReleased` on the thread `setGraph` runs on, which frees each plan the audio thread has handed
  back: only plans it has moved past, as the release ring has always required. With nothing handed back the call pops
  an empty ring and reads a few counters, allocating nothing and taking no lock the audio thread takes (an `[n8-node]`
  case counts it with blocks running). Two places still have no tick while audio runs: a plugin whose editor is closed
  (DPF offers a plugin no idle; its only graph edit then is a DAW's `setState`, whose replaced plan waits for the
  editor to open, the next publish or a deactivate) and the harness hosts (the test host, the CLI, the Node addon, the
  render host), whose background audio thread is a test driver and whose session reclaims through `drainReleased` or
  `stopAudio`. The UI harness never runs an audio thread, so every push there reclaims inline.
- **The surface link's receive ring rode a host's stack** (stage 1). 256 slots of one 562-byte message is 140 KiB, and
  the link is a member of the SDL standalone's `AppState`, which is a local of `main`: Windows gives that thread 1 MiB
  of stack, and every suite in this tree runs where it is 8, so nothing here would have caught it. The ring is one
  allocation made when the link is built, and the drain still copies out of fixed slots and frees nothing. The same
  pass moved the standalone's plan teardown ahead of the surface's farewell, so a surface node's destroy reaches a
  link that is still open, as the plugin's already did (section 3).
- **A detached Launchpad's paint inflated the status line's fault count** (stage 4). `LaunchpadLink` keeps three counts
  apart: `messagesDropped` is a push it lost while the device was attached (a full send ring, an oversized message),
  `messagesDetached` is the paint a session pushed while nothing was connected, and `messagesInboundDropped` is a
  message the device sent that the receive ring had no room for, since that is a press the audio side missed, not an
  LED frame the device never got. The handle a node holds answers the first two; the status line's `dropped` is the
  first and it names the detached traffic as `detached`, so one unplugged cable does not mark the device as faulting
  for the rest of the run. The inbound loss is counted and shown nowhere.

### The suite, as built

- `retroplug-engine-graph-test` gained `[devices]` (`Devices.test.cpp`: both links over fake devices, a host with no
  device and the state its handles answer (both `connected` false, the volume through the fallback), the expansion
  volume through the interface, acquire and release across a reactivate and a teardown, a handover to a new node that
  never leaves the link with no holder, a warmed block allocating nothing), `[capability]` (`Capabilities.test.cpp`:
  each emulated console against the system-id RPCs and the ROM file, the ROM title's printable-ASCII stop, a node that
  presents no capability, a battery dump holding what `readSram` answers, block-boundary completion (or in the call
  while no audio thread runs), removal and discard, the 64 bound, a real device thread's progress, a warmed block with
  writes in flight, and the audio-thread ownership bit held by one guard per case so a failed assertion cannot leave
  the engine believing a thread still runs it), `[n8-node]` (`N8Node.test.cpp`: the fan-out, `connected`, the expansion
  volume, owed note-offs, the held notes a removal, a teardown and a handover hand the link, an idle reclaim freeing a
  replaced plan with blocks running and no further publish (and allocating nothing when nothing came back), the jobs
  and their refusals, abandonment, `threads`, `unique`, a warmed block), `[n8-adapters]` (`N8Adapters.test.cpp`: the
  one case that composes the production adapters behind a real Engine, `rp.n8.nes` through `N8CartLink`, `N8Host` and
  `N8Link` to `test/n8/FakeN8.hpp`'s serial port: a note, then in one section `rom.title` before and after an upload, a
  dump refused at a game and a dump abandoned by a replan with the held note's note-off written once streaming resumes,
  and in another the hosts' teardown order with a note held, its note-off written, ending in `~N8Host`'s join; here
  rather than in `retroplug-n8-test`, which links no engine, and run by `pnpm test:plugin`, which CI runs and
  `pnpm test:n8` is not), `[launchpad]` (`Surface.test.cpp`: the drain, the output, `connected`, the queue's bound, the
  first-block discard, uniqueness, `threads`, acquire balance, a warmed block) and `[stage]` (`StageNode.test.cpp`: a
  staged message reaching the node the running plan places under the id, in both orders relative to the swap that
  changes which plan runs, and one added later). The protocol cases run the surface wiring and hold their recorded
  digests; the goldens' documents moved with the fan-out and the surface nodes, and their driven runs hold each cart
  node's pushes to what the system it drives had fed its own FIFO, byte for byte, one cart per document as the compare
  requires.
- `retroplug-n8-test` gained `[cart-jobs]` (`N8CartJobs.test.cpp`: the pause and its ack bound, battery file dumps and
  restores with their refusals, the upload and its `Stop` (which frees the port even when its trailing flush throws), a
  game's refusals at submission (which drop no push), the probe waiting out an ack the link still owes, a cart reset to
  its menu with a frame its game never acknowledged still taking a job, a link that never goes quiet refused for that, a
  probe's wake outlasting a busy turn, a port that throws as a job starts and after it ran, a job whose `run` throws run
  once, the JSON a name reaches, an abandoned dump) and `[cart-release]` (a leaving node's held notes: behind a note-on
  still waiting for its release time, framed on the reliable link, held through a job's pause, written by a disconnect,
  dropped by a link that is down), over the cart model both suites share (`test/n8/FakeN8.hpp`), and the two link
  adapters have their own files: `N8CartLink.test.cpp` (the volume's retry, its once-a-second bound, the probe's turn of
  its own, the last release's AUTO) and `LaunchpadSurfaceLink.test.cpp` (the ring's inbound losses counted apart from
  `dropped`).
- TS: `test/capability/capability.test.ts` (the promises, the refusals, a close rejecting pending, progress, the full
  title, a node that presents no capability, a battery dump that is not a battery read, the wait's bound, and a queue
  leaving the pump's set), and on the real host `test-native/capability.test.ts` (two backends over one engine settling
  each other's completions) and `test-native/stage-node-midi.test.ts`; `test/graph/build-graph.test.ts` holds the
  placement and the fan-out, `test/systems/store-graph.test.ts` the setting and the live expansion copy, and
  `test/menu/n8-sd-ops.test.ts` the cart menu over the mock's capability model (the restore's result and its created
  warning, the booted ROM, per runtime, across a handover, and a picked file that cannot be read).

## 11. Left as it is

- **A cart's battery is reachable only at the N8 menu**, through the SD card's save file (`EDN8/gamedata/<rom>/
  bram.srm`): a dump takes about a minute for 64 KB, a running game refuses the job (a direct cart-memory read wedges it
  and leaves the menu deaf), and a restore goes before a `rom.upload` so the game loads it at its next boot.
- **A cart-to-graph output** (the cart's replies feeding a block): nothing consumes one; the reliable link handles its
  own back-channel.
- **A "real cart only" system** with no emulated node.
- **The other capability interfaces** (video, buttons, debug) and C4's P3 (the ROM borrow) and P4 (the snapshot seam).
- **MIDI streamed to a cart at its menu reaches the menu's own command parser** (pre-C5, unverified on hardware). The
  menu takes its commands over the same FIFO (`'*'` then a byte), so a MIDI data pair such as `0x2A 0x73` (`*s`) sent
  while the menu shows can ask it to start an app. It holds for raw MIDI after a connect at the menu and for framed
  MIDI after a console reset, where the stale window is re-sent every 100 ms until it re-syncs.
- **The quiet rule assumes the N8 menu writes nothing unprompted on the back-channel** (unverified on a cart).
  Unsolicited bytes would keep `lastHeardNs` fresh and make a menu with stale frames read as never quiet, refusing
  every submission.
- **A handover can cut a key the new node strikes before the old one is reclaimed.** The old node's release waits behind
  every byte pushed when its destroy runs, the new node's first blocks among them, so a note the new node starts between
  the swap and the reclaim, on a key the old node still held, is released with the old one's (the new node's books still
  hold it, and the DAW's next note-on sounds it again). With an idle tick (the plugin with its editor open, the SDL
  standalone) that window is the frame or so before the next tick; without one (a plugin whose editor is closed) it runs
  to the next graph publish, the editor opening or a deactivate. Waiting behind only the old node's own pushes would need
  the ring to say whose bytes are whose.
- **A plugin whose editor is closed reclaims only at a publish, a deactivate or its editor's next idle.** DPF gives a
  plugin no idle callback of its own, so a plan a DAW's `setState` replaced while the transport plays keeps its nodes,
  a cart node's held notes and the pins they hold until then. The harness hosts likewise reclaim only when their session
  asks (`drainReleased`, `stopAudio`), which their tests do.
- **A heavy stream into a menu can starve the probe.** The window's 48-frame re-sync clears and re-stamps the oldest
  frame's first send, so a probe needs a quiet turn between re-syncs; past about 190 messages a second (48 per
  250 ms) into a menu there is none, and the submission is refused for a link that never went quiet.
