# Phase C5: hardware as nodes

**Date:** 2026-10-31 · **Branch:** `feature/modularize` · **Status:** designed; the prototype answers section 9

RetroPlug drives two pieces of real hardware: an Everdrive N8 Pro cartridge in a real NES, over USB serial, and a
Novation Launchpad as a control surface. Both reach the graph through special cases in the engine and the hosts: the
cart mirrors one emulated NES through a plan-level field and a sink function the engine calls, a UI message is mirrored
to the cart by a flag on a staging command, and the Launchpad's MIDI rides two engine staging fields, a command kind
and the controller halves of two host I/O nodes. This phase makes each device a node: `rp.n8.nes` takes the same inputs
as an emulated NES, so emulated, real or both is plain fan-out wiring; `rp.launchpad.surface` has event ports to and
from the device; and the operations both an emulated core and a real cart can answer (memory, battery, ROM) become
capability interfaces with an asynchronous job model, so the N8's seconds-long USB transfers and an emulated core's
block-boundary writes share one API. After C5 the engine names no console and no device. It is Phase C's last piece
([modular-graph-design.md](modular-graph-design.md) sections 6.4, 6.6 and 8), after C4
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
an SD operation, and one port per process all stay in `N8Host` / `N8Link` and the Launchpad's host / controller
transport. **Nodes bind to a device through a host interface, `rp.host.devices/1`**: a node asks for a device link by
name (`"n8"`, `"launchpad"`) and gets a handle, or a handle that is never connected in an instance whose host does not
own the device. A handle offers the device's streaming rings, its connected state and its capability job runner.

**Placement is by setting, not device state**: a system's "drive the real cart" setting places `rp.n8.nes`, the
controller setting places `rp.launchpad.surface`. Both always activate; a `connected` control output says whether the
device is attached, and pushes go nowhere while it is not. A plug, unplug, reset or SD operation never edits the graph.

**The engine loses** the cart mirror (`coreByteSink_`, `SystemTable::cartMirror`, `cartMirrorSystemId`, its NES check,
the `rp.n8.cart-mirror` type), `stageSystemMidi`'s `toCart`, the four controller fields and their command kind, the
controller halves of `rp.host.controller-in` / `-out`, and the hosts' raw-MIDI fallbacks.

## 4. `rp.n8.nes`

**Ports**: the same `midi` and `bytes` inputs as `rp.mesen.nes`; in its block it pushes each message onto the link's
ring with its frame offset, so N8Link's timed-release scheduler is unchanged. A `connected` control output, no audio
output, and an `expansionVolume` param like the emulated NES's, sent to the cart's master-volume register through the
link's control path on a change and again on every reconnect. It declares `threads`.

**Wiring is fan-out**: `buildGraph` wires the protocol node outputs that feed a system's `rp.mesen.nes` into its
`rp.n8.nes` too. The emulated node always stays (a "real cart only" system is later work).

**UI-staged MIDI**: the staging command addresses a node's input instead of a system; TS stages to the system node, and
to its cart node when one is placed, so a UI message still reaches the real cart and the engine stays neutral.

**Lifetime**: disconnected, pushes go nowhere without allocating and `connected` reads false; reconnecting is the link's
job on its own thread; removing the node releases its handle at reclaim, on the control thread. Each plugin instance
keeps its own `N8Host` and the port stays exclusive, so with two instances the first to connect has the cart and the
other's handle stays disconnected with the open error in its `lastError`, as today.

## 5. `rp.launchpad.surface`

**Ports**: `in`, an events output (the device's MIDI drained from the link's receive ring at the start of the node's
block, each message at its frame); `out`, an events input written to the link's send ring; `connected`, a control
output.

**Wiring**: the controller session script node reads the surface's `in` and `connected` and sends its LED and page
output to the surface's `out`; its handshake (Programmer mode and a repaint on the block the link first reports
connected) is unchanged. `buildGraph` places the surface whenever the controller setting is on, beside the session.

**It replaces** the engine's controller staging, output buffer, connected flag and command kind, the controller halves of
the two host I/O nodes, and the SDL standalone's per-block controller staging and drain. The host keeps scanning,
pairing and the farewell; the config, pages and MI.MAP override stay TypeScript's.

## 6. Capabilities as asynchronous jobs

**The interfaces** (P7's split, restricted to what C5 needs; video, buttons and debug stay as they are):
`rp.system.memory/1` (named regions, read and write), `rp.system.sram/1` (dump, restore), `rp.system.rom/1` (header and
title; upload-and-boot on the N8 node only, an emulated system's new ROM staying C3's rebuild). A system node presents
each through a nullable accessor on `SystemBase` (the `channelTap()` / `debugTarget()` pattern); `rp.n8.nes` presents
`sram/1` and `rom/1` (the main design's section 6.6), not `memory/1`, whose cart reads are a DMA that can wedge a running
game and stay with the CLI's tools.

**The job model**: the control thread submits a job (node id, capability, operation, arguments) through one engine RPC
and gets a job id; completions drain from a queue TS polls. On an emulated core a read is served from the snapshot
registry and completes at once, and a write goes through the command ring and completes at the next block boundary,
as `readSram` / `writeRam` do today (those system-id RPCs stay as thin wrappers). On the N8 node a job runs on the
link's worker thread: streaming pauses, `connected` reads false for the duration and pushes are dropped and counted,
the transfer runs, streaming resumes, the completion is posted. One job at a time per device (a second is refused
busy); a job while disconnected fails "not connected".

**TS**: `backend.capability(nodeId, op, args)`, a promise over submit and poll, the same for emulated and real. The UI's
cart menu (back up and restore the cart's SRAM, load a ROM to the cart) moves onto it, and the `__rp_n8LoadRom` /
`__rp_n8DumpSram` / `__rp_n8RestoreSram` / `__rp_getN8SdStatus` hooks and `N8Host`'s SD worker go, the SD and menu
commands those operations use running as the node's jobs; `__rp_setN8ExpVol` goes too, the node's param replacing it.
The CLI's n8 tools keep their raw serial path: they run with no engine and no `N8Host`, and need the raw link (peek,
poke, the sniffer, FIFO reads) that the capabilities deliberately do not offer.

## 7. Tests

- **Parity** holds bit for bit at every commit, nothing re-recorded: the emulated audio path does not change.
- **`retroplug-engine-graph-test`** with fake device links: the same bytes reach the emulated NES and the cart node, in
  order and at their frames; a missing device faults nothing; `connected` flips; a job pauses and resumes streaming;
  busy and not-connected refusals; a warmed block with a placed cart node and a surface allocates nothing; the
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
4. **`rp.launchpad.surface`.**
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

- How does a node reach a host-owned device across hosts (the plugin, the SDL standalone, the CLI, the Node addon, the
  render host, the UI harness), and what does a host with no device give it?
- Does pausing N8Link's streaming for a job need a change in the link (today an SD operation disconnects it), and what
  happens to bytes the graph pushes meanwhile?
- Where does today's UI-staged MIDI enter (who calls `stageSystemMidi` and with what), and does stage-to-a-node cover
  every caller?
- What does the UI's cart menu need from the capability API (progress, the busy state it shows today), and does the
  ROM load's "reconnect off, enabled cleared" lifecycle survive as a job?
- Does the session's message digest stay identical through the surface, block for block?

## 10. Left as it is

- **A cart-to-graph output** (the cart's replies feeding a block): nothing consumes one; the reliable link handles its
  own back-channel.
- **A "real cart only" system** with no emulated node.
- **The other capability interfaces** (video, buttons, debug) and C4's P3 (the ROM borrow) and P4 (the snapshot seam).
