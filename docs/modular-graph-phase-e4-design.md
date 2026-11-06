# Phase E4: TypeScript nodes on the box, on MicroQuickJS

**Date:** 2026-11-06 · **Branch:** `feature/modularize` · **Status:** built (E4, five commits); section 11 records
the build, answers section 10 and carries what was left, and sections 1 to 9 are corrected in place

Phase E is the Pico runtime ([modular-graph-phase-e1-design.md](modular-graph-phase-e1-design.md) section 3). E1 made a
C++ node's source build for the RP2350, E2 put the first pack behind the package boundary, and E3 made a graph run on
the box: the desktop exports a plan as a blob, the firmware loads it into a static arena, and the box's bridge is a
built-in plan ([modular-graph-phase-e3-design.md](modular-graph-phase-e3-design.md)). E4 lets a TypeScript node run in
that plan beside the C++ ones, on MicroQuickJS (mquickjs), and moves BlipToaster's MIDI policy into such a node. E1 set
its gate: a TS node in the box's MIDI path on the rig.

## 1. Before E4

- **mquickjs is not on this branch.** Bellard's MicroQuickJS (pinned at 6d4d7eb) is used on the unmerged
  `feature/launchpad` branch, whose `check:portable` bundles the Launchpad core, lowers it to ES5, scans for library
  calls the engine lacks and runs it under Node and `mqjs` for byte-identical transcripts. The engine runs in one
  caller-supplied buffer with a compacting garbage collector and no malloc; its JavaScript is ES5 plus typed arrays,
  `ArrayBuffer`, JSON, RegExp and closures, and lacks Map, Set, Symbol, Proxy, Promise, BigInt and most ES2015 library
  calls. It compiles bytecode ahead of time (`mqjs -o`, `-m32` for a 32-bit target), which runs without verification,
  so only trusted bytecode may run. Its C functions live in a stdlib table compiled to C structures by its build tool.
  Its README gives about 100 KB of Thumb-2 code and a heap from 10 kB; nothing has been measured on the RP2350.
- **TS nodes run only on the desktop, in full QuickJS.** The script host (`packages/native/src/host/script/ScriptHost`,
  B3a and C1) gives each pack a pair of runtimes, reads settings as JSON only and answers no interfaces, so `exportPlan`
  refuses a script node (`settings-export`). Its block API relies on Promise jobs, Proxy, a frozen prototype and
  `ArrayBuffer.transfer`.
- **`defineNode` is not portable.** It uses Set, `includes`, `find`, `Object.entries`, `startsWith`, `endsWith` and
  `Number.isInteger` (spec/07 records it).
- **The box runs from RAM.** E3's image takes about 305 KiB of the RP2350's 512 KiB SRAM under `copy_to_ram`, which is
  what holds E2's latency; the 128 KiB arena holds the bridge plan's 30 KiB.

## 2. Decisions taken

The user's, in order (2026-11-06; the Phase E list is `.superpowers/e-pico/decisions.md`):

1. **Purpose: MIDI- and event-rate logic.** TS nodes on the box transform, route and generate events, and hold a
   cart's policy; audio-rate DSP stays C++. Box control logic may become nodes later.
2. **One engine per pack wherever it runs.** A pack marked for the device runs on mquickjs on the desktop as well, in a
   second, small script host beside the QuickJS one, so the desktop runs the engine and the source the box runs.
   Desktop-only packs stay on QuickJS.
   *As built:* the desktop runs the engine and the source the box runs, with one difference of scope. Every node's
   context, on the desktop, in the exporter's trial load, in every device-mode test and on the box, loads the pack's
   **device runtime bundle** (the definitions without the settings parse or the descriptor, which the box does not
   need); the desktop's **control context** (one per pack per process) loads the whole pack, for registration, the
   descriptors, `parseSettings` and `layout_for`. The toolchain holds the two bundles to the same output.
3. **Packs are built into the firmware.** Each pack the box lists is compiled at the firmware's build, TypeScript to
   ES5 to mquickjs bytecode, and embedded beside the exported plan; the box registers its types at boot and the blob
   names script types by id. The blob's format does not change, and the bytecode is trusted because only the build
   makes it.
   *As built:* a pack's bytecode is embedded in the firmware's **flash** (`.flashdata`, which `copy_to_ram` leaves
   there) and copied at setup into the pack's own memory in the arena, where it is relocated; the type ids are recorded
   in the pack's generated source, so registration at boot runs no JavaScript. The bytecode is the pack's 32-bit device
   runtime (6,988 bytes for `bliptoaster`, 6,992 placed).
4. **Everything runs from RAM.** mquickjs and the bytecode run from SRAM like the rest of the image, so latency holds;
   the arena shrinks toward what the box's plans need to make room. Moving cold code back to flash is the reserve
   lever if the measured figures do not fit.
   *As built:* the arena is **65536** bytes (the box's plan takes about 57 KB of it, a pack's heap inside), the
   pack's bytecode stays in flash until its setup copies it, and the engine is compiled at **-Os** (98.7 KB of code and
   rodata in RAM; the same sources at -O3 are 159 KB of code, 83 KB against 159 KB like for like). The image takes
   347.5 KiB of the 512 KiB SRAM (section 5), and no cold code went back to flash. The latency held to the figures of
   section 8, not to E3's exactly.
5. **One context per pack.** Each pack's manifest declares its heap; the exporter and the loader size and place it like
   everything else, and refuse a plan that does not fit, naming the pack. A heap that runs out, a throw or a deadline
   overrun silences only that pack's nodes.
   *As built:* `heapBytes` in the manifest is the **box's 32-bit context**, the authority. A 64-bit device build (the
   exporter's trial load, the device-mode tests) gives a pack 3/2 of it, a **heuristic** the toolchain's check also
   applies; the desktop's hosts give a context of at least 1 MiB (four times `heapBytes` past 256 KiB). The box's
   boot line and the device check give the real figure. A throw or a deadline overrun faults only its node; a heap that
   runs out kills the pack's context, and every one of its nodes faults with the same words.
6. **One `defineNode`, made portable.** The same API serves every pack. For a pack marked for the device, the
   definition refuses audio ports and anything asynchronous.
   *As built:* an audio port is refused at definition in a device pack (a bundle-time constant, `RP_DEVICE_PACK`);
   an `async` or `Promise` is refused by the toolchain's scan; a `create()` or `process()` that returns a promise or a
   thenable faults at its first call. `defineNode` gained `idle: "skip"` and the block API `forward()`, which the
   latency needed (section 4).
7. **BlipToaster's policy becomes a TS pack.** A small `bliptoaster` pack holds the cart's MIDI policy and replaces
   `rp.core.midi-filter` in the box's plan; the C++ filter stays in `rp.core` for every other use.

## 3. The shape

| Piece | Where | Built for |
|---|---|---|
| mquickjs at 6d4d7eb, upstream's files unpatched with their LICENSE | `deps/mquickjs` (vendored, not a submodule), built by `cmake/mquickjs.cmake` | desktop and device |
| Our engine wrapper, standard library and table: the symbol rename, the fixes, `rp_mqjs_stdlib.c` | `packages/graph-runtime/native/src/script/` | desktop and device |
| The device-pack toolchain: bundle, lower to ES5, scan, run on our runner, compile to bytecode | `packages/retroplug/scripts/build-device-pack.mjs`, `retroplug-mqjs-run`, CMake | desktop, at build time |
| `defineNode` and its helpers in the mquickjs subset, with the device-pack checks | `packages/retroplug/src/graph` | both engines |
| The mquickjs script host, `MqjsHost` | `packages/graph-runtime/native/src/script/`, beside the loader | desktop and device |
| Package-level memory | the node SDK (`rp_package`, `rp.host.packages/1`) and the loader | desktop and device |
| The `bliptoaster` pack and its policy node | `packages/bliptoaster` | desktop and device |
| The box: the pack embedded and registered, the arena trimmed, the new plan | `pico/n8-host`, `pico/host` | device |

At build time a device pack's TypeScript becomes bytecode (the whole pack, for the desktop's control context, and the
device runtime, at both widths), which the generated C sources (`pack64.c`, `pack32.c`) carry
with the type ids; the box's image embeds the 32-bit runtime in flash. At boot the box registers each pack's node types
beside the C++ ones; the loader sizes and places each pack's memory and every node's state in the arena, and sets the
pack up (copying and relocating its bytecode into a private context) before any of its nodes is created; each block,
the executor calls a script node through the same C ABI as any node, and the host runs its `process(block)` in its
pack's context under a deadline. On the desktop a device pack goes through the same toolchain and the same host, so the
exporter's trial load and the device-mode tests run real script nodes.

## 4. The mquickjs script host

**The vendored engine and our wrapper.** `deps/mquickjs` holds upstream's tracked files at the pin, unpatched; the
generated tables live in the build tree, never beside them. Everything of ours around the engine sits in
`packages/graph-runtime/native/src/script/`:

- **`rp_mqjs_names.h`** renames the engine's 197 external names to `rp_mqjs_*`, force-included into its sources and
  `#include`d by `rp_mqjs.h`, because every desktop product links this engine beside QuickJS and 46 names clashed
  (`JS_Call`, `JS_NewContext`, `js_dtoa`, ...). `cmake/mquickjs-names.cmake` fails the build if the library defines
  any other external name (Linux and macOS; MSVC has no `nm`).
- **`rp_mqjs_engine.c`** includes `mquickjs.c` whole and carries the fixes as wrappers (renaming upstream's definition
  at the preprocessor and defining ours under the old name) and one helper the host calls
  (`rp_mqjs_keep_bytecode_names`). Two are real upstream GC bugs: the run-time property-name table can corrupt itself
  when a collection runs inside its own growth (in `JS_MakeUniqueString`), and a loaded bytecode's string constants,
  marked unique in place, drop out of that table at the next collection (the host marks them kept after relocation).
  Five are exceptions upstream swallows: `String.prototype.replace` ignoring its callback's throw, `string_buffer_puts`
  losing an out-of-memory, `hasOwnProperty` ignoring a throwing key, `Array.prototype.sort` taking a comparator's throw
  (the deadline's interrupt among them) for a comparison's result, and `String.prototype.repeat` answering -1 for a
  count whose conversion throws. One is a report: the engine's check for room (`check_free_mem`, which every allocation
  and every push onto the context's stack makes) tells the host when it finds none after a collection (`rp_mqjs_api`'s
  `ran_out`), since nothing it throws can say the heap ran out (a script may throw `null` or an error saying "out of
  memory"). And `RegExp.prototype.exec`, when its capture array cannot be made, popped a collector root it never pushed,
  setting the context's root list to a stack word: the wrapper sets the list back as exec returns. Typed arrays are
  built in the engine, with no `JS_Call`, so `messages()` works at any depth. **The fixes are guarded by a
  configure-time hash**: `cmake/mquickjs.cmake` hashes the text of each span the wrappers replace (CR stripped) and
  fails on a change naming the file, and counts `js_call_rec_count` mentions (exactly four) and `check_free_mem`'s
  (three), so a bump of the pinned source cannot silently drop one.
- **`rp_mqjs_stdlib.c`**, our standard library for the build tool. It includes upstream's `mqjs_stdlib.c` unpatched
  with its REPL global object and `main` renamed out of the way, so the standard classes are upstream's own tables, and
  its own global object leaves out `print`, `gc`, `load`, the timers, `Date`, `performance` and `eval`. It adds a
  `console` (`log` only) and the block API's classes (`RpBlock`, `RpMessage`, `RpTransport`, `RpControlStream`,
  `RpControlChange`), whose constructors refuse. `rp_mqjs_table.c` holds the generated tables with two dispatchers that
  hand every method and getter to the host through the context's opaque, so one table serves both builds of the host.
- **`rp_mqjs_device.c`** wraps `abort` (newlib's reaches `_malloc_r`, which the link trap forbids) to the SDK's panic;
  a forced-include shim defines the `PRI*64` macros this toolchain's freestanding `<stdint.h>` lacks;
  `cmake/device-image/no-unwind.c`, linked into every device image, replaces the ARM unwinder `setjmp` drags in with a
  trapping definition (3.8 KB of the box's RAM).

**Registration.** A pack's source records its type ids, its heap and its bytecode. On the device the host registers
the types from the recorded ids and runs no JavaScript; setup checks the bytecode's definitions against the ids and
refuses a mismatch naming the type. The desktop's control context runs the whole pack once per process to answer
`descriptor` and `layout_for`. The host makes one `rp_node_type` per definition in one `rp_package` per pack, with the
node's `query_memory` the native shim and the pack's memory declared at package level (section 5). The device build
holds `RP_MQJS_MAX_PACKS` x `RP_MQJS_MAX_TYPES` in static storage (the box sets 1 x 1; the default is 4 x 16).

**Settings.** The desktop host answers `rp.sdk.settings-json/1` from the control context, so `exportPlan` writes a
script node's settings as msgpack as it does for a C++ node. On the device the host decodes the msgpack into a JS
object (depth at most 8, a field name under 64 bytes) and hands it to `create()`; the export applied the defaults.
The decode runs on the block thread (a node's first block makes its instance) with no recursion, so its frame is the
same however deep the settings nest.
The desktop `check()` refuses what the device would not read, so the trial load sees it. A settings change on the
desktop swaps while the layout holds and rebuilds otherwise; this host takes no reshape.

**The block API** is today's (`messages()`, `send()`, `control()`, `emit()`, `frames`, `transport`) plus
**`forward(port, message)`**, which is `send(port, message.frame, message.data, message.flags)` read natively for a
message the running block handed out. Message, change and stream objects are user-class objects with getters; a block
sets no JS property. A node's input pools are **preallocated to its queue's capacity at its first block** (a message
object per place with Uint8Array views of 1 to 3 bytes, the power-of-two arrays), so the pack's heap need is fixed by
the plan and the trial load measures it. A warmed block then allocates nothing at 64 bits, except a payload longer than
3 bytes (its view is made for the block); at 32 bits MicroQuickJS boxes every number that is not a 31-bit integer, so
reading a non-whole stream start, change value or tempo allocates. An object kept past its block never reads out of
bounds: it throws a TypeError when the running node has no port of that ordinal, side and kind, and otherwise reads
that port.

**`idle: "skip"`.** A definition may declare it: the host returns before entering the context on a block with no
message on any events or bytes input, no change on any control input or param, each of those streams starting where it
stood at the end of the node's last call (the host keeps those values natively: on this host a double for each of up
to 64 ports, 512 bytes of each script node's instance), and no activate or settings hook waiting. The node's first
block always runs. A stream starts elsewhere only in a new plan (a rewired or disconnected input's start), so a kept
node's first block there runs when a start moved, whichever plan step it comes from: a plan's steps are no mark of a
new plan, since a host with no audio thread reclaims a replaced plan at once and a plan two re-plans on can take its
memory. A definition with an audio port or with no input to wake it is refused. The node keeps no time of its own,
which no host can check; the contract says so. On the box an idle pass is estimated at about a microsecond (68
instructions, before the starts' compare: a few more a control input or param); `bliptoaster` sets it.

**Faults.** A node that throws faults, with no retry (the box has no one to retry for): the executor silences it and
the box prints it once, with the throw's words on a second `[host]` line. A getter's throw on what the host reads of a
call's result (a result's `then`, an instance's `process` and hooks), or of what `send` and `forward` are handed (an
array-like's `length`, an object's `frame` and `flags`), is the node's like any other, and an entry that
ran past its deadline faults with the deadline's words whatever it returned, since a builtin or a getter may drop the
engine's interrupt on the way out (the host checks at the end of every outermost entry). The host's interfaces are
`rp.script.fault/1` (reason; kind: threw, deadline, heap, refused), `rp.script.heap/1`, `rp.host.clock/1` and
`rp.host.script-log/1` (where `console.log` goes). A pack whose heap runs out (the engine's report, in an entry that
then ends in an exception) has a dead context, read without allocating, and all of its nodes fault with the same words;
on the desktop the pack is set up again once its last node is gone (on the host's one control thread, which creates
and destroys its nodes: `graph/PackageMemory.hpp`). What a node throws does not decide it: `throw null`, or an error
saying "out of memory", faults that node alone.

- **The deadline** is enforced by the engine's interrupt handler against the host's clock: **10 ms a call plus 250 us
  for each message on the block's events and bytes inputs** on the box (26 ms for a full queue of 64), which is the
  author's budget; the device check measured 65 us a note and 48 us a 64-byte SysEx from flash. A host with no clock
  times by the thread's CPU time at **500 ms** on the desktop (Windows counts thread time in 15.6 ms ticks, so a
  deadline must be many ticks to be measured at all). A reading of 0 is no reading.
- **The polls.** MicroQuickJS polls the handler once every so many units of work, 10000 by its own count: each call and
  branch of its interpreter, and (RetroPlug's) each allocation and each push of a frame; a builtin's own work between
  two polls is otherwise unpolled. At that interval a loop over a builtin ran 10000 of its calls past the deadline (`for
  (;;) a.sort()` over 300 numbers in bliptoaster's heap ran 0.7 s past 10 ms on the desktop, seconds on the box, past
  its 3 s watchdog). So the handler sets the next poll's distance (`rp_mqjs_set_poll_interval`): an entry first polls
  after its deadline in microseconds over 2048 units (at least 8: 8 on the box's 10 ms, 244 at the desktop's 500 ms),
  each poll before the deadline sets the next twice as far, but never past the deadline in microseconds over 64 units
  (156 on the box, 7812 on the desktop, 10000 at most) and no further than the rate since the last poll says fits in
  half of what is left; a poll past the deadline sets the next at once, so an interrupt a builtin dropped is thrown
  again at the next call or branch. The builtins whose one call can run long poll in their own loops, off the same
  count: the sort a comparison, the string searches (`indexOf`, `lastIndexOf`, and the search of `replace` and `split`)
  a position, `join` on what is no array (whose length nothing bounds) an element, `match` a match (and it moves past an
  empty match, where upstream's added the same one until the heap ran out). **What the bound is:** a node faults at most
  a cap's units after its deadline (each a call, a branch, an allocation, a push or a builtin's loop step, the costliest
  a collection, linear in the heap) plus the rest of the one builtin call the count ran out in (a poll waits for the
  interpreter's next call or branch), linear in what the pack's heap holds. The cap is what holds a phase change (plain
  code whose cheap units let the polls grow apart, then a loop over a builtin such as a forEach over a plain callback or
  JSON.stringify, whose calls and allocations count). On the desktop each of the four loop kinds, after plain code too,
  faulted within about 50 us of a 50 ms deadline; the device check times seven loops on the M33 in a context of
  bliptoaster's heap, two after plain code, and judges the worst against a third of the watchdog. The residual: a single
  builtin call that neither allocates nor polls and is not bounded by the heap would run to its end; none is known in
  RetroPlug's standard library. A node that finishes in microseconds reads the clock a few times a block (the desktop's
  bench: a transpose's 16-message block about 272 to 280 ns a message at the desktop's deadline, 305 at the box's 10 ms
  where its polls come every 156 units; 256 messages, 245 to 253 and 265).
- **The re-entry cap.** C calls back into JavaScript (`replace`, `forEach`, a sort comparator) nest through the engine's
  `JS_Call`, 290 bytes of the box's 4 KB stack a level. A node's context is capped at **4** re-entries after its load
  (`RP_MQJS_MAX_REENTRIES`), and the box builds with `PICO_USE_STACK_GUARDS=1`, so a node nesting to the cap leaves
  about 1.7 KB for the interrupts and an overrun stops the box for the watchdog to restart.

**One library, two widths.** The host compiles for x86-64 on the desktop and for the M33 on the box, over the same
stdlib table and the same pack source; only the bytecode's width differs (64-bit on the desktop, `-m32` on the box).
The bytecode carries no reference into the tables it was compiled against: at setup every unique string is rebound, by
content, to the firmware's own ROM atoms.

## 5. Memory, the loader and the arena

**Package-level memory.** `rp_package` gains `memory` (name, `query`, `query_target`, `setup`, `teardown`) and `ctx`,
and `rp.host.packages/1` gives a node its package's block; a C++ package leaves them null. A script pack's block is
`[host state][the context's own copy of the bytecode][the context, heapBytes]`, each 8-aligned, the state a reserved
448 bytes (416 on the RP2350). **Every context runs a private copy of its bytecode**: running writes into it (a string
constant used as a key is marked unique in place), so the copy is made and relocated at setup, and the image's array
stays in flash.

**The heap** is declared in the pack's manifest as `heapBytes`, a whole number from 4096 to 524288, the **box's 32-bit
context**. The least a context can be is 4096 bytes at 64 bits and 2560 at 32 (below it the engine writes past its
buffer rather than throwing), and the host and the loader's query refuse below it in words. A 64-bit device build
(the exporter's trial load and the device-mode tests) gives a pack 3/2 of `heapBytes`, a heuristic that fails for a
pack whose heap is mostly payload bytes (`bliptoaster` ratios 1.64 under full SysEx) or mostly floats (boxed at 32 bits
only); the toolchain's check applies the same 3/2, printing "the check would pass X". The box's boot line and the device
check are the authority. The desktop's hosts give a context of at least 1 MiB (four times `heapBytes` above 256 KiB).
The desktop host reports a pack's use through `rp.script.heap/1`.

**Sizing and placing.** The loader finds the packages the plan's node types come from, sizes each package's memory once
in the same pass that sizes everything else (at the target's width, through `query_target`), places it in the arena
after the plan's structures and before the nodes, in the order the plan first names one of its types, sets the package
up before any of its nodes is created and tears it down after the last is destroyed. The nodes are created under a host
of the loader's own that answers `rp.host.packages/1`. A plan whose packages do not fit is refused naming the pack and
the overrun (codes `package-memory`, `package-setup` and, for an overrun, `arena`); the exporter runs the same sizing,
so a box plan that cannot fit fails the firmware build. The blob's format is unchanged.

**The arena** is **65536 bytes** (E3: 131072). The export's estimate for the box's plan is 57,800 (25,784 for the
bridge's three nodes and queues, and 32,016 for the pack: 448 + 6992 + 24576); the box loaded it into about 56.6 KB
before a script node kept its streams' ends for the idle skip (section 4), 512 bytes more.

**The SRAM budget**, measured, and recorded in `tools/build-box.sh`'s size line:

| | E3 | E4 (the PIO-USB image) |
|---|---|---|
| Code and data run from RAM | 121.5 KiB (100,628 + 23,832 bytes) | 229.7 KiB (189,112 + 46,056 bytes; mquickjs 98.7 KB, the host 17.8 KB) |
| Arena | 128 KiB | 64 KiB, the pack's 32,016 bytes inside it |
| Other bss | 55.6 KiB | 53.8 KiB |
| In all | about 305 KiB (312,484 bytes) | 347.5 KiB (355,824 bytes) of 512 |

What remains for the audio version's buffers is 168,464 bytes (164.5 KiB); the native-USB image takes 335,880. No cold
code went back to flash.

## 6. `defineNode`, the toolchain and the `bliptoaster` pack

**`defineNode` made portable.** It and its helpers (the `s` schema, the descriptor) are written in the mquickjs subset:
plain objects for Set, loops or local helpers for `includes`, `find`, `Object.entries`, `startsWith`, `endsWith` and
`Number.isInteger`. Desktop packs behave exactly as before, and the descriptor goldens, `buildGraph`'s goldens, the
protocol nodes' recorded digests and parity hold unchanged as the proof.

**Device-pack checks.** The manifest's `device: true` makes the toolchain bundle with `RP_DEVICE_PACK` defined, so the
answer sits inside the bytecode and every host agrees. For such a pack `defineNode` refuses an audio port at definition,
naming the node and the port, and a settings field named like an integer (mquickjs orders integer-like keys by
insertion, V8 puts them first). The **device runtime** (`RP_DEVICE_RUNTIME`) keeps `{id, ports, create, layout,
idle}` and throws on `parseSettings` and `descriptor`; a pack that calls them at module scope is refused. A `create()`
or `process()` that returns a promise or a thenable faults at its first call (the host).

**The toolchain** (`build-device-pack.mjs`, one run per pack, in a work directory of its own, its outputs written only
on success):

1. esbuild bundles the entry (an iife) against a module allowlist (the pack's directory and the portable SDK);
2. the repo's pinned TypeScript 5 lowers it to ES5;
3. a scan refuses library calls mquickjs lacks, naming the call and its source line through the source maps; it covers
   the bundle, the lowered helpers and the whole SDK, a name read behind its own `typeof` guard passes, host globals
   (`setTimeout`, `print`, `console.warn`, ...) are refused, and `async` and `Promise` are named at their own line;
4. a Node reference run prints every node's descriptor (a definition refusal is reported here, at build time);
5. `mqjs -o` compiles the whole pack and the device runtime, at 64 bits, and the device runtime at 64 bits and with
   `-m32`;
6. `retroplug-mqjs-run`, the runner over **our** stdlib, runs the transcript (Node's and the engine's must agree
   byte for byte; CRLF normalised) and the heap check: it sets the pack up as a host does, creates one node of each type
   from its default settings and runs a five-block cycle at the device MIDI input's capacity (64 notes; 64 SysEx of 64
   bytes; two sweeps covering every first byte; an empty block), then searches for the least heap that run needs. The
   whole pack's and the runtime's digests must be equal, and each type's default layout printed from both bundles.

The outputs are `<name>.js`, `.mqjs64.bin`, `.mqjs32.bin`, `.device64.bin`, `.device.js`, `.transcript.txt` and the
`pack64.c` / `pack32.c` sources. CMake runs the toolchain as `bundles.cmake` runs today's script bundle
(`retroplug_device_script_pack`).

**The manifest.** A script pack's `package.json` declares `retroplug.script`: `entry`, `device: true` and `heapBytes`;
`device: false` is refused as reserved. A package may have `native`, `script` or both. `retroplug_package` builds the
pack for every manifest, `retroplug_product` registers a listed package's script pack beside its native entries, and a
device build takes the pack from the desktop build the exporter came from (`RP_DESKTOP_BUILD_DIR`, derived from
`RP_EXPORT_PLAN_TOOL`). `RP_SCRIPT_HEAP_BYTES_<package>` overrides a pack's heap in a device image's registration copy
(the device check sets devtest's to 45056), refused for a package with no script pack.

**The `bliptoaster` pack** (`packages/bliptoaster`, `heapBytes` 24576) holds one node, `rp.bliptoaster.midi-policy`,
with no settings, `idle: "skip"`, and `in: events.in()` / `out: events.out({ match: "in" })`: it drops polyphonic and
channel aftertouch, system common and realtime (46 of the 257 first bytes the twin test sweeps) and passes every other
message unchanged, its frame, flags and order kept, with `forward()`. The box's plan is `rp.device.midi-in` ->
`rp.bliptoaster.midi-policy` -> `rp.n8.nes`, and `rp.device.midi-in` now carries **64 messages** (and 4096 bytes, so a
4 KB SysEx is still one message), because the pack's pools follow its input's capacity (42,192 bytes at 256 places at
32 bits, 19,296 at 64 places). The cart link keeps its 512. The desktop products list the pack, so the node can be
auditioned there, and their `[packages]` rows gain it. The desktop's own N8 graphs (`buildGraph`) do not use it in E4.

## 7. Tests

- **The mquickjs host on the desktop** (`[script-pack]`, 42 cases, most in `ScriptPack.test.cpp`, run on both builds
  of the host): a node's lifecycle, settings and block API; each fault (a throw, a deadline overrun, a loop over each
  kind of long builtin faulted within a small multiple of its deadline at bliptoaster's heap and devtest's, a getter
  the host reads, a heap that runs out silencing only its pack while a `throw null` faults only its node, the dropped
  exceptions, the re-entry cap, the allowance per message); the idle skip read against what the node's last call read,
  whichever plan step a block comes from; kept objects; a warmed block allocating nothing, counted by the C allocator
  and by the JS heap's growth; the threaded cases under ThreadSanitizer; `[debug-gc]`, the runner over the engine
  built with a collection at every allocation held to the ordinary runner's digests.
- **`retroplug-device-mode-test`**: 87 cases (45 before E4, 84 at its five commits). The box's plan with the
  `bliptoaster` pack, exported and loaded into an arena over the device-mode builds and run in ragged blocks, renders
  what GraphHost renders; a pack heap's sizing and its refusals; a C++ package without package memory unchanged;
  `box-plan.hex` re-recorded.
- **The twin test** (`[bliptoaster]`): `rp.bliptoaster.midi-policy` against `rp.core.midi-filter` with the same kinds
  dropped, message for message, over every first byte and 400 seeded blocks, desktop and device builds of both.
- **`defineNode` on both engines:** the TS tests on Node, and `device-pack.test.ts` (the toolchain's refusals, each with
  its message, and the scenario's transcript identical on Node and as bytecode).
- **The device builds:** `rp-device-check` runs a script pack through the loader on the device (section 11.4);
  `device:check` and `device:box` link mquickjs under the 87-symbol trap (`setjmp` and `longjmp` pass, `abort` is
  wrapped) with `-Werror=stack-usage=1024` on its sources, no frame needing E3's per-source remedy.
- **Held:** parity, the kit and render goldens, the existing descriptor and `buildGraph` goldens, the protocol nodes'
  digests and `edio-golden.json`. The intended changes are the `[packages]` rows gaining `bliptoaster`, `box-plan.hex`,
  and the new goldens (the policy's descriptor and `device-check-script-plan.hex`).

## 8. Landing

Five commits, each leaving every suite green:

1. **`defineNode` made portable** and the device-pack toolchain.
2. **mquickjs and the script host** on the desktop, and package-level memory in the SDK and the loader.
3. **The `bliptoaster` pack and the box:** the pack embedded and registered at boot, the arena trimmed, the new
   `plan.json`.
4. **`rp-device-check` with a script pack.**
5. **Docs.**

**Acceptance:** every suite with and without the fixtures, parity strict, the kit and render goldens under their
require flags, the sanitizers, `device:check`, `device:box` and the CI dry run, a fresh tree, the live hosts and the
consumers. Then the hardware, by the controller, BlipToaster for every leg: E3's Pico gate with the TS node now on every
message; the console showing the pack's heap use; and `rp-device-check` flashed once with its script-pack lines, then
the box flashed back. **Latency** is held to E4's measured figures, not E3's (the user's decision, 2026-11-06, after the
first gate read near E3's line): the script node costs the isolated-latency path one block's base plus a message
(estimated at about 40 us: callgrind on i386, scaled by the box's first-block timing). The runs measured **averages of
550 to 743 us (E3: 610 to 680) and worsts of 1122 to 1170 us (E3: 1086 to 1128)**, so averages are near E3's and the
worst about 50 us higher. The regression line is therefore **a worst above about 1200 us, or averages above about
780 us**, to report and fix; the worst is the primary signal, since averages vary about 190 us from run to run. A lone
run of several milliseconds is the `[stats]` print's known stall (section 11.8), not a regression.

**How it is built** (as E1 to E3, Anthropic models only): a verified prototype on the local branch `proto/e4-mqjs`,
stages 1 to 4 by Opus and stage 5 by Sonnet, each reviewed by Opus with a fix pass, the Pico gate run by the controller
on the prototype's image; the plan generated from the prototype, replayed to identical trees and reviewed by Opus;
Sonnet implementers, Opus code reviews, Sonnet acceptance, the controller's hardware legs, an Opus final review. Work
files in the gitignored `.superpowers/e4pro`, `e4plan` and `e4exec`.

## 9. Left beyond E4

- **Packs in plans that are not built in:** a plan on the SD card or a live upload that brings its own packs needs the
  per-node port signature E3 recorded and a trust model for bytecode, which mquickjs does not verify.
- **The desktop's N8 graphs** using `rp.bliptoaster.midi-policy` in place of their own path.
- **`feature/launchpad`** adopting the toolchain for its portable core.
- **The audio version** (E3 section 10), whose RAM this design budgets around.

## 10. Prototype questions

1. mquickjs's code size and RAM on the RP2350, and its stack frames under the 1 KiB limit.
2. The per-message cost of a script node in the MIDI path, and the garbage collector's behaviour with message objects
   reused.
3. Bytecode placement under `copy_to_ram`: mquickjs wants bytecode relocated to the address it runs from; whether the
   build relocates it to its RAM address or the box relocates it at boot.
4. Keeping the stdlib table the bytecode was compiled against equal to the firmware's (mquickjs checks its version, not
   its atoms).
5. The arena's final size, and the pack's real heap use against its declaration.
6. Whether `setjmp`, `longjmp` and `abort` pass the box's link trap as they are, or need wrapping.

## 11. As built

Five commits on `feature/modularize`: the toolchain, the script host and package memory, the `bliptoaster` pack and
the box, the device check on a script pack, and these docs. Parity bit for bit, the kit goldens, the render baseline,
the descriptor goldens of existing types, `buildGraph`'s goldens, `edio-golden.json`, `plan-blob.hex` and
`device-check-plan.hex` were untouched throughout, and none of them was re-recorded; the golden changes are the
intended ones (the `[packages]` golden gaining `rp.bliptoaster.midi-policy` in each of its nine products,
`box-plan.hex`, and two new goldens: the policy's descriptor and `device-check-script-plan.hex`). Sections 1 to 9 are
corrected in place where the build made them false. The counts: `retroplug-device-mode-test` ends at 84 cases (the
baseline's 45; 1,628,820 assertions under ASan, 39 of the cases `[script-pack]` and run under ThreadSanitizer),
`retroplug-engine-graph-test` at 334 cases (333), `pnpm test` at 175 files and 1792 cases (174 and 1782), parity
(250), native, ui and the render and kit legs unchanged. `device:check`'s image went from 130232 / 2000 / 416830 flash
/ data / bss to 277702 / 2004 / 421515, and the box's two images from 101200 / 23832 / 188020 (PIO-USB) and 101576 /
9836 / 181788 (`RP_NATIVE_USB`) to 196688 / 46056 / 120600 and 197056 / 32020 / 114368, of which 189112 and 189480
bytes of the flash run from RAM, the images taking 355824 and 335880 bytes of SRAM in all (312484 and 292636).

### 11.1 `defineNode` and the toolchain (commit 1)

- **`deps/mquickjs`**: upstream's tracked files at 6d4d7eb, unpatched, LICENSE kept (vendored, not a submodule: no
  fetch for a fresh clone and the pin cannot drift, since the bytecode is only as stable as the compiler).
  `cmake/mquickjs.cmake` builds `mqjs_stdlib` and `mqjs` and generates the host-width tables into the build tree
  (`cmake/mquickjs-table.cmake`); the Windows path (clang-cl objects, a `sys/time.h` shim) is written to SameBoy's
  pattern and unbuilt.
- **`defineNode.ts`** in the mquickjs subset (section 6), the `RP_DEVICE_PACK` refusals, and
  **`scripts/build-device-pack.mjs`** with the steps of section 6.
- **The manifest's `retroplug.script`**, its refusals as scratch manifests (`packages/native/test/packages/refused`),
  and `packages/devtest/ts` as the seed pack (`rp.devtest.script-transpose`).
- **Tests:** `device-pack.test.ts`, the scenario's 84-line transcript identical on Node and as bytecode, and every
  refusal with its words (a missing library call, an `async`, an audio port, an import outside the allowlist, a
  host global, a guarded helper, a runtime bundle that differs).

### 11.2 The script host and package memory (commit 2)

- **`MqjsHost`, `rp_mqjs_stdlib.c`, `rp_mqjs_table.c`, `rp_mqjs.h`, `rp_mqjs_pack.h`** in
  `packages/graph-runtime/native/src/script/`, and `retroplug-mqjs-engine`, `-host`, `-host-device` (the device compile
  in the ordinary build) and `retroplug-mqjs-run`, the toolchain's runner.
- **`rp_package`'s `memory` and `ctx`, `rp.host.packages/1`, `graph/PackageMemory`** (the desktop's answer, torn down
  after `live_.clear()`), the loader's sizing and placement (`PlanSize` gained `structureBytes`, `packageBytes`,
  `packages`), and `rp_script.h` for the host interfaces.
- **Tests:** `ScriptPack.test.cpp` (`[script-pack]`), a hidden `[script-bench]`, two fixture packs
  (`script-packs/scripttest`, `hog`), the threaded cases under TSan.
- **Size:** the loader got smaller with the package pass in it (the text builders out of line): the check's image went
  130232 to 126641 flash, the box's 101200 to 95836; nothing of the host was linked yet.

### 11.3 The `bliptoaster` pack and the box (commit 3)

- **`packages/bliptoaster`**, `BlipToaster.test.cpp` (the twin, the descriptor golden, the pack's heap holding 64 full
  blocks) and the box wiring of section 6: `plan.json`, `plan.cmake` (packages `core device-io n8 bliptoaster`, arena
  65536), `DeviceHost` offering `rp.host.clock/1` (`time_us_64`), `BuiltInPlan` printing the pack line, the first-block
  heap line and a fault's words, and `loop.sh`'s staleness guard extended to `packages/*/ts`, the SDK's TS and the
  toolchain.
- **Sizing at the target's width** (`query_target`, `sizePlan(..., targetPointerBytes)`, `blob::kTargetPointerBytes`),
  the 3/2 factor, the device runtime bundle and its digest and layout checks, `idle: "skip"` and `forward()` on both
  hosts, and the engine wrapper's rename and fixes (section 4), with `retroplug-mqjs-run-gc`, the runner over an engine
  built to collect at every allocation.
- **The box build:** `PICO_USE_STACK_GUARDS=1`, `cmake/device-image/no-unwind.c`, the 87-symbol trap holding with the
  engine, and `-Werror=stack-usage=1024` with no per-source exemption.
- **Size:** the box's flash went 95836 to 196688 and its bss 188020 to 120600 (the arena halved).

### 11.4 `rp-device-check` on a script pack (commit 4)

- **`rp.devtest.script-nest`** (a pass-through that nests `replace` callbacks to the re-entry cap, reads its messages
  there, parses JSON at the deepest level so the stack measure covers the parser and an error's backtrace), and
  **`script-plan.json`**: `rp.device.midi-in` -> `script-transpose` -> `script-nest` -> `rp.devtest.digest`, exported
  at the image's arena, rate and block, with a golden (`device-check-script-plan.hex`).
- **The check:** the host answers `rp.host.packages/1`, `rp.host.clock/1` and `rp.device.midi-in/1`; the stimulus is the
  runner's five-block cycle; a **script bench** (a first block, then 16 rounds of an empty block, 64 notes and 64 SysEx,
  timed with the DWT cycle counter, the XIP cache's hits and reads printed); the **script plan** judged by its digest
  against the desktop's exactly (it hears messages alone, integers, so the M33's 32-bit runtime bytecode is held to
  the 64-bit one's); the pack's report; and the **stack in two phases**, the load and the blocks, each judged against
  4096 bytes with 512 kept for interrupts. The LED blinks 30 + the plan step (Heap is 7), 40 + the bench step and 39 for
  a stack phase under its reserve.
- **The deadline case** (after the code reviews, before the script plan, its line after the bench's, so the check prints
  nine lines): `rp.devtest.script-loop`, a third devtest node, runs seven loops over a builtin (a sort of 600 numbers, a
  search in 4000 characters, `join` of an array-like a billion long, `forEach` over 2000 numbers and `JSON.stringify` of
  400, then `forEach` and `JSON.stringify` again after 2500 turns of plain code, a phase change that lets the polls grow
  apart first) in the pack registered again at 24576 bytes, bliptoaster's heap, each until its 10 ms deadline faults it;
  each loop's faulting block is timed by `time_us_64` and the worst judged against 1 s, a third of the box's watchdog.
  The LED blinks 50 + its step, or 59 for a worst over the bound. A loop the deadline failed to end would hang the image
  there: a run that stops after the bench's line, with no LED, is this case failing.
- **Build:** `RP_SCRIPT_HEAP_BYTES_devtest` (40960 at the five commits, 45056 since the third node's definition: the
  toolchain's 3/2 check passes the pack at 42088 now), `NO_SCRIPTS` retired, the size table's new owners
  (MicroQuickJS 98,798 bytes of flash, the script host 18,685, the devtest pack 17,955 at the five commits).
- **Tests:** 8 new `[device-check]` cases (16 in all, 18 with the deadline case and its failure steps): registration at
  the image's heap, a type whose memory does not fit, the script plan (allocating nothing, 4 nodes, 40 blocks, 2032
  messages, digest pinned on every host), its failure steps, the bench and its failure steps, and the clock case (a
  spinning node faults with the deadline's words).

### 11.5 The prototype questions, answered

1. **Code size, RAM and frames.** The engine is 98,742 bytes in the box's RAM at -Os (stdlib tables 10,236 + 1,968)
   and 159 KB at -O3 (83 KB against 159 KB like for like: 98.7 KB counts rodata), so the box takes -Os; the host is
   17,778 bytes plus 204 of bss; a pack's bytecode is 6,988 bytes of flash. The image takes 347.5 KiB of SRAM (section
   5). No frame passes 1 KiB with no exemption: JS_Parse2 560 at -Os, `js_dump_float64` 352, `build_backtrace` 232,
   the host's `materialize` 376 and `setupPackage` 336. The deepest chains, **measured on the device**
   (`rp-device-check`, a node nesting to the cap of 4): a pack's load 2176 of the 4 KB stack, the blocks 2360 (1736
   left with 512 kept); with the engine's default depth of 8 the same chain read 3320, which is why the cap exists.
   The box's block base is about 0.6 KB, so a nesting node leaves it about 1.7 KB for the UART and USB interrupts.
2. **The per-message cost.** The desktop: 290 to 315 ns a message on either build of the host, against 5 ns for the
   C++ filter, a warmed block allocating nothing and the JS heap's top not moving (the compacting collector never runs
   in steady state). The M33 from flash at 150 MHz (the device check): a first block 166.7k cycles (1.1 ms), an empty
   block 67 us, **65 us a note and 48 us a 64-byte SysEx**, XIP hits 99.5%; on the box's own image (from RAM, 120 MHz)
   the pack's load took 9.6 ms and its first block 1.0 ms on the final image (4.2 ms on the stage-3 images, before
   the arrays were made in the engine), outside any node deadline, printed. With `idle: "skip"` an
   idle pass costs 68 instructions, and `forward()` took a message from 4,213 to 2,886 instructions (callgrind, i386);
   the latency the rig measured is in section 11.7, and the deadline's allowance of 250 us a message is about four
   times a note's measured cost.
3. **Bytecode placement.** The build cannot relocate it: the atom update swaps every unique string for the firmware's
   own ROM atom of the same text, at addresses known after the link, in a live context, and running writes into the
   bytecode, so each context needs a private writable copy. So the image keeps the relocatable bytecode in flash
   (`.flashdata`, which `copy_to_ram` leaves there), setup copies it into the pack's memory in the arena and relocates
   it right after `JS_NewContext` (before any atom exists in RAM), and RAM pays for it once, 6,992 bytes placed,
   inside the pack's block.
4. **The stdlib table.** The compile-time stdlib does not matter: relocation swaps atoms by content, so a name the
   firmware's stdlib defines resolves whatever stdlib the compiler had. What must agree is the width (the engine checks
   it: 0x0001 against 0x8001), the one pinned engine, and that every name a pack uses exists in the firmware's stdlib,
   which the toolchain holds by running the pack on `retroplug-mqjs-run`, built over **our** stdlib; the box compiles
   the 32-bit tables of that same stdlib, generated by the same tool binary in the same desktop build. A missing name
   would fault the node at its first call with words.
5. **The arena and the pack's heap.** The arena is 65,536; the box's plan loads into about 56.6 KB of it (the export's
   estimate 57,288). `bliptoaster` declares 24,576: on the device it read **11,636 in use after the first block**
   (MicroQuickJS's own 412); on i386, 18,100 live after full SysEx blocks and 19,296 as the least context the
   toolchain's run needs. `devtest` in the image, declared 40,960, read 22,084 in use after the script plan's run.
6. **`setjmp`, `longjmp`, `abort`.** `setjmp` and `longjmp` pass the trap as they are; `abort` reaches `_malloc_r`
   through newlib's signal table, so it is wrapped (`__wrap_abort` to the SDK's panic); the trap still wraps 87
   symbols and both probes fail as they must. A finding the question did not ask: newlib's `setjmp.o` pulls libgcc's
   ARM unwinder in through `.ARM.exidx`, about 4 KB nothing calls, replaced by a trapping `__aeabi_unwind_cpp_pr0`.

### 11.6 What the reviews found that changed the design

- **The sizing width.** The first design counted `heapBytes` at one width; the 64-bit engine uses about twice the
  32-bit one for objects, but not for payload bytes (1.64 for `bliptoaster` under full SysEx) and less for floats. So
  `heapBytes` became the box's context, the exporter sizes packs at the target's width, and the factor became the stated
  heuristic of 3/2, with the device as the authority (section 5).
- **Pools at queue capacity.** A burst larger than any block before it grew the heap. The pools are now made to the
  queue's capacity at the first block, which fixes the need by the plan, and the box's MIDI queue went from 256 to 64
  messages, since the pools follow it.
- **A critical heap corruption in stage 2:** `listOf` passed an unrooted copy of an array to `set_length`, so a node
  shrinking its messages array corrupted the heap on a collection; it is rooted, with a test that dies without it.
- **Two engine bugs and five swallowed exceptions** (section 4), found by the toolchain's least-heap search, a
  collect-at-every-allocation build, the device check's nest and the code reviews (the sort's, which dropped the
  deadline too, and then an audit of every builtin that converts a value or calls back into JavaScript, which found
  repeat's). The first fix cannot be reached by any test, so its guard is the configure-time hash (section 11.8).
- **Every node context runs the device bundle.** The first cut ran the box's trimmed bytecode nowhere before the rig.
  The runtime bundle is now what every node context loads, the toolchain holds it to the whole pack's output and
  layouts, and the exclusive-group layout bug that comparison found is fixed.
- **Latency levers.** The estimate at the rig's 120 MHz (not 150) put the script node past E3's line, so `idle: "skip"`
  and `forward()` were added, then measured: averages unchanged, the worst about 50 us higher (section 11.7).
- **A moving collector.** `sendData` held a copied value across allocations (a regression of a fix pass); it reads
  through a slot the collector updates.
- **The idle skip's mark of a new plan.** It first ran a block of another `rp_process` and skipped one of the same, so
  that a re-planned node read its moved control starts. The code review found that a quiescent host repeats a step's
  address two re-plans on (the replaced plan is freed at once, and a later plan of its shape may take its memory), so a
  kept node could skip its first block there and keep a stale start. The node keeps where each control stream stood at
  the end of its last call instead, and a block that starts one elsewhere runs.
- **The stack.** The deep-nest run showed 3320 of 4096 bytes at the engine's default depth, so the re-entry cap of 4,
  the box's stack guard and the check's two-phase judgement with a reserve replaced a single measure.
- **Out of memory by its words.** The host first took `null` or an error saying "out of memory" for the heap running
  out, so one node's `throw null` silenced its whole pack (and the toolchain refused the pack): the engine reports a
  heap that ran out instead (section 4).
- **The deadline** scales with the messages a block brings (10 ms + 250 us each on the box), and the desktop's
  500 ms CPU default has none, since Windows cannot measure a 10 ms thread time. The code reviews found it could be
  swallowed and was coarse: a sort comparator's exception (the interrupt's among them) and a `then` getter's were
  dropped, so a spinning comparator never faulted and a spinning getter burned the deadline every block; and the
  engine's poll every 10000 calls and branches let a loop over a builtin run seconds past it. The sort and repeat are
  fixed in the wrapper, every outermost entry that ran past its deadline faults as it ends whatever it returned, and
  the polls are finer and reach into the long builtins (section 4).
- **The desktop re-plan holds two instances of a pack at once** (the replaced one waits to be reclaimed), so the
  desktop's contexts are 4x `heapBytes` with a 1 MiB floor, and a dead pack is set up again once its last node is gone.

### 11.7 The gate

Run by the controller on the prototype's image (BlipToaster, never `n8-midi.nes`), against section 12.7 of [E3's
design](modular-graph-phase-e3-design.md) as the baseline. The first run, on the first stage-3 image (8ec41346),
loaded the plan on the device (arena 56312 of 65536, the pack's heap 11600 of 24576 after the first block, the load
9235 us and the first block 4247 us), then **the box failed to boot three times at the menu handshake** (`*t`
unanswered). The E3 control image, rebuilt from E3's commit with today's exporter, failed identically, so it was not
E4's code; the video showed no signal (mean luma 0.0 at 35 s and 75 s after power-on). After a clean 12 s off and 35
s, the E3 control image booted BlipToaster through the menu. **The cause of that stuck-menu episode is unknown**: the
NES or N8 not running after the USB cable was moved back, or the SD damaged by a desktop leg, were suspected; it did
not recur, and the gate ran on. The table is the last full run, on the stage-4 fix pass image (fdbce8ba), which is the
last image the gate ran on; stage 3 and 4's final fixes after it move the box image by 32 bytes of a range check
(c554df45), and acceptance gates that final image.

| Leg | What it must show | E4 (fdbce8ba unless noted) |
|---|---|---|
| boot | the plan line, the node lines, the pack line and its first-block heap line | booted; the plan 56.6 KB of 65536 arena bytes, 3 nodes, `pack bliptoaster, 32016 bytes`, heap 11636 of 24576 after the first block, load 9613 us and first block 1016 us (on ec6cb4c0 the first block was 4242 us, load 9601 us), volume 128 written |
| `stress.sh` | every case's counts as E3's, fail 0, ring and link loss 0, isolated latency within the new line (section 8) | 33 functional cases ok, fail 0, ringfull 0, `filtered` 6; latency avg/worst 743/1166, 664/1124, 719/1166, 667/1125 us, and one 901/10581 run whose window right after the case started held 12 messages: the `[stats]` print stall (E3's flash image read 12.6 ms and an earlier E4 run 8.1 ms in the same window) |
| latency across the images | | ae291e38: 713/1169, 625/1156, 660/1170, 550/1163, 713/1145; ec6cb4c0: 666/1122, 632/1165, 638/1165, 709/1163 (and 778/8110 with the same stall); E3: 610 to 680 / 1086 to 1128 |
| audio on the L6's channel 3 | A4 440.30 Hz, C5 522.16 Hz, identical | identical: 440.30 and 522.16 Hz |
| `faults.sh --rounds 8` | 8 of 8; stuck 0 | 8 of 8, stuck 0 on the re-run (the first run's 7 of 8 was the stuck round alone, the only cut with a 2.9 s effective off against 4.9 to 5.0 for the rest: the plug's short-off quirk); abandoned boots 8 and 12 on two images |
| the upload path | two ROMs, SysEx `7D 50` pick, an NES power cycle | uploaded in 4922 and 4926 ms (E3 4922 and 4930), saved choices honoured with no upload |
| `rp-device-check` flashed once (2126adf5) | the lines of section 11.4 | `pass, 22 types`; `n8 protocol pass`; `plan pass` 54,408 arena bytes (the export's estimate 56,144), digest `46e21691` against the desktop's `b8854b2c` as before; `script bench pass` (section 11.5, question 2); `script plan pass`, 4 nodes, 87,416 arena bytes, first block 9.1 ms, 4620 frames in 40 blocks with 2032 messages and 0 drops, digest `c7248a64d500e6d8` = the desktop's; `script pack devtest`, heap 22,084 of 40,960; stack load 2176, blocks 2360 of 4096 |
| the box flashed back | its image | the final gate image, saved choice 0, left on the rig |

**The latency verdict** was the user's (decision A, 2026-11-06): accept the script node's cost on the MIDI path, with
the regression line moved to E4's measured figures (section 8). The first gate that measured latency (ae291e38, with
`idle: "skip"` and `forward()` already in) read averages near E3's and worsts up to 20 us over E3's ~1150 us line
(about 40 to 50 us over E3's worsts of 1086 to 1128).

### 11.8 What is left

- **Windows, macOS and arm64 have never built E4.** The Windows path for the engine (clang-cl objects with a forced
  include of `rp_mqjs_names.h`, the `sys/time.h` shim, `GetThreadTimes`, `mquickjs.h`'s `##__VA_ARGS__` under cl) is
  written to a pattern and unbuilt; `cmake/mquickjs-names.cmake`'s macOS branch has never run; every desktop product
  now links the engine beside QuickJS, so the first Windows build of any product is its test.
- **The 32-bit runner is absent from the build.** The i386 harness that measured the 32-bit figures needs lib32
  libraries a fresh devcontainer, CI and macOS and Windows do not provide; it was a scratch measurement. The 32-bit
  bytecode runs nowhere in CI (the device check is built there and never run), so the 3/2 factor is a heuristic
  and the rig's boot line is the authority.
- **Fix 1 of the engine (the name table's growth under a collection) is guarded only by the configure-time hash.** No
  test reaches its condition, a collect-at-every-allocation build included.
- **What the audit of the engine's builtins left.** Every builtin that converts a value or calls back into JavaScript
  propagates what it is handed (the sort and repeat are fixed, section 4), but for drops of an out-of-memory alone:
  `Function.prototype`'s `prototype` getter and setter, an error's backtrace and a thrown error's message
  (`JS_ThrowError`), each leaving the error pending behind a result. The host meets them as it meets a node that caught
  the out-of-memory: the context's own report that it ran out (`ran_out`) kills the pack when the entry then ends in an
  exception, and a node that runs on does so in a heap with no room. `RegExp.prototype.exec`'s, which corrupted the
  context's root list, is fixed (section 4).
- **The `[stats]` print stalls the loop.** E3's hazard, unchanged: a message arriving as the 13.7 ms print starts reads
  about 12 ms. The sniff / health USB read (about 2.5 ms) holds the loop the same way. Both are the console, not the
  script node, and they appear as isolated outliers in some E4 latency runs.
- **Every context carries its own copy of its pack's bytecode** (running writes into it), so the box pays the 6,992
  bytes, a desktop product pays the bytecode once per context, and the whole pack's in the desktop's control context
  is a second copy beside the runtime's.
- **The desktop re-plan holds two instances of a pack.** The contexts are sized for it (4x, 1 MiB floor) and a dead
  pack is revived, but a plan that re-plans a script node behind a large source can still run its pack out, until the
  replaced instance is reclaimed.
- **The exporter's trial load is laxer than the image's figure for devtest** (the manifest's 57344 against the image's
  45056 through `RP_SCRIPT_HEAP_BYTES_devtest`), and the estimate (107,264) is an upper bound on the image's. An
  exporter `--script-heap` was left out.
- **A SysEx past 3 bytes allocates a Uint8Array per message**, and a block's SysEx arrays stay live until the next
  block refills those places, so a SysEx stream makes the box collect in the MIDI path; at 32 bits a node that reads a
  non-whole stream start, change value or tempo allocates too.
- **A node's budget is its author's:** about 250 us a message, and a nest of native callbacks (`replace`, `forEach`) no
  deeper than the cap of 4; the deadline faults the node, never the box, within the bound section 4 states (the device
  check's deadline case times the worst at bliptoaster's heap on the M33, a phase change among its loops). A single
  builtin call that neither allocates nor polls and is not bounded by the heap would run to its end, none being known;
  the watchdog stays the backstop.
- **A stale exporter** is guarded as in E3 (`loop.sh`'s stamp), now also against a pack's TS sources, the SDK's TS,
  `deps/mquickjs` and the toolchain.
- **The audio version, a live-upload or SD plan that brings its own packs** (a trust model for bytecode mquickjs does
  not verify, and the per-node port signature of E3's section 10), **the desktop's N8 graphs using the policy node**,
  and **`feature/launchpad` adopting the toolchain**: section 9, unchanged.
- **`RP_NATIVE_USB` still has never run on hardware**, and the Launchpad's `threads` flag, `rp.list`'s JSON schema and
  the `rp.core.delay` default of E3's list stand.
