# Phase E3: exported plans and the box as a graph host

**Date:** 2026-11-05 · **Branch:** `feature/modularize` · **Status:** built (E3, five commits); section 12 records
the build, answers section 11 and carries what was left, and sections 1 to 10 are corrected in place

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
   *As built:* latency holds because the image runs from SRAM (`copy_to_ram`); run from flash, E3's longer message path
   cost about 95 us on average (section 12.5, question 3).
3. **Memory: one meaning everywhere; the Pico refuses what does not fit.** Defaults are the same on every target. The
   box's loader sizes a plan against its arena and refuses, naming the node and the overrun, before creating
   anything; the exporter runs the same sizing so a built-in plan that cannot fit fails the firmware build.
   `rp.core.delay`'s one-second default is refused on the Pico; a box plan sets `maxSamples`.
   *As built:* no rule names the delay. A plan is refused when it does not fit its image's arena, so the default (a
   plan of it alone needs about 396 KB) is refused by the box's 128 KiB arena, at export and at boot. It fits
   `rp-device-check`'s 400 KiB arena, whose per-type check runs it. Both built-in plans set `maxSamples`
   (section 12.8).
4. **A generic host, cart policy in the plan.** The firmware parses raw MIDI and filters nothing (it still consumes
   the box's own commands); the box's cart link owns the link's state (no game running: not connected, a push drops
   and counts) and its batching; what one cart wants lives in the plan: BlipToaster's drops aftertouch through a new
   portable `rp.core.midi-filter`. `rp.n8.nes` stays cart-agnostic.
   *As built:* BlipToaster's plan also drops system common and real time (section 5).
5. **The box link applies the node's expansion volume as the desktop's N8Host does**, and the box's own mapper-based
   volume check retires.
   *As built:* written at every connect and on a change; only a failed write is retried, a second later. The link's
   -1 (N8Host's AUTO, which it holds only while no node holds it, so never while the plan runs) writes nothing, where
   N8Host would apply its mapper rule.

## 3. The shape

A JSON document for the box goes through the desktop planner and the exporter into a versioned blob, the firmware
embeds it, and at boot the loader lays it out in a static arena and hands the executor a `PlanView`; each main-loop
pass runs one block.

| Piece | Where | Built for |
|---|---|---|
| The blob format (`PlanBlob.hpp`), `exportPlan` (`PlanExport`) | `packages/graph-runtime` | desktop |
| The loader (`PlanLoader`) | `packages/graph-runtime`, in `retroplug-graph-exec` | desktop and device |
| `retroplug-export-plan` (the build-time tool; `--arena`, `--sample-rate`, `--max-frames`, `--header <symbol>`, `--packages`) | `packages/graph-runtime/export-plan` | desktop |
| `rp.core.midi-filter` | `packages/core` | desktop and device |
| `rp.device.midi-in` and the `rp.device.midi-in/1` interface (`rp_device_midi_in.h`, in `node-sdk`) | `packages/device-io` (new, `device: true`) | desktop (export, tests) and device |
| `rp.devtest.source`, `.digest`, `.lockstep` (test nodes; a coupled pair, a signal, a digest) | `packages/devtest` (new, `device: true`, listed by no product) | the device check, the exporter, the tests |
| The generic box host: `BlockDriver`, `MidiStaging`, `DeviceHost`, `BuiltInPlan` (portable), `UartMidi` (Pico SDK) | `pico/host/` (new library) | device; the portable four also the desktop tests |
| The N8 box: flows, `BoxCartLink`, `BoxStats`, `CartVolume`, `plan.json`, `plan.cmake`, `main.cpp` | `pico/n8-host/` | device; the link, stats, volume and `UsbCdcTransport` also the desktop tests |
| The device check's plan, `plan.json` and `plan.cmake` | `packages/graph-runtime/device-check/` | device |

The device-mode objects of the packages' device entries are built by `retroplug_device_mode_objects`
([cmake/packages.cmake](../cmake/packages.cmake)), which the export tool and `retroplug-device-mode-test` share, and
`retroplug-graph-exec-device` compiles the executor and the loader with `RP_DEVICE`, `-fno-exceptions` and `-fno-rtti`
in the ordinary desktop build, so a non-portable line in the loader fails there.

## 4. The blob, the exporter and the loader

**The blob** (`PlanBlob.hpp`). Little-endian, every record a run of 32-bit words, no pointers: indices and offsets only.

- A header: the magic `RPPL`, the format version (1), the target (`rp2350`, 1), the whole blob's size, the plan's
  sample rate (a double, as two words) and `maxFrames`, the arena budget it was exported against, the pool sizes
  (audio buffers, resource cells) and a 17-entry section table of `{offset, count, recordBytes}`; the reader holds each
  `recordBytes` to its own.
- The sections: Strings, Settings (msgpack), Nodes (type id, settings, coupled group, schedule position), Steps,
  Views, ParamViews (`{stream, wired}`), Channels, Queues, Streams, Clears, Rolls, Feedback, Masks, MaskBytes, Peers,
  PeerNodes and ParamStarts; then a CRC-32 (IEEE) of every byte before it.
- The pools are the planner's, in the planner's order: audio buffers of channels x `maxFrames` (the shared silence and
  scratch first, then every wire buffer, then each feedback front's channels, which load as ordinary buffers), queues
  with their message and byte capacities (the empty and scratch queues, wired outputs, fronts), and control streams
  of 64 changes. A channel table dedupes channel arrays by identity, so an input that shares its producer's array
  shares its run, as on the desktop.
- A param a wire drives is flagged `wired`, and `LoadedPlan::param` answers null for it, as the desktop's `setParam`
  refuses to write one.
- Not carried: instance sizes (the device computes its own), resources (the box has no resource table: a plan that
  names one is refused at export, "resources are not exported to rp2350 yet"; an unbound resource port loads as a null
  cell) and settings commits (every device instance starts fresh).
- The rp2350 hands out alignments up to `kTargetAlign` (8, in `PlanBlob.hpp`). `node-sdk`'s `kDeviceAlign` (8) is what
  a device-mode instance and its tail are aligned to, in place of `max_align_t`, so the host build of a type asks what
  the device asks and a type that truly needs 16 is seen at export and refused (`node-memory`: "node asked for an
  alignment of 16, where an rp2350 arena gives a power of two up to 8") by the exporter and the loader alike. The
  arena itself must be aligned to `kArenaAlign` (`PlanLoader.hpp`, which a firmware declares its arena with), the
  larger of `kTargetAlign` and the compiler's `max_align_t`, which is also the buffer stride. (A portable type holding
  a `long double` or an `alignas(max_align_t)` member reads 16 on x86-64 and 8 on the M33 and is refused for it: the
  safe direction. The `static_assert` that `kDeviceAlign` equals the device's `max_align_t` holds the Arm build only;
  the RP2350's Hazard3 cores are riscv32, where it is 16, and a RISC-V build would need its own figure.)

**The exporter** (`exportPlan(document or json, registry, deviceTypes, ExportOptions{target, sampleRate, maxFrames,
arenaBytes, trialLoad})`, desktop):

- It plans the document with the planner for `Target::Pico`, so every rule the desktop enforces applies (wiring,
  layouts, the `heap` and `threads` refusals, `unique`). Planning instantiates desktop nodes, so the exporter gives
  them a stub host: `rp.host.devices/2` answering null, `rp.device.midi-in/1` with nothing staged and a resource table
  that refuses. A type that answers no `rp.sdk.settings-json/1` is refused (`settings-export`), and a non-finite sample
  rate is (`export-options`).
- It writes each node's settings from its parsed settings (`rp.sdk.settings-json/1`, then `settingsToMsgpack`), never
  from the document's JSON, so the device's reader agrees by construction for an integer past INT64_MAX in a signed
  field and for `-0` ([E1 design](modular-graph-phase-e1-design.md) section 10).
- It sizes the plan with the host-built device-mode objects, in the same one-pass layout the loader uses (a bump that
  counts or places, so the measure and the placement cannot disagree), and over the arena budget it refuses, naming
  the node that tips it over and by how much (`"d" (rp.core.delay) does not fit: the plan needs <N> bytes, <K> past the
  <arena>-byte arena`, N about 396 KB for a defaulted delay). If the plan's own structures alone overflow it names no
  node. With no budget it sizes
  and refuses nothing for size.
- That figure is an upper bound on the device's, which section 12 shows type by type. Every size and alignment of a
  device-mode instance and of the loader's own records is at least as large on x86-64 as on the M33 (8-byte pointers,
  a wider `size_t` in a few members), and the bump layout's total is monotone in them. The hazard class is a 32-bit
  `size_t` wrap in some type's `extra_memory` (the device would ask for less than it uses): `rp.core.delay`'s is safe
  because its desktop `validate` caps the ring, and a new type's needs the same care. The loader's own sizing, in
  64-bit offsets, stays the authority.
- Then it loads the blob it made into a host buffer of exactly the measured size over the device-mode types and unloads
  it (the trial load), so a refusal only a device build makes (a create or an activate that fails) fails the export,
  and with it the firmware build, in the loader's words. A fault at the block (`at: process`) still exports; that is the
  box's to report. The trial load cannot see what only the real box answers (its host interfaces, links, timing).
- `retroplug-export-plan <plan.json> <out> [--arena <bytes>] [--sample-rate <hz>] [--max-frames <frames>] [--header
  <symbol>] [--packages <name>,...]` is a desktop tool, not a user command (defaults 48000 Hz and 256 frames). It
  writes the raw blob, or with `--header` a C++ header `alignas(4) inline constexpr std::uint8_t <symbol>[]` plus
  `inline constexpr std::size_t <symbol>ArenaEstimate` (the export's measured size), rewritten only on a change.
  `--packages` registers only the named packages' device entries in both its registry and its sizing types (none named:
  all), so a plan naming a type the image does not carry fails the firmware build with the planner's `unknown-type`;
  an unknown package is a usage error. Errors go to stderr as `<code> <node>: <message>` with exit 1. The box's and
  the check's builds run it over their `plan.json` and embed the header, as `rp-device-check` consumes its
  generated cases; `tools/build-box.sh`, `tools/build-device-check.sh` and loop.sh need it built in the desktop tree.

**The loader** (`loadPlan(blob, arena, registered packages, rp_host*, PlanLoadError*)`, `sizePlan` for the count-only
form, and `unloadPlan`, portable). The registered types are the span of `rp_package*` a device product's generated
`rp_register_packages()` returns:

1. Checks the magic (`plan-format`), the version (`plan-version`: "plan format 2, this loader reads 1"), the length
   against the header (`plan-truncated`), the CRC (`plan-crc`), the target (`plan-target`), the rate and `maxFrames`,
   the section table, every record's index and range (`plan-malformed`: "views record 7 names queue 6, past the plan's
   6 queues"), and every type id against the image's registration (`unknown-type`, naming the node).
2. Sizes and places the plan in one pass: each node's own `query_memory` on its msgpack settings, plus every pool,
   view array, step and counter. Over the arena it refuses (`arena`) with the node and the overrun; sizes are 64-bit
   with saturating products, so a hostile count cannot wrap a 32-bit `size_t`. Everything placed is zeroed, and a
   refused load has written only into the arena and created nothing.
3. Runs it up in `PlanStorage::adopt`'s order: create and activate each node in document order, bind the steps (and
   each coupled member's `rp.step/1`), check every peer list's `rp.link/1` before any `set_peers` call, call
   `ports_changed`, write the param starts. A failure part-way unwinds what it made in reverse and reports the node and
   the step (`create-failed`, `activate-failed`, `link-without-step`, `peers-without-link`).
4. Hands back the `LoadedPlan`: the `PlanView`, the nodes (each with its fault flag and drop counter), the plan's
   fault and drop totals, `find(id)`, `param(node, k)` (the stream a host pushes a change into) and `bytes` (what the
   plan takes of the arena). `unloadPlan` deactivates and destroys in reverse.

`PlanLoadError` is `{code, node, message}`. The node ids and masks point into the blob, which must outlive the plan
(it is in flash on the box). The per-node fault flags and drop counters live in the arena. Nothing allocates and
nothing throws. Coupled groups and feedback load like everything else, so a plan with audio loads the same way later.
A block must never be longer than the plan's `maxFrames`: `runBlock` does not check.

**The blob is trusted.** The loader's checks bound its own reads and writes and catch corruption, but they cannot hold
a step's views to its node's port layout, because a device build has no `layout_for`. That holds while the only blob is
the firmware's own build output. A blob from outside would need per-step port kinds and counts checked against
something the device knows, such as a port signature (section 10). The same gap means a blob made by an exporter built
from other package sources than the image's is not seen as wrong: `pico/n8-host/loop.sh` refuses to build with an
exporter older than the sources it compiles (section 12.8).

## 5. The box as a graph host

**The code splits** into `pico/host/`, a library with no N8 in it, and `pico/n8-host/`, the product. A later box
product links the same library.

- `pico/host/`: `BlockDriver` (the seam and E3's `ElapsedDriver`), `MidiStaging` (the parser, the staging and the
  `rp.device.midi-in/1` implementation), `DeviceHost` (the device `rp_host`: `rp.host.devices/2` over a table of links
  the product fills, `rp.device.midi-in/1`, a log hook; no allocation, no jobs, no resources), `BuiltInPlan` (the load
  and its console lines, `run(driver)`, each faulted node printed once) and `UartMidi` (the UART1 ISR, its 4096-byte
  ring with timestamps, the masked idle poll, the drain into the staging). All but `UartMidi` compile on the desktop for
  the tests; in a device project `host.cmake` makes the library an INTERFACE one so the sources take the image's flags.
- `pico/n8-host/`: the N8 flows (probe, boot through the menu, health, recovery, the ROM table, the SysEx ROM pick) in
  `main.cpp`, the box cart link (`BoxCartLink`), `BoxStats` (the `filtered` arithmetic), `CartVolume` (the expansion
  volume write), `plan.json` and `plan.cmake`.

**One main-loop pass:**

1. Feed the watchdog, pump USB.
2. Poll the UART and drain it into the staging. The box's own `F0 7D 50` commands are consumed here, by the product's
   SysEx hook; every other message is staged with the frame its timestamp falls on.
3. Ask the block driver for the next block and run it (`plan.run(driver)`); the host then ends the block itself
   (`MidiStaging::endBlock`). With no plan the staging is cleared instead.
4. Drain the cart link: the volume first, then the queued messages, to the N8, batched.
5. The N8 part's probe, boot and health, the stale-MIDI discard at the forward edge, the link's connected state.
6. Stats.

Block work and N8 work run in turn on this one loop, which is what keeps `push` from waiting on the N8 under the
elapsed driver. An audio cadence cannot be served from this loop (section 10).

**The block-driver seam** is one call, `bool next(uint32_t* frames, rp_transport*)`. E3's `ElapsedDriver` counts frames
from its start on a 64-bit microsecond clock (`frameAt = (us - origin) * rate / 1e6`) and answers each block
`min(due - done, maxFrames)`, so a fraction of a frame is never lost and a block never passes `maxFrames`; a pass less
than a frame (20.8 us at 48 kHz) after the last block gets none. The built-in plan runs at 48 kHz with `maxFrames` 256
(5.3 ms).

The carry is bounded: when a call finds more than `kCarryBlocks` (4) x `maxFrames` (1,024 frames, 21.3 ms) due, the
excess is dropped and counted (`skipped()`, on `[stats]`) and the four blocks run, one a pass. That carries the main
loop's ordinary stalls whole (a console line, about 11 ms, is 528 frames) and makes a long N8 wait (a boot, a 12 s
install) at most four blocks behind, so a message lands within 21 ms of its frame and the passes after a wait do no
extra work; running every frame instead would be thousands of empty blocks. What is skipped is time a plan that reads
time (a MIDI clock) would lose under load, which the audio driver settles. `frameOf(us32)` places a 32-bit UART
timestamp on the current block, clamped into it: a message before the block at frame 0, one past it (the block runs
behind the clock) at its last frame. An audio driver later answers with its fixed block when its buffer is ready.

**`MidiStaging`.** The parser stages every message whole with its status byte (running status restored; system common
and real time included). A SysEx is assembled whole in its own 4 KB buffer and staged at its F7 (or at the status that
cut it short), so it is timed from its F7 and a continuation chunk never reaches a `sysex` drop led by a data byte; one
past 4096 bytes is dropped whole and counted (`tooLong`). The staging is bounded by `RP_DEVICE_MIDI_IN_MESSAGES` and
`_BYTES` (static_asserted against the UART ring too): `feed()` refuses a byte, taking nothing, when the staging could
not hold what it might complete, so the caller leaves it in the UART ring for the next pass and nothing is lost to the
bound (a completed SysEx with no room is held whole, with at most one short message behind it, order kept).
`next()` hands messages out in order and answers false once empty. `endBlock()` runs after every block that ran: it
drops, counted (`untaken`), whatever the block did not take (a silenced `rp.device.midi-in`, a plan without one), then
stages what waited for room. So the staging empties whatever the plan is, and the parse, with it the box's commands,
never stops.

**`rp.device.midi-in`** (`packages/device-io`, `unique`: the host's staging is one stream) reads the host's
`rp.device.midi-in/1` interface, a pull: it calls `next(ctx, frames, out)` until it answers false. It keeps the queue's
rules: a frame at or past the block's end is clamped to the last frame, an earlier one up by the push, an empty message
is skipped, a message longer than the queue's bytes is dropped before its length reaches a push, and a full queue
drops and counts. Its output is 256 messages and 4 KB, the UART ring's size. With no interface it activates and gives
empty blocks. (The desktop's `rp.host.midi-in` is one per document by `Engine::setGraph`'s type-id check, not by
`unique`, and refuses to activate without `rp.engine.host-io/3`.) Desktop products do not list the package, so their
`[packages]` rows do not change; the export tool, the device check and the box do.

**The box cart link** (`BoxCartLink`, over `rp::n8::CartLink`):

- `push` copies into a fixed ring (512 messages and 8 KB, one block's MIDI twice over) and answers false, counted,
  while not connected (`notReady`) or when the ring is full (`full`); `connected()` is "a game is running and the box
  forwards", today's not-ready rule as the link's state.
- `drain(writer, now)` after the block writes the volume first, then the ring in FIFO writes of whole messages up to
  192 bytes (only a message longer than 192 is split), each timed from its first message's arrival.
- `releaseNotes` queues the note-offs the same way. After a power cut `rp.n8.nes` owes the note-offs of the notes it
  held, and pays them at its first connected block, so a freshly booted ROM receives a few note-offs (`8c nn 00`, c the
  channel) before anything else.
- `setExpansionVolume` records the value; `drain` writes it to the cart's config register at every connect and on a
  change (`rp::box::writeExpansionVolume`: a write, then a read-back for the console line), as N8Host does on the
  desktop. Only a failed write (the transport took nothing) is retried, a second later; a write that went through is
  done whatever it reads back. The link's own -1 (N8Host's AUTO, held only while no node holds the link) writes nothing
  on the box: N8Host's mapper rule for it would need a mapper table in the firmware. The box's mapper-based check
  retires: the register gets the plan's value
  (unity, 128) on every game, where the box used to leave an existing non-zero value.
- The box offers no `rp.host.jobs/1`, so `rp.n8.nes` refuses jobs ("this host takes no device jobs").

**Stats and logs.** Every log line the gate's scripts match stays, and the `[stats]` fields keep their names and
meanings except `filtered`, which counts at the plan's two edges, both the host's own: the staging (what
`rp.device.midi-in` took, `tooLong`, `untaken`) and the link (pushes received, refused, written, failed).
`sysex` too moves a little: it counts a SysEx once a write the cart took has carried its F7 (E2 counted it as it
entered a batch), so a refused or failed one is not counted.

- `filtered = handed + tooLong + untaken - (pushes the link received - the node's own note-offs)` (one function,
  `rp::box::filtered`). It counts what the plan's filter dropped, including system common and real time,
  rp.device.midi-in's
  own overflow, the SysEx past 4 KB and the messages no block took. The plain "staged minus received" would be wrong by
  the note-offs `rp.n8.nes` owes, which are received and never staged, so the link keeps a mirror of the node's note
  books from its own answers (`made`) and subtracts them. The mirror reads one high in a single case, reachable only
  with `linkfull` moving: a refused payment of an owed note-off whose note-off the same block's input carries.
- `notready` is the link's refusals while not connected. `[stats]` gains two trailing fields: `linkfull=` (pushes
  refused for a full ring; 0 expected) and `skipped=` (frames the driver dropped past its carry, cumulative since
  boot: judged by its growth, not by zero).
- At boot, after the `box_start` line, the box prints `[host] plan loaded: 3 nodes, <N> of 131072 arena bytes, 48000 Hz,
  256 frames a block` and a `[host]   node <id> (<type>), <b> bytes` line per node (the old "registered, not planned"
  line is gone). At every connect it prints `[n8-host] cart link: expansion volume 128 written (reads back 128)`, or
  `written, but reads back N`, `written (no read-back)` or `... expansion volume N: write did not go through, trying
  again each second`; the written lines print at every connect and every change, the retry line once per failing
  streak.
- A node that faults is silenced by the executor and the host prints it once. A built-in plan the loader refuses (which
  the build's check rules out) prints `[host] the built-in plan was refused: <code> <node>: <message>` and `[host] no
  plan runs, and nothing is forwarded`, shows the error LED and forwards nothing.

**`plan.json`:** `rp.device.midi-in` -> `rp.core.midi-filter` (dropping `polyAftertouch`, `channelAftertouch`,
`systemCommon` and `realtime`) -> `rp.n8.nes`. The old firmware forwarded channel voice and SysEx only; the host now
filters nothing, so the cart's policy must drop system common and real time too, or MIDI clock and active sensing would
reach the ROM and stress.sh's runstat case (6 real-time bytes among 6 notes, `fwd == 6`) would fail. `plan.cmake`
holds the plan, 48000 Hz, 256 frames and the 131072-byte arena in one place for the firmware and the tests.

## 6. `rp.core.midi-filter`

One events input and one events output. One setting, `drop`, an `rp::list<MidiKind, 10>` of `rp::choice` message kinds:
`noteOff`, `noteOn`, `polyAftertouch`, `controlChange`, `programChange`, `channelAftertouch`, `pitchBend`, `sysex`,
`systemCommon`, `realtime`; an empty list passes everything, and a kind named twice is refused. Every other message
passes unchanged, its frame, flags and order kept. No channel filter. It is a desktop node as much as a device one:
every desktop product lists `core`, so the `[packages]` golden gains it, and it has its own descriptor golden.

**A kind is read from the first byte:** 0x80 to 0xEF by high nibble (seven channel kinds), F0 and a lone F7 are
`sysex`, F1 to F6 `systemCommon` (the undefined F4 and F5 included), F8 to FF `realtime`. A note-on of velocity 0 is a
`noteOn`: kinds are statuses, not meanings. An empty message or one led by a data byte (running status, which
nothing in the tree produces but nothing refuses) has no kind and always passes; a message holding two MIDI messages
is classified whole by its first.

**Its output's capacity follows its input's** by a mechanism the design did not name because nothing did it: a
capacity of `{"match": "<input>"}` (in C++ `rp::events_out::matching<"in">`, `bytes_out::matching<...>`; in TS
`defineNode`'s `{ match }`). It is exclusive with a size, names an input of the output's own kind (events from events,
bytes from bytes), and is resolved by the planner per plan, in a `capacities()` pass after `channels()`: it follows a
feedback wire to its source, an unwired input gives the default (256 messages and 4 KB), and a loop of matches is
refused (`capacity-match`). Layouts and descriptors keep it as declared, so a source's capacity change resizes the
matched queue at the next plan without reshaping the downstream node. A fixed capacity could not serve both the box
(256 / 4 KB) and the desktop (2048 / 64 KB). The consequence: the filter's output is its source's size and it passes a
subset, so the filter itself never drops (the drop-and-count path in `pushMessage` exists and no real plan reaches it).
The exported blob carries resolved sizes, so the device needs nothing new.

## 7. Inherited items

- **The box image:** `PICO_HEAP_SIZE=0` (no `.heap` reservation; `rp-device-check` keeps the SDK's 2 KB one). The
  arena is a static in `.bss`, 128 KiB: the bridge plan takes under a quarter of it, the rest holds about a hundred mono
  256-frame buffers, or a modest synth's buffers and streams, while the whole image, its code
  included, takes about 305 KiB of the 512 KiB main SRAM (PIO-USB; about 286 KiB for `RP_NATIVE_USB`), because it runs
  from SRAM (`pico_set_binary_type(n8_host copy_to_ram)`, which removes the flash-cache refills a message's path
  paid; section 12.5, question 3). That leaves roughly 200 KiB for E4's MicroQuickJS heap and a codec's DMA buffers,
  and the levers if it is not enough are the 128 KiB arena and cold code (the plan loader, the N8 protocol) moved back
  to flash. The 87-symbol trap holds. A constant-initialised static with any non-zero member lands in `.data` and is
  copied out of flash at boot, so the staging and the link keep their state in all-zero structs in `.bss`.
- **The stack:** `-Werror=stack-usage=1024` (`RP_DEVICE_STACK_FRAME` in `cmake/device-image.cmake`) is on every
  `device: true` package library built in a device project and, through `rp_device_executable`, on both images, whose
  compiles include the SDK's, TinyUSB's and Pico-PIO-USB's sources; it holds in Release and Debug. Every frame is held
  to 1 KiB at build time; the chains are still summed by hand, and `rp-device-check` prints its measured high-water
  mark (section 9). A frame of ours over the limit is fixed (`RP_NOINLINE`, `node-sdk`'s `noinline.hpp`, or the local
  moved off the stack); a vendored one gets a per-source `set_source_files_properties` raising the limit for that
  source alone, with a comment naming the function, its frame and why the chain fits, never the flag taken off a
  target. It is not on the desktop's `<target>-device` object checks: their frames are x86-64's, AppleClang has no
  `-Wstack-usage` and MSVC's equivalent is another tool. `rp-device-check` gets `PICO_STACK_SIZE=0x1000` and
  `PICO_USE_STACK_GUARDS=1`, which writes the M33's `MSPLIM` with `__StackBottom`, so an overrun faults (CFSR STKOF)
  instead of corrupting; the image links no `pico_multicore`, so there is no second stack. The deepest chain by hand
  is the plan's load, about 2.7 KB; the image paints its stack and prints the measured high-water mark (section 9).
- **`UsbCdcTransport`:** a desktop test over a fake TinyUSB (`FakeTinyUsb`, the five calls it makes and the SDK clock)
  for its remount, unmount, drain and flush-silence rules.
- **`rp-device-check` on the loader:** the image also loads and runs a 10-node plan at build-time-exported bytes
  (`packages/graph-runtime/device-check/plan.json`: a coupled pair of `rp.devtest.lockstep`, a feedback wire through a
  delay, a wired param, five params set in the document and eight moves), judged: loaded within the export's
  estimate, every move pushed, no drops, a digest node that heard sound and counted every block. The coupled pair
  comes from `packages/devtest`, a package no product lists, so test nodes ship nowhere and both the image and the
  exporter can register them. The check runs after the per-type checks and the protocol, in the same 400 KiB arena.
- **Left for the audio version:** the codec and its DMA driver, the fixed-cadence block driver, the float tolerance
  between the M33 and the desktop (and `-ffp-contract`: the check prints the device's digest beside the desktop's and
  judges only the same-binary one).

## 8. Tests

- **`retroplug-device-mode-test`** (45 cases): a core and DSP document with a coupled group, a feedback wire and moving
  params, exported, loaded by the loader over the device-mode builds in an arena and run in ragged blocks, renders bit
  for bit what GraphHost renders with the desktop builds, with the counting allocator seeing nothing from load to
  unload (`DevicePlan.test.cpp`, `[plan]`; a second document with events and control feedback, and the bridge plan
  `rp.device.midi-in` -> filter -> `rp.n8.nes` over a fake cart); a mutation case that flips each bit of each byte of the three golden blobs, and each byte whole, resealing the CRC, and holds sizing and
  loading to a clean load or a refusal in words with nothing left alive (about 1 s, so the whole sweep runs, and under
  build-asan the blob and arena are exact-size heap blocks, so an out-of-bounds read or write fails there); every loader
  refusal with its message (a version, the
  CRC, a truncated blob, an index out of range, a wired flag, a section past the blob, an unknown type, over budget to
  the byte, a create or an activate failing part-way and unwinding, an alignment of 16); `rp.device.midi-in` in both
  builds (`DeviceIo.test.cpp`); the midi-filter's device build against its desktop build (the twin table); the box
  host's portable pieces built for the host (`BoxHost.test.cpp`, `[box]`: the block driver's arithmetic, carry and
  bound, the staging and its bounds, staged MIDI reaching `plan.json`'s plan at its frames, the cart link's ring,
  drops, batching, volume and note-offs, the whole pass over a fake transport with no allocation, the refused plan and
  the faulting node, the box's commands getting through whatever the plan does, the volume write over a scripted
  transport); `UsbCdcTransport.test.cpp` (`[usb-cdc]`); and the device check's plan (`DeviceCheck.test.cpp`: the
  judged steps, the estimate, the refusals).
- **`retroplug-graph-test`** (295 cases): the midi-filter (each kind dropped, order and frames kept, an empty list
  passing everything, the matched capacity, nothing allocated once warmed), `capacity.match` (a chain of matches, a
  feedback-wired one, the loop refusal, the budget text), and `reserveMessage`'s 32-bit wrap.
- **Goldens:** three blobs, `plan-blob.hex` (1876 bytes), `box-plan.hex` (784) and `device-check-plan.hex` (2056),
  recorded with `RP_PLAN_BLOB_UPDATE=1` and compared on every host; they hold no float arithmetic (the settings are
  msgpack of parsed numbers, the rate a double's bits, the rest integers in the planner's order), and only the
  header's `ArenaEstimate` follows the host's sizes, which no golden holds. Whether they are stable across hosts is
  owed a Windows and a macOS run. The filter's descriptor golden is the fourth. The device check's desktop digest
  (`kPlanDesktopDigest`) is a linux-x64 GCC 13 float figure and is compared only where `rp::buildTarget()` is
  `linux-x64-gnu13`; the same-binary comparison (the loaded run against GraphHost's) holds everywhere.
- **`exportPlan`:** its refusals (a `heap` or `threads` type, a resource in settings or bound, a plan over budget, a
  cycle, non-JSON), each with its message (`DevicePlan.test.cpp`). **The export tool,** run as a process: the box
  plan's golden bytes and a header whose estimate equals the export's size, `--packages` refusing a type outside the
  list and an unknown package (`BoxHost.test.cpp`).
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
and its four lines read off the console (the UART, GP0, 115200), then the box firmware flashed back:

1. `rp-device-check: pass, 20 types, arena <N> bytes`
2. `rp-device-check: n8 protocol pass, <bytes> bytes`
3. `rp-device-check: plan pass, 10 nodes, <A> arena bytes (the export's estimate 56064, the arena 409600), 48000 Hz, 256
   frames a block: loaded, ran 48000 frames in 467 blocks with 8 param moves and 0 drops, unloaded, in <T> us; digest
   <16 hex> over 467 blocks (the desktop's b8854b2cc247318f)`
4. `rp-device-check: stack <S> of 4096 bytes at most`

A failure prints `rp-device-check: plan failed, step <n> (<load|move|process|result|estimate|case>) at node <id or ->
after <b> blocks: <code>: <message>` for a refused load, `...: <message> (<A> arena bytes, the export's estimate
56064)` with no code once a plan loaded, and blinks 10 plus the step (11 to 16); a type failure blinks its step and the
protocol 20; the LED is steady on a pass. With no plan line and a dark LED it is a hang or a stack overrun: halt with
the probe and read CFSR (0xE000ED28); STKOF (bit 20) with HFSR FORCED (bit 30) and the PC at `isr_hardfault` is the
stack guard.

**How it is built** (as E1 and E2, Anthropic models only): a verified prototype on the local branch `proto/e3-host`,
stages 1 to 4 by Opus and stage 5 by Sonnet, each reviewed by Opus with a fix pass, the Pico gate run by the
controller on the prototype's image; the plan generated from the prototype, replayed to identical trees and reviewed
by Opus; Sonnet implementers, Opus code reviews of tasks 1 and 3, Sonnet acceptance, the controller's hardware legs,
an Opus final review. Work files in the gitignored `.superpowers/e3pro`, `e3plan` and `e3exec`.

## 10. Left beyond E3

- **The audio version:** a codec and its DMA driver, a fixed-cadence block driver, the float tolerance against the
  desktop, a device audio-out node. A plan that reads time (a MIDI clock) on the elapsed driver loses what the carry
  bound skips under load. A fixed cadence (5.3 ms at 256 frames) cannot run from the main loop, which blocks for
  every N8 call (about 5 ms per FIFO write, up to about 200 ms draining a full ring, 2.5 ms per sniff or health read,
  13.7 ms per `[stats]` line, 12 s for an install, up to 48 s for a menu boot), so the audio version runs blocks off it
  (core 1, the DMA interrupt or the transport's idle hook). Block work then overlaps N8 work, and the one-thread
  contracts of `MidiStaging` and `BoxCartLink` no longer hold: the staging and the link's ring become
  single-producer / single-consumer, or the N8 calls become non-blocking. `frameOf` is not part of the `BlockDriver`
  seam today (the box binds the staging to `ElapsedDriver::frameOf`), and moves onto it. The loader and the nodes are
  untouched by a new driver.
- **Plans other than the built-in one:** a plan file on the SD card, a live upload over SysEx with an on-device plan
  swap, and resources on the device. A blob from outside the firmware needs more than the loader checks today: it
  trusts the blob (section 4), so a live upload needs per-step port kinds and counts checked against something the
  device knows: a per-node port signature (input, output and param counts and kinds, which the device-mode build
  knows at compile time from its reflection) carried in the blob and checked at load. The same check would catch a
  stale exporter (section 12.8).
- **Other hosts:** Windows and macOS runs of the blob goldens (which answer whether the blob is byte-stable across
  hosts) and the MSVC build of the new libraries (`retroplug-graph-exec-device`, the export tool's objects, the box
  host's desktop objects, the usb-cdc object library and `packages/devtest`); a Windows export may accept a type with a
  `long double` that Linux refuses (MSVC's is 8 bytes), which is harmless today.
- **E4:** MicroQuickJS on the Pico, TS-authored nodes in a box plan.
- **The rest of the package split** (E2 design section 11), the Launchpad's pack and `threads` flag, and the template
  gaps E2 recorded (section 13.8).

## 11. Prototype questions

Answered in section 12.

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

## 12. As built

Five commits on `feature/modularize`: the blob and the loader, the two nodes, the box as a graph host, the device
check on the loader and these docs. Parity bit for bit, the kit goldens, the render baseline, the descriptor goldens
of existing types, `buildGraph`'s goldens and `edio-golden.json` were untouched throughout, and none of them was
re-recorded; the golden changes are the intended ones (the `[packages]` golden gaining `rp.core.midi-filter` in each
of its nine products, and the filter's descriptor golden and the three blob goldens, new). Sections 1 to 10 are
corrected in place where the build made them false. The counts: `retroplug-device-mode-test` ends at 45 cases (4649
assertions; the baseline's 11 / 2446), `retroplug-graph-test` at 295 cases (356647; 282 / 356435), `pnpm test` at 174
files with `define-node` at 21 cases (20), engine-graph, parity, native, ui and the render and kit legs unchanged,
`device:check`'s image at 130232 / 2000 / 416830 flash / data / bss (82528 / 2000 / 416402) and the box's two images
at flash 101200, data 23832, bss 188020 (PIO-USB; 48796 / 15324 / 32360) and 101576 / 9836 / 181788 (`RP_NATIVE_USB`;
48756 / 1560 / 26128), of which 100628 and 101004 bytes of the flash run from RAM, the images taking 312484 and 292636
bytes of SRAM in all.

### 12.1 The blob, `exportPlan` and the loader (commit 1)

- **`PlanBlob.hpp`, `PlanLoader.{hpp,cpp}`** (portable, in `retroplug-graph-exec`), **`PlanExport.{hpp,cpp}`** (desktop,
  in `retroplug-graph`) and **`retroplug-export-plan`**, as section 4 records them. `retroplug-graph-exec-device`
  compiles the executor and the loader for the device flags in the ordinary desktop build, and the device images compile
  them too.
- **`retroplug_device_mode_objects`** in `cmake/packages.cmake`: the renamed device-mode object library of the packages'
  device entries and a generated `rp_register_device_mode_packages()`, hoisted out of `tests.cmake` so the export tool
  and the test share it.
- **`rp.devtest.lockstep`** (`rp.step/1` and `rp.link/1`, five frames a step, mixing its peer's last frame so the round
  robin's order is in the output): no portable `rp.core` or `rp.dsp` type couples, so the coupled-group proof rests on
  it, with portable twins of the rig's noise and probe (`source`, `digest`). It started in the test and moved into
  `packages/devtest` in commit 4.
- **Tests:** the bit-for-bit case (a document with a coupled pair, a feedback copy, a wired param and six moves, run in
  ragged blocks, equal to GraphHost's digest with zero allocation from load to unload), byte stability, every loader and
  exporter refusal, ASan clean.

### 12.2 `rp.core.midi-filter` and `rp.device.midi-in` (commit 2)

- **The filter** in `CoreNodes.cpp`, with `capacity.match` (section 6) in C++ (`QueueCapacity::match`, the layout parse,
  the planner's `capacities()` pass, `rp::events_out::matching<>`) and in TS (`defineNode`'s `{ match }`, a union that
  makes a match with a size a type error).
- **`packages/device-io`**: `rp.device.midi-in`, `rp_device_midi_in.h` in `node-sdk` (plain C, so the exporter's stub
  host and the box's library include nothing of a pack's) and a manifest entry. The device check's package list took
  `device-io` here, not in commit 4: the `[device-check]` case requires every manifest device entry to be in it.
- **The twin table** gained two rows for the filter, over a `kinds` mode of the rig's noise that emits every kind, so
  the device build is held to the desktop's bit for bit.

### 12.3 The box as a graph host (commit 3)

- **`pico/host/`** and the box product as section 5 records them; `plan.json` and `plan.cmake`; the main loop split; the
  N8 flows unchanged hunk by hunk (the probe, boot through the menu, health, recovery, the ROM table, the SysEx pick,
  every TinyUSB callback). Retired: `midi_to_fifo`, `sysex_emit`, `midi_sysex_to_fifo`, the batch buffer,
  `exp_vol_check`, `expansion_chip` and the `N8_EXP_VOL` knob (the volume is the node's param).
- **`cmake/device-image.cmake`'s stack limit**, `PICO_HEAP_SIZE=0`, `UsbCdcTransport`'s fake, `RP_NOINLINE`, one
  `rp::n8::noteEvent` helper for the node's and the link's note rule, and `pico/midi-in/midi.c` cleaned to compile under
  `/W4 /WX` on the desktop (behaviour unchanged).
- **Run from RAM.** `pico_set_binary_type(n8_host copy_to_ram)` in both USB variants: the image is copied from
  flash to SRAM at boot and runs there (question 3 has why). `N8_LAT_TRACE=1` builds a diagnostic that counts the XIP
  cache's accesses and hits and times each segment of a message's path (`LatTrace.hpp`); it is off by default.
- **Sizes:** the box's flash went 48.8 KB to 101 KB (the executor and the plan loader are 29 KB of it at -O3, rp.core's
  types, which the product registers whole, 24.7 KB, the host 4.3 KB) and its bss 32 KB to 188 KB (the 128 KiB arena,
  the 12.4 KB staging, the 13 KB link ring); 100 KB of the code runs from RAM, so the image takes 305 KiB of the 512 KiB
  SRAM (PIO-USB; 286 KiB native) where the flash-run build took 200 KiB. `tools/build-box.sh`'s size line says how much
  of the flash runs from RAM and the RAM in all.

### 12.4 `rp-device-check` on the loader, and its 4 KB stack (commit 4)

- **`packages/devtest`** and the check's plan (section 7); `PlanStep` names the failing step (load, move, process,
  result, estimate, case); `PICO_STACK_SIZE=0x1000` with `PICO_USE_STACK_GUARDS=1`; the stack painted and its
  high-water mark printed; `retroplug-export-plan --packages` and `<symbol>ArenaEstimate`, so the image's pass is
  judged against the export's estimate on the device; `rp.devtest.flood` and `rp.devtest.miscount` as test nodes (in
  the test's `PlanNodes.hpp`, package `rp.devtest.misfit`, not in `packages/devtest`) for the Result step's remaining
  branches.
- **Size:** the image went 86.9 KB to 130.2 KB of flash (the executor and the loader 29 KB, the devtest types 7.5 KB,
  the check's blob and
  judgement), `.stack_dummy` 2048 to 4096.

### 12.5 The prototype questions, answered

1. **The estimate is an upper bound for every portable type today, measured.** The same device-mode sources compiled by
   the host g++ and by arm-none-eabi-g++ 13.2.1 (`-mcpu=cortex-m33 -mthumb`), `sizeof(rp::detail::Instance<T>)` in
   bytes, x86-64 / M33: add 40/28, merge 32/20, join 120/104, split 120/104, downmix 64/48, relabel 56/40, delay 96/56,
   constant 56/40, buffer-player 88/60, buffer-recorder 88/48, gain 88/48, biquad 144/96, dither 176/108, normalize
   120/76, `rp.n8.nes` 672/600. The extra memory is equal or larger on the host (`Dither`'s `Mt19937` shrinks one member
   on the M33, 2560 against 2556), the loader's own records are larger on the host (`PlanStep` 96/48, `rp_queue` 40/28),
   and every alignment is at least as large, so the bump layout's total is monotone and the host's bounds the device's.
   The device now checks it itself: the image fails a plan that loads into more than the export's estimate (the
   Estimate step), printing the device's own figure beside 56,064. The device's figure for the check's 10-node plan
   is 54,360 bytes,
   under the estimate of 56,064; the box's bridge plan took 29,976 on the rig, under the export's 30,560.
2. **The limit is 1024 bytes a frame**, which the images hold with no exemption in Release and Debug. Before commit 3
   exactly one frame passed it: `loadPlan`, 1120 bytes, 352 of them a `PlanLoadError` fallback for a caller passing no
   error, now in a never-inlined helper (768). The deepest now: `writeBattery` 864 (which the box never calls),
   `sizePlan`'s no-error path 792 (desktop callers only), `loadPlan` 768, `createFn<Dither>` and `<Normalize>` 592
   (the check only), `layout` 456, N8Protocol 432, E1's settings decode 320 to 368. A 512 limit would fail five frames,
   each a single large local or the loader's records, not recursion; 1 KiB, a quarter of the 4 KB stack, holds the tree.
   The chains by hand: the plan's load about 2.7 KB (main, `runPlan`, `loadPlan`, `layout`, a `query_memory`, its
   decode, a list field), a create 2.2, a per-type check 2.0, the protocol 2.0, a block 0.6; the box's load at boot
   about
   2.5 KB before the UART and USB interrupts are enabled. The device's measured high-water mark for the whole check
   run is 2,688 of 4,096 bytes.
3. **Latency holds from SRAM, not from flash.** The path from a UART byte to the FIFO write is E2's with the plan in
   the middle of one pass: parse and stage, the block (four small copies a message), the drain with the same batching,
   and the ACK at the next 1 ms USB frame. `maxFrames` does not delay a message, since a message is staged and run in
   the same pass (or, when the pass comes less than a frame's 20.8 us after the last block, the next) and a block that
   runs behind the clock places it at its last frame. Run from flash, though, the first gate measured E3 at about 95 us
   more on average and 130 us more at the worst than E2's image on the same rig (isolated notes, five runs: 689-742 /
   1192-1248 us against E2's 558-653 / 1083-1097), and one run in five had a 12.6 ms stall. The trace build
   (`N8_LAT_TRACE`, which counts the XIP cache) showed why: the message's path is about six times longer (about 11,000
   cycles against about 3,200 with the code in RAM), about 90 cache refills of about 90 cycles each. So the image
   runs from SRAM (`copy_to_ram`): the same rig then gives 640/1086, 677/1114, 680/1098, 679/1128 and 610/1104 us
   average/worst, E2's figures, with no stall in those runs, at the cost of 305 KiB of SRAM (section 7). A non-blocking
   ring for the console was measured and dropped: it removed the stall but added about 100 us to the worst case of every
   message. The stall is a message arriving during the blocking `[stats]` print (13.7 ms; E2's line was 11.5 ms); E2
   had the same hazard, and it is recorded, not fixed (section 12.8). **Blocks per wait:** the design's "a few blocks"
   was not true as designed (a 12 s install is 576,000 frames, 2,250 blocks, and a 48 s menu boot 9,000); the carry is
   now bounded at 4 blocks (section 5) and the rest is counted `skipped`.
4. **The stats are counted at the plan's edges**, the staging and the cart link, both the host's own. Edges suffice once
   the link can tell `rp.n8.nes`'s own owed note-offs from the plan's input (the `made` mirror, section 5). No node
   counter interface was needed; the loader's per-node `drops` stay available for diagnostics.
5. **The blob is byte-stable on this host** across builds and processes (two exports in one process, the tool in
   another, the goldens matched in `build/` and `build-asan/`). It depends on the document (node, wire and param order,
   the doubles yyjson parsed), the planner's deterministic orders (document order for nodes, Kahn's order with ties by
   document order for steps, buffers by lifetime from a free list), each type's settings as `rp.sdk.settings-json/1`
   writes them then `settingsToMsgpack`, the layout constants and the options; not on pointer values or any `sizeof`.
   Other hosts are owed a run.
6. **The bridge plan takes 30,560 bytes of arena at x86-64 sizes** (the export's figure; 80% of it the three MIDI
   queues: `rp.device.midi-in`'s output, the filter's matched output and the scratch queue every plan has, 8 KiB each),
   and 29,976 bytes on the M33 (3 nodes: 24, 36 and 600 bytes of instance; the box's boot line, on both rig runs).
   A later audio plan wants about 1 KiB per mono 256-frame buffer (the planner reuses buffers by
   lifetime), 1 KiB per param or control stream and 8 KiB per events edge at the default 256 / 4 KB, a delay line 4
   bytes a sample: a 20-node synth with 30 params and four MIDI edges is roughly 100 KiB, which fits the 128 KiB arena.
   What dominates a box plan is the desktop-sized queue and stream defaults, not the audio, so an audio plan will want
   smaller capacities on its MIDI edges.

### 12.6 What the reviews found that changed the design

- **Alignment.** `max_align_t` is 16 on the host and 8 on the M33, so a limit of 8 would have refused every type and a
  16 was invisible: `kDeviceAlign` and `kTargetAlign` (section 4). The "16-byte `max_align_t`" reason the estimate erred
  safe no longer holds as written; every size and alignment being at least the device's does.
- **Wired params** are flagged in the blob, since a stream a wire drives must not be pushed into.
- **The trial load** and the one-pass sizing replaced a count and a separate placement that could disagree.
- **`rp.host.midi-in` is not `unique`**: it is one per document by the engine's type-id check. A 32-bit wrap in
  `reserveMessage`'s sum, reachable from `rp.device.midi-in`, became a room-left check.
- **`capacity.match`** is restricted to an input of the output's own kind.
- **The host ends each block.** Without it a silenced `rp.device.midi-in` left its staging full and the box's commands
  jammed behind it; `endBlock` drops what a block did not take, counted.
- **The volume** was retried every second for ever when the read-back differed from the write; only a failed write is
  retried now.
- **The carry** is bounded (4 blocks) rather than thousands, and `arrival()` matches by frame and bytes.
- **`rp-device-check`'s plan pass** judged too little; moves, drops and the digest are now judged steps, and the
  exporter's `--packages` stops a box plan naming a type its image lacks from building.
- **The desktop digest literal** is compared only on the reference host, since FMA contraction may differ elsewhere.

### 12.7 The gate

Run by the controller on the prototype's image (BlipToaster, never `n8-midi.nes`), against section 13.7 of
[E2's design](modular-graph-phase-e2-design.md) as the baseline. It ran twice. The first run, on the flash-run image
(b36bf92f), passed everything but latency: five isolated-latency runs gave 742/1248, 1023/12589 (a 12.6 ms stall),
699/1198, 689/1223 and 700/1192 us average/worst, where E2's image on the same rig gave 558-653 / 1083-1097; that is
what led to the fix of question 3. The table is the second run, on the final image (copy_to_ram, normalised sha256
`ffadfb26...`), which is the one that ships.

| Leg | What it must show | E3 (final image) |
|---|---|---|
| boot | `[host] plan loaded: 3 nodes, <N> of 131072 arena bytes, 48000 Hz, 256 frames a block` and the three node lines | booted by the Pico 5.2 s after console start (E2 5.3 s); 3 nodes, 29976 of 131072 arena bytes (the export's estimate 30560); `expansion volume 128 written (reads back 128)` |
| `stress.sh` | every case's counts as E2's (decisions 1, 4 and 5; runstat 6/6; the SysEx case 2 SysEx / 4 messages), fail 0, overrun, framing, ring loss and `linkfull` 0, `skipped`'s growth across a case small (a count of frames, cumulative since boot: hundreds of thousands across a boot, about 150 to 1,100 per rerun; `stress.sh` does not judge it); isolated latency within E2's 1110 us worst and 587 us average (budget 1.5 ms) | 34 cases ok, the recovery case included; 0 lost, fail 0, ring full 0; `filtered` 6, the runstat real-time bytes, as designed; latency avg/worst 640/1086, 677/1114, 680/1098, 679/1128, 610/1104 us (E2 558-653 / 1083-1097); no stall in the five runs |
| audio on the L6's channel 3 | A4 440.30 Hz +1.2 c, C5 522.16 Hz -3.6 c, identical | identical: A4 440.30 Hz +1.2 c, C5 522.16 Hz -3.6 c, -32.84 dBFS |
| `faults.sh --rounds 8` | 8 of 8; stuck menus 0, recovery reboots 0, uploads 0 | 8 of 8, stuck 0, abandoned boots 6. Two earlier attempts never started: `loop.sh`'s 5 s power reset landed as a short off (the Zigbee plug's late cut), which leaves the N8 menu stuck at the first boot, the documented quirk; after a clean manual 16 s off the box booted by itself and the run passed. (On the first image it was 7 of 8 twice, each failure the same short-off quirk.) |
| the upload path | two ROMs staged, SysEx `7D 50` pick and an NES power-cycle: uploaded in about 4.9 s and booted; the next cycle booted the saved choice by CRC skip | pick 1 uploaded in 4922 ms, the next cycle saved 1 with no upload; pick 0 uploaded in 4930 ms (E2 4929), the next saved 0 with no upload; the volume written each time, the VRC6 ROM included |
| `rp-device-check` flashed once (e14a1849; the final image afd366d3, flashed once more, gave the same lines) | the four lines of section 9 | `pass, 20 types, arena 386192 bytes` (the delay default fits 400 KiB); `n8 protocol pass, 1390 bytes`; `plan pass, 10 nodes, 54360 arena bytes (the export's estimate 56064, the arena 409600), 48000 Hz, 256 frames a block: loaded, ran 48000 frames in 467 blocks with 8 param moves and 0 drops, unloaded`, 91 to 125 ms for that second of audio, digest `46e21691d4d54687` against the desktop's `b8854b2cc247318f` (they differ, as FMA contraction may); `stack 2688 of 4096 bytes at most`. The two images differ only in the arena's alignment, 16 and then `kArenaAlign` (8); the final one read 91142 us and digest `46e21691d4d54687` |
| the box flashed back | its image, as the gate ran it | the final image `ffadfb26...`, saved choice 0 (BlipToaster), left on the rig |

The acceptance gate was run again by the controller on the box image `ffadfb26...` at the follow-ups' HEAD (b28668095),
with the same figures: boot in 5.3 s with the plan loaded (29976 of 131072 arena bytes) and the volume written;
`stress.sh` with every case's counts as above, and the isolated latency within the budget on the reruns (616 to 680 us
average, 1034 to 1128 us worst), except for two outliers that are known hazards, not regressions: a 11.5 ms worst, a
message landing in the console's `[stats]` print stall, and a 2.5 ms worst, the ~2.5 ms sniff / health USB read (E2's
too); `faults.sh` 8 of 8; the audio at 440.30 and 522.16 Hz; the picks uploaded in 4922 and 4926 ms, each next cycle
booting the saved choice with no upload. The final `rp-device-check` image (afd366d3) was flashed once there (section
12.7's last row).

After a power cut under a live stream (stress's recovery case, `faults.sh`) a freshly booted ROM now receives a few
note-offs (`8c nn 00`) before anything else, paid by `rp.n8.nes`; both scripts send their `B0 07 7F` primer afterwards
anyway.

### 12.8 What is left

- **E4** and the audio version, as section 10; the live-upload item needs per-step port checks or a signature because
  the loader trusts the blob.
- **A stale exporter is guarded only by `loop.sh`.** `retroplug-export-plan` is in no default target, and the blob it
  writes lays out each node's port views by the package sources it was built from while the image compiles the sources
  as they are, with nothing in the blob for the loader to compare (`bindPorts` reads the views by ordinal). Adding,
  removing or reordering a port of `rp.n8.nes`, the filter or `rp.device.midi-in` and flashing with an old exporter
  gives a firmware that boots, prints "plan loaded" and reads the wrong views, and the trial load, which uses the same
  tool's device builds, does not see it. `loop.sh` refuses when a source the exporter compiles (`packages/*/native`,
  which holds node-sdk and graph-runtime, and the exporter's own directory, tests apart) is newer than the stamp
  the `retroplug-export-plan-current` target leaves beside the tool (the tool's own mtime does not move when a source
  it does not link changed), or the stamp is missing; `tools/build-box.sh` and `tools/build-device-check.sh` build that
  target. The structural fix is the per-node port signature of
  section 10, checked at load.
- **An audio cadence needs blocks off the main loop** (section 10): staging and the link's ring made
  single-producer / single-consumer, or the N8 calls made non-blocking, and `frameOf` on `BlockDriver`.
- **Windows, macOS and arm64 have never built E3.** The blob goldens' first run there is where question 5 is answered
  across hosts, so a red there is that answer, not a regression; the new libraries under `/W4 /WX` on MSVC
  (`retroplug-graph-exec-device`, the export tool, `retroplug-box-usb-cdc`, `packages/devtest`, `packages/device-io`)
  have not been built by MSVC or AppleClang; a Windows export may accept a `long double` type Linux refuses.
- **A time-reading plan on the elapsed driver loses skipped time** under load; the audio driver settles it.
- **The SRAM budget.** The box image takes about 305 KiB of the 512 KiB (286 KiB native) because it runs from SRAM.
  E4's MicroQuickJS heap and an audio codec's DMA buffers share the rest; the levers are the 128 KiB arena and moving
  cold code (the plan loader, the N8 protocol) back to flash, which would give back some of the latency question 3
  measured only for the code on a message's path.
- **The `[stats]` print stalls the loop.** A message arriving as a `[stats]` line starts waits for it (13.7 ms; E2's
  line was 11.5 ms), so the rare isolated message can read about 12 ms. E2 had the same hazard, a ring console that
  removes it costs about 100 us at the worst of every message, and it is recorded, not fixed. The sniff / health USB
  read (about 2.5 ms, E2's too) holds the loop the same way, so a message landing in it reads about 2.5 ms late;
  acceptance saw both as its latency outliers.
- **`RP_NATIVE_USB` has never run on hardware.** It builds and links under the trap; the rig is PIO-USB.
- **The trace's counter arithmetic as committed has run only in an emulator** (`N8_LAT_TRACE`'s wrap-safe differences
  were checked there, not on the rig).
- **The Launchpad's `threads` flag** is still the one node declaring `threads` for its link.
- **`rp.core.delay`'s default is refused by no rule that names it.** It is 48,000 samples, as everywhere, and a plan
  holding it alone needs about 396 KB: the box's 128 KiB arena refuses it, at export and at boot, and the 400 KiB
  arena of `rp-device-check` holds it, whose per-type check runs it. Both built-in plans set `maxSamples`.
- **`rp::list`'s JSON schema** writes no `maxItems`, so the UI does not see the filter's bound of 10.
