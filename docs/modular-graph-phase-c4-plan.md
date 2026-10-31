# Phase C4: the tap and the build check name no console - implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for
> tracking.

**Goal:** The engine and a system node's build check stop naming a console: the per-channel tap becomes a `ChannelTap`
capability any core may offer, and a node compares the core's `kind()` with the class's `Core::kKind` before a checked
`static_cast`. A Mesen core collects its audio in a fixed ring sized from the plan's block, so a warmed core's whole
block allocates and frees nothing, Mesen's per-frame video and input plumbing included; arming the tap sizes nothing;
and every ingress queue a block's host input lands in is a bounded ring sized when the core is built, with a push past
it dropped and counted. `SystemBase` loses the members nothing calls.

**Architecture:** Per
[modular-graph-phase-c4-design.md](modular-graph-phase-c4-design.md), which Task 6 lands as the record of
what was built. `MesenAudioDevice` becomes a fixed ring of interleaved stereo `int16_t`, capacity
`maxBlockFrames + 32768` frames, where 32768 is the mixer's own output buffer (`0x10000` `int16`, stereo); the plan's
maximum block reaches the core as `SystemBuildRequest::maxBlockFrames`, set by the system node in `activate` before
`build`, and `SystemBase::reserveAudio` records it for the cores' per-block scratches and the NES tap's rings. A Mesen
frame keeps its buffers instead of rebuilding them (`RenderedFrame::Reset` / `SetInputData`, a frame member per PPU,
`GetPortStates`/`GetRawState` filling the caller's storage, a per-depth notification copy) and `blip_buf` gains
`blip_reset`, which replays `blip_new`'s order so an arm re-initialises a stream's blip instead of allocating one.
`ChannelTap` (`system/ChannelTap.hpp`) is the engine-facing capability, `SystemBase::channelTap()` returns null by
default, and the plan's `SystemTable` slot holds `{ id, core, tap }`. Every ingress queue becomes
`rp::FixedQueue`/`rp::FixedByteRing` (with `rp::stableSortEntries` for the two roles' once-a-block sort), sized from
`system/InputCapacity.hpp`, whose numbers a case reads back from `script/nodes/ports.ts`.

**Tech stack:** C++20 with Catch2 (`retroplug-engine-graph-test`, `retroplug-graph-test`, `retroplug-audio-test`), the
in-tree Mesen fork (`deps/mesen`, ordinary tracked files of this repository, not a submodule), reflect-cpp, and the
graph layer of Phases A to C3. No TypeScript source changes: C4 is native, and its TS-visible surface is unchanged.

**Provenance:** every code block below was built and run before this plan was written, on the scratch branch
`proto/c4-cores` in this worktree, on top of the design commit 33d39f39: six commits, one per task (Task 7 is the
acceptance run and commits nothing), with GCC 13.3 on linux-x64 (a Ryzen 9 9950X). A block that creates a file is that
file as the prototype commit left it; one that changes files is the prototype's diff for them, so `git apply` it from
the step's text (or apply it hunk by hunk with the Edit tool). `deps/mesen`'s files are part of this repository, so
their diffs apply like any other. Every expected pass, test count and figure is quoted from the stage reports in
/tmp/c4pro or from the tip verification's logs there (`tv-*.log`), which is what the generator checks; a failing-test
step's expected failure is what the missing code produces, as each step says, plus the prototype's measured red where
a report records one; where no report states a count, the step says what to check instead.
When this plan was generated it was replayed in a scratch clone of 33d39f39, every block written or applied in order,
and each task committed with the command given: each commit's tree came out exactly as the prototype's, and each
commit message as its. The prototype commits, by task:

- Task 1: `1f84ecbf` refactor(system): SystemBase drops the members nothing calls
- Task 2: `e30c39f5` fix(mesen): a Mesen core collects its audio in a fixed ring, so its audio path allocates nothing
- Task 3: `b63556e7` fix(mesen): a frame's video and input plumbing reuses its buffers, so a whole block allocates nothing
- Task 4: `8dcd3e02` refactor(engine): the NES tap is a capability any core may offer, so the engine names no console
- Task 5: `cb9e8f95` fix(system): a core's input queues are preallocated rings, so a block carrying host input allocates nothing
- Task 6: `c1fc0d68` docs: the specs say what C4 built, the input-queue stage included

**Where this plan departs from the design as committed (33d39f39).** The design was written before the prototype; this
plan follows what was built, and Task 6 lands the design with its Status line, its sections rewritten as "as built",
section 9 (the plumbing), section 10 ("Found while building") and section 11 (the input queues). In short:
- **C4 is six commits, not four: two scopes were added while it was built, both by the user, and each is a commit of
  its own.** The first is **Mesen's frame and input plumbing** (Task 3, the design's section 9), added when the
  ring's zero-allocation case found a Mesen console allocating 13 to 18 times per emulated frame inside Mesen
  itself: 738 allocations in a 4 x 8192-frame window, the same for a NES and an SMS, and 0 in the same window after
  the commit. None of it was the audio path, but "a warmed block allocates nothing" could not be true while it
  stayed.
- **The second added scope is the ingress queues** (Task 5, the design's section 11), added after the docs review
  found the promise held only while the node handed its core nothing: the counted windows warmed with a press and
  then staged nothing, so every queue a block's input lands in could grow inside a window. Staged properly, the
  NES's first counted window showed 7 allocations and 7 frees (11 and 7 with the reliable link on, 7 and 7 with the
  tap armed), the Engine section 1 and 4, the SMS 1 and 1, the Game Boy 4 and 2; every section is 0 and 0 after the
  commit.
- **The ring is sized through the build request, not by a reserve after construction.** `SystemBuildRequest` carries
  `maxBlockFrames`, a node sets it from its plan's maximum before `build`, and both backends call `reserveAudio`
  before `onActivate`, so a core is the plan's size from construction; a build outside a plan leaves it 0 and the
  core sizes for 8192 (held to `Engine::kDefaultMaxBlockFrames` by a test). Dropping the field loses a memory
  saving: a core built at the default and reserved down keeps the larger ring for its life, so a 64-frame plan would
  pay for a plan it never runs (128.25 KiB against 160 KiB for the ring; 2,880 B against 328,000 B for a Game Boy's
  scratches).
- **`blip_reset` is new in the in-tree `blip_buf`, and it is what makes an arm allocation-free.** Preallocating the
  five stream blips at activation *and* rate-setting them there moved the tap's audio, because `blip_clear` seeds a
  blip's fractional offset from `factor / 2`: parity caught it (`parity/edits-structure`, `pinSplit-then-stereo`,
  lane 0, the same RMS and a different digest). `blip_reset(m, size)` replays `blip_new`'s order exactly (the
  default factor, then `blip_clear`), `SetCaptureCapacity` creates the blips with the rings, and an arm
  re-initialises one and then calls `blip_set_rates`, which is where a fresh `blip_new` plus `blip_set_rates` at the
  arm would be.
- **The counting allocator's C half is opt-in per window and off in three places.** `CountingAllocator` now
  interposes `malloc` / `calloc` / `realloc` / `free` and forwards to glibc's own, on request
  (`beginCountingAllocations(true)`); it is off in a sanitizer build (ASan and TSan own the allocator: the first
  attempt segfaulted TSan at startup), on Windows (the static CRT collides with `libcmt`) and on macOS (no glibc
  name), and `countsCAllocations()` says which. The audio-path and whole-block helpers ask for it unconditionally
  and do not skip, so on Windows and macOS they prove the `operator new` half; only the live-arm case skips, because
  a stream's blip is a C block and is its one subject. Forced on, it changes exactly one existing case: the script
  block path's 448 allocations are QuickJS's own.
- **The `[video]` hashes are a check, recorded at the commit before the plumbing and equal after it.** Three
  4096-frame blocks per console, the published frame hashed by FNV-1a over its width, height and XRGB8888 bytes: NES
  14252490387203307000 three times, SMS 16816052646246439624, 16816052646246439624, 668230188373870873, Game Boy
  8259550387572866035, 10074299620259861403, 13782190143996438851, GBA 3100562949282106931, 3100562949282106931,
  2667698829920294451. The NES's three are its boot screen, before bliptoaster draws; a real video change re-records
  that console's row in the same commit, as parity re-records a group.
- **The kind check is a 12-way matrix, not one direction.** Each of the four consoles' nodes against the three other
  kinds, requiring the refusal, `activate-failed`, the same-node message, no built core and the core given back. The
  expected kind is a `static constexpr SystemKind kKind` on each core class, which its `kind()` returns, so the
  value the node compares with is the one the core answers by construction. Its seam is a `NodeRig` option
  (`reportKind`) that wraps the real core, because the real host cannot produce the mismatch. Its red is not a clean
  failure: with the comparison removed the node casts the wrong core and corrupts the heap.
- **Three residuals are deliberate, and the docs say so.** `Engine::setGraph` still refuses a tap mode outside 1 to
  3, which is the NES's numbering rather than the capability's (a later `maxMode()`); `Engine::setGraph` still names
  `SystemKind::MesenNes` for the N8 cart mirror, which is a fact about an N8 being a NES's cart reader rather than a
  cast or an arming; and the test suite keeps its console-only casts (a config field, `linkPeers_`, a ring counter),
  which are the core's own business. No tap read casts any more, and `packages/native/src` has no `dynamic_cast`
  outside `src/system/`.
- **What still allocates inside a long run, and what does not.** Inside a warmed block: nothing, with host input
  staged into it, on the consoles the cases run. Inside a long run, still Mesen's own: the periodic state publish
  (0.5 s, its streaming serializer, charged outside every counted window, which is why the cases on an Engine-built
  core charge a whole interval first), the N8 firmware's SD-command path (`std::string` and `std::filesystem` in the
  command handlers, a bigger directory listing, `_ioScratch`'s growth to a new read size, and `_txQueue`'s unbounded
  growth, which a ring would turn into a contract change), and two output-side buffers (`_txQueue`'s regrowth after
  a debug drain, a Game Boy's `serialOutLog_` capacity under MI.OUT capture). Nothing else: C2's "8 allocations in
  the first block after a setting change" turned out to be Mesen's per-frame video cost, present in every block,
  which Task 3 removed, so the design's section 1 has no such item left. The design's "seven empty slots" the
  deleted snapshot publish walked is nine (`MemoryType.hpp`'s `kMemoryTypeCount`), corrected in Task 6.

**Ground rules for whoever executes this:**
- This plan lives at `docs/modular-graph-phase-c4-plan.md` and is committed before Task 1 (a docs commit of its own):
  the header's link to the design is relative to that location; every command and path in the steps is relative to
  the worktree root.
- Commit after each task (the commands are given; Task 7 commits nothing unless a step fails and is fixed, in a commit
  of its own). No `Co-Authored-By` or "generated with" trailer.
- Do not push, and do not open a PR.
- Never use an emdash character in code, comments, docs or commit messages; use " - ", ":" or ";". Some diffs below
  carry emdashes on lines the prototype did not write (context and removed lines of older code): leave those be.
- Run every command from the worktree root with absolute paths or a subshell (`(cd x && ...)`); never `cd` the session
  itself.
- Don't `rm -rf build` to fix CMake; investigate. Never use bare `git stash`, and never switch branches with uncommitted
  work in the tree. Never touch a submodule. `deps/mesen` is NOT a submodule: it is a tree of ordinarily tracked files
  in this repository, and Task 3's diffs of them apply like any other file's.
- `deps/sameboy` shows as modified after any configure. That is the tracked audio patch: expected, leave it.
- The derived bundle C arrays (`build/native/*bundle_data.c`) are never committed.
- Parity holds the engine's output to the bit, and no task re-records it: the ring changes where samples wait, not
  which samples are made. A strict parity failure is a real change to the audio: stop and find it. Never set
  `RP_PARITY_UPDATE`. The `[video]` hashes are recorded (Task 3) at the commit before the plumbing, and unchanged by
  it.
- The fixture groups need `RETROPLUG_RESOURCES_DIR=/workspaces/resources` on the same command line; without it their
  cases skip. A shell `export` does not survive between commands.
- `node packages/retroplug/scripts/run-native-tests.mjs` (what `pnpm test:native` runs) takes ONE filter: an exact slug
  or a prefix in the dash form. Run several files as separate commands.
- The Catch2 binaries are `EXCLUDE_FROM_ALL`: `cmake --build build` does not relink them, so a library edit needs the
  test target built by name before a run. A new test source (Tasks 3 and 5 add one each) needs `cmake -S . -B build`
  first.
- The sanitizer trees are their own: `build-tsan/` and `build-asan/` (configured with `-DRETROPLUG_SANITIZE=thread` or
  `=address`). Configure one only if its `CMakeCache.txt` is missing. The profile build is its own tree too
  (`build-prof/`, `tools/run-profile.sh`).
- The prototype branch `proto/c4-cores` is this plan's provenance; nothing here reads it, so it may be deleted once
  the plan has been executed and verified.
- While a Reaper check runs it uses `build/bin`: do not rebuild the plugin until it finishes. Run the Reaper checks one
  at a time, and before each make sure no Reaper or jackd of an earlier run is left (`pgrep -a reaper; pgrep -a jackd`).
- `pnpm check:docs` counts the tracked docs: 68 in the prototype, which had no plan file, so 69 once this plan is
  committed.

---

## File structure

| Path | Responsibility |
|---|---|
| `packages/native/src/system/SystemBase.hpp`, `SystemBase.cpp` | the surface after P1 (Task 1); `reserveAudio` / `blockFrames` and `maxBlockFrames_` (Task 2); `channelTap()` (Task 4); `inputAccepted` / `inputDropped` (Task 5) |
| `packages/native/src/system/mesen/*`, `system/sameboy/*` | each core's overrides removed (Task 1); `MesenAudioDevice`'s ring and each core's activation-time sizing (Task 2); `MesenBackend`'s build request (Task 3); `kKind` (Task 4); the bounded queues (Task 5) |
| `packages/native/src/system/mesen/MesenAudioDevice.hpp` | the fixed stereo ring: head and count, `reserveAudio`, `capacityFrames()`, `overflowFrames()` (Task 2) |
| `packages/native/src/host/graph/SystemNodes.cpp`, `SystemTable.hpp`, `Engine.cpp` | the block size handed to the core (Task 2); no cast to a console and the kind check (Task 4); the staging doors' command capacity (Task 5) |
| `packages/native/src/graph/GraphHost.hpp` | `commandCapacity()`, which the press ring's `static_assert` holds (Task 5) |
| `packages/native/src/system/ChannelTap.hpp` | the tap capability (Task 4) |
| `packages/native/src/util/FixedQueue.hpp`, `system/InputCapacity.hpp` | the bounded queues and the ingress capacities (Task 5) |
| `packages/native/src/system/mesen/roles/*`, `transport/N8ReliableLink.hpp`, `NesEverdriveFifo.hpp` | the roles' and the FIFO's queues over `FixedQueue` / `FixedByteRing` (Task 5) |
| `deps/mesen/` | the in-tree tap rings (Task 2); the frame and input plumbing, the notification pool and `blip_reset` (Task 3) |
| `packages/native/test/engine-graph/*`, `test/graph/*`, `test/audio/*` | the `[alloc]`, `[video]`, `[tap]`, `[input-drops]` cases, `MesenShared.test.cpp`, `FixedQueue.test.cpp`, the audio suite's queue cases (Tasks 1 to 5) |
| spec/, docs/, `AGENTS.md` | the specs, the main design, the B1 and C4 designs (Tasks 3, 5 and 6) |

---

### Task 1: SystemBase drops the members nothing calls

Design section 3. Every member the design's section 1 lists goes, with each core's overrides and the calls that only
fed them: the battery-path accessors (`savSuffix`/`setSavSuffix`/`savPath`/`setSavPath`), the `fastBoot()` getter,
`clearSram`, the public `loadStateBytes`, `midiOut()`/`midiOut_`, and the live memory-snapshot mechanism
(`enableMemorySnapshot`, `disableMemorySnapshot`, `memorySnapshot`, `publishMemorySnapshots`, `snapshots_`,
`SnapshotEntry`), whose publish every core's `finishBlock` still ran over an always-empty array. The four cores' config
structs lose `savSuffix`/`savPath`, whose only readers were the deleted accessors. `kind()` stays (Task 4 gives it its
first production caller) and so does the whole-savestate snapshot the registry reads. No test read a deleted member, so
no test moved; the one case this task adds pins what the private `loadStateBytes` still does for a seed.

**Files:**
- Modify: `packages/native/src/system/SystemBase.cpp`, `packages/native/src/system/SystemBase.hpp`, `packages/native/src/system/mesen/MesenGbaConfig.hpp`, `packages/native/src/system/mesen/MesenGbaSystem.cpp`, `packages/native/src/system/mesen/MesenGbaSystem.hpp`, `packages/native/src/system/mesen/MesenNesConfig.hpp`, `packages/native/src/system/mesen/MesenNesSystem.cpp`, `packages/native/src/system/mesen/MesenNesSystem.hpp`, `packages/native/src/system/mesen/MesenSmsConfig.hpp`, `packages/native/src/system/mesen/MesenSmsSystem.cpp`, `packages/native/src/system/mesen/MesenSmsSystem.hpp`, `packages/native/src/system/sameboy/SameBoyConfig.hpp`, `packages/native/src/system/sameboy/SameBoySystem.cpp`, `packages/native/src/system/sameboy/SameBoySystem.hpp`, `packages/native/src/transport/MemorySnapshotTriple.hpp`, `packages/native/test/engine-graph/SystemNode.test.cpp`

- [ ] **Step 1: Write the case the deletion must keep green**

`SystemNode.test.cpp` gains "a NES node built from its ROM resource restores a savestate seed, bit for bit". It runs
the in-repo BlipToaster ROM until a note sounds, takes a state at that block boundary, and compares a node built with
`"state":"res:N"` against a directly built `MesenNesSystem` whose `cfg.savestate` is the same bytes, for one second,
with a probe that the seed's first block sounds (512 frames: a 64-frame first block is still silent after a working
restore). It is green before this task's deletion, which is what makes the deletion a refactor: its red is the
deletion's own hazard, and the report records it:

with the NES's private `loadStateBytes` override deleted temporarily (header declaration and `.cpp` body, built, not
committed), `REQUIRE(sounds(first))` failed; restored, the case is green. That red is the seed probe rather than the
twin comparison: removing the override breaks the direct core and the node together, while the compare catches a
node-only regression such as a dropped `request.savestate`. The same diff takes the neighbouring gain case's first
block from 64 to 512 frames.

````diff
diff --git a/packages/native/test/engine-graph/SystemNode.test.cpp b/packages/native/test/engine-graph/SystemNode.test.cpp
index 3453fe3d..49ba9599 100644
--- a/packages/native/test/engine-graph/SystemNode.test.cpp
+++ b/packages/native/test/engine-graph/SystemNode.test.cpp
@@ -2180,7 +2180,7 @@ TEST_CASE("a system node placed at a gain plays its first block at it, from the
     probe->onActivate(kRate);
     probe->setGainDb(-12.0f, true);
     Direct first({probe.get()}, 2, {{0, 1}});
-    first.block(64);
+    first.block(512);
     REQUIRE(sounds(first));
     // Bit for bit against a core snapped the same way: without the snap the node's first block sits at unity and ramps
     // down, which the comparison covers inside its first 20 ms.
@@ -2227,6 +2227,48 @@ TEST_CASE("a non-default model reaches a Game Boy core before it boots, so a sav
     CHECK(sounds(d));
 }
 
+TEST_CASE("a NES node built from its ROM resource restores a savestate seed, bit for bit",
+          "[engine-graph][system-node][resources]") {
+    // A savestate is a build seed, and a Mesen core's restore is a private virtual reached only through
+    // seedSramAndState, so this pins the dispatch: the node's core and a directly built one restore the same bytes
+    // and render the same block, the N8 flush a restore makes included. The seed is taken while the ROM is playing a
+    // note, so a core that skips the restore starts silent and the comparison sees it. The ROM is in the repository,
+    // so CI runs it.
+    NodeRig rig(kRate);
+    auto source = nes(9, 0);
+    Direct run({source.get()}, 2, {{0, 1}});
+    std::vector<std::uint8_t> state;
+    for (int b = 0, at = 0; b < 400 && state.empty(); ++b, at += 512) {
+        if (at < static_cast<int>(1.5 * kRate) && at + 512 >= static_cast<int>(1.5 * kRate)) noteOn(*source, 0, 60);
+        run.sounded = false;
+        run.block(512);
+        if (run.sounded) state = source->saveStateBytes();  // the note is sounding across the next boundary
+    }
+    REQUIRE(!state.empty());
+    MesenNesConfig cfg;
+    cfg.romPath = RP_BLIPTOASTER_ROM_PATH;
+    cfg.savestate = state;
+    auto direct = std::make_unique<MesenNesSystem>(1, cfg, readFile(RP_BLIPTOASTER_ROM_PATH));
+    direct->onActivate(kRate);
+    // The seed's first block carries signal, which is what makes a core that boots fresh instead of restoring audible.
+    auto probe = std::make_unique<MesenNesSystem>(1, cfg, readFile(RP_BLIPTOASTER_ROM_PATH));
+    probe->onActivate(kRate);
+    Direct first({probe.get()}, 2, {{0, 1}});
+    first.block(512);
+    REQUIRE(sounds(first));
+    const auto romId = resource(rig, "rom", readFile(RP_BLIPTOASTER_ROM_PATH));
+    const auto stateId = resource(rig, "state", state);
+    REQUIRE(rig.set(R"({"nodes":[{"id":"sys-1","type":"rp.mesen.nes","settings":{)" +
+                    rig.boot("rp.mesen.nes", 1, R"("state":"res:)" + std::to_string(stateId) + "\"", romId) +
+                    R"(}},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[2]}}],
+                        "wires":[{"from":"sys-1.mix","to":"out.out.1-2"}]})").ok);
+    SystemBase* const built = rig.builtCore(1);
+    REQUIRE(built != nullptr);
+    Direct d({direct.get()}, 2, {{0, 1}});
+    CHECK(compare(rig, d, 1.0) == "");
+    CHECK(sounds(d));
+}
+
 TEST_CASE("a NES node built from its ROM resource takes its region, export mode and expansion volume",
           "[engine-graph][system-node][resources]") {
     const auto rom = readFile(RP_BLIPTOASTER_ROM_PATH);
````

Run: `cmake --build build --target retroplug-engine-graph-test -j$(nproc) && build/bin/retroplug-engine-graph-test "a NES node built from its ROM resource restores a savestate seed, bit for bit"`
Expected: `All tests passed`: the new case is green on this tree, before the deletion, which is what makes the deletion a refactor. (Optional, but it is the evidence that the red above is the deletion's own hazard.)

- [ ] **Step 2: Delete the surface from SystemBase**

The members go from `SystemBase.hpp` and their definitions from `SystemBase.cpp` (with the anonymous-namespace
`typeIndex` helper that only they used), and each of the four `finishBlock`s loses its snapshot publish.
`loadStateBytes` stays as a **private** virtual so `seedSramAndState` keeps one dispatch point for the three Mesen
restore bodies; SameBoy's override goes entirely, since its seed path already restores `config_.savestate` inline.
`MemorySnapshotTriple.hpp`'s class comment is rewritten for its two real users (SystemBase's whole-savestate triple
and the registry's shadows), because the old one described the deleted subscribe mechanism:

````diff
diff --git a/packages/native/src/system/SystemBase.cpp b/packages/native/src/system/SystemBase.cpp
index 20b1e2a0..e219074e 100644
--- a/packages/native/src/system/SystemBase.cpp
+++ b/packages/native/src/system/SystemBase.cpp
@@ -1,17 +1,8 @@
 #include "system/SystemBase.hpp"
 
-#include <algorithm>
 #include <cstdio>
 #include <cstring>
 
-namespace {
-
-constexpr std::size_t typeIndex(rp::MemoryType t) {
-    return static_cast<std::size_t>(t);
-}
-
-} // namespace
-
 void SystemBase::onProcess(const AudioBlockInfo& info, float* const* outs) {
     // Drive the triad to the block target, then finish: what an unlinked
     // system's node does each block (host/graph/SystemNodes.cpp).
@@ -20,46 +11,6 @@ void SystemBase::onProcess(const AudioBlockInfo& info, float* const* outs) {
     finishBlock(info, outs, 2); // the convenience entry is stereo (one Mix stream = two lanes)
 }
 
-bool SystemBase::enableMemorySnapshot(rp::MemoryType type) {
-    const std::size_t idx = typeIndex(type);
-    if (idx >= rp::kMemoryTypeCount) return false;
-    auto& entry = snapshots_[idx];
-
-    // Already streaming: bump the refcount and we're done.
-    if (entry.triple) {
-        ++entry.refcount;
-        return true;
-    }
-
-    // Probe the region first. Unsupported types return an invalid accessor
-    // and we refuse subscription so the UI can branch cleanly.
-    rp::MemoryAccessor probe = getMemory(type, rp::AccessType::Read);
-    if (!probe.valid()) return false;
-
-    // Size cap protects the audio thread from per-block memcpy of multi-MB
-    // regions. ROM / GBA EWRAM / large SRAM are one-shot only.
-    if (probe.size() > kMaxStreamableBytes) return false;
-
-    entry.triple   = std::make_unique<MemorySnapshotTriple>(probe.size());
-    entry.refcount = 1;
-    return true;
-}
-
-void SystemBase::disableMemorySnapshot(rp::MemoryType type) {
-    const std::size_t idx = typeIndex(type);
-    if (idx >= rp::kMemoryTypeCount) return;
-    auto& entry = snapshots_[idx];
-    if (!entry.triple) return;
-    if (entry.refcount > 0) --entry.refcount;
-    if (entry.refcount == 0) entry.triple.reset();
-}
-
-MemorySnapshotTriple* SystemBase::memorySnapshot(rp::MemoryType type) {
-    const std::size_t idx = typeIndex(type);
-    if (idx >= rp::kMemoryTypeCount) return nullptr;
-    return snapshots_[idx].triple.get();
-}
-
 bool SystemBase::runUntilPc(std::uint32_t target, std::uint64_t maxCycles) {
     std::optional<std::uint32_t> pc = getProgramCounter();
     if (!pc) return false;            // backend has no CPU / no program counter
@@ -76,19 +27,6 @@ bool SystemBase::runUntilPc(std::uint32_t target, std::uint64_t maxCycles) {
     return false;
 }
 
-void SystemBase::publishMemorySnapshots() {
-    for (std::size_t i = 0; i < rp::kMemoryTypeCount; ++i) {
-        auto& entry = snapshots_[i];
-        if (!entry.triple) continue;
-        rp::MemoryAccessor accessor =
-            getMemory(static_cast<rp::MemoryType>(i), rp::AccessType::Read);
-        if (!accessor.valid()) continue;
-        const std::size_t n = std::min(accessor.size(), entry.triple->size());
-        std::memcpy(entry.triple->writeSlot(), accessor.data(), n);
-        entry.triple->publish();
-    }
-}
-
 // The slot layout is [len:4 LE][savestate bytes][unused tail]. The tail is
 // never read (readers honour len), so it's left stale rather than re-zeroed.
 static constexpr std::size_t kStateLenPrefix = 4;
diff --git a/packages/native/src/system/SystemBase.hpp b/packages/native/src/system/SystemBase.hpp
index b9949de9..689f7da2 100644
--- a/packages/native/src/system/SystemBase.hpp
+++ b/packages/native/src/system/SystemBase.hpp
@@ -1,11 +1,11 @@
 #pragma once
 
+#include <algorithm>
 #include <array>
 #include <cstdint>
 #include <cstring>
 #include <memory>
 #include <optional>
-#include <string>
 #include <string_view>
 #include <vector>
 
@@ -133,38 +133,10 @@ public:
     // Returns nullptr for systems without video (or before activation).
     virtual FrameBufferTriple* framebuffer() { return nullptr; }
 
-    // Per-block MIDI output, drained by PluginDSP into DPF's writeMidiEvent
-    // after onProcess.
-    std::vector<::MidiEvent>&       midiOut()       { return midiOut_; }
-    const std::vector<::MidiEvent>& midiOut() const { return midiOut_; }
-
-    // -- Per-system menu actions --------------------------------------------
-    //
-    // Surfaces used by the UI menu (Save SRAM, Save State, Duplicate, etc.).
-    // Default returns mean "this backend doesn't support the feature" — the
-    // UI gates the menu row off when appropriate.
-
-    // Loose-battery disambiguator. 0 => this system owns the plain sibling
-    // `<rom>.sav`; N>=2 => `<rom>-N.sav`. Lets multiple systems backed by the
-    // same ROM file (Duplicate Instance / loading the same file twice) keep
-    // independent battery files instead of clobbering one another. Persisted
-    // in the per-system config; the sav-path derivation lives in TS (savPaths.ts).
-    virtual std::uint32_t savSuffix() const { return 0; }
-    virtual void          setSavSuffix(std::uint32_t /*suffix*/) {}
-
-    // Explicit battery-file override. Empty => the loose `.sav` is derived from the ROM's path and this suffix
-    // (default). Non-empty => this exact file, set when the user pairs a hand-picked `.sav` with the ROM; all battery
-    // I/O targets it. The sav-path derivation lives in TS (savPaths.ts).
-    virtual const std::string& savPath() const {
-        static const std::string empty;
-        return empty;
-    }
-    virtual void          setSavPath(const std::string& /*path*/) {}
-
-    // Boot-time toggle. SameBoy maps this to fastBoot; GBA to skipBootScreen.
-    // Mesen returns nullopt → the UI hides the Fast boot row.
-    virtual std::optional<bool> fastBoot() const { return std::nullopt; }
-    virtual void                setFastBoot(bool /*on*/) {}
+    // Boot-time toggle. SameBoy's node setting maps it to fastBoot; the GBA core maps it to skipBootScreen, but no
+    // node setting reaches that one today (its node builds from the config's default). A core that has no boot-screen
+    // skip keeps the no-op default, and the node does not offer it.
+    virtual void setFastBoot(bool /*on*/) {}
 
     // Live output gain in dB — a universal per-system setting every backend
     // honours (smoothed at the audio frame). `immediate` snaps to the value
@@ -176,12 +148,10 @@ public:
     // Cartridge battery RAM. Empty vector when the cartridge has no battery
     // or the backend doesn't yet support snapshotting.
     virtual std::vector<std::uint8_t> saveSramBytes() const { return {}; }
-    virtual void                       clearSram() {}
 
     // Savestate, byte-for-byte. False on unsupported backends or malformed
     // buffers.
     virtual std::vector<std::uint8_t> saveStateBytes() const { return {}; }
-    virtual bool loadStateBytes(const std::vector<std::uint8_t>& /*bytes*/) { return false; }
 
 
     // -- Memory access -------------------------------------------------------
@@ -258,41 +228,6 @@ public:
     // is owned by the system and invalidated by reset/deactivation.
     virtual rp::IDebugTarget* debugTarget() { return nullptr; }
 
-    // -- Live memory snapshots ----------------------------------------------
-    //
-    // The DSP thread publishes tear-free snapshots into MemorySnapshotTriple
-    // buffers after each onProcess(). The UI thread reads them at uiIdle.
-    //
-    // Subscription is refcounted: multiple UI consumers can subscribe to the
-    // same (system, type); the triple-buffer is allocated on the 0→1
-    // transition and freed on the 1→0 transition. enable/disable are called
-    // from the DSP thread (via the SubscribeMemory / UnsubscribeMemory
-    // command drain) so memorySnapshot() and publishSnapshots() never race.
-
-    // DSP thread: bump refcount. Allocates the triple on first reference.
-    // Returns true on success; false if `type` is unsupported on this
-    // system (the matching getMemory() returns an invalid accessor).
-    bool enableMemorySnapshot(rp::MemoryType type);
-
-    // DSP thread: drop refcount. Frees the triple on last release. No-op if
-    // not subscribed.
-    void disableMemorySnapshot(rp::MemoryType type);
-
-    // UI thread: get the triple to read from. Returns nullptr if not
-    // subscribed. Cheap atomic load.
-    MemorySnapshotTriple* memorySnapshot(rp::MemoryType type);
-
-    // Cap on the size we'll triple-buffer for live streaming. Larger
-    // regions (ROM, GBA EWRAM, large SRAM) refuse subscription and are
-    // one-shot only via getMemory RPC. 64 KiB covers GB RAM (8 KB), GBA
-    // IWRAM (32 KB), NES nametables (2 KB), OAM (≤ 1 KB) comfortably.
-    static constexpr std::size_t kMaxStreamableBytes = 64 * 1024;
-
-    // DSP thread: called from each concrete system's onProcess() AFTER the
-    // emulator step finishes. Walks active subscriptions, copies each
-    // region into its triple-buffer, publishes. Cheap when no subs.
-    void publishMemorySnapshots();
-
     // -- Whole-savestate snapshot -------------------------------------------
     //
     // The DSP thread captures the entire savestate into a triple-buffer on a
@@ -345,9 +280,10 @@ public:
     std::size_t stateSnapshotCapacity() const;
 
     /** Seed a freshly-built core from its construct spec: cartridge battery first, then a savestate over
-     *  the top if one was supplied. Both Mesen and SameBoy backends end onActivate with exactly this, so
-     *  it lives here rather than three times over. A savestate LAST is deliberate - it carries its own
-     *  SRAM, so restoring it after the seed is what makes "boot from a savestate" mean what it says. */
+     *  the top if one was supplied. The Mesen backends end onActivate with exactly this, so it lives here
+     *  rather than three times over; SameBoy seeds its own battery and restores its state inline. A
+     *  savestate LAST is deliberate - it carries its own SRAM, so restoring it after the seed is what makes
+     *  "boot from a savestate" mean what it says. */
     void seedSramAndState(const std::vector<std::uint8_t>& sram, const std::vector<std::uint8_t>& state) {
         if (!sram.empty()) {
             auto accessor = getMemory(rp::MemoryType::Sram, rp::AccessType::ReadWrite);
@@ -360,8 +296,6 @@ public:
     }
 
 protected:
-    std::vector<::MidiEvent> midiOut_;
-
     // DSP thread (or the control thread while quiescent): the backend has rebuilt its core in place —
     // a SameBoy model switch — so the published blob and region table now describe a core that is gone.
     // Arms an immediate republish, which refreshes both together within one block instead of up to
@@ -382,11 +316,10 @@ protected:
     virtual StateRegionTable stateSnapshotRegions() const { return {}; }
 
 private:
-    struct SnapshotEntry {
-        std::unique_ptr<MemorySnapshotTriple> triple;
-        std::uint32_t                         refcount = 0;
-    };
-    std::array<SnapshotEntry, rp::kMemoryTypeCount> snapshots_;
+    // Restore a savestate over the core seedSramAndState() has already put its battery into. Private
+    // because a savestate is a build seed: the only restore is that one, on a core that was just
+    // built, and a core never has a state loaded into it mid-life.
+    virtual bool loadStateBytes(const std::vector<std::uint8_t>& /*bytes*/) { return false; }
 
     // Whole-savestate snapshot. The triple is allocated once at enable and freed only in the destructor,
     // never resized mid-life, for two reasons: publishStateSnapshot can run on the audio thread, where
diff --git a/packages/native/src/transport/MemorySnapshotTriple.hpp b/packages/native/src/transport/MemorySnapshotTriple.hpp
index 696d606f..0af55ef3 100644
--- a/packages/native/src/transport/MemorySnapshotTriple.hpp
+++ b/packages/native/src/transport/MemorySnapshotTriple.hpp
@@ -11,14 +11,17 @@
 // reader-hint discipline as FrameBufferTriple, but the slot type is a
 // fixed-size std::vector<std::uint8_t> sized at construction.
 //
-// Writer: the DSP thread, between emulator-step boundaries (after
-// onProcess() finishes a block — guaranteed internally consistent state).
-// Reader: the UI thread, at uiIdle cadence. Tear-free for multi-byte
-// structs because the writer copies a complete buffer into a stable slot
-// before publishing.
+// Writer: the block thread, between emulator-step boundaries (after onProcess()
+// finishes a block, guaranteed internally consistent state).
+// Readers: SnapshotRegistry::publishSystem on that same block thread, for
+// SystemBase's whole-savestate triple, and the control thread for the
+// registry's own shadows. Tear-free for multi-byte structs because the writer
+// copies a complete buffer into a stable slot before publishing.
 //
-// Use case: per-(system, type) memory snapshot for live UI subscriptions.
-// Allocated on subscribe, freed on unsubscribe.
+// Users: SystemBase's stateSnapshot_ (the whole savestate) and
+// SnapshotRegistry's state/SRAM/WRAM shadows, claimed in SnapshotRegistry::claim.
+// Sized once at construction and never resized, because a writer on the block
+// thread may not allocate.
 class MemorySnapshotTriple {
 public:
     explicit MemorySnapshotTriple(std::size_t regionSize)
````

- [ ] **Step 3: Take each core's overrides, and the config fields behind them**

Each Mesen core's override moves to its `private:` section, its `clearSram`, `fastBoot()` and `midiOut()` overrides
go, and its `finishBlock` comment now describes only the state publish. The GBA's `setFastBoot` becomes callerless
with the getter gone, and `SystemBase::setFastBoot`'s comment says so (the GBA node has no fast-boot setting; only
`config_.skipBootScreen`'s default is read at build). `SameBoyConfig` also loses its `<string>` include, since no
string member is left:

````diff
diff --git a/packages/native/src/system/mesen/MesenGbaConfig.hpp b/packages/native/src/system/mesen/MesenGbaConfig.hpp
index f2e91ea3..c64ea5be 100644
--- a/packages/native/src/system/mesen/MesenGbaConfig.hpp
+++ b/packages/native/src/system/mesen/MesenGbaConfig.hpp
@@ -24,12 +24,6 @@ struct MesenGbaConfig {
     // The file name Mesen stages the ROM under and logs it by. A node's build has only bytes and leaves this empty (the
     // core stages "rom.gba"); a direct construction in a test may name a file.
     std::string   romPath;
-    // See SameBoyConfig::savSuffix. 0 => owns `<rom>.sav`; N>=2 => `<rom>-N.sav`,
-    // so duplicated / repeat-loaded instances don't clobber a shared sibling.
-    std::uint32_t savSuffix = 0;
-    // See SameBoyConfig::savPath. Empty => suffix-derived sibling; non-empty =>
-    // a user-paired `.sav` file that all battery I/O targets.
-    std::string   savPath;
     // The battery and savestate seeds a node's resources carry, as a build takes them: the emulator owns both once it
     // is booted, and a savestate's embedded SRAM wins when both are set.
     std::vector<std::uint8_t> sram;
diff --git a/packages/native/src/system/mesen/MesenGbaSystem.cpp b/packages/native/src/system/mesen/MesenGbaSystem.cpp
index ea05fa3b..7316bcd2 100644
--- a/packages/native/src/system/mesen/MesenGbaSystem.cpp
+++ b/packages/native/src/system/mesen/MesenGbaSystem.cpp
@@ -294,9 +294,9 @@ void MesenGbaSystem::finishBlock(const AudioBlockInfo& info, float* const* outs,
     const std::uint32_t blockSize = info.frames;
     sumStereoWithGain(*audioDevice_, stereoAccum_, outs, blockSize, gainSmoother_);
 
-    // Tear-free memory snapshots for any UI subscriptions. End-of-block =
-    // internally consistent state because the CPU isn't mid-instruction.
-    publishMemorySnapshots();
+    // Publish the whole-savestate snapshot the registry reads, on its coarse interval (the very next
+    // block after enableStateSnapshot or rearmStateSnapshot, which push the accumulator past the
+    // threshold). End-of-block = internally consistent state because the CPU isn't mid-instruction.
     publishStateSnapshot(info.frames, sampleRate_);
 }
 
@@ -436,14 +436,6 @@ std::vector<std::uint8_t> MesenGbaSystem::saveSramBytes() const {
                                      accessor.data() + accessor.size());
 }
 
-void MesenGbaSystem::clearSram() {
-    if (!emu_) return;
-    auto accessor = getMemory(rp::MemoryType::Sram, rp::AccessType::ReadWrite);
-    if (!accessor.valid() || accessor.size() == 0) return;
-    std::memset(accessor.data(), 0, accessor.size());
-    config_.sram.clear();
-}
-
 std::vector<std::uint8_t> MesenGbaSystem::saveStateBytes() const {
     if (!emu_) return {};
     std::stringstream ss(std::ios::out | std::ios::binary);
diff --git a/packages/native/src/system/mesen/MesenGbaSystem.hpp b/packages/native/src/system/mesen/MesenGbaSystem.hpp
index 6194efd2..f9cc17ba 100644
--- a/packages/native/src/system/mesen/MesenGbaSystem.hpp
+++ b/packages/native/src/system/mesen/MesenGbaSystem.hpp
@@ -77,16 +77,9 @@ public:
     std::uint64_t stepInstruction() override;
 
     // SystemBase virtuals — see base class for contracts.
-    std::uint32_t             savSuffix() const override      { return config_.savSuffix; }
-    void                      setSavSuffix(std::uint32_t s) override { config_.savSuffix = s; }
-    const std::string&        savPath() const override        { return config_.savPath; }
-    void                      setSavPath(const std::string& p) override { config_.savPath = p; }
-    std::optional<bool>       fastBoot() const override       { return config_.skipBootScreen; }
     void                      setFastBoot(bool on) override;
     std::vector<std::uint8_t> saveSramBytes() const override;
-    void                      clearSram() override;
     std::vector<std::uint8_t> saveStateBytes() const override;
-    bool                      loadStateBytes(const std::vector<std::uint8_t>& bytes) override;
     std::size_t               stateSnapshotSize() const override;
     bool                      captureStateSnapshot(std::vector<std::uint8_t>& dst) override;
 
@@ -98,6 +91,9 @@ public:
     static constexpr std::uint32_t kPixelHeight = 160;
 
 private:
+    // Restore a savestate seed over the battery seedSramAndState() already put into the core.
+    bool loadStateBytes(const std::vector<std::uint8_t>& bytes) override;
+
     MesenGbaConfig                   config_;
     std::vector<std::uint8_t>         rom_;
     std::unique_ptr<Emulator>         emu_;
diff --git a/packages/native/src/system/mesen/MesenNesConfig.hpp b/packages/native/src/system/mesen/MesenNesConfig.hpp
index 254297a1..7c5d3d1a 100644
--- a/packages/native/src/system/mesen/MesenNesConfig.hpp
+++ b/packages/native/src/system/mesen/MesenNesConfig.hpp
@@ -80,12 +80,6 @@ struct MesenNesConfig {
     // The file name Mesen stages the ROM under and logs it by. A node's build has only bytes and leaves this empty (the
     // core stages "rom.nes"); a direct construction in a test may name a file.
     std::string   romPath;
-    // See SameBoyConfig::savSuffix. 0 => owns `<rom>.sav`; N>=2 => `<rom>-N.sav`,
-    // so duplicated / repeat-loaded instances don't clobber a shared sibling.
-    std::uint32_t savSuffix = 0;
-    // See SameBoyConfig::savPath. Empty => suffix-derived sibling; non-empty =>
-    // a user-paired `.sav` file that all battery I/O targets.
-    std::string   savPath;
     // The battery and savestate seeds a node's resources carry, as a build takes them: the emulator owns both once it
     // is booted, and a savestate's embedded SRAM wins when both are set.
     std::vector<std::uint8_t> sram;
diff --git a/packages/native/src/system/mesen/MesenNesSystem.cpp b/packages/native/src/system/mesen/MesenNesSystem.cpp
index af31befa..10526497 100644
--- a/packages/native/src/system/mesen/MesenNesSystem.cpp
+++ b/packages/native/src/system/mesen/MesenNesSystem.cpp
@@ -565,9 +565,9 @@ void MesenNesSystem::finishBlock(const AudioBlockInfo& info, float* const* outs,
         sumStereoWithGain(*audioDevice_, stereoAccum_, outs, blockSize, gainSmoother_);
     }
 
-    // Tear-free memory snapshots for any UI subscriptions. End-of-block =
-    // internally consistent state because the CPU isn't mid-instruction.
-    publishMemorySnapshots();
+    // Publish the whole-savestate snapshot the registry reads, on its coarse interval (the very next
+    // block after enableStateSnapshot or rearmStateSnapshot, which push the accumulator past the
+    // threshold). End-of-block = internally consistent state because the CPU isn't mid-instruction.
     publishStateSnapshot(info.frames, sampleRate_);
 }
 
@@ -723,14 +723,6 @@ std::vector<std::uint8_t> MesenNesSystem::saveSramBytes() const {
                                      accessor.data() + accessor.size());
 }
 
-void MesenNesSystem::clearSram() {
-    if (!emu_) return;
-    auto accessor = getMemory(rp::MemoryType::Sram, rp::AccessType::ReadWrite);
-    if (!accessor.valid() || accessor.size() == 0) return;
-    std::memset(accessor.data(), 0, accessor.size());
-    config_.sram.clear();
-}
-
 std::vector<std::uint8_t> MesenNesSystem::saveStateBytes() const {
     if (!emu_) return {};
     std::stringstream ss(std::ios::out | std::ios::binary);
diff --git a/packages/native/src/system/mesen/MesenNesSystem.hpp b/packages/native/src/system/mesen/MesenNesSystem.hpp
index 715fc586..0e24f99e 100644
--- a/packages/native/src/system/mesen/MesenNesSystem.hpp
+++ b/packages/native/src/system/mesen/MesenNesSystem.hpp
@@ -94,14 +94,8 @@ public:
     rp::IDebugTarget* debugTarget() override;
 
     // SystemBase virtuals — see base class for contracts.
-    std::uint32_t             savSuffix() const override      { return config_.savSuffix; }
-    void                      setSavSuffix(std::uint32_t s) override { config_.savSuffix = s; }
-    const std::string&        savPath() const override        { return config_.savPath; }
-    void                      setSavPath(const std::string& p) override { config_.savPath = p; }
     std::vector<std::uint8_t> saveSramBytes() const override;
-    void                      clearSram() override;
     std::vector<std::uint8_t> saveStateBytes() const override;
-    bool                      loadStateBytes(const std::vector<std::uint8_t>& bytes) override;
     std::size_t               stateSnapshotSize() const override;
     bool                      captureStateSnapshot(std::vector<std::uint8_t>& dst) override;
 
@@ -143,6 +137,9 @@ public:
     static constexpr std::uint32_t kPixelHeight = 240;
 
 private:
+    // Restore a savestate seed over the battery seedSramAndState() already put into the core.
+    bool loadStateBytes(const std::vector<std::uint8_t>& bytes) override;
+
     MesenNesConfig                       config_;
     std::vector<std::uint8_t>         rom_;
     std::unique_ptr<Emulator>         emu_;
diff --git a/packages/native/src/system/mesen/MesenSmsConfig.hpp b/packages/native/src/system/mesen/MesenSmsConfig.hpp
index ab9bd96c..db38b847 100644
--- a/packages/native/src/system/mesen/MesenSmsConfig.hpp
+++ b/packages/native/src/system/mesen/MesenSmsConfig.hpp
@@ -62,12 +62,6 @@ struct MesenSmsConfig {
     // re-reads; `romPath`'s stem wins when there is one, so this only names a file that has no path. A core built
     // without one stages under `system-<id>`: the stem is the battery identity, so two unnamed systems never share one.
     std::string   romName;
-    // See SameBoyConfig::savSuffix. 0 => owns `<rom>.sav`; N>=2 => `<rom>-N.sav`,
-    // so duplicated / repeat-loaded instances don't clobber a shared sibling.
-    std::uint32_t savSuffix = 0;
-    // See SameBoyConfig::savPath. Empty => suffix-derived sibling; non-empty =>
-    // a user-paired `.sav` file that all battery I/O targets.
-    std::string   savPath;
     // The battery and savestate seeds a node's resources carry, as a build takes them: the emulator owns both once it
     // is booted, and a savestate's embedded SRAM wins when both are set.
     std::vector<std::uint8_t> sram;
diff --git a/packages/native/src/system/mesen/MesenSmsSystem.cpp b/packages/native/src/system/mesen/MesenSmsSystem.cpp
index 3d228760..f0841bcc 100644
--- a/packages/native/src/system/mesen/MesenSmsSystem.cpp
+++ b/packages/native/src/system/mesen/MesenSmsSystem.cpp
@@ -559,9 +559,9 @@ void MesenSmsSystem::finishBlock(const AudioBlockInfo& info, float* const* outs,
     syncRole_.rebase(blockSize);
     sumStereoWithGain(*audioDevice_, stereoAccum_, outs, blockSize, gainSmoother_);
 
-    // Tear-free memory snapshots for any UI subscriptions. End-of-block =
-    // internally consistent state because the CPU isn't mid-instruction.
-    publishMemorySnapshots();
+    // Publish the whole-savestate snapshot the registry reads, on its coarse interval (the very next
+    // block after enableStateSnapshot or rearmStateSnapshot, which push the accumulator past the
+    // threshold). End-of-block = internally consistent state because the CPU isn't mid-instruction.
     publishStateSnapshot(info.frames, sampleRate_);
 }
 
@@ -611,14 +611,6 @@ std::vector<std::uint8_t> MesenSmsSystem::saveSramBytes() const {
     return std::vector<std::uint8_t>(accessor.data(), accessor.data() + accessor.size());
 }
 
-void MesenSmsSystem::clearSram() {
-    if (!emu_) return;
-    auto accessor = getMemory(rp::MemoryType::Sram, rp::AccessType::ReadWrite);
-    if (!accessor.valid() || accessor.size() == 0) return;
-    std::memset(accessor.data(), 0, accessor.size());
-    config_.sram.clear();
-}
-
 std::vector<std::uint8_t> MesenSmsSystem::saveStateBytes() const {
     if (!emu_) return {};
     std::stringstream ss(std::ios::out | std::ios::binary);
diff --git a/packages/native/src/system/mesen/MesenSmsSystem.hpp b/packages/native/src/system/mesen/MesenSmsSystem.hpp
index b9d8833a..26a52be5 100644
--- a/packages/native/src/system/mesen/MesenSmsSystem.hpp
+++ b/packages/native/src/system/mesen/MesenSmsSystem.hpp
@@ -123,17 +123,9 @@ public:
     std::optional<std::uint8_t> readCpuByte(std::uint32_t addr) const override;
     bool                        writeCpuByte(std::uint32_t addr, std::uint8_t value) override;
 
-    // SystemBase virtuals - see base class for contracts. fastBoot() is
-    // deliberately NOT overridden: Mesen's SmsConfig has no boot-screen skip,
-    // so the base nullopt hides the Fast boot row.
-    std::uint32_t             savSuffix() const override      { return config_.savSuffix; }
-    void                      setSavSuffix(std::uint32_t s) override { config_.savSuffix = s; }
-    const std::string&        savPath() const override        { return config_.savPath; }
-    void                      setSavPath(const std::string& p) override { config_.savPath = p; }
+    // SystemBase virtuals - see base class for contracts.
     std::vector<std::uint8_t> saveSramBytes() const override;
-    void                      clearSram() override;
     std::vector<std::uint8_t> saveStateBytes() const override;
-    bool                      loadStateBytes(const std::vector<std::uint8_t>& bytes) override;
     std::size_t               stateSnapshotSize() const override;
     bool                      captureStateSnapshot(std::vector<std::uint8_t>& dst) override;
 
@@ -159,6 +151,9 @@ public:
     static constexpr std::uint32_t kGgPixelHeight  = 144;
 
 private:
+    // Restore a savestate seed over the battery seedSramAndState() already put into the core.
+    bool loadStateBytes(const std::vector<std::uint8_t>& bytes) override;
+
     SmsConsole* smsConsole() const;
 
     // Write rom_ to a real file whose name Mesen can read three separate
diff --git a/packages/native/src/system/sameboy/SameBoyConfig.hpp b/packages/native/src/system/sameboy/SameBoyConfig.hpp
index 4184c266..6df17d38 100644
--- a/packages/native/src/system/sameboy/SameBoyConfig.hpp
+++ b/packages/native/src/system/sameboy/SameBoyConfig.hpp
@@ -1,7 +1,6 @@
 #pragma once
 
 #include <cstdint>
-#include <string>
 #include <vector>
 
 #include "rfl/Literal.hpp"
@@ -77,18 +76,6 @@ struct SameBoyConfig {
     double                    lightTemperature = 0.0;  // -1 cool .. +1 warm, CGB only
     // Per-system trim, dB. Smoothed at audio rate inside SameBoySystem::mixInto.
     float                     gainDb   = 0.0f;
-    // Loose-battery disambiguator. 0 => this system owns the plain sibling
-    // `<rom>.sav`; N>=2 => `<rom>-N.sav`. Assigned when a ROM ends up loaded
-    // into more than one system (Duplicate Instance, or adding the same file
-    // twice) so the instances keep independent battery files instead of all
-    // auto-saving over `<rom>.sav`. Persisted so it survives reload.
-    std::uint32_t             savSuffix = 0;
-    // Explicit battery-file override. Empty => derive `<rom>.sav` / `<rom>-N.sav`
-    // from the ROM's path and savSuffix (the default). Non-empty => this exact file,
-    // set when the user pairs a hand-picked `.sav` with the ROM in the load browser;
-    // all battery reads/writes (auto-save, Save SRAM, project-load restore) then
-    // target it instead of the sibling.
-    std::string               savPath;
     // Cartridge battery RAM (.sav contents), a build seed now: a node and a direct construction both hand the core
     // bytes, and the emulator owns the battery once it is booted. Loaded into the emulator BEFORE `savestate`, so a
     // savestate's embedded SRAM still wins when both are set.
diff --git a/packages/native/src/system/sameboy/SameBoySystem.cpp b/packages/native/src/system/sameboy/SameBoySystem.cpp
index 3f0a6cd4..5a829f56 100644
--- a/packages/native/src/system/sameboy/SameBoySystem.cpp
+++ b/packages/native/src/system/sameboy/SameBoySystem.cpp
@@ -376,15 +376,6 @@ void SameBoySystem::restartEmulator() {
     rearmStateSnapshot();
 }
 
-void SameBoySystem::clearSram() {
-    if (!gb_) return;
-    const int sramSize = GB_save_battery_size(gb_);
-    if (sramSize <= 0) return;
-    std::vector<std::uint8_t> zeros(static_cast<std::size_t>(sramSize), 0);
-    GB_load_battery_from_buffer(gb_, zeros.data(), zeros.size());
-    config_.sram.clear();
-}
-
 std::vector<std::uint8_t> SameBoySystem::saveSramBytes() const {
     if (!gb_) return {};
     const int sramSize = GB_save_battery_size(const_cast<GB_gameboy_t*>(gb_));
@@ -407,11 +398,6 @@ std::vector<std::uint8_t> SameBoySystem::saveStateBytes() const {
     return out;
 }
 
-bool SameBoySystem::loadStateBytes(const std::vector<std::uint8_t>& bytes) {
-    if (!gb_ || bytes.empty()) return false;
-    return GB_load_state_from_buffer(gb_, bytes.data(), bytes.size()) == 0;
-}
-
 // -- Whole-savestate snapshot ------------------------------------------------
 
 std::size_t SameBoySystem::stateSnapshotSize() const {
@@ -785,9 +771,9 @@ void SameBoySystem::finishBlock(const AudioBlockInfo& info, float* const* outs,
     // Clear this block's MIDI so the next block starts empty.
     pendingMidi_.clear();
 
-    // Tear-free memory snapshots for any UI subscriptions. Done AFTER role
-    // processing so kit patches applied this block are visible immediately.
-    publishMemorySnapshots();
+    // Publish the whole-savestate snapshot the registry reads, on its coarse interval (the very next
+    // block after enableStateSnapshot or rearmStateSnapshot, which push the accumulator past the
+    // threshold). Done AFTER role processing so a kit patch applied this block is in the blob.
     publishStateSnapshot(frames, sampleRate_);
 }
 
diff --git a/packages/native/src/system/sameboy/SameBoySystem.hpp b/packages/native/src/system/sameboy/SameBoySystem.hpp
index 2c764b0d..ee7bd09c 100644
--- a/packages/native/src/system/sameboy/SameBoySystem.hpp
+++ b/packages/native/src/system/sameboy/SameBoySystem.hpp
@@ -134,16 +134,9 @@ public:
     void setLightTemperature(double temperature);
 
     // SystemBase virtuals — see base class for contracts.
-    std::uint32_t             savSuffix() const override         { return config_.savSuffix; }
-    void                      setSavSuffix(std::uint32_t s) override { config_.savSuffix = s; }
-    const std::string&        savPath() const override           { return config_.savPath; }
-    void                      setSavPath(const std::string& p) override { config_.savPath = p; }
-    std::optional<bool>       fastBoot() const override         { return config_.fastBoot; }
     void                      setFastBoot(bool on) override     { config_.fastBoot = on; }
-    void                      clearSram() override;
     std::vector<std::uint8_t> saveSramBytes() const override;
     std::vector<std::uint8_t> saveStateBytes() const override;
-    bool                      loadStateBytes(const std::vector<std::uint8_t>& bytes) override;
     std::size_t               stateSnapshotSize() const override;
     bool                      captureStateSnapshot(std::vector<std::uint8_t>& dst) override;
     StateRegionTable          stateSnapshotRegions() const override;
````

- [ ] **Step 4: Run the tests to see them pass, and parity unchanged**

Run: `cmake --build build -j$(nproc)`
Expected: a clean build of every target; the only warnings are the pre-existing `-Wstringop-overflow` in `pressButton` and `-Wformat-truncation` at `PluginDSP.cpp:165`.

Run: `pnpm test:plugin`
Expected: `# 10 plugin test binary(ies) passed (jobs=16)`: the engine-graph binary's `26542 assertions / 198 cases` (the +6 assertions and +1 case over the baseline are the new case), the graph binary's `2174 assertions / 198 cases`, the audio binary's `1233719 / 101`.

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=16)`, none skipped, nothing re-recorded.

Run: `pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=16)`, with `# 46 case(s) SKIPPED in 10 file(s)`, as CI runs it.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native`
Expected: `# 158 native test file(s) passed (jobs=16)`, with `2 case(s) SKIPPED in 2 file(s): dsp-bench(1), nes-bench(1)`.

Run: `pnpm typecheck && pnpm test`
Expected: `pnpm typecheck` exits 0 (the report prints no line for it), then `# 168 test file(s) passed (jobs=16)`.

Run: `pnpm test:node`
Expected: `tests 12 / pass 12 / fail 0`.

Run: `pnpm check:docs`
Expected: `check-doc-links: 69 file(s) clean` (the prototype's 68 plus this plan, which its tree did not hold).

- [ ] **Step 5: Commit**

````bash
git add -- packages/native/src/system/SystemBase.cpp packages/native/src/system/SystemBase.hpp \
  packages/native/src/system/mesen/MesenGbaConfig.hpp packages/native/src/system/mesen/MesenGbaSystem.cpp \
  packages/native/src/system/mesen/MesenGbaSystem.hpp packages/native/src/system/mesen/MesenNesConfig.hpp \
  packages/native/src/system/mesen/MesenNesSystem.cpp packages/native/src/system/mesen/MesenNesSystem.hpp \
  packages/native/src/system/mesen/MesenSmsConfig.hpp packages/native/src/system/mesen/MesenSmsSystem.cpp \
  packages/native/src/system/mesen/MesenSmsSystem.hpp packages/native/src/system/sameboy/SameBoyConfig.hpp \
  packages/native/src/system/sameboy/SameBoySystem.cpp packages/native/src/system/sameboy/SameBoySystem.hpp \
  packages/native/src/transport/MemorySnapshotTriple.hpp packages/native/test/engine-graph/SystemNode.test.cpp
git commit -F - <<'EOF'
refactor(system): SystemBase drops the members nothing calls

The battery-path accessors (savSuffix/setSavSuffix/savPath/setSavPath), the fastBoot() getter,
clearSram, the public loadStateBytes, midiOut()/midiOut_, and the live memory-snapshot mechanism
(enableMemorySnapshot, disableMemorySnapshot, memorySnapshot, publishMemorySnapshots, snapshots_,
SnapshotEntry) had no caller outside SystemBase: a grep over packages/native's src, plugin, sdl,
cli, node and test returns only their definitions and the cores' overrides. The sav path and
suffix are TS's (savPaths.ts), fast boot and the battery are node settings and seeds, and the
serial-out decoder replaced the core-side MIDI out.

Each core's override goes with them, along with the config fields that only backed the sav
accessors and the memory-snapshot publish at the end of every finishBlock. kind() and the
whole-savestate snapshot the registry reads stay.

A savestate is still a build seed: the three Mesen cores keep their restore as a private override
of SystemBase::loadStateBytes, whose only caller is seedSramAndState. SameBoy's seed path already
restored config_.savestate inline, so its override had no caller left.

No behaviour change: parity holds bit for bit, nothing re-recorded.
EOF
````

### Task 2: a Mesen core collects its audio in a fixed ring

Design section 4. `MesenAudioDevice` stops being a `std::vector<int16_t>` that `resize`s when its high-water mark rises
and `erase`s every drain. It becomes a fixed-capacity ring of interleaved stereo with a head and a count, so
`availableFrames()` keeps its exact meaning (frames written and not yet drained) and the cores' step loops and the N8
FIFO's scheduling gate on the same number. Capacity is the plan's maximum block frames plus one Mesen flush
(`kMaxPlayBufferFrames = 0x10000 / 2 = 32768` frames: `SoundMixer::PlayAudioBuffer` resamples into its `0x10000`-`int16`
stereo buffer and is given the whole of it as the output cap, and the pitch-adjust path is capped lower), which is about
160 KiB per Mesen core at the engine's 8192-frame floor and 384 KiB at the 65536 ceiling. A write past capacity drops
the newest frames, counts them and asserts in a debug build; the bound makes it unreachable. Each Mesen core sizes its
own per-block scratches (and the NES its tap rings, in `deps/mesen`) at activation, so a block sizes nothing: the
system node passes the plan's maximum to `core->reserveAudio` after it builds a core and again for a core it already
holds, and a plan that grows the host's block reactivates the graph while it is quiescent. The cases count the audio
path (a drain of a ring that already holds what the block drains), because
Mesen's video path still allocates per emulated frame and Task 3 is what removes that.

**Files:**
- Modify: `deps/mesen/Core/NES/NesSoundMixer.cpp`, `deps/mesen/Core/NES/NesSoundMixer.h`, `packages/native/src/host/graph/SystemNodes.cpp`, `packages/native/src/system/SystemBase.hpp`, `packages/native/src/system/mesen/MesenAudioDevice.hpp`, `packages/native/src/system/mesen/MesenBlockOps.hpp`, `packages/native/src/system/mesen/MesenGbaSystem.cpp`, `packages/native/src/system/mesen/MesenGbaSystem.hpp`, `packages/native/src/system/mesen/MesenNesSystem.cpp`, `packages/native/src/system/mesen/MesenNesSystem.hpp`, `packages/native/src/system/mesen/MesenSmsSystem.cpp`, `packages/native/src/system/mesen/MesenSmsSystem.hpp`, `packages/native/test/engine-graph/SystemNode.test.cpp`

- [ ] **Step 1: Write the failing cases**

`SystemNode.test.cpp` gains the ring's own case (`[engine-graph][mesen][audio-ring][alloc]`: fill, drain,
`availableFrames`, a wrap whose tail runs off the end and continues at the front with the whole drained sequence
checked frame by frame, a reserve that keeps the buffered frames and a smaller one that does not shrink, a write past
capacity that drops the newest and counts them in a release build only, and a warm ring that writes and drains 64 x
1024 frames with zero allocations and zero frees, pinning `kMaxPlayBufferFrames == 0x8000` and the default block size
against the engine's); the audio path at every ragged size (33, 1, 222, 1, 7, 4096, 2, 8192), the first block at each
size being the counted one, for NES mixed, NES with its pins wired, NES with the tap armed over the mix, SMS and GBA;
the live arm on an activated NES (which charges a whole snapshot interval uncounted and drains more than the armed
ring holds, so head and tail wrap mid-drain); and the re-activation through `GraphHost::reactivate`:

````diff
diff --git a/packages/native/test/engine-graph/SystemNode.test.cpp b/packages/native/test/engine-graph/SystemNode.test.cpp
index 49ba9599..46213d31 100644
--- a/packages/native/test/engine-graph/SystemNode.test.cpp
+++ b/packages/native/test/engine-graph/SystemNode.test.cpp
@@ -13,6 +13,10 @@
 // The GBA and LSDj cases use sibling fixture ROMs, so each leaves on a run without the resources tree (as CI's) rather
 // than failing there.
 //
+// The last cases hold the Mesen block path's allocation promise for the audio path (docs/modular-graph-phase-c4-design.md
+// section 4): a fixed audio ring, and scratches and tap buffers sized at activation, so a block's audio work allocates
+// nothing at any block size, and arming the tap sizes nothing on the thread that arms it.
+//
 // Run via `pnpm test:plugin`.
 
 #include <algorithm>
@@ -38,6 +42,7 @@
 #include "GraphDocs.hpp"
 #include "NodeRig.hpp"
 #include "graph/CanonicalJson.hpp"
+#include "system/mesen/MesenAudioDevice.hpp"
 #include "system/mesen/MesenGbaSystem.hpp"
 #include "system/mesen/MesenNesConfig.hpp"
 #include "system/mesen/MesenNesSystem.hpp"
@@ -193,11 +198,11 @@ std::function<void(std::uint64_t)> smsStart(SystemBase& a, SystemBase& b) {
 
 AudioBlockInfo info(std::uint32_t frames) { return AudioBlockInfo{frames, kRate, 120.0, 0.0, false}; }
 
-// `lanes` host channels, each as long as the largest block.
+// `lanes` host channels, each as long as the largest block (`frames`, the default being the engine's 8192).
 struct Lanes {
     std::vector<std::vector<float>> data;
     std::vector<float*>             ptrs;
-    explicit Lanes(std::size_t n) : data(n, std::vector<float>(8192, 0.0f)) {
+    explicit Lanes(std::size_t n, std::size_t frames = 8192) : data(n, std::vector<float>(frames, 0.0f)) {
         for (auto& d : data) ptrs.push_back(d.data());
     }
 };
@@ -2458,3 +2463,324 @@ TEST_CASE("a boot's cost per console, through the build a system node makes", "[
                     built[built.size() / 2] / 1000.0, planned[planned.size() / 2] / 1000.0, boot.rom.size() / 1024);
     }
 }
+// The block thread's allocation promise for the AUDIO PATH (docs/modular-graph-phase-c4-design.md section 4). A Mesen
+// core collects its audio in a fixed ring (MesenAudioDevice), and sizes its per-block scratches, the tap's rings and the
+// tap's resample scratch at activation, so the audio path allocates and frees nothing while a block runs, at every block
+// size a host splits to, and arming the tap only switches modes (a newly armed stream still creates its fixed-size
+// blip, which is Mesen's blip_new). Mesen's own
+// video path is a different matter: it allocates about a dozen times per EMULATED frame (its per-frame controller
+// snapshot and the frame copies, BaseControlManager::GetPortStates through VideoDecoder/RenderedFrame), which is Mesen's
+// and is recorded in the stage's report rather than bounded here. These cases therefore measure the audio path alone:
+// the ring and the scratch work one block does, at ragged sizes, with the ring already holding what the block drains (the
+// ring's fill is the block clock, so a full ring means nothing steps), and the tap's arm, which runs between two blocks.
+
+namespace {
+
+// Host block sizes this file's other cases never run a core at, as parity/blocks-tiny.ts records the FL shapes: 1-frame
+// blocks between odd sizes, a small block, and the plan's maximum, the widest swing a block's buffers have to survive.
+constexpr std::array<std::uint32_t, 8> kRaggedBlocks = {33, 1, 222, 1, 7, 4096, 2, 8192};
+
+// One block as the node runs it: zero the lanes the system sums into, prepare, and - when `fill` - run the step phase
+// between the prepare and the drain. That order is the node's own triad, so the per-block counters the SMS and the NES
+// reset in prepare are reset BEFORE the step and not after it. `frames` is a ragged size from kRaggedBlocks, or 512 for
+// the long tapped run, so the scratches and the tap's rings see every shape. kRaggedBlocks drains ~12.5 k frames: well
+// under SystemBase's 0.5 s snapshot interval (24 k frames at 48 kHz), so no savestate capture - which allocates, in
+// Mesen's serializer - can land in a counted window. A counted stretch longer than that has to account for the publish
+// (chargeSnapshotInterval below).
+//
+// The step stays OUTSIDE the counted window: it emulates, and Mesen's video path allocates inside any window that runs
+// an emulated frame. The counted window is the drain.
+rp::test::AllocationCount drainBlock(SystemBase& core, std::uint32_t frames, float* const* lanes, std::size_t laneCount,
+                                     bool count, bool fill) {
+    for (std::size_t l = 0; l < laneCount; ++l) std::fill_n(lanes[l], frames, 0.0f);
+    core.prepareForBlock(info(frames));
+    if (fill) {
+        while (core.stepIfBelowTarget(frames)) {
+        }
+    }
+    if (count) rp::test::beginCountingAllocations();
+    core.finishBlock(info(frames), lanes, laneCount);
+    return count ? rp::test::endCountingAllocations() : rp::test::AllocationCount{};
+}
+
+// Every ragged size on `core`, drained through `lanes`. The block drained at each size is the FIRST one at that size,
+// filled by real emulation just before it (a window that found an empty ring would measure the zero-fill instead of
+// the drain): a buffer that still grew lazily at a new high-water mark would allocate here. One 1-frame block is run
+// first, because a core's very first block publishes its state snapshot without being asked (SystemBase arms an
+// immediate publish at activation) and that capture allocates inside Mesen.
+void checkRaggedDrainsAreFree(SystemBase& core, const std::vector<float*>& lanes) {
+    drainBlock(core, 1, lanes.data(), lanes.size(), false, true);
+    for (const std::uint32_t frames : kRaggedBlocks) {
+        const auto counted = drainBlock(core, frames, lanes.data(), lanes.size(), true, true);
+        INFO(frames << " frames: " << counted.allocations << " allocations, " << counted.frees << " frees");
+        REQUIRE(counted.allocations == 0);
+        REQUIRE(counted.frees == 0);
+    }
+}
+
+// A core the Engine built publishes a whole savestate every 0.5 s of block frames (SystemBase's accumulator), and that
+// capture allocates, in Mesen's streaming serializer. Charge a whole interval uncounted, through the publish call
+// itself, so the capture happens HERE and not inside a counted window: every counted stretch below then stays under
+// the interval (24 k frames at this file's rate) and cannot reach it.
+void chargeSnapshotInterval(SystemBase& core) {
+    core.publishStateSnapshot(static_cast<std::uint32_t>(kRate * 0.5), kRate);
+}
+
+}  // namespace
+
+TEST_CASE("a Mesen audio ring keeps a block in fixed storage, draining, wrapping and growing without allocating",
+          "[engine-graph][mesen][audio-ring][alloc]") {
+    // One PlayBuffer can never carry more than Mesen's own output buffer holds, and a core built outside a plan
+    // defaults to the engine's plan size (a node hands its own in at activation).
+    STATIC_REQUIRE(MesenAudioDevice::kMaxPlayBufferFrames == 0x8000);
+    STATIC_REQUIRE(MesenAudioDevice::kDefaultBlockFrames == Engine::kDefaultMaxBlockFrames);
+
+    MesenAudioDevice ring;
+    CHECK(ring.capacityFrames() == MesenAudioDevice::kDefaultBlockFrames + MesenAudioDevice::kMaxPlayBufferFrames);
+    CHECK(ring.availableFrames() == 0);
+
+    // A frame's samples are (n, -n) mod 30000, so a drained frame says where it was written.
+    const auto put = [](std::uint32_t first, std::uint32_t count, std::vector<int16_t>& into) {
+        into.resize(std::size_t(count) * 2);
+        for (std::uint32_t i = 0; i < count; ++i) {
+            const auto v = static_cast<std::int16_t>(static_cast<std::int32_t>((first + i) % 30000) - 15000);
+            into[i * 2] = v;
+            into[i * 2 + 1] = static_cast<std::int16_t>(-v);
+        }
+    };
+    // What `count` drained frames from written index `first` must be, bit for bit (the ring normalises with the same
+    // 1/32768). The first frame that is not, or "" when they all are.
+    const auto expect = [](std::uint32_t first, std::uint32_t count, const float* got) -> std::string {
+        constexpr float kScale = 1.0f / 32768.0f;
+        for (std::uint32_t i = 0; i < count; ++i) {
+            const auto v = static_cast<std::int32_t>((first + i) % 30000) - 15000;
+            if (got[i * 2] != static_cast<float>(v) * kScale || got[i * 2 + 1] != static_cast<float>(-v) * kScale)
+                return "frame " + std::to_string(first + i) + ": " + std::to_string(got[i * 2]) + " / " +
+                       std::to_string(got[i * 2 + 1]);
+        }
+        return "";
+    };
+
+    std::vector<int16_t> in;
+    std::vector<float>   out;
+
+    SECTION("fill, drain, and the frame count the block clock reads") {
+        put(0, 5, in);
+        ring.PlayBuffer(in.data(), 5, 48000, true);
+        CHECK(ring.availableFrames() == 5);
+        out.resize(10);
+        CHECK(ring.drain(out.data(), 3) == 3);
+        CHECK(ring.availableFrames() == 2);
+        CHECK(expect(0, 3, out.data()) == "");
+        CHECK(ring.drain(out.data(), 8) == 2);
+        CHECK(ring.availableFrames() == 0);
+        CHECK(expect(3, 2, out.data()) == "");
+        CHECK(ring.drain(out.data(), 8) == 0);
+    }
+
+    SECTION("wrap: a tail that runs off the end continues at the front") {
+        const std::uint32_t capacity = ring.capacityFrames();
+        put(0, capacity - 100, in);
+        ring.PlayBuffer(in.data(), capacity - 100, 48000, true);
+        out.resize(std::size_t(capacity) * 2);
+        CHECK(ring.drain(out.data(), 500) == 500);
+        CHECK(expect(0, 500, out.data()) == "");
+        // The tail now sits 100 frames short of the end, so this write crosses it: 100 frames at the end, 500 at the
+        // front, over the space the drain just freed.
+        put(capacity - 100, 600, in);
+        ring.PlayBuffer(in.data(), 600, 48000, true);
+        CHECK(ring.availableFrames() == capacity);
+        CHECK(ring.drain(out.data(), capacity - 100) == capacity - 100);
+        CHECK(expect(500, capacity - 600, out.data()) == "");
+        CHECK(expect(capacity - 100, 100, out.data() + std::size_t(capacity - 600) * 2) == "");
+        CHECK(ring.availableFrames() == 100);
+    }
+
+    SECTION("a resize on re-activation keeps what the ring holds") {
+        put(0, 7, in);
+        ring.PlayBuffer(in.data(), 7, 48000, true);
+        ring.reserveAudio(65536);  // a host grew its block: the node re-activates and the ring learns the new maximum
+        CHECK(ring.capacityFrames() == 65536 + MesenAudioDevice::kMaxPlayBufferFrames);
+        CHECK(ring.availableFrames() == 7);
+        out.resize(14);
+        CHECK(ring.drain(out.data(), 7) == 7);
+        CHECK(expect(0, 7, out.data()) == "");
+        // Growth only, and a smaller request is not a shrink.
+        ring.reserveAudio(1);
+        CHECK(ring.capacityFrames() == 65536 + MesenAudioDevice::kMaxPlayBufferFrames);
+    }
+
+    SECTION("a write past capacity drops the newest and counts them") {
+        // The drop policy is unreachable in production, so the ring asserts when it happens (a debug build): it is only
+        // holdable where that assert is compiled out, which is every configured build here but a debug one.
+        if (!NDEBUG) SKIP("a debug build asserts on the overflow this provokes");
+        const std::uint32_t capacity = ring.capacityFrames();
+        put(0, capacity, in);
+        ring.PlayBuffer(in.data(), capacity, 48000, true);
+        put(capacity, 5, in);
+        ring.PlayBuffer(in.data(), 5, 48000, true);
+        CHECK(ring.overflowFrames() == 5);
+        CHECK(ring.availableFrames() == capacity);
+        out.resize(std::size_t(capacity) * 2);
+        CHECK(ring.drain(out.data(), capacity) == capacity);
+        // What was already queued survived in order; the five frames that did not fit are gone, not wrapped over it.
+        CHECK(expect(0, capacity, out.data()) == "");
+    }
+
+    SECTION("a warm ring allocates nothing to write or drain") {
+        ring.reserveAudio(4096);
+        std::array<int16_t, 1024 * 2>  chunk{};
+        std::array<float, 8192 * 2>    dest{};
+        for (int i = 0; i < int(chunk.size()); ++i) chunk[std::size_t(i)] = static_cast<std::int16_t>(i);
+        rp::test::beginCountingAllocations();
+        for (int b = 0; b < 64; ++b) {
+            ring.PlayBuffer(chunk.data(), 1024, 48000, true);
+            ring.drain(dest.data(), 1024);
+            (void)ring.availableFrames();
+        }
+        const auto counted = rp::test::endCountingAllocations();
+        INFO("ring: " << counted.allocations << " allocations, " << counted.frees << " frees");
+        CHECK(counted.allocations == 0);
+        CHECK(counted.frees == 0);
+    }
+}
+
+TEST_CASE("a Mesen core's audio path drains without allocating at every block size",
+          "[engine-graph][system-node][alloc]") {
+    // The audio path's own promise. Warm, then one block at each ragged size with the ring already holding what the
+    // block drains: the drain, the scratches and the tap's rings, with no emulated frame in the window. The step phase
+    // is where Mesen's frame plumbing allocates, and it is outside every window here.
+    SECTION("NES, mixed") {
+        auto core = nes(1, 0);
+        core->reserveAudio(8192);  // what a node's activate does with its plan's maximum
+        Lanes lanes(2);
+        checkRaggedDrainsAreFree(*core, {lanes.ptrs[0], lanes.ptrs[1]});
+        CHECK(core->audioRingOverflow() == 0);
+    }
+    SECTION("NES, tap armed, its pins wired") {
+        auto core = nes(1, 1);
+        core->reserveAudio(8192);
+        Lanes lanes(3);
+        // The node points both lanes of a mono stream at its one port buffer.
+        std::vector<float*> tap;
+        for (std::size_t k = 0; k < 3; ++k) {
+            tap.push_back(lanes.ptrs[k]);
+            tap.push_back(lanes.ptrs[k]);
+        }
+        checkRaggedDrainsAreFree(*core, tap);
+        CHECK(core->tapOverflow() == 0);
+    }
+    SECTION("NES, tap armed, driven through the mix") {
+        // A split render's boot settle: the tap captures while the block is fanned out as the mix, so finishBlock also
+        // drains and discards the tap's rings, a block at a time.
+        auto core = nes(1, 1);
+        core->reserveAudio(8192);
+        Lanes lanes(2);
+        checkRaggedDrainsAreFree(*core, {lanes.ptrs[0], lanes.ptrs[1]});
+        CHECK(core->tapOverflow() == 0);
+    }
+    SECTION("SMS") {
+        auto core = sms(1);
+        core->reserveAudio(8192);
+        Lanes lanes(2);
+        checkRaggedDrainsAreFree(*core, {lanes.ptrs[0], lanes.ptrs[1]});
+    }
+    SECTION("GBA") {
+        // A sibling fixture, so this leaves on a run without one (as CI's) rather than failing there.
+        const auto rom = resourceFixture("/roms/nanoloop287d.gba");
+        if (rom.empty()) return;
+        auto core = std::make_unique<MesenGbaSystem>(1, MesenGbaConfig{}, rom);
+        core->onActivate(kRate);
+        core->reserveAudio(8192);
+        Lanes lanes(2);
+        checkRaggedDrainsAreFree(*core, {lanes.ptrs[0], lanes.ptrs[1]});
+    }
+}
+
+TEST_CASE("a tap armed live on an activated NES sizes nothing on the block thread, and its audio path stays free",
+          "[engine-graph][system-node][mesen][tap][alloc]") {
+    // A plugin routing change arms the tap through the plan's SyncTap trailer (Engine::syncTap), which the block thread
+    // runs as it adopts the new plan - i.e. BETWEEN two blocks, on the audio thread. Nothing may be SIZED there: the
+    // ring, the per-block scratches, the tap's rings and its resample scratch were all sized at activation
+    // (reserveAudio). The counted window is the arm itself; the one allocation an arm still makes is a newly armed
+    // stream's blip, which is blip_new's malloc (fixed-size, not the block's), so this allocator does not see it. The
+    // blocks after it are held the way the case above holds a core's: their drain is counted, their step (and Mesen's
+    // frame plumbing inside it) is not.
+    Engine e(kRate);
+    rp::host::test::Roms roms;
+    auto systems = std::vector<rp::host::test::DocSystem>{{1, rp::host::test::kNesType}};
+    rp::host::test::withRoms(e, systems, roms);
+    const auto document = [&] { return rp::host::test::graphRequest(rp::host::test::stereoDocument(systems), {1}); };
+    REQUIRE(e.setGraph(document()).ok);
+    auto* const core = dynamic_cast<MesenNesSystem*>(e.findSystem(1));
+    REQUIRE(core != nullptr);
+    REQUIRE(core->channelExportMode() == 0);
+    CHECK(core->audioRingCapacity() == 8192 + MesenAudioDevice::kMaxPlayBufferFrames);
+
+    std::vector<std::vector<float>> lanes(2, std::vector<float>(8192, 0.0f));
+    float* outs[2] = {lanes[0].data(), lanes[1].data()};
+    for (int b = 0; b < 4; ++b) e.processBlock(4096, outs, 2);  // real blocks: the ring has sound in flight
+
+    // The audio thread owns the next plan's adoption, so the arm is queued and the block thread runs it.
+    e.setAudioThreadOwns(true);
+    REQUIRE(e.setGraph(rp::host::test::rearming(document(), 1, 1)).ok);
+    rp::test::beginCountingAllocations();
+    e.drainCommands();
+    const auto arm = rp::test::endCountingAllocations();
+    e.setAudioThreadOwns(false);
+    INFO("arm: " << arm.allocations << " allocations, " << arm.frees << " frees");
+    CHECK(arm.allocations == 0);
+    CHECK(arm.frees == 0);
+    REQUIRE(core->channelExportMode() == 1);
+    CHECK(core->channelLayout().size() == 3);
+
+    // The blocks after it: the pins the arm just armed, drained through the node's six lanes.
+    Lanes pins(3);
+    std::vector<float*> tap;
+    for (std::size_t k = 0; k < 3; ++k) {
+        tap.push_back(pins.ptrs[k]);
+        tap.push_back(pins.ptrs[k]);
+    }
+    chargeSnapshotInterval(*core);
+    checkRaggedDrainsAreFree(*core, tap);
+    CHECK(core->tapOverflow() == 0);
+
+    // A long armed run, its drain counted a block at a time: the tap's rings take on more than a ringful (so their head
+    // and tail wrap, and DrainChannel crosses the end mid-drain) and never drop or allocate.
+    chargeSnapshotInterval(*core);
+    for (int b = 0; b < 46; ++b) {
+        const auto counted = drainBlock(*core, 512, tap.data(), tap.size(), true, true);
+        INFO("block " << b << ": " << counted.allocations << " allocations, " << counted.frees << " frees");
+        REQUIRE(counted.allocations == 0);
+        REQUIRE(counted.frees == 0);
+    }
+    CHECK(core->tapOverflow() == 0);
+
+    // And through the mix, which discards what the tap captured, a block at a time.
+    Lanes mix(2);
+    chargeSnapshotInterval(*core);
+    checkRaggedDrainsAreFree(*core, {mix.ptrs[0], mix.ptrs[1]});
+    CHECK(core->tapOverflow() == 0);
+}
+
+TEST_CASE("a node re-activation grows a Mesen core's audio ring to the new block",
+          "[engine-graph][system-node][alloc]") {
+    // Engine::setMaxBlockFrames re-activates the graph while it is quiescent, which is where the core's ring learns the
+    // plan's new maximum: the node's activate returns early for a core it holds and reserves for the new block.
+    NodeRig rig(kRate);
+    REQUIRE(rig.set(R"({"nodes":[{"id":"nes","type":"rp.mesen.nes","settings":{)" + rig.boot("rp.mesen.nes", 1) +
+                    R"(}},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[2]}}],
+                        "wires":[{"from":"nes.mix","to":"out.out.1-2"}]})").ok);
+    auto* const core = dynamic_cast<MesenNesSystem*>(rig.builtCore(1));
+    REQUIRE(core != nullptr);
+    CHECK(core->audioRingCapacity() == 8192 + MesenAudioDevice::kMaxPlayBufferFrames);
+    Lanes small(2);
+    const rp_transport t{kRate, 120.0, 0.0, 0};
+    rig.host->processBlock(512, t, small.ptrs.data(), 2);  // sound in flight when the plan is re-activated
+    std::string why;
+    REQUIRE(rig.host->reactivate(kRate, 16384, &why));
+    CHECK(core->audioRingCapacity() == 16384 + MesenAudioDevice::kMaxPlayBufferFrames);
+    Lanes big(2, 16384);
+    rig.host->processBlock(16384, t, big.ptrs.data(), 2);
+    CHECK(core->audioRingOverflow() == 0);
+}
````

- [ ] **Step 2: Run the tests to see them fail**

Run: `cmake --build build --target retroplug-engine-graph-test -j$(nproc)`
Expected: FAIL: the build stops at the names this task adds (`reserveAudio`, the ring's `capacityFrames()` / `overflowFrames()`, `audioRingOverflow()`, `tapOverflow()`, `kMaxPlayBufferFrames` and `kDefaultBlockFrames`).

The prototype's measured reds, from the report, are what a tree with the cases in place and the fix missing prints. On
the parent tree an earlier, 16000-frame shape of the window reported 261 allocations for NES mixed, 279 with the pins
wired, 309 with the tap armed over the mix, 368 for the SMS and 346 for the GBA; of those, ours were `PlayBuffer`'s 4
`resize`s, the tap rings' growth, `finishBlock`'s scratch `assign`s and `channelLayout()`'s 16 vectors, and the rest
was Mesen's video path. With the scratch pre-sizing removed (built, not committed) the case fails on its first counted
size in each NES section:

`33 frames: 1 allocations, 1 frees` (mixed), `3 / 3` (pins), `1 / 1` (tap over the mix). With
`MesenAudioDevice::reserveAudio` made a no-op (built, not committed) the ring case fails on capacity 0 with nothing
kept, and it also showed that `drain` ran `% capacity_` while draining nothing, which the fix made an early return.
With the pre-fix `sizePerBlockBuffers` and its call at the arm restored, the live-arm case fails at the arm:

`arm: 6 allocations, 1 frees`, the six `operator new`s being `stereoAccum_`'s 16384 -> 81920 floats, `chanAccum_[0]`'s
0 -> 40960 and `chanAccum_[1..4]`'s 0 -> 8192 each. It passes at 0 and 0 after.

- [ ] **Step 3: The ring, and the block size a core is told about**

`MesenAudioDevice` is the fixed ring: a head and a count, `PlayBuffer` writing at the tail, `drain(dest, frames)`
reading from the head in the one or two ranges a wrap makes (with an early return when there is nothing to drain),
`availableFrames()` returning the count, `reserveAudio` growing only and carrying what is queued across in at most two
`memcpy`s, `capacityFrames()` / `overflowFrames()` for the tests. `SystemBase` gains `reserveAudio(maxBlockFrames)`
(records the largest block, growth only) and a protected `blockFrames(fallback)` so the three cores share the fallback
rather than copying it, and the system node hands the plan's `maxFrames` to the core in `activate`, both after a build
and for a core it already holds (`Engine::setMaxBlockFrames` re-activates the graph quiescently, so a grown host block
reaches the core with no block in flight):

````diff
diff --git a/packages/native/src/host/graph/SystemNodes.cpp b/packages/native/src/host/graph/SystemNodes.cpp
index 9100cfac..da05ef14 100644
--- a/packages/native/src/host/graph/SystemNodes.cpp
+++ b/packages/native/src/host/graph/SystemNodes.cpp
@@ -408,10 +408,16 @@ struct SystemNode {
 
     // The first activation builds the core the settings name, and gives it every knob the settings carry; later ones
     // (a re-activation) keep it. A core is built at the rate the plan runs at, which Engine::setSampleRate re-rates. A
-    // plan that is then refused destroys it as it unwinds.
-    bool activate(double, std::uint32_t) {
-        if (core) return true;
+    // plan that is then refused destroys it as it unwinds. Either way the core learns the largest block it will run,
+    // which is how its audio ring and per-block scratches are sized before its first block: a plan that grows a host's
+    // block reactivates the whole graph quiescent, so the sizes move with no block in flight.
+    bool activate(double, std::uint32_t maxFrames) {
+        if (core) {
+            core->reserveAudio(maxFrames);
+            return true;
+        }
         build(static_cast<Self*>(this)->config(*settings));
+        core->reserveAudio(maxFrames);
         return true;
     }
 
diff --git a/packages/native/src/system/SystemBase.hpp b/packages/native/src/system/SystemBase.hpp
index 689f7da2..bfd706e9 100644
--- a/packages/native/src/system/SystemBase.hpp
+++ b/packages/native/src/system/SystemBase.hpp
@@ -79,6 +79,16 @@ public:
     virtual void finishBlock(const AudioBlockInfo& /*info*/, float* const* /*outs*/,
                              std::size_t /*laneCount*/) {}
 
+    // The largest host block this core will be asked to run (`maxBlockFrames` frames), so a backend can size the
+    // per-block buffers it must not allocate in while a block runs. A system node gives it at activation - as it
+    // builds the core, and again when a host grows its block, which reactivates the graph while it is quiescent - and
+    // a core built directly (a test, a tool) keeps its backend's own default. Growth only: a plan's maximum never
+    // shrinks. The reservation is recorded here; a backend that sizes per-block buffers overrides this, calls it
+    // first, and sizes from blockFrames().
+    virtual void reserveAudio(std::uint32_t maxBlockFrames) {
+        if (maxBlockFrames > maxBlockFrames_) maxBlockFrames_ = maxBlockFrames;
+    }
+
     // The output streams this system can emit. Default = one stereo "Mix" stream
     // (the mixed console output). A backend that can split its audio reports one
     // entry per stream; that layout only takes effect when the plan wires the
@@ -296,6 +306,11 @@ public:
     }
 
 protected:
+    // The block size a node has reserved for, in frames, or `fallback` (the backend's own default) until one does.
+    std::uint32_t blockFrames(std::uint32_t fallback) const {
+        return maxBlockFrames_ != 0 ? maxBlockFrames_ : fallback;
+    }
+
     // DSP thread (or the control thread while quiescent): the backend has rebuilt its core in place —
     // a SameBoy model switch — so the published blob and region table now describe a core that is gone.
     // Arms an immediate republish, which refreshes both together within one block instead of up to
@@ -327,6 +342,8 @@ private:
     // stateSnapshotCapacity() at claim, so a resize here would silently desync the two. Backends size it
     // to a ceiling instead (see stateSnapshotSize). interval ~0.5s.
     static constexpr double                kStateSnapshotIntervalSec = 0.5;
+    // The largest block a node has reserved for (reserveAudio), in frames; 0 until one does.
+    std::uint32_t                          maxBlockFrames_ = 0;
     // Sanity bound on a single savestate. Comfortably above any GB/NES/GBA
     // state; rejects an absurd size rather than allocating wildly.
     static constexpr std::size_t           kMaxStateSnapshotBytes = 16 * 1024 * 1024;
diff --git a/packages/native/src/system/mesen/MesenAudioDevice.hpp b/packages/native/src/system/mesen/MesenAudioDevice.hpp
index 76e54f3f..88d7c0b0 100644
--- a/packages/native/src/system/mesen/MesenAudioDevice.hpp
+++ b/packages/native/src/system/mesen/MesenAudioDevice.hpp
@@ -3,22 +3,79 @@
 #include "Core/Shared/Interfaces/IAudioDevice.h"
 
 #include <algorithm>
+#include <cassert>
 #include <cstdint>
 #include <cstring>
 #include <vector>
 
-// IAudioDevice implementation that captures rendered samples into an
-// in-process ring buffer. Mesen's SoundMixer feeds us int16 stereo at the
-// rate we ask for; MesenNesSystem drains them as float32 once enough samples
-// for the current audio block have accumulated.
+// IAudioDevice implementation that captures rendered samples into a fixed-capacity ring of interleaved stereo int16.
+// Mesen's SoundMixer feeds us int16 stereo at the rate we ask for; a Mesen system drains them as float32 once enough
+// samples for the current audio block have accumulated, and the ring's FILL is that core's block clock
+// (availableFrames), so the step loops and the N8 FIFO's scheduling keep stepping on the same number.
 //
-// Carried over from the pre-port build unchanged; it needed no adaptation.
+// The ring never allocates on the block thread. Its capacity is one host block plus the most a single PlayBuffer can
+// carry: Mesen's mixer resamples into a fixed 0x10000-int16 buffer and hands the device at most 0x10000 / 2 frames
+// per call (SoundMixer::PlayAudioBuffer), on every console and at every sample rate, and a step loop that gates on
+// `availableFrames() >= framesNeeded` stops with at most one such flush over a block. A system node sizes the ring
+// from the plan's maximum block before its core runs its first block, and again when a host grows its block, which
+// reactivates the graph while it is quiescent (reserveAudio).
 class MesenAudioDevice final : public IAudioDevice {
 public:
+    // The most frames Mesen's SoundMixer hands over in one PlayBuffer. It is the mixer's own output buffer, not a
+    // property of a console or a sample rate: the resampler writes into _sampleBuffer (0x10000 int16, stereo) and
+    // never past it, and a pitch-adjusted flush goes through the smaller 0x8000 one. So this bound holds for every
+    // core RetroPlug builds.
+    static constexpr std::uint32_t kMaxPlayBufferFrames = 0x10000 / 2;
+
+    // The engine's default plan size (Engine::kDefaultMaxBlockFrames), so a core built outside a plan (a test, a tool)
+    // runs blocks the same size an engine starts at. A system node replaces it with the plan's maximum at activation.
+    static constexpr std::uint32_t kDefaultBlockFrames = 8192;
+
+    MesenAudioDevice() { reserveAudio(kDefaultBlockFrames); }
+
+    // Size the ring for blocks of up to `maxBlockFrames`, keeping the frames it already holds in order. Grows only:
+    // a plan's maximum only ever grows (Engine::setMaxBlockFrames), and keeping a larger ring costs nothing but the
+    // memory it already holds. Control thread or activation, quiescent: neither ever runs with a block in flight.
+    void reserveAudio(std::uint32_t maxBlockFrames) {
+        // A block anywhere near this large could not be planned, but the sum must not wrap to a smaller capacity that
+        // the growth check below would then swallow.
+        assert(maxBlockFrames <= UINT32_MAX - kMaxPlayBufferFrames);
+        const std::uint32_t want = maxBlockFrames + kMaxPlayBufferFrames;
+        if (want <= capacity_) return;
+        std::vector<int16_t> grown(static_cast<std::size_t>(want) * 2);
+        // The queued frames are one contiguous run in ring order, so at most two copies carry them across.
+        if (count_ > 0) {
+            const std::uint32_t first = std::min(count_, capacity_ - head_);
+            std::memcpy(grown.data(), buffer_.data() + static_cast<std::size_t>(head_) * 2,
+                        static_cast<std::size_t>(first) * 2 * sizeof(int16_t));
+            std::memcpy(grown.data() + static_cast<std::size_t>(first) * 2, buffer_.data(),
+                        static_cast<std::size_t>(count_ - first) * 2 * sizeof(int16_t));
+        }
+        buffer_.swap(grown);
+        capacity_ = want;
+        head_     = 0;
+    }
+
+    // Append `count` stereo frames at the tail. A write past capacity drops the NEWEST frames (the ones that do not
+    // fit) and counts them: the frames already queued are the ones the pending block's sample indices, and the N8
+    // FIFO's scheduled bytes, already refer to, and they are what `availableFrames()` has told the step loop it has.
+    // Overwriting them would move every sample after the lost ones earlier in the block. The capacity makes this
+    // unreachable; a debug build asserts, a release build counts and plays on.
     void PlayBuffer(int16_t* samples, uint32_t count, uint32_t /*sampleRate*/, bool /*isStereo*/) override {
-        const std::size_t base = buffer_.size();
-        buffer_.resize(base + static_cast<std::size_t>(count) * 2);
-        std::memcpy(buffer_.data() + base, samples, static_cast<std::size_t>(count) * 2 * sizeof(int16_t));
+        std::uint32_t take = count;
+        if (take > capacity_ - count_) {
+            take = capacity_ - count_;
+            overflowFrames_ += count - take;
+            assert(take == count && "the Mesen audio ring overflowed: it was not sized for this block");
+        }
+        if (take == 0) return;
+        const std::uint32_t tail  = (head_ + count_) % capacity_;
+        const std::uint32_t first = std::min(take, capacity_ - tail);
+        std::memcpy(buffer_.data() + static_cast<std::size_t>(tail) * 2, samples,
+                    static_cast<std::size_t>(first) * 2 * sizeof(int16_t));
+        std::memcpy(buffer_.data(), samples + static_cast<std::size_t>(first) * 2,
+                    static_cast<std::size_t>(take - first) * 2 * sizeof(int16_t));
+        count_ += take;
     }
 
     void Stop() override {}
@@ -28,23 +85,36 @@ public:
     void SetAudioDevice(string) override {}
     AudioStatistics GetStatistics() override { return {}; }
 
-    std::size_t availableFrames() const {
-        return buffer_.size() / 2;
-    }
+    // Frames written and not yet drained: the block clock, exactly what the vector's size / 2 meant.
+    std::size_t availableFrames() const { return count_; }
 
-    // Drain up to `frameCount` stereo frames into `dest` as normalised float32.
-    // Returns the number of frames actually written.
+    // Drain up to `frameCount` stereo frames from the head into `dest` as normalised float32. Returns the number of
+    // frames actually written.
     std::uint32_t drain(float* dest, std::uint32_t frameCount) {
-        const std::uint32_t have = static_cast<std::uint32_t>(buffer_.size() / 2);
-        const std::uint32_t take = std::min(have, frameCount);
+        const std::uint32_t take = std::min(frameCount, count_);
+        if (take == 0) return 0;
         constexpr float kScale = 1.0f / 32768.0f;
-        for (std::uint32_t i = 0; i < take * 2; ++i) {
-            dest[i] = buffer_[i] * kScale;
+        const std::uint32_t first = std::min(take, capacity_ - head_);
+        for (std::uint32_t i = 0; i < first * 2; ++i) {
+            dest[i] = buffer_[static_cast<std::size_t>(head_) * 2 + i] * kScale;
         }
-        buffer_.erase(buffer_.begin(), buffer_.begin() + take * 2);
+        for (std::uint32_t i = 0; i < (take - first) * 2; ++i) {
+            dest[static_cast<std::size_t>(first) * 2 + i] = buffer_[i] * kScale;
+        }
+        head_ = (head_ + take) % capacity_;
+        count_ -= take;
         return take;
     }
 
+    // The ring's capacity in frames (a test's window on the sizing: one block plus one Mesen flush), and how many
+    // frames PlayBuffer has had to drop (0 unless a core ran a block bigger than the ring it was given).
+    std::uint32_t capacityFrames() const { return capacity_; }
+    std::uint64_t overflowFrames() const { return overflowFrames_; }
+
 private:
-    std::vector<int16_t> buffer_;
+    std::vector<int16_t> buffer_;  // capacity_ stereo frames, interleaved
+    std::uint32_t        capacity_       = 0;
+    std::uint32_t        head_           = 0;  // frame index of the oldest queued frame
+    std::uint32_t        count_          = 0;  // frames written and not yet drained
+    std::uint64_t        overflowFrames_ = 0;
 };
````

- [ ] **Step 4: Each Mesen core sizes the ring and its scratches at activation**

Each of the three Mesen cores overrides `reserveAudio` to keep the block, grow the ring and size its per-block
scratches (`stereoAccum_` is two floats a frame, every `chanAccum_[k]` one), and `onActivate` sizes a freshly created
device from the default so a tool that reserves first gets a ring that matches its scratches. The NES also sets
`channelLayout()` from one `captureStreamCount()` instead of building a vector a block, and its two discards drain one
block at a time through the block scratch instead of a scratch covering a block plus a flush, so
`setChannelExportMode` sizes nothing. `sumStereoWithGain`'s comment now says its growth is not load-bearing for these
cores:

````diff
diff --git a/packages/native/src/system/mesen/MesenBlockOps.hpp b/packages/native/src/system/mesen/MesenBlockOps.hpp
index 5a034082..c58ff8e1 100644
--- a/packages/native/src/system/mesen/MesenBlockOps.hpp
+++ b/packages/native/src/system/mesen/MesenBlockOps.hpp
@@ -18,6 +18,10 @@
  *  and summed those into the output: a stale echo rather than the silence the caller expects. Zeroing the
  *  unfilled tail is the whole fix, and it only costs anything in the case that was broken.
  *
+ *  The growth below is not load-bearing for the Mesen cores: they size `scratch` at activation (reserveAudio), so a
+ *  Mesen block never takes it. It is kept for a backend that does not (SameBoy's, today), and for a directly built
+ *  core running a block bigger than the one it reserved.
+ *
  *  A short ring is a backstop-only path on SMS and NES: their step loops loop until the ring is full and
  *  give up only at smsInstructionBudget / nesInstructionBudget, both derived from the block with ~8x slack
  *  over the shortest instruction. GBA's loop is bounded by a video-frame count instead. */
diff --git a/packages/native/src/system/mesen/MesenGbaSystem.cpp b/packages/native/src/system/mesen/MesenGbaSystem.cpp
index 7316bcd2..201e555f 100644
--- a/packages/native/src/system/mesen/MesenGbaSystem.cpp
+++ b/packages/native/src/system/mesen/MesenGbaSystem.cpp
@@ -154,9 +154,27 @@ void MesenGbaSystem::onActivate(double sampleRate) {
     // GbaSaveRam region rather than going through Mesen's BatteryManager.
     seedSramAndState(config_.sram, config_.savestate);
 
+    // The block scratch exists before the first block: a block's audio path may not allocate
+    // (docs/modular-graph-phase-c4-design.md section 4). A node that builds at another block size grows it through
+    // reserveAudio before that block runs.
+    audioDevice_->reserveAudio(blockFrames(MesenAudioDevice::kDefaultBlockFrames));
+    sizeBlockScratch();
+
     activated_ = true;
 }
 
+void MesenGbaSystem::reserveAudio(std::uint32_t maxBlockFrames) {
+    SystemBase::reserveAudio(maxBlockFrames);
+    if (audioDevice_) audioDevice_->reserveAudio(blockFrames(MesenAudioDevice::kDefaultBlockFrames));
+    sizeBlockScratch();
+}
+
+// sumStereoWithGain writes one block of stereo floats through it, and a block is the most it ever needs.
+void MesenGbaSystem::sizeBlockScratch() {
+    const std::size_t needed = std::size_t(blockFrames(MesenAudioDevice::kDefaultBlockFrames)) * 2;
+    if (stereoAccum_.size() < needed) stereoAccum_.assign(needed, 0.0f);
+}
+
 void MesenGbaSystem::onDeactivate() {
     if (!activated_) return;
     // GBA registers no audio provider today, so this is a no-op for it — but "no-op because of a fact
diff --git a/packages/native/src/system/mesen/MesenGbaSystem.hpp b/packages/native/src/system/mesen/MesenGbaSystem.hpp
index f9cc17ba..52e3218d 100644
--- a/packages/native/src/system/mesen/MesenGbaSystem.hpp
+++ b/packages/native/src/system/mesen/MesenGbaSystem.hpp
@@ -39,6 +39,7 @@ public:
     void prepareForBlock(const AudioBlockInfo& info) override;
     bool stepIfBelowTarget(std::uint32_t framesNeeded) override;
     void finishBlock(const AudioBlockInfo& info, float* const* outs, std::size_t laneCount) override;
+    void reserveAudio(std::uint32_t maxBlockFrames) override;
 
     // Audio-thread: queue a GBA button transition. The byte is the
     // position-aligned name index (GbaButton wire byte); remapped to
@@ -93,6 +94,7 @@ public:
 private:
     // Restore a savestate seed over the battery seedSramAndState() already put into the core.
     bool loadStateBytes(const std::vector<std::uint8_t>& bytes) override;
+    void sizeBlockScratch();  // stereoAccum_: at activation, never on the block thread
 
     MesenGbaConfig                   config_;
     std::vector<std::uint8_t>         rom_;
@@ -103,7 +105,8 @@ private:
     bool                              activated_      = false;
     double                            sampleRate_     = 44100.0;
     ExpSmoother                       gainSmoother_;
-    std::vector<float>                stereoAccum_;   // sized lazily to 2*blockSize
+    // Sized at activation (reserveAudio), never on the block thread.
+    std::vector<float>                stereoAccum_;   // 2 * the largest block's frames
 
     // Pending GBA button transitions. The byte is the GbaButton wire value
     // (Right=0..Start=7, L=8, R=9); remapped to Mesen's native
diff --git a/packages/native/src/system/mesen/MesenNesSystem.cpp b/packages/native/src/system/mesen/MesenNesSystem.cpp
index 10526497..faf693b8 100644
--- a/packages/native/src/system/mesen/MesenNesSystem.cpp
+++ b/packages/native/src/system/mesen/MesenNesSystem.cpp
@@ -261,7 +261,8 @@ void MesenNesSystem::onActivate(double sampleRate) {
         // (native/test-only), mode 3 = the 5 individual core channels. (CLI-only; a normal Mix build never
         // touches this and stays byte-identical.)
         if (config_.channelExportMode >= 1) {
-            nesMixer_->SetChannelCapture(config_.channelExportMode, static_cast<std::uint32_t>(sampleRate));
+            nesMixer_->SetChannelCapture(config_.channelExportMode, static_cast<std::uint32_t>(sampleRate),
+                                         blockFrames(MesenAudioDevice::kDefaultBlockFrames));
             channelCapture_ = true;
         }
     }
@@ -272,6 +273,12 @@ void MesenNesSystem::onActivate(double sampleRate) {
     // into the live NesSaveRam region.
     seedSramAndState(config_.sram, config_.savestate);
 
+    // The per-block buffers exist before the first block: a block's audio path may not allocate
+    // (docs/modular-graph-phase-c4-design.md section 4). A node that builds at another block size grows them through
+    // reserveAudio before that block runs.
+    if (audioDevice_) audioDevice_->reserveAudio(blockFrames(MesenAudioDevice::kDefaultBlockFrames));
+    sizePerBlockBuffers();
+
     activated_ = true;
 }
 
@@ -311,7 +318,8 @@ void MesenNesSystem::onSampleRateChanged(double sampleRate) {
     }
     // Re-arm the tap at the new host rate (re-inits the per-stream resamplers).
     if (channelCapture_ && nesMixer_) {
-        nesMixer_->SetChannelCapture(config_.channelExportMode, static_cast<std::uint32_t>(sampleRate));
+        nesMixer_->SetChannelCapture(config_.channelExportMode, static_cast<std::uint32_t>(sampleRate),
+                                     blockFrames(MesenAudioDevice::kDefaultBlockFrames));
     }
 }
 
@@ -383,10 +391,13 @@ void MesenNesSystem::setChannelExportMode(std::uint32_t mode) {
     config_.channelExportMode = mode;
     // Before onActivate there is no mixer yet — the new mode is recorded and onActivate arms from it.
     if (!nesMixer_) return;
-    // Re-arms (or, at mode 0, frees) the per-stream blip buffers + resamplers. Any frames already
-    // captured under the OLD mode are dropped with them, which is what we want: the streams change
-    // shape, so a partial block of the previous layout would fan out to the wrong lanes.
-    nesMixer_->SetChannelCapture(mode, static_cast<std::uint32_t>(sampleRate_));
+    // Re-arms (or, at mode 0, frees) the per-stream blip buffers + resamplers. The tap's rings and its resample scratch
+    // were sized at activation, so nothing here grows a buffer: only a newly armed stream's blip is allocated (a fixed
+    // size, not the block's). It may therefore run as the plan's trailer on the block thread. Any frames already
+    // captured under the OLD mode are dropped, which is what we want: the streams change shape, so a partial block of
+    // the previous layout would fan out to the wrong lanes.
+    nesMixer_->SetChannelCapture(mode, static_cast<std::uint32_t>(sampleRate_),
+                                 blockFrames(MesenAudioDevice::kDefaultBlockFrames));
     channelCapture_ = mode >= 1;
 }
 
@@ -541,10 +552,15 @@ void MesenNesSystem::finishBlock(const AudioBlockInfo& info, float* const* outs,
                 outs[2 * k][i] += chanAccum_[k][i] * g;
             }
         }
-        // Flush the mix ring (discard) so it stays bounded — the loop gated on the capture streams, not it.
-        const std::uint32_t mixAvail = static_cast<std::uint32_t>(audioDevice_->availableFrames());
-        if (stereoAccum_.size() < std::size_t(mixAvail) * 2) stereoAccum_.assign(std::size_t(mixAvail) * 2, 0.0f);
-        audioDevice_->drain(stereoAccum_.data(), mixAvail);
+        // Flush the mix ring (discard) so it stays bounded: the loop gated on the capture streams, not it. The ring can
+        // hold a block plus one Mesen flush, so it drains a block at a time through the block scratch, which is one
+        // block.
+        std::uint32_t mixAvail = static_cast<std::uint32_t>(audioDevice_->availableFrames());
+        while (mixAvail > 0) {
+            const auto chunk = static_cast<std::uint32_t>(std::min<std::size_t>(mixAvail, stereoAccum_.size() / 2));
+            audioDevice_->drain(stereoAccum_.data(), chunk);
+            mixAvail -= chunk;
+        }
     } else {
         assert(laneCount == 2); // mixed stereo (default single stereo stream)
         (void)laneCount;
@@ -554,12 +570,16 @@ void MesenNesSystem::finishBlock(const AudioBlockInfo& info, float* const* outs,
         // they accumulate, and a later renderAudioPerChannel finds enough buffered frames to satisfy the
         // block WITHOUT stepping the CPU — the core stalls (input never reaches it, no fresh audio comes).
         if (channelCapture_ && nesMixer_) {
-            if (const std::uint32_t avail = nesMixer_->AvailableCaptureFrames(); avail > 0) {
-                auto& discard = chanAccum_[0]; // reuse a per-stream scratch; the mix path doesn't fan it out
-                if (discard.size() < avail) discard.assign(avail, 0.0f);
-                const std::size_t nStreams = channelLayout().size();
+            // Reuse a per-stream scratch; the mix path doesn't fan it out. The tap's rings can hold a block plus one
+            // Mesen flush (the step loop gated on them, not on the mix ring), so they drain a block at a time.
+            auto& discard = chanAccum_[0];
+            const std::size_t nStreams = captureStreamCount();
+            std::uint32_t left = nesMixer_->AvailableCaptureFrames();
+            while (left > 0) {
+                const auto chunk = static_cast<std::uint32_t>(std::min<std::size_t>(left, discard.size()));
                 for (std::size_t k = 0; k < nStreams; ++k)
-                    nesMixer_->DrainChannel(static_cast<std::uint32_t>(k), discard.data(), avail);
+                    nesMixer_->DrainChannel(static_cast<std::uint32_t>(k), discard.data(), chunk);
+                left -= chunk;
             }
         }
         sumStereoWithGain(*audioDevice_, stereoAccum_, outs, blockSize, gainSmoother_);
@@ -572,21 +592,54 @@ void MesenNesSystem::finishBlock(const AudioBlockInfo& info, float* const* outs,
 }
 
 std::vector<ChannelStream> MesenNesSystem::channelLayout() const {
-    if (channelCapture_) {
-        // Mode 3 (§5b): the 5 individual core channels as raw mono stems ("does not sum").
-        if (config_.channelExportMode == 3) {
-            return {{"Square1", false}, {"Square2", false}, {"Triangle", false}, {"Noise", false}, {"DMC", false}};
-        }
-        // The two 2A03 stereo-mod output pins + the lumped expansion term, as mono streams (spec/10 §5).
-        // Mode 2 (native/test-only) appends the mix-reference stream for the fidelity test.
-        if (config_.channelExportMode == 2) {
-            return {{"Pulse", false}, {"TND", false}, {"Expansion", false}, {"MixRef", false}};
-        }
-        return {{"Pulse", false}, {"TND", false}, {"Expansion", false}};
-    }
+    const std::size_t streams = captureStreamCount();
+    // Mode 3 (§5b): the 5 individual core channels as raw mono stems ("does not sum").
+    if (streams == 5)
+        return {{"Square1", false}, {"Square2", false}, {"Triangle", false}, {"Noise", false}, {"DMC", false}};
+    // The two 2A03 stereo-mod output pins + the lumped expansion term, as mono streams (spec/10 §5). Mode 2
+    // (native/test-only) appends the mix-reference stream for the fidelity test.
+    if (streams == 4) return {{"Pulse", false}, {"TND", false}, {"Expansion", false}, {"MixRef", false}};
+    if (streams == 3) return {{"Pulse", false}, {"TND", false}, {"Expansion", false}};
     return {{"Mix", true}};
 }
 
+// How many streams the armed mode captures, as channelLayout offers them. finishBlock reads it rather than
+// channelLayout(), whose vector would be an allocation on the block thread.
+std::size_t MesenNesSystem::captureStreamCount() const {
+    if (!channelCapture_) return 0;
+    return config_.channelExportMode == 3 ? 5 : config_.channelExportMode == 2 ? 4 : 3;
+}
+
+std::uint32_t MesenNesSystem::audioRingCapacity() const {
+    return audioDevice_ ? audioDevice_->capacityFrames() : 0;
+}
+
+std::uint64_t MesenNesSystem::audioRingOverflow() const {
+    return audioDevice_ ? audioDevice_->overflowFrames() : 0;
+}
+
+std::uint64_t MesenNesSystem::tapOverflow() const {
+    return nesMixer_ ? nesMixer_->CaptureOverflowFrames() : 0;
+}
+
+void MesenNesSystem::reserveAudio(std::uint32_t maxBlockFrames) {
+    SystemBase::reserveAudio(maxBlockFrames);
+    const std::uint32_t block = blockFrames(MesenAudioDevice::kDefaultBlockFrames);
+    if (audioDevice_) audioDevice_->reserveAudio(block);
+    if (nesMixer_) nesMixer_->SetCaptureCapacity(block);
+    sizePerBlockBuffers();
+}
+
+// The two scratch sets the block path writes through, each one block of the largest block this core will run: the mix
+// path's drain scratch, and the per-stream scratch a drain (or a discard) goes through. Sized at activation and on a
+// larger reserve, never in a block. Growth only, so a later smaller reserve keeps the memory.
+void MesenNesSystem::sizePerBlockBuffers() {
+    const std::size_t block = blockFrames(MesenAudioDevice::kDefaultBlockFrames);
+    if (stereoAccum_.size() < block * 2) stereoAccum_.assign(block * 2, 0.0f);
+    for (std::vector<float>& scratch : chanAccum_)
+        if (scratch.size() < block) scratch.assign(block, 0.0f);
+}
+
 std::size_t MesenNesSystem::stateSnapshotSize() const {
     // A ceiling, measured from an UNCOMPRESSED serialize — see mesenStateCeiling. The triple is
     // allocated once from this and never resized, so it must cover the largest capture this core will
diff --git a/packages/native/src/system/mesen/MesenNesSystem.hpp b/packages/native/src/system/mesen/MesenNesSystem.hpp
index 0e24f99e..2d4c2a6f 100644
--- a/packages/native/src/system/mesen/MesenNesSystem.hpp
+++ b/packages/native/src/system/mesen/MesenNesSystem.hpp
@@ -43,6 +43,7 @@ public:
     void prepareForBlock(const AudioBlockInfo& info) override;
     bool stepIfBelowTarget(std::uint32_t framesNeeded) override;
     void finishBlock(const AudioBlockInfo& info, float* const* outs, std::size_t laneCount) override;
+    void reserveAudio(std::uint32_t maxBlockFrames) override;
 
     // Default = one stereo "Mix" stream. Under channelExportMode == StereoModPins (CLI-only), reports
     // the three mono pin streams (Pulse | TND | Expansion) the NesSoundMixer tap captures (spec/10 §5).
@@ -118,9 +119,10 @@ public:
     // can reach the split modes without rebuilding the core the way setRegion does — a routing flip must
     // not reset a playing game. Same modes as MesenNesConfig::channelExportMode (0 = off/Mix, 1 = pins,
     // 3 = 5 individual mono); channelLayout() follows it. Value-guarded, and pre-onActivate it only records
-    // the mode (onActivate arms from config_). NOT cheap - SetChannelCapture allocates the per-stream blip
-    // buffers + resamplers - but it is a user menu action, not a per-block cost, and it runs on the same
-    // audio thread that already absorbs SameBoySystem::restartEmulator from a Game Boy node's model setting.
+    // the mode (onActivate arms from config_). Sizes nothing on the block thread: the tap's rings, its resample
+    // scratch and the per-block scratches are sized by reserveAudio at activation, so an arm only switches modes and
+    // resets the streams. A newly armed stream still creates its own blip (Mesen's blip_new, a fixed ~50 KB), which is
+    // what a routing change costs on whichever thread the plan's trailer runs on.
     void setChannelExportMode(std::uint32_t mode);
     /** The armed per-channel export mode (0 = off). Lets the Engine record what it found before arming,
      *  so it can restore a construct-time (CLI / render) value it did not set. */
@@ -130,6 +132,12 @@ public:
     std::uint32_t apuFlushCycleLength() const;
     // The config the setters above keep, read-only. Exposed for tests / introspection.
     const MesenNesConfig& config() const { return config_; }
+    // The audio ring's capacity in frames (a plan's maximum block plus one Mesen flush), and the frames it had to drop
+    // (0 unless a block ran bigger than the ring it was given, which activation prevents). The tap's own rings report
+    // their drops the same way. For tests / introspection.
+    std::uint32_t audioRingCapacity() const;
+    std::uint64_t audioRingOverflow() const;
+    std::uint64_t tapOverflow() const;
 
     // NES native resolution (256x240). Public so callers can construct a
     // FrameBufferTriple-sized read buffer without hard-coding the constant.
@@ -140,6 +148,11 @@ private:
     // Restore a savestate seed over the battery seedSramAndState() already put into the core.
     bool loadStateBytes(const std::vector<std::uint8_t>& bytes) override;
 
+    // The mix path's drain scratch and the tap's per-stream drains, sized at activation (SystemBase::blockFrames):
+    // a block's audio path must not allocate (docs/modular-graph-phase-c4-design.md section 4).
+    void          sizePerBlockBuffers();
+    std::size_t   captureStreamCount() const;  // streams the armed mode captures (0 = none)
+
     MesenNesConfig                       config_;
     std::vector<std::uint8_t>         rom_;
     std::unique_ptr<Emulator>         emu_;
@@ -151,12 +164,13 @@ private:
     bool                              activated_      = false;
     double                            sampleRate_     = 44100.0;
     ExpSmoother                       gainSmoother_;
-    std::vector<float>                stereoAccum_;   // sized lazily to 2*blockSize
+    std::vector<float>                stereoAccum_;   // 2 * one block of the largest block size
 
     // nesMixer_ is owned by emu_'s NesConsole and borrowed for the whole activated lifetime (nulled in
     // onDeactivate) — used by the live APU-latency knob (all NES systems) AND per-channel capture (spec/10
     // §5 pins / §5b individual mono). chanAccum_ holds the per-stream mono drain each block: 3 pins (mode 1),
-    // 4 with the mix-reference (mode 2), or 5 core channels (mode 3).
+    // 4 with the mix-reference (mode 2), or 5 core channels (mode 3). Each is one block of the largest block size,
+    // sized at activation whether or not the tap is armed, so a routing change that arms it costs no allocation.
     static constexpr std::size_t      kMaxChannelStreams = 5;
     NesSoundMixer*                    nesMixer_ = nullptr;
     bool                              channelCapture_ = false;
diff --git a/packages/native/src/system/mesen/MesenSmsSystem.cpp b/packages/native/src/system/mesen/MesenSmsSystem.cpp
index f0841bcc..1965b665 100644
--- a/packages/native/src/system/mesen/MesenSmsSystem.cpp
+++ b/packages/native/src/system/mesen/MesenSmsSystem.cpp
@@ -307,9 +307,27 @@ void MesenSmsSystem::onActivate(double sampleRate) {
     // SmsCartRam region rather than going through Mesen's BatteryManager.
     seedSramAndState(config_.sram, config_.savestate);
 
+    // The block scratch exists before the first block: a block's audio path may not allocate
+    // (docs/modular-graph-phase-c4-design.md section 4). A node that builds at another block size grows it through
+    // reserveAudio before that block runs.
+    audioDevice_->reserveAudio(blockFrames(MesenAudioDevice::kDefaultBlockFrames));
+    sizeBlockScratch();
+
     activated_ = true;
 }
 
+void MesenSmsSystem::reserveAudio(std::uint32_t maxBlockFrames) {
+    SystemBase::reserveAudio(maxBlockFrames);
+    if (audioDevice_) audioDevice_->reserveAudio(blockFrames(MesenAudioDevice::kDefaultBlockFrames));
+    sizeBlockScratch();
+}
+
+// sumStereoWithGain writes one block of stereo floats through it, and a block is the most it ever needs.
+void MesenSmsSystem::sizeBlockScratch() {
+    const std::size_t needed = std::size_t(blockFrames(MesenAudioDevice::kDefaultBlockFrames)) * 2;
+    if (stereoAccum_.size() < needed) stereoAccum_.assign(needed, 0.0f);
+}
+
 void MesenSmsSystem::onDeactivate() {
     if (!activated_) return;
 
diff --git a/packages/native/src/system/mesen/MesenSmsSystem.hpp b/packages/native/src/system/mesen/MesenSmsSystem.hpp
index 26a52be5..928916d2 100644
--- a/packages/native/src/system/mesen/MesenSmsSystem.hpp
+++ b/packages/native/src/system/mesen/MesenSmsSystem.hpp
@@ -44,6 +44,7 @@ public:
     void prepareForBlock(const AudioBlockInfo& info) override;
     bool stepIfBelowTarget(std::uint32_t framesNeeded) override;
     void finishBlock(const AudioBlockInfo& info, float* const* outs, std::size_t laneCount) override;
+    void reserveAudio(std::uint32_t maxBlockFrames) override;
 
     // Audio-thread: queue a button transition. The byte is the position-aligned
     // name index (SmsButton wire byte); remapped to Mesen's native
@@ -160,6 +161,7 @@ private:
     // meanings out of. Returns the path, or empty on failure. See the comment
     // on the definition.
     std::string stageRom();
+    void sizeBlockScratch();  // stereoAccum_: at activation, never on the block thread
 
     // The staging directory this system owns, as `<home>/staged/<stagingSlot_>`.
     // Unique per system INSTANCE, which the system id is NOT: ids are allocated
@@ -176,7 +178,8 @@ private:
     bool                              activated_      = false;
     double                            sampleRate_     = 44100.0;
     ExpSmoother                       gainSmoother_;
-    std::vector<float>                stereoAccum_;   // sized lazily to 2*blockSize
+    // Sized at activation (reserveAudio), never on the block thread.
+    std::vector<float>                stereoAccum_;   // 2 * the largest block's frames
 
     // The on-disk file handed to Mesen's LoadRom. NOT cosmetic: the VirtualFile
     // name is simultaneously the machine selector, the battery-file stem and
````

- [ ] **Step 5: The tap's rings, in `deps/mesen`'s tracked sources**

Each of the NES's five `_streamBuf[k]` becomes a fixed ring with a capacity, a head and a count, sized by
`SetCaptureCapacity(maxBlockFrames)` to `maxBlockFrames + MaxSamplesPerFrame` (12800, the most one `CaptureStreams`
call appends); `SetChannelCapture` takes the block and sizes before it arms, a reset or a re-arm clears the queues but
keeps their storage, and `CaptureOverflowFrames()` counts a drop the sizing makes unreachable (exposed by the core as
`tapOverflow()`). `CaptureStreams`' two `resize`s move into `SetCaptureCapacity`, so they never run in a block:

````diff
diff --git a/deps/mesen/Core/NES/NesSoundMixer.cpp b/deps/mesen/Core/NES/NesSoundMixer.cpp
index c3c0d77e..3259db9d 100644
--- a/deps/mesen/Core/NES/NesSoundMixer.cpp
+++ b/deps/mesen/Core/NES/NesSoundMixer.cpp
@@ -69,7 +69,8 @@ void NesSoundMixer::Reset()
 		_streamPrev[i] = 0;
 		_streamResampler[i].Reset();
 		_streamResampler[i].SetSampleRates(_sampleRate, _captureHostRate);
-		_streamBuf[i].clear();
+		_streamHead[i] = 0;
+		_streamCount[i] = 0;
 	}
 
 	_timestamps.clear();
@@ -370,25 +371,59 @@ void NesSoundMixer::CaptureStreams()
 	// time), and its own Hermite resampler (96k→host, fresh phase, dynamic-rate 1.0 headless) emits the
 	// same host count as the mix ring — so the pins stay sample-aligned with the mix and re-sum to it.
 	// Read the blip mono into the even lanes, mirror to the odd (the stereo HermiteResampler wants
-	// interleaved L/R), resample, then keep the left lane of the host output.
-	_streamScratch.resize((size_t)NesSoundMixer::MaxSamplesPerFrame * 2);
-	_streamOutScratch.resize((size_t)NesSoundMixer::MaxSamplesPerFrame * 2);
+	// interleaved L/R), resample, then keep the left lane of the host output. The scratch is sized by
+	// SetCaptureCapacity, which is off the block thread.
 	for(uint32_t k = 0; k < _captureStreams; k++) {
 		int n96 = blip_read_samples(_streamBlip[k], _streamScratch.data(), NesSoundMixer::MaxSamplesPerFrame, 1);
 		for(int i = 0; i < n96; i++) {
 			_streamScratch[i * 2 + 1] = _streamScratch[i * 2];
 		}
 		uint32_t nHost = _streamResampler[k].Resample<false>(_streamScratch.data(), (uint32_t)n96, _streamOutScratch.data(), NesSoundMixer::MaxSamplesPerFrame);
-		size_t base = _streamBuf[k].size();
-		_streamBuf[k].resize(base + nHost);
+		if(nHost > _streamCapacity - _streamCount[k]) {
+			// Unreachable with the capacity SetCaptureCapacity sizes: a block plus the most one flush appends. Drop the
+			// newest and count them rather than let the ring wrap onto frames the step loop has already been promised.
+			_streamOverflow += nHost - (_streamCapacity - _streamCount[k]);
+			nHost = _streamCapacity - _streamCount[k];
+		}
+		uint32_t tail = (_streamHead[k] + _streamCount[k]) % _streamCapacity;
 		for(uint32_t i = 0; i < nHost; i++) {
-			_streamBuf[k][base + i] = _streamOutScratch[i * 2];
+			_streamBuf[k][tail] = _streamOutScratch[i * 2];
+			tail = tail + 1 == _streamCapacity ? 0 : tail + 1;
+		}
+		_streamCount[k] += nHost;
+	}
+}
+
+void NesSoundMixer::SetCaptureCapacity(uint32_t maxBlockFrames)
+{
+	// One host block plus the most one CaptureStreams call can append (MaxSamplesPerFrame host frames): the step loops
+	// gate on AvailableCaptureFrames, so they stop with at most one flush over a block. Grow only, keeping the frames
+	// already queued: a plan's block size never shrinks, and what a capture ring holds is still aligned with the mix.
+	uint32_t want = maxBlockFrames + MaxSamplesPerFrame;
+	if(want <= _streamCapacity) {
+		return;
+	}
+	// The rings and the resample scratch are allocated HERE, off the block thread, so an armed block's capture and
+	// drain never allocate. (Each stream's blip stays SetChannelCapture's: it is blip_new's, fixed-size, and created
+	// when a stream arms.)
+	for(uint32_t k = 0; k < MaxCaptureStreams; k++) {
+		vector<int16_t> grown(want, 0);
+		uint32_t from = _streamHead[k];
+		for(uint32_t i = 0; i < _streamCount[k]; i++) {
+			grown[i] = _streamBuf[k][from];
+			from = from + 1 == _streamCapacity ? 0 : from + 1;
 		}
+		_streamBuf[k].swap(grown);
+		_streamHead[k] = 0;
 	}
+	_streamScratch.resize((size_t)MaxSamplesPerFrame * 2);
+	_streamOutScratch.resize((size_t)MaxSamplesPerFrame * 2);
+	_streamCapacity = want;
 }
 
-void NesSoundMixer::SetChannelCapture(uint32_t mode, uint32_t hostRate)
+void NesSoundMixer::SetChannelCapture(uint32_t mode, uint32_t hostRate, uint32_t maxBlockFrames)
 {
+	SetCaptureCapacity(maxBlockFrames);
 	// Stream count per mode: 1 pins → 3, 2 pins+ref → 4, 3 individual-mono → 5, else off.
 	const uint32_t want = mode == 1 ? 3u : mode == 2 ? 4u : mode == 3 ? 5u : 0u;
 	_captureMode = want > 0 ? mode : 0u;
@@ -398,7 +433,8 @@ void NesSoundMixer::SetChannelCapture(uint32_t mode, uint32_t hostRate)
 			blip_delete(_streamBlip[k]);
 			_streamBlip[k] = nullptr;
 		}
-		_streamBuf[k].clear();
+		_streamHead[k] = 0;
+		_streamCount[k] = 0;
 	}
 	_captureStreams = want;
 	if(want > 0) {
@@ -414,7 +450,8 @@ void NesSoundMixer::SetChannelCapture(uint32_t mode, uint32_t hostRate)
 			_streamResampler[k].Reset();
 			_streamResampler[k].SetSampleRates(_sampleRate, hostRate);
 			_streamPrev[k] = 0;
-			_streamBuf[k].clear();
+			_streamHead[k] = 0;
+			_streamCount[k] = 0;
 		}
 	}
 }
@@ -423,7 +460,7 @@ uint32_t NesSoundMixer::AvailableCaptureFrames() const
 {
 	// All captured streams advance in lockstep (identical rates + input counts), so stream 0's length is
 	// the available frame count for every stream.
-	return _captureStreams > 0 ? (uint32_t)_streamBuf[0].size() : 0;
+	return _captureStreams > 0 ? _streamCount[0] : 0;
 }
 
 uint32_t NesSoundMixer::DrainChannel(uint32_t stream, float* dest, uint32_t frameCount)
@@ -432,12 +469,13 @@ uint32_t NesSoundMixer::DrainChannel(uint32_t stream, float* dest, uint32_t fram
 		return 0;
 	}
 	vector<int16_t>& buf = _streamBuf[stream];
-	uint32_t take = std::min((uint32_t)buf.size(), frameCount);
+	uint32_t take = std::min(_streamCount[stream], frameCount);
 	constexpr float scale = 1.0f / 32768.0f;
 	for(uint32_t i = 0; i < take; i++) {
-		dest[i] = buf[i] * scale;
+		dest[i] = buf[_streamHead[stream]] * scale;
+		_streamHead[stream] = _streamHead[stream] + 1 == _streamCapacity ? 0 : _streamHead[stream] + 1;
 	}
-	buf.erase(buf.begin(), buf.begin() + take);
+	_streamCount[stream] -= take;
 	return take;
 }
 
diff --git a/deps/mesen/Core/NES/NesSoundMixer.h b/deps/mesen/Core/NES/NesSoundMixer.h
index b407cbaa..44fb16e1 100644
--- a/deps/mesen/Core/NES/NesSoundMixer.h
+++ b/deps/mesen/Core/NES/NesSoundMixer.h
@@ -83,7 +83,15 @@ private:
 	HermiteResampler _streamResampler[MaxCaptureStreams];
 	vector<int16_t> _streamScratch;               // interleaved 96k read + expand scratch
 	vector<int16_t> _streamOutScratch;            // interleaved host resample scratch
-	vector<int16_t> _streamBuf[MaxCaptureStreams];// host-rate mono ring (drained by MesenNesSystem)
+	// Host-rate mono rings (drained by MesenNesSystem). FIXED capacity, sized by SetCaptureCapacity from the largest
+	// host block the caller will run plus one flush, so capturing and draining the tap never allocate on the audio
+	// thread. _streamHead is the read position of the oldest queued frame and _streamCount the frames queued; an
+	// append past capacity drops the newest and counts them, which the sizing makes unreachable.
+	uint32_t _streamCapacity = 0;
+	uint32_t _streamHead[MaxCaptureStreams] = {};
+	uint32_t _streamCount[MaxCaptureStreams] = {};
+	uint64_t _streamOverflow = 0;
+	vector<int16_t> _streamBuf[MaxCaptureStreams];
 
 	__forceinline double GetChannelOutput(AudioChannel channel, bool forRightChannel);
 	__forceinline int16_t GetOutputVolume(bool forRightChannel);
@@ -118,12 +126,18 @@ public:
 
 	// RetroPlug per-channel capture (spec/10 §5/§5b). `mode` is the channelExportMode: 0 = off, 1 = pins,
 	// 2 = pins + reference (fidelity test), 3 = 5 individual core mono channels. `hostRate` is passed in
-	// (the caller's sample rate) so the mixer needn't reach into settings. AvailableCaptureFrames reports
-	// host frames ready (all captured streams stay equal length); DrainChannel pops `frameCount` mono float
-	// samples from stream k (int16→float ÷32768, erase-from-front), like MesenAudioDevice::drain.
-	void SetChannelCapture(uint32_t mode, uint32_t hostRate);
+	// (the caller's sample rate) so the mixer needn't reach into settings. `maxBlockFrames` is the largest host block
+	// the caller will drain; SetCaptureCapacity has already sized the rings and the resample scratch from it at
+	// activation, so arming only switches modes (a newly armed stream still creates its own blip).
+	// AvailableCaptureFrames reports host frames ready (all captured streams stay equal length); DrainChannel pops
+	// `frameCount` mono float samples from stream k (int16→float ÷32768, oldest first), like MesenAudioDevice::drain.
+	void SetCaptureCapacity(uint32_t maxBlockFrames);
+	void SetChannelCapture(uint32_t mode, uint32_t hostRate, uint32_t maxBlockFrames);
 	uint32_t AvailableCaptureFrames() const;
 	uint32_t DrainChannel(uint32_t stream, float* dest, uint32_t frameCount);
+	// How many frames a capture ring had to drop (always 0: the capacity is a block plus one flush). Exposed so a test
+	// can hold that the sizing is what makes it unreachable.
+	uint64_t CaptureOverflowFrames() const { return _streamOverflow; }
 
 	void Serialize(Serializer& s) override;
 };
````

- [ ] **Step 6: Run the tests to see them pass, and parity unchanged**

Run: `cmake --build build --target retroplug-engine-graph-test -j$(nproc) && build/bin/retroplug-engine-graph-test "[alloc]"`
Expected: `All tests passed` for the `[alloc]` selection (the GBA section returns before its assertions without the fixtures, so the run is the same one CI makes).

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources build/bin/retroplug-engine-graph-test "[alloc]"`
Expected: the same selection with the GBA section running; the prototype's whole-binary run printed `26797 assertions / 202 test cases` without fixtures (baseline 26536/197 to 26797/202 is +5 cases and +261 assertions, and Task 1's 26542/198 to here is +4 and +255; the report's +1 case and +136 assertions are this task's second round over its first, 26661/201) and `26826/202` with them, so report this selection's line.

Run: `pnpm test:plugin`
Expected: `# 10 plugin test binary(ies) passed (jobs=16)`: the engine-graph binary's `26797 assertions / 202 test cases` as the plugin run counts it, the graph binary's `2174/198`, the audio binary's `1233719/101`, and the other seven at their 33d39f39 counts.

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=16)`, 0 skipped, nothing re-recorded: the ring changes where samples wait, not which samples are made.

Run: `pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=16)`, with `# 46 case(s) SKIPPED in 10 file(s)` and the baseline's distribution.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native`
Expected: `# 158 native test file(s) passed`, with `# 2 case(s) SKIPPED in 2 file(s): dsp-bench(1), nes-bench(1)`.

Run: `pnpm typecheck && pnpm test`
Expected: `pnpm typecheck` exit 0, then `# 168 test file(s) passed`.

- [ ] **Step 7: The sanitizers, on this task's tag**

Run: `cmake --build build-tsan --target retroplug-engine-graph-test retroplug-graph-test -j$(nproc) && TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-engine-graph-test "[alloc]"`
Expected: every selected case passes, no ThreadSanitizer report; the report prints `271 assertions / 4 cases` with no report.

Run: `cmake --build build-asan --target retroplug-engine-graph-test retroplug-graph-test -j$(nproc) && ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-engine-graph-test "[alloc]"`
Expected: the same selection passes, no AddressSanitizer or LeakSanitizer report.

Run: `pnpm check:docs`
Expected: `check-doc-links: 69 file(s) clean` (the prototype's 68 plus this plan).

- [ ] **Step 8: Commit**

````bash
git add -- deps/mesen/Core/NES/NesSoundMixer.cpp deps/mesen/Core/NES/NesSoundMixer.h \
  packages/native/src/host/graph/SystemNodes.cpp packages/native/src/system/SystemBase.hpp \
  packages/native/src/system/mesen/MesenAudioDevice.hpp packages/native/src/system/mesen/MesenBlockOps.hpp \
  packages/native/src/system/mesen/MesenGbaSystem.cpp packages/native/src/system/mesen/MesenGbaSystem.hpp \
  packages/native/src/system/mesen/MesenNesSystem.cpp packages/native/src/system/mesen/MesenNesSystem.hpp \
  packages/native/src/system/mesen/MesenSmsSystem.cpp packages/native/src/system/mesen/MesenSmsSystem.hpp \
  packages/native/test/engine-graph/SystemNode.test.cpp
git commit -F - <<'EOF'
fix(mesen): a Mesen core collects its audio in a fixed ring, so its audio path allocates nothing

A Mesen core's audio device appended to a std::vector and drained it with erase, so the ring's high-water mark allocated on
the block thread and every block memmoved what was left; the NES per-channel tap had a second vector ring of its own, whose
resample scratch was sized on the first captured flush; and the cores' per-block scratches were sized with assign on the
block thread.

MesenAudioDevice is now a fixed ring of interleaved stereo int16 (head/count, no resize, no erase), whose capacity is the
plan's maximum block frames plus one Mesen PlayBuffer. That bound is the mixer's own output buffer, 0x10000 / 2 stereo
frames: SoundMixer::PlayAudioBuffer resamples into it and never hands the device more than it holds, on every console and
at every sample rate, so a step loop that gates on availableFrames() stops with at most one such flush over a block. A
write past capacity never allocates: it drops the NEWEST frames (the queued ones are what the pending block's sample
indices and the N8 FIFO's scheduled bytes already refer to), counts them, and asserts in a debug build.

SystemBase::reserveAudio(maxBlockFrames) records the largest block a core will run and lets a backend size for it: the
system node gives it in activate, both as it builds the core and when a host grows its block (Engine::setMaxBlockFrames
re-activates the graph while it is quiescent), and blockFrames() supplies a backend's own default to a core built outside a
plan. The Mesen cores size the ring and their per-block scratches from it at activation, and NesSoundMixer sizes its
capture rings and the resample scratch with them (SetCaptureCapacity), so a block's audio path allocates nothing: no
scratch grows and no ring resizes.

The NES's two discards (the leftover mix frames after a block gated on the tap, and the tap's own frames when a block is
driven as the mix) drain a block at a time through the block scratch that is one block, instead of needing a scratch that
covers a block plus a flush, and the mix-path discard reads captureStreamCount() instead of building a channelLayout()
vector per block.

Arming the tap - a plugin routing change, which the engine runs as the plan's trailer on the block thread - sizes nothing:
everything it could size was sized at activation. The one allocation an arm still makes is a newly armed stream's blip,
Mesen's fixed-size blip_new, and the arm leaves that blip exactly as one created at the arm was: the rates are set by the
arm, never at capacity time, because blip_clear seeds the blip's fractional offset from the current factor. Arming the tap
must not move the audio, and it does not: parity is unchanged.

The engine-graph test gains the ring's own case (fill, drain, wrap, availableFrames across a wrap, a reserve on
re-activation keeping what was buffered, the grow-only capacity, the overflow counter and its drop policy, and a warm ring
that writes and drains 64x1024 frames with no allocation or free), a case that a Mesen core's audio path drains without
allocating at every ragged block size up to the plan's maximum (NES mixed, NES with its pins wired, NES with the tap armed
but driven as the mix, SMS, GBA), a case that arms the tap live on an activated NES through the engine's tap and counts the
arm and the blocks after it, and a case that a node re-activation grows the ring to the new block.

That promise is the AUDIO PATH's, and the cases hold it there: Mesen's own video path allocates about a dozen times per
emulated frame (its per-frame controller snapshot and the frame copies), which no window that emulates a frame can be free
of. It is recorded in the stage's report and belongs to the next stage; here it is why every counted window drains a ring
that already holds what the block drains.
EOF
````

### Task 3: Mesen's frame and input plumbing reuses its buffers

Design section 9, the first of the two scopes the user added while C4 was built (design section 2). Task 2's zero
allocation case could not hold for a whole block because a Mesen console allocated 13 to 18 times per emulated frame
inside Mesen itself: the port snapshot it builds for every video frame, the controller states copied into it, the
`InputData` a frame carries through `VideoDecoder` and `VideoRenderer`, and the notification dispatch. All of it is
Mesen's, none of it is the audio path, and none of it survives a block window. The fix is buffer reuse, in-tree, each
spot marked `// RetroPlug:`: a manager-owned port snapshot and a state filled in place, a persistent `RenderedFrame` per
PPU with `Reset`/`SetInputData`, a per-thread and per-nesting-depth notification copy held behind pointers so growing
the pool cannot move a buffer a send iterates, a controller state sized to `ControlDeviceState::MaxBytes` (64) at the
first poll while staying EMPTY, and `blip_reset` in `blip_buf` so an arm re-initialises a stream's blip. A device that
has had input now holds a 64-byte state, so a Mesen savestate grows by about 62 bytes a device; old states still load.
Three allocations a block carried are ours and go here too: the N8 FIFO's command parser buffers, the arm's
`blip_new`s, and SameBoy's lazy scratch `assign`s plus its dead `pendingMidi_`. The suite's `CountingAllocator` grows
its opt-in C half, which is what sees a blip's or Mesen's C block.

**Files:** 42 files, by step below.
- Create: `packages/native/test/engine-graph/MesenShared.test.cpp`
- Modify: 41 files

- [ ] **Step 1: Write the cases (the shared-class ones are guards), and teach the allocator to see C blocks**

`MesenShared.test.cpp` (new) drives Mesen's shared classes directly: a cleared controller device's state stays empty
and `SetStateFromInput`/`OnAfterSetState` do not throw (the Epoch Barcode Battler), a polled Four Score's composite
state is one length byte per empty port and a hub's composite state with a pressed button round-trips through a second
hub; and a notification's listener copy survives a listener destroying another in a nested send, the manager and its
copy holding only `weak_ptr`s. `CountingAllocator` gains the C half (`malloc`/`calloc`/`realloc`/`free` forwarded to
glibc's own, opt-in per window, `countsCAllocations()` saying whether this binary counts them), and `tests.cmake`
registers the new source. The shared-class cases are guards, not reds: they are green on the parent tree (at e30c39f5
`ClearState` already empties the state and `SendNotification` already iterates a `vector<weak_ptr>` copy), and they
caught the intermediate `ClearState` bug while this task was built. `SystemNode.test.cpp` gains the whole-block case
(a warmed core's whole block allocates and frees nothing, emulation included: NES mixed, NES with its tap armed, NES
through the Engine, SMS, Game Boy mixed and the four stem pairs, GBA, each warming through the real triad with a
button press, then the ragged sizes 33, 1, 222, 1, 7, 4096, 2 and 8192 as whole blocks with the first at each size
counted, each window proved to have emulated), the `[video]` case (three 4096-frame blocks per console, the published
frame hashed by FNV-1a over width, height and XRGB8888 bytes), and the plan-size case (a node sizes its core to the
plan's 64 frames, and a direct build keeps the default). `NodeRig` gains what the windows need to stage and read:

Create `packages/native/test/engine-graph/MesenShared.test.cpp`:

````cpp
// The Mesen shared pieces this fork patches for the block thread, held on their own rather than through a console:
//
// - BaseControlDevice::ClearState leaves a state EMPTY (its buffer kept), which four devices read as their contract.
//   A controller hub appends each port's state to what a clear leaves, so a cleared state that was 64 zero bytes turned
//   every hub's composite state into a 64-byte prefix that SetRawState then read as port lengths, and the barcode
//   readers take "no state" to mean the tape is not playing, so a cleared-but-non-empty state made every input poll
//   arm a stream it had no data for (a length_error out of the Epoch Barcode Battler's InitBarcodeStream).
// - NotificationManager iterates a copy of its listener list, reused per thread and per nesting depth, so a
//   notification allocates nothing once its depth has been reached. These hold what that copy must keep true: a
//   listener destroyed while the notification runs is not called after it, and a listener that sends a notification of
//   its own does not disturb the one being iterated.

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "NES/Input/BarcodeBattlerReader.h"
#include "NES/Input/FourScore.h"
#include "Shared/NotificationManager.h"

namespace {

// Records each notification it is called for, under a name, so a run's exact sequence can be compared.
struct NamedListener : INotificationListener {
    std::string                        name;
    std::vector<std::string>*          seen = nullptr;
    std::function<void()>              on = {};

    void ProcessNotification(ConsoleNotificationType, void*) override {
        seen->push_back(name);
        if (on) on();
    }
};

std::shared_ptr<NamedListener> listener(std::string name, std::vector<std::string>* seen) {
    auto l = std::make_shared<NamedListener>();
    l->name = std::move(name);
    l->seen = seen;
    return l;
}

}  // namespace

TEST_CASE("a cleared controller device's state is still empty, and a hub's composite state round-trips",
          "[engine-graph][mesen][control-state]") {
    SECTION("the Epoch Barcode Battler") {
        // No emulator: ClearState, SetStateFromInput and OnAfterSetState only read the state (an armed stream would
        // read the clock, which is why this stays unarmed, as an empty state is meant to).
        BarcodeBattlerReader barcode(nullptr);
        barcode.ClearState();
        CHECK(barcode.GetRawState().State.empty());
        CHECK_NOTHROW(barcode.SetStateFromInput());
        // What an input poll does after it sets a device's state: with an empty state this must arm nothing. With 64
        // zero bytes it armed the stream, and InitBarcodeStream's fixed padding threw std::length_error from the poll.
        CHECK_NOTHROW(barcode.OnAfterSetState());
        CHECK(barcode.GetRawState().State.empty());
    }

    SECTION("a Four Score's polled state is one length byte per empty port, not a cleared prefix") {
        // Four NES controllers with no keys mapped, so a poll reads no key and needs no emulator.
        ControllerConfig ports[4] = {};
        for (ControllerConfig& port : ports) port.Type = ControllerType::NesController;
        auto hub = std::make_unique<FourScore>(nullptr, ControllerType::FourScore, 0, ports);

        hub->SetStateFromInput();  // what BaseControlManager::UpdateInputState does before OnAfterSetState
        // ControllerHub builds its composite state by appending [length][bytes] per port to what ClearState left: four
        // empty ports are four zero length bytes. A cleared state of 64 zero bytes would sit in front of them, and
        // SetRawState reads lengths from index 0, so every port would parse as empty and the round trip below would
        // lose a pressed button.
        CHECK(hub->GetRawState().State == std::vector<std::uint8_t>(4, 0));

        hub->ClearState();
        CHECK(hub->GetRawState().State.empty());
    }

    SECTION("a hub's composite state round-trips through another hub") {
        ControllerConfig ports[4] = {};
        for (ControllerConfig& port : ports) port.Type = ControllerType::NesController;
        auto from = std::make_unique<FourScore>(nullptr, ControllerType::FourScore, 0, ports);
        auto to = std::make_unique<FourScore>(nullptr, ControllerType::FourScore, 0, ports);

        // Port 0's first button down, then a hub refresh, which is what Serializer/rewind restore into a hub.
        from->GetController(0)->SetRawState(ControlDeviceState{std::vector<std::uint8_t>{0x01}});
        from->RefreshHubState();

        ControlDeviceState saved = from->GetRawState();
        REQUIRE(saved.State.size() == 5);  // port 0 is one byte, ports 1-3 are empty: [1, 1, 0, 0, 0]
        CHECK(saved.State[0] == 1);
        CHECK(saved.State[1] == 0x01);
        CHECK(saved.State[2] == 0);

        to->SetRawState(saved);
        ControlDeviceState back = to->GetRawState();
        CHECK(!(back != saved));
        CHECK(to->GetTextState() == from->GetTextState());
        CHECK(to->GetController(0)->IsPressed(0));  // the pressed button reached the child, not just the hub's copy
    }
}

TEST_CASE("a notification's copy of the listener list survives a nested send and an unregister",
          "[engine-graph][mesen][notification]") {
    NotificationManager manager;
    std::vector<std::string> seen;

    auto first = listener("first", &seen);
    auto second = listener("second", &seen);
    auto third = listener("third", &seen);

    // The outer run destroys `second` (the manager and the copy it iterates hold only weak_ptrs, so this handle is its
    // only owner) and sends a notification of its own. The nested run must see its own listener set with its own
    // buffer, and the outer run must keep iterating the copy it captured: `second` is dead by the time the outer loop
    // reaches it, so it is skipped rather than called.
    first->on = [&] {
        if (!second) return;  // only the outer run does this, not the nested one that follows
        second.reset();
        manager.SendNotification(ConsoleNotificationType::ConfigChanged);
    };

    manager.RegisterNotificationListener(first);
    manager.RegisterNotificationListener(second);
    manager.RegisterNotificationListener(third);

    manager.SendNotification(ConsoleNotificationType::PpuFrameDone);
    const std::vector<std::string> expected{"first", "first", "third", "third"};
    CHECK(seen == expected);  // first + third nested, then first + third in the outer run (second already dead)

    // The next run sees the same set, with `second` gone for good.
    seen.clear();
    manager.SendNotification(ConsoleNotificationType::PpuFrameDone);
    CHECK(seen == std::vector<std::string>{"first", "third"});
}
````

````diff
diff --git a/packages/native/cmake/tests.cmake b/packages/native/cmake/tests.cmake
index 67a9695e..2a4e1109 100644
--- a/packages/native/cmake/tests.cmake
+++ b/packages/native/cmake/tests.cmake
@@ -90,6 +90,7 @@ retroplug_add_test_binary(
     NAME     retroplug-engine-graph-test
     SOURCES  test/engine-graph/Deterministic.cpp
              test/engine-graph/SystemNode.test.cpp
+             test/engine-graph/MesenShared.test.cpp    # ClearState's empty state, and the notification dispatch's copy
              test/engine-graph/EngineGraph.test.cpp
              test/engine-graph/SlurpFile.test.cpp     # the file reader putResource reads a `path` resource with
              test/engine-graph/HostIo.test.cpp
diff --git a/packages/native/test/engine-graph/NodeRig.hpp b/packages/native/test/engine-graph/NodeRig.hpp
index 413e94e1..d677b804 100644
--- a/packages/native/test/engine-graph/NodeRig.hpp
+++ b/packages/native/test/engine-graph/NodeRig.hpp
@@ -67,7 +67,9 @@ struct NodeRig {
     // Last, so it is destroyed first: its nodes free their cores through the host's destroySystem into `released`.
     std::unique_ptr<rp::graph::GraphHost>                host;
 
-    explicit NodeRig(double sampleRate) {
+    // `maxFrames` is the plan's maximum block, as an Engine's is: a node builds its core for it, so a rig can hold what
+    // a small host block sizes a core to.
+    explicit NodeRig(double sampleRate, std::uint32_t maxFrames = 8192) {
         rate = sampleRate;
         registerCoreBackends(factory);
         for (const rp_package* p : {rp::graph::corePackage(), rp::graph::hostPackage(), sameboyPackage(), mesenPackage(),
@@ -75,7 +77,7 @@ struct NodeRig {
             registry.addPackage(p);
         rp::graph::HostConfig config;
         config.sampleRate = sampleRate;
-        config.maxFrames = 8192;
+        config.maxFrames = maxFrames;
         config.interfaces = [](void* ctx, const char* id) -> const void* {
             auto* rig = static_cast<NodeRig*>(ctx);
             const std::string want(id);
diff --git a/packages/native/test/engine-graph/SystemNode.test.cpp b/packages/native/test/engine-graph/SystemNode.test.cpp
index 46213d31..a26c8a83 100644
--- a/packages/native/test/engine-graph/SystemNode.test.cpp
+++ b/packages/native/test/engine-graph/SystemNode.test.cpp
@@ -13,9 +13,10 @@
 // The GBA and LSDj cases use sibling fixture ROMs, so each leaves on a run without the resources tree (as CI's) rather
 // than failing there.
 //
-// The last cases hold the Mesen block path's allocation promise for the audio path (docs/modular-graph-phase-c4-design.md
-// section 4): a fixed audio ring, and scratches and tap buffers sized at activation, so a block's audio work allocates
-// nothing at any block size, and arming the tap sizes nothing on the thread that arms it.
+// The last cases hold the block thread's allocation promise (docs/modular-graph-phase-c4-design.md section 4): a
+// warmed core's block allocates and frees nothing, both for the audio path on its own (a fixed audio ring, and
+// scratches, tap buffers and stream blips sized at activation) and for a whole block with emulation in it (Mesen's
+// per-frame video path and SameBoy's own), and arming the tap allocates nothing on the thread that arms it.
 //
 // Run via `pnpm test:plugin`.
 
@@ -2463,16 +2464,20 @@ TEST_CASE("a boot's cost per console, through the build a system node makes", "[
                     built[built.size() / 2] / 1000.0, planned[planned.size() / 2] / 1000.0, boot.rom.size() / 1024);
     }
 }
-// The block thread's allocation promise for the AUDIO PATH (docs/modular-graph-phase-c4-design.md section 4). A Mesen
-// core collects its audio in a fixed ring (MesenAudioDevice), and sizes its per-block scratches, the tap's rings and the
-// tap's resample scratch at activation, so the audio path allocates and frees nothing while a block runs, at every block
-// size a host splits to, and arming the tap only switches modes (a newly armed stream still creates its fixed-size
-// blip, which is Mesen's blip_new). Mesen's own
-// video path is a different matter: it allocates about a dozen times per EMULATED frame (its per-frame controller
-// snapshot and the frame copies, BaseControlManager::GetPortStates through VideoDecoder/RenderedFrame), which is Mesen's
-// and is recorded in the stage's report rather than bounded here. These cases therefore measure the audio path alone:
-// the ring and the scratch work one block does, at ragged sizes, with the ring already holding what the block drains (the
-// ring's fill is the block clock, so a full ring means nothing steps), and the tap's arm, which runs between two blocks.
+// The block thread's allocation promise (docs/modular-graph-phase-c4-design.md section 4): a warmed core's block
+// allocates and frees nothing, emulation included. Two kinds of case hold it.
+//
+// The AUDIO PATH's own case drains a ring a previous block filled, so no emulated frame runs inside its window: the
+// ring and the scratch work one block does, at ragged sizes, with the ring already holding what the block drains (the
+// ring's fill is the block clock, so a full ring means nothing steps). A Mesen core collects its audio in a fixed ring
+// (MesenAudioDevice), and sizes its per-block scratches, the tap's rings, its resample scratch and each stream's blip
+// at activation, so that path allocates nothing at any block size a host splits to, and arming the tap allocates
+// nothing either (it re-initialises a stream's blip in place; see blip_reset).
+//
+// The WHOLE-BLOCK case runs the node's whole triad inside the window - prepare, step, finish - so it also holds what a
+// real block carries: Mesen's per-frame video path (BaseControlManager::GetPortStates, the controller states and the
+// RenderedFrame input snapshot through VideoDecoder, the notification dispatch) and SameBoy's own per-frame work.
+// Both console paths are Mesen's frame plumbing and ours; neither allocates once the core is warm.
 
 namespace {
 
@@ -2488,8 +2493,8 @@ constexpr std::array<std::uint32_t, 8> kRaggedBlocks = {33, 1, 222, 1, 7, 4096,
 // Mesen's serializer - can land in a counted window. A counted stretch longer than that has to account for the publish
 // (chargeSnapshotInterval below).
 //
-// The step stays OUTSIDE the counted window: it emulates, and Mesen's video path allocates inside any window that runs
-// an emulated frame. The counted window is the drain.
+// The step stays OUTSIDE this window: the case holds the drain's own promise (the ring, the scratches, the tap), and
+// the whole-block case below runs the step inside its window too.
 rp::test::AllocationCount drainBlock(SystemBase& core, std::uint32_t frames, float* const* lanes, std::size_t laneCount,
                                      bool count, bool fill) {
     for (std::size_t l = 0; l < laneCount; ++l) std::fill_n(lanes[l], frames, 0.0f);
@@ -2498,7 +2503,7 @@ rp::test::AllocationCount drainBlock(SystemBase& core, std::uint32_t frames, flo
         while (core.stepIfBelowTarget(frames)) {
         }
     }
-    if (count) rp::test::beginCountingAllocations();
+    if (count) rp::test::beginCountingAllocations(true);
     core.finishBlock(info(frames), lanes, laneCount);
     return count ? rp::test::endCountingAllocations() : rp::test::AllocationCount{};
 }
@@ -2518,6 +2523,68 @@ void checkRaggedDrainsAreFree(SystemBase& core, const std::vector<float*>& lanes
     }
 }
 
+// The samples the step phase captured for the block in progress, before finishBlock drains them. Every backend stops
+// stepping at the block's frame count, so "at least `frames`" is what proves the block was filled by emulation: a core
+// whose step loop stopped, or a plan that refused the block, leaves the ring short, finishBlock blanks what is missing,
+// and the window would have been vacuously free.
+std::uint32_t capturedFrames(SystemBase& core) {
+    if (auto* nes = dynamic_cast<MesenNesSystem*>(&core)) return nes->availableFrames();
+    if (auto* sms = dynamic_cast<MesenSmsSystem*>(&core)) return sms->availableFrames();
+    if (auto* gba = dynamic_cast<MesenGbaSystem*>(&core)) return gba->availableFrames();
+    if (auto* gb = dynamic_cast<SameBoySystem*>(&core)) return gb->audioFrameCount_;
+    return 0;
+}
+
+// One whole block as the node runs it, with the step phase inside the counted window: prepare, step until the core has
+// filled the block, finish. This is the promise's real shape - a block with emulated frames, video frames, controller
+// polls and notifications in it - so it counts everything the block does, not just the drain, and the block it counts
+// is proven filled above.
+rp::test::AllocationCount wholeBlock(SystemBase& core, std::uint32_t frames, const std::vector<float*>& lanes,
+                                     bool count) {
+    for (float* const lane : lanes) std::fill_n(lane, frames, 0.0f);
+    if (count) rp::test::beginCountingAllocations(true);
+    core.prepareForBlock(info(frames));
+    while (core.stepIfBelowTarget(frames)) {
+    }
+    if (count) REQUIRE(capturedFrames(core) >= frames);
+    core.finishBlock(info(frames), lanes.data(), lanes.size());
+    return count ? rp::test::endCountingAllocations() : rp::test::AllocationCount{};
+}
+
+// A hash of a core's whole work RAM, to hold that a counted window really emulated: a plan that refused the block
+// (Engine::processBlock's early return), a node that stopped stepping, or a core that stepped without running the
+// console leaves the memory where it was. Read outside the counted window, on both sides of it.
+std::uint64_t ramHash(SystemBase& core) {
+    auto ram = core.getMemory(rp::MemoryType::Ram, rp::AccessType::Read);
+    REQUIRE(ram.valid());
+    std::uint64_t hash = 1469598103934665603ull;
+    for (std::size_t i = 0; i < ram.size(); ++i) {
+        hash ^= ram[i];
+        hash *= 1099511628211ull;
+    }
+    return hash;
+}
+
+// Warm `core` through real blocks, then run every ragged size as a counted WHOLE block, the first block at each size
+// being the counted one. The warm run is deliberately below the ragged set's maximum, so the 8192-frame window is still
+// the first block at that size: a buffer that grew lazily at a new high-water mark would allocate there. ~4 k frames of
+// warm-up is several emulated frames' worth of Mesen's frame path and of SameBoy's own per-frame work, so what remains
+// is steady state, and it is where a device's state is sized (the first poll reserves it; see ClearState) and where a
+// control manager that registers a port of its own has done so. That one-time sizing is warm-up, like a core's first
+// frame.
+void checkWholeBlocksAreFree(SystemBase& core, const std::vector<float*>& lanes) {
+    core.pressButton(0, true);
+    wholeBlock(core, 1024, lanes, false);
+    core.pressButton(0, false);
+    for (int b = 0; b < 3; ++b) wholeBlock(core, 1024, lanes, false);
+    for (const std::uint32_t frames : kRaggedBlocks) {
+        const auto counted = wholeBlock(core, frames, lanes, true);
+        INFO(frames << " frames: " << counted.allocations << " allocations, " << counted.frees << " frees");
+        REQUIRE(counted.allocations == 0);
+        REQUIRE(counted.frees == 0);
+    }
+}
+
 // A core the Engine built publishes a whole savestate every 0.5 s of block frames (SystemBase's accumulator), and that
 // capture allocates, in Mesen's streaming serializer. Charge a whole interval uncounted, through the publish call
 // itself, so the capture happens HERE and not inside a counted window: every counted stretch below then stays under
@@ -2534,6 +2601,7 @@ TEST_CASE("a Mesen audio ring keeps a block in fixed storage, draining, wrapping
     // defaults to the engine's plan size (a node hands its own in at activation).
     STATIC_REQUIRE(MesenAudioDevice::kMaxPlayBufferFrames == 0x8000);
     STATIC_REQUIRE(MesenAudioDevice::kDefaultBlockFrames == Engine::kDefaultMaxBlockFrames);
+    STATIC_REQUIRE(SystemBase::kDefaultBlockFrames == Engine::kDefaultMaxBlockFrames);
 
     MesenAudioDevice ring;
     CHECK(ring.capacityFrames() == MesenAudioDevice::kDefaultBlockFrames + MesenAudioDevice::kMaxPlayBufferFrames);
@@ -2697,15 +2765,102 @@ TEST_CASE("a Mesen core's audio path drains without allocating at every block si
     }
 }
 
+TEST_CASE("a warmed core's whole block allocates and frees nothing, emulation included",
+          "[engine-graph][system-node][alloc]") {
+    // The promise's other half: a REAL block, with the step phase in the counted window, so the video frames, controller
+    // polls and notifications a block carries are counted too. Each core is built directly (as the cases above build
+    // theirs), warmed by 4 x 1024 frames and then run at every ragged size, and every core is expected to be free at
+    // every size.
+    SECTION("NES, mixed") {
+        auto core = nes(1, 0);
+        core->reserveAudio(8192);  // what a node's activate does with its plan's maximum
+        Lanes lanes(2);
+        checkWholeBlocksAreFree(*core, {lanes.ptrs[0], lanes.ptrs[1]});
+        CHECK(core->audioRingOverflow() == 0);
+    }
+    SECTION("NES, tap armed, its pins wired") {
+        // The tap's own per-block work: the pin blips, the per-stream resamplers and the capture rings, every block.
+        auto core = nes(1, 1);
+        core->reserveAudio(8192);
+        Lanes lanes(3);
+        std::vector<float*> tap;
+        for (std::size_t k = 0; k < 3; ++k) {
+            tap.push_back(lanes.ptrs[k]);
+            tap.push_back(lanes.ptrs[k]);
+        }
+        checkWholeBlocksAreFree(*core, tap);
+        CHECK(core->tapOverflow() == 0);
+    }
+    SECTION("NES, through the engine") {
+        // The same promise at the level the host sees: the whole graph block, so the system node's own per-block work
+        // (params, settings, the tap's sync, its message digests) is inside the window too. The core is the Engine's,
+        // so it publishes a whole savestate every 0.5 s: that capture allocates in Mesen's serializer, and is charged a
+        // full interval here before the window, which stays under one interval (~12.5 k of 24 k frames).
+        Engine e(kRate);
+        rp::host::test::Roms roms;
+        auto systems = std::vector<rp::host::test::DocSystem>{{1, rp::host::test::kNesType}};
+        rp::host::test::withRoms(e, systems, roms);
+        REQUIRE(e.setGraph(rp::host::test::graphRequest(rp::host::test::stereoDocument(systems), {1})).ok);
+        auto* const core = dynamic_cast<MesenNesSystem*>(e.findSystem(1));
+        REQUIRE(core != nullptr);
+        Lanes lanes(2);
+        for (int b = 0; b < 8; ++b) e.processBlock(1024, lanes.ptrs.data(), 2);  // warm
+        chargeSnapshotInterval(*core);
+        for (const std::uint32_t frames : kRaggedBlocks) {
+            std::fill_n(lanes.ptrs[0], frames, 0.0f);
+            std::fill_n(lanes.ptrs[1], frames, 0.0f);
+            const std::uint64_t before = ramHash(*core);
+            rp::test::beginCountingAllocations(true);
+            e.processBlock(frames, lanes.ptrs.data(), 2);
+            const auto counted = rp::test::endCountingAllocations();
+            INFO(frames << " frames: " << counted.allocations << " allocations, " << counted.frees << " frees");
+            REQUIRE(counted.allocations == 0);
+            REQUIRE(counted.frees == 0);
+            // The plan's largest block ran: it was not refused as too long, and the core's RAM moved.
+            if (frames == 8192) REQUIRE(ramHash(*core) != before);
+        }
+    }
+    SECTION("SMS") {
+        auto core = sms(1);
+        core->reserveAudio(8192);
+        Lanes lanes(2);
+        checkWholeBlocksAreFree(*core, {lanes.ptrs[0], lanes.ptrs[1]});
+    }
+    SECTION("Game Boy") {
+        // Both shapes: the mixed stream, and the four stem pairs a plan that wires ch1..ch4 hands it.
+        auto core = mgb(1);
+        core->reserveAudio(8192);
+        Lanes mix(2);
+        checkWholeBlocksAreFree(*core, {mix.ptrs[0], mix.ptrs[1]});
+        Lanes stems(8);
+        checkWholeBlocksAreFree(*core, stems.ptrs);
+    }
+    SECTION("GBA") {
+        // A sibling fixture, so this leaves on a run without one (as CI's) rather than failing there.
+        const auto rom = resourceFixture("/roms/nanoloop287d.gba");
+        if (rom.empty()) return;
+        auto core = std::make_unique<MesenGbaSystem>(1, MesenGbaConfig{}, rom);
+        core->onActivate(kRate);
+        core->reserveAudio(8192);
+        Lanes lanes(2);
+        checkWholeBlocksAreFree(*core, {lanes.ptrs[0], lanes.ptrs[1]});
+    }
+}
+
 TEST_CASE("a tap armed live on an activated NES sizes nothing on the block thread, and its audio path stays free",
           "[engine-graph][system-node][mesen][tap][alloc]") {
     // A plugin routing change arms the tap through the plan's SyncTap trailer (Engine::syncTap), which the block thread
-    // runs as it adopts the new plan - i.e. BETWEEN two blocks, on the audio thread. Nothing may be SIZED there: the
-    // ring, the per-block scratches, the tap's rings and its resample scratch were all sized at activation
-    // (reserveAudio). The counted window is the arm itself; the one allocation an arm still makes is a newly armed
-    // stream's blip, which is blip_new's malloc (fixed-size, not the block's), so this allocator does not see it. The
-    // blocks after it are held the way the case above holds a core's: their drain is counted, their step (and Mesen's
-    // frame plumbing inside it) is not.
+    // runs as it adopts the new plan - i.e. BETWEEN two blocks, on the audio thread. Nothing may be SIZED or allocated
+    // there: the ring, the per-block scratches, the tap's rings, its resample scratch and each stream's blip were all
+    // created at activation (reserveAudio), so an arm only re-initialises the streams it switches on. The counted
+    // window is the arm itself, C allocations included, because a stream's blip would be blip_new's malloc: that is what
+    // the counting allocator's C half is for, and this case measures it. Which builds prove the arm: a plain glibc build,
+    // where countsCAllocations() is true. Under ASan/TSan the sanitizer owns the allocator and the C half is off by
+    // design, and Windows' static CRT and macOS have no glibc name to forward to, so the case SKIPS there rather than
+    // pass green at a parent that allocated: a case whose promise is a C allocation cannot be held without it. The
+    // blocks after it are held the way the case above holds a core's: their drain is counted, their step is not.
+    if (!rp::test::countsCAllocations())
+        SKIP("the counting allocator's C half is off in this build, so the arm's blip allocation is not visible");
     Engine e(kRate);
     rp::host::test::Roms roms;
     auto systems = std::vector<rp::host::test::DocSystem>{{1, rp::host::test::kNesType}};
@@ -2724,7 +2879,7 @@ TEST_CASE("a tap armed live on an activated NES sizes nothing on the block threa
     // The audio thread owns the next plan's adoption, so the arm is queued and the block thread runs it.
     e.setAudioThreadOwns(true);
     REQUIRE(e.setGraph(rp::host::test::rearming(document(), 1, 1)).ok);
-    rp::test::beginCountingAllocations();
+    rp::test::beginCountingAllocations(true);  // the arm's blips are C blocks (see CountingAllocator)
     e.drainCommands();
     const auto arm = rp::test::endCountingAllocations();
     e.setAudioThreadOwns(false);
@@ -2784,3 +2939,113 @@ TEST_CASE("a node re-activation grows a Mesen core's audio ring to the new block
     rig.host->processBlock(16384, t, big.ptrs.data(), 2);
     CHECK(core->audioRingOverflow() == 0);
 }
+
+TEST_CASE("a node sizes its core's block buffers to the plan it was built by, not the engine's default",
+          "[engine-graph][system-node][alloc]") {
+    // The plan's maximum reaches the core in its build request, so activation sizes the ring and the per-block
+    // scratches to it: a DAW that runs 64-frame blocks does not keep the memory an 8192-frame plan would need until a
+    // host grows its block. A core built outside a plan (the direct twins the rest of this file builds) keeps the
+    // console's default.
+    SECTION("NES, at a 64-frame plan") {
+        NodeRig rig(kRate, 64);
+        REQUIRE(rig.set(R"({"nodes":[{"id":"nes","type":"rp.mesen.nes","settings":{)" + rig.boot("rp.mesen.nes", 1) +
+                        R"(}},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[2]}}],
+                            "wires":[{"from":"nes.mix","to":"out.out.1-2"}]})").ok);
+        auto* const core = dynamic_cast<MesenNesSystem*>(rig.builtCore(1));
+        REQUIRE(core != nullptr);
+        CHECK(core->audioRingCapacity() == 64 + MesenAudioDevice::kMaxPlayBufferFrames);
+    }
+    SECTION("Game Boy, at a 64-frame plan") {
+        NodeRig rig(kRate, 64);
+        REQUIRE(rig.set(R"({"nodes":[{"id":"gb","type":"rp.sameboy.gb","settings":{)" +
+                        rig.boot("rp.sameboy.gb", 1, kGbDmg) +
+                        R"(}},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[2]}}],
+                            "wires":[{"from":"gb.mix","to":"out.out.1-2"}]})").ok);
+        auto* const core = dynamic_cast<SameBoySystem*>(rig.builtCore(1));
+        REQUIRE(core != nullptr);
+        CHECK(core->stereoAccum_.size() == std::size_t(2 * 64 + 16));
+        for (const auto& channel : core->chanAccum_) CHECK(channel.size() == std::size_t(2 * 64 + 16));
+    }
+    SECTION("a core built outside a plan keeps the default") {
+        auto mesen = nes(1, 0);
+        CHECK(mesen->audioRingCapacity() ==
+              MesenAudioDevice::kDefaultBlockFrames + MesenAudioDevice::kMaxPlayBufferFrames);
+        auto gameboy = mgb(1);
+        CHECK(gameboy->stereoAccum_.size() == std::size_t(2 * SameBoySystem::kDefaultBlockFrames + 16));
+    }
+}
+
+namespace {
+
+// A hash of a frame's bytes, dimensions included, as FNV-1a. The frame the core last published is what a host reads.
+std::uint64_t frameHash(SystemBase& core) {
+    FrameBufferTriple* fb = core.framebuffer();
+    REQUIRE(fb != nullptr);
+    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(fb->width()) * fb->height());
+    REQUIRE(fb->readInto(pixels.data(), static_cast<std::uint32_t>(pixels.size())));
+    std::uint64_t hash = 1469598103934665603ull;
+    const auto mix = [&hash](const void* bytes, std::size_t size) {
+        const auto* p = static_cast<const std::uint8_t*>(bytes);
+        for (std::size_t i = 0; i < size; ++i) {
+            hash ^= p[i];
+            hash *= 1099511628211ull;
+        }
+    };
+    const std::uint32_t dims[2] = {fb->width(), fb->height()};
+    mix(dims, sizeof dims);
+    mix(pixels.data(), pixels.size() * sizeof(std::uint32_t));
+    return hash;
+}
+
+// `blocks` real blocks on `core`, the hash of the frame the core published in each.
+std::vector<std::uint64_t> frameHashes(SystemBase& core, int blocks, std::uint32_t frames = 4096) {
+    std::vector<std::uint64_t> hashes;
+    Lanes lanes(2, frames);
+    for (int b = 0; b < blocks; ++b) {
+        wholeBlock(core, frames, {lanes.ptrs[0], lanes.ptrs[1]}, false);
+        hashes.push_back(frameHash(core));
+    }
+    return hashes;
+}
+
+}  // namespace
+
+TEST_CASE("each console renders the frames it always has, bit for bit",
+          "[engine-graph][system-node][video]") {
+    // The video path this stage made reuse its buffers is held to the bytes it produced before the change: these are
+    // the frames four consoles publish over their first blocks, recorded at e30c39f5 (the parent of the commit that
+    // made a frame's buffers reusable) and equal after it. Parity is audio plus integer state, so without this a
+    // change to RenderedFrame, VideoDecoder or a PPU's frame plumbing would be read, not checked.
+    SECTION("NES") {
+        auto core = nes(1, 0);
+        const std::vector<std::uint64_t> got = frameHashes(*core, 3);
+        INFO("NES: " << got[0] << ", " << got[1] << ", " << got[2]);
+        CHECK(got == std::vector<std::uint64_t>{14252490387203307000ull, 14252490387203307000ull,
+                                               14252490387203307000ull});
+    }
+    SECTION("Master System") {
+        auto core = sms(1);
+        const std::vector<std::uint64_t> got = frameHashes(*core, 3);
+        INFO("SMS: " << got[0] << ", " << got[1] << ", " << got[2]);
+        CHECK(got == std::vector<std::uint64_t>{16816052646246439624ull, 16816052646246439624ull,
+                                               668230188373870873ull});
+    }
+    SECTION("Game Boy") {
+        auto core = mgb(1);
+        const std::vector<std::uint64_t> got = frameHashes(*core, 3);
+        INFO("GB: " << got[0] << ", " << got[1] << ", " << got[2]);
+        CHECK(got == std::vector<std::uint64_t>{8259550387572866035ull, 10074299620259861403ull,
+                                               13782190143996438851ull});
+    }
+    SECTION("GBA") {
+        // A sibling fixture, so this leaves on a run without one (as CI's) rather than failing there.
+        const auto rom = resourceFixture("/roms/nanoloop287d.gba");
+        if (rom.empty()) return;
+        auto core = std::make_unique<MesenGbaSystem>(1, MesenGbaConfig{}, rom);
+        core->onActivate(kRate);
+        const std::vector<std::uint64_t> got = frameHashes(*core, 3);
+        INFO("GBA: " << got[0] << ", " << got[1] << ", " << got[2]);
+        CHECK(got == std::vector<std::uint64_t>{3100562949282106931ull, 3100562949282106931ull,
+                                               2667698829920294451ull});
+    }
+}
diff --git a/packages/native/test/graph/CountingAllocator.cpp b/packages/native/test/graph/CountingAllocator.cpp
index 39851d10..ef16b6f6 100644
--- a/packages/native/test/graph/CountingAllocator.cpp
+++ b/packages/native/test/graph/CountingAllocator.cpp
@@ -13,16 +13,42 @@
 #include <malloc.h>
 #endif
 
+// A C library's allocations (blip_buf's blip_new, for one) are malloc's, not operator new's, so they are counted
+// too: the interposed forms below forward to the platform's own allocator. Only glibc exports that allocator under a
+// name we can call (`__libc_*`), and only where nothing else owns the allocator: a sanitizer build hands every block
+// to the sanitizer's allocator and must see all of them (a block it allocated, freed through ours, would be freed the
+// wrong way), and Windows links the static CRT, whose malloc would then be defined twice. On those the C allocations
+// are not counted, and countsCAllocations() says so, so a case whose promise includes one SKIPS rather than measuring
+// nothing and passing. Only malloc, calloc, realloc and free are interposed: a block from strdup, posix_memalign or
+// aligned_alloc is invisible as an allocation while the free that releases it is counted, so a window that demands no
+// frees should not put one of those in another window's way.
+#if defined(__has_feature)
+#  if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
+#    define RP_ALLOCATOR_IS_SANITIZED 1
+#  endif
+#elif defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
+#  define RP_ALLOCATOR_IS_SANITIZED 1
+#endif
+
+#if defined(__GLIBC__) && !defined(_WIN32) && !defined(RP_ALLOCATOR_IS_SANITIZED)
+#define RP_COUNT_C_ALLOCATIONS 1
+extern "C" void* __libc_malloc(std::size_t size);
+extern "C" void* __libc_calloc(std::size_t count, std::size_t size);
+extern "C" void* __libc_realloc(void* ptr, std::size_t size);
+extern "C" void  __libc_free(void* ptr);
+#endif
+
 namespace {
 
 thread_local bool counting = false;
+thread_local bool countingC = false;  // the C library's blocks too (see beginCountingAllocations)
 thread_local std::size_t refuseFrom = SIZE_MAX;  // an allocation of at least this many bytes fails
 std::atomic<int>  allocations{0};  // while counting
 std::atomic<int>  frees{0};        // while counting
 
-// Every block is allocated over-aligned through one pair of calls, so any delete form can free any new
-// form - the platform's aligned allocator does not mix with plain free on Windows.
-void* rawAlloc(std::size_t n, std::size_t align) {
+// The allocator the blocks are actually allocated from. Under C counting that is glibc's, called by name so the
+// interposed free below does not see (and count) the frees our own operator delete makes.
+void* systemAlloc(std::size_t n, std::size_t align) {
     if (align < alignof(std::max_align_t)) align = alignof(std::max_align_t);
     if (n > SIZE_MAX - align) return nullptr;  // rounding up would wrap
     const std::size_t size = (n ? n : 1) + align - 1;
@@ -33,15 +59,27 @@ void* rawAlloc(std::size_t n, std::size_t align) {
 #endif
 }
 
-void rawFree(void* p) noexcept {
-    if (counting && p) frees.fetch_add(1, std::memory_order_relaxed);
-#if defined(_WIN32)
+void systemFree(void* p) noexcept {
+#if defined(RP_COUNT_C_ALLOCATIONS)
+    __libc_free(p);
+#elif defined(_WIN32)
     _aligned_free(p);
 #else
     std::free(p);
 #endif
 }
 
+// Every block is allocated over-aligned through one pair of calls, so any delete form can free any new
+// form - the platform's aligned allocator does not mix with plain free on Windows.
+void* rawAlloc(std::size_t n, std::size_t align) {
+    return systemAlloc(n, align);
+}
+
+void rawFree(void* p) noexcept {
+    if (counting && p) frees.fetch_add(1, std::memory_order_relaxed);
+    systemFree(p);
+}
+
 void* allocate(std::size_t n, std::size_t align) {
     if (counting) allocations.fetch_add(1, std::memory_order_relaxed);
     if (n >= refuseFrom) throw std::bad_alloc();
@@ -60,17 +98,27 @@ void* allocateOrNull(std::size_t n, std::size_t align) noexcept {
 
 namespace rp::test {
 
-void beginCountingAllocations() {
+void beginCountingAllocations(bool includeCAllocations) {
     allocations = 0;
     frees = 0;
+    countingC = includeCAllocations;
     counting = true;
 }
 
 AllocationCount endCountingAllocations() {
     counting = false;
+    countingC = false;
     return {allocations.load(), frees.load()};
 }
 
+bool countsCAllocations() {
+#if defined(RP_COUNT_C_ALLOCATIONS)
+    return true;
+#else
+    return false;
+#endif
+}
+
 void refuseAllocationsFrom(std::size_t bytes) { refuseFrom = bytes; }
 
 }  // namespace rp::test
@@ -101,3 +149,34 @@ void operator delete(void* p, const std::nothrow_t&) noexcept { rawFree(p); }
 void operator delete[](void* p, const std::nothrow_t&) noexcept { rawFree(p); }
 void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept { rawFree(p); }
 void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept { rawFree(p); }
+
+#if defined(RP_COUNT_C_ALLOCATIONS)
+// The C library's allocation entry points, counted on the thread that asked for the window. Each forwards to glibc's
+// own (see the note at the top). free counts only a real block, as operator delete does.
+extern "C" void* malloc(std::size_t size) {
+    if (countingC) allocations.fetch_add(1, std::memory_order_relaxed);
+    return __libc_malloc(size);
+}
+
+extern "C" void* calloc(std::size_t count, std::size_t size) {
+    if (countingC) allocations.fetch_add(1, std::memory_order_relaxed);
+    return __libc_calloc(count, size);
+}
+
+extern "C" void* realloc(void* ptr, std::size_t size) {
+    // A realloc to a real size is one allocation (its block may be the same one); to zero it releases.
+    if (countingC) {
+        if (size) {
+            allocations.fetch_add(1, std::memory_order_relaxed);
+        } else if (ptr) {
+            frees.fetch_add(1, std::memory_order_relaxed);
+        }
+    }
+    return __libc_realloc(ptr, size);
+}
+
+extern "C" void free(void* ptr) {
+    if (countingC && ptr) frees.fetch_add(1, std::memory_order_relaxed);
+    __libc_free(ptr);
+}
+#endif
diff --git a/packages/native/test/graph/CountingAllocator.hpp b/packages/native/test/graph/CountingAllocator.hpp
index 572b6651..9b78c443 100644
--- a/packages/native/test/graph/CountingAllocator.hpp
+++ b/packages/native/test/graph/CountingAllocator.hpp
@@ -4,6 +4,14 @@
 // operator new and delete for the binary it is linked into (every form, so a block any of them allocated is
 // freed by any other), and counts each call made on a thread while that thread is counting. It also refuses, on
 // a thread that asked it to, every allocation from a given size up. Link it into one binary at most once.
+//
+// A C library's blocks are malloc's, not operator new's, so the C library's allocation entry points (malloc,
+// calloc, realloc, free) are replaced too, where the platform's allocator can be reached by name: on glibc, and not
+// under AddressSanitizer (whose allocator must see every block) or on Windows (which links the static CRT, where the
+// CRT's malloc cannot be defined twice). countsCAllocations() says whether this binary sees them, so a case that holds
+// a C allocation can say what it measured. They are counted only on request, because a window can be measuring a
+// subsystem whose own allocations ARE the C library's: the script host's QuickJS allocates through malloc, and the
+// script cases read QuickJS's counters for its side and hold operator new to zero for ours.
 
 #include <cstddef>
 
@@ -14,11 +22,15 @@ struct AllocationCount {
     int frees = 0;
 };
 
-// Start counting on this thread, from zero. Only one thread counts at a time.
-void beginCountingAllocations();
+// Start counting on this thread, from zero. With `includeCAllocations`, the C library's blocks are counted too (see
+// above), which a case does when a C allocation is part of the promise it holds. Only one thread counts at a time.
+void beginCountingAllocations(bool includeCAllocations = false);
 // Stop, and what this thread allocated and freed since beginCountingAllocations.
 AllocationCount endCountingAllocations();
 
+// Whether beginCountingAllocations also counts the C library's allocations in this binary (see above).
+bool countsCAllocations();
+
 // From now on, this thread's allocations of at least `bytes` fail (a throwing form throws std::bad_alloc, a
 // nothrow form returns null). SIZE_MAX, the default, refuses none.
 void refuseAllocationsFrom(std::size_t bytes);
````

- [ ] **Step 2: Run the tests to see them fail**

Run: `cmake -S . -B build && cmake --build build --target retroplug-engine-graph-test -j$(nproc)`
Expected: FAIL: the build stops at `SystemBase::kDefaultBlockFrames` in the ring case's `STATIC_REQUIRE`, which is the one name the cases read that this task adds, and Step 6 is where it arrives. Nothing else the new cases use is missing, because Step 1 itself writes both halves of `CountingAllocator`.

The prototype's measured red, over 4 x 8192-frame blocks of real emulation at the parent commit, was the promise's
whole obstacle: `738 allocations for the NES and 738 for the SMS`, about 18 an emulated frame on the audio thread,
attributed by source with a temporary return-address hook (removed before the commit): `GetPortStates` 3,
`GetRawState` 2, the PPU's `RenderedFrame` constructor 6, `VideoDecoder::DecodeFrame`'s `convertedFrame` 6,
`SendNotification` 1. In the same two windows after the change: `0 allocations, 0 frees`. The case also found, in
order and each fixed in this commit: the N8 FIFO parser's growth (7 allocations, then 7 more at 8192), a
first-input-poll ripple of 15 (a device's state sized at the first read rather than the first poll), and the arm's 3
`malloc`s. `VideoRenderer::UpdateFrame`'s `_lastFrame = frame` was on the list and needed nothing: copy assignment
into a persistent member reuses its vectors' capacity, and it still measures zero.

- [ ] **Step 3: Mesen: the port snapshot and the controller state**

`BaseControlManager::GetPortStates` fills the manager's own `_portStates` and returns it by `const&` (the reference's
lifetime is the call, as its header says). `BaseControlDevice` gains `GetRawState(ControlDeviceState&)`, which fills
the caller's state, with the returning form delegating to it so its other callers are untouched; `EnsureCapacity`
grows the state to `ControlDeviceState::MaxBytes` in one step, and `ClearState` clears it in place and reserves that
maximum at a device's first poll. The state stays EMPTY, which is what the four devices that read emptiness as data
depend on. `GetPortStates` sizes each snapshot entry's state before filling it and pads it to the maximum, so a device
no ROM has read yet, or one a mapper or role device registers late, cannot grow the snapshot or any of its copies
inside a block. `NotificationManager`'s send reuses the copy it iterates per thread and per nesting depth, each held
as a `unique_ptr<vector<weak_ptr<...>>>`:

````diff
diff --git a/deps/mesen/Core/Shared/BaseControlDevice.cpp b/deps/mesen/Core/Shared/BaseControlDevice.cpp
index 97d8a3b8..2e91094a 100644
--- a/deps/mesen/Core/Shared/BaseControlDevice.cpp
+++ b/deps/mesen/Core/Shared/BaseControlDevice.cpp
@@ -70,13 +70,33 @@ void BaseControlDevice::StrobeProcessWrite(uint8_t value)
 void BaseControlDevice::ClearState()
 {
 	auto lock = _stateLock.AcquireSafe();
-	_state = ControlDeviceState();
+	// RetroPlug: cleared in place rather than replaced with a fresh empty state, so the vector keeps its capacity. A poll
+	// clears every device before it re-reads the input, and replacing the state freed its buffer every frame; clearing
+	// it frees nothing, and the read that follows grows the state back into the capacity it already has. The state stays
+	// EMPTY, which four devices read as their contract: a controller hub builds its composite state by appending to what
+	// a clear leaves, and the barcode readers and the data recorder take "no state" to mean the tape is not playing.
+	//
+	// The buffer is sized here, at the device's FIRST poll, rather than left to the first read: a ROM may not read its
+	// controller for thousands of frames, and that first read must not be the thing that allocates on the block thread.
+	// This is the only place a state is ever sized from nothing.
+	if (_state.State.capacity() < ControlDeviceState::MaxBytes) {
+		_state.State.reserve(ControlDeviceState::MaxBytes);
+	}
+	_state.State.clear();
 }
 
-ControlDeviceState BaseControlDevice::GetRawState()
+// RetroPlug: assigns into the caller's state (whose vector keeps its capacity) instead of returning a copy.
+void BaseControlDevice::GetRawState(ControlDeviceState& state)
 {
 	auto lock = _stateLock.AcquireSafe();
-	return _state;
+	state.State = _state.State;
+}
+
+ControlDeviceState BaseControlDevice::GetRawState()
+{
+	ControlDeviceState state;
+	GetRawState(state);
+	return state;
 }
 
 void BaseControlDevice::DrawController(InputHud& hud)
@@ -100,6 +120,7 @@ void BaseControlDevice::SetTextState(string textState)
 	ClearState();
 
 	if(IsRawString()) {
+		//ClearState left the state empty, so the text is all of it
 		_state.State.insert(_state.State.end(), textState.begin(), textState.end());
 	} else {
 		if(HasCoordinates()) {
@@ -167,7 +188,10 @@ void BaseControlDevice::EnsureCapacity(int32_t minBitCount)
 	int32_t gap = minByteCount - (int32_t)_state.State.size();
 
 	if(gap > 0) {
-		_state.State.insert(_state.State.end(), gap, 0);
+		// Grown to the maximum in one step, not to the byte this caller needs: a second growth would land inside a
+		// frame (NES $4016's microphone bit is Byte 1, after the eight ToByte() reads sized the state to one), and
+		// every copy of the state - the port snapshot a video frame carries - has to grow with it.
+		_state.State.insert(_state.State.end(), ControlDeviceState::MaxBytes - _state.State.size(), 0);
 	}
 }
 
diff --git a/deps/mesen/Core/Shared/BaseControlDevice.h b/deps/mesen/Core/Shared/BaseControlDevice.h
index e167fa55..a5622ffb 100644
--- a/deps/mesen/Core/Shared/BaseControlDevice.h
+++ b/deps/mesen/Core/Shared/BaseControlDevice.h
@@ -91,6 +91,9 @@ public:
 	virtual void OnAfterSetState() { }
 	
 	virtual void SetRawState(ControlDeviceState state);
+	// RetroPlug: the buffer-filling form is what the per-frame port snapshot uses; it copies into a state the caller
+	// keeps, so a warm caller allocates nothing. The returning form is the same read for the callers that want a copy.
+	virtual void GetRawState(ControlDeviceState& state);
 	virtual ControlDeviceState GetRawState();
 
 	virtual void InternalDrawController(InputHud& hud) {}
diff --git a/deps/mesen/Core/Shared/BaseControlManager.cpp b/deps/mesen/Core/Shared/BaseControlManager.cpp
index 38717704..9dfd50aa 100644
--- a/deps/mesen/Core/Shared/BaseControlManager.cpp
+++ b/deps/mesen/Core/Shared/BaseControlManager.cpp
@@ -58,17 +58,29 @@ void BaseControlManager::UnregisterInputRecorder(IInputRecorder* provider)
 	vec.erase(std::remove(vec.begin(), vec.end(), provider), vec.end());
 }
 
-vector<ControllerData> BaseControlManager::GetPortStates()
+// RetroPlug: fills the manager's own vector rather than building a new one per call, and asks each device for its
+// state through the buffer-filling overload, so the video frame a console emits every frame copies no vectors.
+const vector<ControllerData>& BaseControlManager::GetPortStates()
 {
-	vector<ControllerData> states;
-	for(shared_ptr<BaseControlDevice>& device : _controlDevices) {
-		states.push_back({
-			device->GetControllerType(),
-			device->GetRawState(),
-			device->GetPort()
-		});
+	_portStates.resize(_controlDevices.size());
+	for(size_t i = 0; i < _controlDevices.size(); i++) {
+		ControllerData& state = _portStates[i];
+		state.Type = _controlDevices[i]->GetControllerType();
+		state.Port = _controlDevices[i]->GetPort();
+		// RetroPlug: the snapshot's state buffer is sized before it is filled and padded after, to the largest state a
+		// device can produce, so a frame's snapshot has one length for the frame's whole life. Without it, a device no
+		// ROM has read yet - or a mapper or role device the control manager has just registered - carries an empty state,
+		// and the first read (thousands of frames in, for a ROM that polls its controller late) would be what grows this
+		// vector and every copy of it: the frame's, the decoder's and the renderer's, inside a block.
+		if (state.State.State.capacity() < ControlDeviceState::MaxBytes) {
+			state.State.State.reserve(ControlDeviceState::MaxBytes);
+		}
+		_controlDevices[i]->GetRawState(state.State);
+		if (state.State.State.size() < ControlDeviceState::MaxBytes) {
+			state.State.State.resize(ControlDeviceState::MaxBytes, 0);
+		}
 	}
-	return states;
+	return _portStates;
 }
 
 shared_ptr<BaseControlDevice> BaseControlManager::GetControlDevice(uint8_t port, uint8_t subPort)
diff --git a/deps/mesen/Core/Shared/BaseControlManager.h b/deps/mesen/Core/Shared/BaseControlManager.h
index cc1a3e4b..346986f0 100644
--- a/deps/mesen/Core/Shared/BaseControlManager.h
+++ b/deps/mesen/Core/Shared/BaseControlManager.h
@@ -24,6 +24,9 @@ protected:
 	SimpleLock _deviceLock;
 	vector<shared_ptr<BaseControlDevice>> _systemDevices;
 	vector<shared_ptr<BaseControlDevice>> _controlDevices;
+	// RetroPlug: the port snapshot a video frame carries is built here, so the vector (and each device's state
+	// buffer) keeps its storage from frame to frame instead of being rebuilt for every frame.
+	vector<ControllerData> _portStates;
 	uint32_t _pollCounter = 0;
 	uint32_t _lagCounter = 0;
 	bool _wasInputRead = false;
@@ -65,7 +68,9 @@ public:
 
 	virtual shared_ptr<BaseControlDevice> CreateControllerDevice(ControllerType type, uint8_t port) = 0;
 
-	vector<ControllerData> GetPortStates();
+	// RetroPlug: the manager's own snapshot, refilled per call (no allocation once it has seen this many devices);
+	// the reference is valid until the next call.
+	const vector<ControllerData>& GetPortStates();
 
 	shared_ptr<BaseControlDevice> GetControlDevice(uint8_t port, uint8_t subPort = 0);
 	shared_ptr<BaseControlDevice> GetControlDeviceByIndex(uint8_t index);
diff --git a/deps/mesen/Core/Shared/ControlDeviceState.h b/deps/mesen/Core/Shared/ControlDeviceState.h
index ed706f79..3897071f 100644
--- a/deps/mesen/Core/Shared/ControlDeviceState.h
+++ b/deps/mesen/Core/Shared/ControlDeviceState.h
@@ -5,6 +5,12 @@
 
 struct ControlDeviceState
 {
+	// RetroPlug: the largest state any read or write can ask for - the byte holding bit 255, plus the two 4-byte
+	// coordinate fields a pointing device adds (HasCoordinates()). A state is grown to it in one step (EnsureCapacity),
+	// and every place that keeps a state between calls reserves it before copying one in, so a state is never what grows
+	// a buffer inside a block.
+	static constexpr uint32_t MaxBytes = 255 / 8 + 1 + 32;
+
 	vector<uint8_t> State;
 
 	bool operator!=(ControlDeviceState &other)
diff --git a/deps/mesen/Core/Shared/NotificationManager.cpp b/deps/mesen/Core/Shared/NotificationManager.cpp
index ca210230..c26d3595 100644
--- a/deps/mesen/Core/Shared/NotificationManager.cpp
+++ b/deps/mesen/Core/Shared/NotificationManager.cpp
@@ -2,6 +2,38 @@
 #include <algorithm>
 #include "Shared/NotificationManager.h"
 
+namespace {
+	// RetroPlug: the copy SendNotification iterates (a listener may unregister while the notification runs, so the
+	// list cannot be iterated under the lock) is reused per thread, and per nesting depth: a listener that sends a
+	// notification of its own gets a buffer of its own from the pool, so nesting stays correct. The pool grows to
+	// the deepest nesting a thread has seen and never again, so a frame's notifications allocate nothing.
+	//
+	// Each buffer is held behind its own pointer: growing the pool for a deeper nesting moves the pool's elements,
+	// and a reference to a buffer must stay valid across the nested send that grew it.
+	thread_local vector<unique_ptr<vector<weak_ptr<INotificationListener>>>> dispatchBuffers;
+	thread_local size_t dispatchDepth = 0;
+
+	vector<weak_ptr<INotificationListener>>& takeDispatchBuffer()
+	{
+		if(dispatchBuffers.size() <= dispatchDepth) {
+			dispatchBuffers.push_back(std::make_unique<vector<weak_ptr<INotificationListener>>>());
+		}
+		unique_ptr<vector<weak_ptr<INotificationListener>>>& buffer = dispatchBuffers[dispatchDepth++];
+		return *buffer;
+	}
+
+	// One SendNotification's buffer: taken from the pool on construction, handed back on any exit (a listener is
+	// arbitrary code and may throw).
+	struct DispatchBuffer
+	{
+		vector<weak_ptr<INotificationListener>>& listeners;
+
+		DispatchBuffer() : listeners(takeDispatchBuffer()) {}
+
+		~DispatchBuffer() { dispatchDepth--; }
+	};
+}
+
 void NotificationManager::RegisterNotificationListener(shared_ptr<INotificationListener> notificationListener)
 {
 	auto lock = _lock.AcquireSafe();
@@ -33,7 +65,8 @@ void NotificationManager::CleanupNotificationListeners()
 
 void NotificationManager::SendNotification(ConsoleNotificationType type, void* parameter)
 {
-	vector<weak_ptr<INotificationListener>> listeners;
+	DispatchBuffer dispatch;
+	vector<weak_ptr<INotificationListener>>& listeners = dispatch.listeners;
 	{
 		auto lock = _lock.AcquireSafe();
 		CleanupNotificationListeners();
@@ -41,7 +74,7 @@ void NotificationManager::SendNotification(ConsoleNotificationType type, void* p
 	}
 
 	//Iterate on a copy without using a lock
-	for(weak_ptr<INotificationListener> notificationListener : listeners) {
+	for(weak_ptr<INotificationListener>& notificationListener : listeners) {
 		shared_ptr<INotificationListener> listener = notificationListener.lock();
 		if(listener) {
 			listener->ProcessNotification(type, parameter);
````

- [ ] **Step 4: Mesen: the frame and the input data it carries**

`RenderedFrame` gains `Reset` and `SetInputData`, which assign in place and reserve the destination's state buffer;
each of the three PPUs this fork runs refills one frame member instead of constructing a frame, and `VideoDecoder`
keeps a `_convertedFrame`; the by-value `RenderedFrame` parameter becomes `const RenderedFrame&`, and `VideoRenderer`
refills its last frame the same way instead of assigning a whole frame. Nothing a frame produces moves: the same
frame, the same input snapshot, in the same order, which the `[video]` hashes record. The by-value constructor keeps a
note that the cores this fork does not run (PCE, SNES, WS) still copy a snapshot once more per frame:

````diff
diff --git a/deps/mesen/Core/GBA/GbaPpu.cpp b/deps/mesen/Core/GBA/GbaPpu.cpp
index e8056d25..578e2144 100644
--- a/deps/mesen/Core/GBA/GbaPpu.cpp
+++ b/deps/mesen/Core/GBA/GbaPpu.cpp
@@ -182,9 +182,12 @@ void GbaPpu::SendFrame()
 	_emu->ProcessEvent(EventType::EndFrame, CpuType::Gba);
 	_emu->GetNotificationManager()->SendNotification(ConsoleNotificationType::PpuFrameDone);
 
-	RenderedFrame frame(_currentBuffer, GbaConstants::ScreenWidth, GbaConstants::ScreenHeight, 1.0, _state.FrameCount, _console->GetControlManager()->GetPortStates());
+	// RetroPlug: refilled in place rather than constructed per frame, so the input snapshot it carries reuses its
+	// storage (see RenderedFrame::Reset).
+	_renderedFrame.Reset(_currentBuffer, GbaConstants::ScreenWidth, GbaConstants::ScreenHeight, 1.0, _state.FrameCount);
+	_renderedFrame.SetInputData(_console->GetControlManager()->GetPortStates());
 	bool rewinding = _emu->GetRewindManager()->IsRewinding();
-	_emu->GetVideoDecoder()->UpdateFrame(frame, rewinding, rewinding);
+	_emu->GetVideoDecoder()->UpdateFrame(_renderedFrame, rewinding, rewinding);
 
 	_emu->ProcessEndOfFrame();
 	_console->ProcessEndOfFrame();
diff --git a/deps/mesen/Core/GBA/GbaPpu.h b/deps/mesen/Core/GBA/GbaPpu.h
index 467f8340..f740ba85 100644
--- a/deps/mesen/Core/GBA/GbaPpu.h
+++ b/deps/mesen/Core/GBA/GbaPpu.h
@@ -2,6 +2,7 @@
 #include "pch.h"
 #include "GBA/GbaTypes.h"
 #include "Shared/Emulator.h"
+#include "Shared/RenderedFrame.h"
 #include "Utilities/Timer.h"
 #include "Utilities/ISerializable.h"
 
@@ -119,6 +120,9 @@ private:
 	uint16_t* _outputBuffers[2] = {};
 	uint16_t* _currentBuffer = nullptr;
 
+	// RetroPlug: the frame SendFrame refills, so a video frame's input snapshot reuses its storage.
+	RenderedFrame _renderedFrame;
+
 	GbaPixelData* _oamWriteOutput = nullptr;
 	GbaPixelData* _oamReadOutput = nullptr;
 	GbaPixelData _renderSprPixel = {};
diff --git a/deps/mesen/Core/NES/BaseNesPpu.h b/deps/mesen/Core/NES/BaseNesPpu.h
index 674a5198..3e62a329 100644
--- a/deps/mesen/Core/NES/BaseNesPpu.h
+++ b/deps/mesen/Core/NES/BaseNesPpu.h
@@ -3,6 +3,7 @@
 #include "NES/INesMemoryHandler.h"
 #include "Utilities/ISerializable.h"
 #include "NES/NesTypes.h"
+#include "Shared/RenderedFrame.h"
 
 enum class ConsoleRegion;
 
@@ -90,6 +91,12 @@ protected:
 	//705
 	NesSpriteInfo _spriteTiles[64] = {};
 
+	// RetroPlug: the frame objects SendFrame refills, one per output shape it can emit (the VS dual system has its
+	// own, and a merged dual picture its own), so a video frame's input snapshot reuses its storage.
+	RenderedFrame _renderedFrame;
+	RenderedFrame _vsRenderedFrame;
+	RenderedFrame _mergedRenderedFrame;
+
 	Emulator* _emu = nullptr;
 	EmuSettings* _settings = nullptr;
 	uint16_t* _outputBuffers[2] = {};
diff --git a/deps/mesen/Core/NES/NesPpu.cpp b/deps/mesen/Core/NES/NesPpu.cpp
index f99ff29c..600a2ec7 100644
--- a/deps/mesen/Core/NES/NesPpu.cpp
+++ b/deps/mesen/Core/NES/NesPpu.cpp
@@ -1195,8 +1195,11 @@ template<class T> void NesPpu<T>::SendFrame()
 		videoPhase = _frameCount & 0x01;
 	}
 
-	RenderedFrame frame(_currentOutputBuffer, NesConstants::ScreenWidth, NesConstants::ScreenHeight, 1.0, _frameCount, _console->GetControlManager()->GetPortStates(), videoPhase);
-	frame.Data = frameData; //HD packs
+	// RetroPlug: one frame object for this PPU, refilled instead of constructed per frame, so the input snapshot it
+	// carries reuses its storage (see RenderedFrame::Reset).
+	_renderedFrame.Reset(_currentOutputBuffer, NesConstants::ScreenWidth, NesConstants::ScreenHeight, 1.0, _frameCount, videoPhase);
+	_renderedFrame.SetInputData(_console->GetControlManager()->GetPortStates());
+	_renderedFrame.Data = frameData; //HD packs
 
 	if(_console->GetVsMainConsole() || _console->GetVsSubConsole()) {
 		SendFrameVsDualSystem();
@@ -1205,7 +1208,7 @@ template<class T> void NesPpu<T>::SendFrame()
 		}
 	} else {
 		bool forRewind = _emu->GetRewindManager()->IsRewinding();
-		_emu->GetVideoDecoder()->UpdateFrame(frame, forRewind, forRewind);
+		_emu->GetVideoDecoder()->UpdateFrame(_renderedFrame, forRewind, forRewind);
 		_emu->ProcessEndOfFrame();
 	}
 
@@ -1217,12 +1220,14 @@ template<class T> void NesPpu<T>::SendFrameVsDualSystem()
 	NesConfig& cfg = _settings->GetNesConfig();
 	bool forRewind = _emu->GetRewindManager()->IsRewinding();
 
-	RenderedFrame frame(_currentOutputBuffer, NesConstants::ScreenWidth, NesConstants::ScreenHeight, 1.0, _frameCount, _console->GetControlManager()->GetPortStates());
+	// RetroPlug: refilled in place, as the main frame is (see RenderedFrame::Reset).
+	_vsRenderedFrame.Reset(_currentOutputBuffer, NesConstants::ScreenWidth, NesConstants::ScreenHeight, 1.0, _frameCount);
+	_vsRenderedFrame.SetInputData(_console->GetControlManager()->GetPortStates());
 
 	if(cfg.VsDualVideoOutput == VsDualOutputOption::MainSystemOnly && _console->IsVsMainConsole()) {
-		_emu->GetVideoDecoder()->UpdateFrame(frame, forRewind, forRewind);
+		_emu->GetVideoDecoder()->UpdateFrame(_vsRenderedFrame, forRewind, forRewind);
 	} else if(cfg.VsDualVideoOutput == VsDualOutputOption::SubSystemOnly && !_console->IsVsMainConsole()) {
-		_emu->GetVideoDecoder()->UpdateFrame(frame, forRewind, forRewind);
+		_emu->GetVideoDecoder()->UpdateFrame(_vsRenderedFrame, forRewind, forRewind);
 	} else if(cfg.VsDualVideoOutput == VsDualOutputOption::Both) {
 		if(_console->IsVsMainConsole()) {
 			uint16_t* mergedBuffer = new uint16_t[NesConstants::ScreenWidth * NesConstants::ScreenHeight * 2];
@@ -1239,8 +1244,9 @@ template<class T> void NesPpu<T>::SendFrameVsDualSystem()
 				in2 += NesConstants::ScreenWidth;
 			}
 
-			RenderedFrame mergedFrame(mergedBuffer, NesConstants::ScreenWidth*2, NesConstants::ScreenHeight, 1.0, _frameCount, _console->GetControlManager()->GetPortStates());
-			_emu->GetVideoDecoder()->UpdateFrame(mergedFrame, true, forRewind);
+			_mergedRenderedFrame.Reset(mergedBuffer, NesConstants::ScreenWidth*2, NesConstants::ScreenHeight, 1.0, _frameCount);
+			_mergedRenderedFrame.SetInputData(_console->GetControlManager()->GetPortStates());
+			_emu->GetVideoDecoder()->UpdateFrame(_mergedRenderedFrame, true, forRewind);
 			delete[] mergedBuffer;
 		}
 	}
diff --git a/deps/mesen/Core/SMS/SmsVdp.cpp b/deps/mesen/Core/SMS/SmsVdp.cpp
index 4f210212..a2749e9d 100644
--- a/deps/mesen/Core/SMS/SmsVdp.cpp
+++ b/deps/mesen/Core/SMS/SmsVdp.cpp
@@ -646,9 +646,12 @@ void SmsVdp::ProcessEndOfScanline()
 
 		_emu->GetNotificationManager()->SendNotification(ConsoleNotificationType::PpuFrameDone);
 
-		RenderedFrame frame(_currentOutputBuffer, 256, 240, 1.0, _state.FrameCount, _console->GetControlManager()->GetPortStates());
+		// RetroPlug: refilled in place rather than constructed per frame, so the input snapshot it carries reuses
+		// its storage (see RenderedFrame::Reset).
+		_renderedFrame.Reset(_currentOutputBuffer, 256, 240, 1.0, _state.FrameCount);
+		_renderedFrame.SetInputData(_console->GetControlManager()->GetPortStates());
 		bool rewinding = _emu->GetRewindManager()->IsRewinding();
-		_emu->GetVideoDecoder()->UpdateFrame(frame, rewinding, rewinding);
+		_emu->GetVideoDecoder()->UpdateFrame(_renderedFrame, rewinding, rewinding);
 
 		UpdateConfig();
 
diff --git a/deps/mesen/Core/SMS/SmsVdp.h b/deps/mesen/Core/SMS/SmsVdp.h
index d6f30823..80ae62bf 100644
--- a/deps/mesen/Core/SMS/SmsVdp.h
+++ b/deps/mesen/Core/SMS/SmsVdp.h
@@ -4,6 +4,7 @@
 #include "Shared/SettingTypes.h"
 #include "Shared/ColorUtilities.h"
 #include "Utilities/ISerializable.h"
+#include "Shared/RenderedFrame.h"
 
 class Emulator;
 class SmsConsole;
@@ -59,6 +60,9 @@ private:
 	uint16_t* _outputBuffers[2] = {};
 	uint16_t* _currentOutputBuffer = nullptr;
 
+	// RetroPlug: the frame ProcessEndOfScanline refills, so a video frame's input snapshot reuses its storage.
+	RenderedFrame _renderedFrame;
+
 	SmsVdpState _state = {};
 	uint64_t _lastMasterClock = 0;
 
diff --git a/deps/mesen/Core/Shared/RenderedFrame.h b/deps/mesen/Core/Shared/RenderedFrame.h
index d095046c..7bb0ca0e 100644
--- a/deps/mesen/Core/Shared/RenderedFrame.h
+++ b/deps/mesen/Core/Shared/RenderedFrame.h
@@ -27,6 +27,9 @@ struct RenderedFrame
 		InputData({})
 	{}
 
+	// RetroPlug: the consoles this fork does not run (PCE, SNES, WS) still build a frame through this constructor, so
+	// they copy the port snapshot one extra time per frame. The NES, SMS and GBA refill a member instead (Reset and
+	// SetInputData), so "no copies" is theirs and not universal.
 	RenderedFrame(void* buffer, uint32_t width, uint32_t height, double scale, uint32_t frameNumber, vector<ControllerData> inputData, uint32_t videoPhase = 0) :
 		FrameBuffer(buffer),
 		Data(nullptr),
@@ -37,4 +40,35 @@ struct RenderedFrame
 		VideoPhase(videoPhase),
 		InputData(inputData)
 	{}
+
+	// RetroPlug: refill a frame in place, and set its input snapshot in place, so a core that emits one frame per
+	// video frame keeps the frame object and the input vector's storage between frames. A frame built per video
+	// frame allocates in the constructor's InputData copies, on the emulation thread, for no gain: nothing changes
+	// between frames but the fields below.
+	void Reset(void* buffer, uint32_t width, uint32_t height, double scale, uint32_t frameNumber, uint32_t videoPhase = 0)
+	{
+		FrameBuffer = buffer;
+		Data = nullptr;
+		Width = width;
+		Height = height;
+		Scale = scale;
+		FrameNumber = frameNumber;
+		VideoPhase = videoPhase;
+	}
+
+	void SetInputData(const vector<ControllerData>& inputData)
+	{
+		InputData.resize(inputData.size());
+		for(size_t i = 0; i < inputData.size(); i++) {
+			InputData[i].Type = inputData[i].Type;
+			InputData[i].Port = inputData[i].Port;
+			// RetroPlug: reserved before the copy, for the same reason the port snapshot is (see GetPortStates): a port
+			// the control manager registers late arrives as a new element here, and its state must not be what grows
+			// this frame's storage inside a block.
+			if(InputData[i].State.State.capacity() < ControlDeviceState::MaxBytes) {
+				InputData[i].State.State.reserve(ControlDeviceState::MaxBytes);
+			}
+			InputData[i].State.State = inputData[i].State.State;
+		}
+	}
 };
diff --git a/deps/mesen/Core/Shared/Video/VideoDecoder.cpp b/deps/mesen/Core/Shared/Video/VideoDecoder.cpp
index e6be9f4b..be23a880 100644
--- a/deps/mesen/Core/Shared/Video/VideoDecoder.cpp
+++ b/deps/mesen/Core/Shared/Video/VideoDecoder.cpp
@@ -132,7 +132,8 @@ void VideoDecoder::DecodeFrame(bool forRewind)
 		ScanlineFilter::ApplyFilter(outputBuffer, frameSize.Width, frameSize.Height, _emu->GetSettings()->GetVideoConfig().ScanlineIntensity, scale);
 	}
 
-	RenderedFrame convertedFrame((void*)outputBuffer, frameSize.Width, frameSize.Height, _frame.Scale, _frame.FrameNumber, _frame.InputData);
+	_convertedFrame.Reset((void*)outputBuffer, frameSize.Width, frameSize.Height, _frame.Scale, _frame.FrameNumber);
+	_convertedFrame.SetInputData(_frame.InputData);
 
 	double aspectRatio = _emu->GetSettings()->GetAspectRatio(_emu->GetRegion(), _baseFrameSize);
 	if(frameSize.Height != _lastFrameSize.Height || frameSize.Width != _lastFrameSize.Width || aspectRatio != _lastAspectRatio) {
@@ -142,7 +143,7 @@ void VideoDecoder::DecodeFrame(bool forRewind)
 	_lastFrameSize = frameSize;
 	
 	//Rewind manager will take care of sending the correct frame to the video renderer
-	_emu->GetRewindManager()->SendFrame(convertedFrame, forRewind);
+	_emu->GetRewindManager()->SendFrame(_convertedFrame, forRewind);
 }
 
 uint32_t VideoDecoder::GetFrameCount()
@@ -158,7 +159,7 @@ void VideoDecoder::WaitForAsyncFrameDecode()
 	//}
 }
 
-void VideoDecoder::UpdateFrame(RenderedFrame frame, bool sync, bool forRewind)
+void VideoDecoder::UpdateFrame(const RenderedFrame& frame, bool sync, bool forRewind)
 {
 	if(_emu->IsRunAheadFrame()) {
 		return;
@@ -166,7 +167,11 @@ void VideoDecoder::UpdateFrame(RenderedFrame frame, bool sync, bool forRewind)
 
 	_emu->OnBeforeSendFrame();
 
-	_frame = frame;
+	// RetroPlug: refilled in place rather than assigned, so the input snapshot goes through SetInputData's presized
+	// destinations (a port the control manager registered late is a new element there, not a fresh state buffer).
+	_frame.Reset(frame.FrameBuffer, frame.Width, frame.Height, frame.Scale, frame.FrameNumber, frame.VideoPhase);
+	_frame.Data = frame.Data;
+	_frame.SetInputData(frame.InputData);
 	DecodeFrame(forRewind);
 	_frameCount++;
 }
diff --git a/deps/mesen/Core/Shared/Video/VideoDecoder.h b/deps/mesen/Core/Shared/Video/VideoDecoder.h
index 44b9de01..0313c4f7 100644
--- a/deps/mesen/Core/Shared/Video/VideoDecoder.h
+++ b/deps/mesen/Core/Shared/Video/VideoDecoder.h
@@ -26,6 +26,9 @@ private:
 	FrameInfo _baseFrameSize = {};
 	FrameInfo _lastFrameSize = {};
 	RenderedFrame _frame = {};
+	// RetroPlug: the decoded frame DecodeFrame hands on, refilled per frame for the same reason _frame is kept (its
+	// input snapshot's storage survives from frame to frame).
+	RenderedFrame _convertedFrame = {};
 
 	VideoFilterType _videoFilterType = VideoFilterType::None;
 	unique_ptr<BaseVideoFilter> _videoFilter;
@@ -51,7 +54,7 @@ public:
 	FrameInfo GetFrameInfo();
 	double GetLastFrameScale() { return _frame.Scale; }
 
-	void UpdateFrame(RenderedFrame frame, bool sync, bool forRewind);
+	void UpdateFrame(const RenderedFrame& frame, bool sync, bool forRewind);
 
 	void WaitForAsyncFrameDecode();
 
diff --git a/deps/mesen/Core/Shared/Video/VideoRenderer.cpp b/deps/mesen/Core/Shared/Video/VideoRenderer.cpp
index 6db9f18c..a3845836 100644
--- a/deps/mesen/Core/Shared/Video/VideoRenderer.cpp
+++ b/deps/mesen/Core/Shared/Video/VideoRenderer.cpp
@@ -151,7 +151,10 @@ void VideoRenderer::UpdateFrame(RenderedFrame& frame)
 
 	{
 		auto lock = _frameLock.AcquireSafe();
-		_lastFrame = frame;
+		// RetroPlug: refilled in place rather than assigned, for the same reason the decoder's frame is.
+		_lastFrame.Reset(frame.FrameBuffer, frame.Width, frame.Height, frame.Scale, frame.FrameNumber, frame.VideoPhase);
+		_lastFrame.Data = frame.Data;
+		_lastFrame.SetInputData(frame.InputData);
 	}
 
 	if(_renderer) {
````

- [ ] **Step 5: Mesen: blip_reset, and the tap rings it serves**

`blip_buf` gains `blip_reset(m, size)`, which replays `blip_new`'s order exactly (the default factor, then
`blip_clear`, whose starting phase is `factor / 2`), and `blip_new` now calls it; `SetCaptureCapacity` creates all
five stream blips with the rings and an arm re-initialises one and then calls `blip_set_rates`, which is where a fresh
`blip_new` plus `blip_set_rates` at the arm would be. A disarm keeps the blips for the next arm. That is what makes
the arm allocation-free without moving the audio, which parity caught when the blips were rate-set at capacity time
instead:

````diff
diff --git a/deps/mesen/Core/NES/NesSoundMixer.cpp b/deps/mesen/Core/NES/NesSoundMixer.cpp
index 3259db9d..bf376fea 100644
--- a/deps/mesen/Core/NES/NesSoundMixer.cpp
+++ b/deps/mesen/Core/NES/NesSoundMixer.cpp
@@ -404,8 +404,7 @@ void NesSoundMixer::SetCaptureCapacity(uint32_t maxBlockFrames)
 		return;
 	}
 	// The rings and the resample scratch are allocated HERE, off the block thread, so an armed block's capture and
-	// drain never allocate. (Each stream's blip stays SetChannelCapture's: it is blip_new's, fixed-size, and created
-	// when a stream arms.)
+	// drain never allocate.
 	for(uint32_t k = 0; k < MaxCaptureStreams; k++) {
 		vector<int16_t> grown(want, 0);
 		uint32_t from = _streamHead[k];
@@ -419,6 +418,14 @@ void NesSoundMixer::SetCaptureCapacity(uint32_t maxBlockFrames)
 	_streamScratch.resize((size_t)MaxSamplesPerFrame * 2);
 	_streamOutScratch.resize((size_t)MaxSamplesPerFrame * 2);
 	_streamCapacity = want;
+	// The stream blips too: every stream the tap can arm into gets its fixed-size buffer here, so an arm (which runs
+	// on the block thread) re-initialises one with blip_reset instead of allocating. They are kept across a disarm
+	// and re-initialised by the next arm, which is what a fresh blip_new gave it anyway.
+	for(uint32_t k = 0; k < MaxCaptureStreams; k++) {
+		if(!_streamBlip[k]) {
+			_streamBlip[k] = blip_new(NesSoundMixer::MaxSamplesPerFrame);
+		}
+	}
 }
 
 void NesSoundMixer::SetChannelCapture(uint32_t mode, uint32_t hostRate, uint32_t maxBlockFrames)
@@ -427,12 +434,8 @@ void NesSoundMixer::SetChannelCapture(uint32_t mode, uint32_t hostRate, uint32_t
 	// Stream count per mode: 1 pins → 3, 2 pins+ref → 4, 3 individual-mono → 5, else off.
 	const uint32_t want = mode == 1 ? 3u : mode == 2 ? 4u : mode == 3 ? 5u : 0u;
 	_captureMode = want > 0 ? mode : 0u;
-	// Free any streams no longer wanted (e.g. re-arming with fewer streams).
+	// Drop any streams no longer wanted (e.g. re-arming with fewer streams). Their blips stay for the next arm.
 	for(uint32_t k = want; k < MaxCaptureStreams; k++) {
-		if(_streamBlip[k]) {
-			blip_delete(_streamBlip[k]);
-			_streamBlip[k] = nullptr;
-		}
 		_streamHead[k] = 0;
 		_streamCount[k] = 0;
 	}
@@ -440,10 +443,9 @@ void NesSoundMixer::SetChannelCapture(uint32_t mode, uint32_t hostRate, uint32_t
 	if(want > 0) {
 		_captureHostRate = hostRate;
 		for(uint32_t k = 0; k < want; k++) {
-			if(!_streamBlip[k]) {
-				_streamBlip[k] = blip_new(NesSoundMixer::MaxSamplesPerFrame);
-			}
-			blip_clear(_streamBlip[k]);
+			// What a fresh blip_new() would leave: the default rate, a clear that seeds the phase from it, then the
+			// real rates. The buffer itself was allocated with the rings (SetCaptureCapacity).
+			blip_reset(_streamBlip[k], NesSoundMixer::MaxSamplesPerFrame);
 			if(_clockRate > 0) {
 				blip_set_rates(_streamBlip[k], _clockRate, _sampleRate);
 			}
diff --git a/deps/mesen/Core/NES/NesSoundMixer.h b/deps/mesen/Core/NES/NesSoundMixer.h
index 44fb16e1..fb284900 100644
--- a/deps/mesen/Core/NES/NesSoundMixer.h
+++ b/deps/mesen/Core/NES/NesSoundMixer.h
@@ -127,8 +127,9 @@ public:
 	// RetroPlug per-channel capture (spec/10 §5/§5b). `mode` is the channelExportMode: 0 = off, 1 = pins,
 	// 2 = pins + reference (fidelity test), 3 = 5 individual core mono channels. `hostRate` is passed in
 	// (the caller's sample rate) so the mixer needn't reach into settings. `maxBlockFrames` is the largest host block
-	// the caller will drain; SetCaptureCapacity has already sized the rings and the resample scratch from it at
-	// activation, so arming only switches modes (a newly armed stream still creates its own blip).
+	// the caller will drain; SetCaptureCapacity has sized the rings, the resample scratch and every stream's blip from
+	// it at activation, so arming only switches modes and re-initialises the streams (blip_reset) - it allocates
+	// nothing, which matters because an arm can run on the audio thread.
 	// AvailableCaptureFrames reports host frames ready (all captured streams stay equal length); DrainChannel pops
 	// `frameCount` mono float samples from stream k (int16→float ÷32768, oldest first), like MesenAudioDevice::drain.
 	void SetCaptureCapacity(uint32_t maxBlockFrames);
diff --git a/deps/mesen/Utilities/Audio/blip_buf.cpp b/deps/mesen/Utilities/Audio/blip_buf.cpp
index 5f18db1e..796f39b6 100644
--- a/deps/mesen/Utilities/Audio/blip_buf.cpp
+++ b/deps/mesen/Utilities/Audio/blip_buf.cpp
@@ -110,6 +110,20 @@ static void check_assumptions( void )
 	assert( blip_max_frame <= (fixed_t) -1 >> time_bits );
 }
 
+/* RetroPlug: the state blip_new() gives a fresh buffer, applied in place, so a
+caller can re-use a buffer it already owns without allocating on the audio
+thread. The order is blip_new's and it matters: blip_clear() seeds the starting
+phase from the current factor, so the default rate has to be set first. */
+void blip_reset( blip_t* m, int size )
+{
+	assert( m != NULL );
+	assert( size >= 0 );
+
+	m->factor = time_unit / blip_max_ratio;
+	m->size   = size;
+	blip_clear( m );
+}
+
 blip_t* blip_new( int size )
 {
 	blip_t* m;
@@ -118,9 +132,7 @@ blip_t* blip_new( int size )
 	m = (blip_t*) malloc( sizeof *m + (size + buf_extra) * sizeof (buf_t) );
 	if ( m )
 	{
-		m->factor = time_unit / blip_max_ratio;
-		m->size   = size;
-		blip_clear( m );
+		blip_reset( m, size );
 		check_assumptions();
 	}
 	return m;
diff --git a/deps/mesen/Utilities/Audio/blip_buf.h b/deps/mesen/Utilities/Audio/blip_buf.h
index abdcc4d0..46806544 100644
--- a/deps/mesen/Utilities/Audio/blip_buf.h
+++ b/deps/mesen/Utilities/Audio/blip_buf.h
@@ -27,6 +27,12 @@ so that there are blip_max_ratio clocks per sample. Returns pointer to new
 buffer, or NULL if insufficient memory. */
 EXPORT blip_t* blip_new( int sample_count );
 
+/** Re-initialises an existing buffer in place, exactly as blip_new() leaves a
+fresh one: the default rate, then a clear (whose starting phase blip_clear
+derives from that rate). sample_count must be the size the buffer was created
+with. Lets a caller keep a buffer across uses without allocating. */
+EXPORT void blip_reset( blip_t*, int sample_count );
+
 /** Sets approximate input clock rate and output sample rate. For every
 clock_rate input clocks, approximately sample_rate samples are generated. */
 EXPORT void blip_set_rates( blip_t*, double clock_rate, double sample_rate );
````

- [ ] **Step 6: Our own: the N8 parser, SameBoy's scratches, the build request**

The N8 FIFO's `_params` is reserved at construction to the protocol's largest command frame (`3 + 0xFFFF`) and
`_writeBlock` to `ACK_BLOCK_SIZE`, so no command frame grows a buffer in a block; `execFileRead` and `performDma`
share one `_ioScratch` member, and `_rxQueue` is a vector-backed FIFO reserved to `FILE_READ_MAX + 1` instead of a
`std::queue` whose deque allocates its first node in a block. SameBoy sizes its two scratch sets from `blockFrames()`
in `reserveAudio` and at `onActivate`, so `prepareForBlock` only zeroes them (its lazy growth stays as the fallback
for a direct core at a bigger block than it reserved), and its dead `pendingMidi_` and `onMidi` override are gone, so
a MIDI-framed event to a Game Boy now reaches `SystemBase`'s no-op. `SystemBuildRequest` gains `maxBlockFrames` (a
node sets it from its plan's maximum in `activate`, before `build`; both backends reserve it before `onActivate`; a
build outside a plan leaves it 0), `MesenAudioDevice` gains the constructor that takes the initial block, and
`SystemBase::kDefaultBlockFrames` is the shared default a directly built core sizes for.
`docs/modular-graph-phase-b1-design.md`'s stale "`setChannelExportMode` still allocates" line is corrected:

````diff
diff --git a/docs/modular-graph-phase-b1-design.md b/docs/modular-graph-phase-b1-design.md
index 53c7974c..e947eb8d 100644
--- a/docs/modular-graph-phase-b1-design.md
+++ b/docs/modular-graph-phase-b1-design.md
@@ -175,7 +175,9 @@ build mode) is the one the table needs, so the two always agree by the time the
 **The narrow-host defect stays as it is** (B0 §2.5): a split that does not fit the host falls back to
 Stereo wiring, the `SyncTap` arms the tap regardless, as today, and the NES outputs its mix with the
 capture armed, so the mix drifts. Fixing the defect is its own commit with its re-record.
-`setChannelExportMode` still allocates on the audio thread (`blip_new`), as today.
+`setChannelExportMode` allocates nothing on the audio thread: a stream's blip is created with the tap's rings at
+activation, and an arm re-initialises it in place
+([C4](modular-graph-phase-c4-design.md) section 4).
 
 ### 3.3 `rp.dsp.kernel`
 
diff --git a/packages/native/src/host/graph/SystemNodes.cpp b/packages/native/src/host/graph/SystemNodes.cpp
index da05ef14..62b812b5 100644
--- a/packages/native/src/host/graph/SystemNodes.cpp
+++ b/packages/native/src/host/graph/SystemNodes.cpp
@@ -408,30 +408,34 @@ struct SystemNode {
 
     // The first activation builds the core the settings name, and gives it every knob the settings carry; later ones
     // (a re-activation) keep it. A core is built at the rate the plan runs at, which Engine::setSampleRate re-rates. A
-    // plan that is then refused destroys it as it unwinds. Either way the core learns the largest block it will run,
-    // which is how its audio ring and per-block scratches are sized before its first block: a plan that grows a host's
-    // block reactivates the whole graph quiescent, so the sizes move with no block in flight.
+    // plan that is then refused destroys it as it unwinds. Either way the core learns the largest block it will run
+    // before it runs one: a build is handed the plan's maximum, so the core sizes its audio ring and per-block
+    // scratches to it as it activates (a default-sized core would keep that memory for a small host block's whole
+    // life), and a plan that grows a host's block reactivates the whole graph quiescent, where reserveAudio moves the
+    // sizes with no block in flight.
     bool activate(double, std::uint32_t maxFrames) {
         if (core) {
             core->reserveAudio(maxFrames);
             return true;
         }
-        build(static_cast<Self*>(this)->config(*settings));
+        build(static_cast<Self*>(this)->config(*settings), maxFrames);
         core->reserveAudio(maxFrames);
         return true;
     }
 
-    // Build the core the settings name, through rp.host.systems/1, with the bytes this instance's resources pinned and
-    // the config its settings made: the backend applies the config before onActivate and boots and restores the seeds
-    // there, so a non-default model never restarts the core it just restored. A failure throws its reason, which
-    // rp::detail::activateFn reports and the planner shows as activate-failed on this node.
-    void build(SystemConfig config) {
+    // Build the core the settings name, through rp.host.systems/1, with the bytes this instance's resources pinned, the
+    // config its settings made and the plan's largest block: the backend applies the config and reserves that block
+    // before onActivate, and boots and restores the seeds there, so a non-default model never restarts the core it just
+    // restored. A failure throws its reason, which rp::detail::activateFn reports and the planner shows as
+    // activate-failed on this node.
+    void build(SystemConfig config, std::uint32_t maxBlockFrames) {
         if (!systems) throw std::runtime_error("the host offers no system builder (rp.host.systems/1)");
         SystemBuildRequest request;
         request.rom = bytesOf(settings->rom);
         request.sram = bytesOf(settings->sram);
         request.savestate = bytesOf(settings->state);
         request.romName = settings->romName.value_or(std::string());
+        request.maxBlockFrames = maxBlockFrames;
         request.config = std::move(config);
         rp_error err{};
         SystemBase* made = systems->build(systems->ctx, settings->system, std::move(request), &err);
diff --git a/packages/native/src/system/SystemBase.hpp b/packages/native/src/system/SystemBase.hpp
index bfd706e9..451a9528 100644
--- a/packages/native/src/system/SystemBase.hpp
+++ b/packages/native/src/system/SystemBase.hpp
@@ -89,6 +89,11 @@ public:
         if (maxBlockFrames > maxBlockFrames_) maxBlockFrames_ = maxBlockFrames;
     }
 
+    // The block a core built outside a plan (a test, a tool) sizes its per-block buffers for: the engine's default
+    // plan size, which a test holds this to (a system core cannot include the engine), so its buffers are already
+    // sized when no node has reserved for it.
+    static constexpr std::uint32_t kDefaultBlockFrames = 8192;
+
     // The output streams this system can emit. Default = one stereo "Mix" stream
     // (the mixed console output). A backend that can split its audio reports one
     // entry per stream; that layout only takes effect when the plan wires the
diff --git a/packages/native/src/system/SystemFactory.hpp b/packages/native/src/system/SystemFactory.hpp
index 80b8d25b..d0b321fd 100644
--- a/packages/native/src/system/SystemFactory.hpp
+++ b/packages/native/src/system/SystemFactory.hpp
@@ -28,6 +28,12 @@ struct SystemBuildRequest {
     // The ROM's file stem. A Master System or Game Gear core stages its ROM as a file and takes its battery stem from
     // that name, and now that its ROM is bytes it has nothing else to name the file by; empty keeps the core's "rom".
     std::string  romName;
+    // The largest block the plan this core is built for will run, in frames; 0 for a build outside a plan (a test, a
+    // tool), which then keeps the console's default. It is reserved before the core activates, so its audio ring and
+    // its per-block scratches are sized to the plan from the start rather than to the default and then grown
+    // (docs/modular-graph-phase-c4-design.md section 4). A host that grows its block later reserves again, through
+    // SystemBase::reserveAudio on the reactivation that is quiescent with respect to the core.
+    std::uint32_t maxBlockFrames = 0;
     SystemConfig config;
 };
 
diff --git a/packages/native/src/system/mesen/MesenAudioDevice.hpp b/packages/native/src/system/mesen/MesenAudioDevice.hpp
index 88d7c0b0..5102c3a9 100644
--- a/packages/native/src/system/mesen/MesenAudioDevice.hpp
+++ b/packages/native/src/system/mesen/MesenAudioDevice.hpp
@@ -16,9 +16,9 @@
 // The ring never allocates on the block thread. Its capacity is one host block plus the most a single PlayBuffer can
 // carry: Mesen's mixer resamples into a fixed 0x10000-int16 buffer and hands the device at most 0x10000 / 2 frames
 // per call (SoundMixer::PlayAudioBuffer), on every console and at every sample rate, and a step loop that gates on
-// `availableFrames() >= framesNeeded` stops with at most one such flush over a block. A system node sizes the ring
-// from the plan's maximum block before its core runs its first block, and again when a host grows its block, which
-// reactivates the graph while it is quiescent (reserveAudio).
+// `availableFrames() >= framesNeeded` stops with at most one such flush over a block. A core built for a plan reserves
+// the plan's maximum block before it activates, and reserves again when a host grows its block, which reactivates the
+// graph while it is quiescent (reserveAudio).
 class MesenAudioDevice final : public IAudioDevice {
 public:
     // The most frames Mesen's SoundMixer hands over in one PlayBuffer. It is the mixer's own output buffer, not a
@@ -31,7 +31,10 @@ public:
     // runs blocks the same size an engine starts at. A system node replaces it with the plan's maximum at activation.
     static constexpr std::uint32_t kDefaultBlockFrames = 8192;
 
-    MesenAudioDevice() { reserveAudio(kDefaultBlockFrames); }
+    // A core built outside a plan (a test, a tool) sizes its ring for the default block; one built for a plan passes
+    // the plan's maximum, so the ring is the plan's from the start rather than the default and then grown.
+    MesenAudioDevice() : MesenAudioDevice(kDefaultBlockFrames) {}
+    explicit MesenAudioDevice(std::uint32_t blockFrames) { reserveAudio(blockFrames); }
 
     // Size the ring for blocks of up to `maxBlockFrames`, keeping the frames it already holds in order. Grows only:
     // a plan's maximum only ever grows (Engine::setMaxBlockFrames), and keeping a larger ring costs nothing but the
diff --git a/packages/native/src/system/mesen/MesenBackend.cpp b/packages/native/src/system/mesen/MesenBackend.cpp
index 84c98083..3df9bd49 100644
--- a/packages/native/src/system/mesen/MesenBackend.cpp
+++ b/packages/native/src/system/mesen/MesenBackend.cpp
@@ -15,7 +15,12 @@ namespace {
 // readState/readSram see fresh bytes instead of the boot seed frozen forever. `T` is MesenNesSystem, MesenGbaSystem or
 // MesenSmsSystem.
 template <typename T>
-std::unique_ptr<SystemBase> bootMesen(std::unique_ptr<T> sys, double sampleRate, std::string* why) {
+std::unique_ptr<SystemBase> bootMesen(std::unique_ptr<T> sys, double sampleRate, std::uint32_t maxBlockFrames,
+                                      std::string* why) {
+    // Before onActivate: a node hands over the plan's largest block, so the core sizes its ring and its per-block
+    // scratches to it as it activates instead of taking the default and being grown before its first block. A build
+    // outside a plan leaves it 0, and the core keeps the console's default.
+    if (maxBlockFrames != 0) sys->reserveAudio(maxBlockFrames);
     sys->onActivate(sampleRate);
     if (!sys->activated()) {  // core rejected the ROM at LoadRom → fail the construct
         if (why) *why = "Mesen refused the ROM";
@@ -29,30 +34,32 @@ std::unique_ptr<SystemBase> bootMesen(std::unique_ptr<T> sys, double sampleRate,
 
 std::unique_ptr<SystemBase> MesenBackend::buildNes(SystemId id, MesenNesConfig cfg,
                                                    std::vector<std::uint8_t> romBytes, double sampleRate,
-                                                   std::string* why) {
+                                                   std::uint32_t maxBlockFrames, std::string* why) {
     // The platform's RomFormat gate confirms the bytes actually match the requested platform (a mislabelled ROM is
     // rejected before Mesen sees it). The node type that made the config is what picked this console.
     if (detectRomFormat(romBytes) != RomFormat::Nes) {
         if (why) *why = "the ROM is not a NES ROM";
         return nullptr;
     }
-    return bootMesen(std::make_unique<MesenNesSystem>(id, std::move(cfg), std::move(romBytes)), sampleRate, why);
+    return bootMesen(std::make_unique<MesenNesSystem>(id, std::move(cfg), std::move(romBytes)), sampleRate,
+                     maxBlockFrames, why);
 }
 
 std::unique_ptr<SystemBase> MesenBackend::buildGba(SystemId id, MesenGbaConfig cfg,
                                                    std::vector<std::uint8_t> romBytes, double sampleRate,
-                                                   std::string* why) {
+                                                   std::uint32_t maxBlockFrames, std::string* why) {
     if (detectRomFormat(romBytes) != RomFormat::Gba) {
         if (why) *why = "the ROM is not a GBA ROM";
         return nullptr;
     }
     // biosPath left empty → HLE boot ROM; skipBootScreen stays the default (true).
-    return bootMesen(std::make_unique<MesenGbaSystem>(id, std::move(cfg), std::move(romBytes)), sampleRate, why);
+    return bootMesen(std::make_unique<MesenGbaSystem>(id, std::move(cfg), std::move(romBytes)), sampleRate,
+                     maxBlockFrames, why);
 }
 
 std::unique_ptr<SystemBase> MesenBackend::buildSms(SystemId id, MesenSmsConfig cfg,
                                                    std::vector<std::uint8_t> romBytes, double sampleRate,
-                                                   std::string* why) {
+                                                   std::uint32_t maxBlockFrames, std::string* why) {
     // Deliberately NOT an exact-match gate like the two above. The Sega magic is not required by
     // any boot ROM, so headerless homebrew is legitimate and TS admits it on the strength of the
     // file extension (platform.ts). An == RomFormat::Sms gate here would accept such a ROM in the
@@ -64,29 +71,31 @@ std::unique_ptr<SystemBase> MesenBackend::buildSms(SystemId id, MesenSmsConfig c
         if (why) *why = "the ROM is not a Master System or Game Gear ROM";
         return nullptr;
     }
-    return bootMesen(std::make_unique<MesenSmsSystem>(id, std::move(cfg), std::move(romBytes)), sampleRate, why);
+    return bootMesen(std::make_unique<MesenSmsSystem>(id, std::move(cfg), std::move(romBytes)), sampleRate,
+                     maxBlockFrames, why);
 }
 
 std::unique_ptr<SystemBase> MesenBackend::build(SystemId id, SystemBuildRequest request,
                                                 double sampleRate, std::string* why) {
+    const std::uint32_t maxBlockFrames = request.maxBlockFrames;
     if (const auto* nes = std::get_if<MesenNesConfig>(&request.config)) {
         MesenNesConfig cfg = *nes;
         cfg.sram = std::move(request.sram);
         cfg.savestate = std::move(request.savestate);
-        return buildNes(id, std::move(cfg), std::move(request.rom), sampleRate, why);
+        return buildNes(id, std::move(cfg), std::move(request.rom), sampleRate, maxBlockFrames, why);
     }
     if (const auto* gba = std::get_if<MesenGbaConfig>(&request.config)) {
         MesenGbaConfig cfg = *gba;
         cfg.sram = std::move(request.sram);
         cfg.savestate = std::move(request.savestate);
-        return buildGba(id, std::move(cfg), std::move(request.rom), sampleRate, why);
+        return buildGba(id, std::move(cfg), std::move(request.rom), sampleRate, maxBlockFrames, why);
     }
     if (const auto* sms = std::get_if<MesenSmsConfig>(&request.config)) {
         MesenSmsConfig cfg = *sms;
         cfg.sram = std::move(request.sram);
         cfg.savestate = std::move(request.savestate);
         cfg.romName = std::move(request.romName);
-        return buildSms(id, std::move(cfg), std::move(request.rom), sampleRate, why);
+        return buildSms(id, std::move(cfg), std::move(request.rom), sampleRate, maxBlockFrames, why);
     }
     // The factory dispatched by the config, so this is a host bug rather than a caller's.
     if (why) *why = "Mesen does not build this console";
diff --git a/packages/native/src/system/mesen/MesenBackend.hpp b/packages/native/src/system/mesen/MesenBackend.hpp
index 4715d6d1..0d068aea 100644
--- a/packages/native/src/system/mesen/MesenBackend.hpp
+++ b/packages/native/src/system/mesen/MesenBackend.hpp
@@ -19,15 +19,15 @@ public:
 
 private:
     // One boot function per console, reached by build()'s config alternative and by nothing else: the ROM gate, then
-    // bootMesen (construct, onActivate, reject a refused ROM, enable the state snapshot). cfg and romBytes are moved
-    // into the core; a refusal's reason is in *why when it is given.
+    // bootMesen (construct, reserve the plan's block, onActivate, reject a refused ROM, enable the state snapshot). cfg
+    // and romBytes are moved into the core; a refusal's reason is in *why when it is given.
     static std::unique_ptr<SystemBase> buildNes(SystemId id, MesenNesConfig cfg,
                                                 std::vector<std::uint8_t> romBytes, double sampleRate,
-                                                std::string* why);
+                                                std::uint32_t maxBlockFrames, std::string* why);
     static std::unique_ptr<SystemBase> buildGba(SystemId id, MesenGbaConfig cfg,
                                                 std::vector<std::uint8_t> romBytes, double sampleRate,
-                                                std::string* why);
+                                                std::uint32_t maxBlockFrames, std::string* why);
     static std::unique_ptr<SystemBase> buildSms(SystemId id, MesenSmsConfig cfg,
                                                 std::vector<std::uint8_t> romBytes, double sampleRate,
-                                                std::string* why);
+                                                std::uint32_t maxBlockFrames, std::string* why);
 };
diff --git a/packages/native/src/system/mesen/MesenBoot.hpp b/packages/native/src/system/mesen/MesenBoot.hpp
index 3e7466c0..ff85ce54 100644
--- a/packages/native/src/system/mesen/MesenBoot.hpp
+++ b/packages/native/src/system/mesen/MesenBoot.hpp
@@ -47,6 +47,7 @@ inline MesenCore bootMesenCore(const char*                           tag,
                                const std::string&                    romPathForLog,
                                double                                sampleRate,
                                FrameBufferTriple*                    frames,
+                               std::uint32_t                         maxBlockFrames,
                                const std::function<void(Emulator&)>& configure) {
     // Mesen's home folder + message options are process-global; set them once, thread-safely, so
     // concurrent core construction on background render threads doesn't race (see MesenGlobalInit).
@@ -67,7 +68,8 @@ inline MesenCore bootMesenCore(const char*                           tag,
     audioCfg.SampleRate  = static_cast<uint32_t>(sampleRate);
     out.emu->GetSettings()->SetAudioConfig(audioCfg);
 
-    out.audio = std::make_shared<MesenAudioDevice>();
+    out.audio = std::make_shared<MesenAudioDevice>(
+        maxBlockFrames ? maxBlockFrames : MesenAudioDevice::kDefaultBlockFrames);
     out.emu->GetSoundMixer()->RegisterAudioDevice(out.audio.get());
 
     out.video = std::make_shared<MesenVideoDevice>();
diff --git a/packages/native/src/system/mesen/MesenGbaSystem.cpp b/packages/native/src/system/mesen/MesenGbaSystem.cpp
index 201e555f..8ab56a29 100644
--- a/packages/native/src/system/mesen/MesenGbaSystem.cpp
+++ b/packages/native/src/system/mesen/MesenGbaSystem.cpp
@@ -140,7 +140,7 @@ void MesenGbaSystem::onActivate(double sampleRate) {
     VirtualFile romFile(rom_.data(), rom_.size(),
                         config_.romPath.empty() ? std::string("rom.gba") : config_.romPath);
     MesenCore core = bootMesenCore("MesenGbaSystem", romFile, config_.romPath, sampleRate, &frames_,
-                                   [this](Emulator& emu) {
+                                   blockFrames(MesenAudioDevice::kDefaultBlockFrames), [this](Emulator& emu) {
                                        // Before LoadRom, which is what reads the boot ROM.
                                        if (!firmwareDir_.empty()) emu.SetFirmwareFolderOverride(firmwareDir_);
                                        configureGba(emu, config_.skipBootScreen);
@@ -154,9 +154,10 @@ void MesenGbaSystem::onActivate(double sampleRate) {
     // GbaSaveRam region rather than going through Mesen's BatteryManager.
     seedSramAndState(config_.sram, config_.savestate);
 
-    // The block scratch exists before the first block: a block's audio path may not allocate
-    // (docs/modular-graph-phase-c4-design.md section 4). A node that builds at another block size grows it through
-    // reserveAudio before that block runs.
+    // The block scratch exists before the first block, sized to the largest block this core will run: a block's audio
+    // path may not allocate (docs/modular-graph-phase-c4-design.md section 4). A build for a plan is handed that plan's
+    // maximum in its request, which reserved it before this ran; a build outside a plan keeps the default, and a host
+    // that grows its block later grows it through reserveAudio before that block runs.
     audioDevice_->reserveAudio(blockFrames(MesenAudioDevice::kDefaultBlockFrames));
     sizeBlockScratch();
 
diff --git a/packages/native/src/system/mesen/MesenNesSystem.cpp b/packages/native/src/system/mesen/MesenNesSystem.cpp
index faf693b8..a650c3ef 100644
--- a/packages/native/src/system/mesen/MesenNesSystem.cpp
+++ b/packages/native/src/system/mesen/MesenNesSystem.cpp
@@ -188,7 +188,7 @@ void MesenNesSystem::onActivate(double sampleRate) {
     VirtualFile romFile(rom_.data(), rom_.size(),
                         config_.romPath.empty() ? std::string("rom.nes") : config_.romPath);
     MesenCore core = bootMesenCore("MesenNesSystem", romFile, config_.romPath, sampleRate, &frames_,
-                                   [this](Emulator& emu) {
+                                   blockFrames(MesenAudioDevice::kDefaultBlockFrames), [this](Emulator& emu) {
                                        configureNes(emu, config_.region, config_.removeSpriteLimit,
                                                     config_.s5bNoise, config_.mmc5PhaseReset,
                                                     config_.expansionVolume);
@@ -273,9 +273,10 @@ void MesenNesSystem::onActivate(double sampleRate) {
     // into the live NesSaveRam region.
     seedSramAndState(config_.sram, config_.savestate);
 
-    // The per-block buffers exist before the first block: a block's audio path may not allocate
-    // (docs/modular-graph-phase-c4-design.md section 4). A node that builds at another block size grows them through
-    // reserveAudio before that block runs.
+    // The per-block buffers exist before the first block, sized to the largest block this core will run: a block's
+    // audio path may not allocate (docs/modular-graph-phase-c4-design.md section 4). A build for a plan is handed that
+    // plan's maximum in its request, which reserved it before this ran; a build outside a plan keeps the default, and a
+    // host that grows its block later grows these through reserveAudio before that block runs.
     if (audioDevice_) audioDevice_->reserveAudio(blockFrames(MesenAudioDevice::kDefaultBlockFrames));
     sizePerBlockBuffers();
 
@@ -391,11 +392,11 @@ void MesenNesSystem::setChannelExportMode(std::uint32_t mode) {
     config_.channelExportMode = mode;
     // Before onActivate there is no mixer yet — the new mode is recorded and onActivate arms from it.
     if (!nesMixer_) return;
-    // Re-arms (or, at mode 0, frees) the per-stream blip buffers + resamplers. The tap's rings and its resample scratch
-    // were sized at activation, so nothing here grows a buffer: only a newly armed stream's blip is allocated (a fixed
-    // size, not the block's). It may therefore run as the plan's trailer on the block thread. Any frames already
-    // captured under the OLD mode are dropped, which is what we want: the streams change shape, so a partial block of
-    // the previous layout would fan out to the wrong lanes.
+    // Re-arms (or, at mode 0, leaves idle) the per-stream blip buffers + resamplers. The tap's rings, its resample
+    // scratch and every stream's blip were created at activation, so nothing here allocates or frees a buffer: an arm
+    // re-initialises the streams it switches on (blip_reset). It may therefore run as the plan's trailer on the block
+    // thread, and so may a disarm. Any frames already captured under the OLD mode are dropped, which is what we want:
+    // the streams change shape, so a partial block of the previous layout would fan out to the wrong lanes.
     nesMixer_->SetChannelCapture(mode, static_cast<std::uint32_t>(sampleRate_),
                                  blockFrames(MesenAudioDevice::kDefaultBlockFrames));
     channelCapture_ = mode >= 1;
@@ -614,6 +615,12 @@ std::uint32_t MesenNesSystem::audioRingCapacity() const {
     return audioDevice_ ? audioDevice_->capacityFrames() : 0;
 }
 
+std::uint32_t MesenNesSystem::availableFrames() const {
+    // The metric stepIfBelowTarget gates on: the capture streams when the tap is armed, else the mix ring.
+    if (channelCapture_ && nesMixer_) return nesMixer_->AvailableCaptureFrames();
+    return audioDevice_ ? static_cast<std::uint32_t>(audioDevice_->availableFrames()) : 0;
+}
+
 std::uint64_t MesenNesSystem::audioRingOverflow() const {
     return audioDevice_ ? audioDevice_->overflowFrames() : 0;
 }
diff --git a/packages/native/src/system/mesen/MesenNesSystem.hpp b/packages/native/src/system/mesen/MesenNesSystem.hpp
index 2d4c2a6f..e467023e 100644
--- a/packages/native/src/system/mesen/MesenNesSystem.hpp
+++ b/packages/native/src/system/mesen/MesenNesSystem.hpp
@@ -119,10 +119,10 @@ public:
     // can reach the split modes without rebuilding the core the way setRegion does — a routing flip must
     // not reset a playing game. Same modes as MesenNesConfig::channelExportMode (0 = off/Mix, 1 = pins,
     // 3 = 5 individual mono); channelLayout() follows it. Value-guarded, and pre-onActivate it only records
-    // the mode (onActivate arms from config_). Sizes nothing on the block thread: the tap's rings, its resample
-    // scratch and the per-block scratches are sized by reserveAudio at activation, so an arm only switches modes and
-    // resets the streams. A newly armed stream still creates its own blip (Mesen's blip_new, a fixed ~50 KB), which is
-    // what a routing change costs on whichever thread the plan's trailer runs on.
+    // the mode (onActivate arms from config_). Allocates nothing on the block thread: the tap's rings, its resample
+    // scratch, each stream's blip and the per-block scratches are sized by reserveAudio at activation, so an arm only
+    // switches modes and re-initialises the streams (blip_reset puts a stream's blip back in the state a fresh one
+    // starts in). A routing change therefore costs nothing on whichever thread the plan's trailer runs on.
     void setChannelExportMode(std::uint32_t mode);
     /** The armed per-channel export mode (0 = off). Lets the Engine record what it found before arming,
      *  so it can restore a construct-time (CLI / render) value it did not set. */
@@ -136,6 +136,10 @@ public:
     // (0 unless a block ran bigger than the ring it was given, which activation prevents). The tap's own rings report
     // their drops the same way. For tests / introspection.
     std::uint32_t audioRingCapacity() const;
+    // Frames captured for the block in progress, before finishBlock drains them - the same metric stepIfBelowTarget
+    // gates on (the capture streams when the tap is armed, else the mix ring). For tests / introspection: it is what a
+    // whole-block case reads to hold that the step phase filled the block.
+    std::uint32_t availableFrames() const;
     std::uint64_t audioRingOverflow() const;
     std::uint64_t tapOverflow() const;
 
diff --git a/packages/native/src/system/mesen/MesenSmsSystem.cpp b/packages/native/src/system/mesen/MesenSmsSystem.cpp
index 1965b665..cbf545eb 100644
--- a/packages/native/src/system/mesen/MesenSmsSystem.cpp
+++ b/packages/native/src/system/mesen/MesenSmsSystem.cpp
@@ -289,7 +289,7 @@ void MesenSmsSystem::onActivate(double sampleRate) {
 
     VirtualFile romFile(stagedRomPath_);
     MesenCore core = bootMesenCore("MesenSmsSystem", romFile, stagedRomPath_, sampleRate, &frames_,
-                                   [this](Emulator& emu) { configureSms(emu, config_.gameGear, config_.enableFm); });
+                                   blockFrames(MesenAudioDevice::kDefaultBlockFrames), [this](Emulator& emu) { configureSms(emu, config_.gameGear, config_.enableFm); });
     if (!core) return;
     emu_         = std::move(core.emu);
     audioDevice_ = std::move(core.audio);
@@ -307,9 +307,10 @@ void MesenSmsSystem::onActivate(double sampleRate) {
     // SmsCartRam region rather than going through Mesen's BatteryManager.
     seedSramAndState(config_.sram, config_.savestate);
 
-    // The block scratch exists before the first block: a block's audio path may not allocate
-    // (docs/modular-graph-phase-c4-design.md section 4). A node that builds at another block size grows it through
-    // reserveAudio before that block runs.
+    // The block scratch exists before the first block, sized to the largest block this core will run: a block's audio
+    // path may not allocate (docs/modular-graph-phase-c4-design.md section 4). A build for a plan is handed that plan's
+    // maximum in its request, which reserved it before this ran; a build outside a plan keeps the default, and a host
+    // that grows its block later grows it through reserveAudio before that block runs.
     audioDevice_->reserveAudio(blockFrames(MesenAudioDevice::kDefaultBlockFrames));
     sizeBlockScratch();
 
diff --git a/packages/native/src/system/mesen/NesEverdriveFifo.hpp b/packages/native/src/system/mesen/NesEverdriveFifo.hpp
index b96ad871..62b4b355 100644
--- a/packages/native/src/system/mesen/NesEverdriveFifo.hpp
+++ b/packages/native/src/system/mesen/NesEverdriveFifo.hpp
@@ -100,6 +100,16 @@ namespace rp {
 	// the ROM answers with up to this many payload bytes, and so on until the length is met.
 	static constexpr uint32_t ACK_BLOCK_SIZE = 1024;
 
+	// The most data the SDK's file read asks for in one CMD_F_FRD ("we can read up to 4096 in a single block, but
+	// not recommended, to avoid fifo overload"), so a reply buffer is given room for it and its response byte.
+	static constexpr uint32_t FILE_READ_MAX = 4096;
+
+	// The largest command frame the parser can be asked to collect: a parameter block that is a
+	// u16-length-prefixed payload (CMD_USB_WR / CMD_FIFO_WR / CMD_UART_WR) or string (the file commands),
+	// plus its header. The parser's frame buffer is reserved to this once, so collecting a command never
+	// grows it while a block runs (docs/modular-graph-phase-c4-design.md section 4).
+	static constexpr uint32_t MAX_COMMAND_PARAMS = 3 + 0xFFFF;
+
 	// -----------------------------------------------------------------------
 	// $40FF (REG_MSTAT) - the mapper status register the DMA handshake polls
 	// -----------------------------------------------------------------------
@@ -177,7 +187,36 @@ namespace rp {
 	class NesEverdriveFifo : public INesMemoryHandler {
 	private:
 		// ----- RX queue (emulator → NES) --------------------------------
-		std::queue<uint8_t> _rxQueue;
+		// fifo_a's delivered bytes, oldest first. A vector read from a moving head rather than a std::queue, whose
+		// deque allocates a node on its first push: an Edio reply is pushed WHOLE before the ROM reads a byte of it (a
+		// file read is up to 4 KB), so a block that answers an SD command would allocate inside the block. The vector
+		// keeps its capacity, and what has been read is dropped once the head has moved past half of it, so it stays
+		// bounded by the largest reply rather than by every byte ever delivered.
+		struct DeliveredBytes {
+			std::vector<uint8_t> bytes;
+			std::size_t          head = 0;
+
+			std::size_t size() const { return bytes.size() - head; }
+			bool empty() const { return head == bytes.size(); }
+			uint8_t front() const { return bytes[head]; }
+			void push(uint8_t byte) { bytes.push_back(byte); }
+			void pop() {
+				++head;
+				if (head == bytes.size()) {
+					bytes.clear();
+					head = 0;
+				} else if (head * 2 >= bytes.size()) {
+					bytes.erase(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(head));
+					head = 0;
+				}
+			}
+			void clear() {
+				bytes.clear();
+				head = 0;
+			}
+		};
+
+		DeliveredBytes _rxQueue;
 		std::mutex _mutex;
 
 		// What fifo_a's RAM holds, by slot. On the device the FIFO is a ring over 2048 bytes of block
@@ -215,6 +254,11 @@ namespace rp {
 		std::vector<uint8_t> _params;
 		size_t      _paramBytesNeeded = 0;
 
+		// One block of file data, reused by the two paths that read the open file (a CMD_F_FRD reply and a
+		// CMD_F_FRD_MEM transfer). Each writes the whole length it asks for before reading any of it, so what the
+		// buffer held does not matter. It grows to the largest read a ROM has asked for and keeps that.
+		std::vector<uint8_t> _ioScratch;
+
 		// CMD_F_FWR block state: bytes still owed for the whole write, and the block being collected.
 		// The payload arrives OUTSIDE the command frame (the ROM interleaves it with our ack bytes), so
 		// it needs a parser state of its own rather than a parameter continuation.
@@ -283,6 +327,16 @@ namespace rp {
 		uint64_t _dmaDoneAtCycle = 0;
 
 	public:
+		NesEverdriveFifo() {
+			// The parser's and fifo_a's buffers live as long as the FIFO; without this they grow (push_back / reserve)
+			// the first time a block carries a bigger command frame, file block or reply than any before it, which the
+			// block thread may not do. A file read's reply is one resp byte plus up to the 4096 the SDK reads per
+			// command, so that is what a reply buffer is given; a larger one (a big directory listing) still grows it.
+			_params.reserve(MAX_COMMAND_PARAMS);
+			_writeBlock.reserve(ACK_BLOCK_SIZE);
+			_rxQueue.bytes.reserve(FILE_READ_MAX + 1);
+		}
+
 		// Set the host directory that stands in for the SD card root ("/"). Must be set before the ROM
 		// runs any SD command; MesenNesSystem does it at activate from the "mesen" role's `sdRoot`.
 		//
@@ -484,8 +538,7 @@ namespace rp {
 		// re-pointed.
 		void clearRx() {
 			std::lock_guard<std::mutex> lock(_mutex);
-			std::queue<uint8_t> empty;
-			_rxQueue.swap(empty);
+			_rxQueue.clear();
 			std::queue<uint8_t> emptyWire;
 			_wireQueue.swap(emptyWire);
 			_wireCredit = 0.0;
@@ -840,6 +893,12 @@ namespace rp {
 			for (uint8_t c : s) pushRx(c);
 		}
 
+		// Room for `len` bytes of file data, kept between reads (see _ioScratch). Caller writes all of it.
+		std::uint8_t* ioScratch(uint32_t len) {
+			if (_ioScratch.size() < len) _ioScratch.resize(len);
+			return _ioScratch.data();
+		}
+
 		// Read a length-prefixed string out of _params at a given offset.
 		// Returns the string and advances offset past it.
 		std::string readParamString(size_t offset) {
@@ -1041,12 +1100,12 @@ namespace rp {
 				return;
 			}
 
-			std::vector<uint8_t> buf(len, 0x00);
+			std::uint8_t* const buf = ioScratch(len);
 			uint32_t got = 0;
 			if (_openFile.is_open()) {
 				_openFile.clear();  // a prior read to EOF leaves eofbit set, which fails the next seek
 				_openFile.seekg(_filePtr);
-				_openFile.read(reinterpret_cast<char*>(buf.data()), len);
+				_openFile.read(reinterpret_cast<char*>(buf), len);
 				got = static_cast<uint32_t>(_openFile.gcount());
 				_filePtr += got;
 			}
@@ -1117,10 +1176,10 @@ namespace rp {
 			if (!_cartWriter) return 0x13;          // no backing memory at all
 			if (!_openFile.is_open()) return 0x04;  // FAT_NO_FILE
 
-			std::vector<uint8_t> buf(len, 0x00);
+			std::uint8_t* const buf = ioScratch(len);
 			_openFile.clear();  // a prior read to EOF leaves eofbit set, which fails the next seek
 			_openFile.seekg(_filePtr);
-			_openFile.read(reinterpret_cast<char*>(buf.data()), len);
+			_openFile.read(reinterpret_cast<char*>(buf), len);
 			const uint32_t got = static_cast<uint32_t>(_openFile.gcount());
 			// Already at EOF is an ERROR, not a success that moved nothing, by the same reasoning
 			// as execFileRead: a ROM told "fine" about an empty transfer reads stale pixels forever.
@@ -1128,7 +1187,7 @@ namespace rp {
 
 			// The writer validates the whole range before writing a byte, so a refusal leaves both
 			// cartridge memory and the file pointer untouched.
-			if (!_cartWriter(piAddr, buf.data(), got)) return 0x13;
+			if (!_cartWriter(piAddr, buf, got)) return 0x13;
 
 			_filePtr += got;
 			return 0;
@@ -1148,7 +1207,6 @@ namespace rp {
 
 			_writeRemaining = len;
 			_writeBlock.clear();
-			_writeBlock.reserve(std::min(len, ACK_BLOCK_SIZE));
 			pushRx(0x00);  // ack the first block; collectWriteByte takes it from here
 			_parseState = ParseState::CollectWriteData;
 		}
diff --git a/packages/native/src/system/sameboy/SameBoyBackend.cpp b/packages/native/src/system/sameboy/SameBoyBackend.cpp
index 71edadad..706fc622 100644
--- a/packages/native/src/system/sameboy/SameBoyBackend.cpp
+++ b/packages/native/src/system/sameboy/SameBoyBackend.cpp
@@ -24,6 +24,9 @@ std::unique_ptr<SystemBase> SameBoyBackend::build(SystemId id, SystemBuildReques
     typed.sram = std::move(request.sram);
     typed.savestate = std::move(request.savestate);
     auto sys = std::make_unique<SameBoySystem>(id, std::move(typed), std::move(request.rom));
+    // Before onActivate: a node hands over the plan's largest block, so the per-block scratches are sized to it as the
+    // core activates. A build outside a plan leaves it 0, and the core keeps the default block.
+    if (request.maxBlockFrames != 0) sys->reserveAudio(request.maxBlockFrames);
     sys->onActivate(sampleRate);       // boots gb_ + restores cfg.sram then cfg.savestate
     sys->enableStateSnapshot();        // publish a tear-free savestate each block so Duplicate can clone
                                        // off the snapshot while the audio thread runs (getFrame's analogue)
diff --git a/packages/native/src/system/sameboy/SameBoySystem.cpp b/packages/native/src/system/sameboy/SameBoySystem.cpp
index 5a829f56..1a80110b 100644
--- a/packages/native/src/system/sameboy/SameBoySystem.cpp
+++ b/packages/native/src/system/sameboy/SameBoySystem.cpp
@@ -290,6 +290,12 @@ void SameBoySystem::onActivate(double sampleRate) {
     serialOutByte_ = 0;
     serialOutBits_ = 0;
 
+    // The per-block scratches exist before the first block, sized to the largest block this core will run: a block's
+    // audio path may not allocate. A build for a plan is handed that plan's maximum in its request, which reserved it
+    // before this ran; a build outside a plan keeps the default, and a host that grows its block later grows them
+    // through reserveAudio before that block runs.
+    sizePerBlockBuffers();
+
     activated_ = true;
 }
 
@@ -495,11 +501,6 @@ bool SameBoySystem::nextSerialInBit() {
     return bit;
 }
 
-void SameBoySystem::onMidi(const ::MidiEvent* events, std::uint32_t count) {
-    if (events == nullptr || count == 0) return;
-    pendingMidi_.insert(pendingMidi_.end(), events, events + count);
-}
-
 void SameBoySystem::setModel(SameBoyModel model) {
     if (config_.model == model) return;
     config_.model = model;
@@ -683,6 +684,22 @@ void SameBoySystem::prepareForBlock(const AudioBlockInfo& info) {
     serialOutLog_.clear();
 }
 
+void SameBoySystem::reserveAudio(std::uint32_t maxBlockFrames) {
+    SystemBase::reserveAudio(maxBlockFrames);
+    sizePerBlockBuffers();
+}
+
+// The two scratch sets prepareForBlock zeroes, each one block of the largest block this core will run, plus the
+// headroom writeAudioSample leaves for the at most one sample a block can overproduce. Sized at activation and on a
+// larger reserve, never in a block. Growth only, so a later smaller reserve keeps the memory (as prepareForBlock's
+// own lazily-grown sizing did).
+void SameBoySystem::sizePerBlockBuffers() {
+    const std::size_t want = std::size_t(blockFrames(kDefaultBlockFrames)) * 2 + 16;
+    if (stereoAccum_.size() < want) stereoAccum_.assign(want, 0.0f);
+    for (std::vector<float>& acc : chanAccum_)
+        if (acc.size() < want) acc.assign(want, 0.0f);
+}
+
 bool SameBoySystem::stepIfBelowTarget(std::uint32_t framesNeeded) {
     if (!activated_ || !gb_) return false;
     if (audioFrameCount_ >= framesNeeded) return false;
@@ -768,9 +785,6 @@ void SameBoySystem::finishBlock(const AudioBlockInfo& info, float* const* outs,
 
     audioFrameCount_ = 0;
 
-    // Clear this block's MIDI so the next block starts empty.
-    pendingMidi_.clear();
-
     // Publish the whole-savestate snapshot the registry reads, on its coarse interval (the very next
     // block after enableStateSnapshot or rearmStateSnapshot, which push the accumulator past the
     // threshold). Done AFTER role processing so a kit patch applied this block is in the blob.
diff --git a/packages/native/src/system/sameboy/SameBoySystem.hpp b/packages/native/src/system/sameboy/SameBoySystem.hpp
index ee7bd09c..ccbe6927 100644
--- a/packages/native/src/system/sameboy/SameBoySystem.hpp
+++ b/packages/native/src/system/sameboy/SameBoySystem.hpp
@@ -47,11 +47,6 @@ public:
     // serial-end callback in standalone mode.
     bool nextSerialInBit();
 
-    // Append the routed events to `pendingMidi_` (cleared at the top of each
-    // onProcess). Consumers that care about timing read MidiEvent::frame from
-    // the events themselves.
-    void onMidi(const ::MidiEvent* events, std::uint32_t count) override;
-
     // Queue a button transition. Called from DSP-thread command-drain at the
     // top of each block. The byte is reinterpreted as GameboyButton; pending
     // transitions are spread across the block in applyPending() (port of old
@@ -88,6 +83,10 @@ public:
     // interleaves and serial bits ferry mid-block; an unlinked system is the
     // 1-member case of the same triad, driven by its own node.
     ExpSmoother& gainSmoother() noexcept { return gainSmoother_; }
+    // Sizes the per-block accumulators for the largest block a node reserved for, so a block only ever zeroes them;
+    // a block that had to grow one would allocate on the block thread (docs/modular-graph-phase-c4-design.md
+    // section 4). Called at activation and when a host grows its block, never inside a block.
+    void reserveAudio(std::uint32_t maxBlockFrames) override;
     void prepareForBlock(const AudioBlockInfo& info) override;
     bool stepIfBelowTarget(std::uint32_t framesNeeded) override;
     void finishBlock(const AudioBlockInfo& info, float* const* outs, std::size_t laneCount) override;
@@ -183,8 +182,8 @@ public:
     GB_gameboy_t_fwd*         gb_ = nullptr;
     FrameBufferTriple         frames_{sameboy::kPixelWidth, sameboy::kPixelHeight};
 
-    // Stereo accumulator, sized to 2 * maxBlockSize. Resized lazily in
-    // onProcess if a host hands us a larger block than we've seen.
+    // Stereo accumulator, sized to 2 * the plan's block at activation (or the default outside a plan) and on a larger
+    // reserve (sizePerBlockBuffers); prepareForBlock still grows it for a block bigger than any reserve asked for.
     std::vector<float>        stereoAccum_;
     std::uint32_t             audioFrameCount_ = 0;
 
@@ -220,10 +219,6 @@ public:
     std::vector<SameBoySystem*> linkPeers_;
     bool                        bitToSend_ = false;
 
-    // MIDI events that landed on this system in the current block. Cleared
-    // at the top of onProcess.
-    std::vector<::MidiEvent> pendingMidi_;
-
     // Bytes queued by roles / host MIDI-in for the GB serial port, each tagged
     // with the sample offset it should not be delivered before. Drained one bit
     // at a time (MSB first) by `nextSerialInBit()` from the SameBoy serial-end
@@ -254,5 +249,8 @@ public:
     std::vector<std::pair<std::uint32_t, std::uint8_t>> serialOutLog_;
 
 private:
+    // Size stereoAccum_ and chanAccum_ for blockFrames(), keeping what they hold (growth only).
+    void sizePerBlockBuffers();
+
     ExpSmoother gainSmoother_;
 };
````

- [ ] **Step 7: Run the tests to see them pass, and parity unchanged**

Run: `cmake --build build --target retroplug-engine-graph-test -j$(nproc) && build/bin/retroplug-engine-graph-test "[alloc],[video],[control-state],[notification]"`
Expected: every selected case passes. The prototype's whole binary printed `27000 assertions / 207 test cases` in the plugin run. The `[video]` hashes are the ones recorded at this task's parent commit and equal here: NES 14252490387203307000 three times, SMS 16816052646246439624, 16816052646246439624, 668230188373870873, Game Boy 8259550387572866035, 10074299620259861403, 13782190143996438851, GBA 3100562949282106931, 3100562949282106931, 2667698829920294451.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources build/bin/retroplug-engine-graph-test "[alloc],[video]"`
Expected: the same cases with the GBA video section running; the prototype's whole binary printed `27060 / 207` with the resources tree, so report this selection's line.

Run: `./build.sh`
Expected: the prototype's build ended at `[100%] Built target retroplug-clap` (which target is last depends on the build order); at most the pre-existing warnings: three `-Wstringop-overflow` in `pressButton`'s `push_back` (NES, SMS, GBA) and the pre-existing `-Wformat-truncation` in `PluginDSP.cpp:165`, no new ones.

Run: `pnpm test:plugin`
Expected: `10 plugin test binary(ies) passed`: the engine-graph binary's `27000 assertions / 207 test cases`, the graph binary's 2174/198, the audio binary's 1233719/101, the other seven at their 33d39f39 counts.

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=16)`, 0 skipped, nothing re-recorded.

Run: `pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=16)`, with `# 46 case(s) SKIPPED in 10 file(s)` and the baseline's distribution.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native`
Expected: `# 158 native test file(s) passed`, with `# 2 case(s) SKIPPED in 2 file(s): dsp-bench(1), nes-bench(1)`.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:ui`
Expected: `# 39 UI test file(s) passed`

Run: `pnpm typecheck && pnpm test && pnpm check:docs`
Expected: `# 168 test file(s) passed`, then `check-doc-links: 69 file(s) clean` (the prototype's 68 plus this plan).

- [ ] **Step 8: The sanitizers, on this task's tags**

Run: `cmake --build build-tsan --target retroplug-engine-graph-test retroplug-graph-test -j$(nproc) && TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-engine-graph-test "[alloc]"`
Expected: every selected case passes with no ThreadSanitizer report; the report prints `326 assertions / 5 passed + 1 skipped` (the live-arm case skips where `countsCAllocations()` is false, which is every sanitizer build).

Run: `TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" RETROPLUG_RESOURCES_DIR=/workspaces/resources build-tsan/bin/retroplug-engine-graph-test "[control-state],[notification],[video]"`
Expected: `43 / 3`, no report (with the fixtures present, so the GBA video section runs).

Run: `cmake --build build-asan --target retroplug-engine-graph-test retroplug-graph-test -j$(nproc) && ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-engine-graph-test "[alloc]"`
Expected: the same selection passes with no AddressSanitizer or LeakSanitizer report; the report prints `326 / 5 passed + 1 skipped` there too, with leak detection on.

Run: `ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" RETROPLUG_RESOURCES_DIR=/workspaces/resources build-asan/bin/retroplug-engine-graph-test "[control-state],[notification],[video]"`
Expected: the three cases pass, no report (the pre-existing GBA boot-ROM leak lives in `retroplug-audio-test`, not here).

- [ ] **Step 9: Commit**

````bash
git add -- packages/native/test/engine-graph/MesenShared.test.cpp deps/mesen/Core/GBA/GbaPpu.cpp \
  deps/mesen/Core/GBA/GbaPpu.h deps/mesen/Core/NES/BaseNesPpu.h deps/mesen/Core/NES/NesPpu.cpp \
  deps/mesen/Core/NES/NesSoundMixer.cpp deps/mesen/Core/NES/NesSoundMixer.h deps/mesen/Core/SMS/SmsVdp.cpp \
  deps/mesen/Core/SMS/SmsVdp.h deps/mesen/Core/Shared/BaseControlDevice.cpp \
  deps/mesen/Core/Shared/BaseControlDevice.h deps/mesen/Core/Shared/BaseControlManager.cpp \
  deps/mesen/Core/Shared/BaseControlManager.h deps/mesen/Core/Shared/ControlDeviceState.h \
  deps/mesen/Core/Shared/NotificationManager.cpp deps/mesen/Core/Shared/RenderedFrame.h \
  deps/mesen/Core/Shared/Video/VideoDecoder.cpp deps/mesen/Core/Shared/Video/VideoDecoder.h \
  deps/mesen/Core/Shared/Video/VideoRenderer.cpp deps/mesen/Utilities/Audio/blip_buf.cpp \
  deps/mesen/Utilities/Audio/blip_buf.h docs/modular-graph-phase-b1-design.md packages/native/cmake/tests.cmake \
  packages/native/src/host/graph/SystemNodes.cpp packages/native/src/system/SystemBase.hpp \
  packages/native/src/system/SystemFactory.hpp packages/native/src/system/mesen/MesenAudioDevice.hpp \
  packages/native/src/system/mesen/MesenBackend.cpp packages/native/src/system/mesen/MesenBackend.hpp \
  packages/native/src/system/mesen/MesenBoot.hpp packages/native/src/system/mesen/MesenGbaSystem.cpp \
  packages/native/src/system/mesen/MesenNesSystem.cpp packages/native/src/system/mesen/MesenNesSystem.hpp \
  packages/native/src/system/mesen/MesenSmsSystem.cpp packages/native/src/system/mesen/NesEverdriveFifo.hpp \
  packages/native/src/system/sameboy/SameBoyBackend.cpp packages/native/src/system/sameboy/SameBoySystem.cpp \
  packages/native/src/system/sameboy/SameBoySystem.hpp packages/native/test/engine-graph/NodeRig.hpp \
  packages/native/test/engine-graph/SystemNode.test.cpp packages/native/test/graph/CountingAllocator.cpp \
  packages/native/test/graph/CountingAllocator.hpp
git commit -F - <<'EOF'
fix(mesen): a frame's video and input plumbing reuses its buffers, so a whole block allocates nothing

A Mesen console allocated about 18 times per EMULATED frame on the audio thread (738 in a 4 x 8192-frame window, the
same for a NES and an SMS), all of it its own frame plumbing: the port snapshot it builds for every video frame, the
controller states copied into it, the input data the frame carries through VideoDecoder, and the notification
dispatch. The previous change removed the audio path's own allocations and recorded these; a block with emulation
in it could never be free until they went too.

Mesen, in-tree, each buffer now keeps what it holds instead of being rebuilt per frame:

- BaseControlManager::GetPortStates fills the manager's own vector and returns it by reference, and the new
  BaseControlDevice::GetRawState(ControlDeviceState&) fills the caller's state, so neither the snapshot nor a
  device's state is copied into a fresh vector. Each snapshot entry's state is sized before it is filled and padded
  after, to the largest state a device can produce, so a device no ROM has read yet (or a mapper or role device
  registered late) cannot be what grows this vector or any copy of it inside a block.
- RenderedFrame gains Reset and SetInputData, and the NES, SMS and GBA PPUs refill one frame member instead of
  constructing a frame (whose InputData copy allocated twice per frame, once for the by-value parameter and once for
  the member). VideoDecoder::UpdateFrame takes its frame by const reference and refills its decoded frame in place,
  and VideoRenderer refills its last frame the same way. Nothing a frame produces changes: the same frame, the same
  input snapshot, in the same order.
- NotificationManager reuses the copy it iterates (it must be a copy: a listener may unregister while the notification
  runs) per thread and per nesting depth, so a nested notification still gets a buffer of its own. Each buffer is held
  behind its own pointer, so growing the pool for a deeper nesting cannot move a buffer a send is iterating.
- BaseControlDevice::EnsureCapacity grows the state to the maximum a read can ask for in one step, and ClearState
  clears it in place instead of replacing it with a fresh empty one: the vector keeps its capacity, so a poll no
  longer frees the state's buffer and the read that follows grows it back for free. The state stays EMPTY, which is
  the contract four devices read (a controller hub builds its composite state by appending to what a clear leaves; the
  barcode readers and the data recorder treat "no state" as a tape that is not playing, and a cleared 64-byte state
  made every poll of the Epoch Barcode Battler throw). ClearState also reserves that maximum at a device's first poll,
  so a ROM that does not read its controller for thousands of frames does not pay for it inside a block.

Two allocations a block turned out to carry are ours, and are fixed here too: the N8 FIFO's command parser grew its
frame buffer (and a CMD_F_FWR block's) on the block thread the first time a frame carried a larger one, so both are
reserved when the FIFO is created; its SD path's file reads now reuse one scratch instead of building a vector per
command, and fifo_a's delivered bytes live in a vector-backed queue (a reply is pushed whole before the ROM reads it),
not a std::queue whose deque allocates its first node inside a block. SameBoy's per-block accumulators are sized by
reserveAudio instead of growing inside a block, with its dead pendingMidi_ member deleted. And a core built for a plan
is handed the plan's largest block in its build request, so its ring and scratches are sized to that plan as it
activates rather than to the 8192 default and then grown; a core built outside a plan keeps the default.

The tap's arm allocates nothing either now. blip_buf gains blip_reset, which re-initialises a buffer exactly as
blip_new leaves a fresh one - the default factor first, then blip_clear, whose starting phase is derived from that
factor - so a stream's blip is created with the tap's rings at activation and re-initialised on each arm instead of
malloc'd. Without that order the arm's blips started at a different fractional phase and moved parity.

The suite's CountingAllocator now counts the C library's allocations too (malloc, calloc, realloc, free), which is
what sees a blip's block, on request per window: a window measuring the script host would otherwise count QuickJS's
own allocations as native ones. It is POSIX/glibc only - a sanitizer build owns the allocator and must see every
block, and Windows links the static CRT - so the arm case, whose whole promise is a C allocation, SKIPS where
countsCAllocations() is false instead of passing at the parent.

The new cases hold a whole real block - prepare, step, finish, with video frames, controller polls and notifications
inside the window - at ragged sizes for a warmed NES (mixed, the tap armed, and through the Engine), SMS, the Game
Boy (mixed and the four stem pairs) and the GBA, at zero allocations and zero frees; each counted window is proven
filled by emulation, and the Engine window by work RAM that moved. The failure was about 18 allocations per emulated
frame, and 15 for the one-time ripples of Mesen's first input poll. Three more tests hold what the plumbing must keep:
a cleared controller hub or barcode reader still has an empty state and a hub's composite state round-trips; a
notification's copy survives a listener unregistering and a nested send; and a node sizes its core to the plan it was
built by, 64 frames included. A video case hashes three frames per console, recorded at the parent commit and equal
here, so the video path is a check rather than an argument.
EOF
````

### Task 4: the tap is a capability, and the node's build stops casting

Design section 5, P5 and P6. `Engine` kept a NES core's address in every plan slot purely so a split routing could
arm its per-channel tap, found with `dynamic_cast` in `Engine::buildSystem` and in `publishGraph`'s pre-fill, and
`SystemNodes.cpp`'s build `dynamic_cast`ed the core `rp.host.systems/1` handed back to its own type. Both go. What is
left is a capability seam the engine branches on, as it already did for the debugger: `SystemBase::channelTap()` returns
a `ChannelTap*` (`exportMode()`, `setExportMode(mode)`, a virtual destructor), null by default; the NES's tap is the
core itself, so `MesenNesSystem` implements the interface over the accessors it already had; `SystemTable`'s slot holds
`{ id, core, tap }`; and `ArmedTap` remembers and compares the tap by address. A node's build compares the core's
`kind()` with the class's `Core::kKind` and then `static_cast`s, refusing with the same message and the same
destroy-first order on a mismatch. The engine no longer includes `MesenNesSystem.hpp`.

**Files:** 15 files, by step below.
- Create: `packages/native/src/system/ChannelTap.hpp`
- Modify: 14 files

- [ ] **Step 1: Write the failing cases**

`NesSplitRouting.test.cpp`'s nine `dynamic_cast`s (and its console include) become one local `placed(eng, id)` helper
returning `{ SystemBase*, SystemBase::channelTap() }`; the cases read `tap->exportMode()`, `core->channelLayout()`
(already a base virtual) and, at the one place that knocked the tap off behind the engine's back,
`tap->setExportMode(2)`. `BuildGraphGoldens.test.cpp`'s "each NES ends in the mode it was built in" reads the tap and
fails on a NES slot that offers none instead of skipping the check. `ScriptPacks.test.cpp`'s reload re-arm case reads
`channelTap()` rather than casting. `NodeRig.hpp` gains the `reportKind` seam: a `KindWrapper` that owns the real core
and reports another kind, so a refused build frees the core and the rig stays truthful. `SystemNode.test.cpp` gains
the capability case and the 12-way kind matrix, which also pins kind uniqueness:

````diff
diff --git a/packages/native/test/audio/NesSplitRouting.test.cpp b/packages/native/test/audio/NesSplitRouting.test.cpp
index 115c8188..a04987aa 100644
--- a/packages/native/test/audio/NesSplitRouting.test.cpp
+++ b/packages/native/test/audio/NesSplitRouting.test.cpp
@@ -31,7 +31,6 @@
 #include "host/engine/DspCommand.hpp"
 #include "host/engine/Engine.hpp"
 #include "system/SystemBase.hpp"
-#include "system/mesen/MesenNesSystem.hpp"
 #include "transport/MidiTypes.hpp"
 
 #ifndef RP_BLIPTOASTER_ROM_PATH
@@ -112,6 +111,18 @@ struct NesRig {
 
 using namespace rp::host::test;
 
+// The system a plan placed and the tap it offers: a split-routing case reads the mode through the capability
+// (SystemBase::channelTap), never a concrete core, so what is asserted is what the engine armed.
+struct Placed {
+    SystemBase* core = nullptr;
+    ChannelTap* tap  = nullptr;
+};
+Placed placed(Engine& eng, SystemId id) {
+    SystemBase* core = eng.findSystem(id);
+    REQUIRE(core != nullptr);
+    return {core, core->channelTap()};
+}
+
 } // namespace
 
 TEST_CASE("Engine PinSplit arms a real NES tap live and fans 3 mono pins across outs 0..2",
@@ -121,10 +132,10 @@ TEST_CASE("Engine PinSplit arms a real NES tap live and fans 3 mono pins across
     rig.place(rearming(graphRequest(nesSplitDocument({1, kNesType, rig.rom}, "pins"), {1}), 1, 1));
 
     // The arm rode the plan's swap, against an already-booted core - no rebuild, no reset.
-    auto* nes = dynamic_cast<MesenNesSystem*>(eng.findSystem(1));
-    REQUIRE(nes != nullptr);
-    CHECK(nes->channelExportMode() == 1);           // StereoModPins
-    CHECK(nes->channelLayout().size() == 3);        // Pulse | TND | Expansion
+    const auto nes = placed(eng, 1);
+    REQUIRE(nes.tap != nullptr);
+    CHECK(nes.tap->exportMode() == 1);              // StereoModPins
+    CHECK(nes.core->channelLayout().size() == 3);   // Pulse | TND | Expansion
 
     Lanes lanes;
     driveAndMeasure(eng, 1, lanes, 8);
@@ -146,10 +157,10 @@ TEST_CASE("Engine StereoPinSplit gives each pin its own PAIR, mirrored L=R",
     rig.place(rearming(graphRequest(nesSplitDocument({1, kNesType, rig.rom}, "stereoPins"), {1}), 1, 1));
 
     // Same tap as PinSplit — the modes differ only in how many lanes the three streams spread over.
-    auto* nes = dynamic_cast<MesenNesSystem*>(eng.findSystem(1));
-    REQUIRE(nes != nullptr);
-    CHECK(nes->channelExportMode() == 1);
-    CHECK(nes->channelLayout().size() == 3);
+    const auto nes = placed(eng, 1);
+    REQUIRE(nes.tap != nullptr);
+    CHECK(nes.tap->exportMode() == 1);
+    CHECK(nes.core->channelLayout().size() == 3);
 
     Lanes lanes;
     driveAndMeasure(eng, 1, lanes, 8);
@@ -191,10 +202,10 @@ TEST_CASE("Engine ChannelSplit on a real NES fans 5 mono core channels across ou
     Engine& eng = rig.eng;
     rig.place(rearming(graphRequest(nesSplitDocument({1, kNesType, rig.rom}, "channels"), {1}), 1, 3));
 
-    auto* nes = dynamic_cast<MesenNesSystem*>(eng.findSystem(1));
-    REQUIRE(nes != nullptr);
-    CHECK(nes->channelExportMode() == 3);           // IndividualMono
-    CHECK(nes->channelLayout().size() == 5);        // Square1 | Square2 | Triangle | Noise | DMC
+    const auto nes = placed(eng, 1);
+    REQUIRE(nes.tap != nullptr);
+    CHECK(nes.tap->exportMode() == 3);              // IndividualMono
+    CHECK(nes.core->channelLayout().size() == 5);   // Square1 | Square2 | Triangle | Noise | DMC
 
     Lanes lanes;
     driveAndMeasure(eng, 1, lanes, 8);
@@ -211,19 +222,19 @@ TEST_CASE("Leaving a split routing restores the NES tap and the plain stereo mix
     NesRig rig;
     Engine& eng = rig.eng;
     rig.place(rearming(graphRequest(stereoDocument({{1, kNesType, rig.rom}}, 8), {1})));
-    auto* nes = dynamic_cast<MesenNesSystem*>(eng.findSystem(1));
-    REQUIRE(nes != nullptr);
+    const auto nes = placed(eng, 1);
+    REQUIRE(nes.tap != nullptr);
 
     // Round-trip every split mode. Each leaves the core exactly as it was found, so a user flipping
     // through the Audio Routing cycler can't strand the tap armed (which would leave the mix path
     // draining capture streams forever).
     rig.place(rearming(graphRequest(nesSplitDocument({1, kNesType, rig.rom}, "pins"), {1}), 1, 1));
-    CHECK(nes->channelExportMode() == 1);
+    CHECK(nes.tap->exportMode() == 1);
     rig.place(rearming(graphRequest(nesSplitDocument({1, kNesType, rig.rom}, "channels"), {1}), 1, 3));
-    CHECK(nes->channelExportMode() == 3);
+    CHECK(nes.tap->exportMode() == 3);
     rig.place(rearming(graphRequest(stereoDocument({{1, kNesType, rig.rom}}, 8), {1})));
-    CHECK(nes->channelExportMode() == 0);
-    CHECK(nes->channelLayout().size() == 1);  // back to the single stereo "Mix" stream
+    CHECK(nes.tap->exportMode() == 0);
+    CHECK(nes.core->channelLayout().size() == 1);  // back to the single stereo "Mix" stream
 
     // And the mix still renders: pair 0 carries the note, the other pairs stay silent.
     Lanes lanes;
@@ -239,20 +250,20 @@ TEST_CASE("A second system disarms the NES tap and falls back to Stereo",
     Engine& eng = rig.eng;
     rig.place(rearming(graphRequest(nesSplitDocument({1, kNesType, rig.rom}, "pins"), {1}), 1, 1));
 
-    auto* nes = dynamic_cast<MesenNesSystem*>(eng.findSystem(1));
-    REQUIRE(nes != nullptr);
-    REQUIRE(nes->channelExportMode() == 1);
+    const auto nes = placed(eng, 1);
+    REQUIRE(nes.tap != nullptr);
+    REQUIRE(nes.tap->exportMode() == 1);
 
     // The splits are single-system-only, so adding a peer must retire the wiring AND the tap it needed:
     // otherwise the mix path would keep draining capture streams nothing reads. TS sends a restore only.
     rig.place(rearming(graphRequest(stereoDocument({{1, kNesType, rig.rom}, {2, kNesType, rig.rom}}, 8), {1, 2})));
-    CHECK(nes->channelExportMode() == 0);
-    CHECK(nes->channelLayout().size() == 1);
+    CHECK(nes.tap->exportMode() == 0);
+    CHECK(nes.core->channelLayout().size() == 1);
 
     // Removing the peer re-enters the split, re-arming from scratch.
     rig.place(rearming(graphRequest(nesSplitDocument({1, kNesType, rig.rom}, "pins"), {1}), 1, 1));
-    CHECK(nes->channelExportMode() == 1);
-    CHECK(nes->channelLayout().size() == 3);
+    CHECK(nes.tap->exportMode() == 1);
+    CHECK(nes.core->channelLayout().size() == 3);
 }
 
 TEST_CASE("A re-arm restores and arms again, even with an unchanged document",
@@ -261,16 +272,16 @@ TEST_CASE("A re-arm restores and arms again, even with an unchanged document",
     Engine& eng = rig.eng;
     const Engine::GraphRequest pins = rearming(graphRequest(nesSplitDocument({1, kNesType, rig.rom}, "pins"), {1}), 1, 1);
     rig.place(pins);
-    auto* nes = dynamic_cast<MesenNesSystem*>(eng.findSystem(1));
-    REQUIRE(nes != nullptr);
-    REQUIRE(nes->channelExportMode() == 1);
+    const auto nes = placed(eng, 1);
+    REQUIRE(nes.tap != nullptr);
+    REQUIRE(nes.tap->exportMode() == 1);
 
     // Knock the tap off the mode the routing wants, behind the engine's back: only a re-arm (restore, then
     // arm again, resetting the capture streams) puts it back. Every edit that builds or drops a core, and
     // every routing change, re-arms, even one that leaves the document as it was.
-    nes->setChannelExportMode(2);
+    nes.tap->setExportMode(2);
     rig.place(pins);
-    CHECK(nes->channelExportMode() == 1);
+    CHECK(nes.tap->exportMode() == 1);
     CHECK(eng.systemCount() == 1);
 }
 
@@ -281,15 +292,17 @@ TEST_CASE("A replacing NES is armed afresh, never restored to the replaced core'
     const auto channels = graphRequest(
         nesSplitDocument({1, kNesType, rig.rom, R"("channelExportMode":"pinsPlusRef")"}, "channels"), {1});
     rig.place(rearming(channels, 1, 3));  // armed to 3, remembering 2
-    REQUIRE(dynamic_cast<MesenNesSystem*>(eng.findSystem(1))->channelExportMode() == 3);
+    const auto replaced = placed(eng, 1);
+    REQUIRE(replaced.tap != nullptr);
+    REQUIRE(replaced.tap->exportMode() == 3);
 
     rig.place(rearming(graphRequest(nesSplitDocument({2, kNesType, rig.rom}, "channels"), {2}), 2, 3));
-    auto* fresh = dynamic_cast<MesenNesSystem*>(eng.findSystem(2));
-    REQUIRE(fresh != nullptr);
-    CHECK(fresh->channelExportMode() == 3);
+    const auto fresh = placed(eng, 2);
+    REQUIRE(fresh.tap != nullptr);
+    CHECK(fresh.tap->exportMode() == 3);
     // Leaving the split puts back what THIS core was built with: the old core's 2 left with it.
     rig.place(rearming(graphRequest(stereoDocument({{2, kNesType, rig.rom}}, 8), {2})));
-    CHECK(fresh->channelExportMode() == 0);
+    CHECK(fresh.tap->exportMode() == 0);
 }
 
 TEST_CASE("A routing edit's plan and its tap arming take one ring slot, so they land together",
@@ -297,8 +310,8 @@ TEST_CASE("A routing edit's plan and its tap arming take one ring slot, so they
     NesRig rig;
     Engine& eng = rig.eng;
     rig.place(rearming(graphRequest(stereoDocument({{1, kNesType, rig.rom}}, 8), {1})));
-    auto* nes = dynamic_cast<MesenNesSystem*>(eng.findSystem(1));
-    REQUIRE(nes != nullptr);
+    const auto nes = placed(eng, 1);
+    REQUIRE(nes.tap != nullptr);
 
     // The test thread is the audio thread from here: pushes only queue, and drainCommands applies them.
     eng.setAudioThreadOwns(true);
@@ -315,7 +328,7 @@ TEST_CASE("A routing edit's plan and its tap arming take one ring slot, so they
     REQUIRE(eng.setGraph(rearming(graphRequest(nesSplitDocument({1, kNesType, rig.rom}, "pins"), {1}), 1, 1)).ok);
     CHECK_FALSE(eng.pushCommand(bpm));
     eng.drainCommands();
-    CHECK(nes->channelExportMode() == 1);
-    CHECK(nes->channelLayout().size() == 3);
+    CHECK(nes.tap->exportMode() == 1);
+    CHECK(nes.core->channelLayout().size() == 3);
     eng.setAudioThreadOwns(false);
 }
diff --git a/packages/native/test/engine-graph/BuildGraphGoldens.test.cpp b/packages/native/test/engine-graph/BuildGraphGoldens.test.cpp
index bfa8f28c..5cb53d07 100644
--- a/packages/native/test/engine-graph/BuildGraphGoldens.test.cpp
+++ b/packages/native/test/engine-graph/BuildGraphGoldens.test.cpp
@@ -45,7 +45,7 @@
 #include "graph/CanonicalJson.hpp"
 #include "host/engine/Engine.hpp"
 #include "host/graph/SystemNodes.hpp"
-#include "system/mesen/MesenNesSystem.hpp"  // the export mode a NES ended in
+#include "system/SystemBase.hpp"  // the tap the export mode is read through
 
 using rp::host::test::readFile;
 
@@ -250,11 +250,16 @@ std::optional<std::string> plan(const GoldenCase& c, const std::string& document
     if (e.systemCount() != c.slots.size()) return std::string("planned, but not every slot is placed");
     // Each NES is in the mode the arm asked for, or the one it was built in.
     for (const SystemId id : c.slots) {
-        const auto* nes = dynamic_cast<const MesenNesSystem*>(e.findSystem(id));
-        if (!nes) continue;
+        SystemBase* core = e.findSystem(id);
+        const ChannelTap* tap = core ? core->channelTap() : nullptr;
+        if (!tap) {
+            if (byId.at(id)->type == rp::host::test::kNesType)
+                return "NES " + std::to_string(id) + " offers no tap";
+            continue;
+        }
         const std::uint32_t want = c.tapArm && c.tapArm->id == id ? c.tapArm->mode : byId.at(id)->exportMode;
-        if (nes->channelExportMode() != want)
-            return "NES " + std::to_string(id) + " is in mode " + std::to_string(nes->channelExportMode()) +
+        if (tap->exportMode() != want)
+            return "NES " + std::to_string(id) + " is in mode " + std::to_string(tap->exportMode()) +
                    ", not " + std::to_string(want);
     }
     // Into as many channels as the plan writes. Driven, each block stages a note and its release on each of four
diff --git a/packages/native/test/engine-graph/NodeRig.hpp b/packages/native/test/engine-graph/NodeRig.hpp
index d677b804..fda91ac5 100644
--- a/packages/native/test/engine-graph/NodeRig.hpp
+++ b/packages/native/test/engine-graph/NodeRig.hpp
@@ -9,6 +9,7 @@
 #include <cstdio>
 #include <map>
 #include <memory>
+#include <optional>
 #include <stdexcept>
 #include <string>
 #include <utility>
@@ -42,7 +43,29 @@ struct HostMidiOut {
     bool operator==(const HostMidiOut&) const = default;
 };
 
+// A core the rig hands a node with another console's kind, so a case can drive the node's build check (no real host
+// gets a build's kind wrong). Only the two remaining pure virtuals (onActivate, onSampleRateChanged) reach the core it
+// wraps; everything else resolves to SystemBase's defaults, which is safe because a node only observes the wrapper
+// through the refused build. It owns that core, so the node's destroy frees both.
+class KindWrapper final : public SystemBase {
+public:
+    KindWrapper(std::unique_ptr<SystemBase> core, SystemKind kind)
+        : SystemBase(core->id()), core_(std::move(core)), kind_(kind) {}
+
+    SystemKind kind() const override { return kind_; }
+    void       onActivate(double sampleRate) override { core_->onActivate(sampleRate); }
+    void       onSampleRateChanged(double sampleRate) override { core_->onSampleRateChanged(sampleRate); }
+
+private:
+    std::unique_ptr<SystemBase> core_;
+    SystemKind                  kind_;
+};
+
 struct NodeRig {
+    // The kind a core the factory built is REPORTED as, when set: a node's build compares the kind of the core
+    // rp.host.systems/1 hands back with the kind its type builds, and no real host gets that wrong, so a case that
+    // holds the check sets this and the rig hands the right core back behind a wrapper reporting another kind.
+    std::optional<SystemKind>                            reportKind;
     rp::graph::NodeRegistry                              registry;
     rp::graph::ResourceTable                             resources;
     double                                               rate = 48000.0;
@@ -148,6 +171,7 @@ struct NodeRig {
             std::snprintf(err->message, sizeof err->message, "%s", why.c_str());
             return nullptr;
         }
+        if (rig.reportKind) core = std::make_unique<KindWrapper>(std::move(core), *rig.reportKind);
         rig.built.push_back(core.get());
         return core.release();
     }
diff --git a/packages/native/test/engine-graph/ScriptPacks.test.cpp b/packages/native/test/engine-graph/ScriptPacks.test.cpp
index ae214442..3fe76977 100644
--- a/packages/native/test/engine-graph/ScriptPacks.test.cpp
+++ b/packages/native/test/engine-graph/ScriptPacks.test.cpp
@@ -38,7 +38,7 @@
 #include "host/engine/DspCommand.hpp"
 #include "host/engine/Engine.hpp"
 #include "host/script/ScriptHost.hpp"
-#include "system/mesen/MesenNesSystem.hpp"
+#include "system/ChannelTap.hpp"
 
 using rp::host::ScriptHost;
 using rp::host::test::DocExtras;
@@ -1521,18 +1521,19 @@ TEST_CASE("a reload's replan carries the running plan's tap as it is: neither re
                                                                {docWire("count.out", "midi-out.in1")}}),
                                       {1});
     place(e, rearming(request, 1, 1));  // the tap armed to mode 1 from the core's built mode 0
-    auto* nes = dynamic_cast<MesenNesSystem*>(e.findSystem(1));
-    REQUIRE(nes != nullptr);
-    CHECK(nes->channelExportMode() == 1);
+    SystemBase* core = e.findSystem(1);
+    ChannelTap* tap = core ? core->channelTap() : nullptr;
+    REQUIRE(tap != nullptr);
+    CHECK(tap->exportMode() == 1);
     CHECK(block(e) == std::vector<Out>{{1, 0, {1, 1}}});
     // Moved behind the engine's back, so a replan that restored the tap (to 0) or armed it again (to 1) would show.
-    nes->setChannelExportMode(2);
+    tap->setExportMode(2);
     load(e, "test.tap", toys(tally("test.tap.count", 2)));
     CHECK(block(e) == std::vector<Out>{{1, 0, {2, 1}}});  // the pack's node rebuilt on the new code
-    CHECK(nes->channelExportMode() == 2);
+    CHECK(tap->exportMode() == 2);
     // And the engine still holds what it armed: a re-arm with nothing to arm restores the mode the core had before.
     place(e, rearming(request));
-    CHECK(nes->channelExportMode() == 0);
+    CHECK(tap->exportMode() == 0);
     CHECK(e.scriptThrows() == 0);
     CHECK(e.faultCount() == 0);
 }
diff --git a/packages/native/test/engine-graph/SystemNode.test.cpp b/packages/native/test/engine-graph/SystemNode.test.cpp
index a26c8a83..83e40829 100644
--- a/packages/native/test/engine-graph/SystemNode.test.cpp
+++ b/packages/native/test/engine-graph/SystemNode.test.cpp
@@ -2847,6 +2847,69 @@ TEST_CASE("a warmed core's whole block allocates and frees nothing, emulation in
     }
 }
 
+TEST_CASE("the tap is a capability only a NES offers", "[engine-graph][system-node][tap]") {
+    // SystemBase::channelTap() is how the engine finds the tap a split routing arms
+    // (docs/modular-graph-phase-c4-design.md section 5), so a core without one answers null and the engine arms
+    // nothing. The NES is the only core with one: its tap is the core itself, so the interface and the core's own mode
+    // accessors are one mode.
+    SECTION("NES") {
+        auto core = nes(1, 0);
+        REQUIRE(core->channelTap() != nullptr);
+        CHECK(core->channelTap()->exportMode() == 0);
+        core->channelTap()->setExportMode(1);
+        CHECK(core->channelExportMode() == 1);
+    }
+    SECTION("Game Boy") {
+        auto core = mgb(1);
+        CHECK(core->channelTap() == nullptr);
+    }
+    SECTION("Master System") {
+        auto core = sms(1);
+        CHECK(core->channelTap() == nullptr);
+    }
+    SECTION("GBA") {
+        auto core = std::make_unique<MesenGbaSystem>(1, MesenGbaConfig{}, rp::host::test::romFor("rp.mesen.gba"));
+        core->onActivate(kRate);
+        CHECK(core->channelTap() == nullptr);
+    }
+}
+
+TEST_CASE("a node refuses a core built with another console's kind", "[engine-graph][system-node]") {
+    // The node's build checks that the core rp.host.systems/1 hands back is the kind its own type builds, then casts
+    // it to that type: the config alternative it sent already named the console, so a mismatch is a host or settings
+    // bug, and the check is what keeps the cast sound. No real host can produce one, so the rig reports the kind a case
+    // asks for, and the core it built is given back before the refusal. Every console's node against each other kind
+    // also pins the invariant the cast rests on, that no two core classes report one kind: a duplicated enumerator
+    // would let the wrong class through the comparison, and this matrix is what would see it.
+    struct Type {
+        const char* type;
+        const char* name;
+        const char* extra;
+    };
+    const Type types[] = {
+        {"rp.sameboy.gb", "Game Boy", kGbDmg},
+        {"rp.mesen.nes", "NES", ""},
+        {"rp.mesen.gba", "Game Boy Advance", ""},
+        {"rp.mesen.sms", "Master System / Game Gear", ""},
+    };
+    const SystemKind kinds[] = {SystemKind::SameBoy, SystemKind::MesenNes, SystemKind::MesenGba, SystemKind::MesenSms};
+    for (std::size_t t = 0; t < sizeof(types) / sizeof(types[0]); ++t) {
+        for (const SystemKind kind : kinds) {
+            if (static_cast<std::size_t>(kind) == t) continue;
+            INFO(types[t].type << " node handed a kind " << static_cast<std::uint32_t>(kind) << " core");
+            NodeRig rig(kRate);
+            rig.reportKind = kind;
+            const auto r = rig.set(knobbed(types[t].type, rig.boot(types[t].type, 1, types[t].extra)));
+            CHECK_FALSE(r.ok);
+            REQUIRE(r.errors.size() == 1);
+            CHECK(r.errors[0].code == "activate-failed");
+            CHECK(r.errors[0].message == std::string("the built core is not a ") + types[t].name);
+            CHECK(rig.built.empty());
+            CHECK(rig.released == std::vector<SystemId>{1});
+        }
+    }
+}
+
 TEST_CASE("a tap armed live on an activated NES sizes nothing on the block thread, and its audio path stays free",
           "[engine-graph][system-node][mesen][tap][alloc]") {
     // A plugin routing change arms the tap through the plan's SyncTap trailer (Engine::syncTap), which the block thread
````

- [ ] **Step 2: Run the tests to see them fail**

Run: `cmake --build build --target retroplug-engine-graph-test -j$(nproc)`
Expected: FAIL: the build stops with `fatal error: system/ChannelTap.hpp: No such file or directory` from `ScriptPacks.test.cpp`'s new include (Step 3 creates that header), then on `SystemBase` having no `channelTap()` (Step 3) and `Core::kKind` being no member (Step 4). The report's demonstration is a later one, on the finished tree with `SystemBase::channelTap()` commented out (built, not committed): `MesenNesSystem.hpp:135: error: 'ChannelTap* MesenNesSystem::channelTap()' marked 'override', but does not override`; that is what a tree missing only the member prints.

The kind check's red is not a clean failure, which is worth knowing before writing it: with `if (made->kind() !=
Core::kKind)` temporarily replaced by `if (false)` (built, not committed), the node `static_cast`s the wrong core and
runs the Game Boy's application through it:

`Fatal glibc error: malloc.c:4376 (_int_malloc): assertion failed ... malloc(): unaligned tcache chunk detected`, the
report's red. Without the comparison the cast is unsound immediately, not at some later call. Restored, the case
passes.

- [ ] **Step 3: The capability, and the plan's slot**

`ChannelTap` (`system/ChannelTap.hpp`) is the interface in the global namespace with `SystemBase` and `ChannelStream`,
because the engine reads it as a core capability; its header says what a mode is (the backend's own numbering), who
owns the object, that `setExportMode` runs between two blocks on the block thread or on the control thread while the
engine is quiescent, and that an arm must neither allocate nor restart the core. `SystemBase` gains a `virtual`
`channelTap()` returning `ChannelTap*`, in a "Per-channel audio tap" section beside `debugTarget()`'s capability
pattern, and `MesenNesSystem` becomes `public SystemBase, public ChannelTap` with `channelTap()` returning `this`.
`SystemTable`'s slot becomes `{ id, SystemBase* core, ChannelTap* tap }`; `Engine` forward-declares `ChannelTap`,
`ArmedTap` holds a `const ChannelTap*` and compares the tap's address (the record cannot outlive the core, and within
one class tap equality is core equality because a secondary base's address is the object's plus a per-class constant),
and both `dynamic_cast`s become `channelTap()`:

Create `packages/native/src/system/ChannelTap.hpp`:

````cpp
#pragma once

#include <cstdint>

// A core's per-channel audio tap: the capability a split routing arms (a NES's Pulse | TND | Expansion pins, or its
// five APU channels, exported one stream each). `mode` is the backend's own numbering, the one its config's
// channelExportMode carries: 0 is the mixed output, and the modes above it are the streams the core can capture.
//
// One capability object rather than a virtual per caller, as IDebugTarget is for the debugger: a caller asks
// SystemBase::channelTap() and branches on null, so the engine can arm a routing without naming the console. The
// system owns it; the engine asks only a core it has built, which has activated.
//
// setExportMode runs between two blocks on the block thread (the plan's swap trailer), and may also be called on the
// control thread while the engine is quiescent. An arm must neither allocate nor restart the core: a routing flip must
// not reset a playing game. exportMode is the mode the core was last given (a NES's construct-time value before any
// arm), and is what a caller records so leaving a split mode puts the core back as it found it.
class ChannelTap {
public:
    virtual ~ChannelTap() = default;

    virtual std::uint32_t exportMode() const = 0;
    virtual void          setExportMode(std::uint32_t mode) = 0;
};
````

````diff
diff --git a/packages/native/src/host/engine/Engine.cpp b/packages/native/src/host/engine/Engine.cpp
index 3092e9f4..ee9cad15 100644
--- a/packages/native/src/host/engine/Engine.cpp
+++ b/packages/native/src/host/engine/Engine.cpp
@@ -11,11 +11,11 @@
 #include <string>
 #include <utility>
 
+#include "system/ChannelTap.hpp"  // the capability SyncTap arms
 #include "system/SystemBase.hpp"
 #include "system/SystemTypes.hpp"   // AudioBlockInfo, SystemId
 #include "system/CoreBackends.hpp"  // registerCoreBackends: the engine owns the one factory every build goes through
 #include "system/SystemFactory.hpp"
-#include "system/mesen/MesenNesSystem.hpp"    // the system table's NES, the tap SyncTap arms
 
 #include "transport/FrameBufferTriple.hpp"
 #include "native/core/img/png/lodepng.h"
@@ -287,7 +287,7 @@ SystemBase* Engine::buildSystem(std::uint64_t system, SystemBuildRequest request
     for (auto& e : building_->slots)
         if (e.id == id) {
             e.core = built;
-            e.nes = dynamic_cast<MesenNesSystem*>(built);
+            e.tap = built->channelTap();
             found = true;
         }
     assert(found);
@@ -373,7 +373,7 @@ Engine::GraphResult Engine::publishGraph(const GraphRequest& request, bool repla
         // plan from here; a new instance's has a null entry until its activation, and buildSystem fills it.
         const auto it = liveCores_.find(id);
         SystemBase* core = it == liveCores_.end() ? nullptr : it->second;
-        table->slots.push_back({id, core, core ? dynamic_cast<MesenNesSystem*>(core) : nullptr});
+        table->slots.push_back({id, core, core ? core->channelTap() : nullptr});
     }
     // The host inputs, one of each at most: each takes everything staged, wired or not, so a second would take it from
     // the first whenever it ran first. The host MIDI out, one at most: it frame-sorts what it merges, and a second
@@ -680,23 +680,24 @@ void Engine::applyCommand(DspCommand& cmd, const rp::host::SystemTable& table) {
     }
 }
 
-// The NES tap arming at every structural change, against `table`, the plan just adopted (design section 3.2):
+// The audio tap arming at every structural change, against `table`, the plan just adopted (design section 3.2):
 // restore whatever was armed, then arm `id` to `mode` if `arm`. It runs even when the wanted mode is
 // unchanged, and each run resets the capture streams. A core that has gone (removed, or replaced under
-// its id or a new one) is not restored, and its record is dropped.
+// its id or a new one) is not restored, and its record is dropped; a core that offers no tap
+// (channelTap() null) has nothing to arm, so the document's arm reaches nothing.
 void Engine::syncTap(bool arm, SystemId id, std::uint32_t mode, const rp::host::SystemTable& table) {
     if (armedTap_.active) {
-        if (const auto* e = table.find(armedTap_.id); e && e->nes == armedTap_.core)
-            e->nes->setChannelExportMode(armedTap_.prevMode);
+        if (const auto* e = table.find(armedTap_.id); e && e->tap == armedTap_.tap)
+            e->tap->setExportMode(armedTap_.prevMode);
         armedTap_ = {};
     }
     if (!arm) return;
     const auto* e = table.find(id);
-    MesenNesSystem* nes = e ? e->nes : nullptr;
-    if (!nes) return;
-    if (nes->channelExportMode() != mode) {
-        armedTap_ = { true, nes->id(), nes, nes->channelExportMode() };
-        nes->setChannelExportMode(mode);
+    ChannelTap* tap = e ? e->tap : nullptr;
+    if (!tap) return;
+    if (tap->exportMode() != mode) {
+        armedTap_ = { true, e->id, tap, tap->exportMode() };
+        tap->setExportMode(mode);
     }
 }
 
diff --git a/packages/native/src/host/engine/Engine.hpp b/packages/native/src/host/engine/Engine.hpp
index 11af3d01..56728177 100644
--- a/packages/native/src/host/engine/Engine.hpp
+++ b/packages/native/src/host/engine/Engine.hpp
@@ -21,7 +21,7 @@
 
 class SystemBase;
 class SystemFactory;
-class MesenNesSystem;
+class ChannelTap;
 struct DspCommand;
 struct SystemBuildRequest;
 namespace rp::graph { struct HostCommand; }
@@ -380,18 +380,20 @@ private:
     // emulated program has its own invariants over those bytes. That is the point of the feature.
     bool writeRam(SystemBase* sys, std::uint32_t offset, const std::vector<std::uint8_t>& bytes);
 
-    // The NES tap syncTap() armed, and what its export mode was BEFORE we touched it. Restored before every
+    // The tap syncTap() armed, and what its export mode was BEFORE we touched it. Restored before every
     // re-resolve so leaving a split mode (or gaining a second system) puts the core back exactly as found. Scoped
     // to what WE armed on purpose: the CLI / RenderHost set channelExportMode in the system node's settings and
-    // never select a split routing, so they are never disturbed. `core` is compared, never followed: a fresh core
-    // replaced in under the same id is not the one armed. It is compared by address, so a core allocated where the
-    // armed one was freed defeats it; TS never gets there, since buildSystem refuses a live id, the stores' id
-    // counter only goes up, and every TS edit that drops a core re-arms, which drops this record with it.
+    // never select a split routing, so they are never disturbed. `tap` is compared, never followed: a fresh core
+    // replaced in under the same id is not the one armed. A NES's tap IS the core object, so the record cannot
+    // outlive it, and its address differs from the core pointer's by a per-class constant, so within one class tap
+    // equality is core equality. It is compared by address, so a core whose tap lands where the armed tap was
+    // defeats it; TS never gets there, since buildSystem refuses a live id, the stores' id counter only goes up,
+    // and every TS edit that drops a core re-arms, which drops this record with it.
     struct ArmedTap {
-        bool                  active   = false;
-        SystemId              id       = 0;
-        const MesenNesSystem* core     = nullptr;
-        std::uint32_t         prevMode = 0;
+        bool              active   = false;
+        SystemId          id       = 0;
+        const ChannelTap* tap      = nullptr;
+        std::uint32_t     prevMode = 0;
     };
     ArmedTap armedTap_;
 
diff --git a/packages/native/src/host/graph/SystemTable.hpp b/packages/native/src/host/graph/SystemTable.hpp
index 5a4489cb..61070700 100644
--- a/packages/native/src/host/graph/SystemTable.hpp
+++ b/packages/native/src/host/graph/SystemTable.hpp
@@ -6,16 +6,16 @@
 #include "system/SystemTypes.hpp"
 
 class SystemBase;
-class MesenNesSystem;
+class ChannelTap;
 
 namespace rp::host {
 
-// One system as a plan sees it: its core, plus the concrete pointer the engine's NES tap needs, so the tap needs no
-// dynamic_cast on the block thread.
+// One system as a plan sees it: its core, plus the core's channel tap if it offers one, so a split routing arms it
+// without a cast on the block thread.
 struct SystemEntry {
-    SystemId        id = 0;
-    SystemBase*     core = nullptr;
-    MesenNesSystem* nes = nullptr;  // null unless a NES
+    SystemId    id = 0;
+    SystemBase* core = nullptr;
+    ChannelTap* tap = nullptr;  // core->channelTap(): null unless the core offers one
 };
 
 // The systems of one plan, in project slot order (docs/modular-graph-phase-b1-design.md section 4.5), and what else
diff --git a/packages/native/src/system/SystemBase.hpp b/packages/native/src/system/SystemBase.hpp
index 451a9528..48b0d091 100644
--- a/packages/native/src/system/SystemBase.hpp
+++ b/packages/native/src/system/SystemBase.hpp
@@ -9,6 +9,7 @@
 #include <string_view>
 #include <vector>
 
+#include "system/ChannelTap.hpp"
 #include "system/CpuState.hpp"
 #include "system/InputTypes.hpp"
 #include "system/MemoryAccessor.hpp"
@@ -234,6 +235,15 @@ public:
     // or has no program counter. Not virtual — identical for every backend.
     bool runUntilPc(std::uint32_t target, std::uint64_t maxCycles);
 
+    // -- Per-channel audio tap ----------------------------------------------
+    //
+    // Optional audio capability: the object that exports the core's streams one by one, behind a split routing. Null
+    // when the core has no tap (every core but the NES today). One capability object rather than a virtual per caller;
+    // callers branch on nullptr (no dynamic_cast), as with debugTarget(). The system owns it and it lives as long as
+    // the core does: a reset or deactivation leaves the pointer valid, and a caller re-asks only after the core is
+    // replaced.
+    virtual ChannelTap* channelTap() { return nullptr; }
+
     // -- Debugger / profiler ------------------------------------------------
     //
     // Optional debug capability (profiler, disassembly, breakpoints, …).
diff --git a/packages/native/src/system/mesen/MesenNesSystem.hpp b/packages/native/src/system/mesen/MesenNesSystem.hpp
index e467023e..51320bf5 100644
--- a/packages/native/src/system/mesen/MesenNesSystem.hpp
+++ b/packages/native/src/system/mesen/MesenNesSystem.hpp
@@ -23,14 +23,17 @@ class NesN8FifoRole;
 // gain. Native NES resolution: 256x240. Audio runs at the host sample rate
 // (Mesen's SoundMixer resamples internally). Input arrives as NesButton; host
 // MIDI is forwarded to the N8 FIFO (NesN8FifoRole).
-class MesenNesSystem final : public SystemBase {
+class MesenNesSystem final : public SystemBase, public ChannelTap {
 public:
     MesenNesSystem(SystemId id,
                    MesenNesConfig config,
                    std::vector<std::uint8_t> romBytes);
     ~MesenNesSystem() override;
 
-    SystemKind kind() const override { return SystemKind::MesenNes; }
+    // The console a system node of this type builds: the node compares a built core's kind() with this before it casts
+    // it to this class, and kind() answers it, so the two cannot drift.
+    static constexpr SystemKind kKind = SystemKind::MesenNes;
+    SystemKind kind() const override { return kKind; }
 
     void onActivate(double sampleRate) override;
     void onDeactivate() override;
@@ -127,6 +130,11 @@ public:
     /** The armed per-channel export mode (0 = off). Lets the Engine record what it found before arming,
      *  so it can restore a construct-time (CLI / render) value it did not set. */
     std::uint32_t channelExportMode() const { return config_.channelExportMode; }
+    // The tap as the engine sees it (SystemBase::channelTap): this core is its own, so the two methods above ARE the
+    // capability, and a split routing arms a NES without the engine naming the console.
+    ChannelTap*   channelTap() override { return this; }
+    std::uint32_t exportMode() const override { return channelExportMode(); }
+    void          setExportMode(std::uint32_t mode) override { setChannelExportMode(mode); }
     // The live APU flush window in CPU cycles (the mixer's conversion of apuLatencyMs against the region
     // clock). 0 before onActivate. Exposed for tests / introspection.
     std::uint32_t apuFlushCycleLength() const;
````

- [ ] **Step 4: The kind check**

Each core class carries `static constexpr SystemKind kKind`, which its `kind()` returns, and the node's build compares
what the host handed back with `Core::kKind`, then destroys it and refuses, or `static_cast`s it. `kKind` sits on the
core class rather than on the node type so there is one source of truth: the value the node compares with is the value
the core answers by construction. The cast's soundness is the invariant `SystemFactory` and `MesenBackend` already
rest on, kinds being unique per core class, which the comment in `SystemNodes.cpp` states:

````diff
diff --git a/packages/native/src/host/graph/SystemNodes.cpp b/packages/native/src/host/graph/SystemNodes.cpp
index 62b812b5..7c7b33c9 100644
--- a/packages/native/src/host/graph/SystemNodes.cpp
+++ b/packages/native/src/host/graph/SystemNodes.cpp
@@ -441,11 +441,15 @@ struct SystemNode {
         SystemBase* made = systems->build(systems->ctx, settings->system, std::move(request), &err);
         if (!made)
             throw std::runtime_error(err.message[0] ? std::string(err.message) : std::string("the core did not build"));
-        core = dynamic_cast<Core*>(made);
-        if (!core) {  // the config names the console, so this is a host or settings bug rather than a document's
+        // The config alternative the request carries names the console, so the core handed back must be that console's:
+        // a host that built another reports another kind(), and every core class reports a kind no other one does, so
+        // the comparison checks the type the cast below then assumes. A mismatch is a host or settings bug rather than
+        // a document's, and the core it built is given back before the refusal.
+        if (made->kind() != Core::kKind) {
             systems->destroy(systems->ctx, made);
             throw std::runtime_error(std::string("the built core is not a ") + Self::name);
         }
+        core = static_cast<Core*>(made);
         // The build applied the config, so its fields are on the core already: `applied` is seeded with the settings'
         // live knobs, less what the config does not carry, so the node's own first application finds them equal and
         // calls nothing (a model change would restart the core and lose the savestate the build restored). What is left
diff --git a/packages/native/src/system/mesen/MesenGbaSystem.hpp b/packages/native/src/system/mesen/MesenGbaSystem.hpp
index 52e3218d..8b4ef030 100644
--- a/packages/native/src/system/mesen/MesenGbaSystem.hpp
+++ b/packages/native/src/system/mesen/MesenGbaSystem.hpp
@@ -26,7 +26,10 @@ public:
                    std::vector<std::uint8_t> romBytes);
     ~MesenGbaSystem() override;
 
-    SystemKind kind() const override { return SystemKind::MesenGba; }
+    // The console a system node of this type builds: the node compares a built core's kind() with this before it casts
+    // it to this class, and kind() answers it, so the two cannot drift.
+    static constexpr SystemKind kKind = SystemKind::MesenGba;
+    SystemKind kind() const override { return kKind; }
 
     void onActivate(double sampleRate) override;
     void onDeactivate() override;
diff --git a/packages/native/src/system/mesen/MesenSmsSystem.hpp b/packages/native/src/system/mesen/MesenSmsSystem.hpp
index 928916d2..b9195a7d 100644
--- a/packages/native/src/system/mesen/MesenSmsSystem.hpp
+++ b/packages/native/src/system/mesen/MesenSmsSystem.hpp
@@ -32,7 +32,10 @@ public:
                    std::vector<std::uint8_t> romBytes);
     ~MesenSmsSystem() override;
 
-    SystemKind kind() const override { return SystemKind::MesenSms; }
+    // The console a system node of this type builds: the node compares a built core's kind() with this before it casts
+    // it to this class, and kind() answers it, so the two cannot drift.
+    static constexpr SystemKind kKind = SystemKind::MesenSms;
+    SystemKind kind() const override { return kKind; }
 
     void onActivate(double sampleRate) override;
     void onDeactivate() override;
diff --git a/packages/native/src/system/sameboy/SameBoySystem.hpp b/packages/native/src/system/sameboy/SameBoySystem.hpp
index ccbe6927..dd479ae5 100644
--- a/packages/native/src/system/sameboy/SameBoySystem.hpp
+++ b/packages/native/src/system/sameboy/SameBoySystem.hpp
@@ -36,7 +36,10 @@ public:
                   std::vector<std::uint8_t> romBytes);
     ~SameBoySystem() override;
 
-    SystemKind kind() const override { return SystemKind::SameBoy; }
+    // The console a system node of this type builds: the node compares a built core's kind() with this before it casts
+    // it to this class, and kind() answers it, so the two cannot drift.
+    static constexpr SystemKind kKind = SystemKind::SameBoy;
+    SystemKind kind() const override { return kKind; }
 
     void onActivate(double sampleRate) override;
     void onDeactivate() override;
````

- [ ] **Step 5: Run the tests to see them pass, and parity unchanged**

Run: `cmake --build build --target retroplug-engine-graph-test -j$(nproc) && build/bin/retroplug-engine-graph-test "[tap]" && build/bin/retroplug-engine-graph-test "a node refuses a core built with another console's kind"`
Expected: the capability case passes, and the matrix prints `All tests passed (72 assertions in 1 test case)` (12 combinations x 6 checks).

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources build/bin/retroplug-engine-graph-test`
Expected: with fixtures, the prototype printed `All tests passed (27138 assertions in 209 test cases)` for the whole binary (stage 3 with fixtures 27060 / 207).

Run: `pnpm test:plugin`
Expected: `# 10 plugin test binary(ies) passed (jobs=16)`: the engine-graph binary's 27078 / 209 without fixtures (the report records only the with-fixtures 27138 / 209; Task 4 adds no fixture-gated assertion, so the no-fixture count is 27138 minus Task 3's fixture delta, 27060 - 27000 = 60, which is 27078), the audio binary's `(1233729 assertions in 101 test cases)` (the +10 assertions are the added `REQUIRE(tap != nullptr)` in each split case), the graph binary's `(2174 / 198)`.

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=16)`, 0 skipped, nothing re-recorded (run twice in the prototype, before and after the last polish).

Run: `pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=16)`, with `# 46 case(s) SKIPPED in 10 file(s)` and the baseline's distribution.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native`
Expected: `# 158 native test file(s) passed (jobs=16)`, with `# 2 case(s) SKIPPED in 2 file(s): dsp-bench(1), nes-bench(1)`.

Run: `pnpm typecheck && pnpm test && pnpm check:docs`
Expected: `# 168 test file(s) passed (jobs=16)`, then `check-doc-links: 69 file(s) clean` (the prototype's 68 plus this plan).

- [ ] **Step 6: The sanitizers, on the tap and the alloc cases**

Run: `cmake --build build-tsan --target retroplug-engine-graph-test retroplug-graph-test -j$(nproc) && TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-engine-graph-test "[tap],[alloc]"`
Expected: `7 test cases: 6 passed, 1 skipped; 292 assertions, all passed` as the report puts it (`6 passed, 1 skipped`: the capability case and the six `[alloc]` cases; the arm skips), no report.

Run: `TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-engine-graph-test "a node refuses a core built with another console's kind"`
Expected: `All tests passed (72 assertions in 1 test case)`, no report: the 12-way matrix is its own case and carries no tag, so it is selected by name under both sanitizers.

Run: `TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-engine-graph-test "a reload's replan carries the running plan's tap as it is: neither restored nor armed again"`
Expected: `All tests passed (12 assertions in 1 test case)`, no report: the ScriptPacks re-arm the tap changed.

Run: `cmake --build build-tsan --target retroplug-audio-test -j$(nproc) && TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-audio-test "[channelsplit]"`
Expected: `All tests passed (348 assertions in 9 test cases)`, no report.

Run: `cmake --build build-asan --target retroplug-engine-graph-test retroplug-graph-test -j$(nproc) && ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-engine-graph-test "[tap],[alloc]"`
Expected: the same 7 cases (6 passed, 1 skipped) and 292 assertions, no report.

Run: `ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-engine-graph-test "a node refuses a core built with another console's kind"`
Expected: `All tests passed (72 assertions in 1 test case)`, no report: this is the leg that shows the `KindWrapper` frees the core it owns.

Run: `cmake --build build-asan --target retroplug-audio-test -j$(nproc) && ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-audio-test "[channelsplit]"`
Expected: `348 / 9`, no report.

- [ ] **Step 7: Commit**

````bash
git add -- packages/native/src/system/ChannelTap.hpp packages/native/src/host/engine/Engine.cpp \
  packages/native/src/host/engine/Engine.hpp packages/native/src/host/graph/SystemNodes.cpp \
  packages/native/src/host/graph/SystemTable.hpp packages/native/src/system/SystemBase.hpp \
  packages/native/src/system/mesen/MesenGbaSystem.hpp packages/native/src/system/mesen/MesenNesSystem.hpp \
  packages/native/src/system/mesen/MesenSmsSystem.hpp packages/native/src/system/sameboy/SameBoySystem.hpp \
  packages/native/test/audio/NesSplitRouting.test.cpp packages/native/test/engine-graph/BuildGraphGoldens.test.cpp \
  packages/native/test/engine-graph/NodeRig.hpp packages/native/test/engine-graph/ScriptPacks.test.cpp \
  packages/native/test/engine-graph/SystemNode.test.cpp
git commit -F - <<'EOF'
refactor(engine): the NES tap is a capability any core may offer, so the engine names no console

Engine kept a NES core's address in every plan slot purely so its split routing could arm the per-channel tap, found
with dynamic_cast in buildSystem and in publishGraph's pre-fill, and SystemNodes.cpp's build dynamic_casted the core
rp.host.systems/1 handed back to its own type. Both are gone; what is left is a capability seam the engine branches on,
as it already did for the debugger.

SystemBase::channelTap() returns a ChannelTap* (exportMode(), setExportMode(mode)), null by default. A NES's tap is the
core itself, so MesenNesSystem implements the interface over the channelExportMode accessors it already had, and every
other core keeps the base's null. SystemTable's slot holds that pointer instead of MesenNesSystem*, filled from
channelTap() where the two casts were; ArmedTap remembers the tap and compares it by address. The tap IS the core
object, so the record cannot outlive it and identity is what is compared; its address differs from the core pointer's by
a per-class constant, so within one class tap equality is core equality. That argument is sound for the same reason the
core pointer's was: system ids only go up, buildSystem refuses a live id, and every edit that drops a core re-arms. The
accepted limitation C3 recorded stands: a core whose tap lands where the armed tap was freed would defeat it, and TS
cannot get there. The engine no longer includes MesenNesSystem.hpp; its other console include, CoreBackends.hpp, is the
neutral factory registry it still needs.

A system node's build now compares the built core's kind() with Core::kKind, the kind each core class reports (and the
value its kind() answers), then static_casts. A mismatch is still refused with the same message, the core still given
back first; the kind comparison is what keeps the cast sound, since the factory's dispatch keys on the same enum and no
two core classes share a kind.

The arm's mode validation still takes 1 to 3 in Engine and spec/02 documents that range: a deliberate residual, since TS
owns the numbering and the arm travels as a byte, so the capability does not yet carry its bounds. A later capability
can add maxMode().

The tap tests read the mode through channelTap() now: NesSplitRouting.test.cpp's nine casts, the goldens' "each NES
ends in the mode it was built in", and ScriptPacks.test.cpp's reload re-arm case read the interface (channelLayout() is
already a SystemBase virtual, and the one test that knocked the tap off uses setExportMode). New engine-graph cases:
only a NES offers a tap (a Game Boy, an SMS and a GBA answer null, and the NES's interface and its own accessors are one
mode), and every console's node refuses a core built with each of the other three kinds through a NodeRig seam that
hands the right core back behind a wrapper reporting another kind, asserting the refusal, activate-failed and the
released core each time (with the check disabled the node casts the wrapper and the heap corrupts, which is the red this
holds).

No behaviour changes: strict parity is green and nothing was re-recorded, the [video] hashes are unchanged, and the
engine-level tap arm and restore across a replace still hold.
EOF
````

### Task 5: a core's input queues are preallocated rings

Design section 11, the second of the two scopes the user added (design section 2, the third decision). Task 3's whole
block case warmed with a button press and then staged nothing inside its counted window, so "a warmed core's block
allocates nothing" held only while the node handed its core no host input; a host playing a MIDI-heavy project is
exactly that steady state. Every queue a block's input lands in was a `std::vector` grown by `push_back` (a Mesen press
queue), a `std::deque` (a Game Boy's presses and serial bytes, the N8 role's two feeds, the reliable link's window, the
SMS role's levels, the FIFO's wire) or a `std::vector` a message, and `std::stable_sort` took a temporary buffer once a
block. They are now `rp::FixedQueue<T>`, `rp::FixedByteRing` (runs over one byte arena, so a long SysEx is one
descriptor and one copy) and `rp::stableSortEntries` (an in-place stable insertion sort), every one sized when the core
is built from `system/InputCapacity.hpp`, whose numbers a case reads back from `script/nodes/ports.ts`. A push past a
bound is dropped and counted, and each core reports `inputAccepted()` / `inputDropped()`; the reliable link is the one
exception, which gives up its oldest frame and re-syncs, as it always did.

**Files:** 28 files, by step below.
- Create: `packages/native/src/system/InputCapacity.hpp`, `packages/native/src/util/FixedQueue.hpp`, `packages/native/test/graph/FixedQueue.test.cpp`
- Modify: 25 files

- [ ] **Step 1: Write the failing cases**

`SystemNode.test.cpp`'s whole-block case stages a block's host input inside every counted window, the way a node
applies its inputs before the core's `prepareForBlock`: notes, clocks, a raw run and a settings-length SysEx into a
NES (and a section with the reliable link on, whose ROM capability byte is flipped in a copy so the framing path
really runs), the transport's levels into a SMS, MIDI runs and a press burst into a Game Boy in both stream shapes,
presses into every console, and through the Engine the same staging by the node's own doors;
`wholeBlock`/`checkWholeBlocksAreFree` take a `Stage`, and each window checks `inputAccepted()` grew, so a stage
cannot become a no-op unnoticed. A `[input-drops]` case has a section per core queue that pushes past the bound, holds
the refused count and `inputAccepted()`, and then runs a block. A case reads the four capacities from `ports.ts`
(through a new `RP_PORTS_TS_PATH` define) and compares them with `InputCapacity.hpp`. `FixedQueue.test.cpp` (new, in
`retroplug-graph-test`) covers the queue, the ring and the sort directly. The audio suite reads the new front-byte
accessor in two files and gains the FIFO's own over-bound cases:

Create `packages/native/test/graph/FixedQueue.test.cpp`:

````cpp
// The fixed storage a core's ingress queues are built from (docs/modular-graph-phase-c4-design.md section 4):
// FixedQueue's refusal and its compaction when the storage is full but a prefix was consumed, FixedByteRing's wrap,
// and stableSortEntries's stability. These are the branches a block relies on and a core's cases reach only indirectly.

#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "util/FixedQueue.hpp"

TEST_CASE("FixedQueue pushes, pops, refuses at its bound and counts the refusals", "[graph][fixed-queue]") {
    rp::FixedQueue<int> queue(3);
    CHECK(queue.capacity() == 3);
    CHECK(queue.empty());
    CHECK(queue.push(1));
    CHECK(queue.push(2));
    CHECK(queue.push(3));
    CHECK(queue.size() == 3);
    CHECK(queue.front() == 1);
    CHECK(queue.back() == 3);
    CHECK_FALSE(queue.push(4));
    CHECK(queue.size() == 3);
    CHECK(queue.dropped() == 1);
    CHECK(queue.front() == 1);

    queue.pop();
    CHECK(queue.size() == 2);
    CHECK(queue.front() == 2);
    queue.pop();
    queue.pop();
    CHECK(queue.empty());
    CHECK(queue.dropped() == 1);

    // The counter is cumulative, not reset by a drain.
    CHECK(queue.push(5));
    queue.clear();
    CHECK(queue.empty());
    CHECK(queue.dropped() == 1);
}

TEST_CASE("FixedQueue compacts a consumed prefix before a push, keeping the live order", "[graph][fixed-queue]") {
    rp::FixedQueue<int> queue(3);
    queue.push(10);
    queue.push(20);
    queue.push(30);
    CHECK_FALSE(queue.push(40));  // full with nothing consumed: refused
    queue.pop();                   // 10 goes; the buffer still holds [10, 20, 30] with head at 1

    // The push needs the room the consumed prefix occupies, so the queue erases it first. The order of what is live
    // must be unchanged, and the new entry lands behind it.
    CHECK(queue.push(40));
    CHECK(queue.dropped() == 1);
    CHECK(queue.size() == 3);
    CHECK(queue.front() == 20);
    CHECK(queue[0] == 20);
    CHECK(queue[1] == 30);
    CHECK(queue[2] == 40);

    // begin()/end() are the live range, head included, after the compaction as before it.
    std::vector<int> live(queue.begin(), queue.end());
    CHECK(live == std::vector<int>{20, 30, 40});
}

TEST_CASE("FixedByteRing wraps and delivers in arrival order over more than its capacity", "[graph][fixed-byte-ring]") {
    rp::FixedByteRing ring(4);
    CHECK(ring.capacity() == 4);
    CHECK(ring.empty());
    for (std::uint8_t b = 1; b <= 4; ++b) CHECK(ring.push(b));
    CHECK_FALSE(ring.push(5));
    CHECK(ring.dropped() == 1);
    CHECK(ring.size() == 4);
    CHECK(ring.front() == 1);

    ring.pop();
    ring.pop();                    // two slots are free at the front; the next pushes wrap past the end
    CHECK(ring.push(5));
    CHECK(ring.push(6));
    CHECK(ring.dropped() == 1);
    CHECK(ring.size() == 4);
    CHECK(ring.front() == 3);

    std::vector<std::uint8_t> got;
    while (!ring.empty()) {
        got.push_back(ring.front());
        ring.pop();
    }
    CHECK(got == std::vector<std::uint8_t>{3, 4, 5, 6});
    CHECK(ring.dropped() == 1);
}

TEST_CASE("stableSortEntries keeps equal keys in their original order", "[graph][fixed-queue]") {
    struct Entry {
        int key;
        int seq;
    };
    std::vector<Entry> entries{{2, 0}, {1, 1}, {2, 2}, {1, 3}, {2, 4}};
    rp::stableSortEntries(entries.data(), entries.data() + entries.size(),
                          [](const Entry& a, const Entry& b) { return a.key < b.key; });

    std::vector<int> keys;
    std::vector<int> seqs;
    for (const auto& e : entries) {
        keys.push_back(e.key);
        seqs.push_back(e.seq);
    }
    CHECK(keys == std::vector<int>{1, 1, 2, 2, 2});
    CHECK(seqs == std::vector<int>{1, 3, 0, 2, 4});  // equal keys did not swap: the sort is stable
}
````

````diff
diff --git a/packages/native/cmake/tests.cmake b/packages/native/cmake/tests.cmake
index 2a4e1109..ba94be8d 100644
--- a/packages/native/cmake/tests.cmake
+++ b/packages/native/cmake/tests.cmake
@@ -113,6 +113,7 @@ retroplug_add_test_binary(
              RP_BUILD_GRAPH_TS_PATH="${CMAKE_SOURCE_DIR}/packages/retroplug/src/graph/buildGraph.ts"  # the knob ranges
              RP_CORE_ROLES_TS_PATH="${CMAKE_SOURCE_DIR}/packages/retroplug/src/coreRoles.ts"  # their ranges
              RP_SYSTEM_SETTINGS_TS_PATH="${CMAKE_SOURCE_DIR}/packages/retroplug/src/systemSettings.ts"  # gain's range
+             RP_PORTS_TS_PATH="${CMAKE_SOURCE_DIR}/packages/retroplug/src/script/nodes/ports.ts"  # the queue capacities
              RP_BUILD_TYPE="$<CONFIG>")                 # the bench prints the configuration it was built in
 add_dependencies(retroplug-engine-graph-test retroplug-script-test-bundle)  # the embedded bytecode first
 
@@ -231,6 +232,7 @@ retroplug_add_test_binary(
              test/graph/CanonicalJson.test.cpp
              test/graph/Descriptor.test.cpp
              test/graph/NodeLayer.test.cpp
+             test/graph/FixedQueue.test.cpp              # the fixed ingress storage: refusal, compaction and wrap
              test/graph/Registry.test.cpp
              test/graph/Planner.test.cpp
              test/graph/Execution.test.cpp
diff --git a/packages/native/test/audio/N8ReliableLink.test.cpp b/packages/native/test/audio/N8ReliableLink.test.cpp
index 927fe171..d2e331be 100644
--- a/packages/native/test/audio/N8ReliableLink.test.cpp
+++ b/packages/native/test/audio/N8ReliableLink.test.cpp
@@ -112,6 +112,27 @@ TEST_CASE("reliable link: messages go out as frames behind one sync", "[audio][n
     CHECK(s.outstanding() == 2);
 }
 
+TEST_CASE("reliable link: a window that fills without an ack gives up the oldest and re-syncs, not grows",
+          "[audio][nes][link]") {
+    // The sender's window is 48 frames. A cart that stops answering must not make it grow: at the 49th frame the
+    // oldest are given up and a sync goes out ahead of the new frame, so the ROM is re-pointed at what follows.
+    N8ReliableSender s;
+    std::size_t framesInWire = 0;
+    int framesSentByEmit = 0;  // the seq >= 0 marker counts first sends (and later re-sends) on the wire
+    const auto emit = [&](const std::uint8_t* d, std::size_t n, int seq) {
+        framesInWire += n;
+        if (seq >= 0) ++framesSentByEmit;
+    };
+    for (int i = 0; i < 49; ++i) {
+        const Bytes msg = noteOn(0, static_cast<std::uint8_t>(60 + i % 12), 100);
+        s.send(msg.data(), msg.size(), emit);
+    }
+    CHECK(s.framesSent() == 49);
+    CHECK(framesSentByEmit == 49);
+    CHECK(s.outstanding() == 1);   // the 49th send cleared the window before its own frame
+    CHECK(framesInWire > 0);
+}
+
 TEST_CASE("reliable link: running status is expanded, real-time passes through, SysEx is one frame", "[audio][nes][link]") {
     N8ReliableSender s;
     Bytes wire;
diff --git a/packages/native/test/audio/NesEverdriveFifo.test.cpp b/packages/native/test/audio/NesEverdriveFifo.test.cpp
index 3ac5834b..4f69ec43 100644
--- a/packages/native/test/audio/NesEverdriveFifo.test.cpp
+++ b/packages/native/test/audio/NesEverdriveFifo.test.cpp
@@ -896,6 +896,51 @@ TEST_CASE("the default profile is the permissive queue every existing test assum
     CHECK(readBytes(fifo, burst.size()) == burst);
 }
 
+TEST_CASE("the permissive profile still drops past fifo_a's hard bound, and keeps the rest",
+          "[audio][nes][fifo][wire]") {
+    // depth 0 means "not modelled", not "unbounded forever": fifo_a's storage is still bounded by what a block's
+    // host input can deliver plus one reply, so a directory listing past it is dropped and counted rather than growing
+    // the vector inside a block. The bytes before the bound stay, in order.
+    rp::NesEverdriveFifo fifo;
+    constexpr std::size_t kBound = std::size_t(rp::kMidiInput.bytes) + rp::kBytesInput.bytes +
+                                   rp::FILE_READ_MAX + 1;
+    const auto burst = pattern(kBound + 3);
+    for (std::uint8_t b : burst) fifo.pushByte(b);
+
+    const auto stats = fifo.stats();
+    CHECK(stats.rxCapacity == 0);
+    CHECK(stats.rxDepth == kBound);
+    CHECK(stats.wirePending == 0);
+    CHECK(stats.droppedBytes == 3);
+    CHECK(readBytes(fifo, 5) == std::vector<std::uint8_t>(burst.begin(), burst.begin() + 5));
+}
+
+TEST_CASE("the wire's own buffer drops past a block's input and counts it", "[audio][nes][fifo][wire]") {
+    // The wire buffer stands for the host's outbound buffer and is sized to one block's host input. With no clock
+    // time passing, nothing crosses it, so the push past its bound is the one this case measures; making the wire
+    // instant afterwards shows the retained bytes still arrive in order.
+    rp::NesEverdriveFifo fifo;
+    std::uint64_t cycles = 0;
+    fifo.setCartClock([&cycles] { return cycles; }, PAL_CLOCK_HZ);
+    fifo.setFifoProfile(/*depth*/ 0, /*bytesPerSecond*/ 1);   // a wire that has not had time to carry anything
+
+    constexpr std::size_t kCapacity = std::size_t(rp::kMidiInput.bytes) + rp::kBytesInput.bytes;
+    const auto burst = pattern(kCapacity + 3);
+    for (std::uint8_t b : burst) fifo.pushByte(b);
+
+    auto stats = fifo.stats();
+    CHECK(stats.wirePending == kCapacity);
+    CHECK(stats.rxDepth == 0);
+    CHECK(stats.droppedBytes == 3);
+
+    fifo.setFifoProfile(/*depth*/ 0, /*bytesPerSecond*/ 0);   // instant from here: what was queued crosses
+    stats = fifo.stats();
+    CHECK(stats.wirePending == 0);
+    CHECK(stats.rxDepth == kCapacity);
+    CHECK(stats.droppedBytes == 3);
+    CHECK(readBytes(fifo, 5) == std::vector<std::uint8_t>(burst.begin(), burst.begin() + 5));
+}
+
 TEST_CASE("a rated wire delivers over time instead of atomically", "[audio][nes][fifo][wire]") {
     // The difference that hid nesvj's worst bug: its copy loop kept a byte offset so it could resume
     // when the FIFO ran dry, and the resume path was wrong. Fed a chunk atomically the loop never
diff --git a/packages/native/test/audio/NesN8FifoTiming.test.cpp b/packages/native/test/audio/NesN8FifoTiming.test.cpp
index 745b753a..d9045c2b 100644
--- a/packages/native/test/audio/NesN8FifoTiming.test.cpp
+++ b/packages/native/test/audio/NesN8FifoTiming.test.cpp
@@ -241,3 +241,33 @@ TEST_CASE("flushAll empties both queues, unlike clear", "[audio][nes]") {
     CHECK(role.fifoRxCount() == 0);
     CHECK(role.readFifoByte() == -1);
 }
+
+TEST_CASE("a carried MIDI run and raw run keep their bytes across a rebase", "[audio][nes]") {
+    // Both feeds enqueue past the block end, out of frame order: the MIDI message is queued first (so its bytes come
+    // first in the arena), the raw run second (so it sorts ahead of the message). Both are carried into the next block.
+    // The arena's compaction must not assume the live runs are in queue order: it has to move them in the order their
+    // bytes occupy, or the first memmove overwrites a run it has not copied yet.
+    NesN8FifoRole role;
+    constexpr std::uint32_t kBlock = 512;
+
+    const ::MidiEvent message = noteOn(kBlock + 388, 64);  // 900, enqueued first
+    role.onMidi(&message, 1);
+    const std::uint8_t raw[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04};  // 600, enqueued second
+    role.pushBytes(kBlock + 88, raw, sizeof raw);
+
+    role.pumpUntil(kBlock);  // neither is due; the sort puts the raw run ahead of the message in the queue
+    CHECK(role.pendingCount() == 3 + sizeof raw);
+    role.rebase(kBlock);     // both carried: 88 and 388
+
+    role.pumpUntil(88);
+    CHECK(role.fifoRxCount() == static_cast<int>(sizeof raw));
+    for (const std::uint8_t byte : raw) CHECK(role.readFifoByte() == static_cast<int>(byte));
+    CHECK(role.readFifoByte() == -1);
+
+    role.pumpUntil(388);
+    CHECK(role.fifoRxCount() == 3);
+    CHECK(role.readFifoByte() == 0x90);
+    CHECK(role.readFifoByte() == 64);
+    CHECK(role.readFifoByte() == 100);
+    CHECK(role.readFifoByte() == -1);
+}
diff --git a/packages/native/test/audio/SameBoySerialTiming.test.cpp b/packages/native/test/audio/SameBoySerialTiming.test.cpp
index 6e32af72..ab2c1533 100644
--- a/packages/native/test/audio/SameBoySerialTiming.test.cpp
+++ b/packages/native/test/audio/SameBoySerialTiming.test.cpp
@@ -109,5 +109,7 @@ TEST_CASE("SameBoy rebases serial offsets past the block end", "[audio][sameboy]
     // Untouched this block (offset was past the end), rebased to 100 for the next.
     REQUIRE(sys->serialIn_.size() == 1);
     CHECK(sys->serialIn_.front().offset == 100);
-    CHECK(sys->serialIn_.front().byte == 0x42);
+    std::uint8_t front = 0;
+    REQUIRE(sys->peekSerialByte(front));
+    CHECK(front == 0x42);
 }
diff --git a/packages/native/test/engine-graph/SystemNode.test.cpp b/packages/native/test/engine-graph/SystemNode.test.cpp
index 83e40829..160c0e19 100644
--- a/packages/native/test/engine-graph/SystemNode.test.cpp
+++ b/packages/native/test/engine-graph/SystemNode.test.cpp
@@ -43,12 +43,14 @@
 #include "GraphDocs.hpp"
 #include "NodeRig.hpp"
 #include "graph/CanonicalJson.hpp"
+#include "host/engine/EngineInvoker.hpp"
 #include "system/mesen/MesenAudioDevice.hpp"
 #include "system/mesen/MesenGbaSystem.hpp"
 #include "system/mesen/MesenNesConfig.hpp"
 #include "system/mesen/MesenNesSystem.hpp"
 #include "system/mesen/MesenSmsConfig.hpp"
 #include "system/mesen/MesenSmsSystem.hpp"
+#include "system/InputCapacity.hpp"
 #include "system/InputTypes.hpp"
 #include "system/sameboy/SameBoyConfig.hpp"
 #include "system/sameboy/SameBoySystem.hpp"
@@ -84,6 +86,21 @@ std::unique_ptr<MesenNesSystem> nes(SystemId id, std::uint32_t exportMode) {
     return sys;
 }
 
+// The same core with BlipToaster's reliable link on. The in-repo image predates the capability byte the core reads at
+// activate (its SIG says none), so this copy sets the bit: what the case measures is the host's side of the link (the
+// frame window, the message buffer, the sync), which runs inside a block whether or not the ROM answers, not the ROM's
+// receiver. Nothing else in the image moves.
+std::unique_ptr<MesenNesSystem> linkedNes(SystemId id) {
+    MesenNesConfig cfg;
+    cfg.romPath = RP_BLIPTOASTER_ROM_PATH;
+    auto rom = readFile(RP_BLIPTOASTER_ROM_PATH);
+    REQUIRE(rom.size() > 0x10 + 19);
+    rom[0x10 + 18] = 0x01;  // NesN8FifoRole::romAdvertisesLink: the SIG's capability byte, bit 0
+    auto sys = std::make_unique<MesenNesSystem>(id, cfg, rom);
+    sys->onActivate(kRate);
+    return sys;
+}
+
 // smsggdj reads its options from the battery at boot: an empty SMDJ4 image with the v1 block ('C' 'F', then PAL / SYNC /
 // video / FM off, then their sum) at $3F60 of a 32 KB image, SYNC off so the song free-runs (smsSyncSong.ts).
 std::vector<std::uint8_t> smsBattery() {
@@ -705,7 +722,9 @@ TEST_CASE("a linked Game Boy takes its inputs in prepare, before its group steps
         if (!send) continue;
         REQUIRE(graph1->serialIn_.size() == 1);
         REQUIRE(d1->serialIn_.size() == 1);
-        CHECK(graph1->serialIn_.front().byte == 0x90);
+        std::uint8_t front = 0;
+        REQUIRE(graph1->peekSerialByte(front));
+        CHECK(front == 0x90);
         CHECK(graph1->serialIn_.front().offset == d1->serialIn_.front().offset);
         CHECK(graph1->pendingButtons_.empty());
         CHECK(d1->pendingButtons_.empty());
@@ -2535,14 +2554,93 @@ std::uint32_t capturedFrames(SystemBase& core) {
     return 0;
 }
 
-// One whole block as the node runs it, with the step phase inside the counted window: prepare, step until the core has
-// filled the block, finish. This is the promise's real shape - a block with emulated frames, video frames, controller
-// polls and notifications in it - so it counts everything the block does, not just the drain, and the block it counts
-// is proven filled above.
+// Host input a counted block carries: the calls a system node's applyInputs makes at the top of its prepare, before
+// the core's prepareForBlock, at the frames the node's input queues tag each message with. A core's ingress queues are
+// sized at activation from what a block's input can deliver, so staging a block's input grows nothing.
+struct NoInput {
+    void operator()(SystemBase&, std::uint32_t) const {}
+};
+
+// A NES's block of host input: MIDI at its frames (notes and clocks, which the N8 FIFO's role queues for the sample
+// offset each message carries and, under BlipToaster's reliable link, frames as it releases them), a raw run on the
+// node's `bytes` input, a message longer than a MidiEvent holds down that same raw door, and a burst of presses.
+struct NesInput {
+    void operator()(SystemBase& core, std::uint32_t frames) const {
+        const auto note = [&core](std::uint32_t frame, std::uint8_t status, std::uint8_t key) {
+            ::MidiEvent ev{};
+            ev.frame = frame;
+            ev.size  = 3;
+            ev.data[0] = status;
+            ev.data[1] = key;
+            ev.data[2] = 100;
+            core.onMidi(&ev, 1);
+        };
+        note(frames / 8, 0x90, 60);
+        note(frames / 2, 0x80, 60);
+        ::MidiEvent clock{};
+        clock.frame   = frames / 4;
+        clock.size    = 1;
+        clock.data[0] = 0xF8;
+        core.onMidi(&clock, 1);
+        clock.frame = frames / 3;
+        core.onMidi(&clock, 1);
+        const std::uint8_t raw[] = {0x90, 67, 100, 0xF8, 0x80, 67, 0x00};
+        core.pushCoreBytes(frames / 8, raw, sizeof raw, false);
+        // A settings upload's shape: a SysEx far longer than a MidiEvent holds, so it takes the node's raw door.
+        std::array<std::uint8_t, 256> sysex{};
+        sysex.front() = 0xF0;
+        sysex.back()  = 0xF7;
+        core.pushCoreBytes(frames / 3, sysex.data(), sysex.size(), false);
+        for (int k = 0; k < 12; ++k)
+            core.pressButton(static_cast<std::uint8_t>(k % 8), (k % 2) == 0);
+    }
+};
+
+// A Master System's block of host input: the transport's level byte at its sample offset, and a burst of presses.
+struct SmsInput {
+    void operator()(SystemBase& core, std::uint32_t frames) const {
+        const std::uint8_t levels[] = {0xFF, 0xF7, 0x7F, 0x77};
+        core.pushCoreBytes(frames / 2, levels, sizeof levels, false);
+        for (int k = 0; k < 12; ++k)
+            core.pressButton(static_cast<std::uint8_t>(k % 8), (k % 2) == 0);
+    }
+};
+
+// A Game Boy's block of host input: the MIDI mGB hears, on its serial port, a message's bytes at its message's frame,
+// and a burst of presses.
+struct GameBoyInput {
+    void operator()(SystemBase& core, std::uint32_t frames) const {
+        // A run of notes and clocks, each message its bytes at its own frame, past a library deque's node (64 entries
+        // at this queue's size) and past what one block's presses leave queued, so a queue that grew on the block
+        // thread would show here even though it need not grow every block.
+        for (int m = 0; m < 24; ++m) {
+            const std::uint8_t status = (m % 4 == 3) ? 0xF8 : (m % 2 == 0 ? 0x90 : 0x80);
+            const std::uint8_t bytes[] = {status, static_cast<std::uint8_t>(48 + m), 100};
+            for (const std::uint8_t byte : bytes) core.pushSerialIn(frames * std::uint32_t(m + 1) / 32, byte);
+        }
+        for (int k = 0; k < 64; ++k)
+            core.pressButton(static_cast<std::uint8_t>(k % 8), (k % 2) == 0);
+    }
+};
+
+// A GBA's block of host input: presses, the one door its core offers.
+struct GbaInput {
+    void operator()(SystemBase& core, std::uint32_t) const {
+        for (int k = 0; k < 12; ++k)
+            core.pressButton(static_cast<std::uint8_t>(k % 8), (k % 2) == 0);
+    }
+};
+
+// One whole block as the node runs it, with the step phase inside the counted window: the block's host input, prepare,
+// step until the core has filled the block, finish. This is the promise's real shape - a block with emulated frames,
+// video frames, controller polls, host input and notifications in it - so it counts everything the block does, not just
+// the drain, and the block it counts is proven filled above.
+template <class Stage>
 rp::test::AllocationCount wholeBlock(SystemBase& core, std::uint32_t frames, const std::vector<float*>& lanes,
-                                     bool count) {
+                                     bool count, Stage stage) {
     for (float* const lane : lanes) std::fill_n(lane, frames, 0.0f);
     if (count) rp::test::beginCountingAllocations(true);
+    stage(core, frames);
     core.prepareForBlock(info(frames));
     while (core.stepIfBelowTarget(frames)) {
     }
@@ -2572,13 +2670,14 @@ std::uint64_t ramHash(SystemBase& core) {
 // is steady state, and it is where a device's state is sized (the first poll reserves it; see ClearState) and where a
 // control manager that registers a port of its own has done so. That one-time sizing is warm-up, like a core's first
 // frame.
-void checkWholeBlocksAreFree(SystemBase& core, const std::vector<float*>& lanes) {
+template <class Stage>
+void checkWholeBlocksAreFree(SystemBase& core, const std::vector<float*>& lanes, Stage stage) {
     core.pressButton(0, true);
-    wholeBlock(core, 1024, lanes, false);
+    wholeBlock(core, 1024, lanes, false, stage);
     core.pressButton(0, false);
-    for (int b = 0; b < 3; ++b) wholeBlock(core, 1024, lanes, false);
+    for (int b = 0; b < 3; ++b) wholeBlock(core, 1024, lanes, false, stage);
     for (const std::uint32_t frames : kRaggedBlocks) {
-        const auto counted = wholeBlock(core, frames, lanes, true);
+        const auto counted = wholeBlock(core, frames, lanes, true, stage);
         INFO(frames << " frames: " << counted.allocations << " allocations, " << counted.frees << " frees");
         REQUIRE(counted.allocations == 0);
         REQUIRE(counted.frees == 0);
@@ -2768,15 +2867,32 @@ TEST_CASE("a Mesen core's audio path drains without allocating at every block si
 TEST_CASE("a warmed core's whole block allocates and frees nothing, emulation included",
           "[engine-graph][system-node][alloc]") {
     // The promise's other half: a REAL block, with the step phase in the counted window, so the video frames, controller
-    // polls and notifications a block carries are counted too. Each core is built directly (as the cases above build
-    // theirs), warmed by 4 x 1024 frames and then run at every ragged size, and every core is expected to be free at
-    // every size.
+    // polls and notifications a block carries are counted too, and with the block's host input staged inside the window
+    // (the calls a node's applyInputs makes before the core's prepareForBlock), so what a MIDI run, a byte run and a
+    // press burst do to a core's ingress queues is counted rather than assumed. Each core is built directly (as the
+    // cases above build theirs), warmed by 4 x 1024 frames and then run at every ragged size, and every core is expected
+    // to be free at every size.
     SECTION("NES, mixed") {
         auto core = nes(1, 0);
         core->reserveAudio(8192);  // what a node's activate does with its plan's maximum
         Lanes lanes(2);
-        checkWholeBlocksAreFree(*core, {lanes.ptrs[0], lanes.ptrs[1]});
+        const auto acceptedBefore = core->inputAccepted();
+        checkWholeBlocksAreFree(*core, {lanes.ptrs[0], lanes.ptrs[1]}, NesInput{});
         CHECK(core->audioRingOverflow() == 0);
+        CHECK(core->inputAccepted() > acceptedBefore);  // the staged input really reached the core
+    }
+    SECTION("NES, its reliable link on") {
+        // The link's own block-thread work: every message the host sends is framed as the block's emulation reaches its
+        // offset (a window entry, a sync ahead of the first frame, a re-send when one is due), and a run too long for a
+        // frame takes the raw door.
+        auto core = linkedNes(1);
+        core->reserveAudio(8192);
+        Lanes lanes(2);
+        const auto acceptedBefore = core->inputAccepted();
+        checkWholeBlocksAreFree(*core, {lanes.ptrs[0], lanes.ptrs[1]}, NesInput{});
+        CHECK(core->inputAccepted() > acceptedBefore);
+        const auto stats = core->coreTransportStats();
+        CHECK(stats.linkFrames > 0);  // the frames really were built and sent, not skipped by a link that stayed off
     }
     SECTION("NES, tap armed, its pins wired") {
         // The tap's own per-block work: the pin blips, the per-stream resamplers and the capture rings, every block.
@@ -2788,19 +2904,24 @@ TEST_CASE("a warmed core's whole block allocates and frees nothing, emulation in
             tap.push_back(lanes.ptrs[k]);
             tap.push_back(lanes.ptrs[k]);
         }
-        checkWholeBlocksAreFree(*core, tap);
+        const auto acceptedBefore = core->inputAccepted();
+        checkWholeBlocksAreFree(*core, tap, NesInput{});
         CHECK(core->tapOverflow() == 0);
+        CHECK(core->inputAccepted() > acceptedBefore);
     }
     SECTION("NES, through the engine") {
         // The same promise at the level the host sees: the whole graph block, so the system node's own per-block work
-        // (params, settings, the tap's sync, its message digests) is inside the window too. The core is the Engine's,
-        // so it publishes a whole savestate every 0.5 s: that capture allocates in Mesen's serializer, and is charged a
-        // full interval here before the window, which stays under one interval (~12.5 k of 24 k frames).
+        // (params, settings, the tap's sync, its message digests) is inside the window too, together with the core-side
+        // release of what the host staged through the node's own doors: a short message down pushCoreBytes, and a
+        // settings-length SysEx down the raw door the node uses for one longer than a MidiEvent holds. The core is the
+        // Engine's, so it publishes a whole savestate every 0.5 s: that capture allocates in Mesen's serializer, and is
+        // charged a full interval here before the window, which stays under one interval (~12.5 k of 24 k frames).
         Engine e(kRate);
         rp::host::test::Roms roms;
         auto systems = std::vector<rp::host::test::DocSystem>{{1, rp::host::test::kNesType}};
         rp::host::test::withRoms(e, systems, roms);
         REQUIRE(e.setGraph(rp::host::test::graphRequest(rp::host::test::stereoDocument(systems), {1})).ok);
+        QueuedInvoker invoker(e);
         auto* const core = dynamic_cast<MesenNesSystem*>(e.findSystem(1));
         REQUIRE(core != nullptr);
         Lanes lanes(2);
@@ -2810,6 +2931,13 @@ TEST_CASE("a warmed core's whole block allocates and frees nothing, emulation in
             std::fill_n(lanes.ptrs[0], frames, 0.0f);
             std::fill_n(lanes.ptrs[1], frames, 0.0f);
             const std::uint64_t before = ramHash(*core);
+            // Staged between blocks, as a host stages it: what the node's inputs hold when the counted block begins.
+            const std::uint8_t note[] = {0x90, 64, 100, 0xF8, 0x80, 64, 0x00};
+            invoker.stageSystemMidi(1, std::vector<std::uint8_t>(note, note + sizeof note));
+            std::array<std::uint8_t, 192> sysex{};
+            sysex.front() = 0xF0;
+            sysex.back()  = 0xF7;
+            invoker.stageSystemMidi(1, std::vector<std::uint8_t>(sysex.begin(), sysex.end()));
             rp::test::beginCountingAllocations(true);
             e.processBlock(frames, lanes.ptrs.data(), 2);
             const auto counted = rp::test::endCountingAllocations();
@@ -2824,16 +2952,20 @@ TEST_CASE("a warmed core's whole block allocates and frees nothing, emulation in
         auto core = sms(1);
         core->reserveAudio(8192);
         Lanes lanes(2);
-        checkWholeBlocksAreFree(*core, {lanes.ptrs[0], lanes.ptrs[1]});
+        const auto acceptedBefore = core->inputAccepted();
+        checkWholeBlocksAreFree(*core, {lanes.ptrs[0], lanes.ptrs[1]}, SmsInput{});
+        CHECK(core->inputAccepted() > acceptedBefore);
     }
     SECTION("Game Boy") {
         // Both shapes: the mixed stream, and the four stem pairs a plan that wires ch1..ch4 hands it.
         auto core = mgb(1);
         core->reserveAudio(8192);
         Lanes mix(2);
-        checkWholeBlocksAreFree(*core, {mix.ptrs[0], mix.ptrs[1]});
+        const auto acceptedBefore = core->inputAccepted();
+        checkWholeBlocksAreFree(*core, {mix.ptrs[0], mix.ptrs[1]}, GameBoyInput{});
+        CHECK(core->inputAccepted() > acceptedBefore);
         Lanes stems(8);
-        checkWholeBlocksAreFree(*core, stems.ptrs);
+        checkWholeBlocksAreFree(*core, stems.ptrs, GameBoyInput{});
     }
     SECTION("GBA") {
         // A sibling fixture, so this leaves on a run without one (as CI's) rather than failing there.
@@ -2843,7 +2975,156 @@ TEST_CASE("a warmed core's whole block allocates and frees nothing, emulation in
         core->onActivate(kRate);
         core->reserveAudio(8192);
         Lanes lanes(2);
-        checkWholeBlocksAreFree(*core, {lanes.ptrs[0], lanes.ptrs[1]});
+        const auto acceptedBefore = core->inputAccepted();
+        checkWholeBlocksAreFree(*core, {lanes.ptrs[0], lanes.ptrs[1]}, GbaInput{});
+        CHECK(core->inputAccepted() > acceptedBefore);
+    }
+}
+
+TEST_CASE("the ingress capacities match the queue capacities ports.ts declares",
+          "[engine-graph][system-node][inputs]") {
+    // specs/02's "queue capacities" are TS's, and a core sizes its queues from InputCapacity.hpp: read the four
+    // declarations here so a change on either side is visible, rather than a silently mismatched bound.
+    const auto capacity = [](const std::string& name) {
+        const auto bytes = readFile(RP_PORTS_TS_PATH);
+        const std::string ts(bytes.begin(), bytes.end());
+        const std::string head = "export const " + name + ": QueueCapacity = {";
+        const auto at = ts.find(head);
+        REQUIRE(at != std::string::npos);
+        const auto open = ts.find('{', at);
+        const auto close = ts.find('}', open);
+        REQUIRE(open != std::string::npos);
+        REQUIRE(close != std::string::npos);
+        const std::string body = ts.substr(open + 1, close - open - 1);
+        const auto field = [&](const char* key) {
+            const auto p = body.find(std::string(key) + ":");
+            REQUIRE(p != std::string::npos);
+            return static_cast<std::uint32_t>(std::strtoul(body.c_str() + p + std::strlen(key) + 1, nullptr, 10));
+        };
+        return std::pair{field("messages"), field("bytes")};
+    };
+
+    const auto midi = capacity("CORE_MIDI");
+    CHECK(midi.first == rp::kMidiInput.messages);
+    CHECK(midi.second == rp::kMidiInput.bytes);
+    const auto serialIn = capacity("SERIAL_IN");
+    CHECK(serialIn.first == rp::kSerialInInput.messages);
+    CHECK(serialIn.second == rp::kSerialInInput.bytes);
+    const auto bytes = capacity("CORE_BYTES");
+    CHECK(bytes.first == rp::kBytesInput.messages);
+    CHECK(bytes.second == rp::kBytesInput.bytes);
+    const auto buttons = capacity("BUTTONS");
+    CHECK(buttons.first == rp::kButtonsInput);
+}
+
+TEST_CASE("a core's ingress queues drop what is past their bound, count it, and keep running",
+          "[engine-graph][system-node][input-drops]") {
+    // Each queue is sized from what a block's doors can deliver (InputCapacity.hpp). A push past that bound is dropped
+    // and counted rather than growing the queue on the block thread, and the core still runs the next block. The count
+    // is the core's inputDropped(); inputAccepted() is the other half, holding that the pushes before the bound landed.
+    const auto ran = [](SystemBase& core, const std::vector<float*>& lanes) {
+        wholeBlock(core, 1024, lanes, true, NoInput{});  // the REQUIRE inside holds the block was really filled
+    };
+
+    SECTION("NES presses") {
+        auto core = nes(1, 0);
+        core->reserveAudio(8192);
+        Lanes lanes(2);
+        for (std::uint32_t i = 0; i < rp::kPressInput + 3; ++i)
+            core->pressButton(static_cast<std::uint8_t>(i % 8), (i % 2) == 0);
+        CHECK(core->inputAccepted() == rp::kPressInput);
+        CHECK(core->inputDropped() == 3);
+        ran(*core, {lanes.ptrs[0], lanes.ptrs[1]});
+    }
+    SECTION("SMS presses") {
+        auto core = sms(1);
+        core->reserveAudio(8192);
+        Lanes lanes(2);
+        for (std::uint32_t i = 0; i < rp::kPressInput + 2; ++i)
+            core->pressButton(static_cast<std::uint8_t>(i % 8), (i % 2) == 0);
+        CHECK(core->inputAccepted() == rp::kPressInput);
+        CHECK(core->inputDropped() == 2);
+        ran(*core, {lanes.ptrs[0], lanes.ptrs[1]});
+    }
+    SECTION("GBA presses") {
+        const auto rom = resourceFixture("/roms/nanoloop287d.gba");
+        if (rom.empty()) return;
+        auto core = std::make_unique<MesenGbaSystem>(1, MesenGbaConfig{}, rom);
+        core->onActivate(kRate);
+        core->reserveAudio(8192);
+        Lanes lanes(2);
+        for (std::uint32_t i = 0; i < rp::kPressInput + 1; ++i)
+            core->pressButton(static_cast<std::uint8_t>(i % 8), (i % 2) == 0);
+        CHECK(core->inputAccepted() == rp::kPressInput);
+        CHECK(core->inputDropped() == 1);
+        ran(*core, {lanes.ptrs[0], lanes.ptrs[1]});
+    }
+    SECTION("Game Boy presses") {
+        auto core = mgb(1);
+        core->reserveAudio(8192);
+        Lanes lanes(2);
+        constexpr std::uint32_t kCapacity = rp::kButtonsInput * 16;
+        for (std::uint32_t i = 0; i < kCapacity + 4; ++i)
+            core->pressButton(static_cast<std::uint8_t>(i % 8), (i % 2) == 0);
+        CHECK(core->inputAccepted() == kCapacity);
+        CHECK(core->inputDropped() == 4);
+        ran(*core, {lanes.ptrs[0], lanes.ptrs[1]});
+    }
+    SECTION("Game Boy serial bytes") {
+        // The ring holds two blocks' bytes; one past it is refused, whatever its run's frame.
+        auto core = mgb(1);
+        core->reserveAudio(8192);
+        Lanes lanes(2);
+        constexpr std::uint32_t kCapacity = 2 * rp::kSerialInInput.bytes;
+        for (std::uint32_t i = 0; i < kCapacity + 1; ++i) core->pushSerialIn(0, static_cast<std::uint8_t>(i));
+        CHECK(core->inputAccepted() == kCapacity);
+        CHECK(core->inputDropped() == 1);
+        ran(*core, {lanes.ptrs[0], lanes.ptrs[1]});
+    }
+    SECTION("Game Boy serial runs") {
+        // Each byte at its own frame is a run; the run queue holds two blocks' messages (or one serialOut block), and
+        // the ring has room for all of them, so the 8257th is the queue's refusal.
+        auto core = mgb(1);
+        core->reserveAudio(8192);
+        Lanes lanes(2);
+        for (std::uint32_t i = 0; i < 8257; ++i) core->pushSerialIn(i, 0x90);
+        CHECK(core->inputAccepted() == 8256);
+        CHECK(core->inputDropped() == 1);
+        ran(*core, {lanes.ptrs[0], lanes.ptrs[1]});
+    }
+    SECTION("SMS levels") {
+        // A level is a byte. The queue holds the midi door's longest run plus the bytes door's block, so one level
+        // past that is dropped while the rest stay.
+        auto core = sms(1);
+        core->reserveAudio(8192);
+        Lanes lanes(2);
+        constexpr std::size_t kCapacity = std::size_t(rp::kMidiInput.bytes) + rp::kBytesInput.bytes;
+        const std::vector<std::uint8_t> levels(kCapacity + 1, 0x5A);
+        core->pushCoreBytes(0, levels.data(), levels.size(), false);
+        CHECK(core->inputAccepted() == kCapacity);
+        CHECK(core->inputDropped() == 1);
+        ran(*core, {lanes.ptrs[0], lanes.ptrs[1]});
+    }
+    SECTION("NES N8 pending runs") {
+        auto core = nes(1, 0);
+        core->reserveAudio(8192);
+        Lanes lanes(2);
+        constexpr std::size_t kCapacity = std::size_t(rp::kMidiInput.messages) + rp::kBytesInput.messages;
+        const std::uint8_t byte = 0xF8;
+        for (std::size_t i = 0; i < kCapacity + 1; ++i) core->pushCoreBytes(0, &byte, 1, false);
+        CHECK(core->inputAccepted() == kCapacity);
+        CHECK(core->inputDropped() == 1);
+        ran(*core, {lanes.ptrs[0], lanes.ptrs[1]});
+    }
+    SECTION("NES N8 pending bytes") {
+        auto core = nes(1, 0);
+        core->reserveAudio(8192);
+        Lanes lanes(2);
+        constexpr std::size_t kCapacity = std::size_t(rp::kMidiInput.bytes) + rp::kBytesInput.bytes;
+        const std::vector<std::uint8_t> bytes(kCapacity + 2, 0xF8);
+        core->pushCoreBytes(0, bytes.data(), bytes.size(), false);
+        CHECK(core->inputDropped() == kCapacity + 2);   // a run past the arena is dropped whole, as a graph queue's is
+        ran(*core, {lanes.ptrs[0], lanes.ptrs[1]});
     }
 }
 
@@ -3065,7 +3346,7 @@ std::vector<std::uint64_t> frameHashes(SystemBase& core, int blocks, std::uint32
     std::vector<std::uint64_t> hashes;
     Lanes lanes(2, frames);
     for (int b = 0; b < blocks; ++b) {
-        wholeBlock(core, frames, {lanes.ptrs[0], lanes.ptrs[1]}, false);
+        wholeBlock(core, frames, {lanes.ptrs[0], lanes.ptrs[1]}, false, NoInput{});
         hashes.push_back(frameHash(core));
     }
     return hashes;
````

- [ ] **Step 2: Run the tests to see them fail**

Run: `cmake -S . -B build && cmake --build build --target retroplug-engine-graph-test -j$(nproc)`
Expected: FAIL: the build stops on the headers this task adds: `fatal error: system/InputCapacity.hpp: No such file or directory` from `SystemNode.test.cpp`'s new include, and `util/FixedQueue.hpp` from the new `FixedQueue.test.cpp` (Step 3 creates both). The `inputAccepted()` / `inputDropped()` seam and `peekSerialByte` are later steps too; `Stage` and `RP_PORTS_TS_PATH` are Step 1's own, so they do not appear. The configure is needed here: this task adds a test source and the `RP_PORTS_TS_PATH` definition.

With the extended window in place and no fix, the case fails at its first counted window (33 frames) on every console;
the report's table, before and after:

- NES, mixed (raw N8 mode): `7 allocations, 7 frees` before, 0 and 0 after.
- NES, reliable link on: `11 / 7` before, 0 and 0 after.
- NES, tap armed: `7 / 7` before, 0 and 0 after.
- NES, through the engine: `1 / 4` before, 0 and 0 after.
- SMS: `1 / 1` before, 0 and 0 after.
- Game Boy (press + serial): `4 / 2` before, 0 and 0 after.

A scratch table on the unfixed tree, each input kind alone on a warmed core over one counted 1024-frame block,
attributed them: the Mesen press vector `12 presses: 5 allocations, 4 frees` (the growth steps 1, 2, 4, 8, 16), the
Game Boy's press deque `64 presses/block carried: 2 / 1 at 33 frames`, its serial deque 3 and 0 (2 and 1 of the 4 and
2 window), the N8 role's raw deque 1 and 1 up to 5 and 5, the SMS role's levels 1 and 1, and the Engine section's 1
and 4 was the FIFO's wire and fifo_a.

The stage's one real bug was found by its own case rather than by review, and its red is the shape to expect from the
rebase fix: a case that enqueues a MIDI message at frame 900 first and an 8-byte raw run at frame 600 second, pumps to
the block end (the sort puts the raw run first), rebases, and reads each payload back, failed under a forward
compaction in queue order with `217 == 144`, `173 == 64` and `190 == 100` before the ascending-order compaction, and
passes 16 assertions after it.

- [ ] **Step 3: The queue types, and the capacities the spec gives**

`FixedQueue<T>` is one reserved vector and a head index: `push` refuses past `capacity()` and counts the refusal,
`pop` moves the head and compacts a consumed prefix, and it has `dropped()`; `FixedByteRing` is consumed front-first
only, so the front byte a bit shift reads is the ring's head; `stableSortEntries` is a stable in-place insertion sort.
`InputCapacity.hpp` holds the capacities `spec/02`'s table gives the outputs that feed a system node's inputs (a
script node's `midi` and `serialIn` 4096 messages / 72 KiB, its `bytes` 256 / 1 KiB, its `buttons` 64, a Game Boy's
`serialOut` 8256 / 8256) plus `kPressInput` and `kCommandRingCapacity`, and a case reads `ports.ts` back against them:

Create `packages/native/src/util/FixedQueue.hpp`:

````cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

// The queues a block feeds, held in storage sized before the block runs (docs/modular-graph-phase-c4-design.md
// section 4). A core's ingress queues are filled on the block thread, so they may not grow there: each is reserved to
// the most a block can deliver when the core is built, and a push that does not fit is dropped and counted rather than
// allocating. The bound is the one a graph queue has (a push that does not fit is dropped and counted).
//
// One buffer and a head index rather than a deque: a deque allocates a node on its first push (or at construction, on
// libstdc++), and allocates another as it grows, on whichever thread first reaches that depth.

namespace rp {

template <class T>
class FixedQueue {
public:
    FixedQueue() = default;
    explicit FixedQueue(std::size_t capacity) { reserve(capacity); }

    // Room for `capacity` entries. Growth only, and only where allocating is allowed (a core's construction, an
    // activation): a queue never grows while a block runs.
    void reserve(std::size_t capacity) {
        if (capacity <= capacity_) return;
        buffer_.reserve(capacity);
        capacity_ = capacity;
    }
    std::size_t capacity() const { return capacity_; }
    std::size_t size() const { return buffer_.size() - head_; }
    bool empty() const { return head_ == buffer_.size(); }

    // Append one entry. False, appending nothing, when the queue holds capacity() entries: the caller decides what a
    // dropped push means for its stream. A queue whose overflow is silent reads dropped() instead.
    bool push(const T& value) {
        if (buffer_.size() == capacity_) {
            if (head_ == 0) {
                ++dropped_;
                return false;
            }
            // The consumed prefix takes the room the new entry needs, so the buffer's own size stays the bound.
            buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(head_));
            head_ = 0;
            // Unreachable after the erase above (head_ > 0 left at least one slot free), kept as a guard: a queue that
            // did reach capacity here must still refuse rather than grow past its bound.
            if (buffer_.size() == capacity_) {
                ++dropped_;
                return false;
            }
        }
        buffer_.push_back(value);
        return true;
    }

    T&       front() { return buffer_[head_]; }
    const T& front() const { return buffer_[head_]; }
    T&       back() { return buffer_.back(); }
    const T& back() const { return buffer_.back(); }
    void     pop() {
        ++head_;
        if (head_ == buffer_.size()) clear();
    }
    void clear() {
        buffer_.clear();
        head_ = 0;
    }

    std::uint64_t dropped() const { return dropped_; }

    // The live entries, oldest first, for a pass over what one block queued (a rebase, a sort before the drain).
    T*       begin() { return buffer_.data() + head_; }
    T*       end() { return buffer_.data() + buffer_.size(); }
    const T* begin() const { return buffer_.data() + head_; }
    const T* end() const { return buffer_.data() + buffer_.size(); }
    T&       operator[](std::size_t i) { return buffer_[head_ + i]; }
    const T& operator[](std::size_t i) const { return buffer_[head_ + i]; }

private:
    std::vector<T> buffer_;
    std::size_t    head_     = 0;
    std::size_t    capacity_ = 0;
    std::uint64_t  dropped_  = 0;
};

// A queue of bytes read from the front, held in a ring of fixed capacity: a byte stream that is only ever consumed in
// the order it arrived (a serial port's input), so nothing has to move and nothing can grow.
class FixedByteRing {
public:
    FixedByteRing() = default;
    explicit FixedByteRing(std::size_t capacity) { reserve(capacity); }

    void reserve(std::size_t capacity) {
        if (capacity <= capacity_) return;
        bytes_.resize(capacity);
        capacity_ = capacity;
    }
    std::size_t capacity() const { return capacity_; }
    std::size_t size() const { return size_; }
    bool        empty() const { return size_ == 0; }
    std::uint8_t front() const { return bytes_[head_]; }

    bool push(std::uint8_t byte) {
        if (size_ == capacity_ || capacity_ == 0) {
            ++dropped_;
            return false;
        }
        bytes_[(head_ + size_) % capacity_] = byte;
        ++size_;
        return true;
    }
    void pop() {
        if (size_ == 0) return;
        head_ = (head_ + 1) % capacity_;
        --size_;
    }
    void clear() {
        head_ = 0;
        size_ = 0;
    }
    std::uint64_t dropped() const { return dropped_; }

private:
    std::vector<std::uint8_t> bytes_;
    std::size_t               head_     = 0;
    std::size_t               size_     = 0;
    std::size_t               capacity_ = 0;
    std::uint64_t             dropped_  = 0;
};

// Stable sort of a block's queued entries by offset, in place and allocating nothing: std::stable_sort takes a
// temporary buffer, which the block thread may not have. The queue is built from one or two feeds, each already in
// offset order, so the insertion pass costs about one run of the second feed rather than a sort (the same trade
// Engine::Staging::sortByFrame makes for the host's staged MIDI). A producer that stamps decreasing offsets is
// allowed, though, and the N8 role's queue holds 4352 runs: that worst case is about 9 M entry moves, quadratic on
// the block thread, which is why the feeds' order is the thing to keep.
template <class T, class Less>
void stableSortEntries(T* first, T* last, Less less) {
    for (T* i = first + 1; i < last; ++i)
        for (T* j = i; j > first && less(*j, *(j - 1)); --j) std::swap(*j, *(j - 1));
}

}  // namespace rp
````

Create `packages/native/src/system/InputCapacity.hpp`:

````cpp
#pragma once

#include <cstdint>

// What a system node's inputs can be handed in one block. A wire carries its source's queue and an input declares none
// (spec/02-native-host.md, "Host I/O, the system nodes' inputs and queue capacities"), so the bound is the capacity of
// the outputs that feed a system: a script node's serialIn and midi outputs hold 4096 messages and 72 KiB, its bytes
// output 256 messages and 1 KiB, its buttons output 64 messages of two bytes each, and rp.host.midi-in's output (which
// reaches a system through a routing node) holds less than the midi one. A Game Boy's serialOut output, fixed at 8256
// messages and 8256 bytes, can wire into any system's serialIn or bytes input too. The script numbers are the ones
// packages/retroplug/src/script/nodes/ports.ts declares, and none of them depend on the block size.
//
// A core sizes its ingress queues from them when it is built, so a block's input grows nothing on the block thread
// (docs/modular-graph-phase-c4-design.md section 4).
namespace rp {

struct InputCapacity {
    std::uint32_t messages;
    std::uint32_t bytes;
};

inline constexpr InputCapacity kMidiInput{4096, 73728};
inline constexpr InputCapacity kSerialInInput{4096, 73728};
inline constexpr InputCapacity kBytesInput{256, 1024};
inline constexpr InputCapacity kSerialOutInput{8256, 8256};  // the Game Boy's serialOut (SystemNodes.cpp)
inline constexpr std::uint32_t kButtonsInput = 64;

// Presses reach a core from two doors before the same prepareForBlock clears its queue: a block's buttons input, and
// Engine::applyCommand(PressButton) on GraphHost's command ring (GraphHost::commandCapacity()). A press queue is sized
// for both, so a host that queues a burst between two blocks does not lose the tail.
inline constexpr std::uint32_t kCommandRingCapacity = 256;
inline constexpr std::uint32_t kPressInput = kButtonsInput + kCommandRingCapacity;

}  // namespace rp
````

- [ ] **Step 4: The consoles' queues**

The three Mesen consoles' press queues become a `FixedQueue` of `kPressInput` (320 = `kButtonsInput` 64 +
`GraphHost::commandCapacity()` 256, the two doors a block can press through, with a `static_assert` holding the ring's
256 to the host's, and `prepareForBlock` already empties it every block). A Game Boy's presses become a `FixedQueue`
of 1024 (sixteen blocks of a saturated buttons input, because a press is scheduled 10 ms into the future and carried
across blocks) and its serial-in becomes runs in a `FixedQueue` over a 147456-byte `FixedByteRing` (two blocks of
`serialIn` or one `serialOut` block), with `peekSerialByte` replacing the old `frontSerialByte`, which returned a
fabricated 0 on an empty queue. `SystemBase` gains the `inputAccepted()` / `inputDropped()` seam, which each core sums
its queues into:

````diff
diff --git a/packages/native/src/system/SystemBase.hpp b/packages/native/src/system/SystemBase.hpp
index 48b0d091..e80b0291 100644
--- a/packages/native/src/system/SystemBase.hpp
+++ b/packages/native/src/system/SystemBase.hpp
@@ -138,6 +138,12 @@ public:
     // with no such transport. Control-thread read; the NES implementation is mutex-guarded.
     virtual CoreTransportStats coreTransportStats() { return {}; }
 
+    // What this core's ingress queues have taken and dropped since it was built: bytes for the serial / N8
+    // byte queues, presses for a button queue. A test reads these to hold that staged input reached the core and
+    // that a push past a queue's bound is counted rather than growing it. Zero for a core with no inputs.
+    virtual std::uint64_t inputAccepted() const { return 0; }
+    virtual std::uint64_t inputDropped() const { return 0; }
+
     // Audio-thread: enqueue a button transition.
     virtual void pressButton(std::uint8_t /*button*/, bool /*down*/) {}
 
diff --git a/packages/native/src/system/mesen/MesenGbaSystem.cpp b/packages/native/src/system/mesen/MesenGbaSystem.cpp
index 8ab56a29..49ec5d02 100644
--- a/packages/native/src/system/mesen/MesenGbaSystem.cpp
+++ b/packages/native/src/system/mesen/MesenGbaSystem.cpp
@@ -213,7 +213,7 @@ void MesenGbaSystem::setGainDb(float dB, bool immediate) {
 }
 
 void MesenGbaSystem::pressButton(std::uint8_t button, bool down) {
-    pendingButtons_.push_back({ button, down });
+    if (pendingButtons_.push({ button, down })) ++pressesAccepted_;
 }
 
 namespace {
diff --git a/packages/native/src/system/mesen/MesenGbaSystem.hpp b/packages/native/src/system/mesen/MesenGbaSystem.hpp
index 8b4ef030..74f97e2f 100644
--- a/packages/native/src/system/mesen/MesenGbaSystem.hpp
+++ b/packages/native/src/system/mesen/MesenGbaSystem.hpp
@@ -5,10 +5,12 @@
 #include <string>
 #include <vector>
 
+#include "system/InputCapacity.hpp"
 #include "system/SystemBase.hpp"
 #include "system/mesen/MesenGbaConfig.hpp"
 #include "transport/FrameBufferTriple.hpp"
 #include "util/ExpSmoother.hpp"
+#include "util/FixedQueue.hpp"
 
 class Emulator;
 class MesenAudioDevice;
@@ -49,6 +51,8 @@ public:
     // Mesen's native GbaController::Buttons ordering at apply time.
     // Applied at the top of the next onProcess.
     void pressButton(std::uint8_t button, bool down) override;
+    std::uint64_t inputAccepted() const override { return pressesAccepted_; }
+    std::uint64_t inputDropped() const override { return pendingButtons_.dropped(); }
 
     FrameBufferTriple* framebuffer() override { return &frames_; }
 
@@ -113,9 +117,11 @@ private:
 
     // Pending GBA button transitions. The byte is the GbaButton wire value
     // (Right=0..Start=7, L=8, R=9); remapped to Mesen's native
-    // GbaController::Buttons enum in onProcess.
+    // GbaController::Buttons enum in onProcess. Sized to the most a block's buttons input can carry, and cleared at
+    // each prepare, so a block's presses never make it grow (docs/modular-graph-phase-c4-design.md section 4).
     struct PendingGbaButton { std::uint8_t button; bool down; };
-    std::vector<PendingGbaButton>     pendingButtons_;
+    rp::FixedQueue<PendingGbaButton>  pendingButtons_{rp::kPressInput};
+    std::uint64_t                     pressesAccepted_ = 0;
 
     // This system's private firmware directory: which one it owns, and where it ended up once a BIOS
     // was actually installed there (empty when none was). Private per INSTANCE because Mesen's own
diff --git a/packages/native/src/system/mesen/MesenNesSystem.cpp b/packages/native/src/system/mesen/MesenNesSystem.cpp
index a650c3ef..9f84200e 100644
--- a/packages/native/src/system/mesen/MesenNesSystem.cpp
+++ b/packages/native/src/system/mesen/MesenNesSystem.cpp
@@ -403,7 +403,7 @@ void MesenNesSystem::setChannelExportMode(std::uint32_t mode) {
 }
 
 void MesenNesSystem::pressButton(std::uint8_t button, bool down) {
-    pendingButtons_.push_back({ button, down });
+    if (pendingButtons_.push({ button, down })) ++pressesAccepted_;
 }
 
 void MesenNesSystem::onMidi(const ::MidiEvent* events, std::uint32_t count) {
@@ -435,6 +435,15 @@ CoreTransportStats MesenNesSystem::coreTransportStats() {
     return out;
 }
 
+std::uint64_t MesenNesSystem::inputAccepted() const {
+    return pressesAccepted_ + (n8Role_ ? n8Role_->acceptedBytes() : 0);
+}
+
+std::uint64_t MesenNesSystem::inputDropped() const {
+    if (!n8Role_) return pendingButtons_.dropped();
+    return pendingButtons_.dropped() + n8Role_->pendingDropped() + n8Role_->fifoStats().droppedBytes;
+}
+
 namespace {
 NesController::Buttons toNesButton(std::uint8_t b) {
     switch (static_cast<NesButton>(b)) {
diff --git a/packages/native/src/system/mesen/MesenNesSystem.hpp b/packages/native/src/system/mesen/MesenNesSystem.hpp
index 51320bf5..d17f0e7d 100644
--- a/packages/native/src/system/mesen/MesenNesSystem.hpp
+++ b/packages/native/src/system/mesen/MesenNesSystem.hpp
@@ -5,10 +5,12 @@
 #include <memory>
 #include <vector>
 
+#include "system/InputCapacity.hpp"
 #include "system/SystemBase.hpp"
 #include "system/mesen/MesenNesConfig.hpp"
 #include "transport/FrameBufferTriple.hpp"
 #include "util/ExpSmoother.hpp"
+#include "util/FixedQueue.hpp"
 
 class Emulator;
 class NesSoundMixer;
@@ -64,6 +66,8 @@ public:
     // last drain. Empty before activation or when the ROM has sent nothing.
     std::vector<std::uint8_t> drainCoreBytes() override;
     CoreTransportStats coreTransportStats() override;
+    std::uint64_t inputAccepted() const override;
+    std::uint64_t inputDropped() const override;
 
     // Audio-thread: queue a NES button transition. The byte is reinterpreted
     // as NesButton (Right/Left/Up/Down/A/B/Select/Start, positions 0..7).
@@ -191,7 +195,9 @@ private:
     // Pending NES button transitions (pos 0..7 from NesButton enum). Applied
     // at the top of onProcess via NesController::SetBitValue. Single-shot —
     // we don't bother spreading across the block since NES input polling
-    // happens once per frame, not per sample.
+    // happens once per frame, not per sample. Sized to the most a block's buttons input can carry, and cleared at
+    // each prepare, so a block's presses never make it grow (docs/modular-graph-phase-c4-design.md section 4).
     struct PendingNesButton { std::uint8_t button; bool down; };
-    std::vector<PendingNesButton>     pendingButtons_;
+    rp::FixedQueue<PendingNesButton>  pendingButtons_{rp::kPressInput};
+    std::uint64_t                     pressesAccepted_ = 0;
 };
diff --git a/packages/native/src/system/mesen/MesenSmsSystem.cpp b/packages/native/src/system/mesen/MesenSmsSystem.cpp
index cbf545eb..c6bb46c8 100644
--- a/packages/native/src/system/mesen/MesenSmsSystem.cpp
+++ b/packages/native/src/system/mesen/MesenSmsSystem.cpp
@@ -368,7 +368,15 @@ void MesenSmsSystem::setGainDb(float dB, bool immediate) {
 }
 
 void MesenSmsSystem::pressButton(std::uint8_t button, bool down) {
-    pendingButtons_.push_back({ button, down });
+    if (pendingButtons_.push({ button, down })) ++pressesAccepted_;
+}
+
+std::uint64_t MesenSmsSystem::inputAccepted() const {
+    return pressesAccepted_ + syncRole_.acceptedLevels();
+}
+
+std::uint64_t MesenSmsSystem::inputDropped() const {
+    return pendingButtons_.dropped() + syncRole_.pendingDropped();
 }
 
 void MesenSmsSystem::pushCoreBytes(std::uint32_t frame, const std::uint8_t* data, std::size_t size,
diff --git a/packages/native/src/system/mesen/MesenSmsSystem.hpp b/packages/native/src/system/mesen/MesenSmsSystem.hpp
index b9195a7d..1a859863 100644
--- a/packages/native/src/system/mesen/MesenSmsSystem.hpp
+++ b/packages/native/src/system/mesen/MesenSmsSystem.hpp
@@ -6,11 +6,13 @@
 #include <string>
 #include <vector>
 
+#include "system/InputCapacity.hpp"
 #include "system/SystemBase.hpp"
 #include "system/mesen/MesenSmsConfig.hpp"
 #include "system/mesen/roles/SmsSyncRole.hpp"
 #include "transport/FrameBufferTriple.hpp"
 #include "util/ExpSmoother.hpp"
+#include "util/FixedQueue.hpp"
 
 class Emulator;
 class MesenAudioDevice;
@@ -53,6 +55,8 @@ public:
     // name index (SmsButton wire byte); remapped to Mesen's native
     // SmsController::Buttons ordering at apply time. Applied in prepareForBlock.
     void pressButton(std::uint8_t button, bool down) override;
+    std::uint64_t inputAccepted() const override;
+    std::uint64_t inputDropped() const override;
 
     // Audio-thread: schedule host-transport sync levels at intra-block sample offsets. Each byte is
     // a controller-port level word (see SmsSyncRole for the encoding), NOT a protocol byte - this
@@ -207,7 +211,9 @@ private:
 
     // Pending button transitions. The byte is the SmsButton wire value
     // (Right=0..Start=7); remapped to Mesen's native SmsController::Buttons
-    // enum in prepareForBlock.
+    // enum in prepareForBlock. Sized to the most a block's buttons input can carry, and cleared at each prepare, so a
+    // block's presses never make it grow (docs/modular-graph-phase-c4-design.md section 4).
     struct PendingSmsButton { std::uint8_t button; bool down; };
-    std::vector<PendingSmsButton>     pendingButtons_;
+    rp::FixedQueue<PendingSmsButton>  pendingButtons_{rp::kPressInput};
+    std::uint64_t                     pressesAccepted_ = 0;
 };
diff --git a/packages/native/src/system/sameboy/SameBoySystem.cpp b/packages/native/src/system/sameboy/SameBoySystem.cpp
index 1a80110b..a09f50ac 100644
--- a/packages/native/src/system/sameboy/SameBoySystem.cpp
+++ b/packages/native/src/system/sameboy/SameBoySystem.cpp
@@ -286,6 +286,7 @@ void SameBoySystem::onActivate(double sampleRate) {
     // doesn't carry over half-shifted bytes. Serial-out capture's mode is set
     // separately, by the system node, via setSerialOutCapture.
     serialIn_.clear();
+    serialBytes_.clear();
     serialBitsRemaining_ = 0;
     serialOutByte_ = 0;
     serialOutBits_ = 0;
@@ -490,17 +491,24 @@ bool SameBoySystem::nextSerialInBit() {
         serialBitsRemaining_ = 8;
     }
 
-    const std::uint8_t byte = serialIn_.front().byte;
+    const std::uint8_t byte = serialBytes_.front();
     const int bitIndex = serialBitsRemaining_ - 1; // MSB-first: 7..0
     const bool bit = ((byte >> bitIndex) & 1u) != 0;
 
     --serialBitsRemaining_;
     if (serialBitsRemaining_ == 0) {
-        serialIn_.pop_front();
+        popSerialByte();
     }
     return bit;
 }
 
+// Take the front byte of the serial queue: its run loses one, and the run goes when its last byte has gone. The ring's
+// head is always the front run's first byte, since both are consumed front-first only.
+void SameBoySystem::popSerialByte() {
+    serialBytes_.pop();
+    if (--serialIn_.front().count == 0) serialIn_.pop();
+}
+
 void SameBoySystem::setModel(SameBoyModel model) {
     if (config_.model == model) return;
     config_.model = model;
@@ -598,7 +606,7 @@ void SameBoySystem::pressButton(std::uint8_t button, bool down) {
     std::uint32_t offset = 0;
     if (!pendingButtons_.empty())
         offset = pendingButtons_.back().offset + buttonSpacingSamples_;
-    pendingButtons_.push_back(PendingButton{offset, static_cast<GameboyButton>(button), down});
+    if (pendingButtons_.push(PendingButton{offset, static_cast<GameboyButton>(button), down})) ++pressesAccepted_;
 }
 
 std::vector<ChannelStream> SameBoySystem::channelLayout() const {
@@ -711,7 +719,7 @@ bool SameBoySystem::stepIfBelowTarget(std::uint32_t framesNeeded) {
            pendingButtons_.front().offset <= audioFrameCount_) {
         const auto& pb = pendingButtons_.front();
         GB_set_key_state(gb_, static_cast<GB_key_t>(pb.button), pb.down);
-        pendingButtons_.pop_front();
+        pendingButtons_.pop();
     }
 
     // Slave-mode serial pump. mGB and other "MIDI-listener" ROMs hold SC at
@@ -727,8 +735,8 @@ bool SameBoySystem::stepIfBelowTarget(std::uint32_t framesNeeded) {
         serialIn_.front().offset <= audioFrameCount_) {
         const std::uint8_t sc = GB_safe_read_memory(gb_, 0xFF02);
         if ((sc & 0x80) && !(sc & 0x01)) {
-            const std::uint8_t byte = serialIn_.front().byte;
-            serialIn_.pop_front();
+            const std::uint8_t byte = serialBytes_.front();
+            popSerialByte();
             for (int b = 7; b >= 0; --b) {
                 GB_serial_set_data_bit(gb_, ((byte >> b) & 1u) != 0);
             }
diff --git a/packages/native/src/system/sameboy/SameBoySystem.hpp b/packages/native/src/system/sameboy/SameBoySystem.hpp
index dd479ae5..f9f63b45 100644
--- a/packages/native/src/system/sameboy/SameBoySystem.hpp
+++ b/packages/native/src/system/sameboy/SameBoySystem.hpp
@@ -2,16 +2,17 @@
 
 #include <array>
 #include <cstdint>
-#include <deque>
 #include <memory>
 #include <vector>
 
+#include "system/InputCapacity.hpp"
 #include "system/InputTypes.hpp"
 #include "system/SystemBase.hpp"
 #include "system/sameboy/SameBoyConfig.hpp"
 #include "system/sameboy/SameBoyConstants.hpp"
 #include "transport/FrameBufferTriple.hpp"
 #include "util/ExpSmoother.hpp"
+#include "util/FixedQueue.hpp"
 
 // Forward declare the C type so this header doesn't drag <gb.h> into the
 // rest of the codebase.
@@ -59,8 +60,34 @@ public:
     // Queue a raw byte for the GB serial port, scheduled at sample offset
     // `frame` within the block. Drained MSB-first by nextSerialInBit() / the
     // slave-mode pump in stepIfBelowTarget(), neither before `frame` is reached.
+    // A message's bytes share its frame, so they extend one run; a byte with no room left in the run queue or the
+    // byte ring is refused and counted, as a graph queue's overflow is.
     void pushSerialIn(std::uint32_t frame, std::uint8_t byte) override {
-        serialIn_.push_back({ frame, byte });
+        if (!serialIn_.empty() && serialIn_.back().offset == frame && serialBytes_.size() < serialBytes_.capacity()) {
+            serialBytes_.push(byte);
+            ++serialIn_.back().count;
+            ++serialInAccepted_;
+            return;
+        }
+        if (serialIn_.size() < serialIn_.capacity() && serialBytes_.size() < serialBytes_.capacity() &&
+            serialIn_.push({frame, 1})) {
+            serialBytes_.push(byte);
+            ++serialInAccepted_;
+            return;
+        }
+        ++serialInDropped_;
+    }
+    // What this core's ingress queues have taken and dropped since it was built (SystemBase::inputAccepted /
+    // inputDropped): serial-in bytes and button presses. A block can only deliver what the queues are sized for, so a
+    // drop is zero unless a core is fed outside a plan's queues.
+    std::uint64_t inputAccepted() const override { return serialInAccepted_ + pressesAccepted_; }
+    std::uint64_t inputDropped() const override { return serialInDropped_ + pendingButtons_.dropped(); }
+    // The byte the serial queue would deliver next. False, touching nothing, when it holds none: the queued runs only
+    // name their bytes' offsets, so this is the test seam that holds a serial input reached the core.
+    bool peekSerialByte(std::uint8_t& byte) const {
+        if (serialBytes_.empty()) return false;
+        byte = serialBytes_.front();
+        return true;
     }
 
     FrameBufferTriple* framebuffer() override { return &frames_; }
@@ -201,15 +228,18 @@ public:
     bool                      activated_  = false;
     double                    sampleRate_ = 44100.0;
 
-    // Pending button transitions, ordered by sample offset within the block.
-    // Spread across each block so a press+release pair doesn't collapse to
-    // zero duration (which the SameBoy joypad debouncer would miss).
+    // Pending button transitions, ordered by sample offset. Spread across each block so a press+release pair doesn't
+    // collapse to zero duration (which the SameBoy joypad debouncer would miss): each press is scheduled 10 ms after
+    // the one before it, so what a block adds can still be queued several blocks later. The bound is a block's buttons
+    // input sixteen times over (about ten seconds of scheduled presses at 48 kHz), and a press beyond it is refused
+    // and counted rather than growing the queue on the block thread.
     struct PendingButton {
         std::uint32_t offset;   // samples from block start
         GameboyButton button;   // reinterpreted from the uint8_t opcode
         bool          down;
     };
-    std::deque<PendingButton> pendingButtons_;
+    rp::FixedQueue<PendingButton> pendingButtons_{rp::kButtonsInput * 16};   // at least kPressInput: see above
+    std::uint64_t                 pressesAccepted_ = 0;
 
     // Default per-press duration (samples) used to space queued transitions.
     // Mirrors the old code's "10 ms at sampleRate" default.
@@ -222,20 +252,35 @@ public:
     std::vector<SameBoySystem*> linkPeers_;
     bool                        bitToSend_ = false;
 
-    // Bytes queued by roles / host MIDI-in for the GB serial port, each tagged
-    // with the sample offset it should not be delivered before. Drained one bit
-    // at a time (MSB first) by `nextSerialInBit()` from the SameBoy serial-end
-    // callback, or whole-byte by the slave-mode pump in stepIfBelowTarget() —
-    // both gated on `offset <= audioFrameCount_` so a burst of host MIDI keeps
-    // its intra-block timing instead of collapsing to the block start.
-    // `serialBitsRemaining_` counts how many bits are left in the current front
-    // byte (8 → fresh byte, 0 → next call pops & reloads).
+    // Bytes queued by roles / host MIDI-in for the GB serial port, each run of them (a message's bytes share its
+    // frame) tagged with the sample offset it should not be delivered before. Drained one bit at a time (MSB first) by
+    // `nextSerialInBit()` from the SameBoy serial-end callback, or whole-byte by the slave-mode pump in
+    // stepIfBelowTarget(), both gated on `offset <= audioFrameCount_` so a burst of host MIDI keeps its
+    // intra-block timing instead of collapsing to the block start. `serialBitsRemaining_` counts how many bits are left
+    // in the current front byte (8 → fresh byte, 0 → next call pops & reloads).
+    //
+    // The bytes are held in a fixed ring and the runs in a fixed queue, each sized to what a block's serialIn input can
+    // deliver, so a block's MIDI never grows one: a run is a message's bytes at one frame (a slot per distinct frame),
+    // and the ring holds their bytes (docs/modular-graph-phase-c4-design.md section 4).
     struct PendingSerial {
         std::uint32_t offset;   // samples from block start
-        std::uint8_t  byte;
+        std::uint32_t count;    // bytes at this offset, front-first in serialBytes_
     };
-    std::deque<PendingSerial> serialIn_;
-    int                       serialBitsRemaining_ = 0;
+    // Two blocks of a script serialIn stream, or one of a Game Boy serialOut (8256, the larger single block), in
+    // runs; the same two blocks' bytes in the ring, or a serialOut block's. See InputCapacity.hpp.
+    static constexpr std::size_t kSerialRuns =
+        rp::kSerialOutInput.messages > 2 * rp::kSerialInInput.messages
+            ? rp::kSerialOutInput.messages
+            : 2 * rp::kSerialInInput.messages;
+    static constexpr std::size_t kSerialBytes =
+        rp::kSerialOutInput.bytes > 2 * rp::kSerialInInput.bytes
+            ? rp::kSerialOutInput.bytes
+            : 2 * rp::kSerialInInput.bytes;
+    rp::FixedQueue<PendingSerial> serialIn_{kSerialRuns};
+    rp::FixedByteRing             serialBytes_{kSerialBytes};
+    std::uint64_t                 serialInAccepted_   = 0;
+    std::uint64_t                 serialInDropped_    = 0;
+    int                           serialBitsRemaining_ = 0;
 
     // Outgoing-serial byte accumulator. Bits arrive MSB-first from the GB;
     // on every 8th bit the assembled byte is appended to serialOutLog_ for the
@@ -254,6 +299,8 @@ public:
 private:
     // Size stereoAccum_ and chanAccum_ for blockFrames(), keeping what they hold (growth only).
     void sizePerBlockBuffers();
+    // Take the front byte of the serial queue (its run loses one, and the run goes when its last byte has).
+    void popSerialByte();
 
     ExpSmoother gainSmoother_;
 };
````

- [ ] **Step 5: The roles, the reliable link, and the FIFO**

The N8 role's two deques become one run queue of 4352 descriptors over one 74752-byte arena, shared by both feeds and
both modes: `queue()` appends a run, `pumpUntil` sorts the descriptors (not the bytes) and drains them, and `rebase`
shifts offsets and compacts the arena **in ascending byte order**, not queue order, which is the bug above (the live
run indices go into a preallocated array, one per slot, so the pass still allocates nothing). The SMS role's levels
become a `FixedQueue` of 74752 (`kMidiInput.bytes + kBytesInput.bytes`, because a message longer than a `MidiEvent`
reaches the raw door at up to 72 KiB), and its `stable_sort` becomes `stableSortEntries`. The reliable link's sender
window becomes 48 inline frames of `3 + kMaxMessage` bytes, the message being parsed a `kMaxMessage + 2` array with a
"too long" flag (one longer was never framed before either), the pending sync two bytes; a full window still gives up
its oldest frame and re-syncs, so it refuses nothing and has no dropped count. The FIFO's wire is a `FixedQueue` of a
block's host input and fifo_a's delivered bytes are reserved to `2 * _rxBound` (`_rxBound` = 78849: a block's input
plus `FILE_READ_MAX` plus one), because `pop` only compacts once the head passes half the vector's size; a reply past
that hard bound is dropped and counted even under the permissive (`_rxCapacity == 0`) profile, which now means no
modelled depth rather than no bound:

````diff
diff --git a/packages/native/src/system/mesen/NesEverdriveFifo.hpp b/packages/native/src/system/mesen/NesEverdriveFifo.hpp
index 62b4b355..c334c97f 100644
--- a/packages/native/src/system/mesen/NesEverdriveFifo.hpp
+++ b/packages/native/src/system/mesen/NesEverdriveFifo.hpp
@@ -3,7 +3,6 @@
 #include <algorithm>
 #include <array>
 #include <atomic>
-#include <queue>
 #include <vector>
 #include <mutex>
 #include <fstream>
@@ -16,6 +15,8 @@
 
 #include "Core/NES/INesMemoryHandler.h"
 
+#include "system/InputCapacity.hpp"
+#include "util/FixedQueue.hpp"
 #include "util/ProcessId.hpp"
 
 // EverDrive N8 Pro FIFO emulator. Maps to NES address space at $40F0 (data)
@@ -157,10 +158,11 @@ namespace rp {
 	// -----------------------------------------------------------------------
 	// FIFO fidelity profile: how closely the host->NES queue behaves like the cartridge's
 	// -----------------------------------------------------------------------
-	// The default is PERMISSIVE - unbounded and delivered instantly - because that is what every
-	// existing test was written against. The device is neither, and the difference hides whole
-	// classes of bug: a ROM that can never lose a byte never has its recovery path exercised, and
-	// one fed a whole chunk atomically is never starved mid-structure the way a real wire starves it.
+	// The default is PERMISSIVE - no modelled depth and delivered instantly - because that is what every
+	// existing test was written against. (The storage still has its hard bound; see _rxBound.) The device is
+	// neither, and the difference hides whole classes of bug: a ROM that can never lose a byte never has its
+	// recovery path exercised, and one fed a whole chunk atomically is never starved mid-structure the way a
+	// real wire starves it.
 	// A test opts in to the hardware profile through the "mesen" role's `fifo` field.
 	//
 	// The measured constants, kept here rather than in a shell script:
@@ -190,8 +192,8 @@ namespace rp {
 		// fifo_a's delivered bytes, oldest first. A vector read from a moving head rather than a std::queue, whose
 		// deque allocates a node on its first push: an Edio reply is pushed WHOLE before the ROM reads a byte of it (a
 		// file read is up to 4 KB), so a block that answers an SD command would allocate inside the block. The vector
-		// keeps its capacity, and what has been read is dropped once the head has moved past half of it, so it stays
-		// bounded by the largest reply rather than by every byte ever delivered.
+		// keeps its capacity (see the constructor for the bound), and what has been read is dropped once the head has
+		// moved past half of it, so it stays bounded by the largest reply rather than by every byte ever delivered.
 		struct DeliveredBytes {
 			std::vector<uint8_t> bytes;
 			std::size_t          head = 0;
@@ -217,6 +219,10 @@ namespace rp {
 		};
 
 		DeliveredBytes _rxQueue;
+		// fifo_a's LIVE storage bound: what one block's host input can add to it (the inputs a system node's midi and
+		// bytes ports feed this FIFO through), plus the largest single reply. The vector is reserved to twice this,
+		// because its consumed prefix can sit dead below head until pop() compacts it (see the constructor).
+		static constexpr std::size_t _rxBound = std::size_t(kMidiInput.bytes) + kBytesInput.bytes + FILE_READ_MAX + 1;
 		std::mutex _mutex;
 
 		// What fifo_a's RAM holds, by slot. On the device the FIFO is a ring over 2048 bytes of block
@@ -291,17 +297,19 @@ namespace rp {
 		uint8_t _lastStatus = 0;
 
 		// ----- The wire (host -> MCU -> fifo_a) -------------------------
-		// What the host has handed over but the wire has not yet carried. Unbounded on purpose: it
-		// stands for the host's own outbound buffer, which has no cartridge counterpart and must not
-		// drop - a byte lost HERE would be the emulation inventing a failure the device never has.
-		// Bytes cross into _rxQueue at _wireBytesPerSecond (see pumpWire), and only there can they be
-		// dropped, by the queue being full.
-		std::queue<uint8_t> _wireQueue;
+		// What the host has handed over but the wire has not yet carried: it stands for the host's own
+		// outbound buffer, which has no cartridge counterpart. Bytes cross into _rxQueue at
+		// _wireBytesPerSecond (see pumpWire), and only there can they be dropped, by the queue being full.
+		// The buffer is sized to one block's host input (kMidiInput + kBytesInput, what a system node's midi
+		// and bytes inputs can deliver), so a block never grows it; a push beyond that is dropped and counted
+		// with the RX queue's drops, which no host that outruns the modelled wire by more than a block does.
+		rp::FixedQueue<uint8_t> _wireQueue{std::size_t(kMidiInput.bytes) + kBytesInput.bytes};
 		uint32_t _wireBytesPerSecond = 0;   // 0 = instant: the wire is not modelled
 		uint64_t _wireLastCycle      = 0;
 		double   _wireCredit         = 0.0; // fractional bytes carried over between pumps
 
-		// fifo_a's depth, and what it has cost. 0 = unbounded (the permissive default).
+		// fifo_a's modelled depth, and what it has cost. 0 = no modelled depth (the permissive default); its storage
+		// is still bounded by _rxBound.
 		uint32_t _rxCapacity   = 0;
 		uint64_t _droppedBytes = 0;         // cumulative, never cleared by a read: a test samples it
 
@@ -331,10 +339,15 @@ namespace rp {
 			// The parser's and fifo_a's buffers live as long as the FIFO; without this they grow (push_back / reserve)
 			// the first time a block carries a bigger command frame, file block or reply than any before it, which the
 			// block thread may not do. A file read's reply is one resp byte plus up to the 4096 the SDK reads per
-			// command, so that is what a reply buffer is given; a larger one (a big directory listing) still grows it.
+			// command, so that is what a reply buffer is given; a larger one (a big directory listing) is past
+			// _rxBound, and is dropped and counted rather than growing it, even under the permissive profile.
 			_params.reserve(MAX_COMMAND_PARAMS);
 			_writeBlock.reserve(ACK_BLOCK_SIZE);
-			_rxQueue.bytes.reserve(FILE_READ_MAX + 1);
+			// A block's host input, plus the largest reply: fifo_a holds everything a block delivered that the ROM has
+			// not read yet, and one reply behind it. Twice the live bound, because pop() only erases the consumed
+			// prefix once head passes half of bytes.size(): the vector can hold a dead prefix nearly as large as the
+			// live tail, and a reserve of exactly _rxBound would reallocate inside a block when both are near it.
+			_rxQueue.bytes.reserve(2 * _rxBound);
 		}
 
 		// Set the host directory that stands in for the SD card root ("/"). Must be set before the ROM
@@ -383,7 +396,7 @@ namespace rp {
 			_wireLastCycle = _cartCycles ? _cartCycles() : 0;
 		}
 
-		// How faithfully the host->NES queue behaves like fifo_a. `depth` 0 = unbounded,
+		// How faithfully the host->NES queue behaves like fifo_a. `depth` 0 = no modelled depth,
 		// `bytesPerSecond` 0 = instant delivery; both together are the permissive default every
 		// existing test was written against. MesenNesSystem resolves the "mesen" role's `fifo`
 		// profile (and any explicit override) into this pair at activate.
@@ -517,7 +530,7 @@ namespace rp {
 			// Carry what is already owed before adding to the queue, so an idle gap between pushes is
 			// the host's gap rather than credit the wire banks up and then bursts through.
 			pumpWire();
-			_wireQueue.push(byte);
+			if (!_wireQueue.push(byte)) ++_droppedBytes;   // the wire's buffer is a block's worth (see _wireQueue)
 			if (fifoTraceEnabled())
 				std::fprintf(stderr, "[fifo] midi %02X (wire=%zu depth=%zu) @%llu\n",
 				             byte, _wireQueue.size(), _rxQueue.size(), static_cast<unsigned long long>(_cartCycles ? _cartCycles() : 0));
@@ -539,8 +552,7 @@ namespace rp {
 		void clearRx() {
 			std::lock_guard<std::mutex> lock(_mutex);
 			_rxQueue.clear();
-			std::queue<uint8_t> emptyWire;
-			_wireQueue.swap(emptyWire);
+			_wireQueue.clear();
 			_wireCredit = 0.0;
 			_wireLastCycle = _cartCycles ? _cartCycles() : 0;
 		}
@@ -585,11 +597,14 @@ namespace rp {
 		// accounting artefact of the USB link, and reproducing the number by fitting to it would be
 		// inventing a mechanism rather than modelling one.
 		void pushRx(uint8_t byte) {
-			if (_rxCapacity != 0 && _rxQueue.size() >= _rxCapacity) {
+			// The profile's depth when one is modelled, else the bound the queue is reserved to (a block's host input
+			// plus one reply): the reserve is a hard bound, so fifo_a never grows on the block thread either.
+			const std::size_t limit = _rxCapacity != 0 ? std::min<std::size_t>(_rxCapacity, _rxBound) : _rxBound;
+			if (_rxQueue.size() >= limit) {
 				++_droppedBytes;
 				if (fifoTraceEnabled())
-					std::fprintf(stderr, "[fifo] DROP %02X (full at %u, dropped=%llu)\n",
-					             byte, _rxCapacity, static_cast<unsigned long long>(_droppedBytes));
+					std::fprintf(stderr, "[fifo] DROP %02X (full at %zu, dropped=%llu)\n",
+					             byte, limit, static_cast<unsigned long long>(_droppedBytes));
 				return;
 			}
 			_rxQueue.push(byte);
@@ -598,8 +613,8 @@ namespace rp {
 		}
 
 		// `$40F0` on an empty fifo_a: the slot the read pointer rests on, which is the write pointer's
-		// (see _rxRam). Holds even when the queue is unbounded and outran the ring: the slot has been
-		// rewritten since, exactly as the device's would have been.
+		// (see _rxRam). Holds even when the modelled depth is unbounded and the queue outran the ring: the slot has
+		// been rewritten since, exactly as the device's would have been.
 		uint8_t staleRead() const { return _rxRam[_rxWriteSlot]; }
 
 		// Carry bytes from the host's outbound buffer into fifo_a at the modelled wire rate. Without
diff --git a/packages/native/src/system/mesen/roles/NesN8FifoRole.cpp b/packages/native/src/system/mesen/roles/NesN8FifoRole.cpp
index 08490df3..1c61826e 100644
--- a/packages/native/src/system/mesen/roles/NesN8FifoRole.cpp
+++ b/packages/native/src/system/mesen/roles/NesN8FifoRole.cpp
@@ -1,14 +1,14 @@
 #include "system/mesen/roles/NesN8FifoRole.hpp"
 
-#include <algorithm>
 #include <cstdio>
+#include <cstring>
 
 #include "Core/NES/NesConsole.h"
 #include "Core/NES/NesConstants.h"
 #include "Core/NES/NesCpu.h"
 #include "Core/NES/NesMemoryManager.h"
 
-NesN8FifoRole::NesN8FifoRole() = default;
+NesN8FifoRole::NesN8FifoRole() { bytes_.reserve(kPendingBytes); }
 NesN8FifoRole::~NesN8FifoRole() = default;
 
 void NesN8FifoRole::onAttach(NesConsole& console) {
@@ -32,24 +32,28 @@ void NesN8FifoRole::onAttach(NesConsole& console) {
     std::fprintf(stderr, "[NesN8FifoRole] FIFO attached at $40F0/$40F1/$40FF\n");
 }
 
+void NesN8FifoRole::queue(std::uint32_t offset, const std::uint8_t* data, std::size_t count) {
+    if (count == 0) return;
+    const auto start = static_cast<std::uint32_t>(bytes_.size());
+    if (bytes_.size() + count > kPendingBytes ||
+        !pending_.push({ offset, start, static_cast<std::uint32_t>(count) })) {
+        pendingDropped_ += count;
+        return;
+    }
+    bytes_.insert(bytes_.end(), data, data + count);
+    acceptedBytes_ += count;
+    needsSort_ = true;  // a second feed may enqueue after this one with an earlier offset - re-sort before draining
+}
+
 void NesN8FifoRole::pushBytes(std::uint32_t offset, const std::uint8_t* data, std::size_t count, bool flush) {
     if (data == nullptr || count == 0) return;
-    // A barrier message drops everything still in flight first, in BOTH queues, so it can't be preceded
-    // by bytes belonging to the stream position it replaces.
+    // A barrier message drops everything still in flight first, so it can't be preceded by bytes belonging to the
+    // stream position it replaces.
     if (flush) flushAll();
-    if (link_) {
-        // A link-capable ROM ignores unframed MIDI, and this is how a long SysEx (anything over a MidiEvent's 4
-        // bytes) or a multi-message run arrives - so it is framed like onMidi's, when pumpUntil releases it.
-        pendingMsgs_.push_back({ offset, std::vector<std::uint8_t>(data, data + count) });
-        needsSort_ = true;
-        return;
-    }
-    // Queue (don't deliver) each byte at `offset`. Contiguous + ordered, so pumpUntil releases the whole
-    // run together once its offset is reached. No framing/cap — the raw twin of onMidi.
-    for (std::size_t b = 0; b < count; ++b) {
-        pending_.push_back({ offset, data[b] });
-    }
-    needsSort_ = true;  // a second feed may enqueue after MIDI with an earlier offset — re-sort before draining
+    // One run per message, whichever way it is delivered: the bytes go into the FIFO in order as their offset comes due
+    // (raw mode), or are framed and sent as one message (the link), which is why a long SysEx or a multi-message run
+    // survives the link's framing.
+    queue(offset, data, count);
 }
 
 void NesN8FifoRole::enableLink(rp::N8ReliableSender::Timing timing) {
@@ -87,66 +91,88 @@ void NesN8FifoRole::serviceLink() {
 
 void NesN8FifoRole::onMidi(const ::MidiEvent* events, std::uint32_t count) {
     if (events == nullptr || count == 0) return;
-    if (link_) {
-        // Framed by pumpUntil as it releases them: each event's bytes become frames (plus a sync ahead of the first) at
-        // the event's offset, numbered in release order.
-        for (std::uint32_t i = 0; i < count; ++i) {
-            const auto& ev = events[i];
-            const std::uint32_t size = ev.size > ::MidiEvent::kDataSize ? ::MidiEvent::kDataSize : ev.size;
-            pendingMsgs_.push_back({ ev.frame, std::vector<std::uint8_t>(ev.data, ev.data + size) });
-        }
-        needsSort_ = true;
-        return;
-    }
-    // MIDI-framed adapter over pushBytes: each event's bytes share ev.frame (clamped to the inline size).
+    // Each event is one MIDI-framed run at its frame: pushed into the FIFO in order when it comes due (raw mode), or
+    // framed as one message then (the link). The link ignores anything longer than a MidiEvent holds, and the node sends
+    // such a run down pushCoreBytes instead.
     for (std::uint32_t i = 0; i < count; ++i) {
         const auto& ev = events[i];
         const std::uint32_t size = ev.size > ::MidiEvent::kDataSize ? ::MidiEvent::kDataSize : ev.size;
-        pushBytes(ev.frame, ev.data, size);
+        queue(ev.frame, ev.data, size);
     }
 }
 
+std::size_t NesN8FifoRole::pendingCount() const {
+    if (link_) return 0;  // a message waits unframed and unframed bytes are not what a caller counts
+    std::size_t bytes = 0;
+    for (const auto& run : pending_) bytes += run.count;
+    return bytes;
+}
+
 void NesN8FifoRole::pumpUntil(std::uint32_t sampleOffset) {
     if (needsSort_) {
-        // Stable so bytes of one message (same offset, contiguous) stay together + in order, and link messages at one
-        // offset keep their enqueue order (the sender's parser relies on it: running status, a SysEx split across
-        // pushes) with any carried in from an earlier block ahead of them. A split SysEx holds together only at ONE
-        // offset: halves at different offsets with a message released between them are cut by the sender's parser
-        // (no caller splits one that way). Runs once per block (all enqueuing precedes the step loop), after which
-        // this is a plain front-drain.
-        std::stable_sort(pending_.begin(), pending_.end(),
-                         [](const PendingByte& a, const PendingByte& b) { return a.offset < b.offset; });
-        std::stable_sort(pendingMsgs_.begin(), pendingMsgs_.end(),
-                         [](const PendingMsg& a, const PendingMsg& b) { return a.offset < b.offset; });
+        // Stable so two runs at one offset keep their enqueue order (the sender's parser relies on it: running status,
+        // a SysEx split across pushes) with any carried in from an earlier block ahead of them, and so a run's own bytes
+        // stay together. Runs once per block (all enqueuing precedes the step loop), after which this is a plain
+        // front-drain. A split SysEx holds together only at ONE offset: halves at different offsets with a message
+        // released between them are cut by the sender's parser (no caller splits one that way).
+        rp::stableSortEntries(pending_.begin(), pending_.end(),
+                              [](const PendingRun& a, const PendingRun& b) { return a.offset < b.offset; });
         needsSort_ = false;
     }
     while (!pending_.empty() && pending_.front().offset <= sampleOffset) {
-        fifo_.pushByte(pending_.front().byte);
-        pending_.pop_front();
-    }
-    // Link mode: a message is framed the moment it is released, so its sequence number is the next one the ROM sees -
-    // whichever feed queued it, in whatever order, and however many blocks it was carried by rebase.
-    while (link_ && !pendingMsgs_.empty() && pendingMsgs_.front().offset <= sampleOffset) {
-        const auto& m = pendingMsgs_.front();
-        const std::int64_t now = nowNs();
-        link_->send(m.bytes.data(), m.bytes.size(), [&](const std::uint8_t* data, std::size_t n, int seq) {
-            for (std::size_t b = 0; b < n; ++b) fifo_.pushByte(data[b]);
-            if (seq >= 0) {
-                link_->onSent(seq, now);
-                if (linkDueCycle_ == kNoDue) linkDueCycle_ = 0;   // a frame in flight: its timeout needs arming
-            }
-        });
-        pendingMsgs_.pop_front();
+        const PendingRun run = pending_.front();
+        const std::uint8_t* const data = bytes_.data() + run.start;
+        if (link_) {
+            // A message is framed the moment it is released, so its sequence number is the next one the ROM sees -
+            // whichever feed queued it, in whatever order, and however many blocks it was carried by rebase.
+            const std::int64_t now = nowNs();
+            link_->send(data, run.count, [&](const std::uint8_t* bytes, std::size_t n, int seq) {
+                for (std::size_t b = 0; b < n; ++b) fifo_.pushByte(bytes[b]);
+                if (seq >= 0) {
+                    link_->onSent(seq, now);
+                    if (linkDueCycle_ == kNoDue) linkDueCycle_ = 0;   // a frame in flight: its timeout needs arming
+                }
+            });
+        } else {
+            // Raw mode: every byte into the FIFO, in order, as the run comes due.
+            for (std::uint32_t b = 0; b < run.count; ++b) fifo_.pushByte(data[b]);
+        }
+        pending_.pop();
     }
     // Called before every instruction, so this is the link's clock tick: one compare unless work is due.
     if (linkDueCycle_ != kNoDue && cycles_ && cycles_() >= linkDueCycle_) serviceLink();
 }
 
 void NesN8FifoRole::rebase(std::uint32_t frames) {
-    for (auto& e : pending_) {
-        e.offset = (e.offset > frames) ? e.offset - frames : 0;
+    for (auto& run : pending_) {
+        run.offset = (run.offset > frames) ? run.offset - frames : 0;
+    }
+    // Hand back the bytes of the runs that were released. They sit in front of the live ones in `bytes_`, but the
+    // live runs are in queue order, which a sort by offset may have permuted, so the arena's bytes are not in queue
+    // order: compacting front-to-back in queue order would overwrite a run it has not copied yet. Order the live run
+    // indices by where their bytes are instead, and move each run in that order; the index array is reserved with the
+    // queue, so this pass allocates nothing at the block's end.
+    const std::size_t liveRuns = pending_.size();
+    if (liveRuns == 0) {
+        bytes_.clear();
+        return;
+    }
+    for (std::size_t i = 0; i < liveRuns; ++i) rebaseOrder_[i] = static_cast<std::uint32_t>(i);
+    for (std::size_t i = 1; i < liveRuns; ++i) {
+        const std::uint32_t index = rebaseOrder_[i];
+        std::size_t j = i;
+        while (j > 0 && pending_[rebaseOrder_[j - 1]].start > pending_[index].start) {
+            rebaseOrder_[j] = rebaseOrder_[j - 1];
+            --j;
+        }
+        rebaseOrder_[j] = index;
     }
-    for (auto& m : pendingMsgs_) {
-        m.offset = (m.offset > frames) ? m.offset - frames : 0;
+    std::size_t live = 0;
+    for (std::size_t i = 0; i < liveRuns; ++i) {
+        auto& run = pending_[rebaseOrder_[i]];
+        if (run.start != live) std::memmove(bytes_.data() + live, bytes_.data() + run.start, run.count);
+        run.start = static_cast<std::uint32_t>(live);
+        live += run.count;
     }
+    bytes_.resize(live);
 }
diff --git a/packages/native/src/system/mesen/roles/NesN8FifoRole.hpp b/packages/native/src/system/mesen/roles/NesN8FifoRole.hpp
index b9ddae30..9b0a7e03 100644
--- a/packages/native/src/system/mesen/roles/NesN8FifoRole.hpp
+++ b/packages/native/src/system/mesen/roles/NesN8FifoRole.hpp
@@ -1,15 +1,17 @@
 #pragma once
 
+#include <array>
 #include <cstdint>
-#include <deque>
 #include <functional>
 #include <memory>
 #include <string>
 #include <vector>
 
+#include "system/InputCapacity.hpp"
 #include "system/mesen/NesEverdriveFifo.hpp"
 #include "transport/MidiTypes.hpp"
 #include "transport/N8ReliableLink.hpp"
+#include "util/FixedQueue.hpp"
 
 class NesConsole;
 
@@ -120,7 +122,7 @@ public:
 
     // Drop all queued (not-yet-delivered) bytes — on reset, to avoid stale MIDI after a state change. With the link
     // on, its window goes too and the next frame is preceded by a sync: the ROM's sequence may no longer be ours.
-    void clear() { pending_.clear(); pendingMsgs_.clear(); needsSort_ = false; if (link_) { link_->reset(); linkDueCycle_ = kNoDue; } }
+    void clear() { pending_.clear(); bytes_.clear(); needsSort_ = false; if (link_) { link_->reset(); linkDueCycle_ = kNoDue; } }
 
     // Drop BOTH queues: the not-yet-delivered bytes AND the delivered-but-unread ones sitting in the
     // FIFO's RX queue. `clear()` alone leaves the latter, which the ROM would still read. Used on a
@@ -129,8 +131,13 @@ public:
     void flushAll() { clear(); fifo_.clearRx(); }
 
     // Introspection / tests.
-    // Queued raw BYTES; link-mode messages wait unframed in pendingMsgs_ and are not counted (no caller needs them).
-    std::size_t pendingCount() const { return pending_.size(); }
+    // Queued raw BYTES; link-mode messages wait unframed and are not counted (no caller needs them).
+    std::size_t pendingCount() const;
+    // Bytes the queue dropped for want of room since this role was built. The queue holds what a block's midi and
+    // bytes inputs can deliver, so this is zero unless a role is fed outside a plan's queues.
+    std::uint64_t pendingDropped() const { return pendingDropped_; }
+    // Bytes the queue has accepted since this role was built, for a test that staged input reached it.
+    std::uint64_t acceptedBytes() const { return acceptedBytes_; }
     std::size_t fifoRxCount() { return fifo_.rxCount(); }
     // Write a byte to the FIFO as the ROM would (STA $40F0) - an Edio command to the MCU, e.g. a CMD_USB_WR ack.
     void writeFifoByte(std::uint8_t b) { fifo_.WriteRam(0x40F0, b); }
@@ -141,21 +148,32 @@ public:
     }
 
 private:
-    struct PendingByte {
+    // One feed's bytes at one frame: a MIDI event's bytes (which share its frame), or a raw message's. Held in one
+    // queue for both feeds and both modes, since only one of the two modes is ever in use: without the link the run's
+    // bytes are pushed into the FIFO as they come due, and with it the run is framed and sent as one message - so the
+    // sequence numbers follow release order, not the order the feeds queued them.
+    struct PendingRun {
         std::uint32_t offset;   // samples from block start
-        std::uint8_t  byte;
+        std::uint32_t start;    // first byte in bytes_
+        std::uint32_t count;
     };
-    std::deque<PendingByte> pending_;
-    // Link mode: whole messages wait here unframed, sorted by offset like the bytes, and pumpUntil frames each as it
-    // releases it - so the sequence numbers follow release order, not the order the feeds queued them.
-    // Invariant: in link mode pending_ stays empty (both feeds go here), so pumpUntil's raw-then-messages drain order
-    // never matters. A mid-stream enableLink (bytes already in pending_) would need to revisit that.
-    struct PendingMsg {
-        std::uint32_t             offset;
-        std::vector<std::uint8_t> bytes;
-    };
-    std::deque<PendingMsg>  pendingMsgs_;
-    bool                    needsSort_ = false;  // set on enqueue; pumpUntil stable-sorts by offset once
+    // Sized to what a block's midi and bytes inputs can deliver: a run a message, and its bytes here. The bound is
+    // also the FIFO's first two stages' (NesEverdriveFifo), so a block's host input fits all the way through.
+    static constexpr std::size_t kPendingRuns = std::size_t(rp::kMidiInput.messages) + rp::kBytesInput.messages;
+    static constexpr std::size_t kPendingBytes = std::size_t(rp::kMidiInput.bytes) + rp::kBytesInput.bytes;
+    rp::FixedQueue<PendingRun> pending_{kPendingRuns};
+    std::vector<std::uint8_t>  bytes_;             // the runs' bytes, in enqueue order; the reserve is the bound
+    // Live run indices, ascending by start, for rebase's compaction (sized with the queue, so that pass allocates
+    // nothing).
+    std::array<std::uint32_t, kPendingRuns> rebaseOrder_{};
+    std::uint64_t              pendingDropped_ = 0;
+    std::uint64_t              acceptedBytes_  = 0;
+    bool                       needsSort_ = false;  // set on enqueue; pumpUntil sorts by offset once
+
+    // Append `count` bytes at `offset` as one run. A block's input fits by the bounds above; a run that does not is
+    // dropped whole and counted, as a graph queue's overflow is.
+    void queue(std::uint32_t offset, const std::uint8_t* data, std::size_t count);
+
     rp::NesEverdriveFifo    fifo_;
 
     // The reliable link (null = raw MIDI). Its clock is the CPU's: cycles_ and clockHz_ come from onAttach.
diff --git a/packages/native/src/system/mesen/roles/SmsSyncRole.cpp b/packages/native/src/system/mesen/roles/SmsSyncRole.cpp
index 2464ae9f..d30f5c3f 100644
--- a/packages/native/src/system/mesen/roles/SmsSyncRole.cpp
+++ b/packages/native/src/system/mesen/roles/SmsSyncRole.cpp
@@ -1,7 +1,5 @@
 #include "system/mesen/roles/SmsSyncRole.hpp"
 
-#include <algorithm>
-
 #include "Core/SMS/SmsControlManager.h"
 #include "Core/SMS/SmsMemoryManager.h"
 
@@ -13,10 +11,10 @@ namespace { constexpr std::uint8_t kSyncPort = 1; }
 void SmsSyncRole::pushBytes(std::uint32_t offset, const std::uint8_t* data, std::size_t count, bool /*flush*/) {
     if (data == nullptr || count == 0) return;
     for (std::size_t i = 0; i < count; ++i) {
-        pending_.push_back({ offset, data[i] });
+        if (pending_.push({ offset, data[i] })) ++acceptedLevels_;
     }
     // A second feed could enqueue after this one with an earlier offset; pumpUntil re-sorts once
-    // before draining rather than keeping the deque ordered on every push.
+    // before draining rather than keeping the queue ordered on every push.
     needsSort_ = true;
 }
 
@@ -27,8 +25,8 @@ void SmsSyncRole::pumpUntil(std::uint32_t sampleOffset) {
         // Stable so levels sharing an offset keep enqueue order - the last one queued for a given
         // sample is the one that ends up on the line. Runs once per block (all enqueuing precedes
         // the step loop), after which this is a plain front-drain and the loop below is one compare.
-        std::stable_sort(pending_.begin(), pending_.end(),
-                         [](const PendingLevel& a, const PendingLevel& b) { return a.offset < b.offset; });
+        rp::stableSortEntries(pending_.begin(), pending_.end(),
+                              [](const PendingLevel& a, const PendingLevel& b) { return a.offset < b.offset; });
         needsSort_ = false;
     }
 
@@ -39,7 +37,7 @@ void SmsSyncRole::pumpUntil(std::uint32_t sampleOffset) {
         } else if (controlManager_) {
             controlManager_->SetExternalInput(kSyncPort, lastApplied_);
         }
-        pending_.pop_front();
+        pending_.pop();
     }
 }
 
diff --git a/packages/native/src/system/mesen/roles/SmsSyncRole.hpp b/packages/native/src/system/mesen/roles/SmsSyncRole.hpp
index 0591cedd..424d9b92 100644
--- a/packages/native/src/system/mesen/roles/SmsSyncRole.hpp
+++ b/packages/native/src/system/mesen/roles/SmsSyncRole.hpp
@@ -1,7 +1,9 @@
 #pragma once
 
 #include <cstdint>
-#include <deque>
+
+#include "system/InputCapacity.hpp"
+#include "util/FixedQueue.hpp"
 
 class SmsControlManager;
 class SmsMemoryManager;
@@ -58,7 +60,11 @@ public:
     void onDetach() { controlManager_ = nullptr; memoryManager_ = nullptr; clear(); }
 
     // Audio-thread: QUEUE each byte in `data` as a level tagged with `offset` (samples from block
-    // start). Released by pumpUntil once the block's emulated position reaches that offset.
+    // start). Released by pumpUntil once the block's emulated position reaches that offset. The queue is sized
+    // to what BOTH of its doors can deliver in one block: a level is one byte, so the `bytes` input's 1 KiB costs
+    // 1024 entries, and a message longer than a MidiEvent reaches the same door from the `midi` input (the node's
+    // pushCoreBytes) at up to its 72 KiB. That is 74752 levels, which is a lot of storage but no message the plan
+    // can deliver is dropped; a level past the bound is dropped and counted, as a graph queue's would be.
     //
     // `flush` is accepted and IGNORED, unlike the N8 FIFO's. It exists on the SystemBase seam for
     // byte protocols that need a barrier; a level has no undelivered-stream hazard to clear, and
@@ -87,6 +93,8 @@ public:
 
     // Introspection / tests.
     std::size_t   pendingCount() const { return pending_.size(); }
+    std::uint64_t pendingDropped() const { return pending_.dropped(); }
+    std::uint64_t acceptedLevels() const { return acceptedLevels_; }
     std::uint8_t  lastApplied()  const { return lastApplied_; }
 
 private:
@@ -94,7 +102,9 @@ private:
         std::uint32_t offset;   // samples from block start
         std::uint8_t  levels;
     };
-    std::deque<PendingLevel> pending_;
+    static constexpr std::size_t kPendingLevels = std::size_t(rp::kMidiInput.bytes) + rp::kBytesInput.bytes;
+    rp::FixedQueue<PendingLevel> pending_{kPendingLevels};
+    std::uint64_t            acceptedLevels_ = 0;
     bool                     needsSort_    = false;  // set on enqueue; pumpUntil stable-sorts once
     std::uint8_t             lastApplied_  = 0xFF;   // idle: every line released
     SmsControlManager*       controlManager_ = nullptr;
diff --git a/packages/native/src/transport/N8ReliableLink.hpp b/packages/native/src/transport/N8ReliableLink.hpp
index b9d5b95c..15a286b5 100644
--- a/packages/native/src/transport/N8ReliableLink.hpp
+++ b/packages/native/src/transport/N8ReliableLink.hpp
@@ -1,11 +1,13 @@
 #pragma once
 
+#include <array>
 #include <cstddef>
 #include <cstdint>
-#include <deque>
+#include <cstring>
 #include <functional>
 #include <limits>
-#include <vector>
+
+#include "util/FixedQueue.hpp"
 
 // The host half of BlipToaster's reliable link (bliptoaster src/midi/main.c "The reliable link").
 //
@@ -66,11 +68,12 @@ public:
         window_.clear();
         synced_ = false;
         status_ = 0;
-        msg_.clear();
+        msgLen_ = 0;
+        msgTooLong_ = false;
         inSysex_ = false;
         ackState_ = 0;
         resendAt_ = kNever;
-        pendingSync_.clear();
+        pendingSyncLen_ = 0;
     }
 
     // Host MIDI in - any run of bytes, possibly several messages, running status, SysEx, real-time. Each complete
@@ -110,12 +113,12 @@ public:
         if (nowNs >= resendAt_ || timedOut) {
             resendAt_ = kNever;
             ++resends_;
-            if (!pendingSync_.empty()) { emit(pendingSync_.data(), pendingSync_.size(), -1); pendingSync_.clear(); }
+            if (pendingSyncLen_) { emit(pendingSync_.data(), pendingSyncLen_, -1); pendingSyncLen_ = 0; }
             for (auto& f : window_) {
                 if (f.sentNs < 0) continue;   // still queued in the transport: it goes out on its own
                 f.sentNs = -1;                // re-armed; onSent marks it again when it actually leaves
                 ++framesResent_;
-                emit(f.bytes.data(), f.bytes.size(), f.seq);
+                emit(f.bytes, f.size, f.seq);
             }
         }
         return nextDue();
@@ -133,10 +136,12 @@ public:
     static constexpr std::int64_t kNever = std::numeric_limits<std::int64_t>::max();
 
 private:
+    // A frame's bytes are inline: a window of 48 frames is a fixed ~3 KiB however many messages a block carries.
     struct Frame {
-        std::uint8_t              seq;
-        std::vector<std::uint8_t> bytes;    // F5 seq message...
-        std::int64_t              sentNs;   // -1 = not on its way yet
+        std::uint8_t seq;
+        std::int64_t sentNs;                    // -1 = not on its way yet
+        std::uint32_t size;
+        std::uint8_t  bytes[3 + kMaxMessage];   // F5 seq message... sum
     };
 
     // Is `a` before `b` in the 7-bit sequence space (within half the space)?
@@ -154,23 +159,32 @@ private:
         if (b >= 0xF8) { emit(&b, 1, -1); return; }   // real-time: unframed, anywhere (the ROM skips or acts on it)
         if (inSysex_) {
             if (b == 0xF7) {
-                msg_.push_back(b);
-                if (msg_.size() <= kMaxMessage) frame(emit);   // longer: no frame (see kMaxMessage)
-                else msg_.clear();
+                msgPush(b);
+                if (!msgTooLong_ && msgLen_ <= kMaxMessage) frame(emit);   // longer: no frame (see kMaxMessage)
+                msgLen_ = 0;
+                msgTooLong_ = false;
                 inSysex_ = false;
                 return;
             }
-            if (b < 0x80) { msg_.push_back(b); return; }
+            if (b < 0x80) { msgPush(b); return; }
             inSysex_ = false;   // a status cut the SysEx short: drop it (the ROM would not have applied it either)
-            msg_.clear();
+            msgLen_ = 0;
+            msgTooLong_ = false;
         }
-        if (b == 0xF0) { status_ = 0; msg_.assign(1, b); inSysex_ = true; return; }
-        if (b >= 0xF0) { status_ = 0; msg_.clear(); return; }   // other System Common: the ROM ignores them
-        if (b & 0x80) { status_ = b; msg_.assign(1, b); return; }
+        if (b == 0xF0) { status_ = 0; msg_[0] = b; msgLen_ = 1; msgTooLong_ = false; inSysex_ = true; return; }
+        if (b >= 0xF0) { status_ = 0; msgLen_ = 0; msgTooLong_ = false; return; }  // System Common: the ROM ignores them
+        if (b & 0x80) { status_ = b; msg_[0] = b; msgLen_ = 1; return; }
         if (status_ == 0) return;                       // data with no status: junk
-        if (msg_.empty()) msg_.push_back(status_);      // running status: every frame carries its status
-        msg_.push_back(b);
-        if (msg_.size() == 1 + dataLen(status_)) { frame(emit); }
+        if (msgLen_ == 0) msg_[msgLen_++] = status_;    // running status: every frame carries its status
+        msgPush(b);
+        if (msgLen_ == 1 + dataLen(status_)) { frame(emit); }
+    }
+
+    // Append one byte of the message being collected. Past the longest message a frame may carry the bytes are dropped,
+    // which is what a frame would do with them anyway (see kMaxMessage), and the message is marked too long.
+    void msgPush(std::uint8_t b) {
+        if (msgLen_ < sizeof msg_) msg_[msgLen_++] = b;
+        else msgTooLong_ = true;
     }
 
     void frame(const Emit& emit) {
@@ -186,19 +200,22 @@ private:
             const std::uint8_t s[2] = { SYNC, nextSeq_ };
             emit(s, 2, -1);
         }
-        Frame f{ nextSeq_, {}, -1 };
-        f.bytes.reserve(3 + msg_.size());
-        f.bytes.push_back(FRAME);
-        f.bytes.push_back(nextSeq_);
-        f.bytes.insert(f.bytes.end(), msg_.begin(), msg_.end());
+        Frame f{};
+        f.seq = nextSeq_;
+        f.sentNs = -1;
+        f.size = 3 + static_cast<std::uint32_t>(msgLen_);
+        f.bytes[0] = FRAME;
+        f.bytes[1] = nextSeq_;
+        std::memcpy(f.bytes + 2, msg_.data(), msgLen_);
         unsigned sum = nextSeq_;
-        for (auto b : msg_) sum += b;
-        f.bytes.push_back(static_cast<std::uint8_t>(sum & 0x7F));   // the ROM checks it before anything applies
+        for (std::size_t i = 0; i < msgLen_; ++i) sum += msg_[i];
+        f.bytes[2 + msgLen_] = static_cast<std::uint8_t>(sum & 0x7F);   // the ROM checks it before anything applies
         ++framesSent_;
         nextSeq_ = static_cast<std::uint8_t>((nextSeq_ + 1) & 0x7F);
-        window_.push_back(std::move(f));
-        emit(window_.back().bytes.data(), window_.back().bytes.size(), window_.back().seq);
-        msg_.clear();
+        window_.push(f);
+        emit(window_.back().bytes, window_.back().size, window_.back().seq);
+        msgLen_ = 0;
+        msgTooLong_ = false;
     }
 
     void onAck(std::uint8_t next, std::uint8_t last, std::int64_t nowNs) {
@@ -211,7 +228,7 @@ private:
                 lastRttNs_ = rtt;
                 if (rttMinNs_ < 0 || rtt < rttMinNs_) rttMinNs_ = rtt;
             }
-            window_.pop_front();
+            window_.pop();
         }
         if (window_.empty()) { resendAt_ = kNever; return; }
         if (advanced) resendAt_ = kNever;   // what a pending re-send was for has (partly) arrived: decide afresh
@@ -220,7 +237,8 @@ private:
             // The ROM expects something we no longer hold (it rebooted and latched another number, or we reset):
             // point it at our oldest frame and re-send from there.
             const std::uint8_t s[2] = { SYNC, oldest.seq };
-            pendingSync_.assign(s, s + 2);
+            std::memcpy(pendingSync_.data(), s, 2);
+            pendingSyncLen_ = 2;
             scheduleResend(nowNs);
             return;
         }
@@ -250,16 +268,21 @@ private:
     }
 
     Timing                    timing_;
-    std::deque<Frame>         window_;
+    // Fixed storage, sized once here: the sender runs inside a block (the NES role frames a released message as the
+    // block's emulation reaches it), so neither the window nor the message being collected may grow there.
+    rp::FixedQueue<Frame>     window_{kWindow};
     std::uint8_t              nextSeq_    = 0;
     bool                      synced_     = false;
     std::uint8_t              status_     = 0;
-    std::vector<std::uint8_t> msg_;
+    std::array<std::uint8_t, kMaxMessage + 2> msg_{};   // F0, up to kMaxMessage of payload, F7
+    std::size_t               msgLen_     = 0;
+    bool                      msgTooLong_ = false;      // a byte past what a frame could carry was dropped
     bool                      inSysex_    = false;
     std::uint8_t              ackState_   = 0;   // 0 idle, 1 after F5, 2 after <next>
     std::uint8_t              ackNext_    = 0;
     std::int64_t              resendAt_   = kNever;
-    std::vector<std::uint8_t> pendingSync_;   // a re-sync onAck decided on: poll() sends it ahead of the re-send
+    std::array<std::uint8_t, 2> pendingSync_{};  // a re-sync onAck decided on: poll() sends it ahead of the re-send
+    std::size_t               pendingSyncLen_ = 0;
     std::uint64_t             resends_    = 0;
     std::uint64_t             framesSent_   = 0;
     std::uint64_t             framesResent_ = 0;
````

- [ ] **Step 6: The host's own doors**

`GraphHost` exposes `commandCapacity()` and `Engine.cpp` holds a `static_assert` that `rp::kCommandRingCapacity`
equals it, so the press queue's two doors cannot drift silently; `SystemNodes.cpp` sizes the Game Boy's port from
`kSerialOutInput` rather than repeating the number. The native side of `spec/02` gains the paragraph after the
system-inputs table saying that the core side is bounded and fixed when a core is built, from `InputCapacity.hpp`, so
a block's host input allocates nothing, and a refused input is counted:

````diff
diff --git a/packages/native/src/graph/GraphHost.hpp b/packages/native/src/graph/GraphHost.hpp
index 16038350..8894af92 100644
--- a/packages/native/src/graph/GraphHost.hpp
+++ b/packages/native/src/graph/GraphHost.hpp
@@ -168,6 +168,9 @@ public:
     const void* hostData() const;
 
     const HostConfig& config() const { return config_; }
+    // What the command ring holds, which is also how many host commands (a button press among them) can be queued
+    // between two blocks: a core sizing an input queue from both doors reads this.
+    static constexpr std::size_t commandCapacity() { return kCommandCapacity; }
 
 private:
     struct Command {
diff --git a/packages/native/src/host/engine/Engine.cpp b/packages/native/src/host/engine/Engine.cpp
index ee9cad15..e39b12c8 100644
--- a/packages/native/src/host/engine/Engine.cpp
+++ b/packages/native/src/host/engine/Engine.cpp
@@ -16,6 +16,7 @@
 #include "system/SystemTypes.hpp"   // AudioBlockInfo, SystemId
 #include "system/CoreBackends.hpp"  // registerCoreBackends: the engine owns the one factory every build goes through
 #include "system/SystemFactory.hpp"
+#include "system/InputCapacity.hpp"
 
 #include "transport/FrameBufferTriple.hpp"
 #include "native/core/img/png/lodepng.h"
@@ -35,6 +36,10 @@
 
 namespace {
 
+// A press queue is sized from the two doors a block can press through: its buttons input and this command ring.
+static_assert(rp::kCommandRingCapacity == rp::graph::GraphHost::commandCapacity(),
+              "InputCapacity.hpp's command-ring bound must match GraphHost's ring");
+
 // The table before the first plan. At namespace scope, not function-local: currentTable() may first run on
 // the audio thread, which must not take a static's init guard or register its destructor.
 const rp::host::SystemTable kNoSystems;
diff --git a/packages/native/src/host/graph/SystemNodes.cpp b/packages/native/src/host/graph/SystemNodes.cpp
index 7c7b33c9..cfa0e221 100644
--- a/packages/native/src/host/graph/SystemNodes.cpp
+++ b/packages/native/src/host/graph/SystemNodes.cpp
@@ -589,7 +589,8 @@ struct SystemNode {
 // SC=0x81) shifts at its serial rate instead, and a CGB on its fast clock (32768 bytes a second, about 0.7 a frame
 // at 48 kHz, twice that in double speed) can outrun this in a block past about 12000 frames (about 6000 in double
 // speed): what does not fit is dropped and counted (GraphHost::droppedMessages), never lost silently.
-constexpr rp::capacity kSerialOutCapacity{.messages = 8256, .bytes = 8256};
+constexpr rp::capacity kSerialOutCapacity{.messages = rp::kSerialOutInput.messages,
+                                           .bytes = rp::kSerialOutInput.bytes};
 
 struct GameBoy : SystemNode<GameBoy, SameBoySystem, GameBoySettings> {
     static constexpr const char* id = "rp.sameboy.gb";
diff --git a/spec/02-native-host.md b/spec/02-native-host.md
index 0ab7d627..7b601d55 100644
--- a/spec/02-native-host.md
+++ b/spec/02-native-host.md
@@ -371,6 +371,13 @@ with slack, rather than sized from the plan's block size, so a feedback wire fro
 across every swap; a CGB clocking the port itself on its fast clock can outrun it in a very long block, and
 what does not fit is dropped and counted.
 
+The core side is bounded too, and fixed: each core sizes its ingress queues (a Game Boy's serial runs, a
+NES's FIFO runs and frame window, a Master System's levels, each console's presses) when it is built, from
+what one block's input can deliver ([InputCapacity.hpp](../packages/native/src/system/InputCapacity.hpp),
+the capacities the sources' outputs above declare), so a block that carries host MIDI, raw bytes, serial-in
+bytes or presses allocates nothing in the core. An input a queue cannot take is refused and counted, the way
+a push past a graph queue's capacity is dropped and counted.
+
 An events or bytes output may declare its queue's capacity (`rp::bytes_out::sized<rp::capacity{...}>` in
 C++, `events.out(capacity)` / `bytes.out(capacity)` in TS), by default 256 messages and 4096 bytes, at
 most 1 Mi messages and 16 MiB. A wire carries its source's queue, so an input declares none (both parsers
````

- [ ] **Step 7: Run the tests to see them pass, and parity unchanged**

Run: `cmake --build build --target retroplug-engine-graph-test -j$(nproc) && cmake --build build --target retroplug-graph-test -j$(nproc) && build/bin/retroplug-engine-graph-test "[alloc],[inputs],[input-drops]" && build/bin/retroplug-graph-test "[fixed-queue],[fixed-byte-ring]"`
Expected: every selected case passes; report the selection's line.

Run: `pnpm test:plugin`
Expected: `# 10 plugin test binary(ies) passed (jobs=16)`: the engine-graph binary's `27160 assertions in 211 test cases`, the graph binary's `2221 / 202`, the audio binary's `1233762 / 105` (the FIFO, rebase and link cases and the two timing files this task edits), and the other seven at their 33d39f39 counts.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources build/bin/retroplug-engine-graph-test`
Expected: the same binary with the fixtures present, `27224 / 211`: report the line.

Run: `./build.sh`
Expected: `[100%] Built target retroplug-cli`, `retroplug-vst3`, `retroplug-clap`, `retroplug-vst2`, `retroplug-sdl`, `retroplug-jack`, `retroplug-node`.

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity`
Expected: `# 30 native test file(s) passed`, every file 0 skipped and 0 failed.

Run: `pnpm test:native parity`
Expected: `# 30 native test file(s) passed`, with `# 46 case(s) SKIPPED in 10 file(s)`, exactly the baseline distribution.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native`
Expected: `# 158 native test file(s) passed`, with `# 2 case(s) SKIPPED in 2 file(s): dsp-bench(1), nes-bench(1)`.

Run: `pnpm typecheck && pnpm test && pnpm check:docs`
Expected: `pnpm typecheck` clean, `# 168 test file(s) passed`, `check-doc-links: 69 file(s) clean` (the prototype's 68 plus this plan).

- [ ] **Step 8: The consumers, against this tree's CLI**

Neither consumer needs an edit: this task changes only the cores' inner storage, and the CLI they drive is the one
this tree builds. Run each from a scratch copy so its repository is never written to:

Run: `R=$PWD; rm -rf /tmp/c4-bt && mkdir -p /tmp/c4-bt/build /tmp/c4-bt/retroplug-cli && cp -r /workspaces/bliptoaster/src /tmp/c4-bt/ && cp /workspaces/bliptoaster/build/*.nes /workspaces/bliptoaster/build/*.dbg /workspaces/bliptoaster/build/*.map /tmp/c4-bt/build/ && cp -r /workspaces/bliptoaster/retroplug-cli/tests /tmp/c4-bt/retroplug-cli/ && (cd /tmp/c4-bt && "$R/build/bin/retroplug-cli" test retroplug-cli/tests --rom build/bliptoaster.nes); e=$?; rm -rf /tmp/c4-bt; exit $e`
Expected: `PASS: 57/57 test file(s) ok`, its last file 49 passed with none skipped or failed.

Run: `R=$PWD; rm -rf /tmp/c4-vj && mkdir -p /tmp/c4-vj/build /tmp/c4-vj/retroplug-cli && cp -r /workspaces/nesvj/src /tmp/c4-vj/ && cp /workspaces/nesvj/build/*.nes /workspaces/nesvj/build/*.dbg /workspaces/nesvj/build/*.map /tmp/c4-vj/build/ && cp -r /workspaces/nesvj/retroplug-cli/tests /tmp/c4-vj/retroplug-cli/ && (cd /tmp/c4-vj && "$R/build/bin/retroplug-cli" test retroplug-cli/tests --rom build/nesvj.nes); e=$?; rm -rf /tmp/c4-vj; exit $e`
Expected: `PASS: 19/19 test file(s) ok`, its last file 23 passed with none skipped or failed.

Run: `git -C /workspaces/bliptoaster status --short && git -C /workspaces/nesvj status --short`
Expected: nothing printed: neither repository was edited.

- [ ] **Step 9: The sanitizers, on the alloc and drop cases, and the new units**

Catch2 splits a spec on commas, so the audio selection below escapes the comma in the two case names that carry one
(`\,`, its documented escape). Left bare they became filters that matched nothing, which is why the report's TSan line
for this task's new audio cases is `All tests passed (20 assertions in 2 test cases)`: two of the four selected. With
every comma escaped all four run, 32 assertions.

Run: `cmake --build build-tsan --target retroplug-engine-graph-test retroplug-graph-test -j$(nproc) && TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-engine-graph-test "[alloc],[input-drops],[inputs]"`
Expected: `test cases: 8 | 7 passed | 1 skipped`, `assertions: 411 | 411 passed`, no ThreadSanitizer report (no fixtures, which is how the report recorded these figures).

Run: `TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-graph-test "[fixed-queue],[fixed-byte-ring]"`
Expected: TSan unit case `47 / 4`, no report.

Run: `cmake --build build-tsan --target retroplug-audio-test -j$(nproc) && TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-audio-test "a carried MIDI run and raw run keep their bytes across a rebase,the permissive profile still drops past fifo_a's hard bound\, and keeps the rest,the wire's own buffer drops past a block's input and counts it,reliable link: a window that fills without an ack gives up the oldest and re-syncs\, not grows"`
Expected: `All tests passed (32 assertions in 4 test cases)`: this task's four audio cases, the two that carry a comma included, no ThreadSanitizer report.

Run: `cmake --build build-asan --target retroplug-engine-graph-test retroplug-graph-test -j$(nproc) && ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-engine-graph-test "[alloc],[input-drops],[inputs]"`
Expected: the same 8 cases (`7 passed | 1 skipped`) and 411 assertions, no AddressSanitizer or LeakSanitizer report.

Run: `ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-graph-test "[fixed-queue],[fixed-byte-ring]"`
Expected: ASan unit case `47 / 4`, no report.

Run: `cmake --build build-asan --target retroplug-audio-test -j$(nproc) && ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-audio-test "a carried MIDI run and raw run keep their bytes across a rebase,the permissive profile still drops past fifo_a's hard bound\, and keeps the rest,the wire's own buffer drops past a block's input and counts it,reliable link: a window that fills without an ack gives up the oldest and re-syncs\, not grows"`
Expected: `All tests passed (32 assertions in 4 test cases)` again, no report: the report's ASan `32 / 4`.

- [ ] **Step 10: Commit**

````bash
git add -- packages/native/src/system/InputCapacity.hpp packages/native/src/util/FixedQueue.hpp \
  packages/native/test/graph/FixedQueue.test.cpp packages/native/cmake/tests.cmake \
  packages/native/src/graph/GraphHost.hpp packages/native/src/host/engine/Engine.cpp \
  packages/native/src/host/graph/SystemNodes.cpp packages/native/src/system/SystemBase.hpp \
  packages/native/src/system/mesen/MesenGbaSystem.cpp packages/native/src/system/mesen/MesenGbaSystem.hpp \
  packages/native/src/system/mesen/MesenNesSystem.cpp packages/native/src/system/mesen/MesenNesSystem.hpp \
  packages/native/src/system/mesen/MesenSmsSystem.cpp packages/native/src/system/mesen/MesenSmsSystem.hpp \
  packages/native/src/system/mesen/NesEverdriveFifo.hpp packages/native/src/system/mesen/roles/NesN8FifoRole.cpp \
  packages/native/src/system/mesen/roles/NesN8FifoRole.hpp packages/native/src/system/mesen/roles/SmsSyncRole.cpp \
  packages/native/src/system/mesen/roles/SmsSyncRole.hpp packages/native/src/system/sameboy/SameBoySystem.cpp \
  packages/native/src/system/sameboy/SameBoySystem.hpp packages/native/src/transport/N8ReliableLink.hpp \
  packages/native/test/audio/N8ReliableLink.test.cpp packages/native/test/audio/NesEverdriveFifo.test.cpp \
  packages/native/test/audio/NesN8FifoTiming.test.cpp packages/native/test/audio/SameBoySerialTiming.test.cpp \
  packages/native/test/engine-graph/SystemNode.test.cpp spec/02-native-host.md
git commit -F - <<'EOF'
fix(system): a core's input queues are preallocated rings, so a block carrying host input allocates nothing

The [alloc] cases staged no host input inside their counted windows, so a warmed core's whole block was free only while
the node handed its core nothing: every queue a block's input lands in grew on the block thread the first time a block
reached a depth that queue had not seen. A host playing a MIDI-heavy project is exactly that steady state, so the
promise the phase rests on did not describe it. Every one of those queues is now bounded and sized before a block runs,
from what one block's doors can deliver (system/InputCapacity.hpp: the capacities spec/02's table gives the outputs
that feed a system node's inputs, including a Game Boy's serialOut), and a push that does not fit is dropped and
counted, as a push past a graph queue's capacity is.

What the extended case found, and what each became:

- a Mesen console's press queue (NES, SMS, GBA) was a std::vector grown by push_back: 7 allocations and 7 frees at the
  NES's first counted window, 1 and 1 on the SMS. Now a FixedQueue sized to both doors a block can press through, a
  block's buttons input plus GraphHost's command ring (kPressInput), which prepareForBlock already empties every block.
- a Game Boy's press queue was a std::deque, and its first node is allocated eagerly, so a small burst hides it: 64
  presses a block (carried across blocks, since each is scheduled 10 ms after the one before) allocated 2 more entries
  inside the first window and freed 1. Now a FixedQueue of sixteen blocks' worth of presses, which covers both doors
  too, with a press past it dropped and counted.
- a Game Boy's serial-in queue was a std::deque holding one byte an entry. Now a run a message in a FixedQueue (the
  bytes of one message share its frame, so they extend one run) with the bytes themselves in a FixedByteRing sized to
  two blocks' serialIn input (or one serialOut block): the front byte a bit shift reads is the ring's head, so nothing
  moves and nothing grows.
- the NES N8 FIFO role held two std::deques: a PendingByte a byte for the raw feed and, under the reliable link, a
  PendingMsg a message with a fresh std::vector of bytes each. Now one queue of runs (offset, first byte, count) over
  one byte arena, shared by both feeds and both modes: the bytes stay where they were appended, the descriptors are
  sorted, and a long SysEx or a multi-message run costs one descriptor and one copy. The stable sort it runs once a
  block was std::stable_sort, which takes a temporary buffer; it is an in-place insertion sort now, the trade
  Engine::Staging makes for the host's staged MIDI and for the same reason.
- the N8 role's end-of-block rebase compacts the arena in ascending byte order, not queue order: the sort by offset can
  permute the live runs, and a forward compaction in queue order overwrote a run it had not copied, so two feeds that
  both carried a run past the block end could swap payloads. A case stages exactly that (MIDI first, raw bytes second,
  both carried) and reads each payload back intact.
- the reliable link's sender, which frames a message from inside a block, held a std::deque of frames with a
  std::vector of bytes per frame, and a std::vector collecting the message being parsed. Its window is a FixedQueue of
  48 inline frames now, the message being parsed an array of kMaxMessage + 2 bytes (one longer was never framed), and
  the pending sync two bytes, so a link-mode NES allocates nothing for any message a block carries.
- NesEverdriveFifo's two stages: the wire queue (a std::queue, i.e. a deque) is a FixedQueue sized to a block's host
  input, and fifo_a's delivered bytes are reserved to twice their live bound (the consumed prefix can sit under the
  head until pop compacts it), with a reply past that bound dropped and counted even under the permissive profile.
- a Master System's sync role queue was a std::deque of levels, now a FixedQueue sized in BYTES from both of its doors:
  the bytes input's 1 KiB plus the midi input's 72 KiB for a message longer than a MidiEvent, which is what that door
  hands pushCoreBytes. A level past it is dropped and counted.

Each queue's behaviour for what fits is unchanged: the same bytes reach the cores in the same order at the same frames,
the N8 role's sort and drain order are the same, and the link frames the same bytes with the same sequence numbers.
Parity holds bit for bit with nothing re-recorded, and the [video] hashes are the ones they were. A refusal is now
visible: each core exposes inputDropped() and inputAccepted(), and a case per queue kind pushes past its bound, holds
the count and runs a block, so the core still fills it.

The whole-block [alloc] case now stages a block's input inside every counted window, the way a node applies its inputs
before the core's prepareForBlock: notes, clocks, a raw run and a settings-length SysEx into a NES (and a section with
the reliable link on, which also checks frames were really sent), the transport's levels into a Master System, MIDI runs
and a press burst into a Game Boy in both stream shapes, presses into every console, and through the Engine the same
staging by the node's own doors. The press count and the run lengths are past what a library deque allocates its first
node for, so a queue that grew on the block thread shows in a window even when it would not grow every block.

The fixed storage itself has unit cases: FixedQueue's refusal, its compaction of a consumed prefix and begin/end after
that, FixedByteRing's wrap over more than its capacity, and stableSortEntries's stability on equal keys. The ingress
capacities are read back from ports.ts in a test, so either side of that copy drifting is visible. spec/02's paragraph
is updated in the docs commit, with where the new counters can be read and with the FIFO's permissive-profile drop.
EOF
````

### Task 6: the docs say what C4 built

Design sections 1 to 11, as landed (commit 6). The C4 design is marked built and says what was built in each section it
changed: the ring's capacity, overflow policy and the block size in the build request; the tap rings and scratches
sized at activation; an arm sizing nothing and the `SetCaptureCapacity` call it still makes; the blips re-initialised
by `blip_reset`; the tap as a `ChannelTap` capability with the per-class address reasoning; the node's `kind()` check
with the NES's 1-to-3 mode bound and the cart mirror's `SystemKind::MesenNes` named as deliberate residuals; the tests
as built; the six commits; the prototype's questions answered; the findings; and a section each for the two stages the
user added (Mesen's plumbing, and the input queues). spec/02 gains `SystemBase`'s surface after P1, the ring and the
promise, the tap and `SystemTable`'s slot, the build request's block size, the ingress queues' bounds and counters and
the FIFO's permissive-profile storage bound; spec/06 the cases C4 added; spec/07 C4's leftovers; spec/08 the C++
suite's counting allocator. The main design's capability table, cast table and Phase C record say what C4 closed, the
B1 design notes where C4 replaced its two core pointers, AGENTS.md's engine-graph paragraph gains C4, and
`MemoryType.hpp`'s comment stops naming a subscribe RPC native does not have.

**Files:**
- Modify: `AGENTS.md`, `docs/modular-graph-design.md`, `docs/modular-graph-phase-b1-design.md`, `docs/modular-graph-phase-c4-design.md`, `packages/native/src/system/MemoryType.hpp`, `spec/02-native-host.md`, `spec/06-build-test.md`, `spec/07-remaining-work.md`, `spec/08-profiling.md`

- [ ] **Step 1: The design, as built**

The status line, section 1's nine slots (it said seven; `kMemoryTypeCount` is nine), section 2's two added decisions,
sections 3 to 6 rewritten as "as built", section 7's six commits, section 8's answers, section 9 (the plumbing),
section 10 (the findings, each tagged with the section it changed) and section 11 (the input queues), with "Left as it
is" moved to section 12:

````diff
diff --git a/docs/modular-graph-phase-c4-design.md b/docs/modular-graph-phase-c4-design.md
index 76eaae02..0bc5f9aa 100644
--- a/docs/modular-graph-phase-c4-design.md
+++ b/docs/modular-graph-phase-c4-design.md
@@ -1,17 +1,20 @@
-# Phase C4: the engine names no console
+# Phase C4: the tap and the build check name no console
 
-**Date:** 2026-10-31 · **Branch:** `feature/modularize` · **Status:** designed; the prototype answers section 8
+**Date:** 2026-10-31 · **Branch:** `feature/modularize` · **Status:** built: section 7's six commits; what building
+them changed is in the sections it changed, and what it found is section 10
 
 After C3 a system node builds its own core, but the engine still knows one console by name and one core still allocates
 on the audio thread. `Engine` finds a NES core with `dynamic_cast<MesenNesSystem*>` to arm its per-channel tap, a system
 node `dynamic_cast`s the core it built to its own type, and every Mesen core collects its audio in a `std::vector` that
 `resize`s on the block thread. `SystemBase` also still carries about ten members nothing calls. This phase removes all
 of that: the dead surface goes, Mesen's audio collects in a fixed ring sized before the first block, the tap becomes a
-capability any core may offer, and the node's build check stops casting. It is Phase C's fourth piece
+capability any core may offer, the node's build check stops casting, and the ingress queues a block's host input lands
+in are bounded rings sized when the core is built. It is Phase C's fourth piece
 ([modular-graph-design.md](modular-graph-design.md) section 8: "Console-specific code moves into each node; every
 `dynamic_cast` in section 6.5 goes; `SystemBase` shrinks to the capability interfaces; `MesenAudioDevice` gets a
 preallocated ring"), after C3 ([modular-graph-phase-c3-design.md](modular-graph-phase-c3-design.md)). C4 is a part of
-that bullet, chosen by the user (section 2); the rest is section 9's.
+that bullet, chosen by the user (section 2), plus a second decision taken while it was built (section 9) and a third
+taken after this design's first draft (section 11); the rest is section 12's.
 
 ## 1. Before C4
 
@@ -21,7 +24,7 @@ that bullet, chosen by the user (section 2); the rest is section 9's.
   a seed), `loadStateBytes` from outside (a savestate is a seed; the core's own seed path calls it internally),
   `midiOut()` / `midiOut_` (the old core-side MIDI out; the serial-out decoder replaced it), and a live memory-snapshot
   mechanism nothing enables (`enableMemorySnapshot`, `disableMemorySnapshot`, `memorySnapshot`,
-  `publishMemorySnapshots`, `snapshots_`), whose publish every core's `finishBlock` still runs over seven empty slots.
+  `publishMemorySnapshots`, `snapshots_`), whose publish every core's `finishBlock` still runs over nine empty slots.
   `kind()` has no production caller either.
 - **`MesenAudioDevice`** ([MesenAudioDevice.hpp](../packages/native/src/system/mesen/MesenAudioDevice.hpp)), the
   `IAudioDevice` every Mesen core (NES, GBA, SMS / Game Gear) gives Mesen's mixer, is a `std::vector<int16_t>` of
@@ -46,98 +49,474 @@ that bullet, chosen by the user (section 2); the rest is section 9's.
 ## 2. Decisions taken
 
 The user's, on 2026-10-31: C4 is P1 (the dead surface), P2 (the audio ring), P5 (the tap as a capability) and P6 (the
-build check), from a survey that split the main design's bullet into seven landable pieces. Left for later (section 9):
+build check), from a survey that split the main design's bullet into seven landable pieces. Left for later (section 12):
 P3, cores borrowing their node's pinned ROM bytes; P4, collapsing the snapshot registry's double copy; P7, the full
 capability split of `SystemBase` (main design section 6.4) and one node file per console.
 
-## 3. P1: the dead surface
+A second decision, taken while P2 was built: **Mesen's frame and input plumbing is C4's too**, in a commit of its own
+(section 9). P2's zero-allocation case found a Mesen console allocating 13 to 18 times per emulated frame inside Mesen -
+three for the control manager's port snapshot, two for each device's raw state, twelve for a `RenderedFrame` and its
+`InputData` copies, one for a notification's listener copy - so "a warmed block allocates nothing" could not hold while
+those stayed, and none of them is the audio path. The measurement is section 9's and what fixed them also section 9's.
 
-Every member section 1 lists goes, with each core's overrides and the calls that only fed them (the memory-snapshot
-publish in every `finishBlock`). The core's internal seed path keeps its own `loadStateBytes` use (a private member or
-an inlined restore: the prototype's call). `kind()` stays: P6 reads it. A test that reads a deleted member moves to what
-replaced it (the node setting, the seed, the serial-out decoder) or goes when it only tested the deleted member.
+A third decision, taken after this design was first written: **a core's ingress queues are C4's too**, in a commit of
+its own (section 11). The `[alloc]` cases warmed with a button press and then staged nothing inside their counted
+windows, so "a warmed core's block allocates nothing" held only while the node handed its core no host input: every
+queue a block's input lands in grew on the block thread, and a host playing a MIDI-heavy project is exactly that steady
+state. The extended case found 7 to 11 allocations in the NES's first counted window (5 for 12 presses alone), so every
+one of those queues is now a bounded, preallocated ring sized when the core is built from what one block's doors can
+deliver (`system/InputCapacity.hpp`); a push past its bound is dropped and counted (section 11).
 
-## 4. P2: a fixed audio ring
+## 3. P1: the dead surface, as built
 
-`MesenAudioDevice` becomes a fixed-capacity ring of interleaved stereo `int16_t`: `PlayBuffer` writes at the tail,
-`drain(dest, frames)` reads from the head, and `availableFrames()` keeps its exact meaning (frames written and not yet
-drained), so the step loops and the N8 FIFO behave exactly as before.
+Every member section 1 lists went, with each core's overrides and the calls that only fed them (the memory-snapshot
+publish in every `finishBlock`), and so did the four core configs' `savSuffix` / `savPath` fields, which nothing in
+native assigned (the paths are TS's): the only readers were the deleted accessors. `loadStateBytes` stays as a
+**private** virtual, so `seedSramAndState` keeps one dispatch point for the three Mesen restore bodies without keeping
+the member on the public surface; SameBoy's override went entirely, since its seed path restores inline. `kind()` stayed
+and P6 gave it its first production caller. The deleted publish walked all **nine** `MemoryType` slots every block over
+an always-empty array (section 1's survey said seven; `NametableRam` and `ExtWorkRam` came after it), so P1 takes a real
+if tiny per-block cost off all four cores. No test read a deleted member: none moved, and none was deleted.
 
-**Capacity** is the plan's maximum block frames plus one Mesen flush (Mesen's mixer emits at most 32768 stereo frames
-in one `PlayBuffer`, the cap of its own fixed buffer): about 160 KB a Mesen system at the engine's 8192-frame floor. A
-system node is handed `maxFrames` in `activate`; it gives it to its core (`reserveAudio(maxFrames)`) when it builds the
-core, and again when a host grows its block: `Engine::setMaxBlockFrames` reactivates the graph (quiescent, control
-thread), and the node's `activate`, which returns early for a core it already holds, resizes there. A write past
-capacity, which the bound makes unreachable, never allocates: the ring counts it (a counter tests read) and a debug
-build asserts.
+## 4. What was built: a fixed audio ring
 
-**The rest of the audio path:** `NesSoundMixer::_streamBuf` (the in-tree Mesen's per-channel tap ring) is bounded and
-preallocated the same way, and the NES node's per-block scratches are sized at activation to `maxFrames`.
+`MesenAudioDevice` is a fixed-capacity ring of interleaved stereo `int16_t` with a head and a count: `PlayBuffer`
+writes at the tail, `drain(dest, frames)` reads from the head in the one or two ranges a wrap makes, and
+`availableFrames()` is the count, so it keeps its exact meaning (frames written and not yet drained) and the step loops
+and the N8 FIFO's scheduling gate on the same number. No `resize`, no `erase`.
 
-**The test.** A `retroplug-engine-graph-test` case (the suite's `CountingAllocator`, as the existing no-allocation
-cases use) warms a NES (mixed, and with the per-channel tap armed), a GBA and an SMS, then runs ragged blocks up to the
-maximum and requires zero allocations on the block thread. It fails before P2. It holds steady state: the first block
-after a setting change, where C2 saw Mesen allocate inside its own emulation, is outside the promise. Another
-allocation the test turns up is reported with its source; one in our wrappers or the audio path is fixed, one deep in
-Mesen's emulation is recorded.
+**Capacity** is the plan's maximum block frames plus one Mesen flush: `maxBlockFrames + kMaxPlayBufferFrames`, with
+`kMaxPlayBufferFrames = 0x10000 / 2 = 32768` frames, about 160 KiB a Mesen system at the engine's 8192-frame floor and
+384 KiB at the 65536 ceiling. The bound is the mixer's own output buffer, not a property of a console or a sample rate:
+`SoundMixer::PlayAudioBuffer` resamples into `_sampleBuffer` (0x10000 `int16`, stereo: 0x10000 / 2 frames) and is given
+exactly that as its output cap, the whole buffer rather than half of it, and the one other `PlayBuffer` source (the
+pitch-adjust path, below 100 % emulation speed) is capped by a smaller buffer at 16384. A step loop that gates on
+`availableFrames() >= framesNeeded` therefore stops with at most one flush over a block, at every sample rate.
+`reserveAudio` sizes the ring, grows only, and carries what is queued across in at most two `memcpy`s;
+`capacityFrames()` and `overflowFrames()` exist for the tests.
 
-## 5. P5: the tap as a capability; P6: the build check
+**Overflow** drops the NEWEST frames, the ones that do not fit, copies the prefix that does, counts the rest in
+`overflowFrames_` and asserts in a debug build. The queued frames are the ones the pending block's sample indices and the
+N8 FIFO's already-scheduled bytes refer to, and they are what `availableFrames()` has already told the step loop it has,
+so overwriting them would move every later sample earlier in the block. The capacity makes a drop unreachable.
 
-**P5.** `SystemBase` gains `virtual ChannelTap* channelTap()`, null by default; `ChannelTap` is a small interface,
-`exportMode()` and `setExportMode(mode)`, which `MesenNesSystem` implements (the pattern `debugTarget()` already uses).
-`SystemTable`'s slot holds `ChannelTap* tap` instead of `MesenNesSystem* nes`, filled from `channelTap()` where the two
-casts were; `ArmedTap` remembers the tap it armed, compared by address as today (monotonic system ids keep that
-sound). `Engine` stops including `MesenNesSystem.hpp`. TS does not change: the tap arm still names a system id and a
-mode.
+**The block size comes with the build.** `SystemBuildRequest` carries `maxBlockFrames`, a system node sets it from its
+plan's maximum in `activate` before `build`, and each backend reserves it before `onActivate`, so the ring is the plan's
+from construction and the activation-time reserve is a no-op. A build outside a plan (a test, a tool) leaves it 0 and
+the core sizes for `kDefaultBlockFrames` (8192, the engine's default plan size, which a test holds equal to
+`Engine::kDefaultMaxBlockFrames`). A host that grows its block later reaches the same reserve through the reactivation
+`Engine::setMaxBlockFrames` performs while the graph is quiescent. Memory a plan does not need is not kept: a 64-frame
+plan's ring is 128.25 KiB where an 8192-frame plan's is 160 KiB.
 
-**P6.** The node's build compares the built core's `kind()` with the kind its type builds and then `static_cast`s; a
-mismatch is refused exactly as now.
+**Everything else a block touches is sized at activation.** Each Mesen core sizes its per-block scratches there
+(`stereoAccum_` two floats a frame, every `chanAccum_[k]` one) and at a larger reserve; the NES's tap rings
+(`NesSoundMixer::_streamBuf`, an in-tree patch) are sized by `SetCaptureCapacity(maxBlockFrames)` to
+`maxBlockFrames + MaxSamplesPerFrame` (12800, the most one `CaptureStreams` call appends), and a reset or a re-arm clears
+their queues but keeps their storage; and SameBoy's two scratch sets are sized the same way. A core's ingress queues are
+sized the same way too, from what one block's inputs can deliver, so a block's host input grows nothing (section 11).
+**An arm sizes nothing**:
+the two discards that used to size a scratch from the capture mode drain in block-sized chunks instead, so
+`setChannelExportMode` no longer grows anything. It still calls `SetChannelCapture`, which calls `SetCaptureCapacity` on
+every arm, so "sizes nothing" rests on the activation reserve having already made that call a no-op: an arm before
+activation, or at a block larger than the reserve, would size. The plumbing commit (section 9) took the arm's last
+allocation too:
+`blip_reset(m, size)` replays `blip_new`'s order exactly (the default factor, then `blip_clear`), `SetCaptureCapacity`
+creates all five stream blips with the rings, and an arm re-initialises one with `blip_reset` and then calls
+`blip_set_rates`, which is the state a fresh `blip_new` would reach at the same phase; a disarm keeps the blips for the
+next arm. Preallocating the blips *and* rate-setting them at capacity time shifted the tap's first samples by a
+sub-sample phase (`blip_clear` seeds the offset from `factor / 2`), which parity caught, so this keeps the phase and the
+allocation count both.
 
-After both, no cast to a concrete core is left outside `packages/native/src/system/`.
+**The promise** is the audio path's and, since section 9's commit, the whole block's: a warmed core's block allocates
+and frees nothing, emulation included, and since section 11's commit that holds with a block's host input staged into it
+too. Each queue that input lands in is bounded and sized when the core is built, so a press, a MIDI run, a serial byte
+or a raw message is refused and counted rather than grown. What still allocates inside a long run is Mesen's periodic
+savestate capture (0.5 s, its streaming serializer, charged outside every test window) and the N8 firmware's
+SD-command path (section 9).
 
-## 6. Tests
+## 5. P5 and P6, as built: the tap a capability, and the build check
 
-- **Parity** holds bit for bit at every commit, nothing re-recorded: the ring changes where samples wait, not which
-  samples are made. A moved case means the ring's semantics are wrong.
-- **`retroplug-engine-graph-test`:** section 4's zero-allocation case (red before P2); a ring case (fill, drain,
-  wrap, `availableFrames()` across a wrap, a resize on reactivation keeping what was buffered, the overflow counter);
-  the tap read through `channelTap()` (`NesSplitRouting.test.cpp`, the goldens' "each NES ends in the mode it was built
-  in"); a non-NES core offering no tap; a build whose kind mismatches its node refused (through a test seam: the real
-  host cannot produce one).
-- **ThreadSanitizer and AddressSanitizer** on every case touched; the profile benches still at zero allocations;
-  both consumer suites (BlipToaster, NESVJ) from scratch copies, their repositories untouched.
+**P5.** `SystemBase` gained `virtual ChannelTap* channelTap()`, null by default, in a "Per-channel audio tap" section
+beside `debugTarget()`'s capability pattern. `ChannelTap` ([ChannelTap.hpp](../packages/native/src/system/ChannelTap.hpp))
+is the interface in the global namespace, with `SystemBase` and `ChannelStream`, because the engine reads it as a core
+capability: `exportMode() const`, `setExportMode(std::uint32_t)` and a virtual destructor, and nothing else. A mode is
+the backend's own numbering (a NES's 0 is the mixed output and 1 to 3 are its split exports), and an arm must neither
+allocate nor restart the core: a routing flip must not reset a playing game. `setExportMode` runs between two blocks on
+the block thread, or on the control thread while the engine is quiescent.
 
-## 7. Landing
+**The NES is its own tap.** `MesenNesSystem` implements `ChannelTap` and `channelTap()` returns `this`: the mode is
+`MesenNesConfig::channelExportMode` and the arm is `setChannelExportMode`, so there is no state to own. That is what
+keeps `ArmedTap`'s address comparison sound: a secondary-base `ChannelTap*` is the core object's address plus a
+per-class constant, so within one class tap equality is core equality, the record cannot outlive the core, and C3's
+residual (a core allocated where the armed tap was freed would match) keeps its shape at the tap's address
+(section 12).
 
-Four commits, each green:
+**`SystemTable`.** A slot is `{ id, SystemBase* core, ChannelTap* tap }`, the tap filled from `channelTap()` where the
+two casts were (the plan's build and `publishGraph`'s pre-fill for a reused node), so a slot whose core offers none holds
+null and an arm reaches nothing. `Engine` no longer includes `MesenNesSystem.hpp` (it includes `system/ChannelTap.hpp`
+and forward-declares `ChannelTap`), and TS does not change: the arm still names a system id and a mode.
 
-1. **P1:** the dead surface.
-2. **P2:** the audio ring, the scratches, `_streamBuf`, and the zero-allocation case.
-3. **P5 + P6:** the tap capability and the build check (one seam: the engine and the node).
-4. **Docs:** spec/02 (the core surface, the tap, the ring, the allocation promise), spec/07, spec/08 (native
-   allocations are tested now), AGENTS.md's engine-graph paragraph, the main design's sections 6.4, 6.5 and 8, and
-   this design's status and section 10.
+**The mode range stays with the engine, as a deliberate residual.** `Engine::setGraph` still refuses a tap mode outside
+1 to 3, which is the NES's numbering rather than the capability's. TS owns the numbering and the arm travels as a byte,
+so a second console's tap would be validated against the NES's range; moving the bound onto the capability
+(`maxMode()`) is the later move, and needs no core change.
+
+**P6.** Each core class carries `static constexpr SystemKind kKind`, which its `kind()` returns, and the node's build
+compares what `rp.host.systems/1` handed back with `Core::kKind`, then destroys it and refuses with the same message on a
+mismatch, or `static_cast`s it on a match. `kKind` is on the core class rather than on the node type so there is one
+source of truth: the value the node compares with is the value the core answers by construction, so the two can only
+differ through an edit to the class, which would refuse every plan of that console at once. The cast's soundness is the
+invariant `SystemFactory` and `MesenBackend` already rest on, kinds being unique per core class: the config alternative
+names the console, the factory builds that console's class, and each class reports a kind no other does.
+
+After both, no unchecked cast to a concrete core is left outside `packages/native/src/system/`: `packages/native/src`
+has no `dynamic_cast` outside it, one `static_cast` in `SameBoySystem.cpp` (inside it, from GB user data), and the
+node's own checked `static_cast<Core*>`.
+
+**The one console name left in `src/host/` is the N8 cart mirror's, and it is NES by construction.** `Engine::setGraph`
+reads a `rp.n8.cart-mirror` node's settings as a system id, then refuses one whose document node is not
+`rp::host::systemNodeType(SystemKind::MesenNes)` (`Engine.cpp`), because the N8 mirror is a NES's cart reader and no
+other console's. That is a fact about the device, not a cast and not an arming, so it stays: what C4 did is make the
+tap and the build check name no console, not put the string `MesenNes` nowhere in `src/host/`. The tap's mode bound 1
+to 3 is the other console name `Engine` keeps (section 12).
+
+## 6. Tests, as built
+
+- **Parity** held bit for bit at every commit, nothing re-recorded: the ring changes where samples wait, not which
+  samples are made.
+- **The allocation cases** (`SystemNode.test.cpp`, `[alloc]`), each red before its fix:
+  - *the ring* (`[mesen][audio-ring]`): fill, drain, `availableFrames`; a wrap whose tail runs off the end and continues
+    at the front, the whole drained sequence checked frame by frame; a reserve that keeps the buffered frames and a
+    smaller one that does not shrink; a write past capacity that drops the newest and counts them (release builds only:
+    a debug build asserts there, by design); and a warm ring writing and draining 64 x 1024 frames with zero allocations
+    and zero frees. It also pins `kMaxPlayBufferFrames == 0x8000` and the default block size against the engine's.
+  - *the audio path* (`[system-node][alloc]`): a Mesen core drains without allocating at every ragged size (33, 1, 222,
+    1, 7, 4096, 2, 8192), the first block at each size being the counted one, for NES mixed, NES with its pins wired,
+    NES with the tap armed over the mix, SMS and GBA.
+  - *the whole block*: a warmed core's whole block allocates and frees nothing, emulation included: NES mixed, NES with
+    its tap armed, NES through the Engine (the whole graph block, so the system node's per-block work is inside the
+    window), SMS, Game Boy (the mix and the four stem pairs) and GBA. Each warms through the real triad with a button
+    press, which is where a device's one-time state sizing happens, and **each counted window stages a block's host
+    input inside it**, the way a node applies its inputs before the core's `prepareForBlock`: notes, clocks, a raw run
+    and a settings-length SysEx into a NES (and a section with the reliable link on, whose ROM capability byte is
+    flipped so the framing path really runs), the transport's levels into a SMS, MIDI runs and a press burst into a
+    Game Boy in both stream shapes, presses into every console, and through the Engine the same staging by the node's
+    own doors. The press counts and run lengths are past what a library deque allocates its first node for, so a queue
+    that grew on the block thread shows in a window even when it would not grow every block. Each counted window proves
+    it emulated (the node-level ones by the frames the capture holds, by the same measure the step loop gates on, or
+    SameBoy's audio frame count; the Engine one by its work RAM moving at the plan's largest size), so a window whose
+    step loop stopped cannot be vacuously free.
+  - *the live arm*: a tap armed on an activated NES sizes nothing on the block thread and its audio path stays free. It
+    drains more than the armed ring holds, so head and tail wrap mid-drain. It SKIPS where `countsCAllocations()` is
+    false (a sanitizer build, Windows, macOS), since a stream's blip, a C block, is what it would catch.
+  - *re-activation*: a node reactivation grows the ring to the new block, through `GraphHost::reactivate` (what
+    `Engine::setMaxBlockFrames` does).
+  - *the plan size*: a node sizes its core's block buffers to the plan it was built by, not the engine's default, and a
+    direct build keeps the default.
+- **The counting allocator counts C allocations on request.** `test/graph/CountingAllocator` replaced `operator new`
+  only; it now also interposes `malloc` / `calloc` / `realloc` / `free` and forwards to glibc's own, so a blip's or
+  Mesen's C block is seen. That is opt-in per window (`beginCountingAllocations(true)`), and it is off in a sanitizer
+  build (ASan and TSan own the allocator: the first attempt segfaulted TSan at startup), on Windows (the static CRT,
+  where defining `malloc` collides with `libcmt`) and on macOS (no glibc name to forward to); `countsCAllocations()` says
+  whether the binary sees them. Forcing it on changes exactly one existing case: the script block path's 448 allocations
+  are QuickJS's own, which that case measures through QuickJS's counters. The audio-path and whole-block helpers call
+  `beginCountingAllocations(true)` unconditionally and do not skip where `countsCAllocations()` is false: on Windows and
+  macOS they prove the operator-new half of the promise, where the live-arm case skips because a C block is its one
+  subject.
+- **The `[video]` hashes.** Three 4096-frame blocks per console, the frame the core published hashed by FNV-1a over its
+  width, height and XRGB8888 bytes, recorded with the video-path files restored to the commit before the plumbing and
+  equal after it: NES 14252490387203307000 three times (the boot screen, before bliptoaster draws), SMS
+  16816052646246439624, 16816052646246439624, 668230188373870873, Game Boy 8259550387572866035, 10074299620259861403,
+  13782190143996438851, GBA 3100562949282106931, 3100562949282106931, 2667698829920294451. The hashes are per console and
+  per ROM; a real video change re-records that console's row, as parity re-records a group. The NES's three are its
+  boot screen, before bliptoaster draws, so the console whose per-frame plumbing changed is held to one image; a later
+  change that wants the drawn path should hash frames after bliptoaster starts drawing.
+- **Mesen's shared classes, directly** (`MesenShared.test.cpp`): a cleared controller device's state stays empty (two
+  of the four devices that read emptiness as data depend on it; the other two are section 9's, not this file's) and a
+  hub's composite state round-trips a pressed button through a second hub; and a notification's listener copy survives a
+  listener destroying another in a nested send, the manager and its copy holding only `weak_ptr`s.
+- **The tap as a capability:** `NesSplitRouting.test.cpp`'s nine cases read it through `channelTap()`, the goldens'
+  "each NES ends in the mode it was built in" reads it (and fails on a NES slot that offers no tap, rather than skipping
+  the check), "the tap is a capability only a NES offers" holds a NES's tap and its own accessors to one mode and the
+  Game Boy, Master System and GBA to null, and the ScriptPacks reload case reads the tap rather than casting.
+- **The kind check:** "a node refuses a core built with another console's kind" is a 12-way matrix (each of the four
+  consoles against the three other kinds), requiring the refusal, `activate-failed`, the same-node message, no built
+  core and the core given back. It also pins kind uniqueness. The seam is `NodeRig`'s `reportKind`, which wraps the real
+  core in a `KindWrapper` that owns it, so the refusal's destroy frees the core and the rig stays truthful: the real host
+  cannot produce the mismatch.
+- `kind()` is no longer dead: the node's check is its production reader.
+- **The input queues**, added after this design's first draft (section 11): `FixedQueue.test.cpp` in
+  `retroplug-graph-test` holds a push past the bound and the count, a consumed prefix's compaction keeping the live
+  order, `FixedByteRing`'s wrap over more than its capacity and `stableSortEntries`'s stability on equal keys; a case
+  reads `ports.ts`'s four declarations ([nodes/ports.ts](../packages/retroplug/src/script/nodes/ports.ts)) and compares
+  them with `InputCapacity.hpp`, so either side drifting is visible; and a `[input-drops]` case has a section per core
+  queue (NES, SMS, GBA and Game Boy presses, a Game Boy's serial bytes and runs, an SMS's levels, a NES's N8 runs and
+  bytes) that pushes past the bound, holds the refused count and `inputAccepted()` before it, and then runs a block so
+  the core still fills. The audio suite has the FIFO's own cases (the permissive fifo_a past `_rxBound`, the wire past a
+  block's input), the NES's carried-run rebase, and the reliable link's 48-frame window giving up its oldest frame.
+- **ThreadSanitizer and AddressSanitizer** ran over every case touched (the `[alloc]` and `[input-drops]` tags, the
+  `[control-state]` / `[notification]` / `[video]` cases, the tap and kind cases, the audio suite's `[channelsplit]`
+  cases) with no report, and the profile benches stayed at zero allocations a block.
+
+## 7. Landing, as built
+
+Six commits, each green:
+
+1. **P1:** the dead surface: `refactor(system): SystemBase drops the members nothing calls`.
+2. **P2:** the audio ring, the scratches, `_streamBuf`, and the allocation cases:
+   `fix(mesen): a Mesen core collects its audio in a fixed ring, so its audio path allocates nothing`.
+3. **The Mesen plumbing** (section 2's second decision): `fix(mesen): a frame's video and input plumbing reuses its
+   buffers, so a whole block allocates nothing`.
+4. **P5 + P6:** the tap capability and the build check:
+   `refactor(engine): the NES tap is a capability any core may offer, so the engine names no console`.
+5. **The input queues** (section 2's third decision): `fix(system): a core's input queues are preallocated rings, so a
+   block carrying host input allocates nothing`. Its own commit, before the docs commit, which describes all six.
+6. **Docs:** spec/02 (the core surface, the tap, the ring, the allocation promise and the input bounds and counters),
+   spec/06, spec/07, spec/08 (native allocations are tested now), AGENTS.md's engine-graph paragraph, the main design's
+   sections 6.2, 6.4, 6.5 and 8, and this design's status, section 10 and section 11.
 
 Then the acceptance run. As C3: a prototype on a local branch (`proto/c4-cores`) on DeepSeek, each stage reviewed and
 fixed; the plan generated from it and replayed to identical trees; an Opus review of the plan before execution;
-Flash-Next implementers checked by tree identity; Opus code reviews of the two code-heavy tasks (2 and 3); follow-ups,
-an acceptance run and an Opus final review.
+Flash-Next implementers checked by tree identity; Opus code reviews of the two code-heavy tasks; follow-ups, an
+acceptance run and an Opus final review.
+
+## 8. Prototype questions, answered
+
+- **Does the zero-allocation case find any allocation besides the ring, `_streamBuf` and the scratches?** Yes, five
+  classes of them. Mesen's video and input plumbing, 13 to 18 allocations an emulated frame, which is neither ours nor
+  the audio path and is section 9's; our own N8 firmware parser, whose per-command vectors grew inside the block (7 at
+  4096 frames, 7 more at 8192), fixed there; the tap arm's three `blip_new`s, fixed there; SameBoy's lazy scratch
+  `assign`s plus its dead `pendingMidi_.insert`, fixed there; and, once the counted windows staged a block's host input
+  (section 11), every ingress queue a press, a MIDI run, a serial byte or a raw message landed in, fixed there. Nothing
+  else turned up in NES, SMS, Game Boy or GBA.
+- **Is Mesen's 32768-frame cap the true bound on one `PlayBuffer`, for every console and every sample rate?** Yes. It
+  is the mixer's output buffer (`_sampleBuffer`, 0x10000 `int16`, stereo: 0x10000 / 2 frames, passed whole as the
+  resampler's output cap), reached the
+  same way on all three consoles, and independent of the host rate; the pitch-adjust path is capped lower. The step
+  loops cannot accumulate more over a block because they stop as soon as `availableFrames() >= framesNeeded`, so the
+  overshoot is at most one flush. The overflow counter stayed 0 in every case, an 8192-frame block included.
+- **Does the case need `CountingAllocator` linked into `retroplug-engine-graph-test`, and how do the existing
+  no-allocation cases do it?** It was already linked (`cmake/tests.cmake` puts `test/graph/CountingAllocator.cpp` into
+  the binary, shared with `retroplug-graph-test`), and the cases bracket their window with
+  `beginCountingAllocations()` / `endCountingAllocations()` exactly as the existing ones do. It counts per thread, which
+  is why a window is the blocks the calling thread runs. The C half had to be added for the arm's blips and for Mesen's
+  C blocks, and is opt-in (section 6).
+- **Which tests read a P1 member, and what each moves to?** None did. The only `kind()` readers still compile because
+  `kind()` stayed; no C++ or TS test read the sav accessors, `clearSram`, `loadStateBytes`, the `fastBoot()` getter, the
+  core-side `midiOut()` or the memory-snapshot quartet. So no test moved and none was deleted.
+
+## 9. The Mesen plumbing stage, as built
+
+Section 2's second decision, one commit of its own between the ring and the tap. On the commit before it, over 4 x
+8192-frame blocks of real emulation: **738 allocations** for a NES and 738 for a SMS, about 18 an emulated frame, on the
+audio thread. On it, the same windows are 0 allocations and 0 frees. Attributed by source with a temporary
+return-address hook (removed before the commit):
+
+| Mesen site | per frame | fix |
+|---|---|---|
+| `BaseControlManager::GetPortStates` | 3 (vector growth) | fills the manager's own `_portStates` and returns it by `const&` |
+| `BaseControlDevice::GetRawState` | 2, one a device | a new `GetRawState(ControlDeviceState&)` fills the caller's state, whose vector keeps its capacity; the returning form delegates to it, so its other callers are untouched |
+| `RenderedFrame`'s `InputData` | 12 | `RenderedFrame::Reset` + `SetInputData` assign in place, a frame member per PPU (`BaseNesPpu`, `SmsVdp`, `GbaPpu`), `VideoDecoder::_convertedFrame`, the persistent frames refilled rather than assigned, and the by-value `RenderedFrame` parameter made `const&` |
+| `NotificationManager::SendNotification` | 1 | the copy it iterates (it must be a copy: a listener may unregister mid-notify) is reused per thread and per nesting depth, pooled as `unique_ptr<vector<weak_ptr<...>>>`, so growth moves pointers and never the buffer a send is iterating |
+
+`VideoRenderer::UpdateFrame`'s `_lastFrame = frame` was on the list and turned out to allocate nothing, since copy
+assignment into a persistent member reuses its vectors' capacity; it needed no change and still measures zero.
+
+**The `ClearState` contract is kept, which is what made it delicate.** `_state = ControlDeviceState()` freed the state's
+vector and left it EMPTY, and an input poll clears every device before re-reading it. Four devices read emptiness as
+data: `ControllerHub::UpdateStateFromPorts` appends `[len][bytes]` to what a clear leaves, and `BarcodeBattlerReader`,
+`DatachBarcodeReader` and `FamilyBasicDataRecorder` treat `size() > 0` as "the tape plays". Sizing the state to its
+maximum (64 bytes) in place therefore broke them, the Epoch reader throwing `std::length_error` from every poll. So the
+state stays EMPTY (`_state.State.clear()`) and its capacity grows to `ControlDeviceState::MaxBytes` at the device's first
+poll, and `GetPortStates` sizes each snapshot entry's state before filling it and pads it to 64 bytes, so a port that
+appears late (a mapper or role device the control manager registers a few frames in) does not grow five snapshot
+destinations inside a block. A savestate blob grows by about 62 bytes a device as a result; old savestates still load,
+since Mesen reads a vector's length from the blob. The redundant `clear()` in `SetTextState` went.
+
+**What the same commit fixed in our code.** The N8 FIFO's parser reserved its per-command vectors inside the block:
+`_params` is now reserved to the protocol's largest command frame (`3 + 0xFFFF`) at construction and `_writeBlock` to
+`ACK_BLOCK_SIZE`. The arm's three `blip_new`s went to `blip_reset` (section 4). SameBoy's scratch sets are sized from
+`blockFrames()` at activation and in `reserveAudio`, so `prepareForBlock` only zeroes them, its lazy growth kept as the
+fallback for a direct core run at a bigger block than it reserved; and its dead `pendingMidi_` and `onMidi` override are
+gone (P1's finding: nothing read them, and the `insert` could allocate on the block thread), so a MIDI-framed event to a
+Game Boy now reaches `SystemBase`'s no-op.
+
+**The SD path, what was cheap and what remains.** `execFileRead` and `performDma` share one `_ioScratch` member instead
+of allocating a vector per command, and `_rxQueue` is a vector-backed FIFO (a head index, compacted as it is read,
+reserved to `FILE_READ_MAX + 1`) rather than a `std::queue`, whose deque allocates its first node inside a block. What
+still allocates there, and why:
+
+- `readParamString` returns a `std::string` a command, and `toHostPath` builds a `std::filesystem::path` plus strings:
+  the allocation is inherent to those types, and an SD command is a ROM-initiated once-a-command event, not per frame.
+  `_openFilePath`, `_openFile`'s buffer and `create_directories`' path are the same class.
+- `execDirLoad` fills `_dirRecords` (cleared but not shrunk between listings, so only a bigger listing grows it), and
+  each `EdioDirRecord::name` is a `std::string` from a `directory_entry`.
+- `_txQueue` (CMD_USB_WR with no sink) grows while a ROM writes and no host drains it, unbounded on purpose: a ring
+  would change the "never drop a byte the device would deliver" contract.
+- `_ioScratch` grows to the largest single read a ROM asks for; the first read of a new size is the one allocation.
+
+None of them is reachable from the block cases (the in-repo NES ROM never issues an SD command), so the whole-block case
+is ROM-dependent for the SD path.
+
+## 10. Found while building
+
+The findings that changed a section are in it: the ring's overflow policy and the block size in the build request in
+section 4; the tap's interface, the NES being its own tap and the mode range staying with the engine in section 5; the
+allocation cases, the C-counting options and the `[video]` hashes in section 6; the plumbing's four Mesen sites, the
+`ClearState` contract and the SD path in section 9; and the ingress queues, their bounds and the in-place sort in
+section 11. The ones worth naming on their own:
+
+- **The design's "zero allocations" could not hold for a Mesen console as vendored** (sections 2, 4, 9). Mesen's video
+  path allocates 13 to 18 times an emulated frame on the audio thread, so any window with an emulated frame in it can
+  never be zero. The stage that found it kept the promise strict by counting a drain of a pre-filled ring; the plumbing
+  commit then fixed the path, which is what makes the whole-block promise literally true.
+- **C2's finding was not what it looked like** (sections 4, 8). Its "8 allocations over two 512-frame blocks" that the
+  design attributed to the first block after a setting change are the video path's per-frame cost, present in every
+  block; C2's idle-versus-edited comparison was equal because both sides paid it. This design's section 1 has no such
+  item left.
+- **The blips' phase is audible** (section 4). `blip_clear` seeds the blip's fractional offset from `factor / 2`, so
+  *when* a blip is created and rate-set moves the tap's first samples. A "pure" preallocation is therefore an audio
+  change, and parity caught it (`parity/edits-structure`, `pinSplit-then-stereo`, lane 0: the same RMS and a different
+  digest) before it landed; the capture mode's arm and disarm also run on every plan change while tapped.
+- **The periodic publish is a trap for any `[alloc]` case** (sections 4, 6). It fires inside `finishBlock` every 0.5 s
+  and allocates thousands of times in Mesen's serializer, and it is only visible once a case drains more than 24 k
+  frames. The cases on an Engine-built core charge a whole interval uncounted before each counted stretch.
+- **The C interposer must stay off under sanitizers** (section 6). Under TSan, defining `malloc` and `free` segfaulted
+  the binary at startup, a block the sanitizer's allocator made being freed through glibc's. So the sanitizer runs hold
+  the operator-new half of every case, and the arm's blip count is only visible in a plain build.
+- **`ClearState` mattered as much as `EnsureCapacity`** (section 9), and the first version of the fix broke the
+  controller hub and the two barcode readers and the Family BASIC recorder, which is why the case is in the suite.
+- **The counted allocation every dozen blocks with the tap armed over a mix** turned out to be a video frame: the step
+  gate is then the tap's ring, which `finishBlock` discards, so each 1-frame block emulates a whole flush window and a
+  video frame lands every dozen (sections 6, 9).
+- **Without the block size in the build request, a memory saving was lost** (section 4). A core built at the 8192
+  default and then reserved down keeps the larger ring for its life, so a DAW running 64-frame blocks would pay for a
+  plan it never runs; the request's `maxBlockFrames` is what sizes it to the plan from construction.
+- **The promise's counted windows carried no host input** (sections 4, 6, 11). The cases warmed with a press and then
+  staged nothing, so every queue a block's input lands in could grow on the block thread with no case seeing it. The
+  extended case found 7 allocations and 7 frees at the NES's first counted window (11 and 7 with the reliable link on),
+  1 and 4 through the Engine, and 4 and 2 on a Game Boy carrying presses and serial bytes, all of them queues; the fix
+  is section 11's.
+- **libstdc++'s `std::deque` allocates its first node eagerly** (sections 6, 11). A one-block burst of tens of entries
+  hides a deque regression, so the `[alloc]` case's press counts and run lengths (64 presses, 24 messages a block) are
+  sized past that on purpose; a later case that stages less will not see a re-introduced deque.
+- **`std::stable_sort` allocates** (section 11). It takes a temporary buffer, so the two roles' once-a-block offset
+  sort was an allocation on the block thread; `rp::stableSortEntries` is an in-place insertion sort, the same trade
+  `Engine::Staging::sortByFrame` makes for staged host MIDI.
+- **The N8 role's rebase must compact its arena in ascending byte order, not queue order** (section 11). The sort by
+  offset can permute the live runs, and a forward compaction in queue order overwrote a run it had not copied yet, so
+  two feeds that both carried a run past the block end could swap payloads; a case stages exactly that and reads each
+  payload back.
+- **The permissive FIFO profile still has a storage bound** (sections 9, 11). `_rxCapacity == 0` means no modelled
+  depth, not an unbounded vector: fifo_a's delivered bytes are reserved to `2 * _rxBound` (the live bound plus room for
+  the consumed prefix `pop` has not compacted yet), and a reply past `_rxBound` is dropped and counted even then.
+- **The engine names one console still, and it is the cart mirror's** (section 5). `Engine::setGraph` checks a cart
+  mirror's system for `SystemKind::MesenNes`, which is a fact about an N8 being a NES's cart reader; a reader grepping
+  for a console name in `src/host/` finds that one, plus the tap's 1-to-3 mode bound, and neither is a cast.
+- **The kind check's red was heap corruption, not a clean failure** (section 5). With the comparison removed, the node
+  casts the wrong core and runs the Game Boy's application through a `KindWrapper`, so the failure is an immediately
+  unsound call rather than a later one.
+- **`MesenNesSystem` now has a second base**, so `ChannelTap`'s vtable is part of its layout (section 5). Nothing
+  serialises the class or measures its size; the only ABI surface is the plugin's own, rebuilt with it.
+- **`Engine`'s validation still belongs to the NES** (section 5, section 12): the tap is a capability but its mode bound
+  is not, which only a second tap would show.
+- **The suite keeps its console-only casts** (section 6): `EngineGraph.test.cpp`, `SystemNode.test.cpp` and two others
+  still `dynamic_cast` to read a console-only member (a config field, `linkPeers_`, a ring counter), which is the core's
+  own business rather than the engine's. What the tap capability changed is that no tap read casts any more.
+- **`cmake --build build` does not relink the Catch2 binaries** (a build trap, not a finding of one section): they are
+  `EXCLUDE_FROM_ALL`, so a library edit needs the test target built by name before a run, or a stale binary shows a
+  fixed bug as unfixed.
+
+## 11. The input-queue stage, as built
+
+Section 2's third decision, one commit of its own between the tap and the docs. It answers the promise's one hole: the
+`[alloc]` cases warmed with a press and staged nothing inside their counted windows, so a warmed core's block was free
+only while its node handed it nothing. Extended to stage a block's input inside every window, the case failed at its
+first counted window on every console: 7 allocations and 7 frees on a NES's mixed block, 11 and 7 with the reliable link
+on, 7 and 7 with the tap armed, 1 and 4 through the Engine, 1 and 1 on a SMS, 4 and 2 on a Game Boy carrying presses and
+serial bytes. On the commit, every section is 0 and 0.
+
+**What each queue became.** Every ingress queue is a fixed ring (`util/FixedQueue.hpp`: `rp::FixedQueue<T>`, one
+reserved vector and a head index, `push` refusing past `capacity()` and counting; `rp::FixedByteRing`, a byte ring
+consumed front-first; `rp::stableSortEntries`, an in-place stable insertion sort) sized when the core is built:
+
+- **The three Mesen consoles' presses** were a `std::vector` grown by `push_back` (5 allocations for 12 presses). Now a
+  `FixedQueue` of `kPressInput` 320 = `kButtonsInput` 64 + `GraphHost::commandCapacity()` 256, the two doors a block
+  can press through (the buttons input, and `Engine::applyCommand(PressButton)` on the command ring), with a
+  `static_assert` holding the ring's 256 to the host's. `prepareForBlock` already emptied the queue every block.
+- **A Game Boy's presses** were a `std::deque`, whose first node is allocated eagerly. Now a `FixedQueue` of 1024,
+  sixteen blocks of a saturated buttons input, because a press is scheduled 10 ms into the future and carried across
+  blocks; a press past it is dropped and counted.
+- **A Game Boy's serial-in** was a `std::deque<PendingByte>`, a byte an entry. Now runs in a `FixedQueue` (a message's
+  bytes share its frame, so they extend one run) over a 147456-byte `FixedByteRing`, two blocks of `serialIn` or one
+  `serialOut` block; the front byte a bit shift reads is the ring's head, so nothing moves and nothing grows.
+- **The NES N8 role's two feeds** were `std::deque<PendingByte>` for the raw feed and, under the reliable link,
+  `std::deque<PendingMsg>` with a fresh `std::vector` per message. Now one run queue of 4352 descriptors over one
+  74752-byte arena, shared by both feeds and both modes: a run's bytes stay where they were appended and only the
+  descriptors sort, so a long SysEx is one descriptor and one copy.
+- **A Master System's sync levels** were a `std::deque<PendingLevel>`. Now a `FixedQueue` of 74752 levels,
+  `kMidiInput.bytes + kBytesInput.bytes`, because a message longer than a `MidiEvent` reaches the raw door at up to
+  72 KiB; a level past it is dropped and counted.
+- **The reliable link's sender window** was a `std::deque<Frame>` plus a `std::vector` a frame plus a `std::vector`
+  collecting the message being parsed. Now 48 inline frames of `3 + kMaxMessage` bytes, the parsed message a
+  `kMaxMessage + 2` array (one longer was never framed before either), the pending sync two bytes.
+- **The N8 FIFO's two stages**: its wire queue was a `std::queue` (a deque) and fifo_a's delivered bytes a vector that
+  could grow past their bound. Now the wire is a `FixedQueue` of a block's host input (74752 bytes), and fifo_a's
+  bytes are reserved to `2 * _rxBound` (`_rxBound` = 78849, a block's input plus `FILE_READ_MAX` plus one), because
+  `pop` only compacts the consumed prefix once its head passes half the vector's size.
+
+The bounds are `system/InputCapacity.hpp`'s: what the outputs that feed a system node's inputs declare (a script
+node's `midi` and `serialIn` 4096 messages / 72 KiB, its `bytes` 256 / 1 KiB, its `buttons` 64; a Game Boy's
+`serialOut` 8256 / 8256), which are the numbers `script/nodes/ports.ts` gives, and a case holds the two equal. A core
+cannot read its node's inputs when it builds (a wire carries its source's queue, and an input is bound only after
+`activate`), so the constants are the contract rather than the plan's own capacities.
+
+**What a refusal costs.** A push past a bound is dropped, not grown: `FixedQueue::push` answers false, the queue counts
+it, and each core reports `inputAccepted()` and `inputDropped()` (`SystemBase`), which sum its queues. A run past the
+N8 role's bound is dropped whole and counted as its bytes; a level past the SMS bound likewise. The reliable link is
+the one exception: a full window gives up its oldest frame and re-syncs, the policy it already had, so it refuses
+nothing and has no dropped count.
+
+**The FIFO's permissive profile now has a storage bound.** `_rxCapacity == 0` meant no depth limit and a vector that
+could grow; it now means no modelled depth, since a reply is still refused past `_rxBound` and counted in
+`_droppedBytes`. The wire's own staging is a block's host input for the same reason: it stands for the host's outbound
+buffer, and a host that outruns a modelled wire by more than a block is now a counted drop.
 
-## 8. Prototype questions
+**What did not change.** Every queue's accepted behaviour is the same: the same bytes reach the cores in the same order
+at the same frames, the N8 role's sort and drain order are unchanged, and the link frames the same bytes with the same
+sequence numbers. Parity held bit for bit with nothing re-recorded, and the `[video]` hashes are the ones they were.
+The stage's one real bug is section 10's rebase finding: a case stages two runs past the block end and fails as
+`217 == 144` before the fix.
 
-- Does the zero-allocation case find any allocation besides the ring, `_streamBuf` and the scratches (Mesen's
-  emulation itself, the N8 FIFO, the GBA or SMS paths)? Where, and is it ours?
-- Is Mesen's 32768-frame cap the true bound on one `PlayBuffer`, for every console and every sample rate the engine
-  supports?
-- Does a test of the zero-allocation case need `CountingAllocator` linked into `retroplug-engine-graph-test`, and how do
-  the existing no-allocation cases do it?
-- Which tests read a P1 member, and what each moves to.
+**Where a drop is visible.** `inputAccepted()` / `inputDropped()` on the core, and
+`coreTransportStats().droppedBytes` for the FIFO. Neither is surfaced in the host yet: `GraphHost`'s per-instance drop
+accounting takes them nowhere, because `rp_process` carries no instance-drop pointer, so they are read by a test and by
+the core itself (section 12).
 
-## 9. Left as it is
+## 12. Left as it is
 
 - **P3, the ROM borrow:** a system holds its ROM in its node's pinned resource and again in its core (up to 32 MB for
   a GBA); a true borrow needs a regenerated vendored SameBoy patch, and an SMS re-reads its staged file on reset.
 - **P4, the snapshot double copy:** the registry copies each core's published frame and savestate again; that copy is
   the seam the rule "the control plane never touches a live core" rests on.
 - **P7, the capability split and per-console node files:** best done when C5's hardware nodes need the interfaces.
-- **Mesen allocating in the first block after a setting change** (C2's finding).
+- **The N8 firmware's SD path still allocates** (section 9): `std::string` and `std::filesystem` in the command
+  handlers, a bigger directory listing, and `_txQueue`'s unbounded growth. Bounding the queue would change the "never
+  drop a byte the device would deliver" contract, and a case that drove SD commands would have to account for the rest.
+- **The 0.5 s state publish allocates** inside Mesen's streaming serializer, on the block thread, every 24 k frames at
+  48 kHz. It is the one allocation a long run of an Engine-built core makes that neither a ROM command nor an armed
+  serial-out capture triggers, and it is why every `[alloc]` case on such a core charges an interval first.
+- **The cores this fork does not run** (PCE, SNES, WS) still copy a frame's snapshot once more per emulated frame;
+  `RenderedFrame`'s by-value constructor says so. Only the NES, SMS / Game Gear and GBA paths are copy-free.
+- **The tap's mode bound is `Engine`'s, not the capability's** (sections 5 and 10): the later move is `maxMode()` on
+  `ChannelTap`, beside the mode it validates.
+- **`C3`'s stale-tap-address limitation keeps its shape** at the tap's address (section 5): the record survives a drop
+  and reclaim, and a core whose tap landed where the armed tap was freed would be restored onto. TS cannot get there.
+- **The input-drop counters are not surfaced yet** (section 11): `inputAccepted()` / `inputDropped()` on a core and
+  `coreTransportStats().droppedBytes` on the FIFO are read by tests and by the cores, not reported beside `GraphHost`'s
+  per-instance output drops, since `rp_process` carries no instance-drop pointer and an input-drop service on the node
+  seam is its own change.
+- **Two output-side allocations remain** (section 2's third decision's scope was inputs): the N8 `_txQueue` is a
+  `std::vector` a debug `drainCoreBytes` RPC swaps empty, so a non-link ROM's next `CMD_USB_WR` after a drain grows it,
+  and a Game Boy's `serialOutLog_` is cleared each block but its capacity grows to the largest block's capture while
+  MI.OUT capture is armed. Neither is on the steady-state path the `[alloc]` cases hold.
````

- [ ] **Step 2: The specs, and the comment that named a mechanism that does not exist**

spec/02's Allocation bullet leads with the promise and names what still allocates; its `setGraph` paragraph annotates
the mode bound and gives `SystemTable`'s slot; its `SyncTap` paragraph says the arming goes through the tap and
`armedTap_` holds its address; its `setMaxBlockFrames` line says the reactivation is quiescent; its
`SystemBuildRequest` paragraph gains `maxBlockFrames`; it gains `MesenAudioDevice`'s paragraph, `SystemBase`'s
surface, the cores' row, the ingress bounds and counters, and the deleted memory-subscription mechanism. spec/06's
`test:plugin` row gains C4's cases; spec/07's C2 leftovers and C4 entry say what C4 built and left; spec/08 gains the
C++ suite's counting allocator. `MemoryType.hpp`'s comment says the values are the `type` a `getMemory` call carries:

````diff
diff --git a/packages/native/src/system/MemoryType.hpp b/packages/native/src/system/MemoryType.hpp
index 625e3013..dd87693c 100644
--- a/packages/native/src/system/MemoryType.hpp
+++ b/packages/native/src/system/MemoryType.hpp
@@ -43,8 +43,8 @@ namespace rp {
 // with kMemoryTypeCount sizing SystemBase::StateRegionTable - so it is a
 // wire-format change, not a local one.
 //
-// The integer values double as the wire byte used in subscribe / getMemory
-// RPC calls; keep them stable.
+// The integer values are also the `type` a getMemory call carries (the debug
+// facet's peek), so keep them stable.
 enum class MemoryType : std::uint8_t {
     Ram          = 0,
     Rom          = 1,
diff --git a/spec/02-native-host.md b/spec/02-native-host.md
index 7b601d55..156adda6 100644
--- a/spec/02-native-host.md
+++ b/spec/02-native-host.md
@@ -308,8 +308,9 @@ A system's live knobs never ride a host command: they are its system node's sett
 which the node gives its core at the start of a block. `Engine::setSampleRate` re-rates the running plan's
 cores in slot order, then re-activates the graph's buffers at the new rate;
 `Engine::setMaxBlockFrames` grows the plan's block size to a host's maximum (never below 8192, the
-largest block the parity matrix holds), and the plugin and the SDL standalone call it before handing
-the engine over.
+largest block the parity matrix holds) and re-activates the graph while it is quiescent, where each core's
+`reserveAudio` moves its audio ring and its per-block scratches with no block in flight; the plugin and the SDL
+standalone call it before handing the engine over.
 
 ### Host I/O, the system nodes' inputs and queue capacities
 
@@ -371,12 +372,21 @@ with slack, rather than sized from the plan's block size, so a feedback wire fro
 across every swap; a CGB clocking the port itself on its fast clock can outrun it in a very long block, and
 what does not fit is dropped and counted.
 
-The core side is bounded too, and fixed: each core sizes its ingress queues (a Game Boy's serial runs, a
-NES's FIFO runs and frame window, a Master System's levels, each console's presses) when it is built, from
-what one block's input can deliver ([InputCapacity.hpp](../packages/native/src/system/InputCapacity.hpp),
-the capacities the sources' outputs above declare), so a block that carries host MIDI, raw bytes, serial-in
-bytes or presses allocates nothing in the core. An input a queue cannot take is refused and counted, the way
-a push past a graph queue's capacity is dropped and counted.
+The core side is bounded too, and fixed: each core sizes its ingress queues when it is built, from what one
+block's input can deliver ([InputCapacity.hpp](../packages/native/src/system/InputCapacity.hpp), the
+capacities the sources' outputs above declare: `serialIn` and `midi` 4096 messages / 72 KiB, `bytes` 256 /
+1 KiB, `buttons` 64, a Game Boy's `serialOut` 8256 / 8256), so a block that carries host MIDI, raw bytes,
+serial-in bytes or presses allocates nothing in the core. The queues are a Mesen console's presses (320: a
+block's 64 plus `GraphHost::commandCapacity()`'s 256, a `static_assert` holding the two together), a Game
+Boy's presses (1024, sixteen blocks of a saturated buttons input, because presses are scheduled ahead) and
+its serial-in (a run a message over a byte ring of two blocks' `serialIn` or one `serialOut`), a NES's N8
+runs (4352) and their bytes (74752), and a Master System's levels (74752, in bytes from both doors). A push
+past a bound is dropped rather than grown, and counted: the core reports `inputAccepted()` and
+`inputDropped()`, and the N8 FIFO's `coreTransportStats()` adds `droppedBytes`, the same vocabulary as a
+graph queue's overflow. The reliable link refuses nothing: a full 48-frame window gives up its oldest frame
+and re-syncs. The FIFO's permissive profile (`_rxCapacity == 0`) means no modelled depth, not no storage
+bound: fifo_a still drops and counts a reply past `_rxBound` (a block's input plus `FILE_READ_MAX`), its
+vector reserved to twice that because `pop` compacts the consumed prefix lazily.
 
 An events or bytes output may declare its queue's capacity (`rp::bytes_out::sized<rp::capacity{...}>` in
 C++, `events.out(capacity)` / `bytes.out(capacity)` in TS), by default 256 messages and 4096 bytes, at
@@ -470,10 +480,18 @@ wire could drive; everything else, display knobs, accuracy switches and latencie
   first value is snapped into the core (whose gain smoother starts at 0 dB), so its first sample plays at the
   document's level; every later gain change is smoothed, as it always is. A document without a param leaves its
   stream where it was: at its default, on a new node.
-- **Allocation.** A Game Boy's knobs, the ones it applies in place, a NES's setters but its region, and every param
-  allocate nothing on the block thread. A Game Boy's model change restarts the core (`GB_alloc` / `GB_init`), which
-  allocates, as it did on the command ring; a NES's region change resets it, which allocates too. The NES core also
-  allocates inside Mesen in its own blocks.
+- **Allocation.** A warmed core's whole block allocates and frees nothing, emulation included: its audio ring and its
+  per-block scratches are sized at activation (the block size arrives in the build request, `maxBlockFrames`, and a
+  later host growth re-activates the graph quiescently, where `reserveAudio` moves the sizes with no block in flight),
+  and Mesen's per-frame plumbing reuses its buffers. That holds with a block's host input staged into it too: every
+  ingress queue a press, a MIDI run, a serial byte or a raw message lands in is bounded and sized when the core is
+  built, and a push past its bound is dropped and counted (above), so the counted window may carry what a node applies
+  before the core's `prepareForBlock`. What still allocates on the block thread is work that makes
+  something: a Game Boy's model change restarts the core (`GB_alloc` / `GB_init`), which allocates, as it did on the
+  command ring; a NES's region change resets it, which allocates too; the 0.5 s state-snapshot capture allocates inside
+  Mesen's streaming serializer; and the N8 firmware's SD path allocates in its command handlers, in a bigger directory
+  listing and in an unbounded TX queue ([07-remaining-work.md](07-remaining-work.md)). A Game Boy's knobs, the ones it
+  applies in place, a NES's setters but its region, and every param allocate nothing at all.
 - **A refused document** can still have given knobs to a node it reuses, since activation runs while it plans: a
   reused node's core keeps them, and the next document in the app carries the same. A core a refused plan's new node
   built is destroyed as that plan's instances unwind, with its snapshot slot and its resource pins.
@@ -491,11 +509,12 @@ name:
   handed back (the GraphHost's plans, the script host's released instances), then checks that the
   document's system nodes and the slots name the same systems, each node `sys-<id>` with settings naming
   its system (`"system": <id>`) and its ROM (`"rom": "res:N"`), that a tap arm names one of the slots and a mode
-  from 1 to 3, that there is at most one
+  from 1 to 3 (the NES's own numbering, which the engine validates rather than the capability: the arm travels as a
+  byte TS numbers, and a NES's tap is the only one today), that there is at most one
   `rp.host.midi-in` and one `rp.host.controller-in`, and at most one `rp.n8.cart-mirror`, naming one of
   the slots' NES systems (any of these refuses the document before anything is built); builds the plan's
-  `SystemTable` (a slot-ordered list of `{ id, SystemBase*, MesenNesSystem* }`, the NES pointer for the
-  channel tap, plus the system the cart mirrors,
+  `SystemTable` (a slot-ordered list of `{ id, SystemBase*, ChannelTap* }`, the tap filled from
+  `SystemBase::channelTap()` and null for a core that offers none, plus the system the cart mirrors,
   [SystemTable.hpp](../packages/native/src/host/graph/SystemTable.hpp)) from the slots and the live cores, a slot
   whose node is reused from the previous plan already filled;
   turns `tap` into the swap's `SyncTap` trailer; and hands both to `GraphHost::setGraph`, which computes
@@ -621,14 +640,16 @@ cleanly.
 Heavy, rare payloads (RAM writes, long MIDI) cross as raw **owning** pointers: the accepted
 non-RT-on-rare-op pattern. At drain the GraphHost hands each host command to `Engine::onHostCommand`,
 which applies it (`applyCommand`) against the `SystemTable` of the plan current at that point, and
-`delete`s owning payloads after apply. `SyncTap` is the NES channel-tap
-arming: restore whatever `armedTap_` armed, then, when the request names an arm, set that NES to its
-export mode (either pin split 1, `ChannelSplit` 3, as `buildGraph` decides for a split routing over a
-single NES), recording what it replaced. TS sends it on every edit that builds or drops a core and on
-every routing change, even when the wanted mode is unchanged, and each run resets the capture streams;
-it rides the plan's swap, so no block runs between the two. `armedTap_` holds the core pointer as well
-as the id, so a core that has gone is never restored onto another. A param's value from `setParam` rides
-the same ring as a command of the GraphHost's own, not a `DspCommand`, into its param's control stream.
+`delete`s owning payloads after apply. `SyncTap` is the channel-tap arming: restore whatever `armedTap_` armed,
+then, when the request names an arm, set that core's tap to its export mode (either pin split 1, `ChannelSplit` 3, as
+`buildGraph` decides for a split routing over a single NES), recording what it replaced. A core that offers no tap
+(`channelTap()` null) has nothing to arm, so the arm reaches nothing. TS sends it on every edit that builds or drops a
+core and on every routing change, even when the wanted mode is unchanged, and each run resets the capture streams;
+it rides the plan's swap, so no block runs between the two. `armedTap_` holds the tap's address as well as the id, so a
+duplicate id does not restore onto another core; a NES's tap is the core object, so the address differs from the core
+pointer's by a per-class constant and, within one class, tap equality is core equality. A core allocated where the
+armed tap was freed still matches, which TS cannot reach (design section 12). A param's value from `setParam`
+rides the same ring as a command of the GraphHost's own, not a `DspCommand`, into its param's control stream.
 
 **Ownership handoff.** A displaced or removed core leaves with the plan that last ran it, and is
 freed on the control thread when that plan is reclaimed (`reclaimReleased` → `GraphHost::reclaim`):
@@ -705,9 +726,14 @@ registered.
 
 Every backend takes a
 [`SystemBuildRequest`](../packages/native/src/system/SystemFactory.hpp) by value and consumes it: the ROM bytes, the
-`sram`/`savestate` seed bytes, the ROM's `romName`, and a `SystemConfig`
-(`std::variant<SameBoyConfig, MesenNesConfig, MesenSmsConfig, MesenGbaConfig>`, whose alternative names the
-console). A [`SystemBackend::build`](../packages/native/src/system/SystemFactory.hpp) runs on the
+`sram`/`savestate` seed bytes, the ROM's `romName`, the largest block the plan will run (`maxBlockFrames`, 0 for a
+build outside a plan) and a `SystemConfig` (`std::variant<SameBoyConfig, MesenNesConfig, MesenSmsConfig,
+MesenGbaConfig>`, whose alternative names the console). The block size is what a core sizes its audio ring and its
+per-block scratches to *before* it activates, so a 64-frame plan's core never holds an 8192-frame plan's buffers; a core
+built outside a plan keeps `SystemBase::kDefaultBlockFrames` (8192, held to `Engine::kDefaultMaxBlockFrames` by a test;
+Mesen cores size from `MesenAudioDevice::kDefaultBlockFrames`, held to the same constant, so two names stand for one
+contract until they collapse).
+A [`SystemBackend::build`](../packages/native/src/system/SystemFactory.hpp) runs on the
 control thread (heavy, non-RT) and returns an **already-`onActivate`'d** system, or nullptr with the reason
 in `*why`. The bytes come from the node's resources (its `build` copies them out of the pins its instance
 holds into the request, and the core takes them from there), so nothing here reads a file or decodes a role config.
@@ -721,6 +747,7 @@ gate), and builds the core:
 
 ```cpp
 auto sys = std::make_unique<SameBoySystem>(id, std::move(typed), request.rom);
+if (request.maxBlockFrames) sys->reserveAudio(request.maxBlockFrames);  // the plan's block, before any block runs
 sys->onActivate(sampleRate);       // boots gb_ + restores sram then savestate
 sys->enableStateSnapshot();        // tear-free savestate each block for the registry / Duplicate
 ```
@@ -742,10 +769,20 @@ serving three consoles, picked by the config alternative: `buildNes` gates `RomF
 `MesenNesSystem`, `buildGba` gates `RomFormat::Gba` and builds a `MesenGbaSystem` (empty `biosPath` → HLE
 boot ROM), and `buildSms` refuses a ROM that is positively another console's but accepts a headerless one
 (TS admits homebrew by its extension). The NES and GBA cores take their seeds through the config; the SMS
-takes the `romName` it stages its file under. All three run the same tail, `onActivate` then
+takes the `romName` it stages its file under. All three run the same tail, `reserveAudio` then `onActivate` then
 `enableStateSnapshot`, and refuse a boot Mesen's `LoadRom` rejected after the magic gate ("Mesen refused the
 ROM"), so NES/GBA/SMS cores publish a tear-free savestate for the registry / Duplicate just like SameBoy.
 
+Their audio arrives through [`MesenAudioDevice`](../packages/native/src/system/mesen/MesenAudioDevice.hpp), a
+fixed-capacity ring of interleaved stereo `int16` sized `maxBlockFrames + kMaxPlayBufferFrames` (32768: the most one
+Mesen `PlayBuffer` can carry, the whole of the mixer's own output buffer, `_sampleBuffer`'s 0x10000 `int16` stereo,
+which `PlayAudioBuffer` is given exactly as its output cap). `availableFrames()` is its fill, which is
+the block clock the step loops and the N8 FIFO's scheduling gate on; `PlayBuffer` writes at the tail, `drain` reads from
+the head, and a write past capacity drops the newest frames and counts them rather than moving the queued ones (the
+bound makes it unreachable; a debug build asserts). The ring grows only, keeping what it holds, so a host that raises
+its block size through `setMaxBlockFrames` never loses a queued frame. The NES's per-channel tap rings
+(`NesSoundMixer::_streamBuf`, one a stream) and its blips are sized and made with the same reserve.
+
 ## `ScriptHost` + `ScriptCompiler`: the script-node runtime
 
 Native runs the TypeScript script nodes (the MIDI routing, the controller session and each role's
@@ -966,9 +1003,9 @@ its **runtime** classes:
 
 | Shared class | Host use |
 |---|---|
-| [`SystemBase`](../packages/native/src/system/SystemBase.hpp) | the abstract core; owned by its system node in the running plan (`SystemNode<T>`, [SystemNodes.cpp](../packages/native/src/host/graph/SystemNodes.cpp)); drives `onActivate`/`onProcess`/`framebuffer`/`saveStateBytes`/`readStateSnapshot`/`stateRegions`/`enableStateSnapshot`/`pressButton`/`pushSerialIn` |
+| [`SystemBase`](../packages/native/src/system/SystemBase.hpp) | the abstract core; owned by its system node in the running plan (`SystemNode<T>`, [SystemNodes.cpp](../packages/native/src/host/graph/SystemNodes.cpp)); drives `onActivate`/`onProcess`/`framebuffer`/`saveStateBytes`/`readStateSnapshot`/`stateRegions`/`enableStateSnapshot`/`pressButton`/`pushSerialIn`/`reserveAudio`. Its surface is what the engine and the nodes call: no battery-path accessors, no `fastBoot()` getter, no `clearSram`, no sub-block MIDI out, and no memory-snapshot arming (C4). The capabilities beside `debugTarget()` are `channelTap()`, and the per-block contract is `reserveAudio` plus `kDefaultBlockFrames`; its ingress queues report `inputAccepted()` / `inputDropped()` |
 | [`SameBoySystem`](../packages/native/src/system/sameboy/SameBoySystem.hpp) | the GB core; its node gives it its knobs through value-guarded setters (`setModel`, `setHighpass`, the display group, `setSerialOutCapture`); its `linkPeers_` are the serial cable, set by its node's `rp.link/1` `set_peers` (the other members of its coupled group, in member order) |
-| [`MesenNesSystem` / `MesenGbaSystem` / `MesenSmsSystem`](../packages/native/src/system/mesen) | the NES, GBA and SMS/GG cores; the NES is the `SyncTap` target |
+| [`MesenNesSystem` / `MesenGbaSystem` / `MesenSmsSystem`](../packages/native/src/system/mesen) | the NES, GBA and SMS/GG cores; the NES is the one core with a [`ChannelTap`](../packages/native/src/system/ChannelTap.hpp) today (it implements the interface itself, `channelTap()` returning `this`), so it is the `SyncTap` target. The engine's tap and build check name no console: it forward-declares `ChannelTap` and includes no core header, and its one console check is the N8 cart mirror's (`SystemKind::MesenNes`, NES by construction) |
 | the LSDj kit/sample pipeline (`KitCompiler` / `KitUtil` / `SampleCache` / `Effects`), `util/MinizZip`, `EmbeddedRoms` | backing kit patching, `zip`/`unzip`, and embedded-ROM resolution |
 | `SpscRing` / `FrameBufferTriple` / `MemorySnapshotTriple` | the transport **primitives** the command/release rings and the snapshot registry are built on |
 
@@ -1009,9 +1046,10 @@ is one automatable parameter applied post-render across all 8 channels
 
 ## Not yet built / deferred
 
-- **Live memory-region subscription.** There is no live "watch RAM" streaming path
-  (`enableMemorySnapshot(type)`). Reads are one-shot through the registry read door; if live
-  memory streaming to the UI is ever needed, that arming seam must be built.
+- **Live memory-region subscription.** There is no live "watch RAM" streaming path. Reads are one-shot through the
+  registry read door; if live memory streaming to the UI is ever needed, the arming seam has to be built: the old
+  `enableMemorySnapshot(type)` / `publishMemorySnapshots` mechanism had no caller and published nothing, and C4 deleted
+  it outright, so nothing is left to enable.
 - **Per-block host buttons/keys.** No node has an input for them (the DSP kernel carried empty `buttons`
   and `keys` arrays until B3b) and no host feeds them yet: this blocks the raw LSDj Keyboard mode, which
   needs `keys` on `rp.lsdj.sync` ([07-remaining-work.md](07-remaining-work.md)).
diff --git a/spec/06-build-test.md b/spec/06-build-test.md
index 854a132b..e97b98b6 100644
--- a/spec/06-build-test.md
+++ b/spec/06-build-test.md
@@ -261,7 +261,7 @@ Root [package.json](../package.json). Each builds its CMake target(s) first, the
 | `test` | `tjs-cli` | Mock-backend TS suite (C3's `test/systems/store-resources.test.ts`: each verb's resource and publish order, rollback after a refusal, the construct-time reset, DirectGraph, every put balanced). |
 | `test:native` | `retroplug-host` | Real-host suite (C3's `test-native/app-construct-edit.test.ts`: a construct-time edit reaches the core with no Reset, and a project with one unreadable ROM loads the rest). |
 | `test:ui` | `retroplug-ui-test` | LVGL React UI suite. |
-| `test:plugin` | ten Catch2 binaries (`-plugin-test`, `-dynparams-test`, `-classid-test`, `-audio-test`, `-watcher-test`, `-midi-test`, `-launchpad-test`, `-lottie-test`, `-graph-test`, `-engine-graph-test`) | Pure-C++ unit checks (no TS runner; exit code is pass/fail): the per-context routing behind PluginUI's `__rp_*` window hooks ([ContextTargets.hpp](../packages/native/plugin/ContextTargets.hpp), proven on two live `JSContext`s so concurrent instances never cross-route), DPF's dynamic-parameter diff classifier, the class-id counter sync that keeps the DAW-hosted editor from rendering blank, the per-channel audio split, the efsw file watcher, MIDI in/out, the Launchpad scanner, the Lottie decoder, the node-graph engine's foundations (`retroplug-graph-test`), and the engine on that graph (`retroplug-engine-graph-test`): each system node rendering what its core renders driven directly, bit for bit, its inputs, settings and params included (every setting reaching its core at the next block through its value guard and all of them at activation, a param at the node's first block, `Engine::setParam`, and an LSDj's capture switching in the block its decoder first runs in the new mode: [modular-graph-phase-c2-design.md](../docs/modular-graph-phase-c2-design.md)); each system node builds its core at activation from the resources its settings name (C3, [modular-graph-phase-c3-design.md](../docs/modular-graph-phase-c3-design.md)): its ROM and seed bytes as resources, its construct-time fields, each console's bits held against a directly built core, a Master System's staged stem, and a refused plan unwinding the cores and pins it built (the resource pins themselves in `retroplug-graph-test`); the engine placing, re-arming and freeing what the documents TS builds say; the host I/O nodes and `rp.n8.cart-mirror` moving what the hosts stage and drain (the staging sorted and taken with no allocation); TS nodes behind the C ABI in the script host (marshalling, the JS-owned audio copies, promise jobs, reshape, creation at adoption and release at reclaim, a throw's count (a failed write onto what a node holds among them, while one node's prototype setters charge no other, its prototype methods are adopted by none and it replaces no method of the frozen `Block` prototype), the CPU-time call deadline, an audio thread running them while the control thread replans; a profile build adds eight `[profile]` cases, which `tools/run-profile.sh unit` runs); script node packs beside the built-in one ([ScriptPacks.test.cpp](../packages/native/test/engine-graph/ScriptPacks.test.cpp), [modular-graph-phase-c1-design.md](../docs/modular-graph-phase-c1-design.md)), each in runtimes of its own (a getter on `Object.prototype`, a replaced `Array.prototype.push` and a global reaching no other pack's node), its promise jobs and memory limit faulting only its own nodes, loaded, reloaded and freed while an audio thread runs blocks, each refusal changing neither the types a document can name nor the running plan, a reload's replan carrying the running plan's tap as it is, and the control thread's calls into a pack held to the call deadline; the protocol script nodes, in the documents `buildGraph` builds (read from its role goldens, the systems stood in for by probes, the script nodes run in `buildGraph`'s plan order), held to the digests of what the DSP kernel they replaced was recorded sending, block by block, but for the host MIDI out of MI.OUT and master sync, which `rp.lsdj.serial-out` sends at each byte's frame by design, three cases holding that decoder's recorded digests, and every case that records digests a second one over its system inputs alone, equal to what the rig gave at the commit before the decoder moved, so the move changed no system input ([ProtocolNodes.test.cpp](../packages/native/test/engine-graph/ProtocolNodes.test.cpp): every routing, every LSDj sync mode, risa and smsggdj sync, a controller session, its launches behind routed MIDI and its host MIDI out merged by frame with two LSDjs' in an exact interleave, state across structure changes, a throw stopping only its own node, and a block that fills `rp.host.midi-in` reaching an mGB and a NES whole; and, checked directly, a controller's session kept through a system added behind it, and every structure's script nodes running in `buildGraph`'s order), a Game Boy's serial input hashed byte by byte so how a node groups same-frame bytes moves no recorded value; `rp.host.midi-out`'s systems form, each message under its input's system, its inputs merged by frame with ties in input order, and nothing handed out earlier than a frame already handed out or past the block's last frame whatever an input's queue holds (`HostIo.test.cpp`, the guard through a test node that writes its own queue out of order and past the block), and `Engine::setGraph` refusing a second `rp.host.midi-out` as it refuses a second host input or cart mirror; `rp.lsdj.serial-out` on an engine, decoding what an mGB core captures on its link port into host MIDI inside each block and at each byte's frame, the one fixture-free run of a real capture through the real decoder, and sending master sync's stop at the first frame past 200 ms after `Engine::setSampleRate` drops the rate during a run; and every document in `buildGraph`'s goldens planned on the engine with real cores and run for a block with no node faulting (`Engine::faultCount`), no script node throwing, no message dropped and no system node's settings naming a field its type does not read (a C++ node ignores one, so a misspelt knob would plan and do nothing), the 33-system `perSystem` ones refused for having more buses than `rp.host.audio-out` takes; the role goldens (`golden/build-graph-roles.json`, `buildGraph`'s own record of documents with every protocol node type, a controller each way and every MIDI routing) run twenty blocks each, the transport playing, host MIDI and a control surface's press staged, and a controller must send its surface something and, driving a real Game Boy, the host its launch. A hidden bench in it, `ScriptBench`, times a script node per block, and the protocol nodes in their plan (an LSDj in MIDI sync, and one decoding MI.OUT, its decoder after the systems), at 1, 64 and 512 frames (`build/bin/retroplug-engine-graph-test "[bench]"`). |
+| `test:plugin` | ten Catch2 binaries (`-plugin-test`, `-dynparams-test`, `-classid-test`, `-audio-test`, `-watcher-test`, `-midi-test`, `-launchpad-test`, `-lottie-test`, `-graph-test`, `-engine-graph-test`) | Pure-C++ unit checks (no TS runner; exit code is pass/fail): the per-context routing behind PluginUI's `__rp_*` window hooks ([ContextTargets.hpp](../packages/native/plugin/ContextTargets.hpp), proven on two live `JSContext`s so concurrent instances never cross-route), DPF's dynamic-parameter diff classifier, the class-id counter sync that keeps the DAW-hosted editor from rendering blank, the per-channel audio split, the efsw file watcher, MIDI in/out, the Launchpad scanner, the Lottie decoder, the node-graph engine's foundations (`retroplug-graph-test`), and the engine on that graph (`retroplug-engine-graph-test`): each system node rendering what its core renders driven directly, bit for bit, its inputs, settings and params included (every setting reaching its core at the next block through its value guard and all of them at activation, a param at the node's first block, `Engine::setParam`, and an LSDj's capture switching in the block its decoder first runs in the new mode: [modular-graph-phase-c2-design.md](../docs/modular-graph-phase-c2-design.md)); each system node builds its core at activation from the resources its settings name (C3, [modular-graph-phase-c3-design.md](../docs/modular-graph-phase-c3-design.md)): its ROM and seed bytes as resources, its construct-time fields, each console's bits held against a directly built core, a Master System's staged stem, and a refused plan unwinding the cores and pins it built (the resource pins themselves in `retroplug-graph-test`); a warmed core's whole block allocates and frees nothing, emulation included, and does so with a block's host input staged into it too (C4, [modular-graph-phase-c4-design.md](../docs/modular-graph-phase-c4-design.md)): a Mesen audio ring's fill, drain, wrap, growth and overflow counter, the audio path draining without allocating at every ragged block size, the whole block for a NES (mixed, tapped, and through the Engine), an SMS, a Game Boy and a GBA with every counted window staging a block's host input, the node-level ones proved to have emulated by the frames they captured and the Engine one by its work RAM, a live arm sizing nothing, a re-activation growing the ring, and a node sizing its core to its plan rather than the engine's default (the suite's `CountingAllocator` counts the C library's blocks on request, glibc only and never under a sanitizer, so the arm's case skips where `countsCAllocations()` is false, and the whole-block cases prove the operator-new half where it cannot); the ingress queues a block's input lands in bounded and sized when the core is built (a `[input-drops]` case per queue kind, the bounds held to `ports.ts`'s declarations, and `FixedQueue` / `FixedByteRing` / `stableSortEntries` unit cases in `retroplug-graph-test`); each console's published frames held to recorded hashes (`[video]`, a frame's buffers being reused) and Mesen's cleared controller state and a notification's listener copy held directly; the tap a capability only a NES offers (the engine's tap and build check name no console: it arms through `SystemBase::channelTap()`, and its one console check is the N8 cart mirror's), and a node refusing a core built with another console's kind (a 12-way matrix over the four consoles, through a rig seam that reports another kind, the refusal giving the core back); the engine placing, re-arming and freeing what the documents TS builds say; the host I/O nodes and `rp.n8.cart-mirror` moving what the hosts stage and drain (the staging sorted and taken with no allocation); TS nodes behind the C ABI in the script host (marshalling, the JS-owned audio copies, promise jobs, reshape, creation at adoption and release at reclaim, a throw's count (a failed write onto what a node holds among them, while one node's prototype setters charge no other, its prototype methods are adopted by none and it replaces no method of the frozen `Block` prototype), the CPU-time call deadline, an audio thread running them while the control thread replans; a profile build adds eight `[profile]` cases, which `tools/run-profile.sh unit` runs); script node packs beside the built-in one ([ScriptPacks.test.cpp](../packages/native/test/engine-graph/ScriptPacks.test.cpp), [modular-graph-phase-c1-design.md](../docs/modular-graph-phase-c1-design.md)), each in runtimes of its own (a getter on `Object.prototype`, a replaced `Array.prototype.push` and a global reaching no other pack's node), its promise jobs and memory limit faulting only its own nodes, loaded, reloaded and freed while an audio thread runs blocks, each refusal changing neither the types a document can name nor the running plan, a reload's replan carrying the running plan's tap as it is, and the control thread's calls into a pack held to the call deadline; the protocol script nodes, in the documents `buildGraph` builds (read from its role goldens, the systems stood in for by probes, the script nodes run in `buildGraph`'s plan order), held to the digests of what the DSP kernel they replaced was recorded sending, block by block, but for the host MIDI out of MI.OUT and master sync, which `rp.lsdj.serial-out` sends at each byte's frame by design, three cases holding that decoder's recorded digests, and every case that records digests a second one over its system inputs alone, equal to what the rig gave at the commit before the decoder moved, so the move changed no system input ([ProtocolNodes.test.cpp](../packages/native/test/engine-graph/ProtocolNodes.test.cpp): every routing, every LSDj sync mode, risa and smsggdj sync, a controller session, its launches behind routed MIDI and its host MIDI out merged by frame with two LSDjs' in an exact interleave, state across structure changes, a throw stopping only its own node, and a block that fills `rp.host.midi-in` reaching an mGB and a NES whole; and, checked directly, a controller's session kept through a system added behind it, and every structure's script nodes running in `buildGraph`'s order), a Game Boy's serial input hashed byte by byte so how a node groups same-frame bytes moves no recorded value; `rp.host.midi-out`'s systems form, each message under its input's system, its inputs merged by frame with ties in input order, and nothing handed out earlier than a frame already handed out or past the block's last frame whatever an input's queue holds (`HostIo.test.cpp`, the guard through a test node that writes its own queue out of order and past the block), and `Engine::setGraph` refusing a second `rp.host.midi-out` as it refuses a second host input or cart mirror; `rp.lsdj.serial-out` on an engine, decoding what an mGB core captures on its link port into host MIDI inside each block and at each byte's frame, the one fixture-free run of a real capture through the real decoder, and sending master sync's stop at the first frame past 200 ms after `Engine::setSampleRate` drops the rate during a run; and every document in `buildGraph`'s goldens planned on the engine with real cores and run for a block with no node faulting (`Engine::faultCount`), no script node throwing, no message dropped and no system node's settings naming a field its type does not read (a C++ node ignores one, so a misspelt knob would plan and do nothing), the 33-system `perSystem` ones refused for having more buses than `rp.host.audio-out` takes; the role goldens (`golden/build-graph-roles.json`, `buildGraph`'s own record of documents with every protocol node type, a controller each way and every MIDI routing) run twenty blocks each, the transport playing, host MIDI and a control surface's press staged, and a controller must send its surface something and, driving a real Game Boy, the host its launch. A hidden bench in it, `ScriptBench`, times a script node per block, and the protocol nodes in their plan (an LSDj in MIDI sync, and one decoding MI.OUT, its decoder after the systems), at 1, 64 and 512 frames (`build/bin/retroplug-engine-graph-test "[bench]"`). |
 | `screenshot` | `retroplug-jack` | Boots the standalone headlessly → `/tmp/retroplug.png`. |
 | `sdl:smoke` | `retroplug-sdl` | Headless SDL-standalone boot + shutdown; needs no display server or audio server. In CI. |
 | `sdl:pipewire` | `retroplug-sdl` | Stands up a private PipeWire server and asserts WHICH output device the PortAudio backend opens. Not in CI (the runners have no PipeWire). |
diff --git a/spec/07-remaining-work.md b/spec/07-remaining-work.md
index 8134ad26..29034169 100644
--- a/spec/07-remaining-work.md
+++ b/spec/07-remaining-work.md
@@ -46,13 +46,15 @@ built ([05-data-persistence.md](05-data-persistence.md)).
 
 ### Live memory-region subscription
 
-There is no live "watch RAM" streaming path (`enableMemorySnapshot(type)`). Reads are one-shot through
+There is no live "watch RAM" streaming path. Reads are one-shot through
 the [`SnapshotRegistry`](../packages/native/src/host/engine/SnapshotRegistry.hpp) read door, and the
 consumers that want live memory poll it: the LSDj overlay per frame, and the HD player
 ([useLsdjHdSession.ts](../packages/retroplug/ui/screens/hd/useLsdjHdSession.ts)) on a frame divider,
 because a full 128 KiB WRAM copy plus a song decode is too expensive to do every frame. So this is a
 cost question, not a capability one - an arming seam would let those two stop copying the whole image
-to read a few hundred bytes of it.
+to read a few hundred bytes of it. Nothing of the old mechanism is left to switch on: `enableMemorySnapshot`,
+`disableMemorySnapshot`, `memorySnapshot`, `publishMemorySnapshots` and the nine-slot array behind them had no caller and
+published nothing, and C4 deleted them.
 
 ### MIDI **out** is capped at 4 bytes
 
@@ -192,9 +194,12 @@ frame, `rp.host.midi-out` merges its inputs by frame, the decoders allocate noth
     settings now, and the node builds the core from them; the export mode and a Game Boy's link group come from the
     system's core config in TS, where `buildGraph` reads them for its wiring.
   - **The knobs that allocate on the block thread.** A Game Boy's model change restarts the core, which allocates, as
-    it did on the command ring, and a NES's region change resets its core, which allocates too; the NES core also
-    allocates inside Mesen in its own blocks. So "no allocation" holds for a Game Boy's knobs, the ones it applies in
-    place, a NES's setters but its region, and every node's params ([02-native-host.md](02-native-host.md)).
+    it did on the command ring, and a NES's region change resets its core, which allocates too. So "no allocation"
+    holds for a Game Boy's knobs, the ones it applies in place, a NES's setters but its region, and every node's params
+    ([02-native-host.md](02-native-host.md)). A warmed block itself allocates nothing since C4, Mesen's emulation
+    included, and since the input-queue stage a block carrying host input does too: the ingress queues are bounded and
+    sized when the core is built. The exceptions are the 0.5 s state publish and the N8 firmware's SD path, listed
+    under C4 below.
   - **A system node ignores a settings field it does not know**, as every C++ node does: a misspelt knob plans and
     does nothing. `BuildGraphGoldens.test.cpp` holds the goldens' system nodes to their schemas' fields; a strict
     reader would refuse it at plan time instead.
@@ -209,9 +214,11 @@ frame, `rp.host.midi-out` merges its inputs by frame, the decoders allocate noth
   `rp.host.systems/1`; the old build call, the parked cores, the construct blob and its decoders are gone, and a
   construct-time edit rebuilds the system at once, carrying its battery. Strict parity held bit for bit at its five
   commits, nothing re-recorded. What it leaves (its section 10):
-  - **A stale NES-tap record can still match a fresh core** allocated where the armed one was freed: `armedTap_`
-    compares the core's address, and the record survives a drop and reclaim. TS never gets there (its ids only go up,
-    and every edit that drops a core re-arms), and the suite pins only the new-id replacement.
+  - **A stale tap record can still match a fresh core** allocated where the armed tap was freed: `armedTap_` compares
+    the tap's address (a NES's tap is the core object, a per-class constant offset from the core pointer, so tap
+    equality is core equality within one class), and the record survives a drop and reclaim. TS never gets there (its
+    ids only go up, and every edit that drops a core re-arms), and the suite pins only the new-id replacement. C4
+    restated this at the tap's address and left it as it is.
   - **A node's first block is silent**, so the gain's first-value snap is observable only through a savestate seed
     taken mid-note, which is the case that pins it.
   - **The unnamed Master System stem is `system-<id>`**: unique within a Project, not across the several Projects
@@ -221,8 +228,40 @@ frame, `rp.host.midi-out` merges its inputs by frame, the decoders allocate noth
   - **Each system holds one more full copy of its ROM.** The `rom` resource a node pins for the system's life is on top
     of the vector the build hands the core and Mesen's own copies: a 32 MB GBA ROM is +32 MB per system, per plugin
     instance. And the plugin and SDL reclaim only inside `setGraph` and `loadScriptPack`, so a removed system's core,
-    ROM and seeds stay until the next one of those (the core always lived that long; the bytes are new). C4, which
-    moves the console code into the nodes, could let a core borrow the pinned bytes.
+    ROM and seeds stay until the next one of those (the core always lived that long; the bytes are new). Phase C's
+    P3 (below) is the piece that would let a core borrow the pinned bytes.
+- **C4 built: the tap and the build check name no console, and no allocation in a warmed block**
+  ([modular-graph-phase-c4-design.md](../docs/modular-graph-phase-c4-design.md), six commits): `SystemBase`'s dead
+  surface is gone, a Mesen core collects its audio in a fixed ring sized from the build request's `maxBlockFrames`, the
+  tap is a `ChannelTap` capability any core may offer, and the node's build compares `kind()` with its core's `kKind`
+  instead of casting. A warmed core's whole block allocates and frees nothing, Mesen's per-frame video and input
+  plumbing included, and a block carrying host input does too: every ingress queue is a bounded ring sized when the
+  core is built, and a push past its bound is dropped and counted. Strict parity held bit for bit at every commit,
+  nothing re-recorded. The one console name `Engine` still makes is the N8 cart mirror's (`SystemKind::MesenNes`, NES
+  by construction), plus the tap's 1-to-3 mode bound. What it leaves (its section 12):
+  - **P3, cores borrowing their node's pinned ROM bytes**, and **P4, the snapshot registry's double copy** (section 3
+    above), and **P7, the full capability split of `SystemBase`** and one node file per console, best done when C5's
+    hardware nodes need the interfaces.
+  - **The N8 firmware's SD path still allocates**: `std::string` and `std::filesystem` in the command handlers, a
+    bigger directory listing, and `_txQueue`'s unbounded growth when a ROM writes CMD_USB_WR and no host drains it.
+    Bounding the queue would change the "never drop a byte the device would deliver" contract. Nothing in the block
+    tests drives an SD command, so it is ROM-dependent.
+  - **The 0.5 s state publish allocates** inside Mesen's streaming serializer on the block thread (every 24 k frames at
+    48 kHz). It is the one allocation a long run makes that neither a ROM command nor an armed serial-out capture
+    triggers, so every `[alloc]` case on an Engine-built core charges a whole interval uncounted first.
+  - **The input-drop counters are not surfaced beside the graph's.** Each core reports `inputAccepted()` /
+    `inputDropped()` and the N8 FIFO's `coreTransportStats()` adds `droppedBytes`, but `rp_process` carries no
+    instance-drop pointer, so nothing reports them where `GraphHost` counts its per-instance output drops. An
+    input-drop service on the node seam is its own change.
+  - **Two output-side allocations remain**: a NES's `_txQueue`, a `std::vector` a debug `drainCoreBytes` RPC swaps
+    empty, so a non-link ROM's next `CMD_USB_WR` after a drain grows it; and a Game Boy's `serialOutLog_`, cleared
+    each block but grown to the largest block's capture while MI.OUT capture is armed. Neither is on the steady-state
+    path the `[alloc]` cases hold.
+  - **The tap's mode bound is the engine's, not the capability's**: `Engine::setGraph` still validates 1 to 3, the NES's
+    numbering, because TS owns it and the arm travels as a byte. `maxMode()` on `ChannelTap` is the later move.
+  - **The cores this fork does not run** (PCE, SNES, WS) still copy a frame's snapshot once more per emulated frame.
+  - **The suite keeps its console-only casts** (`EngineGraph`, `SystemNode`, `HostIo`, `EngineSampleRate` tests read a
+    config field, `linkPeers_` or a ring counter); no tap read casts any more.
 - **Left by LSDj's serial-out node, as they are** (its design's sections 11 and 12):
   - **The core's serial byte timing moves with the block split.** The frame SameBoy completes a captured byte at
     moves by tens of frames from one split to another (up to 32 in `lsdj-mastersync` as the decoder's prototype
diff --git a/spec/08-profiling.md b/spec/08-profiling.md
index c863eedf..88f1d8c2 100644
--- a/spec/08-profiling.md
+++ b/spec/08-profiling.md
@@ -57,6 +57,17 @@ length. What still allocates is work that makes something:
 | `rp.risa.sync`'s arm ([risaSync.ts](../packages/retroplug/src/risaSync.ts)) | two objects on a start or a seek: `risaLocate`'s object and `risaArmPacket`'s array |
 | `rp.controller.session`'s surface diff and launches ([controller/session.ts](../packages/retroplug/src/controller/session.ts)) | per LED change and per launch, while a surface is in use |
 
+**The cores.** Since C4 a system core allocates nothing in a warmed block either, Mesen's per-frame video and input
+plumbing included: a Mesen core's audio ring, its per-block scratches and its tap rings are sized at activation from
+the build request's `maxBlockFrames`, its frame and controller-state buffers are reused, and SameBoy's two scratch sets
+are sized the same way. That holds with a block's host input staged into it too, since the input-queue stage: every
+queue a press, a MIDI run, a serial byte or a raw message lands in is bounded and sized when the core is built. The C++
+suite holds that promise directly (`retroplug-engine-graph-test`'s `[alloc]` cases, [06-build-test.md](06-build-test.md));
+the profile benches measure the JS side, so their 0 is the script host's, not the core's. What still allocates inside a
+long run is Mesen's 0.5 s savestate capture (its streaming serializer), a NES's region reset and a Game Boy's model
+restart, the N8 firmware's SD-command path (`std::string` / `std::filesystem`, a bigger directory listing, an unbounded
+TX queue) and a Game Boy's serial-out capture log; [07-remaining-work.md](07-remaining-work.md) lists them.
+
 **How it got here.** Under the DSP kernel (one TS kernel running every role, until B3b) the first
 measure was **~171 allocs/block** on Profile A: the kernel built a `Map` of routed inboxes, per-system
 `keys` / `buttons` filters, and a `SystemCtx` with four sink closures per system and stage, every block.
@@ -228,6 +239,18 @@ past `renderAudio`'s 1024-frame block).
   RPC), and `allocStats()` / `resetAllocStats(disableAutoGc)` (pins `JS_SetGCThreshold(SIZE_MAX)`) / `runGc()`
   (timed `JS_RunGC`). The window, the live and peak heap and the block bracket are behind
   `#ifdef RETROPLUG_PROFILE`; the `#else` path returns `enabled:false`.
+- **Native allocations counted in the C++ suite too** (C4): `retroplug-engine-graph-test` links
+  [test/graph/CountingAllocator](../packages/native/test/graph/CountingAllocator.cpp), which replaces the global
+  `operator new`/`delete` (every form) and counts what a stretch of code allocates and frees on the calling thread
+  (`beginCountingAllocations` / `endCountingAllocations`), and can refuse every allocation from a size up. Since C4 it
+  also interposes the C library's `malloc` / `calloc` / `realloc` / `free`, forwarding to glibc's own, so a core's C
+  blocks are seen; that half is **opt-in per window** (`beginCountingAllocations(true)`) and platform-gated, because it
+  is possible only on glibc and not in a sanitizer build (ASan and TSan own the allocator) or on Windows (the static CRT
+  cannot define `malloc`), and it would otherwise count a subsystem whose own allocations *are* malloc's (QuickJS).
+  `countsCAllocations()` reports which binary sees them, so a case whose promise includes a C allocation SKIPS where it
+  is false. The `[alloc]` cases are the users (section 3's core promise): the whole-block cases count the C library's
+  blocks where the binary can, and prove the operator-new half of the promise where it cannot. The profile benches keep
+  counting QuickJS alone.
 - **The trace window** (Tier B), on the same runtime: `traceReset(arm)` opens a window of nested wall-time
   spans, recorded only while armed. Every script node's process is a span **named by its type**
   (`rp.midi.route`, `rp.mgb.midi`, ...), holding `marshal` (its audio copies in and the transport) and
````

- [ ] **Step 3: The main design, the B1 design, and AGENTS.md**

The main design's section 6.2 (SameBoy's dead `onMidi` is gone), 6.4 (the capability table gains the tap, built as
`ChannelTap` rather than an `rp.system.*` interface), 6.5 (the two casts C4 closed, and the suite's console-only casts
that remain) and section 8's Phase C record; the B1 design's two core-pointer paragraphs; AGENTS.md's engine-graph
paragraph:

````diff
diff --git a/AGENTS.md b/AGENTS.md
index e3e06877..ea63d00a 100644
--- a/AGENTS.md
+++ b/AGENTS.md
@@ -43,8 +43,15 @@ The rules below are the parts that don't fit those.
   block its decoder first runs in the new mode); a system node builds its core at activation from the resources its
   settings name (C3, [docs/modular-graph-phase-c3-design.md](docs/modular-graph-phase-c3-design.md)): the ROM and the
   seeds as resources, its construct-time fields, each console's twin against a directly built core, and a refused plan
-  unwinding the cores and pins it built; the engine places, re-arms and frees what the graph documents TS builds
-  say; the host I/O nodes and the N8 cart mirror move what the hosts stage and drain;
+  unwinding the cores and pins it built; a warmed core's whole block allocates and frees nothing, emulation included,
+  and does so with a block's host input staged into it (C4,
+  [docs/modular-graph-phase-c4-design.md](docs/modular-graph-phase-c4-design.md)): the ring, the scratches, the tap
+  rings and the ingress queues sized from the build request's block and the capacities the inputs' sources declare,
+  the audio path and then the whole block counted at ragged sizes with input staged, the `[video]` hashes, a live arm
+  sizing nothing, a `[input-drops]` case per queue kind, the tap a capability only a NES offers (so the tap and the
+  node's kind check name no console; the one console name left in the host is the N8 cart mirror's), and a node
+  refusing a core of another console's kind; the engine places, re-arms and frees what the graph documents
+  TS builds say; the host I/O nodes and the N8 cart mirror move what the hosts stage and drain;
   TS nodes run behind the C ABI in the script host (B3a,
   [docs/modular-graph-phase-b3a-design.md](docs/modular-graph-phase-b3a-design.md)): marshalling, reshape,
   creation and release on the block thread, throws, a node's promise jobs charged to it; script node packs beside the
diff --git a/docs/modular-graph-design.md b/docs/modular-graph-design.md
index e179035e..afca41b1 100644
--- a/docs/modular-graph-design.md
+++ b/docs/modular-graph-design.md
@@ -879,8 +879,8 @@ product, so `mesen` can be split per console without renaming anything.
 | GBA | `buttons` | `mix` (stereo) |
 
 MIDI never reaches a core directly; it goes through a node that knows a protocol: `mgb.midi` (MIDI ->
-GB serial bytes), `n8.midi-to-fifo` (MIDI -> FIFO writes, replacing native `NesN8FifoRole`). That also
-retires a current oddity: SameBoy's `onMidi` stores MIDI that nothing reads.
+GB serial bytes), `n8.midi-to-fifo` (MIDI -> FIFO writes, replacing native `NesN8FifoRole`). The oddity this
+retires is gone: SameBoy's `onMidi` stored MIDI nothing read, and C4 deleted the buffer and the override.
 
 ### 6.3 Settings and params
 
@@ -913,6 +913,14 @@ Today's fat `SystemBase` splits into optional interfaces:
 | `rp.system.video/1` | framebuffer triple, size, format |
 | `rp.system.buttons/1` | button names to ids, so bindings and extensions say `"start"`, not per-console enums |
 | `rp.system.debug/1` | today's `IDebugTarget` |
+| `rp.system.tap/1` | the per-channel audio tap a split routing arms |
+
+C4 built the first piece of this list that the engine itself needed, and not as an `rp.system.*` interface: the tap is
+`ChannelTap` ([ChannelTap.hpp](../packages/native/src/system/ChannelTap.hpp), `exportMode` / `setExportMode` and nothing
+else), which `SystemBase::channelTap()` returns or answers null for, and `SystemTable`'s slot carries that pointer, so
+`Engine`'s tap arming and build check name no console
+([modular-graph-phase-c4-design.md](modular-graph-phase-c4-design.md); the one console name left in `src/host/` is the
+N8 cart mirror's, NES by construction). The rest of the split is P7's.
 
 **The threading rule is unchanged: the control plane never touches a live core.** The executor calls
 these interfaces at safe points on the audio thread. Reads come from published snapshots; writes and
@@ -928,7 +936,12 @@ Extensions only ever see host RPC that routes through those two doors.
 | `Engine`'s gather of SameBoy `serialOutLog_` | the GB `serialOut` port |
 | `LinkGroup`, `Project::rebuildLinkGroups` | `link` wires, the planner's coupled groups |
 | `BlockRunner` / `OfflineRender` including SameBoy headers | the console-neutral executor |
-| `SystemKind`, `ConfigField`, native `AudioRouting` | deleted (`ConfigField` in C2) |
+| `SystemKind`, `ConfigField`, native `AudioRouting` | deleted (`ConfigField` in C2; `SystemKind` survives as the node's build check) |
+
+Two more went in C4, and they were the last outside the cores: `Engine::syncTap`'s two casts (the plan's build and
+`publishGraph`'s pre-fill) are `SystemBase::channelTap()`, and `SystemNodes.cpp`'s build compares `kind()` with its
+core's `kKind` and then `static_cast`s. So `packages/native/src` has no `dynamic_cast` outside `src/system/`. The
+suites keep their console-only casts (a config field, `linkPeers_`, a ring counter), which is the core's own business.
 
 What remains of `Engine` is the executor, the rings and the registry.
 
@@ -1180,9 +1193,17 @@ engine's `ResourceTable`, pinned through `rp.host.resources/1`), and the node bu
 and a construct-time edit rebuilds the system at once, carrying its battery. Strict parity held bit for bit, nothing
 re-recorded.
 
-- Console-specific code moves into each node; every `dynamic_cast` in §6.5 goes.
-- `SystemBase` shrinks to the capability interfaces; `MesenAudioDevice` gets a preallocated ring (it
-  currently allocates on the audio thread).
+C4 is built ([modular-graph-phase-c4-design.md](modular-graph-phase-c4-design.md)): `SystemBase`'s dead surface is
+gone, a Mesen core collects its audio in a fixed ring sized from the build request's block, its per-frame video and
+input plumbing reuses its buffers, the tap a split routing arms is a `ChannelTap` capability, and a system node's build
+compares its core's `kind()` rather than casting. A warmed core's whole block allocates and frees nothing, and so does
+one carrying host input in it: every ingress queue a press, a MIDI run, a serial byte or a raw message lands in is a
+bounded ring sized when the core is built. Strict parity held bit for bit at every commit, nothing re-recorded.
+
+- Console-specific code moves into each node; every `dynamic_cast` in §6.5 goes: done in C4 for `src/`, the suites'
+  console-only casts aside.
+- `SystemBase` shrinks to the capability interfaces (§6.4): its dead surface went in C4, the split itself is P7's.
+  `MesenAudioDevice`'s preallocated ring is built.
 - `rp.n8.nes` and `rp.launchpad.surface` replace the N8 mirror sink and the controller special cases.
 
 ### Phase D: the DSP library and offline graphs
diff --git a/docs/modular-graph-phase-b1-design.md b/docs/modular-graph-phase-b1-design.md
index e947eb8d..bf7dbcd3 100644
--- a/docs/modular-graph-phase-b1-design.md
+++ b/docs/modular-graph-phase-b1-design.md
@@ -158,7 +158,8 @@ that has gone is not found and its record is dropped, as today.
 (set-routing to the same split, PinSplit to StereoPinSplit), and each call resets the capture streams and
 their resamplers (`NesSoundMixer::SetChannelCapture`), while `ports_changed` fires only when the wiring
 changes. The record (`armedTap_`) therefore stays on the Engine, and the table carries each NES's
-`MesenNesSystem*` so the command needs no cast.
+`MesenNesSystem*` so the command needs no cast (C4 later replaced that pointer with a `ChannelTap*`, so the record
+holds the tap's address; [modular-graph-phase-c4-design.md](modular-graph-phase-c4-design.md) section 5).
 
 The wiring only sets the lane count (§3.1). Which mode a connected tap port means:
 
@@ -288,7 +289,9 @@ holds, B0 §4) and the host's own maximum, re-activated quiescently if a host re
 ### 4.5 The system table
 
 A slot-ordered list of `{ id, SystemBase*, SameBoySystem*, MesenNesSystem* }` (each of the last two null
-for other kinds), built with each document and handed to `setGraph` with it. `GraphHost` stores it with
+for other kinds), built with each document and handed to `setGraph` with it. (C4 replaced the two core pointers
+with a `ChannelTap*`; see [modular-graph-phase-c4-design.md](modular-graph-phase-c4-design.md) section 5.)
+`GraphHost` stores it with
 the plan (opaque host data, freed with the plan on reclaim) and exposes the current plan's table on the
 audio thread. The kernel's fan-out, the command handler (§5.1, `SyncTap` included), the MI.OUT collection
 and the registry read it, so a lookup always matches the plan that is running, including between two swaps
````

- [ ] **Step 4: Verify**

Run: `pnpm check:docs`
Expected: `check-doc-links: 69 file(s) clean` (the prototype's 68 plus this plan).

Run: `pnpm typecheck`
Expected: `packages/retroplug typecheck: Done` (both tsconfigs).

Run: `git diff HEAD | python3 -c "import sys; print(sum('\u2014' in l for l in sys.stdin if l.startswith('+')))"`
Expected: `0`: no emdash on any added line, as the report's own check found (its check also covered the files' extensions and the commit message). No C++ build is needed: the only code change is a comment in a header, and it changes no translation unit's tokens.

- [ ] **Step 5: Commit**

````bash
git add -- AGENTS.md docs/modular-graph-design.md docs/modular-graph-phase-b1-design.md \
  docs/modular-graph-phase-c4-design.md packages/native/src/system/MemoryType.hpp spec/02-native-host.md \
  spec/06-build-test.md spec/07-remaining-work.md spec/08-profiling.md
git commit -F - <<'EOF'
docs: the specs say what C4 built, the input-queue stage included

The C4 design is marked built and says what was built in each section it changed: the ring's capacity and overflow
policy, the block size in the build request, the tap rings and scratches sized at activation, an arm sizing nothing (and
the SetCaptureCapacity call it still makes), the blips re-initialised by blip_reset; the tap a ChannelTap capability
with the per-class address reasoning, and the node's kind check with the NES's 1-to-3 mode bound left as a residual; the
tests as built, the six commits, the prototype's four questions answered, and the findings.

A section of its own records the stage the user added while the ring was built: Mesen's per-frame video and input
plumbing, each site and what fixed it, the ClearState contract kept, our own N8 parser buffers, SameBoy's scratches and
its deleted pendingMidi_, and the SD-path allocations that remain with the reason for each. A second new section records
the stage added after this design's first draft: a core's ingress queues are bounded rings sized when the core is built,
with each queue's bound, the refusal counters, the in-place sort and the N8 role's rebase order, and what is left of the
output side. Section 2 carries both added decisions, section 7 is six commits, and section 10 tags the findings with the
section each changed, the promise's empty counted windows and the eager deque node among them.

The phase's headline is stated exactly: the tap and the build check name no console, and the one console name left in
src/host/ is the N8 cart mirror's, NES by construction, plus the tap's mode bound. The allocation promise now holds
with a block's host input staged into it too, and the 32768-frame cap is the mixer's whole output buffer rather than
half of it; the cast claim says unchecked, and the 0.5 s publish is the one allocation a long run makes that neither a
ROM command nor an armed serial-out capture triggers.

spec/02 gains SystemBase's surface after P1, the ring and the promise, the tap and SystemTable's slot, the build
request's block size (both default constants), the ingress queues' bounds and their refusal counters, the FIFO's
permissive-profile storage bound, and the corrected tap, mode-range, cast and memory-subscription lines; spec/06 the
cases C4 added (the input staged in every counted window, the input-drop cases, the ports.ts tie, the FixedQueue unit
cases); spec/07 C4's leftovers (P3, P4, P7, the SD path, the 0.5 s publish, the input-drop counters, the output-side
allocations, the TX queue); spec/08 the C++ suite's counting allocator with its C half opt-in, platform-gated and the
operator-new half elsewhere. The main design's capability table, its cast table and its Phase C record say what C4
closed; the B1 design notes where C4 replaced its two core pointers. MemoryType's comment no longer names a subscribe
RPC native does not have.
EOF
````

### Task 7: the acceptance run

The acceptance run, on the final tree. Nothing to commit unless a step fails and is fixed; a fix is its own commit,
named for what it fixes. C4's counts are the tip verification at `c1fc0d68` (`/tmp/c4pro/tv-*.log`, its summary lines
in `tv-summary.txt`): every leg exit 0, including the two consumers. A line no log records says so and asks for the
count instead. The prototype's acceptance was headless: no C4 log records a Reaper run or an SDL device run, so those
legs report what they print.

- [ ] **Step 1: The tree and the build**

Run: `git status --short && cmake --build build -j$(nproc)`
Expected: only ` m deps/sameboy`, then a clean build of every target. At most the pre-existing `-Wstringop-overflow` in `pressButton` (Task 5 gives the press queue `FixedQueue::push`, so a fully rebuilt tree may not raise it) and the pre-existing `-Wformat-truncation` at `PluginDSP.cpp:165`; no new warnings. A full build ends at `[100%] Built target <target>`, and which target is last depends on the build order (the prototype's log ends at `[100%] Built target retroplug-cli`).

- [ ] **Step 2: The suites**

Run: `pnpm typecheck`
Expected: `packages/retroplug typecheck: Done`.

Run: `pnpm test`
Expected: `# 168 test file(s) passed (jobs=16)`.

Run: `pnpm test:plugin`
Expected: `All tests passed (27160 assertions in 211 test cases)` for the engine-graph binary, `All tests passed (1233762 assertions in 105 test cases)` for the audio binary, `All tests passed (2221 assertions in 202 test cases)` for the graph binary, and the other seven binaries at their 33d39f39 counts (`All tests passed (35 assertions in 19 test cases)`, `All tests passed (24 assertions in 7 test cases)`, `All tests passed (21 assertions in 6 test cases)`, `All tests passed (10 assertions in 3 test cases)`, `All tests passed (98 assertions in 22 test cases)`, `All tests passed (11 assertions in 3 test cases)`, `All tests passed (7 assertions in 1 test case)`).

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources build/bin/retroplug-engine-graph-test`
Expected: `All tests passed (27224 assertions in 211 test cases)`: the same 211 cases, with the fixture-gated sections running instead of returning early.

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=16)`, none skipped, nothing re-recorded, and `git status` shows no baseline changed.

Run: `pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=16)`, with `# 46 case(s) SKIPPED in 10 file(s)`, as CI runs it.

Run: `RETROPLUG_RESOURCES_DIR=/nonexistent-rp-resources RISA_SRC=/nonexistent-rp-risa pnpm test:native`
Expected: `# 158 native test file(s) passed (jobs=16)`, with `# 136 case(s) SKIPPED in 61 file(s)` and the skip check clean.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native`
Expected: `# 158 native test file(s) passed (jobs=16)`, with `# 2 case(s) SKIPPED in 2 file(s): dsp-bench(1), nes-bench(1)`.

Run: `pnpm test:ui`
Expected: `# 39 UI test file(s) passed (jobs=16)`, with the two fixture cases skipped as the plumbing stage records (`2 skipped (lsdj-hd, lsdj-overlay)`); report this run's line.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:ui`
Expected: `# 39 UI test file(s) passed (jobs=16)` with the fixtures present; report whether any case skipped.

Run: `pnpm test:node && pnpm test:cli-ts && pnpm check:docs`
Expected: `tests 12`, `pass 12`, `fail 0`, then `PASS: retroplug-cli TypeScript harness`, then `check-doc-links: 69 file(s) clean` (the prototype's 68 plus this plan).

- [ ] **Step 3: The sanitizers**

The tip verification's selection over both sanitizer trees covers the resource and system cases, and it is the one
C4's logs record:

Run: `cmake --build build-tsan --target retroplug-engine-graph-test retroplug-graph-test -j$(nproc) && TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" RETROPLUG_RESOURCES_DIR=/workspaces/resources build-tsan/bin/retroplug-engine-graph-test "[resources],[resource],[systems],[host-io],[b2],[params],[settings]" && TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-graph-test "[resource],[resources]"`
Expected: `All tests passed (9341 assertions in 89 test cases)` for the engine-graph binary, then `All tests passed (200 assertions in 14 test cases)` for the graph binary, no ThreadSanitizer report from either (the build's `-Wtsan` warnings about `atomic_thread_fence` are compile warnings from the vendored concurrentqueue, not a report).

Run: `cmake --build build-asan --target retroplug-engine-graph-test retroplug-graph-test -j$(nproc) && ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" RETROPLUG_RESOURCES_DIR=/workspaces/resources build-asan/bin/retroplug-engine-graph-test "[resources],[resource],[systems],[host-io],[b2],[params],[settings]" && ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-graph-test`
Expected: the same engine-graph selection, `All tests passed (9341 assertions in 89 test cases)`, and then the whole graph binary, `All tests passed (2221 assertions in 202 test cases)`, no AddressSanitizer or LeakSanitizer report.

Then C4's own tags, which that selection does not reach. Where a stage report recorded the selection's count it is
quoted below; where none did, the step says what to check instead, and a number that differs is reported rather than a
stop.

Run: `TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-engine-graph-test "[alloc],[input-drops],[inputs]"`
Expected: every selected case passes with no ThreadSanitizer report; the input-queue stage recorded `test cases: 8 | 7 passed | 1 skipped` and `assertions: 411 | 411 passed` for this selection without the fixtures, so this run leaves `RETROPLUG_RESOURCES_DIR` off too: the GBA sections are fixture-gated, so their assertions would change the count.

Run: `TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" RETROPLUG_RESOURCES_DIR=/workspaces/resources build-tsan/bin/retroplug-engine-graph-test "[tap],[control-state],[notification],[video]"`
Expected: every selected case passes, no report; no stage records this selection's count, and the 12-way matrix is not in it (it carries no tag). The pieces are recorded individually: the capability case with `[alloc]` was `292 assertions, all passed` and the three Mesen cases `43 / 3`.

Run: `TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-engine-graph-test "a node refuses a core built with another console's kind"`
Expected: `All tests passed (72 assertions in 1 test case)`, no report: the matrix, run by name.

Run: `cmake --build build-tsan --target retroplug-audio-test -j$(nproc) && TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-audio-test "a carried MIDI run and raw run keep their bytes across a rebase,the permissive profile still drops past fifo_a's hard bound\, and keeps the rest,the wire's own buffer drops past a block's input and counts it,reliable link: a window that fills without an ack gives up the oldest and re-syncs\, not grows"`
Expected: `All tests passed (32 assertions in 4 test cases)`: C4's four new audio cases, the two commas escaped, no report.

Run: `cmake --build build-tsan --target retroplug-audio-test -j$(nproc) && TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-audio-test "[channelsplit]"`
Expected: `All tests passed (348 assertions in 9 test cases)`, no report.

Run: `TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-graph-test "[fixed-queue],[fixed-byte-ring]"`
Expected: `47 / 4` for the unit cases, no report.

Run: `cmake --build build-asan --target retroplug-engine-graph-test retroplug-graph-test -j$(nproc) && ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-engine-graph-test "[alloc],[input-drops],[inputs]" && ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" RETROPLUG_RESOURCES_DIR=/workspaces/resources build-asan/bin/retroplug-engine-graph-test "[tap],[control-state],[notification],[video]"`
Expected: the same cases pass, no AddressSanitizer or LeakSanitizer report; the report's ASan counts are the TSan ones (`7 passed | 1 skipped`, 411 assertions for the first selection).

Run: `ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-engine-graph-test "a node refuses a core built with another console's kind"`
Expected: `All tests passed (72 assertions in 1 test case)`, no report: the leg that shows the `KindWrapper` frees the core.

Run: `cmake --build build-asan --target retroplug-audio-test -j$(nproc) && ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-audio-test "a carried MIDI run and raw run keep their bytes across a rebase,the permissive profile still drops past fifo_a's hard bound\, and keeps the rest,the wire's own buffer drops past a block's input and counts it,reliable link: a window that fills without an ack gives up the oldest and re-syncs\, not grows"`
Expected: `All tests passed (32 assertions in 4 test cases)`, no report: the report's ASan `32 / 4`.

Run: `cmake --build build-asan --target retroplug-audio-test -j$(nproc) && ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-audio-test "[channelsplit]"`
Expected: `348 / 9`, no report.

Run: `ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-graph-test "[fixed-queue],[fixed-byte-ring]"`
Expected: ASan unit case `47 / 4`, no report.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources tools/run-sanitizer.sh thread && RETROPLUG_RESOURCES_DIR=/workspaces/resources tools/run-sanitizer.sh address`
Expected: each prints its passed line and no report, with the fixtures present so `dsp-threaded` runs (a system added and removed while the audio thread plays). No C4 log records this pair, so report both lines.

- [ ] **Step 4: The live hosts**

One at a time, with nothing rebuilding the plugin meanwhile. No C4 log records any of these: the prototype's
acceptance was headless, so report each line, and a failure here is a real finding about a plugin host rather than a
missing figure.

Run: `pnpm reaper:editor`
Expected: the check's `PASS` line; report it.

Run: `pnpm reaper:editor-reopen`
Expected: the check's `PASS` line; report it.

Run: `pnpm reaper:editor-autoload`
Expected: the check's `PASS` line; report it.

Run: `pnpm reaper:two-instances-load`
Expected: the check's `PASS` line, with the heartbeat's start and end counts; report both.

Run: `pnpm reaper:params`
Expected: the check's `PASS` line; report it.

Run: `pnpm reaper:params-clap`
Expected: the check's `PASS` line; report it.

Then the whole Reaper leg at once. Its fixture step authors the LSDj fixtures from `../resources/roms/lsdj/...`, which
does not resolve from this worktree (the sibling resources are at `/workspaces/resources`), so author them by hand
with explicit ROM paths and run the suite with `RP_SUITE_NO_BUILD=1`, which skips the suite's own build and every
fixture, after the build above. Make sure no Reaper or jackd of yours is left first.

Run: `node tools/author-rplg.js && node tools/author-nes-rplg.js && node tools/author-risa-rplg.js /workspaces/resources/roms/risa/risa-v2.3.0/risa-2.3.0-pal.nes && node tools/author-sms-rplg.js && node tools/author-sms-rplg.js resources/roms/smsggdj_v0_45.gg && node tools/author-lsdj-rplg.js midi-metro /workspaces/resources/roms/lsdj/lsdj9_4_2.gb && node tools/author-lsdj-rplg.js midi-drift /workspaces/resources/roms/lsdj/lsdj9_4_2.gb && node tools/author-lsdj-rplg.js arduinoboy-metro /workspaces/resources/roms/lsdj/lsdj9_3_3-arduinoboy.gb`
Expected: an `authored build/<name>.rplg.zip` line for each (eight).

Run: `pgrep -a reaper; pgrep -a jackd; RP_SUITE_NO_BUILD=1 pnpm reaper:all`
Expected: no leftover process listed, then the suite's `REAPER CHECKS` line; report it. If a render fails, run that one check alone once (its own `pnpm reaper:<name>`) and report both results: concurrency in this container has flaked before.

Run: `pnpm sdl:smoke && pnpm sdl:pipewire`
Expected: the smoke's pass line, then the PipeWire check's (the wide-output leg ran; where wireplumber will not start it prints `SKIPPED: wide-output checks need wireplumber` and fewer checks pass). Report both lines.

- [ ] **Step 5: The consumers, against this tree's CLI**

Their suites read `src/` and write beside the ROMs, so each runs from a scratch copy and leaves its repository
untouched. Never edit `/workspaces/bliptoaster` or `/workspaces/nesvj`: both commands copy the suite to `/tmp`, run it
there, and remove the copy, and both repositories' `git status --short` must stay empty. C4 changes no TS source and
no CLI surface, so both pass without an edit.

Run: `R=$PWD; rm -rf /tmp/c4-bt && mkdir -p /tmp/c4-bt/build /tmp/c4-bt/retroplug-cli && cp -r /workspaces/bliptoaster/src /tmp/c4-bt/ && cp /workspaces/bliptoaster/build/*.nes /workspaces/bliptoaster/build/*.dbg /workspaces/bliptoaster/build/*.map /tmp/c4-bt/build/ && cp -r /workspaces/bliptoaster/retroplug-cli/tests /tmp/c4-bt/retroplug-cli/ && (cd /tmp/c4-bt && "$R/build/bin/retroplug-cli" test retroplug-cli/tests --rom build/bliptoaster.nes); e=$?; rm -rf /tmp/c4-bt; exit $e`
Expected: `PASS: 57/57 test file(s) ok`, its last file 49 passed with none skipped or failed.

Run: `R=$PWD; rm -rf /tmp/c4-vj && mkdir -p /tmp/c4-vj/build /tmp/c4-vj/retroplug-cli && cp -r /workspaces/nesvj/src /tmp/c4-vj/ && cp /workspaces/nesvj/build/*.nes /workspaces/nesvj/build/*.dbg /workspaces/nesvj/build/*.map /tmp/c4-vj/build/ && cp -r /workspaces/nesvj/retroplug-cli/tests /tmp/c4-vj/retroplug-cli/ && (cd /tmp/c4-vj && "$R/build/bin/retroplug-cli" test retroplug-cli/tests --rom build/nesvj.nes); e=$?; rm -rf /tmp/c4-vj; exit $e`
Expected: `PASS: 19/19 test file(s) ok`, its last file 23 passed with none skipped or failed.

Run: `git -C /workspaces/bliptoaster status --short && git -C /workspaces/nesvj status --short`
Expected: nothing printed: neither repository was edited.

Run: `pnpm test:cli-ts`
Expected: `PASS: retroplug-cli TypeScript harness`.

- [ ] **Step 6: The profile build and the benches, for the record**

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources tools/run-profile.sh unit`
Expected: the profile build's unit run passes; report its lines. No C4 log records this command.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources tools/run-profile.sh stats && tools/run-profile.sh stats nes-bench`
Expected: dsp-bench's windows and nes-bench's, each printing its `DSP-BENCH` line; every window must report zero allocations a block, as the audio-path stages recorded: `"allocsPerBlock":0,"bytesPerBlock":0,"freesPerBlock":0,"maxBlockAllocs":0` for the DSP profile and the same four fields on nes-bench's `"core":"mesen"` line. These windows count the script host's allocator (QuickJS's); the native zero is the `[alloc]` cases'. No C4 log records the acceptance run's lines, so report them; without `RETROPLUG_RESOURCES_DIR` the LSDj windows skip after the mGB window passes.

Run: `tools/run-profile.sh trace`
Expected: `build-prof/trace.json` written; report the span names it carries.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources build/bin/retroplug-engine-graph-test "[bench]" && build/bin/retroplug-graph-test "[bench]"`
Expected: three outputs, all reported. First C3's boot-cost bench (Phase C3's Task 2 added it; Release, 48 kHz, median of 5), a row per console shaped like `  GBA              boot    15.3 ms   through setGraph    16.5 ms   (8192 KiB of ROM)`; the GBA and LSDj rows need `RETROPLUG_RESOURCES_DIR`, and without it they do not print. Second B3a's script-node table over 1, 64 and 512 frame blocks, and the protocol nodes' planner table. Third the graph binary's planner bench line and table.

Run: `git status --short`
Expected: only ` m deps/sameboy`.

