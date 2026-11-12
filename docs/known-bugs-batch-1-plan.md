# Known bugs, batch 1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Five fixes from [spec/07](../spec/07-remaining-work.md)'s "Known bugs, recorded and not fixed" list: Mesen's
VRC7 sound chip builds its process-wide tables once from any thread, a Game Boy's press queue keeps room for the
releases of what it holds and bounds the button byte, a render's snapshot files become the job pool's to delete (with a
startup sweep for what a crash leaves), and the two documents record the batch.

**Architecture:** Two fixes are local to a core or a queue. `deps/mesen`'s emu2413 drops its plain initialisation flag
for a function-local static, and `packages/sameboy`'s `SameBoySystem::pressButton` gains a reserve for releases (one bit
a button in `queuedDown_`) and refuses a byte past the console's button range before it reaches SameBoy's key arrays.
The rest crosses the seam: `EngineJobRequest` and `RpcEngineJobRequest` gain `discardFiles`, the offline pool owns and
deletes them however a job ends, `startSystemRender` hands its snapshots over instead of deleting them on settle,
`HostRpcService::fileAgeMs` ages a file on the file clock, and `sweepRenderSnapshots` (called from `composeAppStores`)
takes a day-old leftover. Task 5 is the docs.

**Tech Stack:** C++20 with Catch2 (`retroplug-mesen-test`, `retroplug-engine-graph-test`, `retroplug-audio-test`,
`retroplug-sameboy-test`) and ThreadSanitizer in `build-tsan`; the vendored Mesen in-tree (plain source changes marked
`// RetroPlug:`) and the pinned SameBoy submodule; msgpack/rfl RPCs; TypeScript with the repo's own TAP runners
(`pnpm test`, `pnpm test:native`, `pnpm test:ui`, `pnpm test:plugin`); pnpm for the workspace packages.
No submodule pointer moves.

**Spec:** [known-bugs-batch-1-design.md](known-bugs-batch-1-design.md). Its section 7 lists these five tasks and section
6 the docs; the acceptance run is the controller's, after this batch lands, so nothing here re-records a golden, a
parity baseline or a digest.

**Provenance:** every code block below was applied to this worktree at `99e1c04d5` in this plan's order, test first and
then the implementation, and built and run before the plan was written: linux-x64, GCC 13.3, with the sibling fixture
tree at `/workspaces/resources`. There is no prototype branch, so afterward the worktree was restored to its starting
state and the plan was replayed by text: each task's patches applied in order to copies of the touched files at
`99e1c04d5`, the result byte-identical to the validated tree. Every Expected line is quoted from a real run, and a
failing-test step's output is what the code without that step's fix produces.

**How to apply a step's code.** Each code step gives one `patch` block, a `git apply`-able unified diff against the
tree as the steps before it left it. Apply one by running this with the block's text (not its fences) between the
heredoc's first line and the closing `PATCH`, or by making the same hunks with the editor:

```
git -C /workspaces/RetroPlug-main/cuttlefish apply - <<'PATCH'
<the step's patch block>
PATCH
```

The hunks are labelled with the file they belong to. Every command below is written out with absolute paths or `-C`, so
no step needs to change directory.

## Ground rules

These are the repo's rules this batch must keep. They are not optional and no step repeats them.

- **Never leave the worktree**: prefix git with `git -C /workspaces/RetroPlug-main/cuttlefish` and pnpm with
  `pnpm -C /workspaces/RetroPlug-main/cuttlefish` (or use a subshell). Never switch branches, never `git stash`, never
  `git reset --hard`, never push.
- **`deps/sameboy` is modified in a fresh tree and that is expected** (its tracked patches are applied at configure).
  Never stage, reset or stash it, and never touch `deps/dpf.js` or any other submodule. `git status --porcelain` may
  list it; `git add` only the paths a step names.
- **`deps/mesen` is vendored in-tree**, not a submodule, so its fixes are ordinary source changes, each carrying a
  `// RetroPlug:` comment saying why the fork differs from upstream.
- **Never `rm -rf build`** (or any `build*/`). `-D` a configure if one is needed. Build a target with
  `cmake --build <dir> --target <t> -j$(nproc)`: the default is single-threaded.
- **The em dash (U+2014) never appears** in code, comments, docs or commit messages. Use " - ", ":" or ";".
- **A commit message is a subject line, a blank line, then the body**, committed with `git commit -F -`. No
  `Co-Authored-By`, no "generated with" line, no attribution.
- **Match the surrounding code**: names, comment density and wrapping (comments explain WHY, in full sentences, at
  about 116-120 columns, and say what IS, not change history).
- **Nothing in this batch opens an audio or MIDI device.** Never run `retroplug-sdl`, any `sdl:*` script, any `reaper:*`
  script or anything else that initialises PortAudio or SDL audio, and never run a MIDI-port-enumerating harness. The
  Catch2 binaries, `pnpm test`, `pnpm test:native` and `pnpm test:ui` open no device.
- **Nothing is re-recorded**: no `RP_*_UPDATE` variable, no golden, no parity baseline, no digest. A golden or parity
  change would be a bug to find, not a step.
- **Never send repository content to an external service.**

---

## Task 1: emu2413's VRC7 tables are built once, from any thread

A VRC7 cart builds Mesen's emu2413 for the NES's expansion audio, and the chip's four tables are process-wide: whoever
reaches `OPLL_new` first builds them. They were built behind `static uint8_t table_initialized`, so two carts built on
two threads at once (the engine's offline pool beside the live engine, or two render jobs) both saw it clear and both
wrote the tables while either could read them. A function-local `static const bool` runs its initialiser exactly once
and makes every other thread reaching it wait until it has finished; that replaces the flag.

**Files** (line numbers are the file at `99e1c04d5`):

- Modify: `deps/mesen/Core/Shared/Utilities/emu2413.cpp:414-422` (the flag and `initializeTables`) and `:1101-1107`
  (`OPLL_new`'s check).
- Test: `packages/mesen/native/test/MesenShared.test.cpp:32-33` (the includes) and `:206-215` (a case after the GBA
  statics one, at the file's end).

**Interfaces:**

- Consumes: that test file's `onTwoThreadsAtOnce(const std::function<void()>&)` (its line 57), which releases two
  threads together; and emu2413's own API, `OPLL *OPLL_new(uint32_t clk, uint32_t rate)` and `void OPLL_delete(OPLL *)`
  (`deps/mesen/Core/Shared/Utilities/emu2413.h:128-129`).
- Produces: nothing another task or another file uses. `initializeTables` and the new `ensureTables` are file-static.

### Steps

- [ ] **Step 1: write the failing case**

Add `emu2413.h` to the test's includes and the case at the end of the file:

```patch
diff --git a/packages/mesen/native/test/MesenShared.test.cpp b/packages/mesen/native/test/MesenShared.test.cpp
index af247f3..3cc80c0 100644
--- a/packages/mesen/native/test/MesenShared.test.cpp
+++ b/packages/mesen/native/test/MesenShared.test.cpp
@@ -30,6 +30,7 @@
 #include "NES/Input/BarcodeBattlerReader.h"
 #include "NES/Input/FourScore.h"
 #include "Shared/NotificationManager.h"
+#include "Shared/Utilities/emu2413.h"
 #include "system/mesen/MesenGlobalInit.hpp"
 
 namespace {
@@ -213,3 +214,20 @@ TEST_CASE("two GBA consoles can be built on two threads at once, the first in th
     });
     CHECK(built.load() == 2);
 }
+
+TEST_CASE("the VRC7 sound chip's tables can be built on two threads at once, the first in the process",
+          "[engine-graph][mesen][mesen-statics]") {
+    // A VRC7 cart builds Mesen's emu2413 (the OPLL) for the NES's expansion audio, and the chip's tables are
+    // process-wide: whoever calls OPLL_new first builds them, and every later call reads them. With the plain
+    // `table_initialized` flag two carts' first builds both saw it clear, so both wrote the tables while either could
+    // read them - the race ThreadSanitizer reports as the process's first build. Each thread builds and frees a chip
+    // of its own, which is a boot's own call.
+    std::atomic<int> built{0};
+    onTwoThreadsAtOnce([&] {
+        OPLL* chip = OPLL_new(3579545, 48000);
+        if (!chip) return;
+        OPLL_delete(chip);
+        built.fetch_add(1);
+    });
+    CHECK(built.load() == 2);
+}
```

- [ ] **Step 2: build it under ThreadSanitizer and watch it report the race**

The tables are built once per process, so the case only proves the race as the process's FIRST VRC7 build: run the tag
alone, and nothing that builds a chip before it. `build-tsan` is the thread-sanitizer tree
([tools/run-sanitizer.sh](../tools/run-sanitizer.sh) configures it this way); if it is absent, configure it first:

```bash
cmake -S /workspaces/RetroPlug-main/cuttlefish -B /workspaces/RetroPlug-main/cuttlefish/build-tsan \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=ON -DRETROPLUG_SANITIZE=thread \
  -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
cmake --build /workspaces/RetroPlug-main/cuttlefish/build-tsan --target retroplug-mesen-test -j$(nproc)
```

```bash
TSAN_OPTIONS="suppressions=/workspaces/RetroPlug-main/cuttlefish/packages/native/test/sanitizer/tsan.supp halt_on_error=1 second_deadlock_stack=1" \
  /workspaces/RetroPlug-main/cuttlefish/build-tsan/bin/retroplug-mesen-test "[mesen-statics]"
```

Expected: exit 66, and a report whose first frames are the tables being written by both threads (`emu2413.cpp`'s line
numbers are the file before this task's fix):

```
==================
WARNING: ThreadSanitizer: data race (pid=1761630)
  Write of size 4 at 0x55b6f72a8064 by thread T1:
    #0 makeTllTable /workspaces/RetroPlug-main/cuttlefish/deps/mesen/Core/Shared/Utilities/emu2413.cpp:388
    #1 initializeTables /workspaces/RetroPlug-main/cuttlefish/deps/mesen/Core/Shared/Utilities/emu2413.cpp:417
    #2 OPLL_new(unsigned int, unsigned int) /workspaces/RetroPlug-main/cuttlefish/deps/mesen/Core/Shared/Utilities/emu2413.cpp:1106
    ...
  Previous write of size 4 at 0x55b6f72a8064 by thread T2:
    #0 makeTllTable (emu2413.cpp:388)
    ...
  Location is global 'tll_table' of size 131072 at 0x55b6f72a8060
...
SUMMARY: ThreadSanitizer: data race /workspaces/RetroPlug-main/cuttlefish/deps/mesen/Core/Shared/Utilities/emu2413.cpp:388 in makeTllTable
```

(The stacks' build ids, pids and addresses differ run to run; the frames, the summary and the exit code do not. A fresh
`build-tsan` compiles the vendored Mesen first, which takes a few minutes.)

- [ ] **Step 3: build the tables once**

```patch
diff --git a/deps/mesen/Core/Shared/Utilities/emu2413.cpp b/deps/mesen/Core/Shared/Utilities/emu2413.cpp
index f477eaf..df8fd67 100644
--- a/deps/mesen/Core/Shared/Utilities/emu2413.cpp
+++ b/deps/mesen/Core/Shared/Utilities/emu2413.cpp
@@ -411,14 +411,24 @@ static void makeDefaultPatch(void) {
       OPLL_getDefaultPatch(i, j, &default_patch[i][j * 2]);
 }
 
-static uint8_t table_initialized = 0;
-
 static void initializeTables(void) {
   makeTllTable();
   makeRksTable();
   makeSinTable();
   makeDefaultPatch();
-  table_initialized = 1;
+}
+
+/* RetroPlug: these tables are process-wide, built by whichever chip is created first and read by every one after it,
+   and two VRC7 carts can be built on two threads at once (the engine's offline pool beside the live engine, two render
+   jobs), so the plain `table_initialized` flag this used to check let both write the tables while either read them.
+   A block-scope static's initialiser runs exactly once, and every other thread that reaches it waits until it has
+   finished, which is the ordering the flag was spelling out by hand. */
+static void ensureTables(void) {
+  static const bool initialized = [] {
+    initializeTables();
+    return true;
+  }();
+  (void)initialized;
 }
 
 /*********************************************************
@@ -1102,9 +1112,7 @@ OPLL *OPLL_new(uint32_t clk, uint32_t rate) {
   OPLL *opll;
   int i;
 
-  if (!table_initialized) {
-    initializeTables();
-  }
+  ensureTables();
 
   opll = (OPLL *)calloc(sizeof(OPLL), 1);
   if (opll == NULL)
```

- [ ] **Step 4: rebuild and run the tag again**

```bash
cmake --build /workspaces/RetroPlug-main/cuttlefish/build-tsan --target retroplug-mesen-test -j$(nproc)
TSAN_OPTIONS="suppressions=/workspaces/RetroPlug-main/cuttlefish/packages/native/test/sanitizer/tsan.supp halt_on_error=1 second_deadlock_stack=1" \
  /workspaces/RetroPlug-main/cuttlefish/build-tsan/bin/retroplug-mesen-test "[mesen-statics]"
```

Expected: no report, exit 0, and the three `[mesen-statics]` cases pass:

```
===============================================================================
All tests passed (3 assertions in 3 test cases)
```

- [ ] **Step 5: run the binary this file belongs to, in the ordinary build**

```bash
cmake --build /workspaces/RetroPlug-main/cuttlefish/build --target retroplug-mesen-test -j$(nproc)
/workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-mesen-test
```

Expected:

```
===============================================================================
All tests passed (82322 assertions in 81 test cases)
```

(`pnpm -C /workspaces/RetroPlug-main/cuttlefish test:plugin mesen` is the same run through the repo's runner.)

- [ ] **Step 6: commit**

```bash
git -C /workspaces/RetroPlug-main/cuttlefish add -- \
  deps/mesen/Core/Shared/Utilities/emu2413.cpp \
  packages/mesen/native/test/MesenShared.test.cpp
git -C /workspaces/RetroPlug-main/cuttlefish commit -F - <<'MSG'
fix(kb1): build emu2413's tables once, from any thread

A VRC7 cart builds Mesen's emu2413 for the NES's expansion audio, and the chip's tables (makeTllTable, makeRksTable,
makeSinTable, makeDefaultPatch) are process-wide: whoever reaches OPLL_new first builds them. They were built behind a
plain `static uint8_t table_initialized`, so two VRC7 carts built on two threads at once (the engine's offline pool
beside the live engine, or two render jobs) both saw it clear and both wrote the tables while either could read them.
ThreadSanitizer reports exactly that as the process's first build: `[mesen-statics]` run alone in build-tsan, before
this fix, is a data race on tll_table.

A block-scope `static const bool` runs its initialiser exactly once, and every other thread reaching it waits until it
has finished, which is the ordering the flag was spelling out by hand. OPLL_new calls ensureTables() and the flag is
gone.
MSG
```

---

## Task 2: a Game Boy's press queue keeps room for releases, and the button byte is bounded

`SameBoySystem::pressButton` appends an edge (`{offset, button, down}`) to `pendingButtons_`, a `rp::FixedQueue` of 1024
entries, each scheduled 10 ms after the one before so the joypad debouncer sees every edge in turn; a Game Boy's edges
cannot coalesce the way a Mesen console's do. The queue drains about a hundred edges a second, so a session that applies
pushes inline between blocks (the CLI, an offline render) or a script node saturating its buttons output can outrun it,
and the tail past the bound was dropped, release included, leaving a button held. Two rules go in, in the same function:

- the byte bound first: a byte of `GameboyButton::Count` (8) or more counts as a press this core took, and takes no
  slot, adds no spacing and drives nothing. SameBoy checks a key index with an `assert` alone
  (`deps/sameboy/Core/joypad.c`), so in a release build such a byte wrote past the pad's own arrays;
- the reserve: `queuedDown_` keeps one bit a button, the state its last queued edge left it in. A release of a button
  still held needs one free slot; every other edge (a press, or a release of a button already up) needs more than
  `GameboyButton::Count` free. Free slots are `capacity() - size()`. A reserve refusal is counted in `pressesRefused_`,
  which `inputDropped()` adds to the queue's own `dropped()`, and `queuedDown_` changes only when the push succeeds.

The invariant, which the header states: free slots are never fewer than the buttons held. A press is taken only with
more than 8 free, which leaves at least 8, and at most 8 buttons are held; a held release frees a held button for the
slot it takes. So once a button's press is queued, its release always fits. Below the last 8 slots nothing differs,
which is why the `[alloc]` whole-block case still sees zero Game Boy drops at 64 presses a block.

**Files** (line numbers are the file at `99e1c04d5`):

- Modify: `packages/sameboy/native/src/system/sameboy/SameBoySystem.hpp:50-54` (`pressButton`, where the two read-only
  seams go after it), `:79-80` (`inputDropped`), `:241-252` (the queue's comment, `queuedDown_`, `pressesRefused_`).
- Modify: `packages/sameboy/native/src/system/sameboy/SameBoySystem.cpp:600-611` (`pressButton`; `peekPendingPress` goes
  after it).
- Test: `packages/native/test/engine-graph/SystemNode.test.cpp:3747-3756` (the "Game Boy presses" section of the
  drops case), `:3817-3887` (the Mesen burst case, after which the new case goes), `:3889-3917` (the stray-byte case,
  which gains a Game Boy section).

**Interfaces:**

- Consumes: `rp::kButtonsInput` and `rp::kPressInput` (`system/InputCapacity.hpp`), `GameboyButton::Count`
  (`system/sameboy/InputTypes.hpp`), and `rp::FixedQueue`'s `capacity()`, `size()` and `dropped()`.
- Produces: `bool SameBoySystem::peekPendingPress(std::uint8_t button, bool& down) const` (the last queued edge for a
  button, false when none is queued) and `std::size_t SameBoySystem::pendingPresses() const` (the queue's size). Both
  are read-only test seams, shaped like `MesenNesSystem::peekPendingPress`; no other task uses them.

### Steps

- [ ] **Step 1: write the failing test**

The three changes to `SystemNode.test.cpp`, in one patch: the drops case's Game Boy section re-derived (the arithmetic
is in its comment), a new case for the reserve, and a Game Boy section for the stray byte:

```patch
diff --git a/packages/native/test/engine-graph/SystemNode.test.cpp b/packages/native/test/engine-graph/SystemNode.test.cpp
index 483e2c0..3b3535a 100644
--- a/packages/native/test/engine-graph/SystemNode.test.cpp
+++ b/packages/native/test/engine-graph/SystemNode.test.cpp
@@ -3745,14 +3745,19 @@ TEST_CASE("a core's ingress queues drop what is past their bound, count it, and
         ran(*core, {lanes.ptrs[0], lanes.ptrs[1]});
     }
     SECTION("Game Boy presses") {
+        // A Game Boy's queue cannot coalesce, since every edge is meant to be seen in turn, so it keeps the last slots
+        // for the releases of what it holds: a press, and a release of a button already up, is taken only with more
+        // than the console's 8 buttons free, i.e. while fewer than 1024 - 8 = 1016 entries are queued. Every edge of
+        // this loop is one of those (the even ones press buttons 0, 2, 4 and 6, the odd ones release 1, 3, 5 and 7,
+        // which nothing pressed), so the 1028 edges fill 1016 entries and the last 12 are refused.
         auto core = mgb(1);
         core->reserveAudio(8192);
         Lanes lanes(2);
         constexpr std::uint32_t kCapacity = rp::kButtonsInput * 16;
         for (std::uint32_t i = 0; i < kCapacity + 4; ++i)
             core->pressButton(static_cast<std::uint8_t>(i % 8), (i % 2) == 0);
-        CHECK(core->inputAccepted() == kCapacity);
-        CHECK(core->inputDropped() == 4);
+        CHECK(core->inputAccepted() == kCapacity - 8);
+        CHECK(core->inputDropped() == 12);
         ran(*core, {lanes.ptrs[0], lanes.ptrs[1]});
     }
     SECTION("Game Boy serial bytes") {
@@ -3886,6 +3891,31 @@ TEST_CASE("a Mesen press burst cannot push a release out of its queue", "[engine
     }
 }
 
+TEST_CASE("a Game Boy's press queue keeps room for the releases of what it holds",
+          "[engine-graph][system-node][input-drops]") {
+    // The queue holds 1024 entries and a press is taken only with more than the console's 8 buttons free, so a flood
+    // of presses stops at 1016 and leaves exactly the 8 slots a release may need. At most 8 buttons are held then, and
+    // each release frees the button it lets go, so the flood can never push out the edge that lets a button go: a
+    // button held after the flood comes back up. A press past that point is refused, since nothing is free any more.
+    auto core = mgb(1);
+    constexpr std::uint32_t kCapacity = rp::kButtonsInput * 16;
+    constexpr std::uint32_t kReserve = static_cast<std::uint32_t>(GameboyButton::Count);
+    for (std::uint32_t i = 0; i < kCapacity + 16; ++i) core->pressButton(static_cast<std::uint8_t>(i % 8), true);
+    CHECK(core->inputAccepted() == kCapacity - kReserve);
+    CHECK(core->inputDropped() == 16 + kReserve);
+    for (std::uint8_t button = 0; button < kReserve; ++button) core->pressButton(button, false);
+    CHECK(core->inputAccepted() == kCapacity);
+    CHECK(core->inputDropped() == 16 + kReserve);
+    for (std::uint8_t button = 0; button < kReserve; ++button) {
+        bool down = true;
+        REQUIRE(core->peekPendingPress(button, down));
+        CHECK_FALSE(down);   // the flood's press, and behind it the release that stands
+    }
+    core->pressButton(0, true);   // the queue's bound reached exactly: the reserve is empty and this needs it
+    CHECK(core->inputAccepted() == kCapacity);
+    CHECK(core->inputDropped() == 17 + kReserve);
+}
+
 TEST_CASE("a press of a byte the console has no button for drives no button",
           "[engine-graph][system-node][input-drops]") {
     // The press queue coalesces by the wire byte, so the entry that stands for a button is the last one naming it -
@@ -3918,6 +3948,21 @@ TEST_CASE("a press of a byte the console has no button for drives no button",
         core->onActivate(kRate);
         strayByte(*core, 200);
     }
+    SECTION("Game Boy") {
+        // A Game Boy cannot coalesce, so a byte past its table also has to leave the queue alone: it is taken (a press
+        // the console has no bit for), it drives no key past the pad's arrays, and the two entries that stand are A's
+        // press and its release.
+        auto core = mgb(1);
+        core->pressButton(4, true);      // A
+        core->pressButton(200, true);    // past GameboyButton::Count
+        core->pressButton(4, false);
+        CHECK(core->inputAccepted() == 3);
+        CHECK(core->inputDropped() == 0);
+        CHECK(core->pendingPresses() == 2);
+        bool down = true;
+        REQUIRE(core->peekPendingPress(4, down));
+        CHECK_FALSE(down);
+    }
 }
 
 TEST_CASE("the tap is a capability only a NES offers", "[engine-graph][system-node][tap]") {
```

- [ ] **Step 2: build it and watch it fail to compile**

```bash
cmake --build /workspaces/RetroPlug-main/cuttlefish/build --target retroplug-engine-graph-test -j$(nproc)
```

Expected: the build fails, because the seams do not exist yet:

```
/workspaces/RetroPlug-main/cuttlefish/packages/native/test/engine-graph/SystemNode.test.cpp:3911:23: error: 'class SameBoySystem' has no member named 'peekPendingPress'
/workspaces/RetroPlug-main/cuttlefish/packages/native/test/engine-graph/SystemNode.test.cpp:3961:21: error: 'class SameBoySystem' has no member named 'pendingPresses'
gmake[3]: *** [native/CMakeFiles/retroplug-engine-graph-test.dir/build.make:90: native/CMakeFiles/retroplug-engine-graph-test.dir/test/engine-graph/SystemNode.test.cpp.o] Error 1
```

- [ ] **Step 3: add the two read-only seams**

They observe the queue and change no behaviour, so the test can be written and its red is the arithmetic rather than a
missing name:

```patch
diff --git a/packages/sameboy/native/src/system/sameboy/SameBoySystem.cpp b/packages/sameboy/native/src/system/sameboy/SameBoySystem.cpp
index 6bbf18f..80fb991 100644
--- a/packages/sameboy/native/src/system/sameboy/SameBoySystem.cpp
+++ b/packages/sameboy/native/src/system/sameboy/SameBoySystem.cpp
@@ -609,6 +609,18 @@ void SameBoySystem::pressButton(std::uint8_t button, bool down) {
     if (pendingButtons_.push(PendingButton{offset, static_cast<GameboyButton>(button), down})) ++pressesAccepted_;
 }
 
+bool SameBoySystem::peekPendingPress(std::uint8_t button, bool& down) const {
+    // The queue is applied in order, so the entry that stands for `button` is the last one naming it: a pass over the
+    // live entries, oldest first, leaves that one's value behind.
+    bool held = false;
+    for (const PendingButton& pending : pendingButtons_) {
+        if (static_cast<std::uint8_t>(pending.button) != button) continue;
+        down = pending.down;
+        held = true;
+    }
+    return held;
+}
+
 std::vector<ChannelStream> SameBoySystem::channelLayout() const {
     // GB_channel_t order: GB_SQUARE_1, GB_SQUARE_2, GB_WAVE, GB_NOISE.
     return {{"Pulse 1", true}, {"Pulse 2", true}, {"Wave", true}, {"Noise", true}};
diff --git a/packages/sameboy/native/src/system/sameboy/SameBoySystem.hpp b/packages/sameboy/native/src/system/sameboy/SameBoySystem.hpp
index a18b22f..3e04c2e 100644
--- a/packages/sameboy/native/src/system/sameboy/SameBoySystem.hpp
+++ b/packages/sameboy/native/src/system/sameboy/SameBoySystem.hpp
@@ -53,6 +53,16 @@ public:
     // SameBoyUtil.cpp:149-163).
     void pressButton(std::uint8_t button, bool down) override;
 
+    // The transition the queue will leave on button `button` when it applies, false when it leaves none (a queue with
+    // no entry naming it, or a byte outside the console's table, which takes no slot). The queue is applied in order,
+    // so the entry that stands for a button is the last one naming it. A test seam of the shape MesenNesSystem's is:
+    // a block applies what is due and carries the rest, so a false after the queue drained is the read that it was
+    // taken.
+    bool peekPendingPress(std::uint8_t button, bool& down) const;
+    // How many transitions are queued and not yet applied: the queue's own size, so a test can hold that an edge the
+    // console has no button for took no slot and one it has did. A read-only seam beside peekPendingPress.
+    std::size_t pendingPresses() const { return pendingButtons_.size(); }
+
     // Queue a raw byte for the GB serial port, scheduled at sample offset
     // `frame` within the block. Drained MSB-first by nextSerialInBit() / the
     // slave-mode pump in stepIfBelowTarget(), neither before `frame` is reached.
```

- [ ] **Step 4: build and run, and watch the numbers fail**

```bash
cmake --build /workspaces/RetroPlug-main/cuttlefish/build --target retroplug-engine-graph-test -j$(nproc)
/workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-engine-graph-test "[input-drops]"
```

Expected: thirteen failed assertions across four checks, listed below in the order they run (the GBA fixture ROM is
absent in a bare tree, which is what the SKIP is; with `RETROPLUG_RESOURCES_DIR=/workspaces/resources` on the command
the same failures appear and the GBA sections run):

```
/workspaces/RetroPlug-main/cuttlefish/packages/native/test/engine-graph/SystemNode.test.cpp:3904: FAILED:
  CHECK( core->inputAccepted() == kCapacity - kReserve )
with expansion:
  1024 (0x400) == 1016 (0x3f8)

/workspaces/RetroPlug-main/cuttlefish/packages/native/test/engine-graph/SystemNode.test.cpp:3905: FAILED:
  CHECK( core->inputDropped() == 16 + kReserve )
with expansion:
  16 == 24

/workspaces/RetroPlug-main/cuttlefish/packages/native/test/engine-graph/SystemNode.test.cpp:3912: FAILED:
  CHECK_FALSE( down )
with expansion:
  !true

...seven more of the same, one a button...

/workspaces/RetroPlug-main/cuttlefish/packages/native/test/engine-graph/SystemNode.test.cpp:3961: FAILED:
  CHECK( core->pendingPresses() == 2 )
with expansion:
  3 == 2

/workspaces/RetroPlug-main/cuttlefish/packages/native/test/engine-graph/SystemNode.test.cpp:3759: FAILED:
  CHECK( core->inputAccepted() == kCapacity - 8 )
with expansion:
  1024 (0x400) == 1016 (0x3f8)

/workspaces/RetroPlug-main/cuttlefish/packages/native/test/engine-graph/SystemNode.test.cpp:3760: FAILED:
  CHECK( core->inputDropped() == 12 )
with expansion:
  4 == 12

===============================================================================
test cases:  4 |  0 passed |  3 failed | 1 skipped
assertions: 89 | 76 passed | 13 failed
```

The releases at 3912 are the fix's whole point: with the queue full, the old code refused all eight releases, so each
button's last queued edge is still its press. The stray byte's 3 queued entries are the other half: it took a slot it
must not (the old code queued it, and a release build would have driven `gb->keys[0][200]`).

- [ ] **Step 5: the reserve and the byte bound**

```patch
diff --git a/packages/sameboy/native/src/system/sameboy/SameBoySystem.cpp b/packages/sameboy/native/src/system/sameboy/SameBoySystem.cpp
index 80fb991..d9b10f8 100644
--- a/packages/sameboy/native/src/system/sameboy/SameBoySystem.cpp
+++ b/packages/sameboy/native/src/system/sameboy/SameBoySystem.cpp
@@ -598,15 +598,43 @@ void SameBoySystem::applyDisplayConfig() {
 }
 
 void SameBoySystem::pressButton(std::uint8_t button, bool down) {
+    // A byte the console has no button for drives nothing: SameBoy checks a key's index with an assert alone
+    // (Core/joypad.c), so one of 8 or more would write past the pad's own arrays in a release build. It is still a
+    // press this core took, as a Mesen console answers for a byte outside its table, so it takes no slot, adds no
+    // spacing and drives no bit.
+    if (button >= static_cast<std::uint8_t>(GameboyButton::Count)) {
+        ++pressesAccepted_;
+        return;
+    }
     // Append to the back of the queue, advancing the offset by buttonSpacing
     // from the previous entry. This stops a press+release pair sent in the
     // same UI tick from collapsing onto a single sample (which the joypad
     // debouncer would simply miss). Mirrors the old SameBoyUtil::processButtons
     // logic in the legacy build's SameBoyUtil.
+    //
+    // What is scheduled is a release of a button still held, or the release of one already up (or a press): the first
+    // needs one free slot, so a held button always comes back up, and the second needs more than the console's eight
+    // buttons' worth, so the reserve above them stays for releases (queuedDown_'s comment in the header). An edge the
+    // reserve refuses is counted, so inputDropped() still says how many this core did not take.
+    const std::uint8_t bit = static_cast<std::uint8_t>(1u << button);
+    const bool heldRelease = !down && (queuedDown_ & bit) != 0;
+    const std::size_t need = heldRelease ? 1 : static_cast<std::size_t>(GameboyButton::Count) + 1;
+    if (pendingButtons_.capacity() - pendingButtons_.size() < need) {
+        ++pressesRefused_;
+        return;
+    }
     std::uint32_t offset = 0;
     if (!pendingButtons_.empty())
         offset = pendingButtons_.back().offset + buttonSpacingSamples_;
-    if (pendingButtons_.push(PendingButton{offset, static_cast<GameboyButton>(button), down})) ++pressesAccepted_;
+    // The room above is the push's (it compacts a consumed prefix rather than refusing), so this cannot be what
+    // refuses an edge; a queue that did refuse one would still be counted as not having taken it.
+    if (!pendingButtons_.push(PendingButton{offset, static_cast<GameboyButton>(button), down})) {
+        ++pressesRefused_;
+        return;
+    }
+    if (down) queuedDown_ |= bit;
+    else queuedDown_ = static_cast<std::uint8_t>(queuedDown_ & ~bit);
+    ++pressesAccepted_;
 }
 
 bool SameBoySystem::peekPendingPress(std::uint8_t button, bool& down) const {
diff --git a/packages/sameboy/native/src/system/sameboy/SameBoySystem.hpp b/packages/sameboy/native/src/system/sameboy/SameBoySystem.hpp
index 3e04c2e..b90ca53 100644
--- a/packages/sameboy/native/src/system/sameboy/SameBoySystem.hpp
+++ b/packages/sameboy/native/src/system/sameboy/SameBoySystem.hpp
@@ -87,7 +87,9 @@ public:
     // inputDropped): serial-in bytes and button presses. A plan's block fits the queues, so a drop is a push outside
     // one: a core fed by a test or tool, or a press burst scheduled past the queue's carry.
     std::uint64_t inputAccepted() const override { return serialInAccepted_ + pressesAccepted_; }
-    std::uint64_t inputDropped() const override { return serialInDropped_ + pendingButtons_.dropped(); }
+    std::uint64_t inputDropped() const override {
+        return serialInDropped_ + pendingButtons_.dropped() + pressesRefused_;
+    }
     // The serial-in bytes the queues have taken since the core was built: the non-press half of inputAccepted(), for a
     // test that holds a staged serial run reached the core rather than only a press.
     std::uint64_t serialInAccepted() const { return serialInAccepted_; }
@@ -251,7 +253,9 @@ public:
     // collapse to zero duration (which the SameBoy joypad debouncer would miss): each press is scheduled 10 ms after
     // the one before it, so what a block adds can still be queued several blocks later. The bound is a block's buttons
     // input sixteen times over (about ten seconds of scheduled presses at 48 kHz), and a press beyond it is refused
-    // and counted rather than growing the queue on the block thread.
+    // and counted rather than growing the queue on the block thread. Its last GameboyButton::Count slots belong to the
+    // releases of what it holds (queuedDown_ below): a press is taken only with more free than that, so a button it
+    // has pressed always comes back up.
     struct PendingButton {
         std::uint32_t offset;   // samples from block start
         GameboyButton button;   // reinterpreted from the uint8_t opcode
@@ -259,7 +263,15 @@ public:
     };
     rp::FixedQueue<PendingButton> pendingButtons_{rp::kButtonsInput * 16};
     static_assert(rp::kButtonsInput * 16 >= rp::kPressInput, "a Game Boy's press queue must hold both doors");
+    // The state each button's last queued edge leaves it in, one bit a button, all up when the core is built (a
+    // rebuilt core is a new object). pressButton reads it to tell a release that has a press behind it, which needs one
+    // free slot, from every other edge, which needs more than GameboyButton::Count of them. The queue never holds
+    // fewer free slots than the buttons it leaves held: a press is taken only with more than 8 free, so at least 8 are
+    // left and at most 8 buttons can be held, and a held release frees a button for each slot it takes.
+    std::uint8_t                  queuedDown_      = 0;
     std::uint64_t                 pressesAccepted_ = 0;
+    // The edges refused for want of that reserve, counted beside the queue's own drops in inputDropped().
+    std::uint64_t                 pressesRefused_  = 0;
 
     // Default per-press duration (samples) used to space queued transitions.
     // Mirrors the old code's "10 ms at sampleRate" default.
```

- [ ] **Step 6: build and run the tag**

```bash
cmake --build /workspaces/RetroPlug-main/cuttlefish/build --target retroplug-engine-graph-test -j$(nproc)
RETROPLUG_RESOURCES_DIR=/workspaces/resources /workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-engine-graph-test "[input-drops]"
```

Expected:

```
===============================================================================
All tests passed (111 assertions in 4 test cases)
```

Without the fixture tree the same command prints `All tests passed` too, with the GBA sections skipped: `test cases: 4
| 1 passed | 3 skipped`, `assertions: 89 | 89 passed` (a case whose last section was a skip reads as skipped).

- [ ] **Step 7: run the suites this file and this core reach**

```bash
RETROPLUG_RESOURCES_DIR=/workspaces/resources /workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-engine-graph-test
cmake --build /workspaces/RetroPlug-main/cuttlefish/build --target retroplug-sameboy-test -j$(nproc)
RETROPLUG_RESOURCES_DIR=/workspaces/resources /workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-sameboy-test
```

Expected (the whole engine-graph binary holds the `[alloc]` cases, whose Game Boy blocks stage 64 presses each and must
still see no drop; the sameboy binary is the package's own):

```
===============================================================================
All tests passed (1714096 assertions in 339 test cases)

===============================================================================
All tests passed (1152140 assertions in 7 test cases)
```

The engine-graph binary's assertion count moves by a few hundred run to run (a case whose assertions depend on how much
of its work the machine finished); its 339 cases, and the sameboy binary's counts, do not.

- [ ] **Step 8: commit**

```bash
git -C /workspaces/RetroPlug-main/cuttlefish add -- \
  packages/sameboy/native/src/system/sameboy/SameBoySystem.hpp \
  packages/sameboy/native/src/system/sameboy/SameBoySystem.cpp \
  packages/native/test/engine-graph/SystemNode.test.cpp
git -C /workspaces/RetroPlug-main/cuttlefish commit -F - <<'MSG'
fix(kb1): keep a Game Boy's press queue room for its releases, and bound the button byte

A Game Boy's edges cannot coalesce (each is meant to be seen in turn, 10 ms apart), so a flood that outruns the queue
dropped its tail, and the tail could be a release: the button stayed held until a later release. The queue now keeps its
last 8 slots, one per button, for the releases of what it holds. A release of a button still held needs one free slot;
a press, or a release of a button already up, needs more than the console's 8. So a press leaves at least 8 slots, at
most 8 buttons are held, and a queued press's release always fits. What the reserve refuses is counted into
inputDropped() with the queue's own drops, and below the last 8 slots nothing changes, so the [alloc] case's zero Game
Boy drops hold as they were.

The same function now bounds the button byte. SameBoy checks a key's index with an assert alone, so in a release build a
byte of 8 or more (a script node's output, the SDK, a hand-edited document) wrote past gb->keys[0][] and
key_bounce_timing[]. Such a byte drives nothing, as a Mesen console answers for one outside its table, and takes no
slot.

peekPendingPress and pendingPresses are the read seams the cases read the queue and the reserve through.
MSG
```

---

## Task 3: the job pool deletes a render's snapshots, and the UI hands them over

`startSystemRender` snapshots a system's battery and, for a cart whose working song lives outside it, its state, as
`<configDir>/.render-src-<key>.<ext>`, and deletes them in the job's `onSettled`. An instance that ends first never hears
its job settle (the native pool abandons a queued job and cancels a running one at teardown) and a crash leaves them
too. So the files become the pool's:

- `EngineJobRequest` and `RpcEngineJobRequest` gain `discardFiles`, a list of paths, empty by default;
- a `JobFiles` owner on the job deletes each path in its destructor, so every road that drops a job unrun (a refusal at
  `enqueue`, a cancel while queued, `~OfflineJobs` clearing the queue) deletes them with it;
- a job that ran discards them on its worker once its body has returned and before its completion is queued, which is
  the promise `RenderJobs.start`'s comment makes: by the time TS hears a job settled its files are gone;
- TS's `engineRun(kind, spec, opts)` takes `discardFiles`, puts it on the wire request, and `startSystemRender` passes
  its snapshot paths instead of deleting them. `RenderJobs.start` gains one optional parameter for the handover, read
  after `spec` has run (it is the spec function that writes the files). Its `onSettled` contract is unchanged, and the
  UI deletes only when nothing was submitted, i.e. when its spec function threw: an `engineRun` that threw is not that
  case, because a refusal from the pool was a job it built and dropped (its files went with it), and one that never
  reached native at all leaves a file the startup sweep of Task 4 takes.

**Files** (line numbers are the files at `99e1c04d5`):

- Modify: `packages/native/src/host/engine/OfflineTypes.hpp:124-131` (`EngineJobRequest`).
- Modify: `packages/native/src/host/rpc/BackendTypes.hpp:209-215` (`RpcEngineJobRequest`).
- Modify: `packages/native/src/host/rpc/EngineRpcService.cpp:299-307` (the request it builds for the engine).
- Modify: `packages/native/src/host/engine/OfflineJobs.hpp:175` (after `EngineJobBody`: the `JobFiles` class) and
  `:245-252` (the `Job`'s new member).
- Modify: `packages/native/src/host/engine/OfflineJobs.cpp:1-13` (the includes), `:25-26` (`JobFiles`' definitions), and
  `:191-198` (`submitEngine`), `:335-336` (the worker, after the body).
- Modify: `packages/retroplug/src/offline.ts:138-150` (`EngineRunOptions`), `:171-175` (`EngineWireRequest`), `:314-322`
  (`engine()`).
- Modify: `packages/retroplug/src/renderJobs.ts:52-56` (`start`'s signature and doc), `:69-73` (its `engineRun` call).
- Modify: `packages/retroplug/ui/host/render.ts:68-96` (the snapshot block, its comment, and the `renders.start` call).
- Test: `packages/native/test/engine-graph/EngineJobs.test.cpp:845-848` (the new case, before the console-record case),
  and `packages/retroplug/test/render/ui-seam.test.ts:120-200`, `packages/retroplug/test/render/render-jobs.test.ts:151`,
  `packages/retroplug/test/offline/offline.test.ts:179` (its imports at `:18-32`).

**Interfaces:**

- Consumes: `rp::host::EngineJobRequest`, `OfflineJobs::submitEngine(EngineJobRequest)` and the pool's `Job` (whose
  worker runs the body); TS's `EngineRunOptions`, `EngineWireRequest`, `RenderJobs.start` and `jobHandle`; and
  `std::filesystem::remove(path, error_code)`.
- Produces: `rp::host::JobFiles` (a class in `OfflineJobs.hpp`: `take(std::vector<std::string>)`, `discard()`, a
  destructor that discards), `EngineJobRequest::discardFiles` / `RpcEngineJobRequest::discardFiles`, TS's
  `EngineRunOptions.discardFiles`, and `RenderJobs.start(systemId, spec, onSettled?, discardFiles?: () => string[])`.
  Task 4 does not use them; the UI's `startSystemRender` is their one caller.

### Steps

- [ ] **Step 1: write the failing native case**

```patch
diff --git a/packages/native/test/engine-graph/EngineJobs.test.cpp b/packages/native/test/engine-graph/EngineJobs.test.cpp
index 2fdf5ae..e4074e3 100644
--- a/packages/native/test/engine-graph/EngineJobs.test.cpp
+++ b/packages/native/test/engine-graph/EngineJobs.test.cpp
@@ -842,6 +842,134 @@ TEST_CASE("an engine job runs in the default floating-point mode whatever its su
     CHECK(kept.load(std::memory_order_acquire));
 }
 
+TEST_CASE("an engine job deletes the files its submission owns, however it ends",
+          "[engine-graph][engine-jobs]") {
+    // A render's snapshot is a file of the job's own in the config directory every process and instance shares, so it
+    // may not outlive the job by even the span between the job's end and its completion being drained: a worker
+    // deletes the files its request lists before it publishes the completion, and a job dropped unrun (refused,
+    // cancelled while queued, cleared off the pool at teardown) deletes them with itself. A file the submission does
+    // not list, a caller's own `.sav` or a finished WAV beside them, is never touched.
+    TempDir    dir;
+    const auto make = [&dir](const std::string& name) {
+        const std::string path = dir.file(name);
+        std::ofstream(path) << "a snapshot";
+        REQUIRE(fs::exists(path));
+        return path;
+    };
+    SECTION("done") {
+        const std::string listed = make("done.sav");
+        const std::string kept = make("done.wav");
+        const EngineJobBody body = [](const EngineJobRequest&, const EngineJobHooks&) {
+            return EngineJobOutcome{"done", "", {}};
+        };
+        OfflineJobs jobs({}, 2, body);
+        const auto  s = jobs.submitEngine({"render", "{}", {listed}});
+        REQUIRE(s.id != 0);
+        const Seen seen = collect(jobs, {s.id});
+        REQUIRE(seen.done.count(s.id) == 1);
+        CHECK(seen.done.at(s.id).status == "done");
+        CHECK_FALSE(fs::exists(listed));  // gone before the completion was queued, which is when TS hears of it
+        CHECK(fs::exists(kept));
+    }
+    SECTION("error") {
+        const std::string listed = make("error.sav");
+        const EngineJobBody body = [](const EngineJobRequest&, const EngineJobHooks&) -> EngineJobOutcome {
+            throw std::runtime_error("the render failed");  // the pool turns a throw into an error's status
+        };
+        OfflineJobs jobs({}, 2, body);
+        const auto  s = jobs.submitEngine({"render", "{}", {listed}});
+        REQUIRE(s.id != 0);
+        const Seen seen = collect(jobs, {s.id});
+        REQUIRE(seen.done.count(s.id) == 1);
+        CHECK(seen.done.at(s.id).status == "error");
+        CHECK_FALSE(fs::exists(listed));
+    }
+    SECTION("cancelled while running") {
+        std::atomic<bool> started{false};
+        const std::string listed = make("running.sav");
+        const EngineJobBody body = [&](const EngineJobRequest&, const EngineJobHooks& hooks) {
+            started.store(true, std::memory_order_release);
+            while (!hooks.cancelled()) std::this_thread::yield();
+            return EngineJobOutcome{"cancelled", "", {}};
+        };
+        OfflineJobs jobs({}, 2, body);
+        const auto  s = jobs.submitEngine({"render", "{}", {listed}});
+        REQUIRE(s.id != 0);
+        REQUIRE(waitFor(started));
+        CHECK(jobs.cancel(s.id));
+        const Seen seen = collect(jobs, {s.id});
+        REQUIRE(seen.done.count(s.id) == 1);
+        CHECK(seen.done.at(s.id).status == "cancelled");
+        CHECK_FALSE(fs::exists(listed));
+    }
+    SECTION("cancelled while queued, behind a long job on a pool of one") {
+        std::atomic<bool>   running{false};
+        std::atomic<bool>   release{false};
+        const std::string first = make("first.sav");
+        const std::string second = make("second.sav");
+        const EngineJobBody body = [&](const EngineJobRequest&, const EngineJobHooks& hooks) {
+            // The first job holds the pool's one engine worker; the second waits behind it with nothing run.
+            if (!release.load(std::memory_order_acquire)) {
+                running.store(true, std::memory_order_release);
+                while (!release.load(std::memory_order_acquire) && !hooks.cancelled()) std::this_thread::yield();
+            }
+            return EngineJobOutcome{"done", "", {}};
+        };
+        OfflineJobs jobs({}, 1, body);
+        const auto  a = jobs.submitEngine({"render", "{}", {first}});
+        REQUIRE(a.id != 0);
+        REQUIRE(waitFor(running));
+        const auto b = jobs.submitEngine({"render", "{}", {second}});
+        REQUIRE(b.id != 0);
+        REQUIRE(fs::exists(second));   // queued, so its files are still there for the run to boot
+        CHECK(jobs.cancel(b.id));
+        CHECK_FALSE(fs::exists(second));  // the queued job went with its files, unrun
+        CHECK(fs::exists(first));         // and the one running still owns its own
+        release.store(true, std::memory_order_release);
+        const Seen seen = collect(jobs, {a.id, b.id});
+        CHECK(seen.done.at(b.id).status == "cancelled");
+        CHECK(seen.done.at(a.id).status == "done");
+        CHECK_FALSE(fs::exists(first));
+    }
+    SECTION("refused at submit") {
+        const std::string listed = make("refused.sav");
+        OfflineJobs       jobs({}, 2);
+        for (std::size_t i = 0; i < OfflineJobs::kMaxJobs; ++i) {
+            const auto filled = jobs.submit(echoRequest());
+            REQUIRE(filled.id != 0);
+        }
+        // Nothing may be drained here: a job finished and not yet drained is outstanding too, which is the bound's
+        // point, so the pool is full whether its workers ran or not.
+        const auto refused = jobs.submitEngine({"render", "{}", {listed}});
+        CHECK(refused.id == 0);
+        CHECK(refused.error ==
+              "busy: " + std::to_string(OfflineJobs::kMaxJobs) + " offline jobs are outstanding");
+        CHECK_FALSE(fs::exists(listed));  // the pool built the job, refused it, and dropped its files with it
+    }
+    SECTION("abandoned at teardown") {
+        std::atomic<bool>   running{false};
+        const std::string first = make("teardown-running.sav");
+        const std::string queued = make("teardown-queued.sav");
+        const EngineJobBody body = [&](const EngineJobRequest&, const EngineJobHooks& hooks) {
+            running.store(true, std::memory_order_release);
+            while (!hooks.cancelled()) std::this_thread::yield();  // the pool's teardown sets the cancel
+            return EngineJobOutcome{"cancelled", "", {}};
+        };
+        {
+            OfflineJobs jobs({}, 1, body);  // one engine worker: the second job waits behind the first
+            REQUIRE(jobs.submitEngine({"render", "{}", {first}}).id != 0);
+            REQUIRE(waitFor(running));
+            REQUIRE(jobs.submitEngine({"render", "{}", {queued}}).id != 0);
+            CHECK(fs::exists(first));
+            CHECK(fs::exists(queued));
+        }
+        // The pool went with both: the queued job was dropped unrun and the running one ended cancelled, and neither
+        // left a file behind on either road.
+        CHECK_FALSE(fs::exists(first));
+        CHECK_FALSE(fs::exists(queued));
+    }
+}
+
 TEST_CASE("an engine job's console record keeps its bounds between takes, and counts what it drops",
           "[engine-graph][engine-jobs]") {
     EngineJobRecord record;
```

- [ ] **Step 2: build it and watch it fail to compile**

```bash
cmake --build /workspaces/RetroPlug-main/cuttlefish/build --target retroplug-engine-graph-test -j$(nproc)
```

Expected: the request has two members, so an aggregate with three fails to convert:

```
/workspaces/RetroPlug-main/cuttlefish/packages/native/test/engine-graph/EngineJobs.test.cpp:866:42: error: cannot convert '<brace-enclosed initializer list>' to 'rp::host::EngineJobRequest'
/workspaces/RetroPlug-main/cuttlefish/packages/native/test/engine-graph/EngineJobs.test.cpp:868:34: error: no matching function for call to 'collect(rp::host::OfflineJobs&, <brace-enclosed initializer list>)'
```

- [ ] **Step 3: the native half**

The request members, the RPC's, and the pool's owner:

```patch
diff --git a/packages/native/src/host/engine/OfflineJobs.cpp b/packages/native/src/host/engine/OfflineJobs.cpp
index 303a812..26f2611 100644
--- a/packages/native/src/host/engine/OfflineJobs.cpp
+++ b/packages/native/src/host/engine/OfflineJobs.cpp
@@ -7,6 +7,7 @@
 #include <cstdio>
 #include <cstring>
 #include <exception>
+#include <filesystem>
 #include <limits>
 #include <string_view>
 #include <system_error>
@@ -24,6 +25,24 @@
 
 namespace rp::host {
 
+JobFiles::~JobFiles() { discard(); }
+
+void JobFiles::take(std::vector<std::string> paths) {
+    discard();
+    paths_ = std::move(paths);
+}
+
+void JobFiles::discard() {
+    // The error_code overload: a file that is already gone (the job's own render may have written and finished it, and
+    // two instances could name one path) or that cannot be removed leaves nothing to report to anyone, and removal on
+    // a worker thread may not throw into the pool.
+    for (const std::string& path : paths_) {
+        std::error_code ec;
+        std::filesystem::remove(path, ec);
+    }
+    paths_.clear();
+}
+
 namespace {
 
 bool positiveRate(double rate) { return rate > 0.0 && std::isfinite(rate); }
@@ -195,6 +214,9 @@ OfflineSubmit OfflineJobs::submitEngine(EngineJobRequest request) {
     auto job = std::make_unique<Job>();
     job->kind = Kind::Engine;
     job->engine = std::move(request);
+    // The files are the job's from here on, and the request's list is empty after this, so one destructor removes them.
+    // A kind refused above never had the job, so it leaves the list alone.
+    job->discard.take(std::move(job->engine.discardFiles));
     return enqueue(std::move(job));
 }
 
@@ -334,6 +356,10 @@ void OfflineJobs::workerLoop() {
         EngineJobCompletion engineDone;
         if (job->kind == Kind::Graph) done = run(*job);
         else engineDone = runEngine(*job);
+        // The job has let go of its own files before anything can hear that it ended: a render's snapshot is in the
+        // config directory every instance shares, and the next instance may boot from it the moment the next drain
+        // reports the job done. This is a graph job's no-op.
+        job->discard.discard();
         lock.lock();
         running_.erase(std::find(running_.begin(), running_.end(), job.get()));
         (job->kind == Kind::Graph ? runningGraph_ : runningEngine_)--;
diff --git a/packages/native/src/host/engine/OfflineJobs.hpp b/packages/native/src/host/engine/OfflineJobs.hpp
index 36befca..19068ca 100644
--- a/packages/native/src/host/engine/OfflineJobs.hpp
+++ b/packages/native/src/host/engine/OfflineJobs.hpp
@@ -174,6 +174,27 @@ struct EngineJobOutcome {
 // timeline.
 using EngineJobBody = std::function<EngineJobOutcome(const EngineJobRequest&, const EngineJobHooks&)>;
 
+// The files an engine job's submission owns (EngineJobRequest::discardFiles), deleted by this object's destructor: a
+// job that never ran leaves nothing behind, whichever road drops it (a refusal at enqueue, a cancel while it waits in
+// the queue, the pool's own teardown clearing it). A job that ran empties its list itself, on its worker and before its
+// completion is queued (OfflineJobs::workerLoop), so its files are gone by the time a drain reports the job ended.
+// Removal never throws and never fails a job: a file already gone, or one that cannot be removed, is passed over.
+class JobFiles {
+public:
+    JobFiles() = default;
+    ~JobFiles();
+    JobFiles(const JobFiles&) = delete;
+    JobFiles& operator=(const JobFiles&) = delete;
+
+    // Take `paths` as this job's files, deleting whatever it held before, so two lists never name one file.
+    void take(std::vector<std::string> paths);
+    // Delete the files held now, leaving none. Idempotent.
+    void discard();
+
+private:
+    std::vector<std::string> paths_;
+};
+
 class OfflineJobs {
 public:
     static constexpr std::size_t   kMaxJobs = 64;        // outstanding: queued, running, or finished and not drained
@@ -247,6 +268,7 @@ private:
         Kind              kind = Kind::Graph;
         OfflineRequest    request;  // a graph job's
         EngineJobRequest  engine;   // an engine job's
+        JobFiles          discard;  // an engine job's own files (empty for a graph job, and once the job has run)
         std::atomic<bool> cancelled{false};
         EngineJobRecord   record;   // an engine job's
     };
diff --git a/packages/native/src/host/engine/OfflineTypes.hpp b/packages/native/src/host/engine/OfflineTypes.hpp
index 9830093..4f21e6b 100644
--- a/packages/native/src/host/engine/OfflineTypes.hpp
+++ b/packages/native/src/host/engine/OfflineTypes.hpp
@@ -124,6 +124,12 @@ struct OfflineCompletion {
 struct EngineJobRequest {
     std::string kind;
     std::string spec;
+    // Files the submission owns, which the pool deletes however the job ends: on the job's worker once its body has
+    // returned and before its completion is queued, and with the job itself on a road that drops it unrun (a refusal,
+    // a cancel while queued, the pool's teardown). Empty for a caller with none, so a user's own `--sav` or `--state`
+    // is never in it. A UI render lists the snapshots it wrote into the config directory, which every process and
+    // instance shares.
+    std::vector<std::string> discardFiles;
 };
 
 // One console write a job's script made (console.log, console.warn and the rest), without the newline the console
diff --git a/packages/native/src/host/rpc/BackendTypes.hpp b/packages/native/src/host/rpc/BackendTypes.hpp
index 2300fdb..e69bdcc 100644
--- a/packages/native/src/host/rpc/BackendTypes.hpp
+++ b/packages/native/src/host/rpc/BackendTypes.hpp
@@ -210,8 +210,9 @@ struct RpcOfflineCompletion {
 // run on a worker of the same pool with a runtime and an Engine of its own; the one kind is "render", whose `spec` is
 // the render job JSON (a RenderOpts the render worker reads). Its answer is a graph job's, RpcOfflineSubmitResult.
 struct RpcEngineJobRequest {
-    std::string kind;
-    std::string spec;
+    std::string              kind;
+    std::string              spec;
+    std::vector<std::string> discardFiles;  // files the pool deletes however the job ends (EngineJobRequest's)
 };
 // One console write a job's script made, without its newline, and its stream: "stdout" or "stderr".
 struct RpcEngineJobLine {
diff --git a/packages/native/src/host/rpc/EngineRpcService.cpp b/packages/native/src/host/rpc/EngineRpcService.cpp
index 1f8a2ac..f15088d 100644
--- a/packages/native/src/host/rpc/EngineRpcService.cpp
+++ b/packages/native/src/host/rpc/EngineRpcService.cpp
@@ -298,8 +298,8 @@ RpcOfflineSubmitResult EngineRpcService::submitOfflineJob(RpcOfflineRequest requ
 
 RpcOfflineSubmitResult EngineRpcService::submitEngineJob(RpcEngineJobRequest request) {
     RpcOfflineSubmitResult  out{std::nullopt, std::nullopt};
-    rp::host::OfflineSubmit submitted =
-        engine_.submitEngineJob({std::move(request.kind), std::move(request.spec)});
+    rp::host::OfflineSubmit submitted = engine_.submitEngineJob(
+        {std::move(request.kind), std::move(request.spec), std::move(request.discardFiles)});
     if (submitted.id != 0) out.id = submitted.id;
     else out.error = std::move(submitted.error);
     return out;
```

- [ ] **Step 4: build and run the native cases**

```bash
cmake --build /workspaces/RetroPlug-main/cuttlefish/build --target retroplug-engine-graph-test -j$(nproc)
/workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-engine-graph-test "an engine job deletes the files*"
RETROPLUG_RESOURCES_DIR=/workspaces/resources /workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-engine-graph-test "[engine-jobs]"
```

Expected (the new case's six sections run as one case, so Catch2 reports the assertions of all of them):

```
===============================================================================
All tests passed (108 assertions in 1 test case)

===============================================================================
All tests passed (1912 assertions in 22 test cases)
```

The tag's case count is what to hold: the engine-graph binary's assertion count moves a little run to run.

- [ ] **Step 5: write the failing TypeScript cases**

```patch
diff --git a/packages/retroplug/test/offline/offline.test.ts b/packages/retroplug/test/offline/offline.test.ts
index 8053615..6e07cf7 100644
--- a/packages/retroplug/test/offline/offline.test.ts
+++ b/packages/retroplug/test/offline/offline.test.ts
@@ -26,6 +26,7 @@ import {
   type EngineWireCompletion,
   type EngineWireProgress,
   type EngineWireRequest,
+  type OfflinePort,
   type OfflineRunRequest,
   type OfflineSubmitResult,
   type OfflineWireCompletion,
@@ -174,6 +175,42 @@ test("the mock backend rejects every offline job: only a native host runs graphs
   expect(be.pumpJobs()).toBe(0);
 });
 
+test("an engine job's discard files ride its submission, empty when it lists none", async () => {
+  // The pool deletes what a submission lists (whatever road the job takes), so the option crosses on the wire request
+  // rather than being remembered here: one request, one list, and a caller with none sends an empty one.
+  const requests: EngineWireRequest[] = [];
+  let finish = false;
+  const port: OfflinePort = {
+    submitOfflineJob: () => ({ id: null, error: "this test submits no graph job" }),
+    submitEngineJob: (request) => {
+      requests.push(request);
+      return { id: requests.length, error: null };
+    },
+    cancelOfflineJob: () => true,
+    drainOfflineJobs: () => ({
+      completions: [],
+      inFlight: finish ? 0 : requests.length,
+      engineCompletions: finish
+        ? requests.map((_, i) => ({
+            id: i + 1, renderedMs: 0, lines: [], droppedLines: 0, status: "done", message: "", outputs: [],
+          }))
+        : [],
+      engineProgress: [],
+    }),
+  };
+  const jobs = new OfflineJobs(port);
+  const listed = jobs.engine("render", { rom: "/x.gb", out: "/x.wav" },
+    { discardFiles: ["/config/.render-src-a.sav"] });
+  const plain = jobs.engine("render", { rom: "/x.gb", out: "/x.wav" });
+  // Both end first, so no queue of this case's outlives a failed assertion (a live queue is a runtime's, and there is
+  // one at a time).
+  finish = true;
+  jobs.pump();
+  await Promise.all([listed, plain]);
+  expect(requests.map((r) => r.discardFiles)).toEqual([["/config/.render-src-a.sav"], []]);
+  expect(liveJobCount()).toBe(0);
+});
+
 test("the mock backend rejects every engine job, and its handles have nothing to cancel", async () => {
   const be = new MockBackend("/cfg");
   const handle = be.engineRun("render", { rom: "/x.gb", out: "/x.wav" });
diff --git a/packages/retroplug/test/render/render-jobs.test.ts b/packages/retroplug/test/render/render-jobs.test.ts
index 49e7801..f89b087 100644
--- a/packages/retroplug/test/render/render-jobs.test.ts
+++ b/packages/retroplug/test/render/render-jobs.test.ts
@@ -145,6 +145,28 @@ test("a finished job whose system has no tile is dropped as it settles, and when
   expect(renders.list().map((j) => [j.systemId, j.state])).toEqual([[2, "error"]]);
 });
 
+test("a job's discard files are read once its spec has run, and ride the submission", () => {
+  // The UI's spec function is what writes a job's snapshot files, so the list of them is read after it returns and
+  // handed to the engine as the submission's own (EngineRunOptions.discardFiles): the pool deletes them however the
+  // job ends, which is why nothing here deletes them. A job that lists none hands over no list, which the wire request
+  // carries as an empty one (offline.test.ts).
+  const { backend, jobs } = scripted();
+  const renders = new RenderJobs(backend);
+  const written: string[] = [];
+  const id = renders.start(
+    1,
+    (key) => (written.push(`/config/.render-src-${key}.sav`), { sav: written[0] }),
+    undefined,
+    () => [...written],
+  );
+  expect(written.length).toBe(1);
+  expect(jobs[0].options.discardFiles).toEqual([written[0]]);
+  const plain = renders.start(2, {});
+  expect(jobs[1].options.discardFiles).toBe(undefined);
+  renders.dismiss(id);
+  renders.dismiss(plain);
+});
+
 test("a job's spec can name its files by the job's key, and onSettled runs once however the job ends", async () => {
   const { backend, jobs } = scripted();
   const renders = new RenderJobs(backend);
diff --git a/packages/retroplug/test/render/ui-seam.test.ts b/packages/retroplug/test/render/ui-seam.test.ts
index 011e93f..45087b8 100644
--- a/packages/retroplug/test/render/ui-seam.test.ts
+++ b/packages/retroplug/test/render/ui-seam.test.ts
@@ -118,26 +118,37 @@ test("startSystemRender: no on-disk ROM → no-op (returns null, nothing started
 });
 
 /** A RenderJobs over a backend whose engineRun holds every job until the test settles it, as an engine queues a render
- *  behind the worker budget: `specs` lists what each submission carried, `settle(i)` ends job i. */
-function queuedRenders(): { renders: RenderJobs; specs: Captured["spec"][]; settle(i: number): void } {
+ *  behind the worker budget: `specs` lists what each submission carried, `discards` the files each one handed the
+ *  engine's pool (which deletes them), `settle(i)` ends job i. */
+function queuedRenders(): {
+  renders: RenderJobs;
+  specs: Captured["spec"][];
+  discards: (string[] | undefined)[];
+  settle(i: number): void;
+} {
   const specs: Captured["spec"][] = [];
+  const discards: (string[] | undefined)[] = [];
   const resolvers: ((r: EngineRunResult) => void)[] = [];
   const renders = new RenderJobs({
-    engineRun: (_kind, spec) => {
+    engineRun: (_kind, spec, options) => {
       specs.push(spec as Captured["spec"]);
+      discards.push(options?.discardFiles);
       return jobHandle(new Promise<EngineRunResult>((res) => resolvers.push(res)), () => true);
     },
   });
   const settle = (i: number) => resolvers[i]({ status: "done", message: "", outputs: [], renderedMs: 1 });
-  return { renders, specs, settle };
+  return { renders, specs, discards, settle };
 }
 
 const later = (): Promise<void> => new Promise((r) => void Promise.resolve().then(() => r()));
 
-test("startSystemRender: two queued renders of one system boot their own snapshots, gone once settled", async () => {
+test("startSystemRender: two queued renders of one system boot their own snapshots, handed to the pool", async () => {
   // A render reads its snapshot when a worker boots it, which can be minutes after the submission when renders queue
   // behind the engine's budget. A render, an edit and a second render must leave the first booting the state it was
-  // started on: each snapshot is a file of its own, and it goes when its job settles.
+  // started on: each snapshot is a file of its own, named by the job's key, and each job's own list of them rides its
+  // submission (EngineRunOptions.discardFiles), which the engine's pool deletes however the job ends. The UI deletes
+  // nothing on settle, so the files are still there when one settles (a worker removes them before it queues the
+  // completion, which the C++ [engine-jobs] cases hold).
   const backend = new MockBackend("/config");
   const q = queuedRenders();
   const sys = sysView({ id: 1, battery: true });
@@ -150,14 +161,16 @@ test("startSystemRender: two queued renders of one system boot their own snapsho
   expect(first.sav !== undefined && second.sav !== undefined && first.sav !== second.sav).toBeTruthy();
   expect(Array.from(backend.readFile(first.sav!) ?? [])).toEqual([1, 1, 1]);
   expect(Array.from(backend.readFile(second.sav!) ?? [])).toEqual([2, 2, 2]);
+  expect(q.discards).toEqual([[first.sav], [second.sav]]); // each render's own, and only its own
 
   q.settle(0);
   await later();
-  expect(backend.fileExists(first.sav!)).toBe(false);
-  expect(backend.fileExists(second.sav!)).toBe(true); // still queued: its snapshot stays
+  expect(backend.fileExists(first.sav!)).toBe(true); // still the pool's, not this UI's, to delete
+  expect(backend.fileExists(second.sav!)).toBe(true); // and still queued
   q.settle(1);
   await later();
-  expect(backend.paths().filter((p) => p.startsWith("/config/"))).toEqual([]);
+  // Neither was deleted by this side: deleting is the pool's, and it is what the list it was handed is for.
+  expect(backend.paths().filter((p) => p.startsWith("/config/"))).toEqual([first.sav!, second.sav!].sort());
 });
 
 test("startSystemRender: a cart whose song is outside the battery snapshots its state too, of its own", async () => {
@@ -176,27 +189,39 @@ test("startSystemRender: a cart whose song is outside the battery snapshots its
   const [first, second] = q.specs;
   expect(first.state !== undefined && second.state !== undefined && first.state !== second.state).toBeTruthy();
   expect(first.sav !== second.sav).toBeTruthy();
+  // Both files of each job are its submission's: the battery and the savestate that captures the working song.
+  expect(q.discards).toEqual([[first.sav, first.state], [second.sav, second.state]]);
   q.settle(0);
   q.settle(1);
   await later();
-  expect(backend.paths().filter((p) => p.startsWith("/config/"))).toEqual([]);
+  expect(backend.paths().filter((p) => p.startsWith("/config/"))).toEqual([first.sav!, first.state!, second.sav!, second.state!].sort());
 });
 
-test("startSystemRender: a snapshot that cannot be written refuses the render, not boot the on-disk .sav", async () => {
-  class FullDisk extends MockBackend {
-    override writeFileAtomic(): boolean {
-      return false;
+test("startSystemRender: a snapshot that cannot be written refuses the render, and the one it did write goes",
+  async () => {
+    // The savestate's write fails after the battery's succeeded: the spec function throws, so nothing is submitted and
+    // no list reaches the pool, which makes the file it did write the UI's to delete (the only road `onSettled` deletes
+    // on). The render is still listed as an error rather than booting the on-disk .sav.
+    class HalfDisk extends MockBackend {
+      override writeFileAtomic(path: string, bytes: Uint8Array): boolean {
+        return path.endsWith(".ss0") ? false : super.writeFileAtomic(path, bytes);
+      }
+      override readState(): Uint8Array | null {
+        return new Uint8Array([9]);
+      }
     }
-  }
-  const backend = new FullDisk("/config");
-  backend.setSram(1, new Uint8Array([1]));
-  const q = queuedRenders();
-  const id = startSystemRender(backend, q.renders, sysView({ id: 1, battery: true }), { split: "mix" }, "/out/a.wav");
-  expect(q.specs.length).toBe(0); // nothing submitted
-  const job = q.renders.list().find((j) => j.id === id);
-  expect(job?.state).toBe("error");
-  expect(job!.message.includes("could not snapshot")).toBeTruthy();
-});
+    const backend = new HalfDisk("/config");
+    backend.setSram(4, new Uint8Array([1]));
+    const q = queuedRenders();
+    const sys = sysView({ id: 4, romPath: "/roms/smsggdj.sms", battery: true, roles: [{ kind: "sms-sync", config: {} }] });
+    const id = startSystemRender(backend, q.renders, sys, { split: "mix" }, "/out/a.wav");
+    expect(q.specs.length).toBe(0); // nothing submitted
+    expect(q.discards.length).toBe(0); // and so nothing handed over
+    const job = q.renders.list().find((j) => j.id === id);
+    expect(job?.state).toBe("error");
+    expect(job!.message.includes("could not snapshot")).toBeTruthy();
+    expect(backend.paths().filter((p) => p.startsWith("/config/"))).toEqual([]); // the sav it wrote went with it
+  });
 
 test("renderBaseName: song name → SAV basename → ROM stem, in priority order", () => {
   const backend = new MockBackend("/config");
```

- [ ] **Step 6: run them and watch them fail**

```bash
pnpm -C /workspaces/RetroPlug-main/cuttlefish test render/ui-seam
pnpm -C /workspaces/RetroPlug-main/cuttlefish test render/render-jobs
pnpm -C /workspaces/RetroPlug-main/cuttlefish test offline/offline
```

Expected: each file fails on the handover, three different ways:

```
not ok 6 - startSystemRender: two queued renders of one system boot their own snapshots, handed to the pool
  expected [["/config/.render-src-mwcs29nn-1knk0af-1.sav"],["/config/.render-src-mwcs29nn-1knk0af-2.sav"]], got [null,null]
not ok 7 - startSystemRender: a cart whose song is outside the battery snapshots its state too, of its own
# 17 passed, 0 skipped, 2 failed

not ok 8 - a job's discard files are read once its spec has run, and ride the submission
  expected ["/config/.render-src-mwcsfa1k-xlmdt2-1.sav"], got undefined
# 8 passed, 0 skipped, 1 failed

not ok 2 - an engine job's discard files ride its submission, empty when it lists none
  expected [["/config/.render-src-a.sav"],[]], got [null,null]
# 19 passed, 0 skipped, 1 failed
```

(The keys in the paths are the job tokens the tests draw at run time, so theirs differ.)

- [ ] **Step 7: the TypeScript half**

`offline.ts`'s option and wire member, `RenderJobs.start`'s handover, and `startSystemRender`'s:

```patch
diff --git a/packages/retroplug/src/offline.ts b/packages/retroplug/src/offline.ts
index bee4d1e..3a816be 100644
--- a/packages/retroplug/src/offline.ts
+++ b/packages/retroplug/src/offline.ts
@@ -144,6 +144,12 @@ export interface EngineRunOptions {
    *  (EngineJobRecord: 256 lines, 64 KiB); writes past it were dropped, and arrive as one stderr line saying how
    *  many. */
   onLog?: (line: EngineLogLine) => void;
+  /** Files the submission owns, which the engine's pool deletes however the job ends: on the job's worker once its
+   *  body has returned and before its completion is queued, and with the job itself on a road that drops it unrun (a
+   *  refusal, a cancel while it waits in the queue, the pool's teardown). So they are gone by the time the job settles,
+   *  and a caller does not delete them itself. Never a user's own file (a `--sav` handed to a render): a UI render
+   *  lists the snapshots it wrote into the config directory, which every instance shares. */
+  discardFiles?: string[];
 }
 
 /** An engine job's console lines on this runtime's own: stderr to console.error, the rest to console.log. */
@@ -171,6 +177,9 @@ export function jobHandle<T>(promise: Promise<T>, cancel: () => boolean): JobHan
 export interface EngineWireRequest {
   kind: string;
   spec: string;
+  /** Always present, an empty list when the caller lists none: native reads the request without tolerating a missing
+   *  field (rfl), and an empty list deletes nothing. */
+  discardFiles: string[];
 }
 export interface EngineWireProgress {
   id: number;
@@ -316,7 +325,11 @@ export class OfflineJobs implements JobQueue {
     const promise = new Promise<EngineRunResult>(
       (resolve, reject) => (waiter = { kind: "engine", resolve, reject, id: null, options, renderedMs: 0 }),
     );
-    const wire = { kind, spec: typeof spec === "string" ? spec : JSON.stringify(spec) };
+    const wire: EngineWireRequest = {
+      kind,
+      spec: typeof spec === "string" ? spec : JSON.stringify(spec),
+      discardFiles: [...(options.discardFiles ?? [])],
+    };
     return jobHandle(promise, this.start({ kind: "engine", wire }, waiter!));
   }
 
diff --git a/packages/retroplug/src/renderJobs.ts b/packages/retroplug/src/renderJobs.ts
index 1039c55..73427bf 100644
--- a/packages/retroplug/src/renderJobs.ts
+++ b/packages/retroplug/src/renderJobs.ts
@@ -46,10 +46,18 @@ export class RenderJobs {
   /** Start rendering `spec` (a render job spec) for system `systemId`, and return the job's id here. A render the
    *  engine refuses, or that cannot be submitted, is listed as an error. `spec` can be a function of the job's key, a
    *  name no other job of any list shares (the caller's files for this job carry it), and a throw from it is listed as
-   *  the job's error with nothing submitted. `onSettled` runs once the job has ended however it ended (listed or
-   *  dismissed, a throw from `spec` included): the engine has let go of the job's files by then, or can
-   *  no longer report on the job (a broken link to it). */
-  start(systemId: number, spec: object | ((key: string) => object), onSettled?: () => void): number {
+   *  the job's error with nothing submitted. `discardFiles`, when given, answers the files the submission owns once
+   *  `spec` has run (the caller's snapshots, which `spec` writes): they ride the record to the engine
+   *  (EngineRunOptions.discardFiles), whose pool deletes them however the job ends, so the caller deletes nothing on
+   *  settle. `onSettled` runs once the job has ended however it ended (listed or dismissed, a throw from `spec`
+   *  included): the engine has let go of the job's files by then, or can no longer report on the job (a broken link to
+   *  it). */
+  start(
+    systemId: number,
+    spec: object | ((key: string) => object),
+    onSettled?: () => void,
+    discardFiles?: () => string[],
+  ): number {
     const status: RenderJobStatus = { id: this.nextId++, systemId, state: "rendering", renderedMs: 0, message: "" };
     const entry: Entry = { status, handle: null };
     this.entries.set(status.id, entry);
@@ -71,6 +79,7 @@ export class RenderJobs {
       entry.handle = this.backend.engineRun("render", body, {
         onProgress: (renderedMs) => update({ renderedMs }),
         onLog: echoJobLog,
+        discardFiles: discardFiles?.(),
       });
     } catch (e) {
       update({ state: "error", message: (e as Error)?.message ?? String(e) });
diff --git a/packages/retroplug/ui/host/render.ts b/packages/retroplug/ui/host/render.ts
index b381a7b..1f64140 100644
--- a/packages/retroplug/ui/host/render.ts
+++ b/packages/retroplug/ui/host/render.ts
@@ -63,10 +63,16 @@ export function startSystemRender(
   // the render worker's own Engine loads by path. The worker reads them when it boots the render, which is when a
   // worker is free: renders queue behind the engine's budget, so that can be minutes on, after further edits and
   // renders. So each render's files are its own, named by the job's key (unique across instances, which share the
-  // config directory), and deleted once the job has settled. A snapshot that cannot be written refuses the render,
-  // listed as an error, rather than leave the field out and boot the sibling .sav on disk instead of what you hear.
+  // config directory), and they go with the job: the submission lists them as its discardFiles, which the engine's
+  // pool deletes however the job ends, an instance that ends or a crash before the job settled included (the startup
+  // sweep is the backstop for what a crash leaves). A snapshot that cannot be written refuses the render, listed as an
+  // error, rather than leave the field out and boot the sibling .sav on disk instead of what you hear.
   const dir = backend.configDir();
   const snapshots: string[] = [];
+  // Whether the spec reached the submission. The files are the engine's pool's from the moment they ride it (the job's
+  // discardFiles, which the pool deletes however the job ends), so the one road left to this module is a spec that
+  // threw: a snapshot whose write failed leaves the ones before it written, and nothing was submitted to delete them.
+  let built = false;
   const prepare = (key: string): object => {
     const snapshot = (ext: string, bytes: Uint8Array): string => {
       const path = `${dir}/.render-src-${key}.${ext}`;
@@ -91,9 +97,17 @@ export function startSystemRender(
       const state = backend.readState(sys.id);
       if (state) spec.state = snapshot("ss0", state);
     }
+    built = true;
     return spec;
   };
-  return renders.start(sys.id, prepare, () => snapshots.forEach((p) => backend.deleteFile(p)));
+  return renders.start(
+    sys.id,
+    prepare,
+    () => {
+      if (!built) snapshots.forEach((p) => backend.deleteFile(p));
+    },
+    () => snapshots,
+  );
 }
 
 /** The base filename a render should default to, in priority order: the loaded tracker cart's WORKING song
```

- [ ] **Step 8: run the three files again**

```bash
pnpm -C /workspaces/RetroPlug-main/cuttlefish test render/ui-seam
pnpm -C /workspaces/RetroPlug-main/cuttlefish test render/render-jobs
pnpm -C /workspaces/RetroPlug-main/cuttlefish test offline/offline
```

Expected:

```
# 19 passed, 0 skipped, 0 failed
# 1 test file(s) passed (jobs=16)

# 9 passed, 0 skipped, 0 failed
# 1 test file(s) passed (jobs=16)

# 20 passed, 0 skipped, 0 failed
# 1 test file(s) passed (jobs=16)
```

- [ ] **Step 9: run the wider suites this task reaches, including the real RPC**

The render cases, then the real backend end to end: `engine-run` drives `backend.engineRun` over the msgpack RPC into
the pool, which is what proves native reads a request carrying `discardFiles`. The engine-graph binary's bundles embed
these TS sources, so rebuild before the native run.

```bash
pnpm -C /workspaces/RetroPlug-main/cuttlefish test render
pnpm -C /workspaces/RetroPlug-main/cuttlefish test offline
cmake --build /workspaces/RetroPlug-main/cuttlefish/build -j$(nproc)
pnpm -C /workspaces/RetroPlug-main/cuttlefish test:native engine-run
pnpm -C /workspaces/RetroPlug-main/cuttlefish typecheck
```

Expected:

```
# 9 test file(s) passed (jobs=16)

# 20 passed, 0 skipped, 0 failed
# 1 test file(s) passed (jobs=16)
```

`engine-run`'s summary line, the one case that needs the LSDj song fixture skipped on a run without it:

```
# 12 passed, 1 skipped, 0 failed
#   1 case(s) SKIPPED in 1 file(s): engine-run(1)
```

and `typecheck` ends with `packages/retroplug typecheck: Done` for the desktop tree and the plugin product's.

- [ ] **Step 10: commit**

```bash
git -C /workspaces/RetroPlug-main/cuttlefish add -- \
  packages/native/src/host/engine/OfflineTypes.hpp \
  packages/native/src/host/rpc/BackendTypes.hpp \
  packages/native/src/host/rpc/EngineRpcService.cpp \
  packages/native/src/host/engine/OfflineJobs.hpp \
  packages/native/src/host/engine/OfflineJobs.cpp \
  packages/native/test/engine-graph/EngineJobs.test.cpp \
  packages/retroplug/src/offline.ts \
  packages/retroplug/src/renderJobs.ts \
  packages/retroplug/ui/host/render.ts \
  packages/retroplug/test/offline/offline.test.ts \
  packages/retroplug/test/render/render-jobs.test.ts \
  packages/retroplug/test/render/ui-seam.test.ts
git -C /workspaces/RetroPlug-main/cuttlefish commit -F - <<'MSG'
fix(kb1): let the job pool delete a render's snapshots

A UI render copies the live system's battery (and, for a cart whose working song lives outside it, a savestate) into
files of the job's own in the config directory, and the UI deleted them when the job settled. An instance that ends
first never hears its job settle: the native pool abandons a queued job and cancels a running one at teardown, with no
TS left to listen. A crash leaves them too. The directory is shared by every process and instance, so nothing else may
delete another instance's snapshot either.

So the files travel with the submission. EngineJobRequest and RpcEngineJobRequest gain discardFiles (empty by default,
so a caller's own --sav or --state is never in it), and the pool holds them in a JobFiles whose destructor removes each
path: every road that drops a job unrun deletes them with it. A job that ran discards them on its worker once its body
has returned and before its completion is queued, so by the time TS hears the job settled they are gone, which is the
promise RenderJobs.start's comment already made.

startSystemRender passes its snapshot paths and no longer deletes on settle; the one road it still deletes on is a spec
function that threw (a snapshot whose write failed), where nothing was submitted. What survives all that is a crash's
leftovers, which the startup sweep after this change takes.
MSG
```

---

## Task 4: `fileAgeMs`, and a startup sweep for what a crash leaves

A crash leaves a render's snapshots behind, with nothing of that job left to delete them. The sweep that takes them
belongs with the config defaults every startup writes, so it runs in `composeAppStores` beside
`bindings.ensureDefaults()`, which is the one sequence the plugin's control plane, the standalones and the CLI all
compose. It needs an age, and an age must be measured on the file clock itself: C++20's `clock_cast` between the file
clock and the system clock is not implemented the same way by libc++ and MSVC, so `HostRpcService::fileAgeMs` answers
`file_time_type::clock::now() - last_write_time(path)` in milliseconds, and nothing when there is no file to age.

`sweepRenderSnapshots(backend, configDir)` deletes every `.render-src-*` older than 24 hours and skips what it cannot
age or delete, plus anything with a trailing `/` (`listDir` marks subdirectories that way). No live job's snapshot is a
day old: a queued render waits behind the engine's worker budget, which is seconds to minutes per render ahead of it.

**Files** (line numbers are the files at `99e1c04d5`):

- Modify: `packages/native/src/host/rpc/HostRpcService.hpp:40-41` (after `deleteFile`) and
  `packages/native/src/host/rpc/HostRpcService.cpp:204-208` (after `deleteFile`; its includes at `:1-12`).
- Modify: `packages/native/src/host/rpc/BackendRpcRegistration.hpp:14-26` (the host facet, its count, the new method),
  `:135` (the union's count).
- Create: `packages/retroplug/src/renderSnapshotSweep.ts`.
- Modify: `packages/retroplug/src/backend.ts:63-67` (the method, after `deleteFile`) and `:477` (`HostBackend`).
- Modify: `packages/retroplug/src/realBackend.ts:82-84`.
- Modify: `packages/retroplug/testing/mockBackend.ts:116-117` (the ages map), `:194-197` (a setter beside `unseed`),
  `:311-315` (`fileAgeMs` after `deleteFile`).
- Modify: `packages/retroplug/src/appStores.ts:27-28` (the import), `:79-82` (the call).
- Modify: `packages/retroplug/ui/host/render.ts:24-27` (the import) and `:84` (the prefix the path is built from).
- Test: `packages/native/test/host/HostRpcWatching.test.cpp` (its includes at `:15-24`, a case at the file's end),
  `packages/retroplug/test/render/snapshot-sweep.test.ts` (new), and `packages/retroplug/test-native/fs-config.test.ts`
  (a case at the file's end).

**Interfaces:**

- Consumes: `HostRpcService` (its `deleteFile` and `listDir`, beside the new `fileAgeMs`), TS's `HostBackend`,
  `MockBackend` and `composeAppStores`, and `RENDER_SNAPSHOT_PREFIX`, which Task 3's `startSystemRender` now builds its
  paths from.
- Produces: `std::optional<double> HostRpcService::fileAgeMs(std::string path)`; TS's `Backend.fileAgeMs(path): number |
  null`; `RENDER_SNAPSHOT_MAX_AGE_MS`, `RENDER_SNAPSHOT_PREFIX` and `sweepRenderSnapshots(backend, dir): number` in
  `src/renderSnapshotSweep.ts`.

### Steps

- [ ] **Step 1: write the failing native case**

```patch
diff --git a/packages/native/test/host/HostRpcWatching.test.cpp b/packages/native/test/host/HostRpcWatching.test.cpp
index c0c9b95..4b3baaa 100644
--- a/packages/native/test/host/HostRpcWatching.test.cpp
+++ b/packages/native/test/host/HostRpcWatching.test.cpp
@@ -9,6 +9,8 @@
 
 #include <catch2/catch_test_macros.hpp>
 
+#include <filesystem>
+#include <fstream>
 #include <memory>
 #include <string>
 #include <utility>
@@ -91,3 +93,31 @@ TEST_CASE("HostRpcService forwards to the one watcher it was handed", "[watcher]
     CHECK(secondCalls->romSets == 0);
     CHECK(secondCalls->drains == 0);
 }
+
+TEST_CASE("fileAgeMs measures a file's age on the file clock, and has none where there is no file", "[host]") {
+    // What a UI render's snapshot is aged by when a crash leaves it: the sweep TS runs at startup (spec/07) deletes
+    // the ones a day old, and asks here rather than comparing a system clock, which would need C++20's clock_cast
+    // between the file clock and the system clock, an operation libc++ and MSVC do not agree on.
+    namespace fs = std::filesystem;
+    std::error_code    ec;
+    const fs::path     dir = fs::temp_directory_path(ec) / "rp-hostrpc-file-age";
+    fs::remove_all(dir, ec);
+    REQUIRE(fs::create_directories(dir, ec));
+    REQUIRE_FALSE(ec);
+
+    const fs::path file = dir / "written.txt";
+    {
+        std::ofstream out(file);
+        out << "a snapshot";
+    }
+    HostRpcService svc;
+    const auto     age = svc.fileAgeMs(file.string());
+    REQUIRE(age.has_value());
+    CHECK(*age >= 0.0);          // a file written now is not from the future
+    CHECK(*age < 60000.0);       // and is younger than the minute this case could take
+    // A directory ages like a file (the sweep skips them by the trailing '/' listDir marks them with, not here).
+    CHECK(svc.fileAgeMs(dir.string()).has_value());
+    // Nothing to age is nothing, not zero: a missing file is not one the sweep may delete.
+    CHECK_FALSE(svc.fileAgeMs((dir / "absent.txt").string()).has_value());
+    fs::remove_all(dir, ec);
+}
```

- [ ] **Step 2: build it and watch it fail to compile**

```bash
cmake --build /workspaces/RetroPlug-main/cuttlefish/build --target retroplug-audio-test -j$(nproc)
```

Expected:

```
/workspaces/RetroPlug-main/cuttlefish/packages/native/test/host/HostRpcWatching.test.cpp:114:30: error: 'class HostRpcService' has no member named 'fileAgeMs'
/workspaces/RetroPlug-main/cuttlefish/packages/native/test/host/HostRpcWatching.test.cpp:119:15: error: 'class HostRpcService' has no member named 'fileAgeMs'
```

- [ ] **Step 3: the native half**

```patch
diff --git a/packages/native/src/host/rpc/BackendRpcRegistration.hpp b/packages/native/src/host/rpc/BackendRpcRegistration.hpp
index 4b3dfc5..c3e8d7d 100644
--- a/packages/native/src/host/rpc/BackendRpcRegistration.hpp
+++ b/packages/native/src/host/rpc/BackendRpcRegistration.hpp
@@ -11,7 +11,7 @@
 #include "host/rpc/EngineRpcService.hpp"
 #include "host/rpc/HostRpcService.hpp"
 
-// --- host: filesystem / config / codec (19) ---
+// --- host: filesystem / config / codec (20) ---
 template <class Server>
 void registerHostRpc(Server& s, HostRpcService& h) {
     s.template addMethod<&HostRpcService::readFile>(h);
@@ -24,6 +24,7 @@ void registerHostRpc(Server& s, HostRpcService& h) {
     s.template addMethod<&HostRpcService::syncFile>(h);
     s.template addMethod<&HostRpcService::listDir>(h);
     s.template addMethod<&HostRpcService::deleteFile>(h);
+    s.template addMethod<&HostRpcService::fileAgeMs>(h);
     s.template addMethod<&HostRpcService::drainChangedPaths>(h);
     s.template addMethod<&HostRpcService::setWatchedRoms>(h);
     s.template addMethod<&HostRpcService::canonicalize>(h);
@@ -132,7 +133,7 @@ void registerDriverRpc(Server& s, AudioDriverRpcService& d) {
     s.template addMethod<&AudioDriverRpcService::drainReleased>(d);
 }
 
-// The full union (91 methods) + discovery - every facet mounted. Hosts that expose the whole surface
+// The full union (92 methods) + discovery - every facet mounted. Hosts that expose the whole surface
 // (the CLI + test host) use this; scoped hosts call the individual register functions they're allowed to.
 template <class Server>
 void registerAllBackendRpc(Server& s, HostRpcService& host, EngineRpcService& engine,
diff --git a/packages/native/src/host/rpc/HostRpcService.cpp b/packages/native/src/host/rpc/HostRpcService.cpp
index d93c9c2..21c2654 100644
--- a/packages/native/src/host/rpc/HostRpcService.cpp
+++ b/packages/native/src/host/rpc/HostRpcService.cpp
@@ -1,6 +1,7 @@
 #include "host/rpc/HostRpcService.hpp"
 
 #include <atomic>
+#include <chrono>
 #include <cstddef>
 #include <cstdlib>
 #include <filesystem>
@@ -207,6 +208,14 @@ bool HostRpcService::deleteFile(std::string path) {
     return removed && !ec;
 }
 
+std::optional<double> HostRpcService::fileAgeMs(std::string path) {
+    std::error_code ec;
+    const fs::file_time_type written = fs::last_write_time(path, ec);
+    if (ec) return std::nullopt;
+    const std::chrono::duration<double, std::milli> age = fs::file_time_type::clock::now() - written;
+    return age.count();
+}
+
 std::vector<std::string> HostRpcService::drainChangedPaths() {
     return watcher_ ? watcher_->drainChangedPaths() : std::vector<std::string>{};
 }
diff --git a/packages/native/src/host/rpc/HostRpcService.hpp b/packages/native/src/host/rpc/HostRpcService.hpp
index 766005b..25d7bb2 100644
--- a/packages/native/src/host/rpc/HostRpcService.hpp
+++ b/packages/native/src/host/rpc/HostRpcService.hpp
@@ -39,6 +39,11 @@ public:
     bool syncFile(std::string path);
     std::vector<std::string> listDir(std::string dir);
     bool deleteFile(std::string path);
+    // How long ago `path` was written, in milliseconds, or nothing when it is missing or cannot be read. Measured on
+    // the file clock itself (file_time_type::clock::now() minus last_write_time): all a caller wants is an age, and
+    // C++20's clock_cast between the file clock and the system clock is not implemented the same way by libc++ and
+    // MSVC. The UI's startup sweep ages a crashed render's snapshots with it (spec/07).
+    std::optional<double> fileAgeMs(std::string path);
     // The native side of the file watcher (spec/07). drainChangedPaths pulls the paths the efsw watcher
     // saw since the last call (empty when watching isn't enabled); setWatchedRoms tells it which ROM
     // files to watch (their parent dirs), recomputed by TS whenever the systems list changes.
```

- [ ] **Step 4: build and run the case**

```bash
cmake --build /workspaces/RetroPlug-main/cuttlefish/build --target retroplug-audio-test -j$(nproc)
/workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-audio-test "[host]"
```

Expected:

```
===============================================================================
All tests passed (7 assertions in 1 test case)
```

- [ ] **Step 5: write the sweep's case (a new file)**

```patch
diff --git a/packages/retroplug/test/render/snapshot-sweep.test.ts b/packages/retroplug/test/render/snapshot-sweep.test.ts
new file mode 100644
index 0000000..2940b81
--- /dev/null
+++ b/packages/retroplug/test/render/snapshot-sweep.test.ts
@@ -0,0 +1,69 @@
+// The startup sweep for what a crash leaves of a render's snapshots (src/renderSnapshotSweep.ts,
+// docs/known-bugs-batch-1-design.md section 4.2). A live job's snapshot is its own and the engine's pool deletes it
+// (EngineRunOptions.discardFiles), so the sweep may only take what no job can own any more: a file of the snapshot
+// name a day old, past any render's wait behind the engine's worker budget. Everything else in the config directory
+// is left alone, and so is a snapshot it cannot age or cannot delete.
+import { test, expect } from "@retroplug/testing";
+import { MockBackend } from "../../testing/mockBackend";
+import { composeAppStores } from "../../src/appStores";
+import { RENDER_SNAPSHOT_MAX_AGE_MS, sweepRenderSnapshots } from "../../src/renderSnapshotSweep";
+
+const DAY = 24 * 60 * 60 * 1000;
+
+/** A config directory with one file of every kind the sweep meets, and the backend that holds them. */
+function configDir(): MockBackend {
+  const be = new MockBackend("/config");
+  be.seed("/config/.render-src-crashed.sav", "x"); // a process that died with a render queued: gone
+  be.setFileAge("/config/.render-src-crashed.sav", DAY + 1);
+  be.seed("/config/.render-src-live.sav", "x"); // a render submitted a minute ago: kept
+  be.setFileAge("/config/.render-src-live.sav", 60 * 1000);
+  be.seed("/config/.render-src-edge.ss0", "x"); // exactly a day old: the bound is "older than", so kept
+  be.setFileAge("/config/.render-src-edge.ss0", DAY);
+  be.seed("/config/config.json", "{}"); // not a snapshot, however old
+  be.setFileAge("/config/config.json", 30 * DAY);
+  be.seed("/config/.render-src-dir/inside.sav", "x"); // a directory wears the name (listDir marks it "…/"): kept
+  return be;
+}
+
+test("the sweep deletes a render snapshot a day old, and nothing a live render could still own", () => {
+  const be = configDir();
+  expect(sweepRenderSnapshots(be, "/config")).toBe(1);
+  expect(be.fileExists("/config/.render-src-crashed.sav")).toBe(false);
+  expect(be.fileExists("/config/.render-src-live.sav")).toBe(true);
+  expect(be.fileExists("/config/.render-src-edge.ss0")).toBe(true);
+  expect(be.fileExists("/config/config.json")).toBe(true);
+  expect(be.fileExists("/config/.render-src-dir/inside.sav")).toBe(true);
+  // A second sweep has nothing left to do, and its bound is the day the first one used.
+  expect(sweepRenderSnapshots(be, "/config")).toBe(0);
+  expect(RENDER_SNAPSHOT_MAX_AGE_MS).toBe(DAY);
+});
+
+test("the sweep passes over a snapshot it cannot age and one it cannot delete", () => {
+  class Awkward extends MockBackend {
+    override fileAgeMs(path: string): number | null {
+      return path.endsWith("-unreadable.sav") ? null : super.fileAgeMs(path);
+    }
+    override deleteFile(path: string): boolean {
+      return path.endsWith("-stuck.sav") ? false : super.deleteFile(path);
+    }
+  }
+  const be = new Awkward("/config");
+  be.seed("/config/.render-src-unreadable.sav", "x");
+  be.setFileAge("/config/.render-src-unreadable.sav", 9 * DAY);
+  be.seed("/config/.render-src-stuck.sav", "x");
+  be.setFileAge("/config/.render-src-stuck.sav", 9 * DAY);
+  // One that can be deleted beside them, so a sweep that gave up at the first refusal shows.
+  be.seed("/config/.render-src-gone.sav", "x");
+  be.setFileAge("/config/.render-src-gone.sav", 9 * DAY);
+  expect(sweepRenderSnapshots(be, "/config")).toBe(1);
+  expect(be.fileExists("/config/.render-src-unreadable.sav")).toBe(true);
+  expect(be.fileExists("/config/.render-src-stuck.sav")).toBe(true);
+  expect(be.fileExists("/config/.render-src-gone.sav")).toBe(false);
+});
+
+test("composing the store graph is what sweeps, so every host that starts does it once", () => {
+  const be = configDir();
+  composeAppStores({ backend: be });
+  expect(be.fileExists("/config/.render-src-crashed.sav")).toBe(false);
+  expect(be.fileExists("/config/.render-src-live.sav")).toBe(true);
+});
```

- [ ] **Step 6: run it and watch it fail**

```bash
pnpm -C /workspaces/RetroPlug-main/cuttlefish test render/snapshot-sweep
```

Expected: the module does not exist yet, so the bundle fails:

```
packages/retroplug/test/render/snapshot-sweep.test.ts:9:65: ERROR: Could not resolve "../../src/renderSnapshotSweep"
# BUILD FAILED: render/snapshot-sweep
# 1/1 test file(s) FAILED: render/snapshot-sweep
```

- [ ] **Step 7: the sweep, the backend's method, and the UI's prefix**

`realBackend` puts the method on the wire here; the mapping of an absent answer is its own step below, because the case
that pins it is.

```patch
diff --git a/packages/retroplug/src/appStores.ts b/packages/retroplug/src/appStores.ts
index b2ac841..f21a3a5 100644
--- a/packages/retroplug/src/appStores.ts
+++ b/packages/retroplug/src/appStores.ts
@@ -25,6 +25,7 @@ import { BindingsStore } from "./bindingsStore";
 import { ProjectStore, type ProjectContributions } from "./projectStore";
 import { FileSelection } from "./fileSelection";
 import { RenderJobs } from "./renderJobs";
+import { sweepRenderSnapshots } from "./renderSnapshotSweep";
 
 /** The categories of store change a consumer can observe. `project` = project settings / dirty;
  *  `systems` = the systems-structure list; the rest name their store. */
@@ -77,6 +78,9 @@ export function composeAppStores({
 
   const bindings = new BindingsStore(backend, userConfig, () => notify("bindings"));
   bindings.ensureDefaults();
+  // What a crash left of a render's snapshots goes with the defaults this startup writes: a live one is its job's and
+  // the engine's pool deletes it, so only what no job can own any more (a day old) is swept, and only this once.
+  sweepRenderSnapshots(backend, backend.configDir());
 
   const project = new ProjectStore(backend, recent, registry, contributions);
   const renders = new RenderJobs(backend, () => project.systems.view().map((s) => s.id));
diff --git a/packages/retroplug/src/backend.ts b/packages/retroplug/src/backend.ts
index ec2f6ed..e965725 100644
--- a/packages/retroplug/src/backend.ts
+++ b/packages/retroplug/src/backend.ts
@@ -65,6 +65,11 @@ export interface Backend {
   /** Delete the file at `path`. Returns false when it isn't present. */
   deleteFile(path: string): boolean;
 
+  /** How long ago `path` was written, in milliseconds, or `null` when it is missing or cannot be read. Native measures
+   *  it on the file clock itself, so the age is the file's, not a difference between two clocks. The startup sweep of
+   *  render snapshots (src/renderSnapshotSweep.ts) decides what a crash left by it. */
+  fileAgeMs(path: string): number | null;
+
   /** The watched-file paths that changed since the last drain (empty when nothing did).
    *  Native owns the watching — efsw over the config dir + `bindings/` (recursive) and over
    *  each watched ROM's parent directory — and collects the changed paths; TS pulls + reacts
@@ -474,7 +479,7 @@ export interface Backend {
 export type HostBackend = Pick<
   Backend,
   | "readFile" | "writeFile" | "writeFileAtomic" | "appendFile" | "writeFileAt" | "fileExists" | "rename" | "syncFile"
-  | "listDir" | "deleteFile"
+  | "listDir" | "deleteFile" | "fileAgeMs"
   | "drainChangedPaths" | "setWatchedRoms" | "canonicalize" | "readFilePrefix" | "configDir" | "version"
   | "zip" | "unzip" | "pngEncode" | "pngDecode" | "openFileBrowser"
 >;
diff --git a/packages/retroplug/src/realBackend.ts b/packages/retroplug/src/realBackend.ts
index 28cfd91..b05f074 100644
--- a/packages/retroplug/src/realBackend.ts
+++ b/packages/retroplug/src/realBackend.ts
@@ -81,6 +81,7 @@ export function createHostClient(): HostBackend {
     syncFile: (path) => call("syncFile", path) as boolean,
     listDir: (dir) => call("listDir", dir) as string[],
     deleteFile: (path) => call("deleteFile", path) as boolean,
+    fileAgeMs: (path) => call("fileAgeMs", path) as number | null,
     drainChangedPaths: () => call("drainChangedPaths") as string[],
     setWatchedRoms: (paths) => void call("setWatchedRoms", paths),
     canonicalize: (path) => call("canonicalize", path) as string,
diff --git a/packages/retroplug/src/renderSnapshotSweep.ts b/packages/retroplug/src/renderSnapshotSweep.ts
new file mode 100644
index 0000000..a522596
--- /dev/null
+++ b/packages/retroplug/src/renderSnapshotSweep.ts
@@ -0,0 +1,35 @@
+// The render snapshots a crash leaves (docs/known-bugs-batch-1-design.md section 4.2). A UI render writes the system's
+// battery and, where the working song is not in it, a savestate into the config directory as
+// `.render-src-<key>.<ext>` (ui/host/render.ts), and the submission lists them as its own (EngineRunOptions
+// discardFiles), so the engine's pool deletes them however the job ends. A process that dies leaves them: nothing of
+// that job is left to delete anything, and the config directory has no owner between processes (the standalones, every
+// plugin instance and the CLI share it). So the store graph sweeps it once at startup, and takes only what no live job
+// can own any more.
+
+import type { HostBackend } from "./backend";
+
+/** How old a render snapshot must be before the sweep deletes it. A queued render waits behind the engine's worker
+ *  budget, which is seconds to minutes per render ahead of it, so a file of a live job's is never a day old. */
+export const RENDER_SNAPSHOT_MAX_AGE_MS = 24 * 60 * 60 * 1000;
+
+/** The name prefix a render's snapshots carry, built by startSystemRender from the job's key. */
+export const RENDER_SNAPSHOT_PREFIX = ".render-src-";
+
+/** Delete every render snapshot in `dir` older than RENDER_SNAPSHOT_MAX_AGE_MS, and answer how many went. A name that
+ *  is not a snapshot, a subdirectory (listDir marks one with a trailing '/'), a file that cannot be aged and one that
+ *  cannot be deleted are all left alone: this is a backstop for what a crash leaves, never a cleanup that may take
+ *  what a live render is about to boot. */
+export function sweepRenderSnapshots(
+  backend: Pick<HostBackend, "listDir" | "fileAgeMs" | "deleteFile">,
+  dir: string,
+): number {
+  let removed = 0;
+  for (const name of backend.listDir(dir)) {
+    if (!name.startsWith(RENDER_SNAPSHOT_PREFIX) || name.endsWith("/")) continue;
+    const path = `${dir}/${name}`;
+    const age = backend.fileAgeMs(path);
+    if (age === null || age <= RENDER_SNAPSHOT_MAX_AGE_MS) continue;
+    if (backend.deleteFile(path)) removed++;
+  }
+  return removed;
+}
diff --git a/packages/retroplug/testing/mockBackend.ts b/packages/retroplug/testing/mockBackend.ts
index 0563325..79d434a 100644
--- a/packages/retroplug/testing/mockBackend.ts
+++ b/packages/retroplug/testing/mockBackend.ts
@@ -114,6 +114,7 @@ export interface MockResource {
 
 export class MockBackend implements Backend {
   private files = new Map<string, Uint8Array>();
+  private fileAges = new Map<string, number>();
   private dir: string;
 
   /** Names of Backend methods called, in order — lets tests assert side-effects
@@ -195,6 +196,11 @@ export class MockBackend implements Backend {
     this.files.delete(this.canonicalize(path));
   }
 
+  /** Set how many milliseconds old a file on the fake disk reads (a file a test seeds is 0 ms old). */
+  setFileAge(path: string, ageMs: number): void {
+    this.fileAges.set(this.canonicalize(path), ageMs);
+  }
+
   /** Drive a system's live SRAM content (what readSram returns), overriding the
    *  deterministic default — lets a test model SRAM changing between flushes. */
   setRam(id: number, bytes: Uint8Array): void {
@@ -313,6 +319,13 @@ export class MockBackend implements Backend {
     return this.files.delete(this.canonicalize(path));
   }
 
+  fileAgeMs(path: string): number | null {
+    this.log.push("fileAgeMs");
+    const key = this.canonicalize(path);
+    if (!this.files.has(key)) return null;
+    return this.fileAges.get(key) ?? 0; // a file written by a test is a moment old, as one this process wrote is
+  }
+
   drainChangedPaths(): string[] {
     this.log.push("drainChangedPaths");
     const out = this.changedPaths;
diff --git a/packages/retroplug/ui/host/render.ts b/packages/retroplug/ui/host/render.ts
index 1f64140..991e4d6 100644
--- a/packages/retroplug/ui/host/render.ts
+++ b/packages/retroplug/ui/host/render.ts
@@ -9,6 +9,7 @@ import { resolveSongCatalog } from "../../src/trackerRegistry";
 import { consoleOfPlatform } from "../../src/consoles";
 import { stem } from "../../src/pathUtil";
 import { resolveSavPath } from "../../src/savPaths";
+import { RENDER_SNAPSHOT_PREFIX } from "../../src/renderSnapshotSweep";
 import type { RenderJobs, RenderJobStatus } from "../../src/renderJobs";
 
 export type { RenderJobStatus };
@@ -75,7 +76,7 @@ export function startSystemRender(
   let built = false;
   const prepare = (key: string): object => {
     const snapshot = (ext: string, bytes: Uint8Array): string => {
-      const path = `${dir}/.render-src-${key}.${ext}`;
+      const path = `${dir}/${RENDER_SNAPSHOT_PREFIX}${key}.${ext}`;
       snapshots.push(path);
       if (!backend.writeFileAtomic(path, bytes))
         throw new Error(`render: could not snapshot the system's state to ${path}`);
```

- [ ] **Step 8: run the sweep's cases**

```bash
pnpm -C /workspaces/RetroPlug-main/cuttlefish test render/snapshot-sweep
```

Expected:

```
ok 1 - the sweep deletes a render snapshot a day old, and nothing a live render could still own
ok 2 - the sweep passes over a snapshot it cannot age and one it cannot delete
ok 3 - composing the store graph is what sweeps, so every host that starts does it once
# 3 passed, 0 skipped, 0 failed
```

- [ ] **Step 9: write the case that drives the real backend**

```patch
diff --git a/packages/retroplug/test-native/fs-config.test.ts b/packages/retroplug/test-native/fs-config.test.ts
index 92209ba..1f16043 100644
--- a/packages/retroplug/test-native/fs-config.test.ts
+++ b/packages/retroplug/test-native/fs-config.test.ts
@@ -92,3 +92,15 @@ test("writeFileAtomic: a rewrite replaces the contents and still leaves the dir
   const strays = be.listDir(__CONFIG_DIR__).filter((n) => n.includes(".tmp."));
   expect(strays).toEqual([]);
 });
+
+test("fileAgeMs: over the real backend, a file just written is a moment old and a missing one is nothing", () => {
+  // What the startup sweep of render snapshots decides by (src/renderSnapshotSweep.ts): native ages the file on its
+  // own clock, and a path with no file is no age at all rather than zero, which would make it deletable.
+  const be = createRealBackend();
+  const path = __CONFIG_DIR__ + "/aged.bin";
+  expect(be.writeFileAtomic(path, new Uint8Array([1, 2]))).toBe(true);
+  const age = be.fileAgeMs(path);
+  expect(age !== null).toBe(true);
+  expect(age! >= 0 && age! < 60000).toBe(true);
+  expect(be.fileAgeMs(__CONFIG_DIR__ + "/absent.bin")).toBe(null);
+});
```

- [ ] **Step 10: run it and watch it fail on the null**

```bash
pnpm -C /workspaces/RetroPlug-main/cuttlefish test:native fs-config
```

Expected: a file with no age comes back as `undefined` over the RPC, not `null`, so the test fails (and a TS caller
would read `undefined` where the type says `null`):

```
ok 5 - writeFileAtomic: a rewrite replaces the contents and still leaves the dir clean
not ok 6 - fileAgeMs: over the real backend, a file just written is a moment old and a missing one is nothing
  expected null, got undefined
# 5 passed, 0 skipped, 1 failed
# 1/1 native test file(s) FAILED: fs-config
```

- [ ] **Step 11: map an absent answer to `null`**

The file's other nullable reads do the same (`bytesOrNull`, and `unzip`/`pngDecode`'s `?? null`): an absent
`std::optional` arrives as `null` or as absent, and the Backend's type says `null`.

```patch
diff --git a/packages/retroplug/src/realBackend.ts b/packages/retroplug/src/realBackend.ts
index b05f074..69d35cd 100644
--- a/packages/retroplug/src/realBackend.ts
+++ b/packages/retroplug/src/realBackend.ts
@@ -81,7 +81,7 @@ export function createHostClient(): HostBackend {
     syncFile: (path) => call("syncFile", path) as boolean,
     listDir: (dir) => call("listDir", dir) as string[],
     deleteFile: (path) => call("deleteFile", path) as boolean,
-    fileAgeMs: (path) => call("fileAgeMs", path) as number | null,
+    fileAgeMs: (path) => (call("fileAgeMs", path) as number | null) ?? null, // an absent optional (null or absent)
     drainChangedPaths: () => call("drainChangedPaths") as string[],
     setWatchedRoms: (paths) => void call("setWatchedRoms", paths),
     canonicalize: (path) => call("canonicalize", path) as string,
```

- [ ] **Step 12: run it again**

```bash
pnpm -C /workspaces/RetroPlug-main/cuttlefish test:native fs-config
```

Expected:

```
ok 6 - fileAgeMs: over the real backend, a file just written is a moment old and a missing one is nothing
# 6 passed, 0 skipped, 0 failed
# 1 native test file(s) passed (jobs=16)
```

- [ ] **Step 13: run the suites this task reaches**

`pnpm test` is every pure-TypeScript file, the boundary checks, the source-literals walk and the bundle split, so it is
where a new `src/` module and the UI's import of it are held; `plugin-controlplane` composes the store graph over the
real backend, which runs the sweep for real.

```bash
pnpm -C /workspaces/RetroPlug-main/cuttlefish test
pnpm -C /workspaces/RetroPlug-main/cuttlefish test:native plugin-controlplane
pnpm -C /workspaces/RetroPlug-main/cuttlefish typecheck
pnpm -C /workspaces/RetroPlug-main/cuttlefish check:boundaries
```

Expected:

```
# 238 test file(s) passed (jobs=16)

# 4 passed, 0 skipped, 0 failed
# 1 native test file(s) passed (jobs=16)

check-boundaries: the packages hold their boundaries (0 allowed by EXCEPTIONS, 0 warning(s))
```

and `typecheck`'s `Done` lines as in Task 3's step 9.

- [ ] **Step 14: run the binary the native case belongs to**

```bash
/workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-audio-test
```

Expected: `All tests passed (385 assertions in 17 test cases)`.

- [ ] **Step 15: commit**

```bash
git -C /workspaces/RetroPlug-main/cuttlefish add -- \
  packages/native/src/host/rpc/HostRpcService.hpp \
  packages/native/src/host/rpc/HostRpcService.cpp \
  packages/native/src/host/rpc/BackendRpcRegistration.hpp \
  packages/retroplug/src/renderSnapshotSweep.ts \
  packages/retroplug/src/backend.ts \
  packages/retroplug/src/realBackend.ts \
  packages/retroplug/testing/mockBackend.ts \
  packages/retroplug/src/appStores.ts \
  packages/retroplug/ui/host/render.ts \
  packages/native/test/host/HostRpcWatching.test.cpp \
  packages/retroplug/test/render/snapshot-sweep.test.ts \
  packages/retroplug/test-native/fs-config.test.ts
git -C /workspaces/RetroPlug-main/cuttlefish commit -F - <<'MSG'
feat(kb1): age a render snapshot, and sweep a crash's at startup

The pool deletes a render's snapshots with its job, which covers every road but a crash: nothing of that job survives to
delete anything, and the config directory has no owner between processes. So composeAppStores sweeps it once, beside the
defaults every startup writes, and takes only what no live job can own: a .render-src-* a day old. A queued render waits
behind the engine's worker budget, seconds to minutes per render ahead of it, so a live snapshot is never that old.

HostRpcService::fileAgeMs ages a path on the file clock itself, file_time_type::clock::now() minus last_write_time,
because C++20's clock_cast between the file clock and the system clock is not implemented the same way by libc++ and
MSVC. An absent file is no age rather than a zero one, which would make it deletable: realBackend maps the absent answer
to null, as its other nullable reads do. The mock backend stores an age a test can set, and a subdirectory (which
listDir marks with a trailing '/') is skipped without an age.
MSG
```

---

## Task 5: the docs

The batch's record in [spec/07](../spec/07-remaining-work.md) and the marker count in [AGENTS.md](../AGENTS.md). Nothing
else: the design document's as-built section and execution record are the controller's, written after the batch's
acceptance, and are not this task's.

**Files:**

- Modify: `spec/07-remaining-work.md:43-54` (the "Known bugs, recorded and not fixed" checklist), `:271-274` (C2's sync
  latency entry), `:381-388` (C4's press-queue entry), `:590-593` (D3's emu2413 entry), `:596-603` (D3's snapshot entry),
  `:770-777` (the B3b paragraph that repeats the latency line).
- Modify: `AGENTS.md:388` and `:443` (the Mesen marker count, 45 to 46; NOT `:459`'s "~45 s").

**Interfaces:**

- Consumes: the four commits Tasks 1 to 4 made.
- Produces: nothing code uses.

### Steps

- [ ] **Step 1: the doc changes**

Four checklist items are ticked and one (the button byte) is recorded and ticked, with placeholders where a commit's
hash goes; the long entries say what is now true. The latency report's line needs none: its closure is this document's own
commit, and the reason is in the line itself.

```patch
diff --git a/AGENTS.md b/AGENTS.md
index 436aa8f..c08571c 100644
--- a/AGENTS.md
+++ b/AGENTS.md
@@ -385,7 +385,7 @@ The rules below are the parts that don't fit those.
   reset / stash / commit that, and don't bump the pointer to "absorb" it.** A
   submodule bump that invalidates a patch fails LOUDLY at configure (rather than
   silently dropping what it adds); regenerate the patch if that happens. (Mesen is vendored in-tree, so its fixes are
-  plain source changes, each marked `// RetroPlug:` - 45 files carry one, `Utilities/SimpleLock.{h,cpp}`'s atomic
+  plain source changes, each marked `// RetroPlug:` - 46 files carry one, `Utilities/SimpleLock.{h,cpp}`'s atomic
   holder id and count for cores on several threads among them, and the per-console `#if MESEN_ENABLE_*` guards of
   `deps/mesen/CMakeLists.txt`'s compile-out are the newest batch.)
 - **Don't `rm -rf build` to "fix" CMake** — investigate first. The configured
@@ -440,7 +440,7 @@ The rules below are the parts that don't fit those.
   compiler folds the arm a template parameter picks) and fails at `-O0`: `tools/build-mesen-all-consoles.sh --off`
   builds the library as Debug and refuses an archive that references a C++ symbol no member defines. 288 translation
   units of 426, 19.7 MB of 32.7 MB of archive. The vendored tree is SourMesen/Mesen2 at `b9fa69d` with no `.git` of its
-  own: a merge re-applies the 45 files carrying a `// RetroPlug:` marker (the compile-out guards among them) over the
+  own: a merge re-applies the 46 files carrying a `// RetroPlug:` marker (the compile-out guards among them) over the
   upstream snapshot, then runs BOTH modes of `tools/build-mesen-all-consoles.sh`, `--off` being the one that catches a
   guard the merge dropped (a Release link folds it away).
 - **Mesen LTO is opt-in (`-DRETROPLUG_MESEN_LTO=ON`), and that is deliberate.** It
diff --git a/spec/07-remaining-work.md b/spec/07-remaining-work.md
index bd045d0..4074825 100644
--- a/spec/07-remaining-work.md
+++ b/spec/07-remaining-work.md
@@ -43,12 +43,15 @@ each (a phase name means its "What it leaves" list in section 5).
 **Known bugs, recorded and not fixed**
 - [ ] "Is this LSDj" decided by a role's presence, not its placement; needs `placedRole` (found building B3b)
 - [ ] MI.MAP's predicted playhead runs ahead at a tempo divisor of 2 or more (F3)
-- [ ] `__rp_syncLatencyMs` follows the stored sync mode, not the one MI.MAP forces (C2)
+- [x] `__rp_syncLatencyMs` follows the stored sync mode, not the one MI.MAP forces (C2; unreachable since F5a, closed
+      with kb1: no plugin binary's list carries a controller app to force one)
 - [ ] MI.OUT decodes about half the NoteOns a song plays; not investigated (LSDj's serial-out node)
 - [ ] The DPF JACK standalone's control handoff runs on JACK's thread at a buffer-size or rate change (C5)
-- [ ] A Game Boy's press queue can drop a flood's last release, leaving a button held (C4)
-- [ ] A UI render's snapshot files are left behind when the instance ends first (D3)
-- [ ] emu2413's VRC7 tables: an unsynchronised static flag, a race for two VRC7 carts built at once (D3)
+- [x] A Game Boy's press queue can drop a flood's last release, leaving a button held (C4; kb1, <t2>)
+- [x] A Game Boy button byte of 8 or more writes past SameBoy's key arrays in a release build (kb1, <t2>)
+- [x] A UI render's snapshot files are left behind when the instance ends first (D3; kb1, <t3> and <t4>: the job's pool
+      deletes them with it, and the startup sweep covers a crash)
+- [x] emu2413's VRC7 tables: an unsynchronised static flag, a race for two VRC7 carts built at once (D3; kb1, <t1>)
 
 **Engine**
 - [ ] DAW automation of node params through `setParam` (C2)
@@ -268,8 +271,11 @@ frame, `rp.host.midi-out` merges its inputs by frame, the decoders allocate noth
   commits; the later gain snap's 28 re-recorded cases are its section 10's. What it leaves (its sections 9 and 10):
   - **DAW automation of node params.** The plugin's parameters are not mapped onto `setParam`; params make it
     possible, and it is its own work.
-  - **The plugin's sync latency report** (`__rp_syncLatencyMs`) still follows the stored `lsdj-sync` mode, not the
-    one the MI.MAP controller forces.
+  - **The plugin's sync latency report: closed as unreachable, not fixed.** `__rp_syncLatencyMs` follows the stored
+    `lsdj-sync` mode rather than the one a controller app forces, but since F5a no plugin binary's product list carries
+    `launchpad` or `lsdj-launchpad` (the `plugin` product covers every plugin binary, the DPF JACK standalone
+    included), so no binary that reports a latency to a host can have a controller force a mode. A control surface
+    joining the plugin's list must make the report ask the placed mode.
   - **The construct-time fields and the facts a build reports: closed by C3.** SMS `enableFm`, NES `sdRoot`, the
     FIFO profile and its overrides, the N8 link and its round trip and `channelExportMode` are their system node's
     settings now, and the node builds the core from them; the export mode and a Game Boy's link group come from the
@@ -375,11 +381,15 @@ frame, `rp.host.midi-out` merges its inputs by frame, the decoders allocate noth
     discard are paid for. Not arming a split that does not fit is a CPU-only change (`buildGraph` knows the lane
     budget) and is nobody's bug.
   - **The cores this fork does not run** (PCE, SNES, WS) still copy a frame's snapshot once more per emulated frame.
-  - **A Game Boy's press queue drops the tail of a quiescent flood.** Its 1024 entries hold sixteen blocks of a
+  - **A Game Boy's press queue keeps its last slots for releases.** Its 1024 entries hold sixteen blocks of a
     saturated buttons input, and its presses are scheduled 10 ms apart and carried across blocks, so unlike a Mesen
     console's they cannot coalesce. A session that applies pushes inline between blocks (the CLI, an offline render)
-    can queue more than that; what is past the bound is dropped and counted, and the tail can be a release, leaving
-    its button held. A rule that keeps a release, or headroom reserved for one, is its own change.
+    can queue more than that, so an edge other than the release of a button the queue still holds (a press, or a
+    release of a button already up) is taken only with more than the console's 8 buttons free, i.e. with at most 1016
+    entries queued, and such a release needs one slot, which is never refused: a press leaves 8 slots and at most 8
+    buttons are held, and a release frees a button for the slot it takes. What the reserve refuses is counted in
+    `inputDropped()` with the queue's own drops. A button byte past the console's table (8 or more) drives no key and
+    takes no slot, counted as taken, where SameBoy's own bound is an `assert` a release build drops.
   - **The suite keeps its console-only casts** (`EngineGraph`, `SystemNode`, `HostIo`, `EngineSampleRate` tests read a
     config field, `linkPeers_` or a ring counter); no tap read casts any more.
 - **C5 built: hardware as nodes, and the engine names no device**
@@ -578,14 +588,17 @@ frame, `rp.host.midi-out` merges its inputs by frame, the decoders allocate noth
     `start-truncation`, `risa-autodetect` and `render-guard` call `runRenderJob` on an exact-blocks engine, a path no
     production caller uses; for engine jobs their assertions reach users only through the render baseline's hashes and
     `engine-run`.
-  - **emu2413's VRC7 tables** are a lazily built process static behind a plain flag (the one left; the NES database's
-    and the GBA dispatch tables' flags are acquire/release): two VRC7 carts built at once as the process's first race.
-  - **A UI render's snapshot** (`.render-src-<key>.*` in the config directory) is deleted when its job settles, so an
-    instance or process that ends before the job settles leaves it behind: a closed DAW project or a removed plugin
-    with a render queued or running, or a render finished while the editor was closed and the project then closed
-    (nothing drains then), as well as a crash. Nothing removes them. The cheaper real fix is left: the worker deleting
-    its own snapshot once the system is built (a spec flag only the UI sets), the settle hook then covering only jobs
-    that never booted.
+  - **emu2413's VRC7 tables: fixed with kb1.** They are a process static built once by a function-local `static const
+    bool` (the order the NES database's and the GBA dispatch tables' flags spell out with acquire/release atomics), so
+    two VRC7 carts built at once as the process's first wait for the build rather than writing the tables twice.
+    `MesenShared.test.cpp`'s `[mesen-statics]` case holds it under ThreadSanitizer, which reported the old flag.
+  - **A UI render's snapshot** (`.render-src-<key>.*` in the config directory) belongs to its job's submission
+    (`EngineRunOptions.discardFiles`), so the pool deletes it however the job ends: on the job's worker once the render
+    library has returned and before the completion is queued, so it is gone by the time TS hears the job settled, and
+    with the job itself when it is dropped unrun (a refusal, a cancel while queued, the pool's teardown, which is the
+    instance ending first). A crash is what survives that, and the next startup sweeps it (`sweepRenderSnapshots`: a
+    snapshot a day old, skipping what cannot be aged or deleted, since the config directory has no owner between
+    processes). The UI deletes only a snapshot whose spec function threw, before anything was submitted.
   - **The SameBoy camera half of the statics patch has no red-green test** (no Game Boy Camera ROM exists in the repo or
     the fixtures), and the exact render bytes are held only on `linux-x64-gnu13`. The patch uses GNU `__atomic`
     builtins, which GCC and Clang (clang-cl on Windows) have; the tree has not been built with MSVC since D2.
@@ -766,8 +779,9 @@ frame, `rp.host.midi-out` merges its inputs by frame, the decoders allocate noth
   - **The controller's MI.MAP override changed only the document: closed by C2.** `controllerSyncOverride`
     ([launchpad/ts/control/placer.ts](../packages/launchpad/ts/control/placer.ts), applied in `buildGraph`) runs an LSDj's
     nodes in `midiMap` for a cart reading MidiMap (in `off` for one reading another SYNC), and `serialOutCaptureFor`,
-    beside it since C2, computes the Game Boy's capture from that forced mode, so a forced cart captures nothing. Only
-    `__rp_syncLatencyMs` still follows the stored mode, listed with C2's leftovers above.
+    beside it since C2, computes the Game Boy's capture from that forced mode, so a forced cart captures nothing.
+    `__rp_syncLatencyMs` would report the stored mode, and no plugin binary carries a controller app to force one in
+    (C2's leftover above, closed as unreachable with kb1).
   - A few test-native case names still say "the kernel" (in `dsp-lsdj-midisync`, `dsp-risa-sync`,
     `dsp-sms-sync` and `plugin-controlplane`), where they mean the protocol nodes. Renaming
     a case is harmless but was kept out of B3b, which changed no test's identity it did not have to (the one case
```

- [ ] **Step 2: fill in the four hashes**

The batch's own commits are the last four before this task's:

```bash
git -C /workspaces/RetroPlug-main/cuttlefish log --format='%h %s' -5
```

Expected: this task's commit is not there yet, so the five lines are, newest first, the four below and then
`docs(kb1): design the first known-bugs batch`. Replace each placeholder with the short hash of the commit whose subject
it names:

| Placeholder | Commit subject |
|---|---|
| `<t1>` | `fix(kb1): build emu2413's tables once, from any thread` |
| `<t2>` | `fix(kb1): keep a Game Boy's press queue room for its releases, and bound the button byte` |
| `<t3>` | `fix(kb1): let the job pool delete a render's snapshots` |
| `<t4>` | `feat(kb1): age a render snapshot, and sweep a crash's at startup` |

Then check that none is left:

```bash
grep -n '<t[0-9]>' /workspaces/RetroPlug-main/cuttlefish/spec/07-remaining-work.md
```

Expected: no output (the `grep` exits 1, which is what "nothing left" looks like).

- [ ] **Step 3: check the docs**

```bash
pnpm -C /workspaces/RetroPlug-main/cuttlefish check:docs
```

Expected: the same counts as before this task, the one warning being a plan record's pre-existing dead link:

```
WARN  docs/launchpad-plan.md:299  dead link in a plan record  ../packages/retroplug/ui/screens/menu/midiDevices.ts

check-doc-links: 104 file(s) clean, 1 warning(s)
```

- [ ] **Step 4: check that no em dash came in**

```bash
git -C /workspaces/RetroPlug-main/cuttlefish diff -- spec/07-remaining-work.md AGENTS.md | grep '^+' | grep -c $'\u2014'
```

Expected: `0`.

- [ ] **Step 5: commit**

```bash
git -C /workspaces/RetroPlug-main/cuttlefish add -- spec/07-remaining-work.md AGENTS.md
git -C /workspaces/RetroPlug-main/cuttlefish commit -F - <<'MSG'
docs(kb1): record the first known-bugs batch

spec/07's known-bugs list ticks the four items the batch fixed (emu2413's VRC7 tables, a Game Boy's press queue, a UI
render's snapshots, the latency report's stale mode) and records one it found and closed (the Game Boy's button byte),
each with its commit. The long entries say what is true now: the queue keeps its last slots for releases and bounds the
byte, the snapshot belongs to the job's submission with a startup sweep for a crash's leftovers, emu2413's tables are
built by a block-scope static, and the latency report is closed as unreachable rather than fixed, because since F5a no
plugin binary's product list carries a controller app to force a mode. AGENTS.md's Mesen marker count goes from 45 files
to 46.
MSG
```
