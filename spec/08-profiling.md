# 08 — Profiling the audio thread

**Status: built** (compile-guarded behind `RETROPLUG_PROFILE`; run with `pnpm profile`). A
benchmark harness that measures what the DSP-thread **JavaScript runtime** does — allocations and garbage
collection — under an mGB + heavy-MIDI workload. The runtime model it profiles is
[01-architecture.md](01-architecture.md) (two QuickJS runtimes, the audio thread) and
[04-roles-dsp-kernel.md](04-roles-dsp-kernel.md) (the DSP role kernel this exercises).

## 1. The question

The DSP kernel runs per audio block in a **bare QuickJS-ng 0.15.1** context on the audio thread
([04-roles-dsp-kernel.md](04-roles-dsp-kernel.md) "Running on the bare DSP context"). We want to know
what that runtime costs per block — specifically its **allocation behaviour** — so the RT audio path can
be driven toward the ideal of *zero steady-state allocation*, and so a regression is caught
automatically. The workload of interest is **mGB** (the GB MIDI synth, the simplest DSP path — a role
that forwards every host-MIDI byte to serial) driven by a realistic, dense MIDI stream.

## 2. The reframe: measure refcount churn, not "GC"

QuickJS-ng is **reference-counted first, with a mark-sweep collector that exists only to reclaim
reference cycles.** This is the load-bearing fact for the whole plan:

- **Acyclic garbage is freed immediately and deterministically** the instant its refcount hits zero —
  no GC pass, no pause. The transient objects/arrays a block creates die as the block unwinds. The
  V8/JSC "GC pause" model does **not** apply here.
- The **mark-sweep GC** (`JS_RunGC`) only fires when live bytes exceed a threshold that self-tunes to
  **1.5× the live set** (initial 256 KiB), and is only *checked* on new **object** allocation
  (`js_trigger_gc`'s single call site is `JS_NewObjectFromShape`). A kernel with a bounded live set can
  run indefinitely and **never auto-GC**.

**Consequence:** the per-block cost you feel is **malloc/free churn from refcounting** — allocate-and-
free-within-the-block. The headline metric is **allocations/block and bytes/block**. Cycle-GC passes
are a secondary metric you track only to *prove they are ~zero* (any GC on the audio path is an RT
hazard — an unbounded sweep). A profiling effort framed around "GC pauses" would find almost none and
wrongly conclude all is well, while the real cost — a storm of tiny malloc/free pairs every ~23 ms —
goes unmeasured.

## 3. What the per-block path allocates today

The kernel header comments claim "no per-block allocation" ([dspKernel.ts:56](../packages/retroplug/src/dspKernel.ts#L56)).
That holds **only for the top-level reused `Block` struct**; in practice two layers allocate every
block, and the harness should expose this on day one.

**Native marshalling** ([DspRuntime.cpp:135-202](../packages/native-greenfield/src/DspRuntime.cpp#L135))
rebuilds a fresh JS object graph each block: an `input` object + 5 props; a `midiIn` array (per event: an
object + a nested `data` array + a JS int per byte); and `buttons`/`keys` arrays **created even when
empty**. The whole graph is freed after the call → per-block garbage.

**The TS kernel** ([dspKernel.ts:220-266](../packages/retroplug/src/dspKernel.ts#L220)) adds
per block:

| Site | Allocates |
|---|---|
| `const routed = new Map()` ([:220](../packages/retroplug/src/dspKernel.ts#L220)) | 1 Map every block |
| `keys.filter()` / `buttons.filter()` ([:232-233](../packages/retroplug/src/dspKernel.ts#L232)) | 2 arrays **per system** (even when empty) |
| `makeCtx(...)` object + 4 sink closures ([:252-266](../packages/retroplug/src/dspKernel.ts#L252)) | 1 `SystemCtx` + 4 closures **per system × per pipeline stage** |
| `routeBlock` inboxes + per-target event objects ([midiRouting.ts:68-73](../packages/retroplug/src/midiRouting.ts#L68)) | array graph + 1 `{frame,data}` per routed event |
| `mgb` role `forEach` closures ([dspRoles.ts:23](../packages/retroplug/src/dspRoles.ts#L23)) | 2 nested arrow closures per call |

The mGB **sink** path itself is clean — `pushSerialIn` crosses scalars via the C thunk
([DspRuntime.cpp:17-29](../packages/native-greenfield/src/DspRuntime.cpp#L17)) with no JS allocation, and
`serialIn_` is a cleared-not-freed member vector — but everything upstream churns. (`emitMidiOut`
[DspRuntime.cpp:32-56](../packages/native-greenfield/src/DspRuntime.cpp#L32), which *does* allocate per
call, is unused by mGB.)

**The TS-kernel rows are now optimized away** (the "hoist the ctx/closures/filters/routing-Map out of the
per-block path" pass): the per-system `SystemCtx` + its sink closures, the routed inboxes, and the
per-system key/button lists are built **once** at `setSystems` and mutated in place each block
([dspKernel.ts](../packages/retroplug/src/dspKernel.ts) `slots`/`buildCtx`,
`routeBlockInto` in [midiRouting.ts](../packages/retroplug/src/midiRouting.ts), and an
indexed-loop `mgb`). That cut Profile A from **~171 to ~32 allocs/block** (−81%). The residual per-block
churn is now the **native marshalling** row (the C→JS input object graph — a separate, deferred pass) plus
QuickJS per-call internals.

## 4. Approach

### 4a. Drive the real code path **off** the real-time thread

There is already a deterministic, device-free pull-render path:
[`EngineRpcService::renderAudio(ms)`](../packages/native-greenfield/src/EngineRpcService.cpp#L182) loops
`Engine::processBlock` in 1024-frame blocks synchronously on the **calling** thread — no audio device, no
RT scheduling, fully repeatable. It is exposed to TS as `createAudioDriver().renderAudio`
([audioDriver.ts](../packages/retroplug/src/audioDriver.ts)). Profile **this**, not a live
JACK/CLAP host: under a live host the measurement is confounded by RT jitter, CPU-frequency settling,
xruns, and the callback deadline itself — the very noise that swamps the allocation signal. Removing the
clock is what makes allocation and instruction counts **deterministic** (identical run to run), which is
the property an agent/CI needs to tell a real regression from noise.

The benchmark is a new `test-native/*.test.ts` run under `pnpm test:native` (which builds
`retroplug-host` and injects the real compiled kernel as `__DSP_KERNEL_BUNDLE__` —
[06-build-test.md](06-build-test.md)); copy [test-native/audio-render.test.ts](../packages/retroplug/test-native/audio-render.test.ts).
Shape: `setSystems({ midi-routing + mgb })`, then per block *stage this block's MIDI →
`renderAudio(oneBlock)`*. `renderAudio` also runs the SameBoy APU (C++), which dominates **wall-time** but
contributes **zero JS heap** — so the JS allocation counts stay cleanly isolated even though wall-time
does not.

### 4b. Primary signal: instrument QuickJS's own allocator (in-process, deterministic)

The direct, deterministic answer to "what does the JS runtime allocate per block" is to count every
allocation the DSP runtime makes. The DSP context ctor currently calls `JS_NewRuntime`
([DspRuntime.cpp](../packages/native-greenfield/src/DspRuntime.cpp) — binds 3 CFunctions after it); swap
to **`JS_NewRuntime2(&mf, &stats)`** with a counting `JSMallocFunctions` wrapper (quickjs-ng shape:
`js_calloc/js_malloc/js_free/js_realloc/js_malloc_usable_size`, `quickjs.h:451-457`). The wrapper bumps
cumulative `alloc_calls`/`alloc_bytes`/`free_calls` + live/peak and delegates to libc.

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
MIDI-over-serial; the `mgb` role forwards every byte verbatim
([dspRoles.ts:23](../packages/retroplug/src/dspRoles.ts#L23)). It responds to note on/off
(velocity 0 = note-off), a handful of CCs (pulse-width, envelope, sweep, PB-range, preset, pan, sustain),
pitch bend, and program change (1–15 = preset). Profiles:

- **A — 4-part arrangement @140 BPM** with two CC "LFOs" ≈ **105 events/s** (~2.4 messages/block).
  Musically plausible; the realistic default.
- **B — poly chord bursts** (each chord change = a 6-message burst landing in one block) — stresses
  per-block routing + FIFO fill, the interesting kernel path.
- **C — synthetic worst case** ≈ **700–860 events/s** — floods the serial FIFO / kernel forward loop to
  find the cliff (not musical).

Two cost levers: **MIDI density** stresses the *kernel/routing* path specifically (what we care about);
**system count** (4–8 cores in `setSystems`) is the *dominant* total-thread-cost multiplier because the
per-sample APU cost is MIDI-independent. A realistic heavy run combines both.

**Injection:** `audioDriver.stageMidiIn(bytes)` → `EngineRpcService::stageMidiIn`
([:221](../packages/native-greenfield/src/EngineRpcService.cpp#L221)) → `Engine::stageMidi` →
`pendingMidi_` at frame 0, consumed on the next `processBlock`
([Engine.cpp:63-78](../packages/native-greenfield/src/Engine.cpp#L63)). **Determinism recipe:** fixed SR
44100 / block 1024 (23.22 ms); `setBpm`/`setTransport` once; a **seeded PRNG** (mulberry32 — never
`Math.random`) pre-generates the whole `{block, msg[]}` schedule; loop `renderAudio(blockMs)` one block
at a time, staging each block's events first; a `renderAudio(1500)` warm-up (mGB is silent until firmware
init) is discarded. Gate on RMS band + wall-time, not exact samples.

**Limitation to note:** `stageMidiIn` hardcodes frame 0 and `renderAudio` consumes the whole pending
batch on its first block, so delivery is **block-quantized** (23.2 ms grid) — fine for mGB (the serial
fan ignores sub-block frame anyway). True sub-block jitter would need a one-arg `frame` addition to
`stageMidiIn` → `Engine::stageMidi` (the `MidiIn.frame` plumbing already exists).

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
  [`AudioDriverRpcService::audioLoop`](../packages/native-greenfield/src/AudioDriverRpcService.cpp#L32),
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

- **Native allocator instrumentation** ([DspRuntime.hpp/.cpp](../packages/native-greenfield/src/DspRuntime.cpp)):
  a counting `JSMallocFunctions` installed via `JS_NewRuntime2` (the opaque is a `DspAllocCounters`
  member; `js_malloc_usable_size` returns the real platform size so QuickJS's own accounting stays
  valid), a per-`processBlock` allocation bracket (native aggregation → no per-block RPC), and
  `allocStats()` / `resetAllocStats(disableAutoGc)` (pins `JS_SetGCThreshold(SIZE_MAX)`) / `runGc()`
  (timed `JS_RunGC`). All behind `#ifdef RETROPLUG_PROFILE`; the `#else` path returns `enabled:false`.
- **Introspection RPCs** — `dspAllocStats` / `dspResetAllocStats` / `dspRunGc` forwarded
  `Engine` → `EngineRpcService` → `BackendFacade` (reached directly through `engine_` in the quiescent
  `renderAudio` regime) and surfaced in [audioDriver.ts](../packages/retroplug/src/audioDriver.ts).
- **The seeded-MIDI benchmark** [test-native/dsp-bench.test.ts](../packages/retroplug/test-native/dsp-bench.test.ts):
  a mulberry32 generator for Profiles A/B/C, the block-stepped `stageMidiIn` + `renderAudio(BLOCK_MS)`
  loop (warmup window discarded), one JSON metrics line. Env knobs `RP_BENCH_{PROFILE,CORES,BLOCKS,WARMUP,SEED}`.
  No-ops (warns) under a non-profile host so the normal sweep stays green.
- **Build knob + scripts** — `RETROPLUG_PROFILE` ([CMakeLists.txt](../CMakeLists.txt), mirrors
  `RETROPLUG_SANITIZE`), `tools/run-profile.sh [stats|dhat|callgrind|massif]` (separate
  `build-prof/` RelWithDebInfo dir), and `pnpm profile`.

**Deferred:** a committed baseline + CI threshold gate, and a RealtimeSanitizer build variant, are not
built. `perf` / `heaptrack` are not installed in the devcontainer (valgrind 3.22 is), so those wrapper
modes are out.

## 9. Verification (done)

- `pnpm profile` builds `build-prof` and prints a JSON metrics line; the allocation counts are
  **bit-identical run-to-run** (only `xRT`/`gcMs` wall-clock vary). First measured result (Profile A, 1
  mGB core, 2000 blocks): **~171 allocs/block**, `freesPerBlock` ≈ allocs (balanced churn),
  `liveHeapDelta` ≈ 0 (not a leak), `maxBlockAllocs` 279.
- **Optimization pass (done):** hoisting the per-block ctx/closures/filters/routing-Map to `setSystems`
  (§3) cut Profile A to **~32 allocs/block** (−81%), **~2.3 KB/block** (−79%), `maxBlockAllocs` 279 → 60,
  behaviour-identical (41-file mock + 25-file native suites green).
- **Correction to the earlier cyclic-garbage read.** The end-of-window `runGc` reclaims ~595 KB in **both**
  the old and new kernels, and a **block-count sweep (500 / 1000 / 2000 / 4000 blocks) shows it is
  constant, not proportional to blocks** — so it is *not* per-block cyclic garbage from the ctx/closures
  (removing them left it unchanged). It is a **fixed, one-time setup structure** (the zod-based role
  registry built at kernel load) that production's auto-GC collects once early and never regenerates — not
  an unbounded audio-thread hazard. The real steady-state RT cost was the malloc/free churn, now ~5×
  lower; further reduction means the native marshalling row (§3), a separate pass.
- **Ship path clean:** dsp-bench no-ops under the default host; the full `pnpm test:native`
  sweep (25 files) + `pnpm test` (41) stay green; the default `build/` and shipped plugin never
  define `RETROPLUG_PROFILE` (zero overhead).
- `tools/run-profile.sh callgrind|dhat|massif` attributes the churn to the marshalling +
  `makeCtx` closures + `routeBlock` (filtered top-N text / DHAT JSON).

## 10. Key files

Native ([packages/native-greenfield/src/](../packages/native-greenfield/src/)):
- [DspRuntime.cpp](../packages/native-greenfield/src/DspRuntime.cpp) — the bare QuickJS runner + sink
  thunks + per-block C→JS marshalling; **where the allocator instrumentation goes**.
- [Engine.cpp](../packages/native-greenfield/src/Engine.cpp#L74) — `processBlock` + `stageMidi` + the
  serial-in fan.
- [EngineRpcService.cpp](../packages/native-greenfield/src/EngineRpcService.cpp#L182) — `renderAudio`
  (deterministic off-RT loop) + `stageMidiIn`; where introspection RPCs register.
- [AudioDriverRpcService.cpp](../packages/native-greenfield/src/AudioDriverRpcService.cpp#L32) — the
  threaded free-run (RT/soak path).
- QuickJS-ng vendored at `deps/dpf.js/deps/lv_binding_js/deps/txiki/deps/quickjs/quickjs.{h,c}` —
  `JSMallocFunctions` / `JS_NewRuntime2` / `JS_ComputeMemoryUsage` / `JS_SetGCThreshold` / `JS_RunGC`.

TypeScript ([packages/retroplug/](../packages/retroplug/)):
- [src/audioDriver.ts](../packages/retroplug/src/audioDriver.ts) — `renderAudio` /
  `stageMidiIn` / `setBpm` / `setTransport` (the bench's injection surface).
- [src/dspKernel.ts](../packages/retroplug/src/dspKernel.ts) / [src/midiRouting.ts](../packages/retroplug/src/midiRouting.ts) / [src/dspRoles.ts](../packages/retroplug/src/dspRoles.ts) — the per-block allocation sites (§3).
- [test-native/audio-render.test.ts](../packages/retroplug/test-native/audio-render.test.ts) —
  the setup template to copy.
- [scripts/run-native-tests.mjs](../packages/retroplug/scripts/run-native-tests.mjs) — how a
  `test-native/*.test.ts` is bundled + run.
