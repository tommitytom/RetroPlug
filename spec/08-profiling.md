# 08 — Profiling the audio thread

**Status: built** (compile-guarded behind `RETROPLUG_PROFILE`; run with `pnpm profile`). A
benchmark harness that measures what the DSP-thread **JavaScript runtime** does — allocations and garbage
collection — under an mGB + heavy-MIDI workload. The runtime model it profiles is
[01-architecture.md](01-architecture.md) (the QuickJS runtimes, the audio thread) and
[04-roles-protocol-nodes.md](04-roles-protocol-nodes.md) (the protocol script nodes this exercises).

## 1. The question

The protocol nodes run per audio block as script nodes, in the script host's **bare QuickJS-ng 0.15.1**
audio runtime ([04-roles-protocol-nodes.md](04-roles-protocol-nodes.md) "The protocol nodes"). We want to
know what that runtime costs per block, specifically its **allocation behaviour**, so the RT audio path
can be driven toward the ideal of *zero steady-state allocation*, and so a regression is caught
automatically. The workload of interest is **mGB** (the GB MIDI synth, the simplest path: the routing
node, then a node that forwards every host-MIDI byte to serial) driven by a realistic, dense MIDI stream.

## 2. The reframe: measure refcount churn, not "GC"

QuickJS-ng is **reference-counted first, with a mark-sweep collector that exists only to reclaim
reference cycles.** This is the load-bearing fact for the whole plan:

- **Acyclic garbage is freed immediately and deterministically** the instant its refcount hits zero —
  no GC pass, no pause. The transient objects/arrays a block creates die as the block unwinds. The
  V8/JSC "GC pause" model does **not** apply here.
- The **mark-sweep GC** (`JS_RunGC`) only fires when live bytes exceed a threshold that self-tunes to
  **1.5× the live set** (initial 256 KiB), and is only *checked* on new **object** allocation
  (`js_trigger_gc`'s single call site is `JS_NewObjectFromShape`). A runtime with a bounded live set can
  run indefinitely and **never auto-GC**.

**Consequence:** the per-block cost you feel is **malloc/free churn from refcounting** — allocate-and-
free-within-the-block. The headline metric is **allocations/block and bytes/block**. Cycle-GC passes
are a secondary metric you track only to *prove they are ~zero* (any GC on the audio path is an RT
hazard — an unbounded sweep). A profiling effort framed around "GC pauses" would find almost none and
wrongly conclude all is well, while the real cost — a storm of tiny malloc/free pairs every ~23 ms —
goes unmeasured.

## 3. What the per-block path allocates today

**Marshalling.** The script host hands a node its inputs through reused objects (a port's `Message[]`
refilled in place, its payloads copied into one JS-owned buffer:
[ScriptHost.cpp](../packages/native/src/host/script/ScriptHost.cpp) `fillMessages`), so a node written
with indexed loops allocates nothing per block once warm: a message's `data` view is made again only when
its place in the payload buffer moves, and the pool and the buffer grow only past their largest block.

**The protocol nodes** (each package's `ts/protocol.ts` with its node module beside it, since B3b; the
shell's controller session is the one left) are
written that way: each reads the `Message` views it is handed, sends on its own ports, and keeps its state
and tick counter on its instance, so the routing, mGB, N8 and LSDj MIDI-sync nodes, and LSDj's serial-out
decoder (`rp.lsdj.serial-out`) in master sync and MI.OUT, allocate **nothing** in a warm block, JS or
native, and `dsp-bench` and `nes-bench` measure 0 allocations a block (section 9). The decoders
([lsdjArduinoboy.ts](../packages/lsdj/ts/lsdjArduinoboy.ts)) hand each message over byte by byte,
`emit(frame, len, b0, b1, b2)`, and the node sends it from one of three arrays it made once, one a message
length. What still allocates is work that makes something:

| Site | Allocates |
|---|---|
| `rp.risa.sync`'s arm ([risaSync.ts](../packages/risa/ts/risaSync.ts)) | two objects on a start or a seek: `risaLocate`'s object and `risaArmPacket`'s array |
| `rp.controller.session`'s surface diff and launches ([session.ts](../packages/launchpad/ts/session.ts)) | per LED change and per launch, while a surface is in use |

**The cores.** Since C4 a system core allocates nothing in a warmed block either, Mesen's per-frame video and input
plumbing included: a Mesen core's audio ring, its per-block scratches and its tap rings are sized at activation from
the build request's `maxBlockFrames`, its frame and controller-state buffers are reused, and SameBoy's two scratch sets
are sized the same way. That holds with a block's host input staged into it too, since the input-queue stage: every
queue a press, a MIDI run, a serial byte or a raw message lands in is bounded and sized when the core is built. The C++
suite holds that promise directly (`retroplug-engine-graph-test`'s `[alloc]` cases, [06-build-test.md](06-build-test.md));
the profile benches measure the JS side, so their 0 is the script host's, not the core's. What still allocates inside a
long run is Mesen's 0.5 s savestate capture (its streaming serializer), a NES's region reset and a Game Boy's model
restart, the N8 firmware's SD-command path (`std::string` / `std::filesystem`, a bigger directory listing, an unbounded
TX queue) and a Game Boy's serial-out capture log; the two knob restarts are accepted
([02-native-host.md](02-native-host.md)), and
[07-remaining-work.md](07-remaining-work.md#allocations-left-on-the-block-thread) lists the rest as open work.

**How it got here.** Under the DSP kernel (one TS kernel running every role, until B3b) the first
measure was **~171 allocs/block** on Profile A: the kernel built a `Map` of routed inboxes, per-system
`keys` / `buttons` filters, and a `SystemCtx` with four sink closures per system and stage, every block.
Hoisting those to the kernel's structure change cut it to **~32** (−81%). The kernel node B3a built then
measured **33.81**: it still built the kernel's `BlockInput` afresh every block (an input object, a
`midiIn` array of `{ frame, data }` objects with `data` copied to a plain array, empty `buttons` / `keys`
arrays, a `{ system, byte }` object per MI.OUT byte). B3b's nodes take the marshalled inputs as they are,
and the count is 0. The MI.OUT and master-sync decoders built each message they emitted as a new array
until their emit took its bytes as arguments: 3.56 allocations a block in `dsp-bench`'s master-sync window
and 0.44 in its MI.OUT one, both 0 since.

## 4. Approach

### 4a. Drive the real code path **off** the real-time thread

There is already a deterministic, device-free pull-render path:
[`EngineRpcService::renderAudio(ms)`](../packages/native/src/host/rpc/EngineRpcService.cpp#L293) loops
`Engine::processBlock` in 1024-frame blocks synchronously on the **calling** thread — no audio device, no
RT scheduling, fully repeatable. It is exposed to TS as `createAudioDriver().renderAudio`
([audioDriver.ts](../packages/retroplug/src/audioDriver.ts)). Profile **this**, not a live
JACK/CLAP host: under a live host the measurement is confounded by RT jitter, CPU-frequency settling,
xruns, and the callback deadline itself — the very noise that swamps the allocation signal. Removing the
clock is what makes allocation and instruction counts **deterministic** (identical run to run), which is
the property an agent/CI needs to tell a real regression from noise.

The benchmark is a new `test-native/*.test.ts` run under `pnpm test:native` (which builds
`retroplug-host`, whose every Engine embeds the script nodes: [06-build-test.md](06-build-test.md)); copy
[test-native/audio-render.test.ts](../packages/retroplug/test-native/audio-render.test.ts).
Shape: an `mgb` role per system and the send-to-all MIDI routing (`DirectGraph.setPipeline` /
`setMidiRouting`), so a routing node and an `rp.mgb.midi` a system, then per block *stage this block's MIDI →
`renderAudio(oneBlock)`*. `renderAudio` also runs the SameBoy APU (C++), which dominates **wall-time** but
contributes **zero JS heap** — so the JS allocation counts stay cleanly isolated even though wall-time
does not.

### 4b. Primary signal: instrument QuickJS's own allocator (in-process, deterministic)

The direct, deterministic answer to "what does the JS runtime allocate per block" is to count every
allocation the audio runtimes make. The script host makes each of a pack's runtimes with
**`JS_NewRuntime2(&mf, &heap)`**, the opaque a heap of the pack's for that runtime (the counters it adds
to, its live bytes and the pack's memory limit), and a counting `JSMallocFunctions` wrapper (quickjs-ng
shape: `js_calloc/js_malloc/js_free/js_realloc/js_malloc_usable_size`, `quickjs.h:451-457`;
[ScriptHost.cpp](../packages/native/src/host/script/ScriptHost.cpp)). The wrapper bumps cumulative
allocation, realloc and free calls and the bytes asked for (in every build: the native tests read them;
an audio runtime's go to the shared counters once it joins: from the start for the built-in pack, after its load for
any other, a control runtime's to a count of its pack's that nothing reads), keeps the runtime's live bytes, refusing an allocation that would take
them past the limit, and, in a profile build, the live and peak heap in usable bytes, and delegates to
libc.

- **Critical:** `js_malloc_usable_size` **must return the real platform usable size**
  (`malloc_usable_size`), or QuickJS's internal byte accounting and GC-threshold math break.
- For a live snapshot, `JS_ComputeMemoryUsage(rt, &JSMemoryUsage)` (`quickjs.h:589` — **note: it is
  `JS_ComputeMemoryUsage`, not `JS_GetMemoryUsage`**) fills counts for malloc/objects/atoms/strings/
  shapes/arrays. Its `malloc_count`/`malloc_size` are **live/net, not cumulative** (they decrement on
  free) and it is **expensive** (walks every live object) → call it once **before/after** the measured
  window, never per block. Cumulative totals come from the allocator wrapper.

Snapshot the wrapper counters before/after the measured window and report deltas ÷ K. This alone — no
external tool — gives a deterministic `allocs_per_block`, and is the cheapest first experiment.

**Counting / controlling GC:** there is no built-in GC callback, and `JS_SetDumpFlags(JS_DUMP_GC)` is
compiled out under `NDEBUG` (release). To get GC counts deterministically, **disable auto-GC**
(`JS_SetGCThreshold(rt, SIZE_MAX)`) for the window and **self-drive `JS_RunGC(rt)`** at known block
boundaries, counting invocations and timing them — this both isolates allocation from collection and
proves how much cyclic garbage (if any) accumulates. (`JS_SetGCThreshold` / `JS_RunGC` /
`JS_SetMemoryLimit` are `quickjs.h:494-513`.)

### 4c. External tools — tiered, ranked by agent-friendliness

The honest state of the art: **no turnkey C++ "agent profiler" exists** (the one product branded for it,
Posit `debrief`, is R-only). The winning pattern is **deterministic tools + filtered text/JSON** — a
sampling profiler gives a different number every run, so it can't gate a regression. Because QuickJS is a
**no-JIT interpreter**, native backtraces collapse at `JS_CallInternal` (every JS frame is the same C
function): you get "QuickJS allocates *here*", never "*this JS function* is hot" — fine for allocation
attribution, useless for JS-level CPU. Build QuickJS + host with `-g -fno-omit-frame-pointer`.

| Tier | Tool | Answers | Invocation → text |
|---|---|---|---|
| Primary | **In-process allocator counter** (§4b) | allocs/bytes per block, cumulative | prints one JSON line |
| Attribution | **Valgrind DHAT** | *which* QuickJS sites allocate; block lifetimes; alive-at-peak vs alive-at-exit (churn vs retention/leak) | `valgrind --tool=dhat --dhat-out-file=dhat.json` → parse JSON |
| CPU + gate | **Valgrind Callgrind** | deterministic instruction counts to `js_malloc_rt`/GC/`JS_CallInternal` | `callgrind_annotate --auto=yes` |
| Long runs | **heaptrack** | same census, lower overhead | `heaptrack_print -a` (sort by alloc count = churn) |
| Heap shape | **Massif** | peak & growth over time (leak detection) | `ms_print` |
| Reality check | **perf** (sampling — secondary only) | real cycles/cache | `perf stat`; `perf record -g --call-graph dwarf` → `perf report --stdio` |
| RT-safety gate | **RealtimeSanitizer** (`-fsanitize=realtime`, mark the path `[[clang::nonblocking]]`) | pass/fail: any malloc/lock reaching the audio path | non-zero exit names the call |

Always emit **filtered** output (`--threshold`, `-p N`) — the deterministic tools produce megabytes; a
top-N is what fits an agent's context and yields a trustworthy diff.

## 5. The MIDI workload

mGB is 4 monophonic voices + a poly voice (MIDI ch1→PU1, ch2→PU2, ch3→WAV, ch4→NOISE, ch5→POLY), fed raw
MIDI-over-serial; the `mgb` role's node forwards every byte verbatim
([nodes/mgbMidi.ts](../packages/mgb/ts/mgbMidiNode.ts)). It responds to note on/off
(velocity 0 = note-off), a handful of CCs (pulse-width, envelope, sweep, PB-range, preset, pan, sustain),
pitch bend, and program change (1–15 = preset). Profiles:

- **A — 4-part arrangement @140 BPM** with two CC "LFOs" ≈ **105 events/s** (~2.4 messages/block).
  Musically plausible; the realistic default.
- **B - poly chord bursts** (each chord change = a 6-message burst landing in one block): stresses
  per-block routing + FIFO fill, the interesting script-node path.
- **C - synthetic worst case** ≈ **700-860 events/s**: floods the serial FIFO and the nodes' forward loops
  to find the cliff (not musical).

Two cost levers: **MIDI density** stresses the *routing and protocol nodes* specifically (what we care about);
**system count** (4–8 cores in the graph) is the *dominant* total-thread-cost multiplier because the
per-sample APU cost is MIDI-independent. A realistic heavy run combines both.

**Injection:** `audioDriver.stageMidiIn(bytes)` → `EngineRpcService::stageMidiIn`
([:485](../packages/native/src/host/rpc/EngineRpcService.cpp#L485)) → `Engine::stageMidi`
([Engine.cpp:699](../packages/native/src/host/engine/Engine.cpp#L699)) → `pendingMidi_` at frame 0, which
the plan's `rp.host.midi-in` takes, sorted by frame, at the top of the next `processBlock`.
**Determinism recipe:** fixed SR
44100 / block 1024 (23.22 ms); `setBpm`/`setTransport` once; a **seeded PRNG** (mulberry32 — never
`Math.random`) pre-generates the whole `{block, msg[]}` schedule; loop `renderAudio(blockMs)` one block
at a time, staging each block's events first; a `renderAudio(1500)` warm-up (mGB is silent until firmware
init) is discarded. Gate on RMS band + wall-time, not exact samples.

**Limitation to note:** the bench stages every message at frame 0 and `renderAudio` takes the whole
staging on its first block, so delivery is **block-quantized** (23.2 ms grid), which is fine for mGB.
Sub-block placement is `stageMidiIn(bytes, frame)`, a frame inside the next block (the RPC refuses one
past `renderAudio`'s 1024-frame block).

## 6. Methodology

- **Warmup → measured window.** Discard **W** blocks (bytecode compile, atom interning, pool growth),
  then measure **K** blocks (1k–10k). Verify per-block allocs are **flat** before opening the window
  (print the first ~256 blocks) — a fixed W is a heuristic that silently fails for a path that keeps
  allocating. Report per-block **average + the max single block** (audio lives on the tail).
- **Metric set** (one JSON line per run, diffed against a committed baseline):
  `allocs_per_block`, `bytes_per_block`, `live_heap_delta` (flat = churn OK; rising = **leak** — the
  churn-vs-leak discriminator), `gc_cycles` (target 0), `max_block_allocs`, `%cycles_in_malloc_gc`
  (Callgrind). Target for an RT-safe path: **allocs_per_block == 0**.
- **Two harnesses, labelled.** The off-RT one answers *"what does the runtime allocate"* (deterministic).
  A separate live / SCHED_FIFO run answers *"does it still fit `bufferSize/sampleRate`"* — median / p99 /
  **max** block wall-time and xrun count — the only questions the off-RT harness structurally cannot
  answer. The threaded free-run path already exists (`startAudio` →
  [`AudioDriverRpcService::audioLoop`](../packages/native/src/host/rpc/AudioDriverRpcService.cpp#L34),
  validated under `tools/run-sanitizer.sh`), but its ~200 µs/block pacing is non-deterministic
  → use it for the race/soak/deadline run, not for allocation counts.
- **CI gates (exit-code walls):** RealtimeSanitizer exit == 0; `allocs_per_block == 0` (or ≤ a pinned
  budget); `gc_cycles == 0`; Callgrind `Ir` within baseline ±X%. Deterministic inputs make an exact-match
  gate on the count metrics viable. For pinned wall-clock runs use `taskset` + `performance` governor +
  no-turbo (note the Docker caveat: `personality(2)`/ASLR-disable and `perf_event_paranoid` may be
  blocked in a container — lean on the deterministic tools there).

## 7. Gotchas

- It is **`JS_ComputeMemoryUsage`**, not `JS_GetMemoryUsage` (that symbol does not exist in quickjs-ng).
- `JS_ComputeMemoryUsage` counters are **live/net and expensive** → before/after snapshots only; use the
  allocator wrapper for cumulative totals.
- The allocator wrapper **must return real `malloc_usable_size`** or QuickJS's byte accounting + GC
  threshold break.
- `JS_SetDumpFlags(JS_DUMP_GC)` is **`NDEBUG`-gated out** in release — count GC by self-driving
  `JS_RunGC` instead.
- No-JIT interpreter → native backtraces collapse at `JS_CallInternal`; good for allocation attribution,
  not JS-function CPU. Build with `-g -fno-omit-frame-pointer`.
- `renderAudio` runs the SameBoy APU (C++) → it dominates wall-time but not JS heap; allocation counts
  stay isolated, wall-time does not.

## 8. What was built

- **Native allocator instrumentation**, on the script host's audio runtimes, every pack's summed
  ([ScriptHost.hpp/.cpp](../packages/native/src/host/script/ScriptHost.cpp); a pack loaded beside the built-in
  one, [modular-graph-phase-c1-design.md](../docs/modular-graph-phase-c1-design.md), counts its load and its
  freeing apart, so a profile build's absolute live heap leaves out what the load of any pack loaded beside
  the built-in one allocated, its deltas exact): the counting `JSMallocFunctions` installed via
  `JS_NewRuntime2` (`js_malloc_usable_size` returns the real platform size so QuickJS's own accounting stays
  valid), a per-Engine-block allocation bracket (`beginBlock` to `endBlock`: native aggregation → no per-block
  RPC), and `allocStats()` / `resetAllocStats(disableAutoGc)` (pins `JS_SetGCThreshold(SIZE_MAX)`) / `runGc()`
  (timed `JS_RunGC`). The window, the live and peak heap and the block bracket are behind
  `#ifdef RETROPLUG_PROFILE`; the `#else` path returns `enabled:false`.
- **Native allocations counted in the C++ suite too** (C4): `retroplug-engine-graph-test` links
  [graph/CountingAllocator](../packages/testing/native/src/graph/CountingAllocator.cpp), which replaces the global
  `operator new`/`delete` (every form) and counts what a stretch of code allocates and frees on the calling thread
  (`beginCountingAllocations` / `endCountingAllocations`), and can refuse every allocation from a size up. Since C4 it
  also interposes the C library's `malloc` / `calloc` / `realloc` / `free`, forwarding to glibc's own, so a core's C
  blocks are seen; that half is **opt-in per window** (`beginCountingAllocations(true)`) and platform-gated, because it
  is possible only on glibc and not in a sanitizer build (ASan and TSan own the allocator) or on Windows (the static CRT
  cannot define `malloc`), and it would otherwise count a subsystem whose own allocations *are* malloc's (QuickJS).
  `countsCAllocations()` reports which binary sees them, so a case whose promise includes a C allocation SKIPS where it
  is false. The `[alloc]` cases are the users (section 3's core promise): the whole-block cases count the C library's
  blocks where the binary can, and prove the operator-new half of the promise where it cannot. The profile benches keep
  counting QuickJS alone.
- **The trace window** (Tier B), on the same runtime: `traceReset(arm)` opens a window of nested wall-time
  spans, recorded only while armed. Every script node's process is a span **named by its type**
  (`rp.midi.route`, `rp.mgb.midi`, ...), holding `marshal` (its audio copies in and the transport) and
  `js-call` (its JS, inside which each input it reads is a `marshal` span); the Engine adds `apu-render` (the
  plan's whole run, from where it starts to where it ends, so every script node's process span lies inside it,
  wherever the planner runs the node, before a system or after one, and its figure includes the script nodes'
  few microseconds beside the cores') and `state-publish`. A span is stored as it closes, so a block's
  `apu-render` follows the spans inside it in the list; the trace-event JSON gives each span its start and
  length, so nothing that reads it depends on that order. The labels are fixed: `script-node` (0), `marshal`,
  `js-call`, `apu-render`, `state-publish` (1 to 4), then the packs' types from `DSP_SPAN_TYPE_BASE` (16) up,
  in the order their packs first loaded them, each named by its id and keeping its label across its pack's
  reloads, so a dev loop's reloads do not use up the table (the desktop's production bundle has nine, 16 to 24:
  `rp.midi.route`, `rp.mgb.midi`, `rp.n8.midi-to-fifo`, `rp.risa.sync`, `rp.sms.sync`, `rp.lsdj.sync`,
  `rp.controller.session`, `rp.lsdj.serial-out` and `rp.genmddj.sync`, registered last so the others kept their labels; a
  product that drops a package has its own positions, so the plugin's eight are 16 to 23, with `rp.lsdj.serial-out` 22
  and `rp.genmddj.sync` 23 there). By
  type
  rather than node id, as the DSP kernel's per-role spans were per kind until B3b made each role a node: a
  type's label is fixed when the bundle loads, where an id comes and goes with every document, so two systems'
  nodes of one type are two spans of one label a block, in plan order. Every label is below `kDspSpanLabels`
  (1024), so the name table stays bounded: a type past it is traced as `script-node`. JS adds no spans of its
  own (the kernel's roles were the only JS that did, through span globals that went with them), so a span is
  always closed by the host, every way a call ends. The promise jobs a node's call queues have no span of
  their own: they run at the checkpoint after the call, which for `process` lies inside its process span,
  after its `js-call`. `dspTraceReset` / `dspTrace` / `dspTraceNames` read it, and
  `tools/run-profile.sh trace` writes it as Chrome trace-event JSON.
- **Introspection RPCs** — `dspAllocStats` / `dspResetAllocStats` / `dspRunGc` on the harness facet
  (`Engine` → `EngineRpcService`, reached directly through `engine_` in the quiescent `renderAudio`
  regime) and surfaced in [audioDriver.ts](../packages/retroplug/src/audioDriver.ts). The calls that act on
  the windows are quiescent only: while an audio thread owns the runtime, `dspResetAllocStats` and
  `dspTraceReset` answer false and `dspRunGc` a result with `enabled` false (the first refusal said once on
  stderr), and `dspTrace` / `dspTraceNames` read nothing; `dspAllocStats` never refuses (a benchmark reads its
  `enabled` to know a profile host). So a trace armed before `startAudio` cannot be reset until `stopAudio`,
  and meanwhile it records every block, its span list growing, which allocates on the audio thread (only a
  profile host can arm one, and only through the harness facet).
- **The seeded-MIDI benchmark** [test-native/dsp-bench.test.ts](../packages/retroplug/test-native/dsp-bench.test.ts):
  a mulberry32 generator for Profiles A/B/C, the block-stepped `stageMidiIn` + `renderAudio(BLOCK_MS)`
  loop (warmup window discarded), one JSON metrics line. Env knobs `RP_BENCH_{PROFILE,CORES,BLOCKS,WARMUP,SEED}`,
  and `RP_BENCH_LSDJ` (below).
  It asserts the measured window allocated nothing (at the default 200-block warmup only; `RP_BENCH_WARMUP` is a
  documented knob, and a shorter one prints the figure without asserting it), and proves the window live with a second
  one after it: adding a system places a protocol node. In the `renderAudio` pull path `GraphHost::publish` adopts the plan at once, so
  the node's JS state is created inside `graph.add` / `setPipeline` / `loadRom`, before any block (counted in
  `allocCalls`, not in `maxBlockAllocCalls`); what lands inside the block is `fillMessages` making each fresh port's
  reused `Message[]` and its pool the first time the node calls `messages()` (the new `rp.mgb.midi` /
  `rp.n8.midi-to-fifo`, and `rp.midi.route`, whose reshape freed its port arrays when its outputs grew). That count
  must be above 0; pre-making those arrays at create time would move the allocation out of the block, and the
  assertion would need another trigger. (The runtime's peak heap cannot be the proof: it is never reset, and the
  bundle's load puts ~300 KB in it before the window opens.) Skips (a counted `skip`) under a non-profile host so
  the normal sweep stays fast and green;
  [test-native/nes-bench.test.ts](../packages/retroplug/test-native/nes-bench.test.ts) is its NES twin, on
  the Mesen core and the `nes-n8-midi` role's node, and does the same. After the mGB window, the mGB systems gone,
  dsp-bench has a window each on a real LSDj, playing, its `rp.lsdj.serial-out` decoding what it clocks out: master
  sync (a stock LSDj in SYNC=LSDj, captured in `listen`) and MI.OUT (an Arduinoboy build in SYNC=MI.OUT, captured in
  `drive`), each over `max(100, min(RP_BENCH_BLOCKS, 400))` blocks after the same warmup (MI.OUT's clocks come
  unevenly, none in a window's first 20 blocks at the default warmup, so a shorter window could hold none), proved
  live by the same second window. At the default warmup each is held to the mGB window's zero and to more clocks
  than its bound, so LSDj played through it: 40 a second in master sync, most of the about 51 a second LSDj sends at
  tempo 128, and 2 a second in MI.OUT, half of its about 4. They need the fixture tree's LSDj ROMs
  (`RETROPLUG_RESOURCES_DIR`): without them the case skips there, after the mGB window has been held to its zero.
  `RP_BENCH_LSDJ=0` leaves them out (a line says so, and the case passes), and `tools/run-profile.sh`'s valgrind
  modes default to it unless it is set: the windows' boots and warmups do not shrink with `RP_BENCH_BLOCKS`, which
  those modes lower to 200, so they would make a run several times longer and put an LSDj's setup in the census
  of the mGB workload. Their zero is the `stats` mode's to hold.
- **The Mega Drive benchmark** [test-native/md-bench.test.ts](../packages/retroplug/test-native/md-bench.test.ts): the
  BlastEm twin, at the same 1024-sample partition and the same `DSP-BENCH` line, with a window each on the seven
  synthetic images the parity group boots (from `@retroplug/blastem/testFixtures`, so the two groups time the same
  bytes) and one on a real genmddj cart playing a song the DAW clocks through port 2. That last window is the file's
  fixture-gated half (`genmddj_v0_27.bin` under `RETROPLUG_RESOURCES_DIR`): a box without the tree prints every
  synthetic line and then skips, counted, and the profile guard comes before any console boots, so the default host's
  sweep pays a skip rather than a boot. Only the genmddj window holds a zero, because it is the only window whose graph
  plan holds a script node (`rp.genmddj.sync`, the role's clock words on their way to the port device), and what says
  something ran there is the cart's own row counter, read after the window: `readRam` copies into a fresh array in the
  counted runtime, so a read inside the loop would land in the census. The synthetic windows run no node, so their counts
  print and no zero is claimed for them. Env knobs `RP_BENCH_{BLOCKS,WARMUP}`; the headline is one
  `md: <xRT>x realtime (<window>)` line per window.
- **Build knob + scripts** — `RETROPLUG_PROFILE` ([CMakeLists.txt](../CMakeLists.txt), mirrors
  `RETROPLUG_SANITIZE`), `tools/run-profile.sh [stats|trace|unit|dhat|callgrind|massif]` (separate
  `build-prof/` RelWithDebInfo dir), and `pnpm profile`.

**Deferred:** a committed baseline + CI threshold gate, and a RealtimeSanitizer build variant, are not
built. `perf` / `heaptrack` are not installed in the devcontainer (valgrind 3.22 is), so those wrapper
modes are out.

## 9. Verification (done)

- `pnpm profile` builds `build-prof` and prints a JSON metrics line; the allocation counts are
  **bit-identical run-to-run** (only `xRT`/`gcMs` wall-clock vary). First measured result (Profile A, 1
  mGB core, 2000 blocks): **~171 allocs/block**, `freesPerBlock` ≈ allocs (balanced churn),
  `liveHeapDelta` ≈ 0 (not a leak), `maxBlockAllocs` 279.
- The profile build's own unit checks are the eight Catch2 `[profile]` cases of `retroplug-engine-graph-test`
  (a script node's span labelled by its type, holding `marshal` then `js-call` and closed every way a call
  ends; nodes of one type sharing its label and each type labelled in load order; the calls refused while
  the audio thread runs and the trace reads reading nothing then; a type past the name table traced as
  `script-node`; the desktop bundle's types labelled 16 to 24 in order, `rp.lsdj.serial-out` 23 and `rp.genmddj.sync`
  24 (a product's list is its own positions: the plugin numbers the serial-out decoder 22 and genmddj's sync 23, which
  its bundle case holds); and an
  Engine's `apu-render` spanning its plan's whole run, each block's holding the process spans of a script node
  before its Game Boy and of one after it, the first ending before the second starts; and two pack cases: a type's
  label kept across its pack's reloads, and a freed generation's share taken off the live heap, quiescent and under an audio thread). Nothing else runs them:
  `tools/run-profile.sh unit`, or by hand `cmake --build build-prof --target retroplug-engine-graph-test -j$(nproc)`
  then
  `build-prof/bin/retroplug-engine-graph-test "[profile]"`.
- **Optimization pass (done):** hoisting the per-block ctx/closures/filters/routing-Map to `setSystems`
  (§3) cut Profile A to **~32 allocs/block** (−81%), **~2.3 KB/block** (−79%), `maxBlockAllocs` 279 → 60,
  behaviour-identical (41-file mock + 25-file native suites green).
- **Correction to the earlier cyclic-garbage read.** The end-of-window `runGc` reclaims ~595 KB in **both**
  the old and new kernels, and a **block-count sweep (500 / 1000 / 2000 / 4000 blocks) shows it is
  constant, not proportional to blocks**, so it is *not* per-block cyclic garbage from the ctx/closures
  (removing them left it unchanged). It was a **fixed, one-time setup structure** (the zod-based role
  registry built at kernel load, gone with the kernel) that production's auto-GC collected once early and
  never regenerated, not an unbounded audio-thread hazard. The real steady-state RT cost was the
  malloc/free churn, then ~5× lower; B3b's protocol nodes took it to zero (below).
- **On the script host.** With the kernel running as the `rp.dsp.kernel` script node (B3a), the same run
  (Profile A, 1 mGB core, 2000 blocks, a `build-prof` host) measured **33.81 allocs/block**, ~2.1 KB/block,
  `maxBlockAllocs` 62; nes-bench, its twin on the Mesen core, 20.14, ~1.3 KB and 44.
- **On the protocol nodes (B3b).** The same runs measure **0 allocs/block** and 0 bytes, `maxBlockAllocs` 0,
  in both: dsp-bench with a peak heap of 307096 bytes at 16.6× real time, nes-bench 305064 bytes at 10.8×
  (2243 us a block, nearly all of it the Mesen core). The bundle's load leaves no cycles in the audio runtime
  (B3a's, which carried zod and the kernel's role registry, left about 144 KiB for the first quiescent reclaim
  to collect), so the end-of-window `runGc` reads what the window itself left in cycles: 0 bytes, in dsp-bench
  and nes-bench alike. `allocsPerBlock` is the stable metric: the byte counts are bytes requested, which move
  with the heap's layout. A trace (`tools/run-profile.sh trace`, 64 blocks of dsp-bench) has `rp.midi.route`
  at 1.37 us a block and `rp.mgb.midi` at 0.83 (each the whole `process` span of its node, so the `js-call` and
  `marshal` spans nest inside those figures); the means over every `js-call` and `marshal` span in the trace, the
  two nodes' together, are about 0.95 and 0.11 us, against 1428 us of `apu-render` and 2.0 of `state-publish`;
  nes-bench's has `rp.midi.route` at 1.09 us and `rp.n8.midi-to-fifo` at 0.53, against 2151 of `apu-render` and 3.7 of
  `state-publish`. The script nodes are under 0.2% of a block's work; the cores are the rest. (That `apu-render`
  began where the last script node's `process` ended; it spans the whole plan since LSDj's decoder moved after its
  Game Boy, so a trace's figure holds the script nodes' microseconds too.)
- **On LSDj's serial-out decoder.** dsp-bench's two LSDj windows read **0 allocs/block** and 0 bytes,
  `maxBlockAllocs` 0, over 400 blocks each: master sync with 476 host MIDI messages decoded, every one a clock, and
  MI.OUT with 60 (40 clocks, 20 NoteOns); the same run's mGB window reads 0 with a peak heap of 311912 bytes at
  16.4× real time. With the decoders' array per message (the emit before it took each message's bytes as arguments)
  the two windows read 3.56 and 0.44 allocations a block. `ScriptBench`'s protocol-node plan
  (`build/bin/retroplug-engine-graph-test "[bench]"`, five script nodes, the decoder placed after the systems) reads
  0.00 JS allocations a block in every row, where its MI.OUT rows read 0.23, 15 and 120 at 1, 64 and 512 frames with
  that emit: the script nodes alone take 0.87, 1.03 and 1.83 us a block with the LSDj in MIDI sync, and 0.85, 3.86 and
  23.84 us decoding MI.OUT at a byte every 8 frames.
- **On the Mega Drive core.** `pnpm run profile md-bench` (the wrapper's `stats` mode; the root script is named
  `profile`, which pnpm 10 routes to npm's own `profile` command, so the `run` is load-bearing), measured on a desktop
  box (x86-64, AMD Ryzen 9 9950X 16-Core) in the `build-prof` RelWithDebInfo host: 2000 blocks of 1024 samples at the
  driver's 44.1 kHz after the default 200-block warmup, one console per window. The three images whose only work is a
  chip-register write ran at **26 to 28x real time** (that run: fm-psg 27.68, psg 27.83, fm 25.96, at 834 to 894 us a
  block), and the four that watch their own VDP counters, take HINT and VINT, run a Z80 program and start a 68k-to-VDP
  DMA at 18.69 (hv-psg), 17.59 (vint-hint), 15.79 (z80-timing) and 15.58 (vint-dma). A real genmddj song, clocked
  through port 2 by `rp.genmddj.sync`, ran at **17.7x**. Every window read **0 allocs/block**, 0 bytes and
  `maxBlockAllocs` 0, with a peak heap of 336272 bytes, `liveHeapDelta` 0 and the end-of-window `runGc` freeing 0. Four
  runs of the file moved each figure by under 3% except the `fm` window, whose readings spread 25.96 to 28.74: the wall
  figures are the noisy half of these metrics, the counts are exact. The stop grid's own cost, measured natively in the
  core's invariance work (Release, 20 s of audio, 8192-frame blocks), was hvPsgRom 22.7x -> 19.7x and vintDmaRom
  19.2x -> 16.3x, so the engine, the resampler and the video path cost the couple of times of real time the two sets
  differ by, not an order of magnitude. The A53 device figure this line would have been paired with is unmeasured (the
  board was not available).
- **Ship path clean:** dsp-bench skips under the default host; the full `pnpm test:native`
  sweep and `pnpm test` stay green; the default `build/` and shipped plugin never
  define `RETROPLUG_PROFILE` (zero overhead).
- `tools/run-profile.sh callgrind|dhat|massif` attributed the kernel's churn to the marshalling +
  `makeCtx` closures + `routeBlock` (filtered top-N text / DHAT JSON), which is what the passes above
  removed.

## 10. Key files

Native ([packages/native/src/](../packages/native/src/)):
- [ScriptHost.cpp](../packages/native/src/host/script/ScriptHost.cpp): the script-node runtime, its
  per-block C→JS marshalling, and **where the allocator instrumentation and the trace window live**.
- [Engine.cpp](../packages/native/src/host/engine/Engine.cpp#L714): `processBlock` (the plan, with the
  `apu-render` and `state-publish` spans) + `stageMidi`.
- [EngineRpcService.cpp](../packages/native/src/host/rpc/EngineRpcService.cpp#L293): `renderAudio`
  (deterministic off-RT loop) + `stageMidiIn`; where introspection RPCs register.
- [AudioDriverRpcService.cpp](../packages/native/src/host/rpc/AudioDriverRpcService.cpp#L34): the
  threaded free-run (RT/soak path).
- QuickJS-ng vendored at `deps/dpf.js/deps/lv_binding_js/deps/txiki/deps/quickjs/quickjs.{h,c}` —
  `JSMallocFunctions` / `JS_NewRuntime2` / `JS_ComputeMemoryUsage` / `JS_SetGCThreshold` / `JS_RunGC`.

TypeScript ([packages/retroplug/](../packages/retroplug/)):
- [src/audioDriver.ts](../packages/retroplug/src/audioDriver.ts) — `renderAudio` /
  `stageMidiIn` / `setBpm` / `setTransport` (the bench's injection surface).
- [packages/*/ts/protocol.ts](../packages/core/ts/protocol.ts) with the node modules beside them / [lsdjArduinoboy.ts](../packages/lsdj/ts/lsdjArduinoboy.ts) / [core's midiRouting.ts](../packages/core/ts/midiRouting.ts): the protocol nodes the benchmarks drive, and the MI.OUT and master-sync decoders `rp.lsdj.serial-out` runs (§3).
- [test-native/audio-render.test.ts](../packages/retroplug/test-native/audio-render.test.ts) —
  the setup template to copy.
- [scripts/run-native-tests.mjs](../packages/retroplug/scripts/run-native-tests.mjs) — how a
  `test-native/*.test.ts` is bundled + run.
