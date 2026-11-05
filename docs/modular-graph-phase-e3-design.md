# Phase E3: exported plans and the box as a graph host

**Date:** 2026-11-05 · **Branch:** `feature/modularize` · **Status:** designed (on 44cd7b178)

Phase E is the Pico runtime ([modular-graph-phase-e1-design.md](modular-graph-phase-e1-design.md) section 3). E1 made
a C++ node's source build for the RP2350; E2 put the first pack behind the package boundary and ran the box on the
pack's Edio. E3 makes a graph run on the box: the desktop plans a document for the RP2350 and exports it as a
versioned binary blob, the firmware loads the blob into a static arena with the same executor the desktop runs, and
the box's bridge becomes a plan (MIDI in, a filter, `rp.n8.nes`) in place of hand-written forwarding. The box must
still do everything the baseline records ([modular-graph-phase-e2-design.md](modular-graph-phase-e2-design.md)
sections 8 and 13.7 give the figures), and nothing in E3 may assume a silent box: the Pico will process audio in a
later version.

## 1. Before E3

- **The executor is portable; a planned graph is not.** `runBlock(const PlanView&, frames, transport)`
  (`packages/graph-runtime/native/src/graph/Executor.cpp`) needs no heap, and a `PlanView` is spans of steps,
  queues to clear, control streams to roll and feedback copies. What a plan holds after planning (`PlanStorage`) is a
  web of `std::vector`, `unique_ptr` and `shared_ptr` joined by raw pointers, desktop-only. No export, blob format or
  loader exists; `Target::Pico` is read in one place, the planner's refusal of a `heap` or `threads` type.
- **Planning instantiates nodes.** The planner creates every instance with JSON settings and the desktop's
  `query_memory`, whatever the target; a device instance's size differs (its settings held inline, 4-byte pointers),
  so a device must size its own nodes.
- **`rp-device-check` already runs the executor on the RP2350**, one node type at a time in a 400 KiB bump arena
  sized by `rp.core.delay`'s default (a one-second stereo ring, 384,008 bytes), from cases a desktop tool generates at
  build time. CI builds it and never runs it.
- **The host's MIDI input is desktop-shaped.** `rp.host.midi-in` (`rp.host.io`, `device: false`) reads
  `rp.engine.host-io/3` into a queue of 2048 messages and 64 KB, 98 KB in all; `rp.host.audio-out` has a `std::vector`
  setting.
- **The box forwards by hand.** `pico/n8-host` registers `rp.n8.nes` and plans nothing: its main loop parses UART
  MIDI, drops aftertouch, drops everything while no game is ready, consumes its own `F0 7D 50` commands and batches up
  to 192 bytes into one FIFO write per pass (about 0.6 ms from the last UART byte to the N8's acknowledgement, 1.5 ms
  budget). Its expansion-volume check writes the cart's register only for an expansion mapper whose register reads 0.

## 2. Decisions taken

The user's, in order (2026-11-05; the Phase E list is `.superpowers/e-pico/decisions.md`):

1. **Plans are built in only.** The default plan is exported by a desktop tool when the firmware is built and
   embedded in it; another graph means reflashing. No plan file on the SD card and no live upload in E3.
2. **Block cadence: one block per main-loop pass, sized by elapsed time, behind a seam.** Today's latency holds, and
   a fixed cadence (a timer, or an audio DMA when the box makes sound) replaces the driver later without touching the
   loader or a node. The Pico will process audio later, so the blob states its sample rate and `maxFrames`, the arena
   sizes real audio buffers, and the cart link only queues.
3. **Memory: one meaning everywhere; the Pico refuses what does not fit.** Defaults are the same on every target. The
   box's loader sizes a plan against its arena and refuses, naming the node and the overrun, before creating
   anything; the exporter runs the same sizing so a built-in plan that cannot fit fails the firmware build.
   `rp.core.delay`'s one-second default is refused on the Pico; a box plan sets `maxSamples`.
4. **A generic host, cart policy in the plan.** The firmware parses raw MIDI and filters nothing (it still consumes
   the box's own commands); the box's cart link owns the link's state (no game running: not connected, a push drops
   and counts) and its batching; what one cart wants lives in the plan: BlipToaster's drops aftertouch through a new
   portable `rp.core.midi-filter`. `rp.n8.nes` stays cart-agnostic.
5. **The box link applies the node's expansion volume as the desktop's N8Host does**, and the box's own mapper-based
   volume check retires.

## 3. The shape

A JSON document for the box goes through the desktop planner and the exporter into a versioned blob, the firmware
embeds it, and at boot the loader lays it out in a static arena and hands the executor a `PlanView`; each main-loop
pass runs one block.

| Piece | Where | Built for |
|---|---|---|
| The blob format, `exportPlan` | `packages/graph-runtime` | desktop |
| The loader | `packages/graph-runtime` | desktop and device |
| `retroplug-export-plan` (the build-time tool) | `packages/graph-runtime` | desktop |
| `rp.core.midi-filter` | `packages/core` | desktop and device |
| `rp.device.midi-in` | `packages/device-io` (new, `device: true`) | desktop (export, tests) and device |
| The generic box host | `pico/host/` (new library) | device |
| The N8 box: flows, cart link, `plan.json` | `pico/n8-host/` | device |

## 4. The blob, the exporter and the loader

**The blob.** Little-endian, 4-byte aligned, no pointers: indices and offsets only.

- A header: the magic `RPPL`, a format version, the target (`rp2350`), the plan's sample rate and `maxFrames`, the
  arena budget it was exported against, and a section table.
- A string table; the node table (type id, msgpack settings, coupled group, schedule position); the pools (audio
  buffers of channels x `maxFrames`, queues with their message and byte capacity, control streams of 64 changes, the
  shared silence, scratch, empty queue and empty stream); each step's input, output and param view indices; the
  feedback copies, the queues to clear and the streams to roll; the initial port masks, param starts and peer lists;
  a CRC-32 at the end.
- Not carried: instance sizes (the device computes its own), resources (E3's box has no resource table, so a plan
  that names one is refused at export, "resources are not exported to rp2350 yet"), and settings commits (every device
  instance starts fresh).

**The exporter** (`exportPlan(document, registry, options{target, arenaBytes, sampleRate, maxFrames})`, desktop):

- It plans the document with the planner for `Target::Pico`, so every rule the desktop enforces applies (wiring,
  layouts, the `heap` and `threads` refusals, `unique`). Planning instantiates desktop nodes, so the exporter gives
  them a stub host: `rp.host.devices/2` answering null, `rp.device.midi-in/1` with nothing to give.
- It writes each node's settings from its parsed settings (`rp.sdk.settings-json/1`, then `settingsToMsgpack`), never
  from the document's JSON, so the device's reader agrees by construction for an integer past INT64_MAX in a signed
  field and for `-0` ([E1 design](modular-graph-phase-e1-design.md) section 10).
- It sizes the plan with the host-built device-mode objects. Their instances are at least as large on x86-64 as on
  the 32-bit M33 (8-byte pointers, a 16-byte `max_align_t`), so the estimate errs on the safe side; over the arena
  budget it refuses, naming the node that tips it over and by how much.
- `retroplug-export-plan` is a desktop tool, not a user command: the box's build runs it over `pico/n8-host/plan.json`
  and embeds the bytes as a generated header, as `rp-device-check` consumes its generated cases today.
  `tools/build-box.sh` and CI's `device:box` step build it first.

**The loader** (`loadPlan(blob, arena, registered types, rp_host*, error*)` and `unloadPlan`, portable):

1. Checks the magic, the version (another is refused as "plan format N, this loader reads M"), the CRC, every index
   in bounds, and every type id present in the image's generated registration (an unknown one is refused by name).
2. Sizes the plan, creating nothing: each node's own `query_memory` on its msgpack settings, plus every pool, view
   array, step and counter. Over the arena it refuses with the node and the overrun. This check is the authority; the
   exporter's is the early warning.
3. Lays the plan out in the arena and runs it up in `PlanStorage::adopt`'s order: create and activate each node, bind
   the views, set the peers, call `ports_changed`, write the param starts. A failure part-way unwinds what it made, in
   reverse, and reports the node and the step.
4. Hands back the `PlanView`; `unloadPlan` deactivates and destroys in reverse.

The per-node fault flags and drop counters live in the arena. Nothing allocates and nothing throws. Coupled groups and
feedback load like everything else, so a plan with audio loads the same way later.

## 5. The box as a graph host

**The code splits** into `pico/host/`, a library with no N8 in it (the device `rp_host` and its interfaces, the
arena and the built-in plan's load, the block driver, the UART MIDI path: the IRQ ring with timestamps, the parser and
staging), and `pico/n8-host/`, the product (the N8 flows: probe, boot through the menu, health, recovery, the ROM
table, the SysEx ROM pick; the box cart link; `plan.json`; `main.cpp`). A later box product links the same library.

**One main-loop pass:**

1. Feed the watchdog, pump USB.
2. Parse UART bytes. The box's own `F0 7D 50` commands are consumed here; every other message is staged with the
   frame its timestamp falls on.
3. Ask the block driver for the next block; `runBlock`.
4. Drain the cart link's queue to the N8, batched.
5. The N8 part's probe, boot and health.
6. Stats.

Block work and N8 work never overlap, which is what keeps `push` from ever waiting on the N8, now and when an audio
driver sets the cadence.

**The block-driver seam** is one call, `bool next(uint32_t* frames, rp_transport*)`. E3's driver answers the time
since the previous block at the plan's sample rate, carrying a remainder past `maxFrames` into the next block. The
built-in plan runs at 48 kHz with `maxFrames` 256 (5.3 ms), so a long N8 wait becomes a few blocks. An audio driver
later answers with its fixed block when its buffer is ready.

**`rp.device.midi-in`** (`packages/device-io`) reads the host's `rp.device.midi-in/1` interface, which drains the
staged messages into its queue: 256 messages and 4 KB, the UART ring's size. Desktop products do not list the package,
so their `[packages]` rows do not change; the export tool and the device builds do.

**The box cart link** (`rp::n8::CartLink`):

- `push` copies into a fixed byte ring and answers false, counted, when the ring is full or no game is running;
  `connected()` is "a game is running and the box forwards", today's not-ready rule as the link's state.
- After the block the main loop drains the ring in FIFO writes of up to 192 bytes, today's batching.
- `releaseNotes` queues the note-offs the same way.
- `setExpansionVolume` records the value; the main loop writes it to the cart's config register while connected, as
  N8Host does on the desktop. The box's mapper-based check retires: at connect the register gets the plan's value
  (unity, 128, unless `plan.json` sets the param), where the box used to leave an existing non-zero value.
- The box offers no `rp.host.jobs/1`, so `rp.n8.nes` refuses jobs ("this host takes no device jobs").

**Stats and logs.** Every log line the gate's scripts match stays, and the `[stats]` fields keep their names and
meanings; counts that now happen inside the plan are taken from its edges (`filtered`: messages staged minus messages
the link received; `notready`: the link's drops while not connected). A node that faults is silenced by the executor
and the host prints it once. A built-in plan the loader refuses (which the build's check rules out) prints why, shows
the error LED and forwards nothing.

**`plan.json`:** `rp.device.midi-in` -> `rp.core.midi-filter` (dropping `polyAftertouch` and `channelAftertouch`) ->
`rp.n8.nes`.

## 6. `rp.core.midi-filter`

One events input and one events output, whose capacity follows the input's. One setting, `drop`, an `rp::list` of
`rp::choice` message kinds: `noteOff`, `noteOn`, `polyAftertouch`, `controlChange`, `programChange`,
`channelAftertouch`, `pitchBend`, `sysex`, `systemCommon`, `realtime`; an empty list passes everything. Every other
message passes unchanged, its frame, flags and order kept. No channel filter. It is a desktop node as much as a device
one: every desktop product lists `core`, so the `[packages]` golden gains it, and it has its own descriptor golden.

## 7. Inherited items

- **The box image:** `PICO_HEAP_SIZE=0` (no `.heap` reservation). The arena is a static in `.bss`, sized at build
  time for the box's plan with room for a later plan's audio buffers. The 87-symbol trap holds.
- **The stack:** `rp-device-check` gets `PICO_STACK_SIZE=0x1000`. `-Wstack-usage` with a limit stated in
  `cmake/device-image.cmake` goes on every `device: true` package's device-mode target and on both images, so a frame
  past the budget fails the build: that is the measure of the device's stack in `query_memory` and `create` E1 asked
  for, in place of a hand sum.
- **`UsbCdcTransport`:** a desktop test over a fake TinyUSB for its remount, unmount, drain and flush-silence rules.
- **`rp-device-check` on the loader:** the image also exports, at build time, a multi-node core and DSP plan with a
  coupled group, a feedback wire and params, loads it with the loader in its arena and runs blocks, beside its per-type
  checks.
- **Left for the audio version:** the codec and its DMA driver, the fixed-cadence block driver, the float tolerance
  between the M33 and the desktop (and `-ffp-contract`).

## 8. Tests

- **`retroplug-device-mode-test`:** a core and DSP document with a coupled group, a feedback wire and moving params,
  exported, loaded by the loader over the device-mode builds in an arena and run in ragged blocks, renders bit for bit
  what GraphHost renders from it with the desktop builds, and the counting allocator sees nothing from load to unload;
  every loader refusal with its message (a version, the CRC, a truncated blob, an index out of range, an unknown type,
  over budget, a failure part-way through that unwinds); the midi-filter's device build against its desktop build;
  the box host's portable pieces built for the host (the block driver's arithmetic and carry, staging into
  `rp.device.midi-in` with in-block frames, the cart link's ring, drops, batching and note-offs over a fake transport).
- **`retroplug-graph-test`:** the midi-filter (each kind dropped, order and frames kept, an empty list passing
  everything), its descriptor golden, nothing allocated once warmed.
- **The export tool:** its refusals (a `heap` or `threads` type, a resource, a plan over budget), each with its
  message; the built-in `plan.json`'s blob pinned by a golden of its bytes, so a change to the format or to a node's
  settings is a reviewed diff.
- **The device builds:** `device:check` and `device:box` link under the trap with `-Wstack-usage` on; the `env -i` CI
  dry run.
- **Held:** parity, the kit goldens, the render baseline and every buildGraph golden; the `[packages]` golden (the
  midi-filter) and the new descriptor and blob goldens are the intended changes.

## 9. Landing

Five commits, each leaving every suite green:

1. **The blob, `exportPlan` and the loader**, proven on the desktop.
2. **`rp.core.midi-filter` and `rp.device.midi-in`** (`packages/device-io`).
3. **The box as a graph host:** `pico/host/`, the block driver, the cart link, the built-in plan exported at build
   time, the main-loop split, `PICO_HEAP_SIZE=0`, `-Wstack-usage`, the `UsbCdcTransport` fake.
4. **`rp-device-check` on the loader** and its 4 KB stack.
5. **Docs.**

**Acceptance:** every suite with and without the fixtures, parity strict, the kit and render goldens under their
require flags, the sanitizers, `device:check`, `device:box` and the CI dry run, a fresh tree, the live hosts, the
consumers. Then the hardware, by the controller, BlipToaster for every leg (a flash without `--rom` erases the ROM
table and boots `n8-midi.nes`, which never counts): the Pico gate as E2 ran it (`loop.sh --rom`, `stress.sh` within
the budget, A4 and C5 on channel 3, `faults.sh --rounds 8`, the two-ROM upload and SysEx pick), the console showing the
plan loaded (its node count and arena use) and the expansion volume the link wrote; and `rp-device-check` flashed once
and its pass line read off the console, then the box firmware flashed back.

**How it is built** (as E1 and E2, Anthropic models only): a verified prototype on the local branch `proto/e3-host`,
stages 1 to 4 by Opus and stage 5 by Sonnet, each reviewed by Opus with a fix pass, the Pico gate run by the
controller on the prototype's image; the plan generated from the prototype, replayed to identical trees and reviewed
by Opus; Sonnet implementers, Opus code reviews of tasks 1 and 3, Sonnet acceptance, the controller's hardware legs,
an Opus final review. Work files in the gitignored `.superpowers/e3pro`, `e3plan` and `e3exec`.

## 10. Left beyond E3

- **The audio version:** a codec and its DMA driver, a fixed-cadence block driver, the float tolerance against the
  desktop, a device audio-out node.
- **Plans other than the built-in one:** a plan file on the SD card, a live upload over SysEx with an on-device plan
  swap, and resources on the device.
- **E4:** MicroQuickJS on the Pico, TS-authored nodes in a box plan.
- **The rest of the package split** (E2 design section 11), the Launchpad's pack and `threads` flag, and the template
  gaps E2 recorded (section 13.8).

## 11. Prototype questions

1. Whether the exporter's host-built size estimate is really an upper bound for every device type (a type whose
   `extra_memory` depends on `sizeof` of something smaller on x86-64 would break it); the loader's refusal stays the
   authority either way.
2. What `-Wstack-usage` limit the images can hold today, and which frames exceed it (the protocol's `writeBattery`,
   E1's settings decode).
3. Whether one 256-frame `maxFrames` and the carry keep the stress latency within the baseline, and how many blocks a
   long N8 wait (a boot, a 12 s install) becomes.
4. Where the box's stats can still be counted without the firmware reaching into the plan (the edges, or a node
   counter through an interface).
5. Whether the blob's golden can stay byte-stable across hosts (the settings' msgpack from parsed settings is; the
   pools' order follows the planner's).
6. How large the box's arena must be for the bridge plan, and how much a later audio plan would want.
