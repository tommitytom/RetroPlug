# LSDj's serial out as its own node: host MIDI at each byte's frame - implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for
> tracking.

**Goal:** LSDj's host MIDI (MI.OUT and master sync) goes out at each byte's own frame, in the block the Game Boy
captured it, instead of at frame 0 of the block after: the two decoders move out of `rp.lsdj.sync` into a new script
node, `rp.lsdj.serial-out`, placed after the Game Boy and reading its `serialOut` on a forward wire; master sync's stop
goes at its 200 ms mark; `rp.host.midi-out` merges its inputs by frame; the decoders emit without allocating; and the
profile's `apu-render` span covers the whole plan.

**Architecture:** Per [docs/lsdj-serial-out-design.md](lsdj-serial-out-design.md). An `lsdj-sync` role places two
nodes, one per direction: `rp.lsdj.sync` (host to Game Boy) before its system as now, and `rp.lsdj.serial-out` (Game
Boy to host) after it, both with the role's config as settings through one shared schema. The protocol-node table
(`graph/protocolNodes.ts`) names the after-node on the before-node's spec, and `buildGraph` places it after the systems.
The pure decoders keep their protocol; their emit carries each message's frame and fills reused arrays. The host's
MIDI-out node merges its inputs by frame, ties in input order, which is the old concatenation while every frame is 0.

**Tech stack:** TypeScript on txiki (`pnpm test` mock tier, `pnpm test:native` real host), the script host's bare
QuickJS, C++20 with Catch2 (`retroplug-engine-graph-test`), the graph layer of Phases A to B3b.

**Provenance:** every code block below was built and run before this plan was written, on the scratch branch
`proto/lsdj-serial-out` in this worktree, on top of the design commit 1d33aa37: five commits, one per task (Task 6 is
the acceptance run and commits nothing), with GCC 13.3 on linux-x64 (a Ryzen 9 9950X). A code block that creates a
file is that file as the prototype commit left it; one that changes files is the prototype's diff for them, so
`git apply` it from the step's text (or apply it hunk by hunk with the Edit tool). The files a command writes are not
shown: the role goldens and the three parity baselines Task 4 re-records, which the step then checks equal to the
prototype's. The one-off checker that proves what the re-record moved was never committed; it is quoted from the
prototype's scratch directory. Every expected pass, test count and benchmark figure is what the prototype's runs
printed; where a failing-test step has no recorded run, the expected failure is what the missing code produces. When
this plan was generated it was replayed in a scratch clone of 1d33aa37, every block written or applied in order, the
generated files taken from the prototype, and each task committed with the command given: each commit's tree came out
exactly as the prototype's, and each commit message as its. On those replayed trees every mock-tier step was run as
well (`pnpm test` and its filters, red and green): each red step failed as it says and each green step printed the
count quoted. The native steps' counts are the prototype's. The prototype commits, by task:
- Task 1: `493a756e` feat(host-io): rp.host.midi-out merges its inputs by frame, ties in input order
- Task 2: `fd51838b` feat(profile): apu-render spans the whole plan, the script nodes' spans inside it
- Task 3: `06cf3965` feat(lsdj): the MI.OUT and master-sync decoders emit each message with its frame into reused arrays
- Task 4: `0cdf1abb` feat(lsdj): rp.lsdj.serial-out decodes MI.OUT and master sync after the Game Boy, each message at its byte's frame
- Task 5: `718143ab` docs: rp.lsdj.serial-out, the merge by frame and apu-render, as built

**Where this plan departs from the design.** The design as committed (1d33aa37) was written before the prototype; the
plan follows what was built, and Task 5's docs commit lands the design with its Status line and a section 12, "Found
while building", which lists every departure. In short: the guard after the merge is pinned by a test-only node that
writes its queue out of order (`rp.test.raw-feed`), since every RetroPlug node's queue raises a frame pushed behind a
later one; the schema both LSDj nodes take lives in its own module, `script/nodes/lsdjSyncSettings.ts`; a spec's
`out` is a plain string that `outNodeOf` casts; the decoder reads nothing outside its two modes; a hand-edited
`lsdj-sync` on a system other than a Game Boy places no decoder, so `rp.host.midi-out` has no input for it; the
controller-order case in `ProtocolNodes.test.cpp` checks the merge; and no re-recorded case gained a message (the
design expected each case's last block, decoded at last, to add some). Task 3 also adds two allocation windows to the
profile build's `dsp-bench` (master sync and MI.OUT, each held to zero allocations a block), and Task 4 updates the
hidden `ScriptBench` `[bench]` case, which wired a feedback wire into `rp.lsdj.sync`.

**Ground rules for whoever executes this:**
- Commit after each task (the commit commands are given; Task 6 commits nothing unless a step fails and is fixed,
  in a commit of its own). No `Co-Authored-By` or "generated with" trailer.
- Do not push, and do not open a PR.
- Never use an emdash character in code, comments, docs or commit messages; use " - ", ":" or ";". Some diffs below
  carry emdashes on lines the prototype did not write (context and removed lines of older code): leave those be.
- Run every command from the worktree root with absolute paths or a subshell (`(cd x && ...)`); never `cd` the
  session itself.
- Don't `rm -rf build` to fix CMake; investigate. Never use bare `git stash`, and never switch branches with
  uncommitted work in the tree.
- `deps/sameboy` shows as modified after any configure. That is the tracked audio patch: expected, leave it.
- The derived bundle C arrays (`build/native/*bundle_data.c`) are never committed.
- Parity holds the engine's output to the bit. Tasks 1, 2, 3 and 5 re-record nothing, and a parity failure there is
  a real change to the output: stop and find it. Task 4 re-records exactly three groups (`parity/lsdj-modes`,
  `parity/blocks-tiny-lsdj-master`, `parity/blocks-tiny-lsdj`), and its checker must show that only the twelve cases
  the design names moved, and only in their host MIDI out. The skip baseline does not move.
- The fixture groups need `RETROPLUG_RESOURCES_DIR=/workspaces/resources`; without it their cases skip. Pass it on
  the same command line: a shell `export` does not survive between commands. Every case Task 4 re-records is
  fixture-only, so CI cannot see them: the strict run is the one that judges them.
- `node packages/retroplug/scripts/run-native-tests.mjs` (what `pnpm test:native` runs) takes ONE filter: an exact
  slug (`parity/lsdj-modes`) or a prefix in the dash form (`parity-blocks-tiny`; `parity/blocks-tiny` matches
  nothing). Run several files as separate commands, or the whole suite.
- The profile build is its own tree (`build-prof/`, `tools/run-profile.sh unit|stats`); it rebuilds what it needs.
- The prototype branch `proto/lsdj-serial-out` must stay until this plan has been executed: Task 4's generated
  files are checked against it.
- While a Reaper check runs it uses `build/bin`: do not rebuild the plugin until it finishes. Run the Reaper checks
  one at a time, and before each make sure no Reaper or jackd of an earlier run is left (`pgrep -a reaper;
  pgrep -a jackd`).
- `pnpm check:docs` counts the tracked docs: 60 in the prototype, which had no plan file, so 61 once this plan is
  committed.

---

## File structure

| Path | Responsibility |
|---|---|
| `packages/native/src/host/graph/HostIoNodes.cpp` | `rp.host.midi-out`: a stable merge of its inputs by frame, then the `max(frame, last)` guard (Task 1) |
| `packages/native/src/host/engine/Engine.cpp`, `src/host/script/ScriptHost.{hpp,cpp}` | `apu-render` spans the whole plan; `lastProcessEnd` goes (Task 2) |
| `packages/retroplug/src/lsdjArduinoboy.ts` | the MI.OUT and master-sync decoders: emit `(frame, len, b0, b1, b2)` (Task 3); master sync's stop at its mark, the bytes this block's (Task 4) |
| `packages/retroplug/src/script/nodes/lsdjSerialOut.ts` | new: `rp.lsdj.serial-out`, the Game Boy-to-host node (Task 4) |
| `packages/retroplug/src/script/nodes/lsdjSyncSettings.ts` | new: the one settings schema both LSDj nodes take (Task 4) |
| `packages/retroplug/src/script/nodes/lsdjSync.ts` | `rp.lsdj.sync`, host to Game Boy only: loses `serialOut` and `midiOut` (Tasks 3, 4) |
| `packages/retroplug/src/graph/protocolNodes.ts`, `src/graph/buildGraph.ts` | the after-node on a spec (`out`, `outNodeOf`) and its placement after the systems (Task 4) |
| `packages/retroplug/src/script/bundle.ts` | registers `rp.lsdj.serial-out` last, span label 23 (Task 4) |
| `packages/retroplug/test/script/nodes/lsdj-serial-out.test.ts` | new: the node's tests (Task 4) |
| `packages/retroplug/test-native/parity/baseline/{lsdj-modes,blocks-tiny-lsdj-master,blocks-tiny-lsdj}.json` | re-recorded by Task 4 |
| `packages/retroplug/test/graph/golden/build-graph-roles.json` | the role goldens, re-recorded by Task 4 |
| spec/, docs/, AGENTS.md | Task 5 |

---

### Task 1: rp.host.midi-out merges its inputs by frame

Design section 5. Today `rp.host.midi-out` hands its inputs out one after another, each message at the later of its
frame and the last frame handed out; once a producer sends real frames that raises a later input's earlier frame to
an earlier input's. It becomes a stable merge: each step sends the head with the earliest frame, the lower input on a
tie, with `max(frame, last)` after it as a guard. With every frame 0, which is all any node sends until Task 4, the
merge is the old concatenation, so nothing in parity moves.

**Files:**
- Modify: `packages/native/src/host/engine/Engine.hpp`, `packages/native/src/host/graph/HostIoNodes.cpp`, `packages/native/src/host/graph/HostIoNodes.hpp`, `packages/native/test/engine-graph/HostIo.test.cpp`

- [ ] **Step 1: Write the failing tests**

The midi-out cases of `HostIo.test.cpp` that encoded the old order (the input-by-input case, the replan case's
expected order, and the case whose later input's earlier frame was raised) now expect the merge; two new cases pin
ties in input order and the guard. The guard's case needs an input whose own queue goes back in frame, which no node
of RetroPlug's can make (every one pushes through `rp::graph::pushMessage`, which raises such a frame), so the file
gains a test-only node, `rp.test.raw-feed`, that writes its queue's items directly, as a node on the C ABI may:

````diff
diff --git a/packages/native/test/engine-graph/HostIo.test.cpp b/packages/native/test/engine-graph/HostIo.test.cpp
index cdaefd1e..5c779aef 100644
--- a/packages/native/test/engine-graph/HostIo.test.cpp
+++ b/packages/native/test/engine-graph/HostIo.test.cpp
@@ -12,6 +12,7 @@
 #include <cstdint>
 #include <cstdio>
 #include <cstdlib>
+#include <cstring>
 #include <filesystem>
 #include <fstream>
 #include <functional>
@@ -201,6 +202,33 @@ std::vector<std::pair<std::uint32_t, std::uint8_t>> serialStamped(const SameBoyS
     return log;
 }
 
+// Writes what the test queued in `next` onto its one output as it is, frames and all: a node on the C ABI owns its
+// output queue, and nothing makes it keep the queue frame-sorted, as rp::graph::pushMessage's clamp keeps it.
+struct RawFeed {
+    static constexpr const char* id = "rp.test.raw-feed";
+    struct {
+        rp::events_out::sized<rp::host::test::kFeedCapacity> midi;
+    } outputs;
+    std::vector<rp::host::test::Message> next;
+
+    void process(const rp::Block& b) noexcept {
+        rp_queue& q = *b.output(0).u.queue;
+        for (const auto& m : next) {
+            const auto size = static_cast<std::uint32_t>(m.data.size());
+            if (size) std::memcpy(q.bytes + q.bytes_used, m.data.data(), size);
+            q.items[q.count++] = rp_message{m.frame, size, q.bytes_used, m.flags};
+            q.bytes_used += size;
+        }
+        next.clear();
+    }
+};
+
+const rp_package* rawFeedPackage() {
+    static const rp_node_type* const kTypes[] = {rp::node_type<RawFeed>()};
+    static const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.test.host-io"})", 1, kTypes};
+    return &kPackage;
+}
+
 // Set RETROPLUG_SYNC_TRACE, or clear it (null), for the engines constructed after: each reads it as it is built.
 void setSyncTraceEnv(const char* value) {
 #if defined(_WIN32)
@@ -410,14 +438,14 @@ TEST_CASE("host MIDI staged through the command ring is drained and taken withou
     CHECK(midiOut(e) == std::vector<Out>{{kLoop, 2, {0xFA}}, {kLoop, 5, {0xF8}}});
 }
 
-TEST_CASE("rp.host.midi-out hands the engine each input's messages, input by input, frame-sorted, under its system",
+TEST_CASE("rp.host.midi-out hands the engine its inputs' messages merged by frame, each under its input's system",
           "[engine-graph][host-io]") {
     // Three producers, each wired to its own input, the first and third naming one system (a controller's launches to
     // a real Game Boy, under the id of a system whose own MI.OUT goes out too), and a fourth input left unwired. Each
-    // input's messages go out in queue order, a whole input before the next, and a frame on a later input that is
-    // earlier than one already handed out is raised to it, so the host's MIDI out stays frame-sorted (CLAP and LV2
-    // require it), as the one queue the kernel kept clamped it. A message goes out as it is, whatever its bytes:
-    // a short one, an empty one and one that is no MIDI at all alike.
+    // step hands out the input whose next message is earliest, at its own frame, so the host's MIDI out is
+    // frame-sorted (CLAP and LV2 require it) and each producer's messages keep their times; each input's own go out in
+    // queue order. A message goes out as it is, whatever its bytes: a short one, an empty one and one that is no MIDI
+    // at all alike.
     using rp::host::test::Feed;
     using rp::host::test::HostMidiOut;
     using rp::host::test::NodeRig;
@@ -432,13 +460,13 @@ TEST_CASE("rp.host.midi-out hands the engine each input's messages, input by inp
     rig.node<Feed>("c").next[Feed::Midi] = {{1, {0xFC}}, {1, {}}, {30, {0x80, 60, 0}}};
     const rp_transport t{kRate, 120.0, 0.0, 0};
     rig.host->processBlock(64, t);
-    CHECK(rig.midiOut == std::vector<HostMidiOut>{{21, 5, {0x90, 60, 100}},
+    CHECK(rig.midiOut == std::vector<HostMidiOut>{{22, 0, {0xFA}},
+                                                  {21, 1, {0xFC}},
+                                                  {21, 1, {}},
+                                                  {22, 2, {7, 0, 0, 0, 0xF8}},
+                                                  {22, 2, {0xF0, 1, 2, 0xF7}},
+                                                  {21, 5, {0x90, 60, 100}},
                                                   {21, 9, {0xF8}},
-                                                  {22, 9, {0xFA}},
-                                                  {22, 9, {7, 0, 0, 0, 0xF8}},
-                                                  {22, 9, {0xF0, 1, 2, 0xF7}},
-                                                  {21, 9, {0xFC}},
-                                                  {21, 9, {}},
                                                   {21, 30, {0x80, 60, 0}}});
 
     // And only this block's: a block with nothing queued sends nothing.
@@ -480,7 +508,7 @@ TEST_CASE("rp.host.midi-out with no systems, or an empty list of them, has no in
 
 TEST_CASE("a change to rp.host.midi-out's systems replans cleanly, its inputs moved or kept", "[engine-graph][host-io]") {
     // On an Engine, from no inputs to some and back, rp.host.midi-in's output wired to every input: the engine's MIDI
-    // out is each input's messages in turn, under that input's system, frame-sorted. A change that moves the inputs
+    // out is the inputs' messages merged by frame, each under its input's system. A change that moves the inputs
     // rebuilds the node and one that keeps them swaps its settings; either way the next block goes out as the new
     // settings say, with nothing dropped and no fault.
     Engine e(kRate);
@@ -497,14 +525,12 @@ TEST_CASE("a change to rp.host.midi-out's systems replans cleanly, its inputs mo
         block(e);
         return midiOut(e);
     };
-    // Each system in turn: the two messages under it. The first input's are at frames 1 and 3; a later input's frame 1
-    // is raised to the 3 already handed out.
+    // Every input holds the same two messages, at frames 1 and 3: frame 1's under each system in input order, then
+    // frame 3's.
     const auto under = [](std::vector<std::uint32_t> systems) {
         std::vector<Out> out;
-        for (const auto s : systems) {
-            out.push_back({s, out.empty() ? 1u : 3u, {0xFA}});
-            out.push_back({s, 3, {0xF8}});
-        }
+        for (const auto s : systems) out.push_back({s, 1, {0xFA}});
+        for (const auto s : systems) out.push_back({s, 3, {0xF8}});
         return out;
     };
 
@@ -524,10 +550,11 @@ TEST_CASE("a change to rp.host.midi-out's systems replans cleanly, its inputs mo
     CHECK(e.faultCount() == 0);
 }
 
-TEST_CASE("a later input's earlier frame is raised to the last frame handed out, and a new block starts again from 0",
+TEST_CASE("rp.host.midi-out sends a later input's earlier frame before an earlier input's later one, at its own frame",
           "[engine-graph][host-io]") {
-    // The deleted kernel's one queue clamped each message to the latest frame queued, so the host's MIDI out was
-    // frame-sorted whatever order the producers were wired in; CLAP and LV2 require it of the events the plugin writes.
+    // Two LSDjs in master sync each send a clock at its own byte's frame: merged by frame, the second's keep their
+    // times rather than bunching behind the first's, and the host's MIDI out stays frame-sorted, which CLAP and LV2
+    // require of the events the plugin writes.
     using rp::host::test::Feed;
     using rp::host::test::HostMidiOut;
     using rp::host::test::NodeRig;
@@ -537,19 +564,68 @@ TEST_CASE("a later input's earlier frame is raised to the last frame handed out,
                         "wires":[{"from":"a.midi","to":"midi-out.in1"},{"from":"b.midi","to":"midi-out.in2"}]})")
                 .ok);
     const rp_transport t{kRate, 120.0, 0.0, 0};
-    rig.node<Feed>("a").next[Feed::Midi] = {{10, {0xF8}}};
-    rig.node<Feed>("b").next[Feed::Midi] = {{3, {0xFA}}, {12, {0xFC}}, {5, {0xFB}}};
+    rig.node<Feed>("a").next[Feed::Midi] = {{10, {0xF8}}, {40, {0xF8}}};
+    rig.node<Feed>("b").next[Feed::Midi] = {{3, {0xFA}}, {12, {0xF8}}, {41, {0xFC}}};
     rig.host->processBlock(64, t);
-    CHECK(rig.midiOut == std::vector<HostMidiOut>{{1, 10, {0xF8}}, {2, 10, {0xFA}}, {2, 12, {0xFC}}, {2, 12, {0xFB}}});
+    CHECK(rig.midiOut == std::vector<HostMidiOut>{{2, 3, {0xFA}}, {1, 10, {0xF8}}, {2, 12, {0xF8}}, {1, 40, {0xF8}},
+                                                  {2, 41, {0xFC}}});
 
-    // The floor is the block's: the next block's early frame is its own.
+    // The last frame handed out is the block's: the next block's early frame is its own.
     rig.midiOut.clear();
-    rig.node<Feed>("a").next[Feed::Midi] = {};
     rig.node<Feed>("b").next[Feed::Midi] = {{3, {0xFA}}};
     rig.host->processBlock(64, t);
     CHECK(rig.midiOut == std::vector<HostMidiOut>{{2, 3, {0xFA}}});
 }
 
+TEST_CASE("rp.host.midi-out sends messages at one frame in input order, so with every frame 0 its inputs in turn",
+          "[engine-graph][host-io]") {
+    // A tie goes to the lower input, and each input's own messages keep their queue order: with every frame 0 the merge
+    // is the inputs' concatenation, input by input.
+    using rp::host::test::Feed;
+    using rp::host::test::HostMidiOut;
+    using rp::host::test::NodeRig;
+    NodeRig rig(kRate);
+    REQUIRE(rig.set(R"({"nodes":[{"id":"a","type":"rp.test.feed"},{"id":"b","type":"rp.test.feed"},
+                                 {"id":"midi-out","type":"rp.host.midi-out","settings":{"systems":[1,2]}}],
+                        "wires":[{"from":"a.midi","to":"midi-out.in1"},{"from":"b.midi","to":"midi-out.in2"}]})")
+                .ok);
+    const rp_transport t{kRate, 120.0, 0.0, 0};
+    rig.node<Feed>("a").next[Feed::Midi] = {{4, {0x90, 60, 100}}, {8, {0x80, 60, 0}}};
+    rig.node<Feed>("b").next[Feed::Midi] = {{4, {0x90, 64, 100}}, {8, {0x80, 64, 0}}};
+    rig.host->processBlock(64, t);
+    CHECK(rig.midiOut == std::vector<HostMidiOut>{{1, 4, {0x90, 60, 100}}, {2, 4, {0x90, 64, 100}},
+                                                  {1, 8, {0x80, 60, 0}}, {2, 8, {0x80, 64, 0}}});
+
+    rig.midiOut.clear();
+    rig.node<Feed>("a").next[Feed::Midi] = {{0, {0xFA}}, {0, {0xF8}}};
+    rig.node<Feed>("b").next[Feed::Midi] = {{0, {0x90, 64, 100}}, {0, {0xF8}}};
+    rig.host->processBlock(64, t);
+    CHECK(rig.midiOut == std::vector<HostMidiOut>{{1, 0, {0xFA}}, {1, 0, {0xF8}}, {2, 0, {0x90, 64, 100}}, {2, 0, {0xF8}}});
+}
+
+TEST_CASE("rp.host.midi-out sends nothing earlier than the last frame it handed out, whatever order an input holds",
+          "[engine-graph][host-io]") {
+    // Every node of RetroPlug's pushes through rp::graph::pushMessage, whose clamp keeps a queue frame-sorted, but a
+    // node on the C ABI owns its output queue and may leave it out of order. The merge reads each input's head, so an
+    // input that steps back in frame would step the host's MIDI out back with it; the node sends each message at the
+    // later of its frame and the last it handed out instead, so Engine::midiOut stays frame-sorted.
+    using rp::host::test::Feed;
+    using rp::host::test::HostMidiOut;
+    using rp::host::test::NodeRig;
+    NodeRig rig(kRate);
+    REQUIRE(rig.registry.addPackage(rawFeedPackage()));
+    REQUIRE(rig.set(R"({"nodes":[{"id":"raw","type":"rp.test.raw-feed"},{"id":"b","type":"rp.test.feed"},
+                                 {"id":"midi-out","type":"rp.host.midi-out","settings":{"systems":[1,2]}}],
+                        "wires":[{"from":"raw.midi","to":"midi-out.in1"},{"from":"b.midi","to":"midi-out.in2"}]})")
+                .ok);
+    rig.node<RawFeed>("raw").next = {{10, {0xF8}}, {3, {0xFA}}, {12, {0xFC}}};
+    rig.node<Feed>("b").next[Feed::Midi] = {{5, {0xFB}}, {11, {0xF8}}};
+    rig.host->processBlock(64, rp_transport{kRate, 120.0, 0.0, 0});
+    CHECK(rig.midiOut == std::vector<HostMidiOut>{{2, 5, {0xFB}}, {1, 10, {0xF8}}, {1, 10, {0xFA}}, {2, 11, {0xF8}},
+                                                  {1, 12, {0xFC}}});
+    for (std::size_t i = 1; i < rig.midiOut.size(); ++i) CHECK(rig.midiOut[i - 1].frame <= rig.midiOut[i].frame);
+}
+
 TEST_CASE("a change to rp.host.midi-out's systems keeps the node while its inputs hold and rebuilds it when they move",
           "[engine-graph][host-io]") {
     using rp::host::test::NodeRig;
````

- [ ] **Step 2: Run the tests to see them fail**

Run: `cmake --build build --target retroplug-engine-graph-test -j$(nproc) && build/bin/retroplug-engine-graph-test "[host-io]"`
Expected: FAIL: five `[host-io]` cases fail (the three whose expectations now encode the merge, the ties case and the guard's), every other case passes. That is what the prototype's run of the tests before the implementation printed.

- [ ] **Step 3: Merge the inputs by frame**

The merge keeps one head index per input in a member array, `kMidiOutMaxInputs` long, reset each block for the inputs
in use, so it allocates nothing; a strict `<` keeps a tie with the lower input. The node's comment says what it does
and why, and `Engine.hpp`'s `midiOut` comment and the node list in `HostIoNodes.hpp` follow:

````diff
diff --git a/packages/native/src/host/engine/Engine.hpp b/packages/native/src/host/engine/Engine.hpp
index 591ad31c..22ecb3d1 100644
--- a/packages/native/src/host/engine/Engine.hpp
+++ b/packages/native/src/host/engine/Engine.hpp
@@ -121,11 +121,12 @@ public:
     void setSampleRate(double sr);
 
     // The host MIDI out of this block: what the plan's rp.host.midi-out appended, in plan order, each tagged with
-    // the system that emitted it. Frame-sorted within the plan's one rp.host.midi-out: the node raises a later input's
-    // earlier frame to the last it handed out, which the plugin relies on to write the events to a DAW that requires
-    // them in order (CLAP, LV2). Cleared at the top of each block that runs: a 0-frame block, or one longer than the
-    // plan's buffers, runs nothing and keeps the previous contents. The plugin drains this to the DAW after
-    // processBlock, and the SDL standalone to its MIDI out; the RPC drains (drainMidiOut, the harness's) read it too.
+    // the system that emitted it. Frame-sorted within the plan's one rp.host.midi-out: the node merges its inputs by
+    // frame and sends nothing earlier than the last frame it handed out, which the plugin relies on to write the
+    // events to a DAW that requires them in order (CLAP, LV2). Cleared at the top of each block that runs: a 0-frame
+    // block, or one longer than the plan's buffers, runs nothing and keeps the previous contents. The plugin drains
+    // this to the DAW after processBlock, and the SDL standalone to its MIDI out; the RPC drains (drainMidiOut, the
+    // harness's) read it too.
     const std::vector<EngineMidiOut>& midiOut() const { return midiOut_; }
     void clearMidiOut() { midiOut_.clear(); }
 
diff --git a/packages/native/src/host/graph/HostIoNodes.cpp b/packages/native/src/host/graph/HostIoNodes.cpp
index 4613cd40..73b5fb7f 100644
--- a/packages/native/src/host/graph/HostIoNodes.cpp
+++ b/packages/native/src/host/graph/HostIoNodes.cpp
@@ -56,12 +56,17 @@ rp::graph::PortDesc eventsIn(std::string id) {
 }
 
 // An input per entry of its settings' `systems` (docs/modular-graph-phase-b3b-design.md section 5), in1 to inN, each
-// holding one producer's MIDI as it is, which goes out as that entry's system: input by input, each in queue order,
-// never merged by frame. A message whose frame is earlier than one already handed out in the block goes out at that
-// frame instead, as the kernel's one queue clamped it, so Engine::midiOut stays frame-sorted whatever order a document
-// wires its producers in. Two entries may name one system (a controller sending to a real Game Boy under the id of a
-// system whose own MI.OUT also goes out). No entries, no inputs: a document whose roles send no host MIDI places it
-// all the same, and it sends nothing.
+// holding one producer's MIDI as it is, which goes out as that entry's system. The inputs are merged by frame
+// (docs/lsdj-serial-out-design.md section 5): each step hands out the head with the earliest frame, the lower input on
+// a tie, so two LSDjs in master sync each keep their clocks' times rather than the second's bunching behind the
+// first's, and with every frame 0 the merge is the inputs' concatenation, input by input, each in queue order. A port's
+// queue raises a frame pushed behind a later one, so an input arrives frame-sorted; a message still goes out at the
+// later of its frame and the last handed out in the block, a guard that keeps Engine::midiOut frame-sorted whatever an
+// input holds (a node on the C ABI writes its own queue), which the plugin relies on to write a DAW's events in order.
+// It allocates nothing: a head index per input, reset each block, and the merge costs messages times inputs. Two
+// entries may name one system (a controller sending to a real Game Boy under the id of a system whose own MI.OUT also
+// goes out). No entries, no inputs: a document whose roles send no host MIDI places it all the same, and it sends
+// nothing.
 // The inputs are appended here, none reflected, since their count is the settings'.
 struct MidiOut : HostIoNode {
     static constexpr const char* id = "rp.host.midi-out";
@@ -93,16 +98,30 @@ struct MidiOut : HostIoNode {
             systems[inputCount++] = s.systems[k];
     }
 
+    // Each input's next message this block, an index into its queue; only process reads or writes them.
+    std::array<std::uint32_t, kMidiOutMaxInputs> heads{};
+
     void process(const rp::Block& b) noexcept {
+        const std::uint32_t inputs = inputCount < b.raw.input_count ? inputCount : b.raw.input_count;
+        for (std::uint32_t k = 0; k < inputs; ++k) heads[k] = 0;
         std::uint32_t last = 0;  // the latest frame handed out this block
-        for (std::uint32_t k = 0; k < inputCount && k < b.raw.input_count; ++k) {
-            const rp_queue& q = *b.input(k).u.queue;
-            for (std::uint32_t i = 0; i < q.count; ++i) {
-                const rp_message& m = q.items[i];
-                const auto data = rp::graph::messageData(q, m);
-                if (m.frame > last) last = m.frame;
-                io->midiOut(io->ctx, systems[k], last, data.data(), data.size());
+        for (;;) {
+            std::uint32_t next = inputs;  // the input whose head goes out next: none yet
+            std::uint32_t frame = 0;
+            for (std::uint32_t k = 0; k < inputs; ++k) {
+                const rp_queue& q = *b.input(k).u.queue;
+                if (heads[k] >= q.count) continue;
+                if (next == inputs || q.items[heads[k]].frame < frame) {  // strictly earlier: a tie keeps the lower
+                    next = k;
+                    frame = q.items[heads[k]].frame;
+                }
             }
+            if (next == inputs) return;
+            const rp_queue&   q = *b.input(next).u.queue;
+            const rp_message& m = q.items[heads[next]++];
+            const auto        data = rp::graph::messageData(q, m);
+            if (m.frame > last) last = m.frame;
+            io->midiOut(io->ctx, systems[next], last, data.data(), data.size());
         }
     }
 };
diff --git a/packages/native/src/host/graph/HostIoNodes.hpp b/packages/native/src/host/graph/HostIoNodes.hpp
index 956fcae5..bddb1300 100644
--- a/packages/native/src/host/graph/HostIoNodes.hpp
+++ b/packages/native/src/host/graph/HostIoNodes.hpp
@@ -13,9 +13,9 @@ namespace rp::host {
 //
 //   rp.host.midi-in         out midi: the host MIDI the engine staged, stable-sorted by frame
 //   rp.host.midi-out        {"systems": [<id>, ...]}: inputs in1 to inN, one per entry, whose messages land in
-//                           Engine::midiOut tagged with that entry's system, input by input, each in queue order and
-//                           none earlier in frame than one before it
-//                           (docs/modular-graph-phase-b3b-design.md section 9); two entries may name one system.
+//                           Engine::midiOut tagged with that entry's system, merged by frame, ties in input order,
+//                           and none earlier in frame than one before it (docs/modular-graph-phase-b3b-design.md
+//                           section 9, docs/lsdj-serial-out-design.md section 5); two entries may name one system.
 //                           With no systems (the default) it has no inputs and sends nothing
 //   rp.host.controller-in   out midi: the staged control-surface MIDI, at frame 0; out connected: 1 while a
 //                           control surface is attached, from frame 0 of the block
````

- [ ] **Step 4: Run the tests to see them pass, and parity unchanged**

Run: `cmake --build build --target retroplug-engine-graph-test -j$(nproc) && build/bin/retroplug-engine-graph-test "[host-io]"`
Expected: `All tests passed` for the `[host-io]` cases.

Run: `pnpm test:plugin`
Expected: `# 10 plugin test binary(ies) passed (jobs=16)`, the engine-graph binary's `All tests passed (15484 assertions in 120 test cases)` (two cases more than before: the ties and the guard).

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=16)`, none skipped, nothing re-recorded.

Run: `pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=16)`, with `46 case(s) SKIPPED in 10 file(s)` (every fixture group's cases, as CI runs them) and the skip check clean.

- [ ] **Step 5: Commit**

````bash
git add -- packages/native/src/host/engine/Engine.hpp packages/native/src/host/graph/HostIoNodes.cpp \
  packages/native/src/host/graph/HostIoNodes.hpp packages/native/test/engine-graph/HostIo.test.cpp
git commit -F - <<'EOF'
feat(host-io): rp.host.midi-out merges its inputs by frame, ties in input order

Each step hands out the head with the earliest frame, the lower input on a tie, so two LSDjs in master sync keep
their clocks' times once they send at their bytes' frames; with every frame 0 the merge is the concatenation it
always was, so nothing moves yet (strict parity unchanged). max(frame, last) stays after the merge as a guard, so
Engine::midiOut is frame-sorted whatever an input's queue holds: a raw-feed test node on the C ABI writes its queue
out of order to pin it. A head index per input, reset each block; nothing allocated.
EOF
````

### Task 2: the profile's apu-render spans the whole plan

Design section 6. A profile build's `apu-render` span starts where the last script node's `process` ended, which is
right only while every script node runs before every system; Task 4 places one after a Game Boy. It becomes the
plan's whole run, the script nodes' spans nested inside it, and `ScriptHost::lastProcessEnd`, which only it read,
goes. A profile-build change: parity cannot move.

**Files:**
- Modify: `packages/native/src/host/engine/Engine.cpp`, `packages/native/src/host/script/ScriptHost.cpp`, `packages/native/src/host/script/ScriptHost.hpp`, `packages/native/test/engine-graph/ScriptNodes.test.cpp`, `packages/retroplug/src/graph/buildGraph.ts`, `packages/retroplug/test-native/dsp-bench.test.ts`, `packages/retroplug/test-native/nes-bench.test.ts`

- [ ] **Step 1: Write the failing test**

A `[profile]` case drives an Engine on the embedded bundle with a real mGB core and two script nodes, one before the
system (`rp.mgb.midi`, feeding its `serialIn`) and one after it (`rp.lsdj.sync` on a forward wire from
`sys-1.serialOut`; Task 4 moves this case onto `rp.lsdj.serial-out`), and checks over four blocks that each node's
process span lies inside its block's `apu-render`:

````diff
diff --git a/packages/native/test/engine-graph/ScriptNodes.test.cpp b/packages/native/test/engine-graph/ScriptNodes.test.cpp
index 0970786e..99e3be73 100644
--- a/packages/native/test/engine-graph/ScriptNodes.test.cpp
+++ b/packages/native/test/engine-graph/ScriptNodes.test.cpp
@@ -9,7 +9,8 @@
 // around a toy bundle close it. A RETROPLUG_PROFILE build (build-prof/, which tools/run-profile.sh configures) adds the
 // trace window's cases, [profile]: a script node's process span labelled by its type and closed every way a call ends,
 // a label per type in load order, the profile calls refused while an audio thread owns the runtime, the types past
-// the label table's bound, and the embedded bundle's types labelled in the order the docs quote.
+// the label table's bound, the embedded bundle's types labelled in the order the docs quote, and an Engine's apu-render
+// spanning its plan's whole run, its script nodes' process spans inside it.
 //
 // Run via `pnpm test:plugin engine-graph`; the profile cases, build-prof/bin/retroplug-engine-graph-test "[profile]".
 
@@ -18,6 +19,7 @@
 #include <chrono>
 #include <cstdint>
 #include <cstdio>
+#include <memory>
 #include <mutex>
 #include <string>
 #include <string_view>
@@ -27,8 +29,12 @@
 #include <catch2/catch_test_macros.hpp>
 
 #include "../graph/CountingAllocator.hpp"
+#include "GraphDocs.hpp"
+#include "NodeRig.hpp"
 #include "ScriptRig.hpp"
 #include "host/engine/Engine.hpp"
+#include "system/sameboy/SameBoyConfig.hpp"
+#include "system/sameboy/SameBoySystem.hpp"
 
 using rp::host::ScriptHost;
 using rp::host::test::Feed;
@@ -2208,4 +2214,63 @@ TEST_CASE("the embedded bundle's types are labelled 16 to 22 in the order spec/0
     REQUIRE(names.size() >= rp::host::DSP_SPAN_TYPE_BASE + ids.size());
     for (std::size_t i = 0; i < ids.size(); ++i) CHECK(names[rp::host::DSP_SPAN_TYPE_BASE + i] == ids[i]);
 }
+
+TEST_CASE("an Engine's apu-render spans its plan's whole run, each script node's process span inside it wherever it runs",
+          "[script][profile][trace]") {
+    // The embedded bundle's rp.mgb.midi runs before its Game Boy, as buildGraph places it, and its rp.lsdj.sync after it,
+    // over a forward wire from the Game Boy's serialOut, as a node placed after a system runs: each block's apu-render
+    // holds both nodes' process spans (Chrome trace events nest), and no script node's span starts it.
+    using rp::host::test::DocExtras;
+    using rp::host::test::docNode;
+    using rp::host::test::docWire;
+    constexpr double kRate = 48000.0;
+    constexpr std::size_t kBlocks = 4;
+    Engine e(kRate);
+    SameBoyConfig cfg;
+    cfg.model = SameBoyModel::DmgB;
+    auto core = std::make_unique<SameBoySystem>(1, cfg, rp::host::test::readFile(RP_MGB_ROM_PATH));
+    core->onActivate(kRate);
+    REQUIRE(e.parkCore(std::move(core)));
+    const DocExtras extras{{docNode("midi-in", "rp.host.midi-in"), docNode("mgb", "rp.mgb.midi"), docNode("late", "rp.lsdj.sync")},
+                           {docWire("midi-in.midi", "mgb.midi"), docWire("mgb.serialIn", "sys-1.serialIn"),
+                            docWire("sys-1.serialOut", "late.serialOut")}};
+    const auto planned =
+        e.setGraph(rp::host::test::graphRequest(rp::host::test::stereoDocument({{1, rp::host::test::kGbType}}, extras), {1}));
+    INFO((planned.errors.empty() ? std::string() : planned.errors[0].code + ": " + planned.errors[0].message));
+    REQUIRE(planned.ok);
+    REQUIRE(e.dspTraceReset(true));
+    std::vector<float> l(256), r(256);
+    for (std::size_t i = 0; i < kBlocks; ++i) {
+        e.stageMidi(0, std::vector<std::uint8_t>{0x90, 60, 100});
+        e.processBlock(256, l.data(), r.data());
+    }
+    const auto spans = e.dspTrace();
+    const auto names = e.dspTraceNames();
+    REQUIRE(e.dspTraceReset(false));
+    CHECK(e.scriptThrows() == 0);
+
+    // A label's spans, in the order they started: one a block.
+    const auto spansOf = [&](std::uint32_t label) {
+        std::vector<rp::host::DspTraceSpan> out;
+        for (const auto& sp : spans)
+            if (sp.label == label) out.push_back(sp);
+        std::sort(out.begin(), out.end(), [](const auto& x, const auto& y) { return x.t0 < y.t0; });
+        return out;
+    };
+    const auto labelOf = [&](const std::string& type) {
+        return static_cast<std::uint32_t>(std::find(names.begin(), names.end(), type) - names.begin());
+    };
+    const auto apu = spansOf(rp::host::DSP_SPAN_APU);
+    REQUIRE(apu.size() == kBlocks);
+    for (const char* type : {"rp.mgb.midi", "rp.lsdj.sync"}) {
+        INFO(type);
+        const auto process = spansOf(labelOf(type));
+        REQUIRE(process.size() == kBlocks);
+        for (std::size_t k = 0; k < kBlocks; ++k) {
+            INFO("block " << k);
+            CHECK(apu[k].t0 <= process[k].t0);
+            CHECK(process[k].t1 <= apu[k].t1);
+        }
+    }
+}
 #endif
````

- [ ] **Step 2: Run it to see it fail**

Run: `tools/run-profile.sh unit`
Expected: FAIL: the new case's 8 checks fail (in each of the 4 blocks, `apu-render` starts after both nodes' process spans have started), the other five `[profile]` cases pass.

- [ ] **Step 3: Span the plan**

`Engine.cpp` opens `apu-render` at the plan's start; `lastProcessEnd` and its member go from the script host, whose
process-span end becomes a plain close. The comments that described the old span (`buildGraph.ts`'s note on document
order, the trace comments of the two profile benches) say what is true now:

````diff
diff --git a/packages/native/src/host/engine/Engine.cpp b/packages/native/src/host/engine/Engine.cpp
index 8757b66f..7a68a645 100644
--- a/packages/native/src/host/engine/Engine.cpp
+++ b/packages/native/src/host/engine/Engine.cpp
@@ -616,11 +616,10 @@ void Engine::processBlock(std::uint32_t frames, float* const* outputs, std::size
     // The host inputs, the script nodes, every system, the mixing and the host outputs (the host drained before this).
     graph_->host->run(frames, transport, outputs, static_cast<std::uint32_t>(numOutputs));
 #ifdef RETROPLUG_PROFILE
-    // apu-render: the plan from where its last script node ends. buildGraph places every script node ahead of every
-    // system and the planner takes the first ready node in document order (Planner.cpp order()), so they all run
-    // first: a script node reads only earlier nodes, a Game Boy's serialOut reaching its LSDj over a feedback wire,
-    // which orders nothing.
-    script.spanSince(rp::host::DSP_SPAN_APU, std::max(planStart, script.lastProcessEnd()));
+    // apu-render: the plan's whole run, from where it starts to where it ends. Each script node's process span lies
+    // inside it (Chrome trace events nest), so it holds wherever the planner puts a script node, before a system or
+    // after one, and its figure includes the script nodes' few microseconds beside the cores'.
+    script.spanSince(rp::host::DSP_SPAN_APU, planStart);
 #endif
     // The promise jobs the block's script nodes queued, then (a profile build) the allocation window's block counts.
     graph_->script.endBlock();
diff --git a/packages/native/src/host/script/ScriptHost.cpp b/packages/native/src/host/script/ScriptHost.cpp
index f683ae77..d856437e 100644
--- a/packages/native/src/host/script/ScriptHost.cpp
+++ b/packages/native/src/host/script/ScriptHost.cpp
@@ -626,16 +626,9 @@ struct ScriptHost::Glue {
     static void jsCallBegin(ScriptHost& h) { traceBegin(h, DSP_SPAN_JSCALL); }
     static void jsCallEnd(ScriptHost& h) { traceEnd(h); }
 
-    // The end of a process call's span, its type's (a profile build), where apu-render starts if no script node runs
-    // after it.
-    static void processSpanEnd([[maybe_unused]] ScriptHost& h) {
-#ifdef RETROPLUG_PROFILE
-        if (h.traceArmed_) {
-            h.spanEnd();  // the type's
-            h.lastProcessEnd_ = h.traceNow();
-        }
-#endif
-    }
+    // The end of a process call's span, its type's (a profile build), which the Engine's apu-render holds: that spans
+    // the plan's whole run.
+    static void processSpanEnd(ScriptHost& h) { traceEnd(h); }
 
     static rp_prepare_result prepare(rp_node* n, const rp_settings* s, void** prepared, rp_error* err) {
         Instance& in = instanceOf(n);
@@ -1735,7 +1728,6 @@ bool ScriptHost::traceReset(bool arm) {
     traceStack_.clear();
     traceSpans_.clear();
     traceStartNs_ = steadyNs();
-    lastProcessEnd_ = 0.0;
     traceArmed_ = arm;
     return true;
 }
diff --git a/packages/native/src/host/script/ScriptHost.hpp b/packages/native/src/host/script/ScriptHost.hpp
index 2ce227e1..36e597ff 100644
--- a/packages/native/src/host/script/ScriptHost.hpp
+++ b/packages/native/src/host/script/ScriptHost.hpp
@@ -78,7 +78,8 @@ enum DspSpanLabel : std::uint32_t {
     DSP_SPAN_MARSHAL     = 1,   // ScriptHost: its inputs made JS values (the audio copies in and the transport, then
                                 //   each input it reads)
     DSP_SPAN_JSCALL      = 2,   // ScriptHost: the JS_Call into its process
-    DSP_SPAN_APU         = 3,   // Engine: the plan after its script nodes (the cores, the mixing, the host outputs)
+    DSP_SPAN_APU         = 3,   // Engine: the plan's whole run (the cores, the mixing, the host outputs), holding its
+                                //   script nodes' process spans
     DSP_SPAN_PUBLISH     = 4,   // Engine: the state pump to the control plane (the per-block frame copy, and the timed
                                 //   savestate and SRAM republish, so spiky)
     DSP_SPAN_TYPE_BASE   = 16,  // ScriptHost: the process call of a script node of the bundle's first type, and up
@@ -266,13 +267,12 @@ public:
     // The trace window. traceReset clears it and arms (or disarms) it, quiescent, as are the reads; the block thread
     // records, and only while it is armed, so a window armed before an audio thread starts records (and grows) until
     // that thread hands the runtime back. spanBegin and spanEnd bracket a span; spanSince records one from `t0`, a
-    // traceNow(), to now. lastProcessEnd() is when the last script node's process span ended, on traceNow's clock.
+    // traceNow(), to now.
     // traceName names a label below kDspSpanLabels, and ignores one past it.
     void                      spanBegin(std::uint32_t label);
     void                      spanEnd();
     void                      spanSince(std::uint32_t label, double t0);
     double                    traceNow() const;
-    double                    lastProcessEnd() const { return lastProcessEnd_; }
     void                      traceName(std::uint32_t label, const std::string& name);
     bool                      traceReset(bool arm);
     bool                      traceArmed() const { return traceArmed_; }
@@ -361,7 +361,6 @@ private:
     // The trace window: its clock's start (steady_clock nanoseconds), the spans open as {label, t0} and those closed.
     bool                                          traceArmed_ = false;
     std::uint64_t                                 traceStartNs_ = 0;
-    double                                        lastProcessEnd_ = 0.0;
     std::vector<std::pair<std::uint32_t, double>> traceStack_;
     std::vector<DspTraceSpan>                     traceSpans_;
     std::vector<std::string>                      traceNames_;
diff --git a/packages/retroplug/src/graph/buildGraph.ts b/packages/retroplug/src/graph/buildGraph.ts
index 3be1b26d..769704a2 100644
--- a/packages/retroplug/src/graph/buildGraph.ts
+++ b/packages/retroplug/src/graph/buildGraph.ts
@@ -146,8 +146,7 @@ export function buildGraph(model: GraphModel): BuiltGraph {
   nodes.push({ id: "midi-in", type: "rp.host.midi-in" }, { id: "controller-in", type: "rp.host.controller-in" });
   // Every script node goes before the systems in the document, so before them in the plan: the planner breaks ties by
   // document order (Planner.cpp order()), and nothing a script node reads comes from a node after it but over a
-  // feedback wire. A profile build's apu-render span begins where the last script node's process ended, which
-  // relies on it.
+  // feedback wire.
   //
   // The routing: the host's MIDI fanned out to the systems in slot order (positional routing reads it), an output
   // for each, sys-<id>.
diff --git a/packages/retroplug/test-native/dsp-bench.test.ts b/packages/retroplug/test-native/dsp-bench.test.ts
index 05d4e747..5e8c1130 100644
--- a/packages/retroplug/test-native/dsp-bench.test.ts
+++ b/packages/retroplug/test-native/dsp-bench.test.ts
@@ -207,9 +207,9 @@ test("dsp-bench: the script nodes' per-block allocation under an mGB + MIDI work
 
   // --- optional per-role runtime trace (spec/08-profiling.md Tier B) --------------------------------
   // A separate SHORT armed window AFTER the alloc measurement (so those numbers stay pristine). Records nested
-  // wall-time spans (per block: each script node's process, a span named by its type, rp.midi.route then each
-  // rp.mgb.midi, holding marshal and then js-call, which holds a marshal per input it reads; then apu-render, the rest
-  // of the plan; then state-publish) and prints ONE Chrome trace-event JSON line the wrapper captures into
+  // wall-time spans (per block: apu-render, the plan's whole run, holding each script node's process, a span named by
+  // its type, rp.midi.route then each rp.mgb.midi, which holds marshal and then js-call, which holds a marshal per
+  // input it reads; then state-publish) and prints ONE Chrome trace-event JSON line the wrapper captures into
   // build-prof/trace.json (load in ui.perfetto.dev or speedscope). Gated on RP_BENCH_TRACE so the normal stats run
   // stays a single line.
   if (env("RP_BENCH_TRACE")) {
diff --git a/packages/retroplug/test-native/nes-bench.test.ts b/packages/retroplug/test-native/nes-bench.test.ts
index c140c0a4..a8a6dad7 100644
--- a/packages/retroplug/test-native/nes-bench.test.ts
+++ b/packages/retroplug/test-native/nes-bench.test.ts
@@ -2,9 +2,9 @@
 // Mesen core off the RT thread via the deterministic renderAudio pull path under a MIDI workload, and
 // reports the same per-block allocation + per-stage timing metrics. The point of comparison: on the
 // handheld the NES core "struggles" far more than SameBoy, and this pins down WHERE — the Mesen core
-// step (CPU+PPU+APU: the "apu-render" span is the plan after the script nodes, Mesen included) vs the
-// script nodes (spans named by their types, rp.midi.route and rp.n8.midi-to-fifo, each holding marshal and
-// js-call). One real NES system driven by host MIDI (the nes-n8-midi role, placed as rp.n8.midi-to-fifo).
+// step (CPU+PPU+APU: the "apu-render" span is the plan's whole run, Mesen included) vs the script nodes
+// (spans named by their types, rp.midi.route and rp.n8.midi-to-fifo, inside apu-render, each holding marshal
+// and js-call). One real NES system driven by host MIDI (the nes-n8-midi role, placed as rp.n8.midi-to-fifo).
 //
 // The counters/spans exist only in a RETROPLUG_PROFILE host (built by tools/run-profile.sh); under the
 // default host this SKIPS (so the normal test:native sweep stays fast, and says the case did not run
@@ -183,9 +183,9 @@ test("nes-bench: Mesen core per-block cost + JS allocation under a NES + MIDI wo
     expect(s.maxBlockAllocCalls, "allocations in the worst block").toBe(0);
   }
 
-  // --- optional per-stage runtime trace (spec/08 Tier B): each script node's span (holding marshal, js-call)
-  // + apu-render (the Mesen core step). One Chrome trace-event JSON line the wrapper
-  // captures → build-prof/trace.json.
+  // --- optional per-stage runtime trace (spec/08 Tier B): apu-render (the plan's whole run, the Mesen core
+  // step most of it), holding each script node's span (holding marshal, js-call). One Chrome trace-event JSON
+  // line the wrapper captures → build-prof/trace.json.
   if (env("RP_BENCH_TRACE")) {
     const traceBlocks = Math.max(1, envInt("RP_BENCH_TRACE_BLOCKS", 64));
     const tSched = buildSchedule(profile, traceBlocks, seed ^ 0x9e3779b9);
````

- [ ] **Step 4: Run the profile cases, the suites and parity**

Run: `tools/run-profile.sh unit`
Expected: `All tests passed (126 assertions in 6 test cases)`.

Run: `pnpm test:plugin`
Expected: `# 10 plugin test binary(ies) passed (jobs=16)`, the engine-graph binary's `All tests passed (15484 assertions in 120 test cases)` (the new case is a `[profile]` one, built only in the profile tree).

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=16)`, none skipped.

A span is stored as it closes, so a block's `apu-render` now follows the spans it holds in the trace; the trace gives
each span its start and length (Chrome trace-event `X` events), so nothing reading it depends on the order.

- [ ] **Step 5: Commit**

````bash
git add -- packages/native/src/host/engine/Engine.cpp packages/native/src/host/script/ScriptHost.cpp \
  packages/native/src/host/script/ScriptHost.hpp packages/native/test/engine-graph/ScriptNodes.test.cpp \
  packages/retroplug/src/graph/buildGraph.ts packages/retroplug/test-native/dsp-bench.test.ts \
  packages/retroplug/test-native/nes-bench.test.ts
git commit -F - <<'EOF'
feat(profile): apu-render spans the whole plan, the script nodes' spans inside it

apu-render opened where the last script node's process ended, which held only while every script node ran before
every system. It is now the plan's whole run, from planStart, with each script node's process span nested inside
it (Chrome trace events nest), so it holds wherever a script node runs. ScriptHost::lastProcessEnd goes: nothing
else read it. A [profile] case drives an Engine on the embedded bundle with rp.mgb.midi before its Game Boy and
rp.lsdj.sync after it over a forward wire, and holds each block's process spans inside its apu-render.
EOF
````

### Task 3: the decoders emit each message with its frame, into reused arrays

Design section 4, "The emit". The MI.OUT and master-sync decoders built a new array for every message they decoded,
the one per-block JS allocation left on a production path. Their emit becomes `(frame, len, b0, b1, b2)`, each message
at the frame of the byte that completes it, and `rp.lsdj.sync` sends from one of three arrays it made with the
instance, one per length (`send` takes an array-like and sends all of it). The node still sends at frame 0: its bytes
are last block's until Task 4 moves the decoders after the Game Boy, so parity cannot move.

**Files:**
- Modify: `packages/retroplug/src/lsdjArduinoboy.ts`, `packages/retroplug/src/script/nodes/lsdjSync.ts`, `packages/retroplug/test-native/dsp-bench.test.ts`, `packages/retroplug/test/dsp/lsdj-arduinoboy.test.ts`, `packages/retroplug/test/script/nodes/_rig.ts`, `packages/retroplug/test/script/nodes/lsdj-sync.test.ts`

- [ ] **Step 1: Write the failing tests**

The pure decoder tests move to the new emit, through an `into(out, frames)` helper, and gain cases that each message
carries its byte's frame: an MI.OUT message at the frame of the captured byte whose bits complete it, a frame
straddling two calls and a command whose value byte lands in the next call both at the completing byte's frame in the
later call; master sync's NoteOn, 0xFA and 0xF8 at the first byte's frame, a stop found before a byte at that byte's
frame, and one found at a block start at that start. The node test rig records what each send was handed
(`NodeRig.sources`), so a node case can check that MI.OUT and master sync send the same array per message length
across blocks:

````diff
diff --git a/packages/retroplug/test/dsp/lsdj-arduinoboy.test.ts b/packages/retroplug/test/dsp/lsdj-arduinoboy.test.ts
index a7d42d06..5115168e 100644
--- a/packages/retroplug/test/dsp/lsdj-arduinoboy.test.ts
+++ b/packages/retroplug/test/dsp/lsdj-arduinoboy.test.ts
@@ -4,8 +4,9 @@
 //     packages/native/test/ArduinoboyMasterTests.cpp (same bytes in, same MIDI out).
 //   - arduinoboyDecodeSerialOut: the flag-gated framing in front of it — the raw captured bytes LSDj
 //     actually emits (each frame = 1 flag bit + 7 payload bits, MSB-first), decoded to the same MIDI.
-// Then master sync's decoder, arduinoboyMasterSyncBlock. The rp.lsdj.sync node that runs them off its Game Boy's
-// serial out (modes 7 and 8) is test/script/nodes/lsdj-sync.test.ts's.
+// Then master sync's decoder, arduinoboyMasterSyncBlock. Each decoder hands every message to its emit with its byte's
+// frame. The rp.lsdj.sync node that runs them off its Game Boy's serial out (modes 7 and 8) is
+// test/script/nodes/lsdj-sync.test.ts's.
 import { test, expect } from "../../testing/harness";
 import {
   arduinoboyDecodeByte,
@@ -14,13 +15,20 @@ import {
   arduinoboyReset,
   type ArduinoboyState,
   type MasterSyncState,
+  type MidiEmit,
 } from "../../src/lsdjArduinoboy";
 
+/** An emit recording each message's bytes into `out`, and its frame into `frames` when given. */
+const into = (out: number[][], frames?: number[]): MidiEmit => (frame, len, b0, b1, b2) => {
+  out.push([b0, b1, b2].slice(0, len));
+  frames?.push(frame);
+};
+
 // Feed a run of protocol bytes through a fresh decoder, returning the emitted MIDI messages.
 function decode(bytes: number[]): number[][] {
   const out: number[][] = [];
   const state: ArduinoboyState = {};
-  for (const b of bytes) arduinoboyDecodeByte(b, state, (d) => out.push(d));
+  bytes.forEach((b, i) => arduinoboyDecodeByte(b, i, state, into(out)));
   return out;
 }
 
@@ -70,12 +78,12 @@ test("ArduinoboyMaster: high-bit bytes (>=0x80) are ignored", () => {
 test("ArduinoboyMaster: reset clears pending state mid-command", () => {
   const out: number[][] = [];
   const state: ArduinoboyState = {};
-  arduinoboyDecodeByte(0x71, state, (d) => out.push(d)); // pending NoteOn ch 1
+  arduinoboyDecodeByte(0x71, 0, state, into(out)); // pending NoteOn ch 1
   expect(out.length).toBe(0);
   arduinoboyReset(state);
-  arduinoboyDecodeByte(60, state, (d) => out.push(d)); // would complete the note; now dropped
+  arduinoboyDecodeByte(60, 0, state, into(out)); // would complete the note; now dropped
   expect(out.length).toBe(0);
-  arduinoboyDecodeByte(0x7f, state, (d) => out.push(d)); // realtime still works
+  arduinoboyDecodeByte(0x7f, 0, state, into(out)); // realtime still works
   expect(out).toEqual([[0xf8]]);
 });
 
@@ -89,10 +97,11 @@ test("ArduinoboyMaster: command 0x7C consumes its value but emits nothing (m=0xC
 // packed into a single captured 8-bit byte (so a data byte reads as 0x80 | payload).
 const frame = (cmd: number): number => 0x80 | (cmd & 0x7f);
 
+// A byte every eight frames, as SameBoy clocks MI.OUT's port (one bit an audio frame).
 function decodeSerial(bytes: number[]): number[][] {
   const out: number[][] = [];
   const state: ArduinoboyState = {};
-  arduinoboyDecodeSerialOut(bytes, state, (d) => out.push(d));
+  arduinoboyDecodeSerialOut(bytes, bytes.map((_, i) => 8 * i + 7), state, into(out));
   return out;
 }
 
@@ -115,12 +124,35 @@ test("framing: a frame straddling a captured-byte boundary buffers, then decodes
   // bit + idle padding). The decoder must buffer the partial frame across the two calls and emit once.
   const out: number[][] = [];
   const state: ArduinoboyState = {};
-  arduinoboyDecodeSerialOut([0x7f], state, (d) => out.push(d));
+  arduinoboyDecodeSerialOut([0x7f], [0], state, into(out));
   expect(out.length).toBe(0); // partial frame buffered, nothing decoded yet
-  arduinoboyDecodeSerialOut([0x80], state, (d) => out.push(d));
+  arduinoboyDecodeSerialOut([0x80], [0], state, into(out));
   expect(out).toEqual([[0xf8]]); // the straddling clock frame completes exactly once
 });
 
+test("framing: each message goes at the frame of the captured byte whose bits complete it", () => {
+  // START, clock, then a NoteOn whose command byte and value byte are two captured bytes: it completes at the value's.
+  const out: number[][] = [];
+  const frames: number[] = [];
+  const bytes = [frame(0x7d), frame(0x7f), frame(0x70), frame(0x69)];
+  arduinoboyDecodeSerialOut(bytes, [7, 15, 23, 31], {}, into(out, frames));
+  expect(out).toEqual([[0xfa], [0xf8], [0x90, 0x69, 0x7f]]);
+  expect(frames).toEqual([7, 15, 31]);
+});
+
+test("framing: a frame straddling two calls goes at its completing byte's frame in the second", () => {
+  // The straddling clock above, its first byte at frame 1000 of one block and its second at frame 3 of the next; and a
+  // NoteOn whose command ends one call and whose value starts the next, at the value's frame there.
+  const out: number[][] = [];
+  const frames: number[] = [];
+  const state: ArduinoboyState = {};
+  arduinoboyDecodeSerialOut([0x7f], [1000], state, into(out, frames));
+  arduinoboyDecodeSerialOut([0x80, frame(0x70)], [3, 11], state, into(out, frames));
+  arduinoboyDecodeSerialOut([frame(60)], [5], state, into(out, frames));
+  expect(out).toEqual([[0xf8], [0x90, 60, 0x7f]]);
+  expect(frames).toEqual([3, 5]);
+});
+
 // --- Master Sync (mode 8, SYNC=LSDJ): each byte a clock, a silence the stop ------------------------------------
 
 const RATE = 44100;
@@ -159,16 +191,23 @@ const FL_BUFFER = [256, 33, 1, 222, 255, 1, 1, 255, 7, 249];
 
 /** Decode `timeline` in the blocks `split` cuts `total` frames into, fed as the feedback wire feeds the decoder: a
  *  block's bytes reach it with the next block, at their frames in the block that produced them. Each message with the
- *  index of the block it went out in. */
-function masterSync(timeline: Timeline, split: (total: number) => number[], total: number, rate = RATE): { block: number; data: number[] }[] {
-  const out: { block: number; data: number[] }[] = [];
+ *  index of the block it went out in and the frame the decoder gave it, a frame of the block before. */
+function masterSync(
+  timeline: Timeline,
+  split: (total: number) => number[],
+  total: number,
+  rate = RATE,
+): { block: number; frame: number; data: number[] }[] {
+  const out: { block: number; frame: number; data: number[] }[] = [];
   const state: MasterSyncState = {};
   let start = 0;
   let next = 0;
   let bytes: number[] = [];
   let frames: number[] = [];
   split(total).forEach((n, block) => {
-    arduinoboyMasterSyncBlock(bytes, frames, { frames: n, sampleRate: rate }, state, (data) => out.push({ block, data }));
+    arduinoboyMasterSyncBlock(bytes, frames, { frames: n, sampleRate: rate }, state, (frame, len, b0, b1, b2) =>
+      out.push({ block, frame, data: [b0, b1, b2].slice(0, len) }),
+    );
     bytes = [];
     frames = [];
     for (; next < timeline.length && timeline[next][0] < start + n; next++) {
@@ -235,3 +274,32 @@ test("MasterSync: the checks before a byte and at a block start share the stop's
   for (const split of [even(1), cycle(FL_RAGGED), cycle(FL_BUFFER), even(1024), even(4096), even(8192)])
     expect(sent(masterSync(timeline, split, 40000))).toEqual(expected);
 });
+
+test("MasterSync: each byte's messages go at its frame, the run's NoteOn and 0xFA at its first byte's", () => {
+  // Bytes at frames 100, 960 and 1820 of 1024-frame blocks: each reaches the decoder in the block after its own.
+  const out = masterSync([[100, 5], [960, 5], [1820, 5]], even(1024), 4096);
+  expect(out).toEqual([
+    { block: 1, frame: 100, data: row(5) },
+    { block: 1, frame: 100, data: START },
+    { block: 1, frame: 100, data: CLOCK },
+    { block: 1, frame: 960, data: CLOCK },
+    { block: 2, frame: 796, data: CLOCK },
+  ]);
+});
+
+test("MasterSync: a stop found before a byte goes at that byte's frame, and one found at a block start at that start", () => {
+  // The silence after frame 100 passes 200 ms inside the 16384-frame block, so the byte at 16000 that ends it finds
+  // the stop, at its own frame, before its run's NoteOn. The second run's silence is found at a 16384-frame block's
+  // start: frame 16384 of the block before, its bytes' block.
+  const out = masterSync([[100, 1], [16000, 2]], even(16384), 49152);
+  expect(out).toEqual([
+    { block: 1, frame: 100, data: row(1) },
+    { block: 1, frame: 100, data: START },
+    { block: 1, frame: 100, data: CLOCK },
+    { block: 1, frame: 16000, data: STOP },
+    { block: 1, frame: 16000, data: row(2) },
+    { block: 1, frame: 16000, data: START },
+    { block: 1, frame: 16000, data: CLOCK },
+    { block: 2, frame: 16384, data: STOP },
+  ]);
+});
diff --git a/packages/retroplug/test/script/nodes/_rig.ts b/packages/retroplug/test/script/nodes/_rig.ts
index 3acdf818..8eb1e8fb 100644
--- a/packages/retroplug/test/script/nodes/_rig.ts
+++ b/packages/retroplug/test/script/nodes/_rig.ts
@@ -5,7 +5,8 @@
 // - messages, send and control take only a port the layout has, of the method's kind, and throw a TypeError for
 //   another, as the host does; every Block method throws once the process call it was handed to has returned.
 // - send copies the data (each value & 0xff) and records the frame twice: as sent, and as the port's queue holds it,
-//   raised to the last frame sent on that port this block.
+//   raised to the last frame sent on that port this block. It keeps what it was handed too (`sources`), so a test can
+//   tell an array a node reuses from one it makes.
 // - The messages a block hands out are the node's for that call only: their bytes are scribbled over once it returns,
 //   so a node that kept a view past the block reads garbage, as it would from the host's refilled buffers.
 import type { Block, ControlStream, DefinedNode, Message, NodeInstance } from "../../../src/graph/defineNode";
@@ -49,6 +50,8 @@ export class NodeRig<S> {
   creates = 1;
   /** The last block's sends, kept when its process threw. */
   last: Sent[] = [];
+  /** What each of the last block's sends was handed, index for index with `last`. */
+  sources: ArrayLike<number>[] = [];
 
   constructor(
     readonly def: DefinedNode<S>,
@@ -87,7 +90,9 @@ export class NodeRig<S> {
   /** One block, and every send it made. A throw from process is the caller's, the sends before it kept in `last`. */
   run(spec: BlockSpec): Sent[] {
     const sent: Sent[] = [];
+    const sources: ArrayLike<number>[] = [];
     this.last = sent;
+    this.sources = sources;
     const ports = this.layout.ports;
     const port = (id: unknown, dir: "in" | "out", kinds: readonly string[], what: string) => {
       const p = ports.find((q) => q.id === id && q.dir === dir && kinds.includes(q.kind));
@@ -122,6 +127,7 @@ export class NodeRig<S> {
         const queued = Math.max(at, lastFrame.get(id) ?? 0);
         lastFrame.set(id, queued);
         sent.push({ port: id, frame: at, queued, data: Array.from(data, (v) => v & 0xff), flags: flags >>> 0 });
+        sources.push(data);
         return true;
       },
       control: (id) => {
diff --git a/packages/retroplug/test/script/nodes/lsdj-sync.test.ts b/packages/retroplug/test/script/nodes/lsdj-sync.test.ts
index dfb4d0d1..cf96a8cc 100644
--- a/packages/retroplug/test/script/nodes/lsdj-sync.test.ts
+++ b/packages/retroplug/test/script/nodes/lsdj-sync.test.ts
@@ -255,6 +255,32 @@ test("leaving master sync drops its state, so coming back opens a fresh run", ()
   expect(block([{ frame: 0, data: [7] }])).toEqual([[0x90, 7, 0x7f], [0xfa], [0xf8]]);
 });
 
+test("MI.OUT and master sync send each message from an array the node made once, one a message length", () => {
+  // The rig keeps what send was handed: in a warm block the node passes the same arrays it passed in the first, one for
+  // each length a message has, so it makes none for a message it decodes.
+  const framed = (cmd: number, frame: number): In => ({ frame, data: [0x80 | (cmd & 0x7f)] });
+  // MI.OUT: a clock (1 byte), a NoteOn (3) and a program change (2), the same each block.
+  const miOut = [framed(0x7f, 7), framed(0x70, 15), framed(60, 23), framed(0x78, 31), framed(5, 39)];
+  // Master sync: a run's first block sends its NoteOn (3), 0xFA and a clock (1), and each block after a clock.
+  const master = (b: number): In[] => [{ frame: 100, data: [b === 0 ? 4 : 9] }];
+  for (const [mode, serialOut] of [["midiOut", () => miOut], ["masterSync", master]] as const) {
+    const rig = lsdj(mode);
+    const byLength = new Map<number, ArrayLike<number>>();
+    for (let b = 0; b < 4; b++) {
+      const sent = rig.run({ frames: 1024, inputs: { serialOut: serialOut(b) } });
+      expect(sent.length > 0).toBeTruthy();
+      sent.forEach((s, i) => {
+        const source = rig.sources[i];
+        expect(s.port).toBe("midiOut");
+        expect(source.length).toBe(s.data.length);
+        if (!byLength.has(source.length)) byLength.set(source.length, source);
+        expect(byLength.get(source.length) === source).toBeTruthy();
+      });
+    }
+    expect([...byLength.keys()].sort()).toEqual(mode === "midiOut" ? [1, 2, 3] : [1, 3]);
+  }
+});
+
 // --- state across settings changes ---------------------------------------------------------------------------------
 
 test("a settings change keeps the clock: a mode change mid-run does not restart the tick count", () => {
````

- [ ] **Step 2: Run them to see them fail**

Run: `pnpm test dsp/lsdj-arduinoboy; pnpm test script/nodes/lsdj-sync`
Expected: FAIL: `# 0 passed, 0 skipped, 26 failed` for `dsp/lsdj-arduinoboy` (every case reads its messages through the new emit, which the old decoders do not call so), then `# 22 passed, 0 skipped, 1 failed` for `script/nodes/lsdj-sync` (the case that MI.OUT and master sync send from arrays the node made once). That is what the replay of this plan printed when it was checked.

- [ ] **Step 3: Give the decoders the new emit**

`MidiEmit` is `(frame, len, b0, b1, b2)`; `arduinoboyDecodeByte` and `emitCommandValue` take a frame;
`arduinoboyDecodeSerialOut(bytes, frames, state, emit)` emits at `frames[i]` of the completing byte; master sync emits
a byte's messages at `frames[i]`, a stop found before a byte at it, and its end-of-block stop at `from` (the length of
the block its bytes came from: this block's start, in the bytes' frames), which keeps one call's frames in one
coordinate system and non-decreasing; Task 4 replaces that rule. `lsdjSync.ts`'s `emitMidiOut` fills a `Uint8Array` of
the message's length and still sends at frame 0:

````diff
diff --git a/packages/retroplug/src/lsdjArduinoboy.ts b/packages/retroplug/src/lsdjArduinoboy.ts
index 79617de2..bc42fc96 100644
--- a/packages/retroplug/src/lsdjArduinoboy.ts
+++ b/packages/retroplug/src/lsdjArduinoboy.ts
@@ -33,8 +33,10 @@ const MIDI_NOTE_OFF_BASE = 0x80;
 const MIDI_CC_BASE = 0xb0;
 const MIDI_PC_BASE = 0xc0;
 
-/** Emit one decoded MIDI message (raw status + data bytes). */
-export type MidiEmit = (data: number[]) => void;
+/** Emit one decoded MIDI message: its frame, its length (1 to 3 bytes) and its bytes, 0 past the length. A
+ *  message is handed over byte by byte, so a decoder builds nothing to emit one, and the caller sends it from what
+ *  it reuses. */
+export type MidiEmit = (frame: number, len: number, b0: number, b1: number, b2: number) => void;
 
 /** Cross-block decoder state (kept in the lsdj-sync node's scratch). Fields are created lazily by the
  *  decoders, so a fresh `{}` is a valid starting state. */
@@ -49,38 +51,39 @@ export interface ArduinoboyState {
   bitCount?: number;
 }
 
-// Complete a pending command with its value byte. `m` is the 0..11 command id (byte-0x70); channels are
-// 0-indexed (GB channel 0..3 → MIDI channel low nibble). Mirrors ArduinoboyMaster::emitCommandValue.
-function emitCommandValue(m: number, v: number, emit: MidiEmit): void {
+// Complete a pending command with its value byte, at the value byte's `frame`. `m` is the 0..11 command id
+// (byte-0x70); channels are 0-indexed (GB channel 0..3 → MIDI channel low nibble). Mirrors
+// ArduinoboyMaster::emitCommandValue.
+function emitCommandValue(m: number, v: number, frame: number, emit: MidiEmit): void {
   if (m < 4) {
     const ch = m;
     // value 0 → NoteOff. The firmware sends a NoteOff for the channel's most-recent note; without that
     // running state we emit NoteOff on note 0 — an unambiguous "channel quiet" signal downstream.
-    if (v === 0) emit([MIDI_NOTE_OFF_BASE | ch, 0, 0]);
-    else emit([MIDI_NOTE_ON_BASE | ch, v & 0x7f, 0x7f]);
+    if (v === 0) emit(frame, 3, MIDI_NOTE_OFF_BASE | ch, 0, 0);
+    else emit(frame, 3, MIDI_NOTE_ON_BASE | ch, v & 0x7f, 0x7f);
   } else if (m < 8) {
     const ch = m - 4;
     // Simplest of the firmware's CC-encoding modes: CC number = m, value = v (documented simplification).
-    emit([MIDI_CC_BASE | ch, m, v & 0x7f]);
+    emit(frame, 3, MIDI_CC_BASE | ch, m, v & 0x7f);
   } else if (m < 0x0c) {
     const ch = m - 8;
-    emit([MIDI_PC_BASE | ch, v & 0x7f]);
+    emit(frame, 2, MIDI_PC_BASE | ch, v & 0x7f, 0);
   }
   // m >= 0x0C: undefined per the firmware; drop.
 }
 
 /** Feed one already-de-framed protocol byte (0x00..0x7F) through the Arduinoboy state machine, pushing
- *  decoded MIDI into `emit`. The TS twin of ArduinoboyMaster::feed. */
-export function arduinoboyDecodeByte(byte: number, state: ArduinoboyState, emit: MidiEmit): void {
+ *  decoded MIDI into `emit` at `frame`, the byte's. The TS twin of ArduinoboyMaster::feed. */
+export function arduinoboyDecodeByte(byte: number, frame: number, state: ArduinoboyState, emit: MidiEmit): void {
   // Not part of the documented MI.OUT protocol — drop defensively without disturbing pending state.
   if (byte >= 0x80) return;
 
   // Realtime commands are single-byte and orthogonal to the command/value pairing — they can fire even
   // while we're waiting on a value byte without disturbing the wait.
   switch (byte) {
-    case REALTIME_CLOCK: emit([0xf8]); return;
-    case REALTIME_START: emit([0xfa]); return;
-    case REALTIME_STOP: emit([0xfc]); return;
+    case REALTIME_CLOCK: emit(frame, 1, 0xf8, 0, 0); return;
+    case REALTIME_START: emit(frame, 1, 0xfa, 0, 0); return;
+    case REALTIME_STOP: emit(frame, 1, 0xfc, 0, 0); return;
     default: break;
   }
 
@@ -92,7 +95,7 @@ export function arduinoboyDecodeByte(byte: number, state: ArduinoboyState, emit:
 
   // Value byte (0x00..0x6F). Only meaningful when a command is pending.
   if (state.pendingValueExpected) {
-    emitCommandValue(state.pendingCmd ?? 0, byte, emit);
+    emitCommandValue(state.pendingCmd ?? 0, byte, frame, emit);
     state.pendingValueExpected = false;
     state.pendingCmd = 0;
   }
@@ -106,10 +109,17 @@ export function arduinoboyReset(state: ArduinoboyState): void {
   state.bitCount = 0;
 }
 
-/** Decode a block's worth of RAW captured serial-out bytes: reconstruct the MSB-first bit stream,
- *  strip the flag-gated framing, and drive the byte protocol. Partial bits (a frame split across a
- *  block boundary) persist in `state` for the next call. */
-export function arduinoboyDecodeSerialOut(bytes: number[], state: ArduinoboyState, emit: MidiEmit): void {
+/** Decode a block's worth of RAW captured serial-out bytes, each with its frame (`frames`, index for index):
+ *  reconstruct the MSB-first bit stream, strip the flag-gated framing, and drive the byte protocol. Partial bits (a
+ *  frame split across a block boundary) persist in `state` for the next call. A message goes to `emit` at the frame of
+ *  the captured byte whose bits complete it, so a frame split across two calls goes at its completing byte's frame in
+ *  the later one. */
+export function arduinoboyDecodeSerialOut(
+  bytes: number[],
+  frames: number[],
+  state: ArduinoboyState,
+  emit: MidiEmit,
+): void {
   let acc = state.bitAcc ?? 0;
   let count = state.bitCount ?? 0;
 
@@ -130,7 +140,7 @@ export function arduinoboyDecodeSerialOut(bytes: number[], state: ArduinoboyStat
       if (count < 8) break; // flag says a 7-bit payload follows, but it hasn't fully arrived yet
       const cmd = (acc >>> (count - 8)) & 0x7f; // the 7 payload bits after the flag
       count -= 8;
-      arduinoboyDecodeByte(cmd, state, emit);
+      arduinoboyDecodeByte(cmd, frames[i], state, emit);
     }
 
     // Keep only the still-buffered low `count` bits so `acc` can't overflow a 32-bit shift over a long run.
@@ -156,9 +166,9 @@ export function arduinoboyDecodeSerialOut(bytes: number[], state: ArduinoboyStat
 // (docs/lsdj-master-sync-fix-design.md). So every byte is a clock, and a stopped LSDj goes quiet, which makes the
 // firmware's stop a silence. A playing master clocks one byte per 24-PPQN tick, 62.5 ms apart at LSDj's slowest tempo
 // (40 BPM), so a silence this long is a stop: about three times that gap. It is measured on the bytes' own frames, so
-// the message sequence does not depend on how the host cuts its blocks. The 0xFC's time does: like every message this
-// decoder sends, it goes out at frame 0, here of the first block that starts more than MASTER_SYNC_STOP_MS after the
-// last byte, so it is quantised to a block start and up to a block late.
+// the message sequence does not depend on how the host cuts its blocks. The 0xFC's time does: it is found at the first
+// block start more than MASTER_SYNC_STOP_MS after the last byte (or at the byte that ends the silence, when no block
+// starts between them), so it is quantised to a block start and up to a block late.
 const MASTER_SYNC_STOP_MS = 200;
 
 /** Cross-block Master Sync state (the lsdj-sync stage's `masterSync` sub-object, dropped whenever the mode is not
@@ -180,7 +190,8 @@ export interface MasterSyncBlock {
  *  bytes the lsdj-sync node read off its `serialOut` input and each one's frame, which are LAST block's bytes at
  *  their frames in it (the feedback wire's one block), and this block's length and rate. Each byte is a tempo clock,
  *  the first of a run opened with LSDj's song row; a run ends with one 0xFC once MASTER_SYNC_STOP_MS pass with no
- *  byte. */
+ *  byte. Every message goes to `emit` at a frame of the bytes' block: a byte's at its own frame, a stop found before a
+ *  byte at that byte's, and one found at this block's start at that start, one past the bytes' block's last frame. */
 export function arduinoboyMasterSyncBlock(
   bytes: number[],
   frames: number[],
@@ -197,23 +208,23 @@ export function arduinoboyMasterSyncBlock(
     // Checked only at a block start, the restart would carry on the old run (no 0xFC, no fresh NoteOn and 0xFA), so
     // the output would depend on the block split.
     if (state.started && at - (state.lastAt ?? 0) > stop) {
-      emit([0xfc]);
+      emit(frames[i], 1, 0xfc, 0, 0);
       state.started = false;
     }
     if (!state.started) {
       // First tick of a run: the byte value is LSDj's song row → NoteOn, then transport start, then this
       // byte's own clock tick (the firmware sends NoteOn + 0xFA before the first 0xF8).
       state.started = true;
-      emit([0x90 | ch, bytes[i] & 0x7f, 0x7f]);
-      emit([0xfa]);
+      emit(frames[i], 3, 0x90 | ch, bytes[i] & 0x7f, 0x7f);
+      emit(frames[i], 1, 0xfa, 0, 0);
     }
-    emit([0xf8]); // one MIDI clock per tempo byte
+    emit(frames[i], 1, 0xf8, 0, 0); // one MIDI clock per tempo byte
     state.lastAt = at;
   }
   // Every byte before this block's start is known: a run whose newest byte lies further back than the stop's
   // silence has ended.
   if (state.started && -(state.lastAt ?? 0) > stop) {
-    emit([0xfc]);
+    emit(from, 1, 0xfc, 0, 0);
     state.started = false;
   }
   if (state.started) state.lastAt = (state.lastAt ?? 0) - block.frames;
diff --git a/packages/retroplug/src/script/nodes/lsdjSync.ts b/packages/retroplug/src/script/nodes/lsdjSync.ts
index 6d58bfae..53ac63d4 100644
--- a/packages/retroplug/src/script/nodes/lsdjSync.ts
+++ b/packages/retroplug/src/script/nodes/lsdjSync.ts
@@ -13,7 +13,8 @@
 // press as [button, down] at frame 0; host MIDI out at frame 0. The frame-ordered queue raises a byte sent behind a
 // later one (Arduinoboy's START after a row byte, MI.MAP's clocks after its rows) as it raised the kernel's.
 //
-// Nothing is allocated in a block but what the decoders allocate for the MIDI they emit, as under the kernel.
+// Nothing is allocated in a warm block: the decoders hand each message's bytes to emitMidiOut, which sends them from
+// arrays made with the instance.
 import { bytes, defineNode, events, type Block, type Message } from "../../graph/defineNode";
 import { s, type Infer } from "../../graph/settingsSchema";
 import { LSDJ_MODE_VALUES, LsdjSyncMode } from "../../settingsEnums";
@@ -129,8 +130,18 @@ export const lsdjSyncNode = defineNode<LsdjSyncSettings>({
       press[1] = down ? 1 : 0;
       block!.send("buttons", 0, press);
     };
-    // Frame 0: MI.OUT and master sync carry no intra-block timing, and the host timestamps by block.
-    const emitMidiOut = (data: number[]) => {
+    // A decoded message, at frame 0: the decoders give each its byte's frame, but that is a frame of last block, which
+    // the feedback wire brought, so the host timestamps it by block. A message is 1 to 3 bytes and send sends all of
+    // the array it is given, so each length has an array of its own, made once here; send copies a message into the
+    // port's queue, so its array is free again when send returns.
+    const out1 = new Uint8Array(1);
+    const out2 = new Uint8Array(2);
+    const out3 = new Uint8Array(3);
+    const emitMidiOut = (_frame: number, len: number, b0: number, b1: number, b2: number) => {
+      const data = len === 1 ? out1 : len === 2 ? out2 : out3;
+      data[0] = b0;
+      if (len > 1) data[1] = b1;
+      if (len > 2) data[2] = b2;
       block!.send("midiOut", 0, data);
     };
     // Each inbox event, through `handle`: the routed MIDI, then the launches.
@@ -260,7 +271,7 @@ export const lsdjSyncNode = defineNode<LsdjSyncSettings>({
           eachEvent(passthroughEvent);
           break;
         case LsdjSyncMode.MidiOut: // ArduinoboyMaster / MIDIOUT: LSDj's MI.OUT stream, decoded to host MIDI
-          if (serialOut.length > 0) arduinoboyDecodeSerialOut(serialOut, st, emitMidiOut);
+          if (serialOut.length > 0) arduinoboyDecodeSerialOut(serialOut, serialOutFrames, st, emitMidiOut);
           break;
         case LsdjSyncMode.MasterSync: // LSDj drives the host clock: a byte a clock, a silence the stop
           arduinoboyMasterSyncBlock(serialOut, serialOutFrames, clock.block, (st.masterSync ??= {}), emitMidiOut);
````

- [ ] **Step 4: Run them to see them pass**

Run: `pnpm test dsp/lsdj-arduinoboy; pnpm test script/nodes/lsdj-sync`
Expected: `26 passed, 0 skipped, 0 failed`, then `23 passed, 0 skipped, 0 failed`.

- [ ] **Step 5: Hold the warm block to zero allocations in the profile bench**

`dsp-bench` gains, after its mGB window, one window for each decoding mode, with LSDj playing: master sync on
`lsdj9_4_2.gb` (capture `listen`) and MI.OUT on `lsdj9_3_3-arduinoboy.gb` (capture `drive`). Each warms up for 200
blocks, measures `min(blocks, 400)`, asserts zero allocations a block (gated on the default warmup, as the mGB window
is), proves LSDj played by its clock count (more than 40 a second in master sync, 2 in MI.OUT), and then proves the
counters live as the mGB window does (`expectWindowLive()`, factored out of it). Without the fixture ROMs the case
skips at the first LSDj window, after the mGB window has passed, so the skip baseline holds:

````diff
diff --git a/packages/retroplug/test-native/dsp-bench.test.ts b/packages/retroplug/test-native/dsp-bench.test.ts
index 5e8c1130..474ee8d5 100644
--- a/packages/retroplug/test-native/dsp-bench.test.ts
+++ b/packages/retroplug/test-native/dsp-bench.test.ts
@@ -2,7 +2,9 @@
 // rp.mgb.midi a system) off the RT thread via the deterministic renderAudio pull path under a seeded mGB + heavy-MIDI
 // workload, and reports what the script host's audio runtime (the bare QuickJS runtime they run in) allocates PER
 // BLOCK: the refcount-churn signal (QuickJS frees acyclic garbage immediately, so "GC" is rare; malloc/free churn
-// per block is the real cost).
+// per block is the real cost). Then a window each on a real LSDj, playing, whose rp.lsdj.sync decodes its serial out
+// to host MIDI, in master sync and in MI.OUT: those need the fixture tree's LSDj ROMs (RETROPLUG_RESOURCES_DIR), and
+// without them the case SKIPs there, after the mGB window has been held to its zero.
 //
 // The allocation counters exist only in a RETROPLUG_PROFILE host (built by `pnpm profile` /
 // tools/run-profile.sh). Under the default host the counters report enabled:false and this
@@ -11,8 +13,12 @@
 // native Engine is shared per host process).
 import { test, expect, skip } from "../testing/harness";
 import { createAudioDriver } from "../src/audioDriver";
-import type { ConstructSpec } from "../src/backend";
+import type { ConstructSpec, SerialOutCapture } from "../src/backend";
 import { directGraph } from "../src/graph/directGraph";
+import { savFrom, type SavInput } from "../src/lsdjSav";
+import { createRealBackend } from "../src/realBackend";
+
+declare const __RESOURCES_DIR__: string;
 
 // Structure for this file's directly built systems (docs/modular-graph-phase-b2-design.md section 2.4).
 const graph = directGraph();
@@ -20,6 +26,40 @@ const graph = directGraph();
 // The embedded mGB, built bare (no roles of its own): the pipeline set here is its one protocol node.
 const MGB: ConstructSpec = { romPath: "", platform: "gb", core: "sameboy", embeddedRom: "mgb", savPath: null, statePath: null };
 
+// The LSDj windows' ROMs (the fixture tree's) and songs, as lsdj-mastersync.test.ts and lsdj-midiout.test.ts author
+// them: a stock LSDj in SYNC=LSDj playing a one-note phrase, and an Arduinoboy build in SYNC=MI.OUT playing eight
+// notes, each with an N command, which MI.OUT sends as a NoteOn.
+const LSDJ_ROM = __RESOURCES_DIR__ + "/roms/lsdj/lsdj9_4_2.gb";
+const LSDJ_ABOY_ROM = __RESOURCES_DIR__ + "/roms/lsdj/lsdj9_3_3-arduinoboy.gb";
+const LSDJ_START = 7; // GameboyButton::Start
+const MASTER_SONG: SavInput = {
+  workingSong: {
+    formatVersion: 22,
+    settings: { syncMode: "Lsdj" },
+    rows: [{ chains: [0] }],
+    chains: [{ phrases: [0] }],
+    phrases: [{ notes: [1], instruments: [0] }],
+    instruments: [{ type: "pulse", panning: "LeftRight", adsr: { initialLevel: 8, attackSpeed: 8 } }],
+  },
+};
+const MIDIOUT_SONG: SavInput = {
+  workingSong: {
+    formatVersion: 22,
+    settings: { syncMode: "MidiOut" },
+    rows: [{ chains: [0] }],
+    chains: [{ phrases: [0] }],
+    phrases: [
+      {
+        notes: [1, 13, 25, 1, 13, 25, 1, 13],
+        instruments: [0, 0, 0, 0, 0, 0, 0, 0],
+        commands: ["N", "N", "N", "N", "N", "N", "N", "N"],
+        commandValues: [48, 52, 55, 60, 64, 67, 72, 36],
+      },
+    ],
+    instruments: [{ type: "pulse", panning: "LeftRight", adsr: { initialLevel: 8, attackSpeed: 8 } }],
+  },
+};
+
 const SR = 44100;
 const BLK = 1024;
 const DEFAULT_WARMUP = 200; // blocks discarded before the window; the zero-allocation assertion needs at least this
@@ -245,13 +285,84 @@ test("dsp-bench: the script nodes' per-block allocation under an mGB + MIDI work
   // a node calls messages() (ScriptHost.cpp): for the new rp.mgb.midi, and for rp.midi.route, whose reshape freed its
   // port arrays when its outputs grew. Pre-making those arrays at create time would move that allocation out of the
   // block, and this assertion would then need another trigger. The zero above is only worth printing if this can fail.
-  audio.dspResetAllocStats(true);
   const extra = cores + 1;
-  expect(graph.add(MGB, extra)).toBeTruthy();
-  expect(graph.setPipeline(extra, [{ kind: "mgb", config: {} }])).toBeTruthy();
-  audio.renderAudio(BLOCK_MS);
-  const live = audio.dspAllocStats();
-  expect(live.blockCount, "blocks in the proof window").toBeGreaterThan(0);
-  expect(live.allocCalls, "a block whose fresh ports fill their message arrays allocates").toBeGreaterThan(0);
-  expect(live.maxBlockAllocCalls, "and the worst-block counter sees it").toBeGreaterThan(0);
+  const expectWindowLive = (): void => {
+    audio.dspResetAllocStats(true);
+    expect(graph.add(MGB, extra)).toBeTruthy();
+    expect(graph.setPipeline(extra, [{ kind: "mgb", config: {} }])).toBeTruthy();
+    audio.renderAudio(BLOCK_MS);
+    const live = audio.dspAllocStats();
+    expect(live.blockCount, "blocks in the proof window").toBeGreaterThan(0);
+    expect(live.allocCalls, "a block whose fresh ports fill their message arrays allocates").toBeGreaterThan(0);
+    expect(live.maxBlockAllocCalls, "and the worst-block counter sees it").toBeGreaterThan(0);
+    graph.remove(extra);
+  };
+  expectWindowLive();
+
+  // --- an LSDj decoding its serial out to host MIDI, playing ------------------------------------------------------
+  // rp.lsdj.sync's two MIDI-out modes run their decoders only on what a playing LSDj clocks out, so each has a window
+  // of its own on a real LSDj, the mGB systems gone: master sync on a stock LSDj in SYNC=LSDj (a byte a 24-PPQN tick,
+  // which the core captures in `listen`, as lsdj-mastersync.test.ts runs it), and MI.OUT on an Arduinoboy build in
+  // SYNC=MI.OUT (its clock and the N commands' NoteOns, captured in `drive`, as lsdj-midiout.test.ts runs it). Each
+  // window is held to the mGB window's zero, the clocks its decoder sent say LSDj played through it, and the same proof
+  // as above says the window counted.
+  for (const id of ids) graph.remove(id);
+  const be = createRealBackend();
+  const lsdjBlocks = Math.min(blocks, 400);
+  const lsdjWindow = (
+    workload: string,
+    romPath: string,
+    song: SavInput,
+    mode: string,
+    capture: SerialOutCapture,
+    minClocks: number,
+  ): void => {
+    if (!be.fileExists(romPath)) skip(`dsp-bench: the ${workload} window needs the LSDj ROM at ${romPath}`);
+    const id = extra + 1;
+    const spec: ConstructSpec = { romPath, platform: "gb", core: "sameboy", embeddedRom: "", savPath: null, statePath: null };
+    expect(graph.add({ ...spec, sramBytes: savFrom(song) }, id)).toBeTruthy();
+    expect(graph.setPipeline(id, [{ kind: "lsdj-sync", config: { mode } }])).toBeTruthy();
+    expect(be.setSerialOutCapture(id, capture)).toBeTruthy(); // what the store does for the mode
+    audio.renderAudio(6000); // the song screen
+    audio.pressButton(id, LSDJ_START, true); // play
+    audio.renderAudio(80);
+    audio.pressButton(id, LSDJ_START, false);
+    for (let b = 0; b < warmup; b++) audio.renderAudio(BLOCK_MS);
+    audio.drainMidiOut();
+    audio.dspResetAllocStats(true);
+    for (let b = 0; b < lsdjBlocks; b++) audio.renderAudio(BLOCK_MS);
+    const w = audio.dspAllocStats();
+    const status = audio.drainMidiOut().map((m) => m.data[0]);
+    const clocks = status.filter((st) => st === 0xf8).length;
+    const notes = status.filter((st) => (st & 0xf0) === 0x90).length;
+    const wb = w.blockCount || 1;
+    console.log(
+      "DSP-BENCH " +
+        JSON.stringify({
+          workload,
+          warmupBlocks: warmup,
+          blocks: w.blockCount,
+          allocsPerBlock: round(w.allocCalls / wb),
+          bytesPerBlock: round(w.allocBytes / wb),
+          maxBlockAllocs: w.maxBlockAllocCalls,
+          maxBlockBytes: w.maxBlockAllocBytes,
+          midiOut: status.length,
+          clocks,
+          noteOns: notes,
+        }),
+    );
+    expect(w.blockCount, `${workload}: blocks measured`).toBe(lsdjBlocks);
+    // LSDj played through the window: its decoder sent more clocks than its tempo sends in half of it (in master sync
+    // about 51 a second at tempo 128, a byte a 24-PPQN tick, against the 40 asked; in MI.OUT about 4, against 2).
+    expect(clocks, `${workload}: clocks decoded in the window`).toBeGreaterThan(minClocks);
+    if (warmup >= DEFAULT_WARMUP) {
+      expect(w.allocCalls, `${workload}: allocations in the measured window`).toBe(0);
+      expect(w.maxBlockAllocCalls, `${workload}: allocations in the worst block`).toBe(0);
+    }
+    graph.remove(id);
+    expectWindowLive();
+  };
+  const seconds = (lsdjBlocks * BLOCK_MS) / 1000;
+  lsdjWindow("lsdj-masterSync", LSDJ_ROM, MASTER_SONG, "masterSync", "listen", 40 * seconds);
+  lsdjWindow("lsdj-midiOut", LSDJ_ABOY_ROM, MIDIOUT_SONG, "midiOut", "drive", 2 * seconds);
 });
````

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources tools/run-profile.sh stats`
Expected: `DSP-BENCH {"profile":"A","cores":1,"warmupBlocks":200,"blocks":2000,"allocsPerBlock":0,"bytesPerBlock":0,"freesPerBlock":0,"liveHeapDelta":0,"peakLive":309072,"maxBlockAllocs":0,"maxBlockBytes":0,"gcMs":0.07,"gcFreedBytes":0,"xRT":16.31}`, then `DSP-BENCH {"workload":"lsdj-masterSync","warmupBlocks":200,"blocks":400,"allocsPerBlock":0,"bytesPerBlock":0,"maxBlockAllocs":0,"maxBlockBytes":0,"midiOut":475,"clocks":475,"noteOns":0}` and `DSP-BENCH {"workload":"lsdj-midiOut","warmupBlocks":200,"blocks":400,"allocsPerBlock":0,"bytesPerBlock":0,"maxBlockAllocs":0,"maxBlockBytes":0,"midiOut":60,"clocks":40,"noteOns":20}`: no allocation in any block. With the old emit the prototype's run measured 3.56 allocations a block in master sync (about three a message) and 0.44 in MI.OUT.

- [ ] **Step 6: Run the suites and parity**

Run: `pnpm typecheck && pnpm test`
Expected: `packages/retroplug typecheck: Done`, then `# 166 test file(s) passed (jobs=16)`.

Run: `pnpm test:plugin`
Expected: `# 10 plugin test binary(ies) passed (jobs=16)`, the engine-graph binary's `All tests passed (15484 assertions in 120 test cases)` (`ProtocolNodes.test.cpp`'s MI.OUT and master-sync digests still hold: the node sends what it sent).

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=16)`, none skipped.

Run: `pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=16)`, with `46 case(s) SKIPPED in 10 file(s)` (every fixture group's cases, as CI runs them) and the skip check clean.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native lsdj`
Expected: `# 22 native test file(s) passed (jobs=16)`.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native dsp`
Expected: `# 16 native test file(s) passed (jobs=16)`, with `1 case(s) SKIPPED in 1 file(s): dsp-bench(1)` (the bench runs only in the profile build).

- [ ] **Step 7: Commit**

````bash
git add -- packages/retroplug/src/lsdjArduinoboy.ts packages/retroplug/src/script/nodes/lsdjSync.ts \
  packages/retroplug/test-native/dsp-bench.test.ts packages/retroplug/test/dsp/lsdj-arduinoboy.test.ts \
  packages/retroplug/test/script/nodes/_rig.ts packages/retroplug/test/script/nodes/lsdj-sync.test.ts
git commit -F - <<'EOF'
feat(lsdj): the MI.OUT and master-sync decoders emit each message with its frame into reused arrays

MidiEmit is (frame, len, b0, b1, b2): the decoders build no array per message. arduinoboyDecodeSerialOut takes each
byte's frame and emits a message at the frame of the byte whose bits complete it; arduinoboyMasterSyncBlock emits a
byte's messages at its frame, a stop found before a byte at that byte's and one found at a block start at that
start. Every decision is as it was: same messages, same order. rp.lsdj.sync sends them from three arrays made with
the instance, still at frame 0 (its frames are last block's), so parity is unchanged.

dsp-bench gains a playing LSDj window each for master sync and MI.OUT, held to zero allocations a block with the
mGB window's liveness proof: an allocating emit puts 3.56 and 0.44 allocations a block in them. The node test holds
the arrays' reuse.
EOF
````

### Task 4: rp.lsdj.serial-out decodes MI.OUT and master sync after the Game Boy

Design sections 3, 4, 7 and 8. The decoders move into a node of their own, placed after the Game Boy and reading its
`serialOut` on a forward wire in the same block, so each message goes out at its byte's own frame in the block that
byte was captured; master sync's stop goes at its mark, the first frame its silence passes 200 ms. `rp.lsdj.sync`
keeps the host-to-Game Boy direction. This task re-records exactly three parity groups, the role goldens and three
digests of `ProtocolNodes.test.cpp`, and proves what the re-record moved.

**Files:** 29 files, by step below.
- Create: `packages/retroplug/src/script/nodes/lsdjSerialOut.ts`, `packages/retroplug/src/script/nodes/lsdjSyncSettings.ts`, `packages/retroplug/test/script/nodes/lsdj-serial-out.test.ts`
- Modify: 26 files

- [ ] **Step 1: Before changing anything, record what the old code sends**

The re-record is judged against the old code's own output, so take it now, on Task 3's tree: copy the three baselines
and dump their cases (`RP_PARITY_DUMP` writes each case's lanes and MIDI out; see `test-native/parity/group.ts`). The
checker in a later step reads these:

Run: `mkdir -p /tmp/lsdj-serial-out/baseline-before && cp packages/retroplug/test-native/parity/baseline/{lsdj-modes,blocks-tiny-lsdj-master,blocks-tiny-lsdj}.json /tmp/lsdj-serial-out/baseline-before/ && cmake --build build --target retroplug-host -j$(nproc)`
Expected: nothing printed but the build's progress.

Run: `RP_PARITY_DUMP=/tmp/lsdj-serial-out/dump-before RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity/lsdj-modes`
Expected: `# 1 native test file(s) passed (jobs=16)`.

Run: `RP_PARITY_DUMP=/tmp/lsdj-serial-out/dump-before RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity/blocks-tiny-lsdj-master`
Expected: `# 1 native test file(s) passed (jobs=16)`.

Run: `RP_PARITY_DUMP=/tmp/lsdj-serial-out/dump-before RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity/blocks-tiny-lsdj`
Expected: `# 1 native test file(s) passed (jobs=16)`.

- [ ] **Step 2: Write the failing decoder tests**

Master sync's stop at its mark mid-block; at a block's first frame (a mark at the previous block's length); carried
across a block with no bytes; a restart in the block of its stop (0xFC at the mark, then NoteOn, 0xFA, 0xF8 at the
byte); exactly 200 ms is no stop and a frame more is. And the property the design rests on, that the decoders add no
split dependence of their own: one absolute byte stream (runs, silences past 200 ms, restarts; for MI.OUT, commands
straddling blocks) fed through 1-frame, ragged and 1024-frame block sequences yields the identical list of (absolute
frame, bytes) in every split:

````diff
diff --git a/packages/retroplug/test/dsp/lsdj-arduinoboy.test.ts b/packages/retroplug/test/dsp/lsdj-arduinoboy.test.ts
index 5115168e..d05fe5f1 100644
--- a/packages/retroplug/test/dsp/lsdj-arduinoboy.test.ts
+++ b/packages/retroplug/test/dsp/lsdj-arduinoboy.test.ts
@@ -5,8 +5,9 @@
 //   - arduinoboyDecodeSerialOut: the flag-gated framing in front of it — the raw captured bytes LSDj
 //     actually emits (each frame = 1 flag bit + 7 payload bits, MSB-first), decoded to the same MIDI.
 // Then master sync's decoder, arduinoboyMasterSyncBlock. Each decoder hands every message to its emit with its byte's
-// frame. The rp.lsdj.sync node that runs them off its Game Boy's serial out (modes 7 and 8) is
-// test/script/nodes/lsdj-sync.test.ts's.
+// frame, and adds no dependence on the host's block split of its own: one byte stream cut into any blocks sends the
+// same messages at the same absolute frames. The rp.lsdj.serial-out node that runs them off its Game Boy's serial out
+// (modes 7 and 8) is test/script/nodes/lsdj-serial-out.test.ts's.
 import { test, expect } from "../../testing/harness";
 import {
   arduinoboyDecodeByte,
@@ -189,36 +190,40 @@ const cycle = (sizes: number[]) => (total: number): number[] => {
 const FL_RAGGED = [33, 1, 222, 1, 7, 4096, 2];
 const FL_BUFFER = [256, 33, 1, 222, 255, 1, 1, 255, 7, 249];
 
-/** Decode `timeline` in the blocks `split` cuts `total` frames into, fed as the feedback wire feeds the decoder: a
- *  block's bytes reach it with the next block, at their frames in the block that produced them. Each message with the
- *  index of the block it went out in and the frame the decoder gave it, a frame of the block before. */
-function masterSync(
-  timeline: Timeline,
-  split: (total: number) => number[],
-  total: number,
-  rate = RATE,
-): { block: number; frame: number; data: number[] }[] {
-  const out: { block: number; frame: number; data: number[] }[] = [];
+/** A message as a split decoder sent it: the block it went out in, its frame there, that frame counted from the
+ *  timeline's start, and whether the frame lies inside its block. */
+interface Sent {
+  block: number;
+  frame: number;
+  at: number;
+  inside: boolean;
+  data: number[];
+}
+
+/** Decode `timeline` in the blocks `split` cuts `total` frames into, each block's bytes handed to the decoder in that
+ *  block at their frames in it, as rp.lsdj.serial-out reads its Game Boy's serial out after the system. */
+function masterSync(timeline: Timeline, split: (total: number) => number[], total: number, rate = RATE): Sent[] {
+  const out: Sent[] = [];
   const state: MasterSyncState = {};
   let start = 0;
   let next = 0;
-  let bytes: number[] = [];
-  let frames: number[] = [];
   split(total).forEach((n, block) => {
-    arduinoboyMasterSyncBlock(bytes, frames, { frames: n, sampleRate: rate }, state, (frame, len, b0, b1, b2) =>
-      out.push({ block, frame, data: [b0, b1, b2].slice(0, len) }),
-    );
-    bytes = [];
-    frames = [];
+    const bytes: number[] = [];
+    const frames: number[] = [];
     for (; next < timeline.length && timeline[next][0] < start + n; next++) {
       frames.push(timeline[next][0] - start);
       bytes.push(timeline[next][1]);
     }
+    arduinoboyMasterSyncBlock(bytes, frames, { frames: n, sampleRate: rate }, state, (frame, len, b0, b1, b2) =>
+      out.push({ block, frame, at: start + frame, inside: frame >= 0 && frame < n, data: [b0, b1, b2].slice(0, len) }),
+    );
     start += n;
   });
   return out;
 }
 const sent = (out: { data: number[] }[]): number[][] => out.map((m) => m.data);
+/** Each message at its absolute frame. */
+const timed = (out: Sent[]): [number, number[]][] => out.map((m) => [m.at, m.data]);
 
 test("MasterSync: every byte is a clock, and the first opens the run with the song-row NoteOn and 0xFA", () => {
   expect(sent(masterSync(clocks(100, 3, 5), even(1024), 4096))).toEqual([row(5), START, CLOCK, CLOCK, CLOCK]);
@@ -235,11 +240,11 @@ test("MasterSync: before any byte, and while stopped, nothing is sent", () => {
   expect(sent(masterSync([[100, 9]], even(1024), 40000))).toEqual([row(9), START, CLOCK, STOP]);
 });
 
-test("MasterSync: the 0xFC goes out in the first block that starts more than 200 ms after the last byte", () => {
-  const stopBlock = (out: { block: number; data: number[] }[]) => out.find((m) => m.data[0] === 0xfc)?.block;
-  // A byte at frame 100: the stop is due after frame 100 + STOP_FRAMES = 8920.
-  expect(stopBlock(masterSync([[100, 9]], even(1024), 20000))).toBe(Math.floor((100 + STOP_FRAMES) / 1024) + 1); // 9216
-  expect(stopBlock(masterSync([[100, 9]], even(1), 10000))).toBe(100 + STOP_FRAMES + 1);
+test("MasterSync: the 0xFC goes at the run's mark, the first frame its silence passes 200 ms, in the block that holds it", () => {
+  // A byte at frame 100: the silence passes STOP_FRAMES after frame 100 + STOP_FRAMES = 8920, so the mark is 8921.
+  const stopOf = (out: Sent[]) => out.find((m) => m.data[0] === 0xfc);
+  expect(stopOf(masterSync([[100, 9]], even(1024), 20000))).toEqual({ block: 8, frame: 729, at: 8921, inside: true, data: STOP });
+  expect(stopOf(masterSync([[100, 9]], even(1), 10000))).toEqual({ block: 8921, frame: 0, at: 8921, inside: true, data: STOP });
 });
 
 test("MasterSync: the silence is time, so 10000 frames is a stop at 44.1 kHz and not at 96 kHz", () => {
@@ -257,49 +262,205 @@ test("MasterSync: one timeline decodes to the same messages however the host cut
 });
 
 test("MasterSync: a silence that ends inside one call still stops the run, however the host cuts its blocks", () => {
-  // The silence before frame 18100 passes 200 ms at frame 16920, and no 4096- or 8192-frame block starts between
-  // that and the byte that ends it, so the byte reaches the decoder in the same call as the silence's end.
+  // The silence before frame 18100 passes 200 ms at frame 16921, and no 4096- or 8192-frame block ends between that
+  // and the byte that ends it, so the byte reaches the decoder in the same call as the silence's mark.
   const timeline: Timeline = [[100, 1], [8100, 1], [18100, 2], [40000, 3]];
   const expected = [row(1), START, CLOCK, CLOCK, STOP, row(2), START, CLOCK, STOP, row(3), START, CLOCK, STOP];
   for (const split of [even(1), cycle(FL_RAGGED), cycle(FL_BUFFER), even(1024), even(4096), even(8192)])
     expect(sent(masterSync(timeline, split, 60000))).toEqual(expected);
 });
 
-test("MasterSync: the checks before a byte and at a block start share the stop's boundary: exactly 200 ms is no stop, a frame more is", () => {
-  // The second byte comes exactly STOP_FRAMES after the first, the third STOP_FRAMES + 1 after the second. In 1-frame
-  // blocks a block starts at each mark, so the block-start check meets both; in longer ones the check before each
-  // byte does.
+test("MasterSync: the checks before a byte and at a block's end share the stop's boundary: exactly 200 ms is no stop, a frame more is", () => {
+  // The second byte comes exactly STOP_FRAMES after the first, before its mark; the third STOP_FRAMES + 1 after the
+  // second, on its mark. So the check before the third sends the stop, at the third's frame, wherever the blocks are
+  // cut: a block that ends on the mark carries it to the next, whose first frame the third byte is.
   const timeline: Timeline = [[100, 1], [100 + STOP_FRAMES, 1], [100 + 2 * STOP_FRAMES + 1, 2]];
   const expected = [row(1), START, CLOCK, CLOCK, STOP, row(2), START, CLOCK, STOP];
-  for (const split of [even(1), cycle(FL_RAGGED), cycle(FL_BUFFER), even(1024), even(4096), even(8192)])
-    expect(sent(masterSync(timeline, split, 40000))).toEqual(expected);
+  for (const split of [even(1), cycle(FL_RAGGED), cycle(FL_BUFFER), even(1024), even(4096), even(8192)]) {
+    const out = masterSync(timeline, split, 40000);
+    expect(sent(out)).toEqual(expected);
+    expect(out.filter((m) => m.data[0] === 0xfc).map((m) => m.at)).toEqual([100 + 2 * STOP_FRAMES + 1, 100 + 3 * STOP_FRAMES + 2]);
+  }
 });
 
-test("MasterSync: each byte's messages go at its frame, the run's NoteOn and 0xFA at its first byte's", () => {
-  // Bytes at frames 100, 960 and 1820 of 1024-frame blocks: each reaches the decoder in the block after its own.
+test("MasterSync: each byte's messages go at its frame, in its own block, the run's NoteOn and 0xFA at its first byte's", () => {
+  // Bytes at frames 100, 960 and 1820 of 1024-frame blocks: each reaches the decoder in the block that captured it.
   const out = masterSync([[100, 5], [960, 5], [1820, 5]], even(1024), 4096);
-  expect(out).toEqual([
-    { block: 1, frame: 100, data: row(5) },
-    { block: 1, frame: 100, data: START },
-    { block: 1, frame: 100, data: CLOCK },
-    { block: 1, frame: 960, data: CLOCK },
-    { block: 2, frame: 796, data: CLOCK },
+  expect(out.slice(0, 5).map(({ block, frame, data }) => ({ block, frame, data }))).toEqual([
+    { block: 0, frame: 100, data: row(5) },
+    { block: 0, frame: 100, data: START },
+    { block: 0, frame: 100, data: CLOCK },
+    { block: 0, frame: 960, data: CLOCK },
+    { block: 1, frame: 796, data: CLOCK },
   ]);
 });
 
-test("MasterSync: a stop found before a byte goes at that byte's frame, and one found at a block start at that start", () => {
-  // The silence after frame 100 passes 200 ms inside the 16384-frame block, so the byte at 16000 that ends it finds
-  // the stop, at its own frame, before its run's NoteOn. The second run's silence is found at a 16384-frame block's
-  // start: frame 16384 of the block before, its bytes' block.
-  const out = masterSync([[100, 1], [16000, 2]], even(16384), 49152);
-  expect(out).toEqual([
-    { block: 1, frame: 100, data: row(1) },
-    { block: 1, frame: 100, data: START },
-    { block: 1, frame: 100, data: CLOCK },
-    { block: 1, frame: 16000, data: STOP },
-    { block: 1, frame: 16000, data: row(2) },
-    { block: 1, frame: 16000, data: START },
-    { block: 1, frame: 16000, data: CLOCK },
-    { block: 2, frame: 16384, data: STOP },
+test("MasterSync: a run stops at its mark mid-block", () => {
+  // One byte at frame 100 of a 16384-frame block: its mark, 8921, is inside the same block.
+  expect(masterSync([[100, 1]], even(16384), 16384)).toEqual([
+    { block: 0, frame: 100, at: 100, inside: true, data: row(1) },
+    { block: 0, frame: 100, at: 100, inside: true, data: START },
+    { block: 0, frame: 100, at: 100, inside: true, data: CLOCK },
+    { block: 0, frame: 8921, at: 8921, inside: true, data: STOP },
+  ]);
+});
+
+test("MasterSync: a mark at a block's length goes at the next block's first frame", () => {
+  // A byte at frame 395: its mark is 9216, block 8's length past its start, so block 8 carries it and block 9 sends it
+  // at its frame 0, with no byte there to send it before.
+  const out = masterSync([[395, 1]], even(1024), 12288);
+  expect(out[out.length - 1]).toEqual({ block: 9, frame: 0, at: 9216, inside: true, data: STOP });
+  expect(out.length).toBe(4);
+});
+
+test("MasterSync: a run's mark is carried across a block with no bytes", () => {
+  // A byte at frame 100 of a 4096-frame block: block 1 holds no byte and not the mark, which it carries; block 2
+  // sends it at 8921 - 8192.
+  const out = masterSync([[100, 1]], even(4096), 16384);
+  expect(out.map(({ block, frame, data }) => [block, frame, data])).toEqual([
+    [0, 100, row(1)],
+    [0, 100, START],
+    [0, 100, CLOCK],
+    [2, 729, STOP],
+  ]);
+});
+
+test("MasterSync: a restart in the block of its stop sends the 0xFC at the mark, then the NoteOn, 0xFA and 0xF8 at the byte", () => {
+  // The silence after frame 100 passes 200 ms at 8921, inside the 16384-frame block whose byte at 16000 ends it: the
+  // check before that byte sends the stop at the mark, and the byte opens a fresh run at its own frame. That run's
+  // mark, 24821, is past the block, so the next block sends it, at 24821 - 16384.
+  expect(masterSync([[100, 1], [16000, 2]], even(16384), 49152).map(({ block, frame, data }) => [block, frame, data])).toEqual([
+    [0, 100, row(1)],
+    [0, 100, START],
+    [0, 100, CLOCK],
+    [0, 8921, STOP],
+    [0, 16000, row(2)],
+    [0, 16000, START],
+    [0, 16000, CLOCK],
+    [1, 8437, STOP],
+  ]);
+});
+
+// --- the decoders add no split dependence of their own ----------------------------------------------------------
+//
+// A core's byte timing moves with the host's block split (test-native/lsdj-mastersync.test.ts says so), and that is the
+// core's. What the decoders are held to here is that they add none: one byte stream at absolute frames, cut into
+// 1-frame blocks, FL-style ragged ones (some longer than a run's gaps, some shorter), a 256-frame buffer split at
+// automation points and 1024-frame blocks, sends the same messages at the same absolute frames, each inside the block
+// it goes out in.
+
+/** Block lists the property cases cut a stream into: ragged lengths below and above a clock's 860-frame gap and the
+ *  stop's 8820, among them. */
+const SPLITS: [string, (total: number) => number[]][] = [
+  ["1", even(1)],
+  ["fl", cycle(FL_RAGGED)],
+  ["fl256", cycle(FL_BUFFER)],
+  ["ragged", cycle([5000, 13, 777, 9000, 1, 861, 12000, 3])],
+  ["1024", even(1024)],
+];
+
+test("MasterSync: one byte stream sends the same messages at the same absolute frames however the host cuts its blocks", () => {
+  // Runs, silences past 200 ms and restarts: 80 clocks; a second's silence; 40; a lone byte; a restart on its mark
+  // exactly; a gap of exactly 200 ms inside a run; and a run left open at the end, its mark past the stream.
+  const last1 = 5000 + 79 * 860;
+  const lone = 170000;
+  const onMark = lone + STOP_FRAMES + 1;
+  const timeline: Timeline = [
+    ...clocks(5000, 80),
+    ...clocks(120000, 40, 3),
+    [lone, 4],
+    ...clocks(onMark, 5, 6),
+    [onMark + 4 * 860 + STOP_FRAMES, 6],
+    ...clocks(215000, 3, 7),
+  ];
+  const total = 220000;
+  const runs = SPLITS.map(([name, split]) => {
+    const out = masterSync(timeline, split, total);
+    expect(out.every((m) => m.inside), `${name}: every frame inside its block`).toBe(true);
+    return { name, out: timed(out) };
+  });
+  for (const { name, out } of runs) expect(out, name).toEqual(runs[0].out);
+  // And that one list is the rule's: each byte's messages at its frame, each stop at its run's mark.
+  const at = (frame: number, data: number[]): [number, number[]] => [frame, data];
+  const run = (t: Timeline): [number, number[]][] => t.flatMap(([f, b], i) => (i === 0 ? [at(f, row(b)), at(f, START), at(f, CLOCK)] : [at(f, CLOCK)]));
+  const stopAfter = (frame: number) => at(frame + STOP_FRAMES + 1, STOP);
+  const second = clocks(120000, 40, 3);
+  const fourth: Timeline = [...clocks(onMark, 5, 6), [onMark + 4 * 860 + STOP_FRAMES, 6]];
+  expect(runs[0].out).toEqual([
+    ...run(clocks(5000, 80)),
+    stopAfter(last1),
+    ...run(second),
+    stopAfter(second[second.length - 1][0]),
+    ...run([[lone, 4]]),
+    stopAfter(lone),
+    ...run(fourth),
+    stopAfter(fourth[fourth.length - 1][0]),
+    ...run(clocks(215000, 3, 7)),
+  ]);
+});
+
+/** An MI.OUT bit stream: each command a flag bit and its 7 payload bits, `idle` zero bits before it, MSB-first. */
+function miOutBits(commands: [number, number][]): number[] {
+  const bits: number[] = [];
+  for (const [idle, cmd] of commands) {
+    for (let k = 0; k < idle; k++) bits.push(0);
+    bits.push(1);
+    for (let k = 6; k >= 0; k--) bits.push((cmd >> k) & 1);
+  }
+  while (bits.length % 8 !== 0) bits.push(0);
+  return bits;
+}
+
+test("MI.OUT: one captured stream sends the same messages at the same absolute frames however the host cuts its blocks", () => {
+  // A byte every 8 frames from frame 3 (MI.OUT's port, a bit a frame), the commands off the byte grid by the idle bits
+  // before them, so most straddle two captured bytes and, cut into short blocks, two blocks: clocks, START, a NoteOn
+  // and its NoteOff, a CC, a program change, STOP.
+  const bits = miOutBits([
+    [0, 0x7d], [3, 0x7f], [0, 0x70], [1, 60], [5, 0x7f], [2, 0x74], [0, 64], [7, 0x70], [1, 0],
+    [0, 0x78], [4, 9], [1, 0x7f], [0, 0x7e], [9, 0x7f],
+  ]);
+  const bytes: number[] = [];
+  for (let i = 0; i < bits.length; i += 8) bytes.push(bits.slice(i, i + 8).reduce((v, b) => (v << 1) | b, 0));
+  const frameOf = (i: number) => 3 + 8 * i;
+  const total = frameOf(bytes.length) + 100;
+  const runs = SPLITS.map(([name, split]) => {
+    const out: [number, number[]][] = [];
+    const state: ArduinoboyState = {};
+    let start = 0;
+    let next = 0;
+    let inside = true;
+    for (const n of split(total)) {
+      const blockBytes: number[] = [];
+      const frames: number[] = [];
+      for (; next < bytes.length && frameOf(next) < start + n; next++) {
+        blockBytes.push(bytes[next]);
+        frames.push(frameOf(next) - start);
+      }
+      arduinoboyDecodeSerialOut(blockBytes, frames, state, (frame, len, b0, b1, b2) => {
+        inside = inside && frame >= 0 && frame < n;
+        out.push([start + frame, [b0, b1, b2].slice(0, len)]);
+      });
+      start += n;
+    }
+    expect(inside, `${name}: every frame inside its block`).toBe(true);
+    return { name, out };
+  });
+  for (const { name, out } of runs) expect(out, name).toEqual(runs[0].out);
+  // Each message at the frame of the captured byte holding its last bit.
+  const ends: number[] = [];
+  let bit = 0;
+  for (const [idle] of [[0], [3], [0], [1], [5], [2], [0], [7], [1], [0], [4], [1], [0], [9]]) ends.push((bit += idle + 8) - 1);
+  const atBit = (k: number) => frameOf(Math.floor(ends[k] / 8));
+  expect(runs[0].out).toEqual([
+    [atBit(0), [0xfa]],
+    [atBit(1), [0xf8]],
+    [atBit(3), [0x90, 60, 0x7f]],
+    [atBit(4), [0xf8]],
+    [atBit(6), [0xb0, 4, 64]],
+    [atBit(8), [0x80, 0, 0]],
+    [atBit(10), [0xc0, 9]],
+    [atBit(11), [0xf8]],
+    [atBit(12), [0xfc]],
+    [atBit(13), [0xf8]],
   ]);
 });
````

Run: `pnpm test dsp/lsdj-arduinoboy`
Expected: FAIL: `# 24 passed, 0 skipped, 7 failed`, the seven failures all master-sync cases (the stop's mark, the block-length mark, the carried mark, the restart, the boundary, and the split property); MI.OUT needs no rule change and passes as it is.

- [ ] **Step 3: Stop master sync at its mark**

The bytes and frames are this block's, so `MasterSyncState.lastFrames` goes. Before each byte a started run whose mark
(`lastAt + stop + 1`) is at or before the byte stops there; at the block's end a mark inside the block stops the run
there, and a started run carries, `lastAt` moved back by the block's length. The rule (a gap longer than `stop`) is
the master-sync fix's, so the messages and their order are what they were:

````diff
diff --git a/packages/retroplug/src/lsdjArduinoboy.ts b/packages/retroplug/src/lsdjArduinoboy.ts
index bc42fc96..bbd6ace0 100644
--- a/packages/retroplug/src/lsdjArduinoboy.ts
+++ b/packages/retroplug/src/lsdjArduinoboy.ts
@@ -1,7 +1,7 @@
 // The Arduinoboy MIDIOUT (MI.OUT) decoder for the `lsdj-sync` role's mode 7 — the TS port of the
 // native ArduinoboyMaster role (packages/native/src/system/sameboy/roles/ArduinoboyMaster.{hpp,cpp}).
 // Per the "native owns bytes, TS owns meaning" split: native captures LSDj's raw outgoing serial
-// bytes, which reach the rp.lsdj.sync node on its `serialOut` input; this module turns them into host MIDI.
+// bytes, which reach the rp.lsdj.serial-out node on its `serialOut` input; this module turns them into host MIDI.
 //
 // Two layers, both pure + testable in isolation:
 //
@@ -38,7 +38,7 @@ const MIDI_PC_BASE = 0xc0;
  *  it reuses. */
 export type MidiEmit = (frame: number, len: number, b0: number, b1: number, b2: number) => void;
 
-/** Cross-block decoder state (kept in the lsdj-sync node's scratch). Fields are created lazily by the
+/** Cross-block decoder state (kept on the rp.lsdj.serial-out node's instance). Fields are created lazily by the
  *  decoders, so a fresh `{}` is a valid starting state. */
 export interface ArduinoboyState {
   // Byte protocol: a 0x70..0x7C command awaiting its value byte. `pendingCmd` is m = byte-0x70; stored
@@ -165,19 +165,17 @@ export function arduinoboyDecodeSerialOut(
 // stopped LSDj master waits on an external clock for another master, and clocked, it turns slave and ignores START
 // (docs/lsdj-master-sync-fix-design.md). So every byte is a clock, and a stopped LSDj goes quiet, which makes the
 // firmware's stop a silence. A playing master clocks one byte per 24-PPQN tick, 62.5 ms apart at LSDj's slowest tempo
-// (40 BPM), so a silence this long is a stop: about three times that gap. It is measured on the bytes' own frames, so
-// the message sequence does not depend on how the host cuts its blocks. The 0xFC's time does: it is found at the first
-// block start more than MASTER_SYNC_STOP_MS after the last byte (or at the byte that ends the silence, when no block
-// starts between them), so it is quantised to a block start and up to a block late.
+// (40 BPM), so a silence this long is a stop: about three times that gap. It is measured on the bytes' own frames, and
+// the 0xFC goes at the first frame the silence passes it, so neither what is sent nor where it goes depends on how
+// the host cuts its blocks.
 const MASTER_SYNC_STOP_MS = 200;
 
-/** Cross-block Master Sync state (the lsdj-sync stage's `masterSync` sub-object, dropped whenever the mode is not
- *  master sync, so coming back starts clean). */
+/** Cross-block Master Sync state (the rp.lsdj.serial-out node's, dropped whenever the mode is not master sync, so
+ *  coming back starts clean). */
 export interface MasterSyncState {
   started?: boolean; // a run is in progress (0xFA sent; 0xFC pending on the silence that ends it)
   channel?: number; // MIDI channel for the song-row NoteOn (default 0)
   lastAt?: number; // the run's newest byte: frames from the current block's start (negative: an earlier block)
-  lastFrames?: number; // the last call's block length: where this call's bytes, from that block, sit
 }
 
 /** The block a Master Sync call runs in: its length and rate. */
@@ -186,12 +184,13 @@ export interface MasterSyncBlock {
   sampleRate: number;
 }
 
-/** Decode the Master Sync serial-out bytes that reached this block into host MIDI. Call once per block with the
- *  bytes the lsdj-sync node read off its `serialOut` input and each one's frame, which are LAST block's bytes at
- *  their frames in it (the feedback wire's one block), and this block's length and rate. Each byte is a tempo clock,
- *  the first of a run opened with LSDj's song row; a run ends with one 0xFC once MASTER_SYNC_STOP_MS pass with no
- *  byte. Every message goes to `emit` at a frame of the bytes' block: a byte's at its own frame, a stop found before a
- *  byte at that byte's, and one found at this block's start at that start, one past the bytes' block's last frame. */
+/** Decode one block's Master Sync serial-out bytes into host MIDI. Call once per block with the bytes the Game Boy
+ *  captured in it, each one's frame in it, and the block's length and rate. Each byte is a tempo clock, sent at its
+ *  frame, the first of a run opened there with LSDj's song row and 0xFA; a run ends with one 0xFC at its mark, the
+ *  first frame its silence passes MASTER_SYNC_STOP_MS, `lastAt + stop + 1`. A mark is sent before the byte at or past
+ *  it (which then opens a fresh run at its own frame), or at the end of the block it falls inside; one past the block
+ *  carries to the next, with the run's newest byte moved back by the block's length, so a mark is never behind the
+ *  block it is sent in. A gap of exactly `stop` frames is no stop: the mark is the frame after. */
 export function arduinoboyMasterSyncBlock(
   bytes: number[],
   frames: number[],
@@ -200,33 +199,37 @@ export function arduinoboyMasterSyncBlock(
   emit: MidiEmit,
 ): void {
   const ch = state.channel ?? 0;
-  const from = state.lastFrames ?? block.frames; // the length of the block these bytes came from
   const stop = Math.round((block.sampleRate * MASTER_SYNC_STOP_MS) / 1000);
   for (let i = 0; i < bytes.length; i++) {
-    const at = frames[i] - from;
-    // A silence can end inside this call, with no block start between its 200 ms mark and the byte that ends it.
-    // Checked only at a block start, the restart would carry on the old run (no 0xFC, no fresh NoteOn and 0xFA), so
+    const at = frames[i];
+    // The silence before this byte passed 200 ms at the mark: the run stopped there, and this byte opens a new one.
+    // Checked only at a block's end, the restart would carry on the old run (no 0xFC, no fresh NoteOn and 0xFA), so
     // the output would depend on the block split.
-    if (state.started && at - (state.lastAt ?? 0) > stop) {
-      emit(frames[i], 1, 0xfc, 0, 0);
-      state.started = false;
+    if (state.started) {
+      const mark = (state.lastAt ?? 0) + stop + 1;
+      if (at >= mark) {
+        emit(mark, 1, 0xfc, 0, 0);
+        state.started = false;
+      }
     }
     if (!state.started) {
       // First tick of a run: the byte value is LSDj's song row → NoteOn, then transport start, then this
       // byte's own clock tick (the firmware sends NoteOn + 0xFA before the first 0xF8).
       state.started = true;
-      emit(frames[i], 3, 0x90 | ch, bytes[i] & 0x7f, 0x7f);
-      emit(frames[i], 1, 0xfa, 0, 0);
+      emit(at, 3, 0x90 | ch, bytes[i] & 0x7f, 0x7f);
+      emit(at, 1, 0xfa, 0, 0);
     }
-    emit(frames[i], 1, 0xf8, 0, 0); // one MIDI clock per tempo byte
+    emit(at, 1, 0xf8, 0, 0); // one MIDI clock per tempo byte
     state.lastAt = at;
   }
-  // Every byte before this block's start is known: a run whose newest byte lies further back than the stop's
-  // silence has ended.
-  if (state.started && -(state.lastAt ?? 0) > stop) {
-    emit(from, 1, 0xfc, 0, 0);
-    state.started = false;
+  // Every byte of this block is known: a run whose mark falls inside it stopped there.
+  if (state.started) {
+    const mark = (state.lastAt ?? 0) + stop + 1;
+    if (mark < block.frames) {
+      emit(mark, 1, 0xfc, 0, 0);
+      state.started = false;
+    } else {
+      state.lastAt = (state.lastAt ?? 0) - block.frames;
+    }
   }
-  if (state.started) state.lastAt = (state.lastAt ?? 0) - block.frames;
-  state.lastFrames = block.frames;
 }
````

Run: `pnpm test dsp/lsdj-arduinoboy`
Expected: `31 passed, 0 skipped, 0 failed`.

- [ ] **Step 4: The node, its settings, and rp.lsdj.sync without the Game Boy-to-host direction**

`rp.lsdj.serial-out` takes `serialOut` (bytes) and sends `midiOut` (events), each with the capacity `rp.lsdj.sync`'s
port of that name had, reshapes, and reads only its settings' `mode`: MI.OUT and master sync decode, every other mode
reads nothing at all. Its scratch is MI.OUT's state, kept across mode changes, and master sync's sub-object, dropped
whenever the mode is not master sync. Both LSDj nodes take one schema from a module of their own, so the decoder does
not import the sync node:

Create `packages/retroplug/src/script/nodes/lsdjSyncSettings.ts`:

````ts
// The `lsdj-sync` role's config as its two nodes take it: rp.lsdj.sync, placed before its Game Boy (the host-to-Game
// Boy direction, lsdjSync.ts), and rp.lsdj.serial-out, placed after it (the Game Boy-to-host one, lsdjSerialOut.ts).
// buildGraph hands both the role's config as their settings, the MI.MAP controller's forced mode included, so one
// schema checks both and the two cannot read a config differently.
import { s, type Infer } from "../../graph/settingsSchema";
import { LSDJ_MODE_VALUES } from "../../settingsEnums";

// mode: LsdjSyncMode. tempoDivisor subdivides the 24-PPQN clock (24/divisor) for MidiSync, MidiSyncArduinoboy and
// MidiMap; the menu offers 1/2/4/8. autoStart taps START on the host transport's rise to auto-arm a SYNC=MIDI cart
// (MidiSync, MidiSyncArduinoboy), which a headless DAW render needs; off by default, so a cart arms by hand.
export const lsdjSyncSettings = s.object({
  mode: s.enum(LSDJ_MODE_VALUES, "midiSync"),
  tempoDivisor: s.int({ min: 1, max: 8, default: 1 }),
  autoStart: s.bool(false),
});

export type LsdjSyncSettings = Infer<typeof lsdjSyncSettings>;
````

Create `packages/retroplug/src/script/nodes/lsdjSerialOut.ts`:

````ts
// LSDj's serial out, decoded to host MIDI: the `lsdj-sync` role's Game Boy-to-host direction (dspRoles.ts registers
// the role, and buildGraph places this node for it after the systems, as sys-<id>/lsdj-sync/out, where rp.lsdj.sync,
// before them, takes the host-to-Game Boy one). Each block it reads what its Game Boy captured on its link port in that
// block, each byte at the frame its core completed it, over a forward wire from the system's `serialOut`, and in the
// role's two MIDI-out modes decodes it (lsdjArduinoboy.ts): MI.OUT (`midiOut`, Arduinoboy's Mode_LSDJ_Midiout: notes,
// CCs, program changes and realtime) or master sync (`masterSync`, a clock a byte, a silence the stop). Each message
// goes on `midiOut` at the frame the decoder gives it, a frame of this block, so the host hears it where LSDj sent it.
//
// The other modes send nothing, and read nothing: the core captures only for those two, so the input is empty. The
// node is placed whatever the mode, so a mode switch is a change of settings, as it is for rp.lsdj.sync, and never of
// the document. Its settings are the role's config through the schema rp.lsdj.sync takes too (lsdjSyncSettings.ts),
// the MI.MAP controller's forced mode included; it reads only `mode`.
//
// MI.OUT's bit accumulator and pending command are kept on the instance across mode changes; master sync's state is
// dropped whenever the mode is not master sync, so coming back opens a fresh run. Nothing is allocated in a warm
// block: the byte and frame arrays are refilled in place, and the decoders hand each message's bytes to emitMidiOut,
// which sends them from arrays made with the instance.
import { bytes, defineNode, events, type Block } from "../../graph/defineNode";
import { LsdjSyncMode } from "../../settingsEnums";
import {
  arduinoboyDecodeSerialOut,
  arduinoboyMasterSyncBlock,
  type ArduinoboyState,
  type MasterSyncBlock,
  type MasterSyncState,
} from "../../lsdjArduinoboy";
import { lsdjSyncSettings, type LsdjSyncSettings } from "./lsdjSyncSettings";
import { MIDI_OUT } from "./ports";

export const LSDJ_SERIAL_OUT_NODE_TYPE = "rp.lsdj.serial-out";

export const lsdjSerialOutNode = defineNode<LsdjSyncSettings>({
  id: LSDJ_SERIAL_OUT_NODE_TYPE,
  name: "LSDj Serial Out",
  category: "protocol",
  settings: lsdjSyncSettings,
  reshapes: true,
  ports: {
    serialOut: bytes.in(),
    midiOut: events.out(MIDI_OUT),
  },
  create: ({ settings: initial }) => {
    let mode = initial.mode;
    let block: Block | null = null; // the block being processed, which every send goes on
    const miOut: ArduinoboyState = {};
    let masterSync: MasterSyncState | undefined;
    // This block's length and rate, as master sync reads them.
    const span: MasterSyncBlock = { frames: 0, sampleRate: 44100 };
    // This block's serial-out bytes, and each one's frame in it, index for index, as the decoders take them. Refilled
    // each block, so they allocate nothing once grown.
    const serialOut: number[] = [];
    const serialOutFrames: number[] = [];
    // A decoded message, at the frame the decoder gives it. A message is 1 to 3 bytes and send sends all of the array
    // it is given, so each length has an array of its own, made once here; send copies a message into the port's
    // queue, so its array is free again when send returns.
    const out1 = new Uint8Array(1);
    const out2 = new Uint8Array(2);
    const out3 = new Uint8Array(3);
    const emitMidiOut = (frame: number, len: number, b0: number, b1: number, b2: number) => {
      const data = len === 1 ? out1 : len === 2 ? out2 : out3;
      data[0] = b0;
      if (len > 1) data[1] = b1;
      if (len > 2) data[2] = b2;
      block!.send("midiOut", frame, data);
    };

    return {
      process(b) {
        block = b;
        // Master sync's state would go stale while another mode runs: dropped here, coming back starts clean rather
        // than from a stale run or a stale last byte.
        if (masterSync !== undefined && mode !== LsdjSyncMode.MasterSync) masterSync = undefined;
        if (mode !== LsdjSyncMode.MidiOut && mode !== LsdjSyncMode.MasterSync) return;
        serialOut.length = 0;
        serialOutFrames.length = 0;
        const captured = b.messages("serialOut");
        for (let i = 0; i < captured.length; i++) {
          const m = captured[i];
          for (let j = 0; j < m.data.length; j++) {
            serialOut.push(m.data[j]);
            serialOutFrames.push(m.frame);
          }
        }
        if (mode === LsdjSyncMode.MidiOut) {
          if (serialOut.length > 0) arduinoboyDecodeSerialOut(serialOut, serialOutFrames, miOut, emitMidiOut);
          return;
        }
        span.frames = b.frames;
        span.sampleRate = b.transport.sampleRate;
        arduinoboyMasterSyncBlock(serialOut, serialOutFrames, span, (masterSync ??= {}), emitMidiOut);
      },
      onSettings(next) {
        mode = next.mode;
      },
    };
  },
});
````

Its tests take the MI.OUT and master-sync cases from `lsdj-sync.test.ts`, now in the same block and at their frames,
with Task 3's reuse case, mode changes, the other modes, and a check that the two nodes' settings descriptors are
identical:

Create `packages/retroplug/test/script/nodes/lsdj-serial-out.test.ts`:

````ts
// rp.lsdj.serial-out (src/script/nodes/lsdjSerialOut.ts): the lsdj-sync role's Game Boy-to-host direction, placed
// after its Game Boy, which decodes the serial out the core captured in the same block into host MIDI: MI.OUT (mode 7)
// and master sync (mode 8), each message at the frame the decoder gives it in that block: the MI.OUT and master-sync
// cases rp.lsdj.sync's tests held, here at each message's frame, and what the node keeps across a mode change. The
// decoders' pure cases are test/dsp/lsdj-arduinoboy.test.ts's.
import { test, expect } from "../../../testing/harness";
import { LSDJ_SERIAL_OUT_NODE_TYPE, lsdjSerialOutNode } from "../../../src/script/nodes/lsdjSerialOut";
import { lsdjSyncNode } from "../../../src/script/nodes/lsdjSync";
import { LSDJ_MODE_VALUES } from "../../../src/settingsEnums";
import { NodeRig, type In, type Sent } from "./_rig";

const serialOut = (mode: string, sampleRate = 44100) => new NodeRig(lsdjSerialOutNode, { mode }, sampleRate);
/** The host MIDI sent, each as [frame, ...bytes]. */
const midiOut = (sent: Sent[]): number[][] => sent.filter((s) => s.port === "midiOut").map((s) => [s.frame, ...s.data]);
const data = (sent: Sent[]): number[][] => sent.filter((s) => s.port === "midiOut").map((s) => s.data);
/** A flag-framed MI.OUT byte (0x80 | the protocol byte), captured at `frame`. */
const framed = (cmd: number, frame: number): In => ({ frame, data: [0x80 | (cmd & 0x7f)] });

test("its ports: the Game Boy's serial out in, host MIDI out, and the lsdj-sync role's settings, as rp.lsdj.sync takes them", () => {
  expect(serialOut("midiOut").ports()).toEqual(["in bytes serialOut", "out events midiOut"]);
  expect(lsdjSerialOutNode.descriptor().id).toBe(LSDJ_SERIAL_OUT_NODE_TYPE);
  const caps = lsdjSerialOutNode.layout(lsdjSerialOutNode.parseSettings({})).ports.slice(1).map((p) => p.capacity);
  expect(caps).toEqual([{ messages: 1024, bytes: 8192 }]);
  expect(lsdjSerialOutNode.reshapes).toBe(true);
  // One schema: the same defaults, the same refusals, the same JSON Schema in the descriptor.
  expect(lsdjSerialOutNode.parseSettings({})).toEqual({ mode: "midiSync", tempoDivisor: 1, autoStart: false });
  expect(() => lsdjSerialOutNode.parseSettings({ mode: "sync" })).toThrow("mode: Invalid option");
  expect(() => lsdjSerialOutNode.parseSettings({ tempoDivisor: 9 })).toThrow("tempoDivisor: Too big");
  expect(JSON.stringify(lsdjSerialOutNode.descriptor().settings)).toBe(JSON.stringify(lsdjSyncNode.descriptor().settings));
});

// --- MI.OUT (mode 7) ------------------------------------------------------------------------------------------------

test("MI.OUT: the serial-out bytes decode to host MIDI in their own block, each message at its completing byte's frame", () => {
  // A byte every eight frames, as SameBoy clocks MI.OUT's port (one bit an audio frame); each flag-frames one protocol
  // byte: START, clock, and a NoteOn ch0 note 0x69, whose command and value are two bytes and which completes at the
  // value's.
  const sent = serialOut("midiOut").run({ frames: 1024, inputs: { serialOut: [framed(0x7d, 7), framed(0x7f, 15), framed(0x70, 23), framed(0x69, 31)] } });
  expect(midiOut(sent)).toEqual([[7, 0xfa], [15, 0xf8], [31, 0x90, 0x69, 0x7f]]);
  expect(sent.every((s) => s.port === "midiOut" && s.queued === s.frame)).toBe(true);
});

test("MI.OUT: a frame split across two blocks' bytes decodes once, at its completing byte's frame in the later block", () => {
  const rig = serialOut("midiOut");
  expect(rig.run({ frames: 1024, inputs: { serialOut: [{ frame: 1000, data: [0x7f] }] } })).toEqual([]);
  expect(midiOut(rig.run({ frames: 1024, inputs: { serialOut: [{ frame: 3, data: [0x80] }] } }))).toEqual([[3, 0xf8]]);
});

// --- master sync (mode 8) -------------------------------------------------------------------------------------------

test("master sync: each byte a clock at its frame, the first opening the run with LSDj's song row and 0xFA there", () => {
  const sent = serialOut("masterSync").run({ frames: 1024, inputs: { serialOut: [{ frame: 0, data: [5] }, { frame: 900, data: [6] }] } });
  expect(midiOut(sent)).toEqual([[0, 0x90, 5, 0x7f], [0, 0xfa], [0, 0xf8], [900, 0xf8]]);
});

/** The block and frame of the first 0xFC after a run's one byte at `frame` of block 0, in 1024-frame blocks. */
function stopOf(frame: number, sampleRate: number): [number, number] | null {
  const rig = serialOut("masterSync", sampleRate);
  for (let b = 0; b < 16; b++) {
    const sent = rig.run({ frames: 1024, sampleRate, playing: true, inputs: b === 0 ? { serialOut: [{ frame, data: [5] }] } : {} });
    const stop = sent.find((s) => s.data[0] === 0xfc);
    if (stop) return [b, stop.frame];
  }
  return null;
}

test("master sync stops a run at its mark, the first frame its silence passes 200 ms, in the block that holds it", () => {
  // 200 ms is 8820 frames at 44.1 kHz, so a byte at frame f stops at f + 8821.
  expect(stopOf(0, 44100)).toEqual([8, 8821 - 8 * 1024]);
  expect(stopOf(1000, 44100)).toEqual([9, 9821 - 9 * 1024]);
});

test("master sync's 200 ms stop is 200 ms at the host's rate: 9600 frames at 48 kHz, not 8820", () => {
  expect(stopOf(0, 48000)).toEqual([9, 9601 - 9 * 1024]);
  expect(stopOf(1000, 48000)).toEqual([10, 10601 - 10 * 1024]);
});

// --- state across settings changes ----------------------------------------------------------------------------------

test("leaving master sync drops its state, so coming back opens a fresh run", () => {
  const rig = serialOut("masterSync");
  const block = (bytes: In[]) => data(rig.run({ frames: 1024, inputs: { serialOut: bytes } }));
  expect(block([{ frame: 100, data: [5] }])).toEqual([[0x90, 5, 0x7f], [0xfa], [0xf8]]); // a run opens
  rig.set({ mode: "off" });
  block([]);
  rig.set({ mode: "masterSync" });
  // Kept, the stale open run would swallow this run's NoteOn and 0xFA.
  expect(block([{ frame: 0, data: [7] }])).toEqual([[0x90, 7, 0x7f], [0xfa], [0xf8]]);
  expect(rig.creates).toBe(1);
});

test("MI.OUT keeps its decoder's state across a mode change: a frame begun before it completes after it", () => {
  // The first half of a framed clock, then a block in master sync (whose run does not touch MI.OUT's bits) and one in
  // off, then MI.OUT again: the second half completes the clock, once.
  const rig = serialOut("midiOut");
  expect(rig.run({ frames: 1024, inputs: { serialOut: [{ frame: 0, data: [0x7f] }] } })).toEqual([]);
  rig.set({ mode: "masterSync" });
  expect(data(rig.run({ frames: 1024, inputs: { serialOut: [{ frame: 10, data: [3] }] } }))).toEqual([[0x90, 3, 0x7f], [0xfa], [0xf8]]);
  rig.set({ mode: "off" });
  expect(rig.run({ frames: 1024 })).toEqual([]);
  rig.set({ mode: "midiOut" });
  expect(midiOut(rig.run({ frames: 1024, inputs: { serialOut: [{ frame: 5, data: [0x80] }] } }))).toEqual([[5, 0xf8]]);
  expect(rig.creates).toBe(1);
});

test("every other mode sends nothing and reads nothing of its input", () => {
  // The core captures only for MI.OUT and master sync, so another mode's input is empty: an input whose bytes throw
  // when read shows the node never reads it.
  for (const mode of LSDJ_MODE_VALUES.filter((m) => m !== "midiOut" && m !== "masterSync")) {
    const rig = serialOut(mode);
    const process = rig.node.process.bind(rig.node);
    rig.node.process = (b) => {
      const messages = b.messages;
      b.messages = (port) => {
        if (port === "serialOut") throw new Error(`${mode} read its serialOut`);
        return messages.call(b, port);
      };
      return process(b);
    };
    expect(rig.run({ frames: 1024, playing: true, inputs: { serialOut: [framed(0x7f, 7), { frame: 100, data: [5] }] } }), mode).toEqual([]);
  }
});

test("MI.OUT and master sync send each message from an array the node made once, one a message length", () => {
  // The rig keeps what send was handed: in a warm block the node passes the same arrays it passed in the first, one for
  // each length a message has, so it makes none for a message it decodes.
  // MI.OUT: a clock (1 byte), a NoteOn (3) and a program change (2), the same each block.
  const miOut = [framed(0x7f, 7), framed(0x70, 15), framed(60, 23), framed(0x78, 31), framed(5, 39)];
  // Master sync: a run's first block sends its NoteOn (3), 0xFA and a clock (1), and each block after a clock.
  const master = (b: number): In[] => [{ frame: 100, data: [b === 0 ? 4 : 9] }];
  for (const [mode, bytes] of [["midiOut", () => miOut], ["masterSync", master]] as const) {
    const rig = serialOut(mode);
    const byLength = new Map<number, ArrayLike<number>>();
    for (let b = 0; b < 4; b++) {
      const sent = rig.run({ frames: 1024, inputs: { serialOut: bytes(b) } });
      expect(sent.length > 0).toBeTruthy();
      sent.forEach((s, i) => {
        const source = rig.sources[i];
        expect(s.port).toBe("midiOut");
        expect(source.length).toBe(s.data.length);
        if (!byLength.has(source.length)) byLength.set(source.length, source);
        expect(byLength.get(source.length) === source).toBeTruthy();
      });
    }
    expect([...byLength.keys()].sort()).toEqual(mode === "midiOut" ? [1, 2, 3] : [1, 3]);
  }
});
````

`rp.lsdj.sync` loses its `serialOut` input, its `midiOut` output, their arrays and its emit, and sends nothing in
modes `midiOut` and `masterSync`; its tests say so. The bundle registers the new type last, so the production span
labels 16 to 22 keep their types and it takes 23, and the no-zod test's list of node modules gains it:

````diff
diff --git a/packages/retroplug/src/script/bundle.ts b/packages/retroplug/src/script/bundle.ts
index 8e1fd61d..8d345562 100644
--- a/packages/retroplug/src/script/bundle.ts
+++ b/packages/retroplug/src/script/bundle.ts
@@ -11,10 +11,13 @@ import { risaSyncNode } from "./nodes/risaSync";
 import { smsSyncNode } from "./nodes/smsSync";
 import { lsdjSyncNode } from "./nodes/lsdjSync";
 import { controllerSessionNode } from "./nodes/controllerSession";
+import { lsdjSerialOutNode } from "./nodes/lsdjSerialOut";
 import { registerScriptNodes } from "./register";
 
 // The production script node types, in registration order: the MIDI routing, a protocol node for each role that has
-// one, and the control-surface session (docs/modular-graph-phase-b3b-design.md section 2), which buildGraph places.
+// one, the control-surface session (docs/modular-graph-phase-b3b-design.md section 2), and LSDj's serial-out decoder,
+// which buildGraph places after the systems (docs/lsdj-serial-out-design.md). The order is the profile's span labels'
+// (spec/08-profiling.md: 16 up, one a type in this order), so a type added goes last, keeping the others' labels.
 // Nothing here registers roles or imports zod: a node's settings are graph/settingsSchema.ts's, and
 // test/script/bundle-no-zod.test.ts holds the bundle to that.
 const scriptNodes: readonly DefinedNode<any>[] = [
@@ -25,6 +28,7 @@ const scriptNodes: readonly DefinedNode<any>[] = [
   smsSyncNode,
   lsdjSyncNode,
   controllerSessionNode,
+  lsdjSerialOutNode,
 ];
 
 registerScriptNodes(scriptNodes);
diff --git a/packages/retroplug/src/script/nodes/lsdjSync.ts b/packages/retroplug/src/script/nodes/lsdjSync.ts
index 53ac63d4..977b17ad 100644
--- a/packages/retroplug/src/script/nodes/lsdjSync.ts
+++ b/packages/retroplug/src/script/nodes/lsdjSync.ts
@@ -1,23 +1,21 @@
-// LSDj's sync (the `lsdj-sync` role's audio-thread work; dspRoles.ts registers the role, and buildGraph places this
-// node for it): each block, the configured mode of LsdjSyncMode, the glue the role ran around the protocol's pure
-// parts under the DSP kernel, ported line for line. Its scratch is the role's one scratch bag, kept on the instance,
-// so a mode change keeps what the role kept, and master sync's sub-object is dropped outside that mode as the role
-// dropped it.
+// LSDj's sync, host to Game Boy (the `lsdj-sync` role's audio-thread work into the cart; dspRoles.ts registers the
+// role, and buildGraph places this node for it before the systems): each block, the configured mode of LsdjSyncMode,
+// the glue the role ran around the protocol's pure parts under the DSP kernel, ported line for line. Its scratch is
+// the role's one scratch bag, kept on the instance, so a mode change keeps what the role kept. The Game Boy-to-host
+// direction, the MI.OUT and master-sync decoders, is rp.lsdj.serial-out's (lsdjSerialOut.ts), placed for the same
+// role after the systems, where it reads its Game Boy's serial out in the block the core captured it.
 //
 // What the modes read: the routed MIDI on `midi`, then the controller's launches on `launches`, as ONE inbox in that
 // order, the order the kernel's inbox held them in (its launches appended at frame 0 after the routed events, so a
-// frame merge would not be the same); and on `serialOut`, a feedback wire from the Game Boy's node, last block's
-// captured serial-out bytes at their frames in that block, which MI.OUT and master sync decode.
+// frame merge would not be the same).
 //
 // What they send: serial-in bytes, a run of same-frame bytes to a message as the kernel node sent them; a button
-// press as [button, down] at frame 0; host MIDI out at frame 0. The frame-ordered queue raises a byte sent behind a
-// later one (Arduinoboy's START after a row byte, MI.MAP's clocks after its rows) as it raised the kernel's.
+// press as [button, down] at frame 0. The frame-ordered queue raises a byte sent behind a later one (Arduinoboy's
+// START after a row byte, MI.MAP's clocks after its rows) as it raised the kernel's.
 //
-// Nothing is allocated in a warm block: the decoders hand each message's bytes to emitMidiOut, which sends them from
-// arrays made with the instance.
+// Nothing is allocated in a warm block: the serial-in run and the button press are made once and reused.
 import { bytes, defineNode, events, type Block, type Message } from "../../graph/defineNode";
-import { s, type Infer } from "../../graph/settingsSchema";
-import { LSDJ_MODE_VALUES, LsdjSyncMode } from "../../settingsEnums";
+import { LsdjSyncMode } from "../../settingsEnums";
 import {
   KEYBOARD_LOW_OCTAVE_MAP,
   KEYBOARD_LOW_START,
@@ -28,12 +26,6 @@ import {
   isExtendedScancode,
   toGbSerialByte,
 } from "../../lsdjKeyboardMap";
-import {
-  arduinoboyDecodeSerialOut,
-  arduinoboyMasterSyncBlock,
-  type ArduinoboyState,
-  type MasterSyncState,
-} from "../../lsdjArduinoboy";
 import {
   LSDJ_CLOCK,
   LSDJ_START,
@@ -47,25 +39,14 @@ import {
   midiMapRow,
 } from "../../lsdjSyncBytes";
 import { TickClock } from "./clock";
-import { BUTTONS, MIDI_OUT, SERIAL_IN } from "./ports";
+import { lsdjSyncSettings, type LsdjSyncSettings } from "./lsdjSyncSettings";
+import { BUTTONS, SERIAL_IN } from "./ports";
 
 export const LSDJ_SYNC_NODE_TYPE = "rp.lsdj.sync";
 
-// mode: LsdjSyncMode. tempoDivisor subdivides the 24-PPQN clock (24/divisor) for MidiSync, MidiSyncArduinoboy and
-// MidiMap; the menu offers 1/2/4/8. autoStart taps START on the host transport's rise to auto-arm a SYNC=MIDI cart
-// (MidiSync, MidiSyncArduinoboy), which a headless DAW render needs; off by default, so a cart arms by hand.
-const settings = s.object({
-  mode: s.enum(LSDJ_MODE_VALUES, "midiSync"),
-  tempoDivisor: s.int({ min: 1, max: 8, default: 1 }),
-  autoStart: s.bool(false),
-});
-
-export type LsdjSyncSettings = Infer<typeof settings>;
-
 /** The role's scratch bag, every mode's fields in one, as the kernel kept it for the stage: a mode reads what another
- *  left (the Arduinoboy and MidiSync auto-arms share armDown) until the node goes. The MI.OUT decoder keeps its
- *  own fields in it too. */
-interface LsdjState extends ArduinoboyState {
+ *  left (the Arduinoboy and MidiSync auto-arms share armDown) until the node goes. */
+interface LsdjState {
   // MidiSyncArduinoboy
   playing?: boolean;
   divisor?: number;
@@ -77,23 +58,19 @@ interface LsdjState extends ArduinoboyState {
   lastRow?: number;
   // KeyboardMidi
   octave?: number;
-  // MasterSync: dropped whenever the mode is not master sync, so coming back starts clean.
-  masterSync?: MasterSyncState;
 }
 
 export const lsdjSyncNode = defineNode<LsdjSyncSettings>({
   id: LSDJ_SYNC_NODE_TYPE,
   name: "LSDj Sync",
   category: "protocol",
-  settings,
+  settings: lsdjSyncSettings,
   reshapes: true,
   ports: {
     midi: events.in(),
     launches: events.in(),
-    serialOut: bytes.in(),
     serialIn: bytes.out(SERIAL_IN),
     buttons: events.out(BUTTONS),
-    midiOut: events.out(MIDI_OUT),
   },
   create: ({ settings: initial }) => {
     let config = initial;
@@ -105,10 +82,6 @@ export const lsdjSyncNode = defineNode<LsdjSyncSettings>({
     // inside process, after process has set them for the block.
     let midi: readonly Message[] = [];
     let launches: readonly Message[] = [];
-    // Last block's serial-out bytes, and each one's frame in that block, index for index: what the kernel handed a
-    // stage as ctx.serialOut and ctx.serialOutFrames. Refilled each block, so they allocate nothing once grown.
-    const serialOut: number[] = [];
-    const serialOutFrames: number[] = [];
     const press = new Uint8Array(2);
 
     // The serial-in bytes not yet sent: consecutive bytes at runFrame, sent as one message when a byte at another
@@ -130,20 +103,6 @@ export const lsdjSyncNode = defineNode<LsdjSyncSettings>({
       press[1] = down ? 1 : 0;
       block!.send("buttons", 0, press);
     };
-    // A decoded message, at frame 0: the decoders give each its byte's frame, but that is a frame of last block, which
-    // the feedback wire brought, so the host timestamps it by block. A message is 1 to 3 bytes and send sends all of
-    // the array it is given, so each length has an array of its own, made once here; send copies a message into the
-    // port's queue, so its array is free again when send returns.
-    const out1 = new Uint8Array(1);
-    const out2 = new Uint8Array(2);
-    const out3 = new Uint8Array(3);
-    const emitMidiOut = (_frame: number, len: number, b0: number, b1: number, b2: number) => {
-      const data = len === 1 ? out1 : len === 2 ? out2 : out3;
-      data[0] = b0;
-      if (len > 1) data[1] = b1;
-      if (len > 2) data[2] = b2;
-      block!.send("midiOut", 0, data);
-    };
     // Each inbox event, through `handle`: the routed MIDI, then the launches.
     const eachEvent = (handle: (e: Message) => void) => {
       for (let i = 0; i < midi.length; i++) handle(midi[i]);
@@ -223,9 +182,6 @@ export const lsdjSyncNode = defineNode<LsdjSyncSettings>({
 
     const dispatch = () => {
       const transport = clock.block.transport;
-      // A mode's scratch survives a config change, so master sync's would go stale while another mode runs: dropped
-      // here, coming back starts clean rather than from a stale run or a stale last byte.
-      if (st.masterSync !== undefined && config.mode !== LsdjSyncMode.MasterSync) st.masterSync = undefined;
       switch (config.mode) {
         case LsdjSyncMode.MidiSync: {
           // Optional auto-arm: SYNC=MIDI LSDj follows the clock only once START has parked it in "wait for MIDI", so
@@ -270,12 +226,9 @@ export const lsdjSyncNode = defineNode<LsdjSyncSettings>({
         case LsdjSyncMode.MidiPassthrough:
           eachEvent(passthroughEvent);
           break;
-        case LsdjSyncMode.MidiOut: // ArduinoboyMaster / MIDIOUT: LSDj's MI.OUT stream, decoded to host MIDI
-          if (serialOut.length > 0) arduinoboyDecodeSerialOut(serialOut, serialOutFrames, st, emitMidiOut);
-          break;
-        case LsdjSyncMode.MasterSync: // LSDj drives the host clock: a byte a clock, a silence the stop
-          arduinoboyMasterSyncBlock(serialOut, serialOutFrames, clock.block, (st.masterSync ??= {}), emitMidiOut);
-          break;
+        case LsdjSyncMode.MidiOut:
+        case LsdjSyncMode.MasterSync:
+          break; // LSDj's serial out, which rp.lsdj.serial-out decodes after the Game Boy
         default:
           break; // Off / Keyboard send nothing here
       }
@@ -291,16 +244,6 @@ export const lsdjSyncNode = defineNode<LsdjSyncSettings>({
         clock.begin(b);
         midi = b.messages("midi");
         launches = b.messages("launches");
-        serialOut.length = 0;
-        serialOutFrames.length = 0;
-        const captured = b.messages("serialOut");
-        for (let i = 0; i < captured.length; i++) {
-          const m = captured[i];
-          for (let j = 0; j < m.data.length; j++) {
-            serialOut.push(m.data[j]);
-            serialOutFrames.push(m.frame);
-          }
-        }
         try {
           dispatch();
         } finally {
diff --git a/packages/retroplug/test/script/bundle-no-zod.test.ts b/packages/retroplug/test/script/bundle-no-zod.test.ts
index 63d1d618..4a59f4c3 100644
--- a/packages/retroplug/test/script/bundle-no-zod.test.ts
+++ b/packages/retroplug/test/script/bundle-no-zod.test.ts
@@ -68,7 +68,7 @@ test("the script-node bundle imports no zod module", async () => {
   // B3a's bundle was 660 KB with zod; this one is about 78 KB. A loose ceiling, for zod inlined where no path says so.
   expect(bytes < 200 * 1024, `the bundle is ${bytes} bytes, past 200 KB`).toBe(true);
   // Every production node type is in it, so an entry that bundled nothing cannot pass.
-  for (const node of ["midiRoute", "mgbMidi", "n8MidiToFifo", "risaSync", "smsSync", "lsdjSync", "controllerSession"])
+  for (const node of ["midiRoute", "mgbMidi", "n8MidiToFifo", "risaSync", "smsSync", "lsdjSync", "controllerSession", "lsdjSerialOut"])
     expect(inputs.includes(`src/script/nodes/${node}.ts`), node).toBe(true);
   // zod from node_modules, however the package manager lays it out (node_modules/zod/..., .pnpm/zod@x/...).
   const zod = inputs.filter((path) => /(^|[\\/])zod(@[^\\/]*)?[\\/]/.test(path));
diff --git a/packages/retroplug/test/script/nodes/lsdj-sync.test.ts b/packages/retroplug/test/script/nodes/lsdj-sync.test.ts
index cf96a8cc..1ab51aec 100644
--- a/packages/retroplug/test/script/nodes/lsdj-sync.test.ts
+++ b/packages/retroplug/test/script/nodes/lsdj-sync.test.ts
@@ -1,7 +1,7 @@
-// rp.lsdj.sync (src/script/nodes/lsdjSync.ts): LSDj's sync modes, as the DSP kernel's lsdj-sync role ran them until
-// B3b. The cases its kernel-level tests held (every mode, MI.OUT and master sync off the Game Boy's serial out), at the
-// node, with what the kernel node's own tests held of the serial-in runs and the feedback wire's frames; the decoders'
-// pure cases are test/dsp/lsdj-arduinoboy.test.ts's.
+// rp.lsdj.sync (src/script/nodes/lsdjSync.ts): LSDj's sync modes, host to Game Boy, as the DSP kernel's lsdj-sync role
+// ran them until B3b. The cases its kernel-level tests held of every mode that feeds the cart, at the node, with what
+// the kernel node's own tests held of the serial-in runs. MI.OUT and master sync, the Game Boy's serial out decoded to
+// host MIDI, are rp.lsdj.serial-out's (lsdj-serial-out.test.ts); here they send nothing.
 import { test, expect } from "../../../testing/harness";
 import { LSDJ_SYNC_NODE_TYPE, lsdjSyncNode } from "../../../src/script/nodes/lsdjSync";
 import { NodeRig, bytesOn, on, type BlockSpec, type In, type Sent } from "./_rig";
@@ -14,20 +14,12 @@ const noteOn = (channel: number, note: number, frame = 0): In => ({ frame, data:
 const noteOff = (channel: number, note: number, frame = 0): In => ({ frame, data: [0x80 | channel, note, 0] });
 const serial = (sent: Sent[]): number[] => bytesOn(sent, "serialIn").map(([, b]) => b);
 const clocks = (sent: Sent[]): number => serial(sent).filter((b) => b === 0xf8).length;
-const midiOut = (sent: Sent[]): number[][] => sent.filter((s) => s.port === "midiOut").map((s) => s.data);
 
-test("its ports: the routed MIDI, the launches and the Game Boy's serial out in; serial in, buttons and MIDI out", () => {
-  expect(lsdj("midiSync").ports()).toEqual([
-    "in events midi",
-    "in events launches",
-    "in bytes serialOut",
-    "out bytes serialIn",
-    "out events buttons",
-    "out events midiOut",
-  ]);
+test("its ports: the routed MIDI and the launches in; serial in and buttons out", () => {
+  expect(lsdj("midiSync").ports()).toEqual(["in events midi", "in events launches", "out bytes serialIn", "out events buttons"]);
   expect(lsdjSyncNode.descriptor().id).toBe(LSDJ_SYNC_NODE_TYPE);
-  const caps = lsdjSyncNode.layout(lsdjSyncNode.parseSettings({})).ports.slice(3).map((p) => p.capacity);
-  expect(caps).toEqual([{ messages: 4096, bytes: 73728 }, { messages: 64, bytes: 128 }, { messages: 1024, bytes: 8192 }]);
+  const caps = lsdjSyncNode.layout(lsdjSyncNode.parseSettings({})).ports.slice(2).map((p) => p.capacity);
+  expect(caps).toEqual([{ messages: 4096, bytes: 73728 }, { messages: 64, bytes: 128 }]);
   expect(lsdjSyncNode.reshapes).toBe(true);
 });
 
@@ -193,91 +185,14 @@ test("a run a throw leaves is sent, and a run a stopped call left is never sent
   expect(run(withMidi([noteOn(0, 61, 7)], { frames: 1024 })).map((s) => [s.frame, ...s.data])).toEqual([[7, 0x90, 61, 100]]);
 });
 
-// --- MI.OUT (mode 7) and master sync (mode 8): the Game Boy's serial out, a block late ---------------------------------
+// --- MI.OUT (mode 7) and master sync (mode 8): rp.lsdj.serial-out's -------------------------------------------------
 
-test("MI.OUT: the serial-out bytes decode to host MIDI at frame 0, and nothing reaches serial in", () => {
-  // A byte every eight frames, as SameBoy clocks MI.OUT's port (one bit an audio frame); each flag-frames one protocol
-  // byte (0x80 | cmd): START, clock, NoteOn ch0 note 0x69.
-  const framed = (cmd: number, frame: number): In => ({ frame, data: [0x80 | (cmd & 0x7f)] });
-  const sent = lsdj("midiOut").run({ frames: 1024, inputs: { serialOut: [framed(0x7d, 7), framed(0x7f, 15), framed(0x70, 23), framed(0x69, 31)] } });
-  expect(midiOut(sent)).toEqual([[0xfa], [0xf8], [0x90, 0x69, 0x7f]]);
-  expect(sent.every((s) => s.port === "midiOut" && s.frame === 0)).toBe(true);
-});
-
-test("MI.OUT: a frame split across two blocks' bytes decodes once, its partial bits held between them", () => {
-  const rig = lsdj("midiOut");
-  expect(rig.run({ frames: 1024, inputs: { serialOut: [{ frame: 0, data: [0x7f] }] } })).toEqual([]);
-  expect(midiOut(rig.run({ frames: 1024, inputs: { serialOut: [{ frame: 0, data: [0x80] }] } }))).toEqual([[0xf8]]);
-});
-
-test("master sync: each byte a clock, the first opening the run with LSDj's song row and 0xFA", () => {
-  const sent = lsdj("masterSync").run({ frames: 1024, inputs: { serialOut: [{ frame: 0, data: [5] }, { frame: 900, data: [6] }] } });
-  expect(midiOut(sent)).toEqual([[0x90, 5, 0x7f], [0xfa], [0xf8], [0xf8]]);
-  expect(bytesOn(sent, "serialIn")).toEqual([]); // a pure decoder: no serial in
-});
-
-test("master sync times its stop from the byte's own frame, which only the feedback wire's frame can tell", () => {
-  // A run's one byte at frame 0 or at frame 1000 of a 1024-frame block: the 0xFC goes out in the first block that
-  // starts more than 200 ms (8820 frames) after the byte.
-  const stopsIn = (frame: number) => {
-    const rig = lsdj("masterSync");
-    for (let b = 0; b < 16; b++) {
-      const sent = rig.run({ frames: 1024, playing: true, inputs: b === 0 ? { serialOut: [{ frame, data: [5] }] } : {} });
-      if (midiOut(sent).some((m) => m[0] === 0xfc)) return b;
-    }
-    return -1;
-  };
-  expect(stopsIn(0)).toBe(8); // the byte 1024 frames before block 0's start: 1024 + 8 * 1024 > 8820
-  expect(stopsIn(1000)).toBe(9); // 24 frames before it: 24 + 9 * 1024 > 8820
-});
-
-test("master sync's 200 ms stop is 200 ms at the host's rate: 9600 frames at 48 kHz, not 8820", () => {
-  const stopsIn = (frame: number) => {
-    const rig = new NodeRig(lsdjSyncNode, { mode: "masterSync" }, 48000);
-    for (let b = 0; b < 16; b++) {
-      const sent = rig.run({ frames: 1024, sampleRate: 48000, playing: true, inputs: b === 0 ? { serialOut: [{ frame, data: [5] }] } : {} });
-      if (midiOut(sent).some((m) => m[0] === 0xfc)) return b;
-    }
-    return -1;
-  };
-  expect(stopsIn(0)).toBe(9); // 1024 + 9 * 1024 = 10240 > 9600, where 1024 + 8 * 1024 = 9216 is not (and was at 44.1 kHz)
-  expect(stopsIn(1000)).toBe(10); // 24 + 10 * 1024 = 10264 > 9600; 24 + 9 * 1024 = 9240 is not
-});
-
-test("leaving master sync drops its state, so coming back opens a fresh run", () => {
-  const rig = lsdj("masterSync");
-  const block = (serialOut: In[]) => midiOut(rig.run({ frames: 1024, inputs: { serialOut } }));
-  expect(block([{ frame: 100, data: [5] }])).toEqual([[0x90, 5, 0x7f], [0xfa], [0xf8]]); // a run opens
-  rig.set({ mode: "off" });
-  block([]);
-  rig.set({ mode: "masterSync" });
-  // Kept, the stale open run would swallow this run's NoteOn and 0xFA.
-  expect(block([{ frame: 0, data: [7] }])).toEqual([[0x90, 7, 0x7f], [0xfa], [0xf8]]);
-});
-
-test("MI.OUT and master sync send each message from an array the node made once, one a message length", () => {
-  // The rig keeps what send was handed: in a warm block the node passes the same arrays it passed in the first, one for
-  // each length a message has, so it makes none for a message it decodes.
-  const framed = (cmd: number, frame: number): In => ({ frame, data: [0x80 | (cmd & 0x7f)] });
-  // MI.OUT: a clock (1 byte), a NoteOn (3) and a program change (2), the same each block.
-  const miOut = [framed(0x7f, 7), framed(0x70, 15), framed(60, 23), framed(0x78, 31), framed(5, 39)];
-  // Master sync: a run's first block sends its NoteOn (3), 0xFA and a clock (1), and each block after a clock.
-  const master = (b: number): In[] => [{ frame: 100, data: [b === 0 ? 4 : 9] }];
-  for (const [mode, serialOut] of [["midiOut", () => miOut], ["masterSync", master]] as const) {
-    const rig = lsdj(mode);
-    const byLength = new Map<number, ArrayLike<number>>();
-    for (let b = 0; b < 4; b++) {
-      const sent = rig.run({ frames: 1024, inputs: { serialOut: serialOut(b) } });
-      expect(sent.length > 0).toBeTruthy();
-      sent.forEach((s, i) => {
-        const source = rig.sources[i];
-        expect(s.port).toBe("midiOut");
-        expect(source.length).toBe(s.data.length);
-        if (!byLength.has(source.length)) byLength.set(source.length, source);
-        expect(byLength.get(source.length) === source).toBeTruthy();
-      });
-    }
-    expect([...byLength.keys()].sort()).toEqual(mode === "midiOut" ? [1, 2, 3] : [1, 3]);
+test("MI.OUT and master sync send nothing: the Game Boy's serial out is decoded after the Game Boy, not here", () => {
+  // The routed MIDI and the transport each mode could act on, reaching neither the cart nor the host.
+  for (const mode of ["midiOut", "masterSync"]) {
+    const rig = lsdj(mode, { autoStart: true });
+    expect(rig.run(withMidi([noteOn(0, 24), noteOn(0, 40, 9)], { playing: true })), mode).toEqual([]);
+    expect(rig.run(beat({ ppq: 1, playing: true })), mode).toEqual([]);
   }
 });
 
````

Run: `pnpm test script/nodes/lsdj-serial-out; pnpm test script/nodes/lsdj-sync; pnpm test script/bundle-no-zod`
Expected: `10 passed, 0 skipped, 0 failed`, `17 passed, 0 skipped, 0 failed`, `1 passed, 0 skipped, 0 failed`.

- [ ] **Step 5: Place the node after its Game Boy**

`rp.lsdj.sync`'s spec drops `serialOut` and `midiOut` and names its after-node (`out`), whose own spec reads its
system's `serialOut` and sends host MIDI; `outNodeOf` reads it. `buildGraph` pushes each after-node once the systems
and their link wires are in, in slot order, id `sys-<id>/lsdj-sync/out`, with exactly its before-node's settings (the
MI.MAP controller's forced mode included), only on a Game Boy; it wires `sys-<id>.serialOut` to it plainly and its
`midiOut` into `rp.host.midi-out` at the input its before-node's had, so the host's inputs and their `systems` stay as
they were. The tests hold the spec, the placement and the table; the store's tests read a system's nodes through a
`roleNodes()` helper, since the mock backend lists the decoder among them:

````diff
diff --git a/packages/retroplug/src/backend.ts b/packages/retroplug/src/backend.ts
index 54648d2a..37efff72 100644
--- a/packages/retroplug/src/backend.ts
+++ b/packages/retroplug/src/backend.ts
@@ -142,8 +142,8 @@ export interface Backend {
   applyRoleConfig(id: number, kind: string, config: Record<string, unknown>): boolean;
 
   /** Set capture of a system's outgoing serial bytes (SerialOutCapture), or null for off. The store calls this when
-   *  a system's `lsdj-sync` mode changes: armed, the Game Boy node's `serialOut` port carries the raw bytes, a feedback
-   *  wire into its rp.lsdj.sync node, for the MI.OUT and master-sync decoders. False for a mode native does not know.
+   *  a system's `lsdj-sync` mode changes: armed, the Game Boy node's `serialOut` port carries the raw bytes, a wire
+   *  into its rp.lsdj.serial-out node, for the MI.OUT and master-sync decoders. False for a mode native does not know.
    *  SameBoy-only (a no-op on other cores). */
   setSerialOutCapture(id: number, mode: SerialOutCapture | null): boolean;
 
diff --git a/packages/retroplug/src/graph/buildGraph.ts b/packages/retroplug/src/graph/buildGraph.ts
index 769704a2..0d7c9b5d 100644
--- a/packages/retroplug/src/graph/buildGraph.ts
+++ b/packages/retroplug/src/graph/buildGraph.ts
@@ -2,12 +2,20 @@
 // order, the project's audio routing and the host's output shape, turned into the document setGraph plans. Ahead of
 // the systems, each document places the script nodes that feed them (docs/modular-graph-phase-b3b-design.md
 // section 4): the MIDI routing, a controller session when one is enabled, and a protocol node for each role of each
-// system that has one, wired between the host's I/O and the systems as the DSP kernel's ports were.
+// system that has one, wired between the host's I/O and the systems as the DSP kernel's ports were. After the
+// systems, it places each role's second node where its type names one, which reads what its system sent in the same
+// block: LSDj's serial-out decoder, whose host MIDI goes where the kernel's did (docs/lsdj-serial-out-design.md).
 // Pure, and held to test/graph/golden/build-graph.json, whose audio wiring is the B1 native translator's, so every
 // audio node id, the document order (which is the plan order on ties) and every per-lane sum is the translator's.
 import type { AudioRouting, MidiRouting } from "../settingsEnums";
 import type { GraphDocument, NodeDoc, WireDoc } from "./graphDocument";
-import { PROTOCOL_NODES, type ProtocolNodeSpec, type ProtocolRole, type SystemInput } from "./protocolNodes";
+import {
+  outNodeOf,
+  PROTOCOL_NODES,
+  type ProtocolNodeSpec,
+  type ProtocolRole,
+  type SystemInput,
+} from "./protocolNodes";
 import { controllerSettings, controllerSyncOverride, controllerTarget, NO_SYSTEM, type GraphController } from "./controller";
 
 export type SystemNodeType = "rp.sameboy.gb" | "rp.mesen.nes" | "rp.mesen.gba" | "rp.mesen.sms";
@@ -144,9 +152,10 @@ export function buildGraph(model: GraphModel): BuiltGraph {
   };
 
   nodes.push({ id: "midi-in", type: "rp.host.midi-in" }, { id: "controller-in", type: "rp.host.controller-in" });
-  // Every script node goes before the systems in the document, so before them in the plan: the planner breaks ties by
-  // document order (Planner.cpp order()), and nothing a script node reads comes from a node after it but over a
-  // feedback wire.
+  // A script node reads only nodes placed before it in the document, or a feedback wire, so the plan runs the script
+  // nodes in document order: the planner orders nodes topologically and breaks ties by document order (Planner.cpp
+  // order()), and a feedback wire orders nothing. So the nodes that feed the systems go before them, and a node that
+  // reads what a system sent goes after the systems.
   //
   // The routing: the host's MIDI fanned out to the systems in slot order (positional routing reads it), an output
   // for each, sys-<id>.
@@ -156,7 +165,7 @@ export function buildGraph(model: GraphModel): BuiltGraph {
     wire("midi-in.midi", "routing.midiIn");
   }
   // Host MIDI out's inputs, in the order the kernel emitted: the controller's first, then each system's in slot
-  // order, every message under the system named beside it.
+  // order, every message under the system named beside it, wherever in the document the node that sends it sits.
   const hostMidi: { from: string; system: number }[] = [];
   const controller = model.controller ?? null;
   const target = controller ? controllerTarget(controller, systems) : NO_SYSTEM;
@@ -170,6 +179,9 @@ export function buildGraph(model: GraphModel): BuiltGraph {
   }
   // The MI.MAP controller's cart, in the lsdj-sync mode it needs: that node's settings alone, never the project's.
   const sync = controllerSyncOverride(systems, controller);
+  // The nodes placed after the systems, in slot order, then pipeline order: each role's second node, and the system
+  // output it reads, if any.
+  const after: { node: NodeDoc; reads: string | null }[] = [];
   // Each system's protocol nodes, in slot order, then pipeline order. Their outputs feed the system's inputs of the
   // same name; what reads MIDI reads its system's routed output.
   for (const s of systems) {
@@ -178,16 +190,23 @@ export function buildGraph(model: GraphModel): BuiltGraph {
       const spec: ProtocolNodeSpec = PROTOCOL_NODES[role.node];
       const id = protocolId(s.id, role.kind);
       const forced = sync !== null && sync.systemId === s.id && role.node === "rp.lsdj.sync";
-      nodes.push({ id, type: role.node, settings: forced ? { ...role.config, mode: sync.mode } : role.config });
+      const settings = forced ? { ...role.config, mode: sync.mode } : role.config;
+      nodes.push({ id, type: role.node, settings });
       if (spec.midiIn !== null && midiRouting !== null) wire(`routing.${n}`, `${id}.${spec.midiIn}`);
       // The launches follow the routed MIDI, as the kernel appended them to its target's inbox.
       if (spec.launches && controller?.target === "system" && target === s.id)
         wire("controller.launches", `${id}.launches`);
       for (const input of spec.writes) wire(`${id}.${input}`, `${n}.${input}`);
-      // MI.OUT comes back from the Game Boy a block after the core sent it, as it always has.
-      if (spec.serialOut && s.type === "rp.sameboy.gb")
-        wires.push({ from: `${n}.serialOut`, to: `${id}.serialOut`, feedback: true });
       if (spec.midiOut) hostMidi.push({ from: `${id}.midiOut`, system: s.id });
+      // The role's second node takes the same settings, so the two read one mode, the controller's forced one
+      // included. One that reads a serial out is placed only on a Game Boy, the one system that has one.
+      const outType = outNodeOf(role.node);
+      if (outType === undefined) continue;
+      const out: ProtocolNodeSpec = PROTOCOL_NODES[outType];
+      if (out.serialOut && s.type !== "rp.sameboy.gb") continue;
+      const outId = `${id}/out`;
+      after.push({ node: { id: outId, type: outType, settings }, reads: out.serialOut ? `${n}.serialOut` : null });
+      if (out.midiOut) hostMidi.push({ from: `${outId}.midiOut`, system: s.id });
     }
   }
   const groups = linkGroups(systems);
@@ -195,6 +214,12 @@ export function buildGraph(model: GraphModel): BuiltGraph {
   for (const i of order) nodes.push({ id: sysNode(systems[i].id), type: systems[i].type, settings: { core: systems[i].id } });
   for (const g of groups)
     for (let k = 1; k < g.length; ++k) wire(`${sysNode(systems[g[k - 1]].id)}.link`, `${sysNode(systems[g[k]].id)}.link`);
+  // After the systems, so each reads its system's output of this block: a Game Boy's serial out, which LSDj's
+  // decoder turns into host MIDI at each byte's frame.
+  for (const a of after) {
+    nodes.push(a.node);
+    if (a.reads !== null) wire(a.reads, `${a.node.id}.serialOut`);
+  }
   // A physical N8 mirrors the first NES in slot order: what its protocol nodes send that system's core MIDI and core
   // bytes, they send the cart.
   const cart = systems.find((s) => s.type === "rp.mesen.nes");
diff --git a/packages/retroplug/src/graph/protocolNodes.ts b/packages/retroplug/src/graph/protocolNodes.ts
index 29cab6cf..a9943339 100644
--- a/packages/retroplug/src/graph/protocolNodes.ts
+++ b/packages/retroplug/src/graph/protocolNodes.ts
@@ -1,7 +1,9 @@
 // The protocol nodes a system's roles are placed as (docs/modular-graph-phase-b3b-design.md section 2): which role
 // kind has a script node, its type, and how buildGraph wires each type between the routing, the controller, its
-// system and the host. Pure and zod-free, so buildGraph, both stores and a DirectGraph read the same table, with or
-// without a role registry.
+// system and the host. A role's node is placed before the systems, feeding its system's inputs; its type may name one
+// more for the role, placed after the systems, which reads what its system sent in the same block (`out`: LSDj's
+// serial-out decoder, docs/lsdj-serial-out-design.md section 3). Pure and zod-free, so buildGraph, both stores and a
+// DirectGraph read the same table, with or without a role registry.
 //
 // The mapping lives here rather than on RoleType because the node types are code in the script-node bundle every
 // Engine embeds: which role kinds have one is a fact of that bundle, not of whichever registry a store was handed,
@@ -22,11 +24,15 @@ export interface ProtocolNodeSpec {
   writes: readonly SystemInput[];
   /** It takes the controller's launches on `launches` when its system is the controller's target. */
   launches?: true;
-  /** It takes its Game Boy's serial out on `serialOut`, a block late (a feedback wire): what MI.OUT and master sync
-   *  decode. */
+  /** It takes its Game Boy's serial out on `serialOut`, in the block the core captured it, over a forward wire from
+   *  the system, so it is placed after its system: what MI.OUT and master sync decode. */
   serialOut?: true;
   /** It sends host MIDI on `midiOut`, under its system's id. */
   midiOut?: true;
+  /** The type of the second node buildGraph places for the role, after the systems, as sys-<id>/<kind>/out with the
+   *  same settings: a PROTOCOL_NODES type that writes no system input, so the one-writer rule below is the first
+   *  node's alone. */
+  out?: string;
 }
 
 // Each type's id as its node module spells it (script/nodes/*.ts), and each port name as that module declares it, which
@@ -41,19 +47,30 @@ export interface ProtocolNodeSpec {
 // (protocolPipeline), where the kernel merged what both sent; the store orders a loaded system's roles so that, of two
 // that collide, the one its ROM calls for comes first (SystemsStore.suggestedFirst). A kind named twice (a hand-edited
 // file) is left out the same way, as its node id, sys-<id>/<kind>, is taken, which the planner refuses too.
+//
+// An lsdj-sync role is two nodes: rp.lsdj.sync, before its Game Boy, takes the host's side into the cart (the routed
+// MIDI and the launches in, serial in and buttons out), and rp.lsdj.serial-out, after it, the cart's side to the host
+// (the Game Boy's serial out in, host MIDI out).
 export const PROTOCOL_NODES = {
   "rp.mgb.midi": { midiIn: "midi", writes: ["serialIn"] },
   "rp.n8.midi-to-fifo": { midiIn: "midiIn", writes: ["midi"] },
   "rp.risa.sync": { midiIn: null, writes: ["bytes"] },
   "rp.sms.sync": { midiIn: null, writes: ["bytes"] },
-  "rp.lsdj.sync": { midiIn: "midi", writes: ["serialIn", "buttons"], launches: true, serialOut: true, midiOut: true },
+  "rp.lsdj.sync": { midiIn: "midi", writes: ["serialIn", "buttons"], launches: true, out: "rp.lsdj.serial-out" },
+  "rp.lsdj.serial-out": { midiIn: null, writes: [], serialOut: true, midiOut: true },
 } as const satisfies Record<string, ProtocolNodeSpec>;
 
 export type ProtocolNodeType = keyof typeof PROTOCOL_NODES;
 
-/** The role kinds that have a protocol node (dspRoles.ts registers them), each with the node type it is placed as.
- *  Exported for test/graph/protocol-nodes.test.ts, which holds each type to one the script bundle registers; everything
- *  else reads it through protocolNodeOf. */
+/** The type of the node placed after its system for a role whose node is of type `node`, or undefined for none. */
+export function outNodeOf(node: ProtocolNodeType): ProtocolNodeType | undefined {
+  // A PROTOCOL_NODES key, which test/graph/protocol-nodes.test.ts holds every `out` to.
+  return (PROTOCOL_NODES[node] as ProtocolNodeSpec).out as ProtocolNodeType | undefined;
+}
+
+/** The role kinds that have a protocol node (dspRoles.ts registers them), each with the node type it is placed as,
+ *  before its system (its `out`, after it, follows from the type). Exported for test/graph/protocol-nodes.test.ts,
+ *  which holds each type to one the script bundle registers; everything else reads it through protocolNodeOf. */
 export const ROLE_NODES: Readonly<Record<string, ProtocolNodeType>> = {
   mgb: "rp.mgb.midi",
   "nes-n8-midi": "rp.n8.midi-to-fifo",
@@ -63,9 +80,9 @@ export const ROLE_NODES: Readonly<Record<string, ProtocolNodeType>> = {
 };
 
 /** A role on a system as buildGraph places it: its node, `sys-<id>/<kind>`, of type `node`, with the role's config as
- *  its settings. A store with a registry that registers the kind hands it as the role's schema parsed it (an edit, a
- *  fresh system's defaults and a loaded project's alike); a DirectGraph, or a store with no registry, as it was
- *  given. */
+ *  its settings (and its type's `out`, when it names one, `sys-<id>/<kind>/out`, with the same). A store with a
+ *  registry that registers the kind hands it as the role's schema parsed it (an edit, a fresh system's defaults and a
+ *  loaded project's alike); a DirectGraph, or a store with no registry, as it was given. */
 export interface ProtocolRole {
   kind: string;
   node: ProtocolNodeType;
diff --git a/packages/retroplug/test/graph/build-graph-role-goldens.test.ts b/packages/retroplug/test/graph/build-graph-role-goldens.test.ts
index 208bd68c..96634254 100644
--- a/packages/retroplug/test/graph/build-graph-role-goldens.test.ts
+++ b/packages/retroplug/test/graph/build-graph-role-goldens.test.ts
@@ -19,12 +19,12 @@
 // packages/native/test/engine-graph/BuildGraphGoldens.test.cpp plans every document of this file on the engine with
 // real cores, beside build-graph.json's, and runs it twenty blocks with the transport playing, host MIDI and a control
 // surface's press staged, so CI plans buildGraph's per-role wiring on the real engine: each protocol node type against
-// the system node it feeds, the routing in each mode, a controller each way, the MI.OUT feedback wire and the N8 cart
-// mirror. Every document must plan, and its blocks fault no node, throw in no script node and drop no message.
+// the system node it feeds, the routing in each mode, a controller each way, LSDj's decoder after its Game Boy and the
+// N8 cart mirror. Every document must plan, and its blocks fault no node, throw in no script node and drop no message.
 import { test, expect } from "../../testing/harness";
 import { canonicalJson } from "../../src/graph/canonicalJson";
 import { buildGraph, type GraphModel, type GraphSystem, type HostShape, type SystemNodeType, type TapArm } from "../../src/graph/buildGraph";
-import { protocolPipeline } from "../../src/graph/protocolNodes";
+import { outNodeOf, protocolPipeline } from "../../src/graph/protocolNodes";
 import type { GraphController } from "../../src/graph/controller";
 import type { RoleInstance } from "../../src/systemRoles";
 import { MIDI_ROUTING_VALUES, type ControllerTarget } from "../../src/settingsEnums";
@@ -95,8 +95,8 @@ const model = (systems: GraphSystem[], over: Partial<GraphModel> = {}): GraphMod
 });
 
 // Every protocol node type on the system type it is placed for: mGB and an LSDj decoding MI.OUT on Game Boys (the
-// LSDj's serial out fed back to it, its host MIDI out an input of rp.host.midi-out), the N8 and risa on a NES (which
-// the cart mirrors), smsggdj on an SMS.
+// LSDj's serial out read by its decoder after the systems, whose host MIDI out is an input of rp.host.midi-out), the
+// N8 and risa on a NES (which the cart mirrors), smsggdj on an SMS.
 const everyNode = (): GraphSystem[] => [gb(1, [MGB]), gb(2, [lsdj("midiOut")]), nes(3, [N8, RISA]), sms(4, [smsSync("sms")])];
 
 const CASES: Case[] = [
@@ -123,8 +123,8 @@ const CASES: Case[] = [
   },
   // No system to resolve "the first" to: the controller's host MIDI is tagged 4294967295, the node's system 0.
   { name: "controller/no-systems", model: model([], { controller: launchpad("system", 0) }) },
-  // The protocol nodes stay in slot order while a link group moves the systems' own order; MI.OUT comes back to each
-  // linked LSDj.
+  // The protocol nodes stay in slot order while a link group moves the systems' own order, before the systems and
+  // after them: the linked LSDj in MI.OUT has its decoder after every system.
   {
     name: "audio/linked-lsdj/twoPerInstance/multi4",
     model: model([gb(1, [lsdj("midiSync")], 1), gb(2, [MGB]), gb(3, [lsdj("midiOut")], 1)], {
@@ -269,18 +269,25 @@ test("the goldens are what buildGraph builds for every case, and nothing else",
 });
 
 test("every protocol node type, both controller targets and every routing mode are planned", () => {
-  // What makes the file the CI check of buildGraph's per-role wiring on the engine: each of these is in some case.
+  // What makes the file the CI check of buildGraph's per-role wiring on the engine: each of these is in some case, the
+  // node a role's type places after its system (outNodeOf) among them.
   const types = new Set<string>();
   const targets = new Set<string>();
   const modes = new Set<string>();
   for (const c of CASES) {
-    for (const s of c.model.systems) for (const r of s.pipeline) types.add(`${r.node} on ${s.type}`);
+    for (const s of c.model.systems)
+      for (const r of s.pipeline) {
+        types.add(`${r.node} on ${s.type}`);
+        const out = outNodeOf(r.node);
+        if (out !== undefined) types.add(`${out} on ${s.type}`);
+      }
     if (c.model.controller) targets.add(c.model.controller.target);
     if (c.model.midiRouting) modes.add(c.model.midiRouting);
   }
   for (const t of [
     "rp.mgb.midi on rp.sameboy.gb",
     "rp.lsdj.sync on rp.sameboy.gb",
+    "rp.lsdj.serial-out on rp.sameboy.gb",
     "rp.n8.midi-to-fifo on rp.mesen.nes",
     "rp.risa.sync on rp.mesen.nes",
     "rp.sms.sync on rp.mesen.sms",
diff --git a/packages/retroplug/test/graph/build-graph.test.ts b/packages/retroplug/test/graph/build-graph.test.ts
index 9087e3de..27e79247 100644
--- a/packages/retroplug/test/graph/build-graph.test.ts
+++ b/packages/retroplug/test/graph/build-graph.test.ts
@@ -3,7 +3,8 @@
 // engine stepped them (unlinked by slot, then each link group). These are the B1 translator's own tests,
 // carried over when buildGraph replaced it; build-graph-goldens.test.ts holds the two builders to each other.
 // Then the script nodes ahead of the systems and the host's I/O around them (docs/modular-graph-phase-b3b-design.md
-// section 4): the MIDI routing, the controller and each role's protocol node, wired as the DSP kernel's ports were.
+// section 4): the MIDI routing, the controller and each role's protocol node, wired as the DSP kernel's ports were; and
+// after the systems, LSDj's serial-out decoder (docs/lsdj-serial-out-design.md section 3).
 import { test, expect } from "../../testing/harness";
 import { buildGraph, type GraphModel, type GraphSystem, type HostShape } from "../../src/graph/buildGraph";
 import { protocolPipeline, type LeftOutRole, type ProtocolRole } from "../../src/graph/protocolNodes";
@@ -15,13 +16,14 @@ import { N8_MIDI_TO_FIFO_NODE_TYPE } from "../../src/script/nodes/n8MidiToFifo";
 import { RISA_SYNC_NODE_TYPE } from "../../src/script/nodes/risaSync";
 import { SMS_SYNC_NODE_TYPE } from "../../src/script/nodes/smsSync";
 import { LSDJ_SYNC_NODE_TYPE } from "../../src/script/nodes/lsdjSync";
+import { LSDJ_SERIAL_OUT_NODE_TYPE } from "../../src/script/nodes/lsdjSerialOut";
 import type { AudioRouting, MidiRouting } from "../../src/settingsEnums";
 
 const gb = (id: number, linkGroup = 0, pipeline: ProtocolRole[] = []): GraphSystem => ({ id, type: "rp.sameboy.gb", linkGroup, exportMode: 0, pipeline });
 const nes = (id: number, exportMode = 0, pipeline: ProtocolRole[] = []): GraphSystem => ({ id, type: "rp.mesen.nes", linkGroup: 0, exportMode, pipeline });
 const multi = (channels: number): HostShape => ({ kind: "multi", channels });
 
-// The host's I/O and the script nodes ahead of the systems, which every document carries; the B1 tables below are the
+// The host's I/O and the script nodes around the systems, which every document carries; the B1 tables below are the
 // rest, the audio.
 const AROUND_THE_AUDIO = new Set(["midi-in", "controller-in", "routing", "controller", "cart", "midi-out", "controller-out"]);
 const aroundTheAudio = (id: string): boolean => AROUND_THE_AUDIO.has(id) || id.includes("/");
@@ -243,20 +245,26 @@ const withController = (over: Partial<GraphController> = {}): Project => ({ midi
 test("each type buildGraph places is the id its node module registers", () => {
   const types = (g: ReturnType<typeof built>) => g.document.nodes.map((n) => n.type);
   const g = built([gb(1, 0, [MGB]), nes(2, 0, [N8, RISA]), sms(3, [SMS]), gb(4, 0, [LSDJ()])], "stereo", multi(2), withController());
-  for (const type of [MIDI_ROUTE_NODE_TYPE, CONTROLLER_SESSION_NODE_TYPE, MGB_MIDI_NODE_TYPE, N8_MIDI_TO_FIFO_NODE_TYPE, RISA_SYNC_NODE_TYPE, SMS_SYNC_NODE_TYPE, LSDJ_SYNC_NODE_TYPE])
+  for (const type of [MIDI_ROUTE_NODE_TYPE, CONTROLLER_SESSION_NODE_TYPE, MGB_MIDI_NODE_TYPE, N8_MIDI_TO_FIFO_NODE_TYPE, RISA_SYNC_NODE_TYPE, SMS_SYNC_NODE_TYPE, LSDJ_SYNC_NODE_TYPE, LSDJ_SERIAL_OUT_NODE_TYPE])
     expect(types(g).includes(type)).toBe(true);
   expect(protocolPipeline([{ kind: "sameboy", config: {} }, { kind: "lsdj-assets", config: {} }, { kind: "risa", config: {} }])).toEqual([]);
 });
 
-test("every script node is placed before the systems: the routing, the controller, then each system's in slot then pipeline order", () => {
-  // Slot order, not the sum order a link group gives the systems themselves.
+test("the script nodes that feed the systems go before them, in slot then pipeline order, and LSDj's decoder after them", () => {
+  // Slot order, not the sum order a link group gives the systems themselves: the routing, the controller, each system's
+  // protocol nodes, the systems and their link cable, then each node that reads what a system sent, before the cart
+  // mirror and the host outputs.
   const g = built([gb(1, 1, [MGB]), nes(2, 0, [N8, RISA]), gb(3, 1, [LSDJ()])], "stereo", multi(2), withController());
-  expect(g.nodeOrder.slice(0, 11)).toEqual([
+  expect(g.nodeOrder.slice(0, 14)).toEqual([
     "midi-in", "controller-in", "routing", "controller", "sys-1/mgb", "sys-2/nes-n8-midi", "sys-2/risa-sync", "sys-3/lsdj-sync",
-    "sys-2", "sys-1", "sys-3",
+    "sys-2", "sys-1", "sys-3", "sys-3/lsdj-sync/out", "cart", "midi-out",
   ]);
   // Each protocol node takes its role's config as its settings.
   expect(g.node("sys-3/lsdj-sync")).toEqual({ id: "sys-3/lsdj-sync", type: LSDJ_SYNC_NODE_TYPE, settings: LSDJ().config });
+  expect(g.node("sys-3/lsdj-sync/out")).toEqual({ id: "sys-3/lsdj-sync/out", type: LSDJ_SERIAL_OUT_NODE_TYPE, settings: LSDJ().config });
+  // The link cable's wire is the systems', and the decoder's forward wire follows it.
+  const wireIndex = (from: string) => g.document.wires.findIndex((w) => w.from === from);
+  expect(wireIndex("sys-1.link") < wireIndex("sys-3.serialOut")).toBe(true);
 });
 
 test("the routing fans the host's MIDI out to the systems in slot order, into each node that reads it", () => {
@@ -315,10 +323,33 @@ test("of two roles whose nodes write one system input, the first in role order i
   expect(none).toEqual([]);
 });
 
-test("MI.OUT comes back from a Game Boy a block later, into its lsdj-sync node and nowhere else", () => {
+test("a Game Boy's serial out goes forward into its LSDj's decoder, whose host MIDI is the system's input of midi-out", () => {
+  // Read in the block the core captured it: a plain wire into a node placed after the system, and no feedback wire in
+  // the document. The decoder writes no system input, and its rp.lsdj.sync neither reads the serial out nor sends host
+  // MIDI.
   const g = built([gb(1, 0, [MGB]), gb(2, 0, [LSDJ("midiOut")]), gb(3)]);
-  const back = g.document.wires.filter((w) => w.feedback);
-  expect(back).toEqual([{ from: "sys-2.serialOut", to: "sys-2/lsdj-sync.serialOut", feedback: true }]);
+  expect(g.document.wires.filter((w) => w.feedback)).toEqual([]);
+  expect(g.document.wires.filter((w) => w.from.endsWith(".serialOut"))).toEqual([{ from: "sys-2.serialOut", to: "sys-2/lsdj-sync/out.serialOut" }]);
+  expect(g.wired("sys-2.serialOut", "sys-2/lsdj-sync/out.serialOut")).toBe(true);
+  const at = (id: string) => g.nodeOrder.indexOf(id);
+  for (const sys of ["sys-1", "sys-2", "sys-3"]) expect(at(sys) < at("sys-2/lsdj-sync/out"), sys).toBe(true);
+  expect(g.node("midi-out")?.settings).toEqual({ systems: [2] });
+  expect(g.into("midi-out.in1")).toEqual(["sys-2/lsdj-sync/out.midiOut"]);
+  expect(g.document.wires.filter((w) => w.from.startsWith("sys-2/lsdj-sync/out.")).map((w) => w.to)).toEqual(["midi-out.in1"]);
+  expect(g.document.wires.some((w) => w.from === "sys-2/lsdj-sync.midiOut" || w.to === "sys-2/lsdj-sync.serialOut")).toBe(false);
+  // Placed whatever the mode: a mode switch is a change of settings, never of the document.
+  for (const mode of ["midiSync", "masterSync", "off"])
+    expect(built([gb(2, 0, [LSDJ(mode)])]).node("sys-2/lsdj-sync/out")?.settings, mode).toEqual(LSDJ(mode).config);
+});
+
+test("LSDj's decoder is placed only on a Game Boy, the one system with a serial out to decode", () => {
+  // A hand-edited project can hand lsdj-sync to another system: its rp.lsdj.sync is placed, and nothing reads a serial
+  // out it has not got, so host MIDI out takes no input for it.
+  const g = built([nes(4, 0, [LSDJ("midiOut")])]);
+  expect(g.node("sys-4/lsdj-sync")?.type).toBe(LSDJ_SYNC_NODE_TYPE);
+  expect(g.node("sys-4/lsdj-sync/out")).toBe(undefined);
+  expect(g.node("midi-out")?.settings).toEqual({ systems: [] });
+  expect(g.document.wires.some((w) => w.from.endsWith(".serialOut"))).toBe(false);
 });
 
 test("the controller takes the surface, sends it its LEDs, and its target ('0 is the first system') resolved", () => {
@@ -359,10 +390,13 @@ test("the controller's launches reach only a 'system' target's lsdj-sync node",
   expect(gone.node("controller")?.settings?.systemId).toBe(9);
 });
 
-test("host MIDI out takes the controller's first, then each lsdj-sync node's in slot order, each under its system", () => {
+test("host MIDI out takes the controller's first, then each LSDj decoder's in slot order, each under its system", () => {
   const g = built([gb(3, 1, [LSDJ("midiOut")]), gb(1, 0, [MGB]), gb(2, 1, [LSDJ("masterSync")])], "stereo", multi(2), withController({ systemId: 2 }));
   expect(g.node("midi-out")?.settings).toEqual({ systems: [2, 3, 2] });
-  expect([1, 2, 3].map((k) => g.into(`midi-out.in${k}`))).toEqual([["controller.midiOut"], ["sys-3/lsdj-sync.midiOut"], ["sys-2/lsdj-sync.midiOut"]]);
+  expect([1, 2, 3].map((k) => g.into(`midi-out.in${k}`))).toEqual([["controller.midiOut"], ["sys-3/lsdj-sync/out.midiOut"], ["sys-2/lsdj-sync/out.midiOut"]]);
+  // The decoders in slot order too, after the systems the link group reorders.
+  expect(g.nodeOrder.filter((id) => id.endsWith("/out"))).toEqual(["sys-3/lsdj-sync/out", "sys-2/lsdj-sync/out"]);
+  expect(g.nodeOrder.indexOf("sys-3/lsdj-sync/out") > g.nodeOrder.indexOf("sys-2")).toBe(true);
   // With no system to resolve "the first" to, the controller's is tagged as the kernel tagged it (its -1), and the node
   // takes 0, its none.
   const empty = built([], "stereo", multi(2), withController());
@@ -391,12 +425,19 @@ test("the cart mirrors the first NES in slot order, fed what its protocol nodes
 // cart clocked it for a mode it was not in, sent launches nowhere, and left LSDj sitting on "WAIT". Reported from a
 // hardware session. The document forces it in that cart's lsdj-sync node alone, so nothing reaches the `.rplg`.
 
-const syncModeOf = (g: ReturnType<typeof built>, id: number): unknown => g.node(`sys-${id}/lsdj-sync`)?.settings?.mode;
+// Its decoder, after the Game Boy, takes the same settings, so the two always read one mode: syncModeOf holds them to
+// each other.
+const syncModeOf = (g: ReturnType<typeof built>, id: number): unknown => {
+  const mode = g.node(`sys-${id}/lsdj-sync`)?.settings?.mode;
+  expect(g.node(`sys-${id}/lsdj-sync/out`)?.settings?.mode, `sys-${id}'s decoder`).toBe(mode);
+  return mode;
+};
 
 test("an enabled controller drives its cart in MI.MAP, whatever the stored mode said", () => {
   const g = built([gb(1, 0, [LSDJ()])], "stereo", multi(2), withController({ cartSync: "MidiMap" }));
   expect(syncModeOf(g, 1)).toBe("midiMap");
   expect(g.node("sys-1/lsdj-sync")?.settings).toEqual({ mode: "midiMap", tempoDivisor: 1, autoStart: false });
+  expect(g.node("sys-1/lsdj-sync/out")?.settings).toEqual({ mode: "midiMap", tempoDivisor: 1, autoStart: false });
 });
 
 test("systemId 0 means the first system, matching what the node resolves", () => {
diff --git a/packages/retroplug/test/graph/protocol-nodes.test.ts b/packages/retroplug/test/graph/protocol-nodes.test.ts
index 43ccb63a..56393107 100644
--- a/packages/retroplug/test/graph/protocol-nodes.test.ts
+++ b/packages/retroplug/test/graph/protocol-nodes.test.ts
@@ -1,26 +1,28 @@
 // graph/protocolNodes.ts against the node modules it describes. buildGraph wires each protocol node by PROTOCOL_NODES'
-// port names (its routed MIDI's input, the system inputs it writes, its launches, MI.OUT and host MIDI out), and the
-// table spells them without importing the modules, so nothing but this file holds a name to the port its module
-// declares: a name the module does not have is a wire the planner refuses, and with it the whole document, for every
-// project with that role. So each entry's names are held to the ports of the node of that type, each in the direction
-// and kind buildGraph wires it, and the other way round, each port the module declares to an entry's name, since a port
-// the table does not name is one buildGraph never wires. And every type the table names, and every type a role kind is
-// placed as, is one the script-node bundle registers.
+// port names (its routed MIDI's input, the system inputs it writes, its launches, the Game Boy's serial out and host
+// MIDI out), and the table spells them without importing the modules, so nothing but this file holds a name to the port
+// its module declares: a name the module does not have is a wire the planner refuses, and with it the whole document,
+// for every project with that role. So each entry's names are held to the ports of the node of that type, each in the
+// direction and kind buildGraph wires it, and the other way round, each port the module declares to an entry's name,
+// since a port the table does not name is one buildGraph never wires. And every type the table names, every type a
+// role kind is placed as and every type an entry names as the node after its system (`out`) is one the script-node
+// bundle registers.
 import { test, expect } from "../../testing/harness";
 import type { DefinedNode } from "../../src/graph/defineNode";
 import type { PortDesc } from "../../src/graph/descriptor";
-import { PROTOCOL_NODES, ROLE_NODES, protocolNodeOf, type ProtocolNodeSpec } from "../../src/graph/protocolNodes";
+import { PROTOCOL_NODES, ROLE_NODES, outNodeOf, protocolNodeOf, type ProtocolNodeSpec, type ProtocolNodeType } from "../../src/graph/protocolNodes";
 import { SCRIPT_NODES_GLOBAL } from "../../src/script/register";
 import { mgbMidiNode } from "../../src/script/nodes/mgbMidi";
 import { n8MidiToFifoNode } from "../../src/script/nodes/n8MidiToFifo";
 import { risaSyncNode } from "../../src/script/nodes/risaSync";
 import { smsSyncNode } from "../../src/script/nodes/smsSync";
 import { lsdjSyncNode } from "../../src/script/nodes/lsdjSync";
+import { lsdjSerialOutNode } from "../../src/script/nodes/lsdjSerialOut";
 // The production bundle's entry point: importing it registers its node types on SCRIPT_NODES_GLOBAL, as it does in each
 // of the script host's contexts.
 import "../../src/script/bundle";
 
-const PROTOCOL_MODULES: readonly DefinedNode<any>[] = [mgbMidiNode, n8MidiToFifoNode, risaSyncNode, smsSyncNode, lsdjSyncNode];
+const PROTOCOL_MODULES: readonly DefinedNode<any>[] = [mgbMidiNode, n8MidiToFifoNode, risaSyncNode, smsSyncNode, lsdjSyncNode, lsdjSerialOutNode];
 const moduleOf = (type: string): DefinedNode<any> | undefined => PROTOCOL_MODULES.find((d) => d.id === type);
 
 const registered = (): string[] =>
@@ -52,13 +54,25 @@ test("every PROTOCOL_NODES entry names exactly the ports its node module declare
   }
 });
 
-test("every type a role kind is placed as, and every type PROTOCOL_NODES names, is one the script bundle registers", () => {
+test("every type a role kind is placed as, every type PROTOCOL_NODES names and every `out` is one the script bundle registers", () => {
   const types = new Set(registered());
   for (const [kind, type] of Object.entries(ROLE_NODES)) {
     expect(types.has(type), `${kind} -> ${type}`).toBe(true);
     expect(protocolNodeOf(kind)).toBe(type);
   }
   for (const type of Object.keys(PROTOCOL_NODES)) expect(types.has(type), type).toBe(true);
-  // Every protocol node type is some role's: a type no role is placed as would be dead in the bundle.
-  expect([...new Set(Object.values(ROLE_NODES))].sort()).toEqual(Object.keys(PROTOCOL_NODES).sort());
+  // Each `out` is an entry of the table, and one placed after its system: it writes no system input, so the one-writer
+  // rule, which protocolPipeline keeps for a role's first node, has nothing of it to keep.
+  const outs: string[] = [];
+  for (const [type, spec] of Object.entries(PROTOCOL_NODES) as [ProtocolNodeType, ProtocolNodeSpec][]) {
+    if (spec.out === undefined) continue;
+    expect(Object.prototype.hasOwnProperty.call(PROTOCOL_NODES, spec.out), `${type} -> ${spec.out}`).toBe(true);
+    expect(outNodeOf(type)).toBe(spec.out);
+    expect((PROTOCOL_NODES as Record<string, ProtocolNodeSpec>)[spec.out].writes, spec.out).toEqual([]);
+    outs.push(spec.out);
+  }
+  expect(outs).toEqual(["rp.lsdj.serial-out"]);
+  // Every protocol node type is some role's, before its system or after it: a type no role is placed as would be dead
+  // in the bundle.
+  expect([...new Set([...Object.values(ROLE_NODES), ...outs])].sort()).toEqual(Object.keys(PROTOCOL_NODES).sort());
 });
diff --git a/packages/retroplug/test/systems/store-graph.test.ts b/packages/retroplug/test/systems/store-graph.test.ts
index 97cff511..ed4ffa81 100644
--- a/packages/retroplug/test/systems/store-graph.test.ts
+++ b/packages/retroplug/test/systems/store-graph.test.ts
@@ -81,6 +81,11 @@ function refuseNext(be: MockBackend, n: number): void {
 // `v` as a document carries it: canonical JSON, its keys sorted.
 const canon = (v: unknown): unknown => JSON.parse(canonicalJson(v));
 
+// The nodes a role of `kind` on Game Boy `id` is placed as, in document order: lsdj-sync's two (its rp.lsdj.sync, and
+// its serial-out decoder after the systems), any other's one.
+const roleNodes = (id: number, kind: string): string[] =>
+  kind === "lsdj-sync" ? [`sys-${id}/lsdj-sync`, `sys-${id}/lsdj-sync/out`] : [`sys-${id}/${kind}`];
+
 // Whether the last document sent wires `from` into `to` (not over a feedback wire).
 const wired = (be: MockBackend, from: string, to: string): boolean =>
   be.lastDocument()!.wires.some((w) => w.from === from && w.to === to && !w.feedback);
@@ -371,12 +376,11 @@ test("each document places the routing and a protocol node for each role that ha
   const roles = sys.view().map((v) => v.roles.map((r) => r.kind));
   expect(roles[0].includes("sameboy")).toBe(true);
   expect(roles[0].includes("lsdj-assets")).toBe(true);
-  expect(be.protocolNodes(a)).toEqual([`sys-${a}/lsdj-sync`]);
+  expect(be.protocolNodes(a)).toEqual(roleNodes(a, "lsdj-sync"));
   expect(be.protocolNodes(c)).toEqual([`sys-${c}/nes-n8-midi`]);
-  // The Game Boy's serial out comes back into its lsdj-sync node, a block late.
-  expect(be.lastDocument()!.wires.filter((w) => w.feedback)).toEqual(
-    canon([{ from: `sys-${a}.serialOut`, to: `sys-${a}/lsdj-sync.serialOut`, feedback: true }]),
-  );
+  // The Game Boy's serial out goes forward into its lsdj-sync role's decoder, in the block the core captured it.
+  expect(be.lastDocument()!.wires.filter((w) => w.feedback)).toEqual([]);
+  expect(wired(be, `sys-${a}.serialOut`, `sys-${a}/lsdj-sync/out.serialOut`)).toBe(true);
 });
 
 test("an edit to a role with a protocol node publishes its settings, without a re-arm; the same edit again sends nothing", () => {
@@ -481,7 +485,7 @@ test("a systems store without a registry places the same protocol nodes, and an
     romPath: "/roms/a.gb",
     roles: [{ kind: "sameboy", config: {} }, { kind: "lsdj-sync", config: { mode: LsdjSyncMode.MidiSync } }],
   })!;
-  expect(be.protocolNodes(id)).toEqual([`sys-${id}/lsdj-sync`]);
+  expect(be.protocolNodes(id)).toEqual(roleNodes(id, "lsdj-sync"));
   expect(be.graphNode(`sys-${id}/lsdj-sync`)?.settings).toEqual({ mode: LsdjSyncMode.MidiSync });
   // A store on its own routes no MIDI and runs no controller.
   expect(be.graphNode("routing")).toBe(undefined);
@@ -642,7 +646,7 @@ test("of two roles whose nodes write serialIn, a loaded system places the one it
     // The role the ROM calls for goes ahead of the stale one, which is the order a save then writes.
     const kinds = sys.view().find((v) => v.id === id)!.roles.map((r) => r.kind);
     expect(kinds.slice(0, 3)).toEqual(["sameboy", placed, left]);
-    expect(be.protocolNodes(id)).toEqual([`sys-${id}/${placed}`]);
+    expect(be.protocolNodes(id)).toEqual(roleNodes(id, placed));
     const serialIn = be.lastDocument()!.wires.filter((w) => w.to === `sys-${id}.serialIn`);
     expect(serialIn.map((w) => w.from)).toEqual([`sys-${id}/${placed}.serialIn`]);
     expect(refusals(be.lastDocument()!)).toEqual([]);
@@ -650,7 +654,7 @@ test("of two roles whose nodes write serialIn, a loaded system places the one it
     // An edit to the role left out leaves it out, and says nothing more.
     const again = warned(() => sys.setRoleConfig(id, left, {}));
     expect(again.result).toBe(true);
-    expect(be.protocolNodes(id)).toEqual([`sys-${id}/${placed}`]);
+    expect(be.protocolNodes(id)).toEqual(roleNodes(id, placed));
     expect(again.warnings).toEqual([]);
     sys.removeSystem(id);
   }
@@ -685,13 +689,13 @@ test("a collision's outcome survives a save and a load: the ROM's role first and
   };
   const first = settle("/proj/collide.rplg");
   expect(first.kinds.slice(0, 3)).toEqual(["sameboy", "lsdj-sync", "mgb"]); // moved ahead of the stale role
-  expect(first.placed).toEqual([`sys-${first.id}/lsdj-sync`]);
+  expect(first.placed).toEqual(roleNodes(first.id, "lsdj-sync"));
   expect(first.warnings.length).toBe(1);
   expect(be.graphNode(`sys-${first.id}/lsdj-sync`)?.settings).toEqual(canon({ mode: LsdjSyncMode.MidiMap, tempoDivisor: 1, autoStart: false }));
   expect(p.save("/proj/saved.rplg")).toBe(true);
   const again = settle("/proj/saved.rplg");
   expect(again.kinds).toEqual(first.kinds);
-  expect(again.placed).toEqual([`sys-${again.id}/lsdj-sync`]);
+  expect(again.placed).toEqual(roleNodes(again.id, "lsdj-sync"));
   expect(again.warnings.length).toBe(1); // the stale role is still kept, so it is still noted
   expect(be.graphNode(`sys-${again.id}/lsdj-sync`)?.settings).toEqual(canon({ mode: LsdjSyncMode.MidiMap, tempoDivisor: 1, autoStart: false }));
 });
@@ -709,7 +713,7 @@ test("a suggested role that collides with nothing keeps its place in the stored
   expect(result.kind).toBe("loaded");
   const [entry] = sys.systems();
   expect(sys.view().find((v) => v.id === entry.id)!.roles.map((r) => r.kind).slice(0, 3)).toEqual(["sameboy", "retired-x", "lsdj-sync"]);
-  expect(be.protocolNodes(entry.id)).toEqual([`sys-${entry.id}/lsdj-sync`]);
+  expect(be.protocolNodes(entry.id)).toEqual(roleNodes(entry.id, "lsdj-sync"));
   expect(warnings).toEqual([]);
 });
 
@@ -723,7 +727,7 @@ test("a kind stored twice: the first is placed and is the one an edit reaches, a
   const { result: id, warnings } = warned(() => sys.adopt({ romPath: "/roms/lsdj.gb", core: "sameboy", roles })!);
   expect(warnings.length).toBe(1);
   expect(warnings[0].includes("its second lsdj-sync role is not placed")).toBe(true);
-  expect(be.protocolNodes(id)).toEqual([`sys-${id}/lsdj-sync`]);
+  expect(be.protocolNodes(id)).toEqual(roleNodes(id, "lsdj-sync"));
   const settings = () => be.graphNode(`sys-${id}/lsdj-sync`)?.settings;
   expect(settings()).toEqual(canon({ mode: LsdjSyncMode.MidiSync, tempoDivisor: 2, autoStart: false }));
   // An edit reaches the first role, the placed one; the second keeps what it was stored with.
````

Re-record the role goldens, which `buildGraph` writes, and check them equal to the prototype's. In the prototype's
diff 24 of the 37 cases moved, all and only those with an LSDj on a Game Boy: each gained its decoder and the forward
wire and lost its feedback wire, `rp.host.midi-out`'s settings and inputs unchanged, and no document kept a feedback
wire:

Run: `RP_GOLDEN_UPDATE=1 pnpm test graph/build-graph-role-goldens && git diff --stat 0cdf1abb -- packages/retroplug/test/graph/golden/build-graph-roles.json`
Expected: the golden test passes and the diff against the prototype prints nothing.

Run: `pnpm test graph; pnpm test systems`
Expected: every `graph/` file passes (`32 passed, 0 skipped, 0 failed` for `graph/build-graph`, `2 passed, 0 skipped, 0 failed` for `graph/protocol-nodes`), and `systems/` likewise (`41 passed, 0 skipped, 0 failed` for `systems/store-graph`).

- [ ] **Step 6: The engine-graph tests**

`ProtocolNodes.test.cpp`'s rig stands a Game Boy's `serialOut` in with a feed on a plain wire, this block's bytes;
three recorded digests move (the LSDj modes', the controller's, the controller-order case's, each count unchanged) and
its header says they differ from what the kernel sent by design; the controller-order case checks the merge ((frame,
system) order, the launch at frame 0). `BuildGraphGoldens.test.cpp`'s floors follow the decoder. Task 2's `[profile]`
case moves onto `rp.lsdj.serial-out`, and the label case pins 23. The hidden `[bench]` places the decoder after the
systems (five script nodes) and counts MI.OUT through the last block:

````diff
diff --git a/packages/native/test/engine-graph/BuildGraphGoldens.test.cpp b/packages/native/test/engine-graph/BuildGraphGoldens.test.cpp
index 35d1e8a7..4217fbcd 100644
--- a/packages/native/test/engine-graph/BuildGraphGoldens.test.cpp
+++ b/packages/native/test/engine-graph/BuildGraphGoldens.test.cpp
@@ -6,8 +6,8 @@
 // among them, from the script bundle every Engine embeds), so a port name, a settings key or a node limit the planner
 // refuses fails here for a shape no other suite runs: chained adds past rp.core.add's 16 inputs, a NES's ten channels,
 // the Game Boy stems, link cables under every routing, the routing's outputs for thirty-three systems, and each
-// protocol node type wired into the system node it feeds, MI.OUT fed back to an LSDj, the N8 cart mirror and a
-// controller each way.
+// protocol node type wired into the system node it feeds, an LSDj's serial-out decoder after its Game Boy, the N8 cart
+// mirror and a controller each way.
 //
 // The cores are real: a system node takes its core by its concrete class, so a stand-in reporting only its
 // kind is refused when the node activates. Each is built as the case's model says (a Game Boy's link group, a
@@ -19,7 +19,9 @@
 // and a control surface attached and pressed, so its protocol nodes send into the system inputs they feed and its
 // controller launches into its cart or out of the host. What reaches a system input the engine does not hand back, so
 // only the host's side has a floor: a controller sends its surface something, and one driving a real Game Boy sends
-// the host its launch under the system it resolved. The thirty-three systems under perSystem give
+// the host its launch under the system it resolved. No core here captures its serial out (that is armed only for an
+// LSDj in a MIDI-out mode, by the control plane), so the LSDj decoders send the host nothing, and host MIDI under the
+// controller's system is the controller's. The thirty-three systems under perSystem give
 // rp.host.audio-out 33 buses, past the 32 it takes: those are refused instead.
 //
 // Run via `pnpm test:plugin engine-graph`.
diff --git a/packages/native/test/engine-graph/ProtocolNodes.test.cpp b/packages/native/test/engine-graph/ProtocolNodes.test.cpp
index e1b948f5..e5144805 100644
--- a/packages/native/test/engine-graph/ProtocolNodes.test.cpp
+++ b/packages/native/test/engine-graph/ProtocolNodes.test.cpp
@@ -9,8 +9,8 @@
 // (Structure, below). So the graph these cases hold to their values is the one buildGraph builds for them: a change to
 // its wiring for these structures fails that TS test until it is re-recorded, and then fails a case here if it moves
 // what reaches a system input or the host. It does not reach what the stand-ins replace: the system nodes, the audio
-// and the cart mirror (BuildGraphGoldens.test.cpp plans these same documents whole on real cores), nor the feedback
-// wire a Game Boy's MI.OUT comes back on, whose feed here is a plain wire carrying what the core captured last block.
+// and the cart mirror (BuildGraphGoldens.test.cpp plans these same documents whole on real cores), nor what a Game
+// Boy's core captures on its link port, which a case writes into the feed standing in for the Game Boy's serialOut.
 // Nor any model but these, or what the stores hand buildGraph. The plan order is buildGraph's: the feeds lead the
 // document, so the script nodes run in their document order, as production plans them, and a case holds every
 // structure to that. The throw case relies on it: only a node planned after the one that throws can show that a throw
@@ -21,24 +21,31 @@
 // bytes in order; the host MIDI out with its system tags in emission order; the control-surface out). A digest hashes
 // what reaches each system input and the host outputs, never which node sent it, so a split of the kernel's work over
 // nodes is held to what the kernel sent. That is B3b's proof, bit for bit, that splitting the kernel changed nothing a
-// core or the host can see in those cases, and in no others. The design's section 9 lists where the per-role graph
-// behaves otherwise than the kernel did: among them a node's state keyed by its role's kind, so a role removed and
-// added again on a surviving system starts afresh; the controller's default target following a change of first system;
-// its session rebuilt on a change to its parsed config rather than its raw one; its launches going only into an
-// lsdj-sync node; a config read with its defaults filled. No case here sends a message through any of those; the throw
-// is the one difference a case reaches, below.
+// core or the host can see in those cases, and in no others. But for the host MIDI out of MI.OUT and master sync, which
+// differs from what the kernel sent by design (docs/lsdj-serial-out-design.md): rp.lsdj.serial-out, placed after the
+// Game Boy, sends each message at its byte's frame in the block the core captured it, where the kernel sent it at
+// frame 0 of the block after, and master sync's stop at the first frame its silence passes 200 ms. So the three cases
+// whose host MIDI out carries it (the LSDj modes, and the controller's two, beside an LSDj in MI.OUT) hold the values
+// that decoder was recorded sending, every system input in them still as the kernel sent it, and every other case still
+// holds the kernel's values. The design's section 9 lists where the per-role graph behaves otherwise than the kernel
+// did: among them a node's state keyed by its role's kind, so a role removed and added again on a surviving system
+// starts afresh; the controller's default target following a change of first system; its session rebuilt on a change to
+// its parsed config rather than its raw one; its launches going only into an lsdj-sync node; a config read with its
+// defaults filled. No case here sends a message through any of those; the throw is the one difference a case reaches,
+// below.
 //
 // The values have a history. The kernel node (B3a) was run beside the native kernel stage it replaced and found equal
 // to it block by block, from c323c7a8 to e3de2e46, the last commit before the stage's deletion; the B3a prototype
 // recorded the values from the same cases, and they held as recorded. One was re-recorded since: the LSDj modes case,
-// when master sync's stop became a silence rather than a flood (docs/lsdj-master-sync-fix-design.md). One case came
-// after the stage had gone, recorded from the kernel node alone: the controller's launches behind routed MIDI and its
-// host MIDI out ahead of the systems', two orders no earlier case sends a message through, which the stage-era proof
-// therefore never reached. Then, until B3b deleted the kernel node (from the commit that held its recorded digests on
-// the per-role graph too, to the flip that had buildGraph place that graph), every case ran on both, the kernel node's
-// graph and this one, each held to the same values. So a changed value is a change to what reaches a core or the host:
-// a deliberate one is re-recorded from the values a failing case prints. The other cases, which came after the kernel
-// or record nothing, check what they hold directly: a block that fills rp.host.midi-in, a controller's session kept
+// when master sync's stop became a silence rather than a flood (docs/lsdj-master-sync-fix-design.md). Then the three
+// above, the LSDj modes case among them, when the decoder moved after the Game Boy. One case came after the stage had
+// gone, recorded from the kernel node alone: the controller's launches behind routed MIDI and its host MIDI out ahead
+// of the systems', two orders no earlier case sends a message through, which the stage-era proof therefore never
+// reached. Then, until B3b deleted the kernel node (from the commit that held its recorded digests on the per-role
+// graph too, to the flip that had buildGraph place that graph), every case ran on both, the kernel node's graph and
+// this one, each held to the same values. So a changed value is a change to what reaches a core or the host: a
+// deliberate one is re-recorded from the values a failing case prints. The other cases, which came after the kernel or
+// record nothing, check what they hold directly: a block that fills rp.host.midi-in, a controller's session kept
 // through a system added behind it, every structure's script nodes running in buildGraph's order, and the restructuring
 // under an audio thread.
 //
@@ -184,14 +191,13 @@ std::pair<std::string, std::string> splitEndpoint(const std::string& endpoint) {
 // A structure the rig plans: one of buildGraph's role goldens, its document as buildGraph built it but for what the
 // rig stands in for. Each system node is replaced where it stood by a probe for its serialIn and midi inputs (in-<id>)
 // and one for its bytes and buttons (io-<id>). A Game Boy's serialOut is stood in for by a feed (mi-out-<id>), which
-// the rig fills with what the core captured last block: so the wire from it into an LSDj's node is a plain one, where
-// buildGraph's from the Game Boy is a feedback wire, the feed's bytes being a block late already. The feeds lead the
-// document, which keeps buildGraph's plan order: a feedback wire orders nothing, and a plain one from a feed where its
-// system stood would hold the LSDj node back until the feed, behind every script node placed after it (the planner
-// takes the first ready node in document order), while a feed at the head, with no inputs, is planned before them all.
-// The audio nodes and the N8 cart mirror are left out (no core runs here; HostIo.test.cpp holds the mirror), with every
-// wire to or from them or a system node that no stand-in takes. Every other node and wire, the script nodes' with their
-// settings and the host I/O's, is buildGraph's as recorded.
+// the rig fills with what the core captures in the block, so the wire from it into the LSDj's decoder is the plain one
+// buildGraph wires from the Game Boy, read in the same block. The feeds lead the document: a feed has no inputs, so at
+// the head it is planned before every node, and each node reading it runs where buildGraph's document places it, the
+// planner taking the first ready node in document order. The audio nodes and the N8 cart mirror are left out (no core
+// runs here; HostIo.test.cpp holds the mirror), with every wire to or from them or a system node that no stand-in
+// takes. Every other node and wire, the script nodes' with their settings and the host I/O's, is buildGraph's as
+// recorded.
 struct PlacedNode {
     std::string id;
     std::string type;
@@ -249,9 +255,10 @@ Structure standIns(const GoldenCase& c, const Document& doc) {
         const auto fromSystem = systemOf.find(fromNode);
         const auto toSystem = systemOf.find(toNode);
         if (fromSystem != systemOf.end()) {
-            // A Game Boy's MI.OUT into its LSDj's node comes from its feed. Past the audio, left out above, the one
-            // other wire a system sends is a link cable into another system, which no stand-in takes: any other would
-            // be a system output buildGraph wires to a node the rig plans, which must fail here rather than vanish.
+            // A Game Boy's serial out into its LSDj's decoder comes from its feed. Past the audio, left out above, the
+            // one other wire a system sends is a link cable into another system, which no stand-in takes: any other
+            // would be a system output buildGraph wires to a node the rig plans, which must fail here rather than
+            // vanish.
             if (fromPort == "serialOut" && toSystem == systemOf.end()) {
                 wires.push_back(wireJson("mi-out-" + std::to_string(fromSystem->second) + ".bytes", w.to));
                 continue;
@@ -294,7 +301,7 @@ Structure structure(const std::string& name) {
 
 // ---- one block's input -------------------------------------------------------------------------------------------
 
-// What one Game Boy captured last block, as its serialOut stamps it: each byte at the frame its core completed it, here
+// What one Game Boy captures in a block, as its serialOut stamps it: each byte at the frame its core completed it, here
 // the first at `first` and each next `stride` frames on (MI.OUT's port clocks a byte every eight frames; a playing LSDj
 // master clocks one every few hundred).
 struct Captured {
@@ -312,7 +319,7 @@ struct BlockIn {
     std::vector<std::pair<std::uint32_t, Bytes>>      midi;        // as a host stages it, in any frame order
     std::vector<Bytes>                                controller;  // control-surface traffic, at frame 0
     bool                                              connected = false;
-    std::vector<Captured>                             serialOut;   // what each Game Boy captured last block
+    std::vector<Captured>                             serialOut;   // what each Game Boy captures in the block
 };
 
 Bytes on(int channel, int key, int velocity = 100) {
@@ -723,9 +730,10 @@ TEST_CASE("the per-role graph drives each LSDj sync mode, MI.OUT and master sync
     rig.block({.ppq = 0.0, .playing = true, .midi = {{200, on(0, 31)}, {200, on(0, 7)}}});
     BlockIn playing{.frames = 1024, .ppq = 1024.0 / kBeat, .playing = true};
     rig.run(playing, 30);
-    // MI.OUT: framed protocol bytes, a frame split over two captured bytes; master sync: a run (a playing LSDj's bytes
-    // a few hundred frames apart), a stopped LSDj's silence, whose 200 ms (8820 frames) pass in the ninth quiet block
-    // and send its stop, then a run again.
+    // MI.OUT: framed protocol bytes, a frame split over two captured bytes, each message at its completing byte's
+    // frame; master sync: a run (a playing LSDj's bytes a few hundred frames apart, a clock at each), a stopped LSDj's
+    // silence, whose 200 ms (8820 frames) pass at frame 212 of the ninth block after its last byte, which sends its
+    // stop there, then a run again.
     playing.ppq = 31 * 1024.0 / kBeat;
     for (auto captured : {std::vector<Captured>{{7, {framed(0x7d), framed(0x7f), framed(0x70), framed(0x69), 0x7f}},
                                                 {8, {5, 5, 5}, 300}},
@@ -746,7 +754,7 @@ TEST_CASE("the per-role graph drives each LSDj sync mode, MI.OUT and master sync
     }
     rig.block({.ppq = fast.ppq, .tempo = 174.0, .playing = false, .midi = {{0, on(0, 25)}, {0, on(0, 26)}}});
     rig.run({.frames = 2048, .ppq = fast.ppq}, 2);
-    rig.matches({269, 0, 0, 4, 16, 0, 0xef0d11f87eff9493ULL});  // the buttons: autoStart's START press and release
+    rig.matches({269, 0, 0, 4, 16, 0, 0x50c11f5dc636b650ULL});  // the buttons: autoStart's START press and release
     rig.finish();
 }
 
@@ -805,7 +813,7 @@ TEST_CASE("the per-role graph runs a controller session: its surface, its launch
         play.controller.clear();
         play.ppq += 1024.0 / kBeat;
     }
-    rig.matches({161, 0, 0, 0, 80, 35, 0x097e9ae8508f0a25ULL});
+    rig.matches({161, 0, 0, 0, 80, 35, 0xcf9b8961b949c269ULL});
     rig.finish();
 }
 
@@ -838,15 +846,22 @@ TEST_CASE("the per-role graph puts a controller's launches after the routed MIDI
                 cartSerialIn.push_back(serialBytes(rig.node<Probe>("in-1").bytes));
                 continue;
             }
-            // Host MIDI out: the controller's launch first, under the system its default target resolved to (Game Boy
-            // 1), then Game Boy 2's decoded MI.OUT, then Game Boy 3's clocks.
+            // Host MIDI out, merged by frame with ties in input order: the controller's launch first, at frame 0, under
+            // the system its default target resolved to (Game Boy 1), then Game Boy 2's decoded MI.OUT and Game Boy 3's
+            // clocks at their bytes' frames, Game Boy 2's first on a tie. The system tags follow the input order here,
+            // so the merge is (frame, system) ascending.
             std::vector<std::uint32_t> systems;
-            for (const ProtocolRig::Out& m : rig.midiOut) systems.push_back(m.system);
+            std::vector<std::pair<std::uint32_t, std::uint32_t>> merged;
+            for (const ProtocolRig::Out& m : rig.midiOut) {
+                systems.push_back(m.system);
+                merged.push_back({m.frame, m.system});
+            }
             INFO("press " << i);
             REQUIRE_FALSE(rig.midiOut.empty());
             hostLaunches.push_back(rig.midiOut.front().data);
             CHECK(systems.front() == 1);
-            CHECK(std::is_sorted(systems.begin(), systems.end()));
+            CHECK(rig.midiOut.front().frame == 0);
+            CHECK(std::is_sorted(merged.begin(), merged.end()));
             CHECK(std::count(systems.begin(), systems.end(), 1u) == 1);
             CHECK(std::count(systems.begin(), systems.end(), 2u) > 0);
             CHECK(std::count(systems.begin(), systems.end(), 3u) > 0);
@@ -868,7 +883,7 @@ TEST_CASE("the per-role graph puts a controller's launches after the routed MIDI
         CHECK(cartSerialIn[i][0] == Message{300, {static_cast<std::uint8_t>(20 + i)}, 0});
         CHECK(cartSerialIn[i][1] == Message{300, {hostLaunches[i][1]}, 0});
     }
-    rig.matches({32, 0, 0, 0, 68, 84, 0xeb884025953336a5ULL});
+    rig.matches({32, 0, 0, 0, 68, 84, 0x5474d124ec373f2bULL});
     rig.finish();
 }
 
@@ -1007,9 +1022,9 @@ TEST_CASE("a throw in a protocol node keeps what was sent before it and runs the
 
 TEST_CASE("each structure's script nodes run in the order buildGraph's document places them",
           "[engine-graph][protocol-nodes]") {
-    // Production plans the script nodes in their document order: nothing a script node reads comes from a node after it
-    // but over a feedback wire, which orders nothing (test/graph/build-graph-role-goldens.test.ts holds every golden
-    // document to that), and the planner breaks ties by document order. The rig must run
+    // Production plans the script nodes in their document order: a script node reads only nodes placed before it, or a
+    // feedback wire, which orders nothing (test/graph/build-graph-role-goldens.test.ts holds every golden document to
+    // that), and the planner breaks ties by document order. The rig must run
     // them in that order too, or a case that holds what one node does to the nodes after it (the throw) holds nothing.
     // So each structure is planned and run a block with every script node's process recorded as it is called (the wrap
     // injected below, through __probe), and the record is held to buildGraph's script nodes in document order, each by
diff --git a/packages/native/test/engine-graph/ScriptBench.test.cpp b/packages/native/test/engine-graph/ScriptBench.test.cpp
index 281229d5..c47a67f4 100644
--- a/packages/native/test/engine-graph/ScriptBench.test.cpp
+++ b/packages/native/test/engine-graph/ScriptBench.test.cpp
@@ -6,16 +6,17 @@
 // 64 trivial TS nodes (an events pass-through written with indexed loops) between a native source and sink, the source
 // sending nothing or one message a block. A node's cost is the chain's less the source wired straight to the sink, over
 // the node count. The second times the protocol nodes buildGraph places for one project, in a plan driven as
-// ProtocolNodes.test.cpp drives one: mGB, an LSDj and a NES on nes-n8-midi, so four script nodes (the routing, sending
-// every host message to all three, and a protocol node a system: rp.mgb.midi, rp.lsdj.sync and rp.n8.midi-to-fifo), the
-// transport playing and a steady MIDI stream. The LSDj's node takes its Game Boy's serial out a block after the core
-// clocked it out (a feedback wire, as buildGraph wires it), at the most the port carries, a byte every 8 frames, in two
-// structures: in MIDI sync, where it is the idle line, which the node reads and copies every block and decodes in no
-// mode but MI.OUT and master sync (the worst case B3a timed: capture is armed only for an LSDj in a MIDI-out mode); and
-// in MI.OUT, where every byte is a framed command, so the decoder runs and the node sends host MIDI. mGB's serial out
-// is wired to nothing (buildGraph wires a Game Boy's only into rp.lsdj.sync), so its core captures none and nothing
-// stands in for it. No core runs: the plan's system stand-ins count what reaches their inputs. The plan's native nodes
-// alone (the same document with no script nodes) are timed too, so the script nodes' own share shows.
+// ProtocolNodes.test.cpp drives one: mGB, an LSDj and a NES on nes-n8-midi, so five script nodes (the routing, sending
+// every host message to all three, a protocol node a system: rp.mgb.midi, rp.lsdj.sync and rp.n8.midi-to-fifo, and the
+// LSDj's decoder, rp.lsdj.serial-out, after the systems), the transport playing and a steady MIDI stream. The decoder
+// takes its Game Boy's serial out in the block the core clocked it out (a forward wire, as buildGraph wires it), at the
+// most the port carries, a byte every 8 frames, in two structures: in MIDI sync, where it is the idle line, which the
+// decoder does not read (it reads its input only in MI.OUT and master sync, the modes capture is armed for, so in
+// production this line is not even captured); and in MI.OUT, where every byte is a framed command, so the decoder runs
+// and sends host MIDI. mGB's serial out is wired to nothing (buildGraph wires a Game Boy's only into its LSDj's
+// decoder), so its core captures none and nothing stands in for it. No core runs: the plan's system stand-ins count
+// what reaches their inputs. The plan's native nodes alone (the same document with no script nodes) are timed too, so
+// the script nodes' own share shows.
 //
 // Until B3b the second table timed the one DSP kernel node that did the four nodes' work, in the MIDI-sync structure,
 // against the native kernel stage it replaced while that still existed (9c3c10f9: the node took 1.36 to 1.62 times as
@@ -23,8 +24,9 @@
 // over three runs, docs/modular-graph-phase-b3a-design.md section 7.4), were 2.68 to 2.74 us, 7.69 to 7.92 us and 39.5
 // to 40.7 us at 1, 64 and 512 frames, with 25, 81 and 431 JS allocations a block. They were measured on another tree,
 // on another day, so they compare with this table's MIDI-sync rows by their size, not to the percent. B3a's bench also
-// fed both Game Boys' idle serial-out bytes to the kernel node (128 a block at 512 frames, against this table's 64:
-// only the LSDj's now), which its design section 7.4 says were nearly all the node read in the larger blocks.
+// fed both Game Boys' idle serial-out bytes to the kernel node (128 a block at 512 frames, against this table's 64,
+// only the LSDj's, which its decoder does not read in MIDI sync), which its design section 7.4 says were nearly all the
+// node read in the larger blocks.
 //
 // Each figure is a median of kRuns runs; a run times the script nodes' plan, then that plan with no script nodes, over
 // the same blocks. Allocations are per block over every run: JS from the script host's audio runtime, native on the
@@ -128,8 +130,8 @@ rp.register([rp.defineNode({
 std::string node(const std::string& id, const std::string& type, const std::string& settings = "") {
     return R"({"id":")" + id + R"(","type":")" + type + "\"" + (settings.empty() ? "" : R"(,"settings":)" + settings) + "}";
 }
-std::string wire(const std::string& from, const std::string& to, bool feedback = false) {
-    return R"({"from":")" + from + R"(","to":")" + to + "\"" + (feedback ? R"(,"feedback":true)" : "") + "}";
+std::string wire(const std::string& from, const std::string& to) {
+    return R"({"from":")" + from + R"(","to":")" + to + "\"}";
 }
 std::string graphJson(const std::vector<std::string>& nodes, const std::vector<std::string>& wires) {
     std::string doc = R"({"nodes":[)";
@@ -193,8 +195,9 @@ ChainRow timeChain(bool message, int n, std::uint32_t frames) {
 int protocolBlocks(std::uint32_t frames) { return frames == 1 ? 32768 : frames == 64 ? 8192 : 2048; }
 
 // The three systems, each with the one role buildGraph places a protocol node for (graph/protocolNodes.ts): its node
-// sys-<id>/<kind>, its type, the input its routed MIDI arrives on, and the system inputs it writes. The LSDj's node
-// (lsdj-sync) takes its Game Boy's serial out too, and sends host MIDI.
+// sys-<id>/<kind>, its type, the input its routed MIDI arrives on, and the system inputs it writes. The LSDj's role
+// places a second node after the systems, its decoder (kLsdjDecoder), which takes its Game Boy's serial out and sends
+// host MIDI.
 struct BenchSystem {
     std::uint32_t            id;
     const char*              kind;
@@ -213,6 +216,8 @@ constexpr std::size_t kMgb = 0;
 constexpr std::size_t kLsdj = 1;
 constexpr std::size_t kNes = 2;
 bool isLsdj(const BenchSystem& s) { return s.id == kSystems[kLsdj].id; }
+// The LSDj's decoder, as buildGraph places it: sys-<id>/lsdj-sync/out, with its rp.lsdj.sync's settings.
+constexpr const char* kLsdjDecoder = "rp.lsdj.serial-out";
 
 // The structures the table times, by the LSDj's mode (its role's config as the store parses it, its node's settings)
 // and what its Game Boy's serial port clocks out: the idle line, or a framed MI.OUT stream (kMiOut). The other roles'
@@ -362,9 +367,9 @@ struct ProtocolBench {
     ~ProtocolBench() { host.reset(); }
 
     // As buildGraph wires it: the host I/O nodes, the routing (every host message to every system), each system's
-    // protocol node fed its routed MIDI and wired into its system's inputs, the LSDj's taking its Game Boy's serialOut
-    // by a feedback wire and its host MIDI out going to rp.host.midi-out's one input (under its system), and the systems,
-    // the LSDj's Game Boy with its serial out.
+    // protocol node fed its routed MIDI and wired into its system's inputs, the systems, the LSDj's Game Boy with its
+    // serial out, then the LSDj's decoder, taking that serial out by a forward wire, its host MIDI out going to
+    // rp.host.midi-out's one input (under its system), and the host outputs.
     static std::string document(const Structure& structure, bool scriptNodes) {
         std::vector<std::string> nodes{node("midi-in", "rp.host.midi-in"), node("controller-in", "rp.host.controller-in")};
         std::vector<std::string> wires;
@@ -376,18 +381,22 @@ struct ProtocolBench {
                 nodes.push_back(node(id, s.type, isLsdj(s) ? structure.lsdjSettings : "{}"));
                 wires.push_back(wire("routing." + sys, id + "." + s.midiIn));
                 for (const char* in : s.writes) wires.push_back(wire(id + "." + in, sys + "." + in));
-                if (!isLsdj(s)) continue;
-                wires.push_back(wire(sys + "-serial.serialOut", id + ".serialOut", true));
-                wires.push_back(wire(id + ".midiOut", "midi-out.in1"));
             }
         }
-        nodes.push_back(node("midi-out", "rp.host.midi-out", scriptNodes ? R"({"systems":[2]})" : R"({"systems":[]})"));
-        nodes.push_back(node("controller-out", "rp.host.controller-out"));
         for (const BenchSystem& s : kSystems) {
             const std::string sys = "sys-" + std::to_string(s.id);
             nodes.push_back(node(sys, "rp.bench.system"));
             if (isLsdj(s)) nodes.push_back(node(sys + "-serial", structure.miOut ? "rp.bench.mi-out" : "rp.bench.serial-out"));
         }
+        if (scriptNodes) {
+            const BenchSystem& s = kSystems[kLsdj];
+            const std::string sys = "sys-" + std::to_string(s.id), id = sys + "/" + s.kind + "/out";
+            nodes.push_back(node(id, kLsdjDecoder, structure.lsdjSettings));
+            wires.push_back(wire(sys + "-serial.serialOut", id + ".serialOut"));
+            wires.push_back(wire(id + ".midiOut", "midi-out.in1"));
+        }
+        nodes.push_back(node("midi-out", "rp.host.midi-out", scriptNodes ? R"({"systems":[2]})" : R"({"systems":[]})"));
+        nodes.push_back(node("controller-out", "rp.host.controller-out"));
         return graphJson(nodes, wires);
     }
 
@@ -481,10 +490,10 @@ ProtocolRows timeProtocolNodes(const Structure& structure, std::uint32_t frames)
     CHECK(rows.delivered.bytes == 0);            // nothing here sends core bytes
     CHECK(rows.delivered.buttons == 0);          // or presses a button
     if (structure.miOut) {
-        // The LSDj sends its core nothing, and its node decodes every byte its port clocked out up to the block before
-        // the last (the feedback wire's), each 8 into 5 host MIDI messages.
+        // The LSDj sends its core nothing, and its decoder decodes every byte its port clocked out, the last block's
+        // included (it reads them in the block they were clocked out), each 8 into 5 host MIDI messages.
         CHECK(bench.nodeCounts(kLsdj).serialIn == 0);
-        CHECK(bench.midiOut == miOutMessages(clocked(0, static_cast<std::uint32_t>((k - 1) * frames))));
+        CHECK(bench.midiOut == miOutMessages(clocked(0, static_cast<std::uint32_t>(k * frames))));
     } else {
         CHECK(bench.nodeCounts(kLsdj).serialIn > 0);  // its clocks
         CHECK(bench.midiOut == 0);                     // and nothing decoded
@@ -493,7 +502,7 @@ ProtocolRows timeProtocolNodes(const Structure& structure, std::uint32_t frames)
     rows.midiOut = bench.midiOut;
     CHECK(bench.host->droppedMessages() == 0);
     CHECK(bench.scripts.throwCount() == 0);
-    CHECK(bench.scripts.liveInstances() == 1 + kSystemCount);  // the routing and a protocol node a system
+    CHECK(bench.scripts.liveInstances() == 2 + kSystemCount);  // the routing, a protocol node a system, the decoder
     rows.blocks = k;
     return rows;
 }
@@ -534,8 +543,9 @@ void protocolTable() {
                     "host message every %llu frames; the LSDj's serial out a byte every 8 frames, %s\n",
                     structure.name, kTempo, static_cast<unsigned long long>(kMidiEvery),
                     structure.miOut ? "every one a framed command, 5 host MIDI messages each 8 bytes" : "the idle line");
-        std::printf("nodes: their plan (the host I/O nodes, the routing and a protocol node a system, the LSDj's serialOut, a "
-                    "stand-in for each system's inputs); plan: that plan with no script nodes; no core runs\n");
+        std::printf("nodes: their plan (the host I/O nodes, the routing and a protocol node a system, the LSDj's serialOut "
+                    "and its decoder, a stand-in for each system's inputs); plan: that plan with no script nodes; no core "
+                    "runs\n");
         std::printf("%6s  %-20s %6s | %9s %9s %9s | %9s %9s %9s\n", "frames", "path", "blocks", "us/block", "%1-frame", "%own",
                     "JS allocs", "JS bytes", "native");
         for (const std::uint32_t frames : kBlockSizes) {
diff --git a/packages/native/test/engine-graph/ScriptNodes.test.cpp b/packages/native/test/engine-graph/ScriptNodes.test.cpp
index 99e3be73..75c26867 100644
--- a/packages/native/test/engine-graph/ScriptNodes.test.cpp
+++ b/packages/native/test/engine-graph/ScriptNodes.test.cpp
@@ -1923,10 +1923,11 @@ TEST_CASE("the embedded script bundle loads, and registers the production script
     REQUIRE(bundle.size() > 0);
     REQUIRE(host.load({&bundle, 1}, &error));
     CHECK(error.empty());
-    // In the bundle's order: the MIDI routing, each role's protocol node and the controller session, which buildGraph
-    // places (ProtocolNodes.test.cpp holds them to what the DSP kernel they replaced was recorded sending).
-    const std::vector<std::string> ids{"rp.midi.route", "rp.mgb.midi",  "rp.n8.midi-to-fifo",   "rp.risa.sync",
-                                       "rp.sms.sync",   "rp.lsdj.sync", "rp.controller.session"};
+    // In the bundle's order: the MIDI routing, each role's protocol node, the controller session and LSDj's serial-out
+    // decoder, which buildGraph places (ProtocolNodes.test.cpp holds them to what the DSP kernel they replaced was
+    // recorded sending, the decoder by design aside).
+    const std::vector<std::string> ids{"rp.midi.route", "rp.mgb.midi",  "rp.n8.midi-to-fifo",    "rp.risa.sync",
+                                       "rp.sms.sync",   "rp.lsdj.sync", "rp.controller.session", "rp.lsdj.serial-out"};
     REQUIRE(host.package()->node_type_count == ids.size());
     for (std::size_t i = 0; i < ids.size(); ++i) CHECK(std::string(host.package()->node_types[i]->id) == ids[i]);
     CHECK(host.package()->abi_version == RP_ABI_VERSION);
@@ -2203,23 +2204,25 @@ TEST_CASE("a type past the trace's name table is traced under the shared script-
     rig.scripts.traceReset(false);
 }
 
-TEST_CASE("the embedded bundle's types are labelled 16 to 22 in the order spec/08 and the B3b design quote",
+TEST_CASE("the embedded bundle's types are labelled 16 to 23 in the order spec/08 quotes, the serial-out decoder last",
           "[script][profile][trace]") {
+    // 16 to 22 the seven the B3b design quotes, and 23 the one registered after them (docs/lsdj-serial-out-design.md).
     ScriptHost host;
     const ScriptHost::Chunk bundle = ScriptHost::embeddedBundle();
     REQUIRE(host.load({&bundle, 1}));
     const auto names = host.traceNames();
-    const std::vector<std::string> ids{"rp.midi.route", "rp.mgb.midi",  "rp.n8.midi-to-fifo",   "rp.risa.sync",
-                                       "rp.sms.sync",   "rp.lsdj.sync", "rp.controller.session"};
+    const std::vector<std::string> ids{"rp.midi.route", "rp.mgb.midi",  "rp.n8.midi-to-fifo",    "rp.risa.sync",
+                                       "rp.sms.sync",   "rp.lsdj.sync", "rp.controller.session", "rp.lsdj.serial-out"};
     REQUIRE(names.size() >= rp::host::DSP_SPAN_TYPE_BASE + ids.size());
     for (std::size_t i = 0; i < ids.size(); ++i) CHECK(names[rp::host::DSP_SPAN_TYPE_BASE + i] == ids[i]);
+    CHECK(names[23] == "rp.lsdj.serial-out");
 }
 
 TEST_CASE("an Engine's apu-render spans its plan's whole run, each script node's process span inside it wherever it runs",
           "[script][profile][trace]") {
-    // The embedded bundle's rp.mgb.midi runs before its Game Boy, as buildGraph places it, and its rp.lsdj.sync after it,
-    // over a forward wire from the Game Boy's serialOut, as a node placed after a system runs: each block's apu-render
-    // holds both nodes' process spans (Chrome trace events nest), and no script node's span starts it.
+    // The embedded bundle's rp.mgb.midi runs before its Game Boy, and its rp.lsdj.serial-out after it, over a forward
+    // wire from the Game Boy's serialOut, as buildGraph places each: each block's apu-render holds both nodes' process
+    // spans (Chrome trace events nest), and no script node's span starts it.
     using rp::host::test::DocExtras;
     using rp::host::test::docNode;
     using rp::host::test::docWire;
@@ -2231,7 +2234,8 @@ TEST_CASE("an Engine's apu-render spans its plan's whole run, each script node's
     auto core = std::make_unique<SameBoySystem>(1, cfg, rp::host::test::readFile(RP_MGB_ROM_PATH));
     core->onActivate(kRate);
     REQUIRE(e.parkCore(std::move(core)));
-    const DocExtras extras{{docNode("midi-in", "rp.host.midi-in"), docNode("mgb", "rp.mgb.midi"), docNode("late", "rp.lsdj.sync")},
+    const DocExtras extras{{docNode("midi-in", "rp.host.midi-in"), docNode("mgb", "rp.mgb.midi"),
+                            docNode("late", "rp.lsdj.serial-out")},
                            {docWire("midi-in.midi", "mgb.midi"), docWire("mgb.serialIn", "sys-1.serialIn"),
                             docWire("sys-1.serialOut", "late.serialOut")}};
     const auto planned =
@@ -2262,7 +2266,7 @@ TEST_CASE("an Engine's apu-render spans its plan's whole run, each script node's
     };
     const auto apu = spansOf(rp::host::DSP_SPAN_APU);
     REQUIRE(apu.size() == kBlocks);
-    for (const char* type : {"rp.mgb.midi", "rp.lsdj.sync"}) {
+    for (const char* type : {"rp.mgb.midi", "rp.lsdj.serial-out"}) {
         INFO(type);
         const auto process = spansOf(labelOf(type));
         REQUIRE(process.size() == kBlocks);
````

Run: `pnpm test:plugin`
Expected: `# 10 plugin test binary(ies) passed (jobs=16)`, the engine-graph binary's `All tests passed (15598 assertions in 120 test cases)`.

Run: `cmake --build build --target retroplug-engine-graph-test -j$(nproc) && build/bin/retroplug-engine-graph-test "[bench]"`
Expected: `All tests passed (672 assertions in 1 test case)`, 0.00 JS allocations a block in every row.

Run: `tools/run-profile.sh unit`
Expected: `All tests passed (128 assertions in 6 test cases)`.

- [ ] **Step 7: The real-host tests**

`lsdj-mastersync.test.ts` checks, at every split and both rates, that a run's consecutive clocks lie within 3% of the
tempo's period in absolute frames (the prototype measured 846 to 874 frames at 44.1 kHz against a period of 861.33,
921 to 951 at 48 kHz against 937.5, identical in every split, LSDj's own repeating pattern of six gaps), and a run's
first gap within 30% (LSDj's first tick after START comes at 0.78 of a period); at block-start timing a 1024-frame
block is 19% off, so the old code fails it. Its extra 1-frame render, there because the bytes reached the decoder a
block late, goes. `lsdj-midiout.test.ts` checks MI.OUT's messages carry their frames (the prototype measured 58 to
993, none at 0). The profile bench's LSDj windows now run through the new node, and two parity files' comments follow:

````diff
diff --git a/packages/retroplug/test-native/dsp-bench.test.ts b/packages/retroplug/test-native/dsp-bench.test.ts
index 474ee8d5..33024d07 100644
--- a/packages/retroplug/test-native/dsp-bench.test.ts
+++ b/packages/retroplug/test-native/dsp-bench.test.ts
@@ -2,9 +2,9 @@
 // rp.mgb.midi a system) off the RT thread via the deterministic renderAudio pull path under a seeded mGB + heavy-MIDI
 // workload, and reports what the script host's audio runtime (the bare QuickJS runtime they run in) allocates PER
 // BLOCK: the refcount-churn signal (QuickJS frees acyclic garbage immediately, so "GC" is rare; malloc/free churn
-// per block is the real cost). Then a window each on a real LSDj, playing, whose rp.lsdj.sync decodes its serial out
-// to host MIDI, in master sync and in MI.OUT: those need the fixture tree's LSDj ROMs (RETROPLUG_RESOURCES_DIR), and
-// without them the case SKIPs there, after the mGB window has been held to its zero.
+// per block is the real cost). Then a window each on a real LSDj, playing, whose rp.lsdj.serial-out decodes its serial
+// out to host MIDI, in master sync and in MI.OUT: those need the fixture tree's LSDj ROMs (RETROPLUG_RESOURCES_DIR),
+// and without them the case SKIPs there, after the mGB window has been held to its zero.
 //
 // The allocation counters exist only in a RETROPLUG_PROFILE host (built by `pnpm profile` /
 // tools/run-profile.sh). Under the default host the counters report enabled:false and this
@@ -300,12 +300,12 @@ test("dsp-bench: the script nodes' per-block allocation under an mGB + MIDI work
   expectWindowLive();
 
   // --- an LSDj decoding its serial out to host MIDI, playing ------------------------------------------------------
-  // rp.lsdj.sync's two MIDI-out modes run their decoders only on what a playing LSDj clocks out, so each has a window
-  // of its own on a real LSDj, the mGB systems gone: master sync on a stock LSDj in SYNC=LSDj (a byte a 24-PPQN tick,
-  // which the core captures in `listen`, as lsdj-mastersync.test.ts runs it), and MI.OUT on an Arduinoboy build in
-  // SYNC=MI.OUT (its clock and the N commands' NoteOns, captured in `drive`, as lsdj-midiout.test.ts runs it). Each
-  // window is held to the mGB window's zero, the clocks its decoder sent say LSDj played through it, and the same proof
-  // as above says the window counted.
+  // The lsdj-sync role's two MIDI-out modes run their decoders, in rp.lsdj.serial-out after the Game Boy, only on what
+  // a playing LSDj clocks out, so each has a window of its own on a real LSDj, the mGB systems gone: master sync on a
+  // stock LSDj in SYNC=LSDj (a byte a 24-PPQN tick, which the core captures in `listen`, as lsdj-mastersync.test.ts
+  // runs it), and MI.OUT on an Arduinoboy build in SYNC=MI.OUT (its clock and the N commands' NoteOns, captured in
+  // `drive`, as lsdj-midiout.test.ts runs it). Each window is held to the mGB window's zero, the clocks its decoder
+  // sent say LSDj played through it, and the same proof as above says the window counted.
   for (const id of ids) graph.remove(id);
   const be = createRealBackend();
   const lsdjBlocks = Math.min(blocks, 400);
diff --git a/packages/retroplug/test-native/lsdj-mastersync.test.ts b/packages/retroplug/test-native/lsdj-mastersync.test.ts
index 3b66bfd6..c6e328ab 100644
--- a/packages/retroplug/test-native/lsdj-mastersync.test.ts
+++ b/packages/retroplug/test-native/lsdj-mastersync.test.ts
@@ -3,10 +3,12 @@
 // serial MASTER and streams one byte out its link port per MIDI-clock tick. Native captures those bytes
 // (setSerialOutCapture's `listen`: the internal-clock serialStart path, same serialOutLog_ MI.OUT uses, no
 // peer needed, and never `drive`, which would clock a stopped LSDj's wait for another master and turn it
-// slave) and feeds them to the lsdj-sync node, where the mode-8 decoder turns each byte into a 0xF8 clock (+ a
-// song-row NoteOn/0xFA at run start) and a 200 ms silence into 0xFC. So the host follows LSDj's tempo. The
-// decoder times the silence on the bytes' own frames, so what it decodes must not depend on how the host cuts
-// its blocks, at either rate: the second case holds it to that.
+// slave) and feeds them, in the block it captured them, to the lsdj-sync role's decoder after the Game Boy
+// (rp.lsdj.serial-out), where the mode-8 decoder turns each byte into a 0xF8 clock at its frame (+ a song-row
+// NoteOn/0xFA at run start) and a 200 ms silence into 0xFC. So the host follows LSDj's tempo. The decoder times
+// the silence on the bytes' own frames and sends every message at a frame of its own, so what it decodes must not
+// depend on how the host cuts its blocks, at either rate, and its clocks lie a tempo period apart whatever the
+// block size: the second case holds it to both.
 //
 // A playing stock LSDj master emits a TEMPO-LOCKED byte stream (~50/sec = ~130 BPM at 24 PPQN: tens a second,
 // NOT thousands), and a stopped one goes quiet. The thousands-a-second stream once taken for SYNC=LSDj's raw link
@@ -122,7 +124,20 @@ function story(sent: number[][]): (string | number)[] {
   return out;
 }
 
+/** The song's tempo (LSDj's default), a clock every 24th of a beat: about 861 frames at 44.1 kHz, 937.5 at 48. */
+const TEMPO = 128;
+/** How far a clock gap inside a run may lie from the tempo's period, as a share of it. Measured in every split at both
+ *  rates, they lie within 1.8% of it: LSDj's own tick timing, a pattern of six gaps that repeats, 846 to 874 frames at
+ *  44.1 kHz and 921 to 951 at 48, the same in every split. So 3% holds them with room and still refuses a clock moved
+ *  by its block: two clocks sent at their blocks' starts are 0 or a block apart, and a 1024-frame block is 19% past the
+ *  period at 44.1 kHz. */
+const TOLERANCE = 0.03;
+/** A run's first gap, from the clock of its first byte to the next: LSDj's first tick after START comes early, 0.78 of
+ *  a period in every split at both rates (673 frames at 44.1 kHz, 732 and 733 at 48), so it is held within 30%. */
+const FIRST_TOLERANCE = 0.3;
+
 test("Master Sync tells one story at 44.1 and 48 kHz in 1-frame, FL-style ragged and 1024-frame blocks, a restart included", () => {
+  const clockGaps: { name: string; gaps: number[]; firsts: number[] }[] = [];
   const be = createRealBackend();
   if (!be.fileExists(LSDJ)) skip(`lsdj-mastersync: LSDj ROM not found at ${LSDJ}`);
   const audio = createAudioDriver();
@@ -140,9 +155,18 @@ test("Master Sync tells one story at 44.1 and 48 kHz in 1-frame, FL-style ragged
       expect(graph.setMidiRouting("sendToAll")).toBeTruthy();
       expect(graph.setPipeline(id, lsdjSync(MASTER_SYNC))).toBeTruthy();
       expect(be.setSerialOutCapture(id, "listen")).toBeTruthy();
-      const sent: number[][] = [];
+      // Each message with its absolute frame: its block's start, counted from the system's first block, plus its frame.
+      const sent: { at: number; data: number[] }[] = [];
+      let next = 0; // the absolute frame the next render starts at
       const render = (ms: number) => {
-        for (const m of audio.renderBlocks(2, split(frames(ms)).map((n) => ({ frames: n }))).midiOut) sent.push(Array.from(m.data));
+        const blocks = split(frames(ms));
+        const starts: number[] = [];
+        for (const n of blocks) {
+          starts.push(next);
+          next += n;
+        }
+        for (const m of audio.renderBlocks(2, blocks.map((n) => ({ frames: n }))).midiOut)
+          sent.push({ at: starts[m.block] + m.frame, data: Array.from(m.data) });
       };
       const tap = () => {
         render(10);
@@ -157,12 +181,27 @@ test("Master Sync tells one story at 44.1 and 48 kHz in 1-frame, FL-style ragged
       render(1200);
       tap(); // play again
       render(1500);
-      // One frame more: a block's bytes reach the decoder with the block after it, so without it a split whose last
-      // block is long (a ragged one's can be 4096 frames) would leave that block's clocks undecoded.
-      for (const m of audio.renderBlocks(2, [{ frames: 1 }]).midiOut) sent.push(Array.from(m.data));
       graph.remove(id);
-      const s = story(sent);
-      console.log(`[lsdj-mastersync] ${rate} ${name}: ${JSON.stringify(s)}`);
+      const s = story(sent.map((m) => m.data));
+      // The gaps between consecutive clocks of a run, in absolute frames: each run's first apart (`firsts`).
+      const gaps: number[] = [];
+      const firsts: number[] = [];
+      let last = -1;
+      let clocksInRun = 0;
+      for (const m of sent) {
+        if (m.data[0] === 0xfa || m.data[0] === 0xfc) {
+          last = -1;
+          clocksInRun = 0;
+        }
+        if (m.data[0] !== 0xf8) continue;
+        if (last >= 0) (++clocksInRun === 1 ? firsts : gaps).push(m.at - last);
+        last = m.at;
+      }
+      console.log(
+        `[lsdj-mastersync] ${rate} ${name}: ${JSON.stringify(s)} clock gaps ${Math.min(...gaps)}..${Math.max(...gaps)}, ` +
+          `mean ${(gaps.reduce((a, b) => a + b, 0) / gaps.length).toFixed(2)}; each run's first ${JSON.stringify(firsts)}`,
+      );
+      clockGaps.push({ name, gaps, firsts });
       return s;
     });
     // Nothing from the boot; a run, its stop, then a fresh run: no clock while stopped, and the restart opens with its
@@ -176,6 +215,18 @@ test("Master Sync tells one story at 44.1 and 48 kHz in 1-frame, FL-style ragged
       for (const n of counts) expect(n > 60 && n < 110).toBeTruthy();
       expect(Math.max(...counts) - Math.min(...counts) <= 1).toBeTruthy();
     }
+    // Consecutive clocks of a run lie about a tempo period apart in absolute frames, in every split: each goes out at
+    // its byte's frame in the block LSDj clocked it, never bunched at a block's start. TOLERANCE and FIRST_TOLERANCE
+    // say how near.
+    const period = (rate * 60) / (TEMPO * 24);
+    for (const { name, gaps, firsts } of clockGaps.splice(0)) {
+      expect(gaps.length > 100, `${rate} ${name}: clock gaps measured`).toBeTruthy();
+      expect(firsts.length, `${rate} ${name}: a first gap for each run`).toBe(2);
+      const far = gaps.filter((g) => Math.abs(g - period) > TOLERANCE * period);
+      expect(far, `${rate} ${name}: clock gaps more than ${TOLERANCE * 100}% off ${period.toFixed(1)} frames`).toEqual([]);
+      const farFirst = firsts.filter((g) => Math.abs(g - period) > FIRST_TOLERANCE * period);
+      expect(farFirst, `${rate} ${name}: a run's first clock gap more than ${FIRST_TOLERANCE * 100}% off the period`).toEqual([]);
+    }
   }
   expect(audio.setSampleRate(44100)).toBeTruthy(); // back to the engine's default, for a case added after this one
 });
diff --git a/packages/retroplug/test-native/lsdj-midiout.test.ts b/packages/retroplug/test-native/lsdj-midiout.test.ts
index 62866f93..33a86890 100644
--- a/packages/retroplug/test-native/lsdj-midiout.test.ts
+++ b/packages/retroplug/test-native/lsdj-midiout.test.ts
@@ -1,9 +1,11 @@
 // Arduinoboy MIDIOUT (MI.OUT / "Arduinoboy Master", mode 7) — the full end-to-end functional proof on a
 // real LSDj core. A song authored with SYNC=MI.OUT + N (NoteOn) commands, once playing, emits the
 // Arduinoboy protocol on LSDj's serial-out port; native captures those raw bytes (armed via
-// setSerialOutCapture) and feeds them to the lsdj-sync node, where the mode-7 decoder strips the flag-gated
-// framing + decodes the byte protocol into host MIDI (its midiOut port). We drain that MIDI and assert a
-// transport START (0xFA), a clock stream (0xF8), and NoteOns (0x9x).
+// setSerialOutCapture) and feeds them, in the block it captured them, to the lsdj-sync role's decoder after the
+// Game Boy (rp.lsdj.serial-out), where the mode-7 decoder strips the flag-gated framing + decodes the byte
+// protocol into host MIDI (its midiOut port), each message at the frame of the byte that completes it. We drain
+// that MIDI and assert a transport START (0xFA), a clock stream (0xF8), and NoteOns (0x9x), at frames inside
+// their blocks.
 //
 // This is the reversal of the old "MI.OUT is unreachable headlessly" folklore (docs/lsdj.md): SYNC=MI.OUT
 // is byte 9 (cold-authored here), and the wire's 1-flag-bit + 7-payload-bit framing — which the raw 8-bit
@@ -49,7 +51,7 @@ const lsdjSync = (mode: string) => [{ kind: "lsdj-sync", config: { mode } }];
 // Flatten a drainMidiOut result into the raw status/data byte messages for assertions.
 const messages = (drained: { data: Uint8Array }[]): number[][] => drained.map((m) => Array.from(m.data));
 
-test("MIDIOUT (mode 7): a real LSDj in SYNC=MI.OUT emits Arduinoboy MIDI the kernel decodes to host MIDI", () => {
+test("MIDIOUT (mode 7): a real LSDj in SYNC=MI.OUT emits Arduinoboy MIDI its decoder turns into host MIDI", () => {
   const be = createRealBackend();
   if (!be.fileExists(ABOY)) skip(`lsdj-midiout: aboy LSDj ROM not found at ${ABOY}`);
 
@@ -91,7 +93,8 @@ test("MIDIOUT (mode 7): a real LSDj in SYNC=MI.OUT emits Arduinoboy MIDI the ker
   tap(); // play again → 0xFA, mid-stream where framing is aligned
   audio.renderAudio(2000);
 
-  const drained = messages(audio.drainMidiOut());
+  const out = audio.drainMidiOut();
+  const drained = messages(out);
   const status = drained.map((m) => m[0]);
   const starts = status.filter((s) => s === 0xfa).length;
   const stops = status.filter((s) => s === 0xfc).length;
@@ -105,4 +108,11 @@ test("MIDIOUT (mode 7): a real LSDj in SYNC=MI.OUT emits Arduinoboy MIDI the ker
   expect(noteOns > 0).toBeTruthy();
   // A transport bookend — the stop→restart toggle sends 0xFC then 0xFA on the (aligned) MI.OUT stream.
   expect(starts + stops > 0).toBeTruthy();
+  // Each message at its byte's frame in the block it was captured in: inside renderAudio's 1024-frame blocks, and
+  // spread across them, never all at a block's frame 0 as a decoder timestamping by block would send them.
+  const frames = out.map((m) => m.frame);
+  const offZero = frames.filter((f) => f > 0).length;
+  console.log(`[lsdj-midiout] frames ${Math.min(...frames)}..${Math.max(...frames)}, ${offZero} of ${frames.length} past frame 0`);
+  expect(frames.every((f) => f >= 0 && f < 1024)).toBeTruthy();
+  expect(offZero > frames.length / 2).toBeTruthy();
 });
diff --git a/packages/retroplug/test-native/parity/blocks-tiny-lsdj.test.ts b/packages/retroplug/test-native/parity/blocks-tiny-lsdj.test.ts
index 2fc72f88..3a5cfef9 100644
--- a/packages/retroplug/test-native/parity/blocks-tiny-lsdj.test.ts
+++ b/packages/retroplug/test-native/parity/blocks-tiny-lsdj.test.ts
@@ -1,10 +1,10 @@
 // LSDj's host-sync protocols in 1-frame blocks and FL-style ragged splits (blocks-tiny.ts has the lists, lsdj.ts
 // the scenes): SYNC=MIDI following the host clock, each tick at its own frame; the Arduinoboy protocol, its
 // clocks at a block's frame 0, its play / stop notes, 0xFA and 0xFC on the transport's edges; MI.OUT, LSDj's
-// serial out decoded into host MIDI out a block later, so the recorded block indices pin that one-block latency
-// at every block size here. Master sync is blocks-tiny-lsdj-master.test.ts. Tempo
-// and transport points after a segment's start sit on 1-frame blocks, where FL puts them (it splits a buffer
-// at each one), and the sync notes on block edges. Needs the sibling LSDj
+// serial out decoded into host MIDI out in the block the core captured it, each message at its completing byte's
+// frame, which the recorded block indices and frames pin at every block size here. Master sync is
+// blocks-tiny-lsdj-master.test.ts. Tempo and transport points after a segment's start sit on 1-frame blocks,
+// where FL puts them (it splits a buffer at each one), and the sync notes on block edges. Needs the sibling LSDj
 // ROMs, so these skip in CI.
 import { runGroup, type ParityCase } from "./group";
 import { ANY, BIG, BUFFER, Edges, TINY_SPLITS } from "./blocks-tiny";
diff --git a/packages/retroplug/test-native/parity/blocks-tiny.ts b/packages/retroplug/test-native/parity/blocks-tiny.ts
index 96114c11..27f89d92 100644
--- a/packages/retroplug/test-native/parity/blocks-tiny.ts
+++ b/packages/retroplug/test-native/parity/blocks-tiny.ts
@@ -15,9 +15,9 @@ import { BOOT_MS, frames, mgb, MGB_CHANNELS, NES_CHANNELS, phrase, ROMS, type Bl
 
 /** The design's ragged list (section 7.1). No buffer grid at all: 1-frame blocks between odd sizes, and a
  *  4096-frame block next to a 2-frame one, the largest swing in size from one block to the next, which is
- *  what a core's per-block run-ahead and MI.OUT's one-block latency move with. It sums to 4362 frames, a
- *  multiple of neither a 24-PPQN tick nor a console frame, so its edges drift against both from one cycle to
- *  the next rather than meeting them at one phase. */
+ *  what a core's per-block run-ahead moves with. It sums to 4362 frames, a multiple of neither a 24-PPQN tick
+ *  nor a console frame, so its edges drift against both from one cycle to the next rather than meeting them
+ *  at one phase. */
 export const FL_RAGGED = [33, 1, 222, 1, 7, 4096, 2];
 
 /** FL's own shape: a fixed 256-frame buffer, split inside at automation points and never across one, as five
````

Run: `cmake --build build --target retroplug-host -j$(nproc) && RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native lsdj-mastersync && RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native lsdj-midiout`
Expected: `# 1 native test file(s) passed (jobs=16)` twice.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources tools/run-profile.sh stats`
Expected: `DSP-BENCH {"profile":"A","cores":1,"warmupBlocks":200,"blocks":2000,"allocsPerBlock":0,"bytesPerBlock":0,"freesPerBlock":0,"liveHeapDelta":0,"peakLive":311912,"maxBlockAllocs":0,"maxBlockBytes":0,"gcMs":0.07,"gcFreedBytes":0,"xRT":16.38}`, `DSP-BENCH {"workload":"lsdj-masterSync","warmupBlocks":200,"blocks":400,"allocsPerBlock":0,"bytesPerBlock":0,"maxBlockAllocs":0,"maxBlockBytes":0,"midiOut":476,"clocks":476,"noteOns":0}` and `DSP-BENCH {"workload":"lsdj-midiOut","warmupBlocks":200,"blocks":400,"allocsPerBlock":0,"bytesPerBlock":0,"maxBlockAllocs":0,"maxBlockBytes":0,"midiOut":60,"clocks":40,"noteOns":20}`: still no allocation in any block.

- [ ] **Step 8: Re-record the three parity groups, and prove what moved**

Run: `RP_PARITY_UPDATE=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity/lsdj-modes`
Expected: `# 1 native test file(s) passed (jobs=16)`, recording.

Run: `RP_PARITY_UPDATE=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity/blocks-tiny-lsdj-master`
Expected: `# 1 native test file(s) passed (jobs=16)`, recording.

Run: `RP_PARITY_UPDATE=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity/blocks-tiny-lsdj`
Expected: `# 1 native test file(s) passed (jobs=16)`, recording.

Run: `git diff --stat 0cdf1abb -- packages/retroplug/test-native/parity/baseline/`
Expected: nothing: the three baselines equal the prototype's byte for byte (the emulation is deterministic).

Run: `RP_PARITY_DUMP=/tmp/lsdj-serial-out/dump-after RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity/lsdj-modes`
Expected: `# 1 native test file(s) passed (jobs=16)`.

Run: `RP_PARITY_DUMP=/tmp/lsdj-serial-out/dump-after RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity/blocks-tiny-lsdj-master`
Expected: `# 1 native test file(s) passed (jobs=16)`.

Run: `RP_PARITY_DUMP=/tmp/lsdj-serial-out/dump-after RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity/blocks-tiny-lsdj`
Expected: `# 1 native test file(s) passed (jobs=16)`.

The checker holds the design's claim case by case: every case of the three groups keeps every lane, its SRAM and its
work RAM; the twelve cases that should move send the same message bytes in the same order (with whatever a case's last
block now decodes after them), each in the same block as before or an earlier one; every other case keeps its whole
digest; and each dump hashes to its baseline's digest, so the dumps are the baselines' runs.

Save the script as `/tmp/lsdj-serial-out/check-moved.py`:

````python
#!/usr/bin/env python3
"""What the stage-4 parity re-record moved, case by case, in the three LSDj groups it re-recorded.

Inputs: the baselines before (/tmp/lsdj-serial-out/baseline-before/<group>.json, copied from the stage-3 tree) and after (the
worktree's test-native/parity/baseline/<group>.json), and each case's dump before and after
(/tmp/lsdj-serial-out/dump-{before,after}/<group>/<case>/midi-out.json and lane<n>.wav, RP_PARITY_DUMP).

Holds:
  1. every case: every segment (frames and each lane's "fnv rms"), its SRAM and its work RAM identical to the old
     baseline, and its dumped lanes byte for byte;
  2. the cases that should move: MIDI out's message BYTES, in order, equal the old list's followed by a (possibly
     empty) suffix, and each prefix message goes out in the same block as before or an earlier one;
  3. every other case: its whole digest identical, MIDI out included, and its dumped MIDI out identical;
  4. each dump's MIDI out hashes to its baseline's digest (the FNV-1a of parity/digest.ts midiDigest), so the dumps are
     the baselines' runs.
Exit status 0 when all hold.
"""
import json
import os
import sys

ROOT = "/workspaces/RetroPlug-main/cuttlefish/packages/retroplug/test-native/parity/baseline"
BEFORE = "/tmp/lsdj-serial-out/baseline-before"
DUMP_BEFORE = "/tmp/lsdj-serial-out/dump-before"
DUMP_AFTER = "/tmp/lsdj-serial-out/dump-after"

GROUPS = ["lsdj-modes", "blocks-tiny-lsdj-master", "blocks-tiny-lsdj"]
SHOULD_MOVE = {
    "lsdj-modes": lambda c: c in ("midiOut", "masterSync-44100", "masterSync-48000"),
    "blocks-tiny-lsdj-master": lambda c: True,
    "blocks-tiny-lsdj": lambda c: c.startswith("midiOut-"),
}

FNV_OFFSET, FNV_PRIME = 0x811C9DC5, 0x01000193


def fnv1a(data, h):
    for b in data:
        h = ((h ^ b) * FNV_PRIME) & 0xFFFFFFFF
    return h


def midi_digest(entries):
    """parity/digest.ts midiDigest: per message (block, slot, frame, length) as LE u32, then the bytes."""
    h = FNV_OFFSET
    for e in entries:
        for n in (e["block"], e["slot"], e["frame"], len(e["data"])):
            h = fnv1a((n & 0xFFFFFFFF).to_bytes(4, "little"), h)
        h = fnv1a(bytes(e["data"]), h)
    return {"count": len(entries), "fnv": f"{h:08x}"}


def hexes(msgs):
    return [" ".join(f"{b:02x}" for b in m) for m in msgs]


def load(path):
    with open(path) as f:
        return json.load(f)


failures = []
lines = []


def fail(msg):
    failures.append(msg)
    lines.append(f"    FAIL: {msg}")


for group in GROUPS:
    old = load(f"{BEFORE}/{group}.json")
    new = load(f"{ROOT}/{group}.json")
    lines.append(f"{group}: target {old['target']} -> {new['target']}, {len(old['cases'])} -> {len(new['cases'])} cases")
    if old["target"] != new["target"]:
        fail(f"{group}: target changed")
    if sorted(old["cases"]) != sorted(new["cases"]):
        fail(f"{group}: case names changed: {sorted(old['cases'])} -> {sorted(new['cases'])}")
    for name in sorted(old["cases"]):
        o, n = old["cases"][name], new["cases"].get(name)
        if n is None:
            continue
        moves = SHOULD_MOVE[group](name)
        tag = "moves" if moves else "holds"
        # 1. the audio, the battery and the work RAM
        same_audio = o["segments"] == n["segments"] and o["sram"] == n["sram"] and o["ram"] == n["ram"]
        dir_o, dir_n = f"{DUMP_BEFORE}/{group}/{name}", f"{DUMP_AFTER}/{group}/{name}"
        lanes_o = sorted(f for f in os.listdir(dir_o) if f.endswith(".wav"))
        lanes_n = sorted(f for f in os.listdir(dir_n) if f.endswith(".wav"))
        same_wavs = lanes_o == lanes_n and all(
            open(f"{dir_o}/{f}", "rb").read() == open(f"{dir_n}/{f}", "rb").read() for f in lanes_o
        )
        # 4. the dumps are the baselines' runs
        mo, mn = load(f"{dir_o}/midi-out.json"), load(f"{dir_n}/midi-out.json")
        if midi_digest(mo) != o["midiOut"]:
            fail(f"{group}/{name}: the old dump's MIDI out does not hash to the old baseline's")
        if midi_digest(mn) != n["midiOut"]:
            fail(f"{group}/{name}: the new dump's MIDI out does not hash to the new baseline's")
        bo, bn = [m["data"] for m in mo], [m["data"] for m in mn]
        summary = (
            f"  {name} [{tag}]: segments {len(n['segments'])} {'same' if o['segments'] == n['segments'] else 'MOVED'}, "
            f"sram {'same' if o['sram'] == n['sram'] else 'MOVED'}, ram {'same' if o['ram'] == n['ram'] else 'MOVED'}, "
            f"lanes {len(lanes_n)} wav {'same' if same_wavs else 'MOVED'}; MIDI out {o['midiOut']['count']} -> "
            f"{n['midiOut']['count']} ({o['midiOut']['fnv']} -> {n['midiOut']['fnv']})"
        )
        lines.append(summary)
        if not same_audio:
            fail(f"{group}/{name}: a segment, the SRAM or the work RAM moved")
        if not same_wavs:
            fail(f"{group}/{name}: a dumped lane moved")
        if moves:
            # 2. the same bytes in order, then a suffix
            if bn[: len(bo)] != bo:
                k = next((i for i, (a, b) in enumerate(zip(bo, bn)) if a != b), min(len(bo), len(bn)))
                fail(f"{group}/{name}: the old message list is not a prefix of the new one (first difference at {k})")
            else:
                suffix = bn[len(bo):]
                earlier = [mo[i]["block"] - mn[i]["block"] for i in range(len(mo))]
                later = [d for d in earlier if d < 0]
                hist = {}
                for d in earlier:
                    hist[d] = hist.get(d, 0) + 1
                off_zero = sum(1 for m in mn if m["frame"] != 0)
                lines.append(
                    f"      prefix of {len(bo)} identical; suffix {len(suffix)}: {hexes(suffix)}; "
                    f"blocks earlier than before (old - new: count) {dict(sorted(hist.items()))}; "
                    f"{off_zero} of {len(mn)} new messages off frame 0 (old: "
                    f"{sum(1 for m in mo if m['frame'] != 0)} of {len(mo)})"
                )
                if later:
                    fail(f"{group}/{name}: {len(later)} message(s) go out in a later block than before")
                if o["midiOut"] == n["midiOut"]:
                    fail(f"{group}/{name}: was to move, and its MIDI out did not")
        else:
            # 3. nothing at all
            if o != n:
                fail(f"{group}/{name}: its digest moved")
            if mo != mn:
                fail(f"{group}/{name}: its dumped MIDI out moved")

print("\n".join(lines))
print(f"\n{len(failures)} failure(s)" if failures else "\nall held")
sys.exit(1 if failures else 0)
````

Run: `python3 /tmp/lsdj-serial-out/check-moved.py`
Expected: exit status 0 and a last line `all held`. In the prototype's run each of the twelve moving cases kept its count (masterSync 85, lsdj-modes' midiOut 42, blocks-tiny-lsdj's midiOut-* 38) with an empty suffix, every message a block earlier (but one stop in each 1-frame master-sync case, which stays in its block), and every message off frame 0 except in the 1-frame splits, where frame 0 is the only frame.

- [ ] **Step 9: Run everything**

Run: `pnpm typecheck && pnpm check:docs`
Expected: `packages/retroplug typecheck: Done`, then `> node tools/check-doc-links.mjs`.

Run: `pnpm test`
Expected: `# 167 test file(s) passed (jobs=16)` (one file more: the node's).

Run: `pnpm test:plugin`
Expected: `# 10 plugin test binary(ies) passed (jobs=16)`, the engine-graph binary's `All tests passed (15598 assertions in 120 test cases)`.

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=16)`, none skipped, the re-recorded groups among them.

Run: `pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=16)`, with `46 case(s) SKIPPED in 10 file(s)` (every fixture group's cases, as CI runs them) and the skip check clean: every re-recorded case is fixture-only, so the skip baseline holds.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native`
Expected: `# 154 native test file(s) passed (jobs=16)`, with `2 case(s) SKIPPED in 2 file(s): dsp-bench(1), nes-bench(1)`.

Run: `pnpm test:node && pnpm test:cli-ts`
Expected: `ℹ pass 12`, then `PASS: retroplug-cli TypeScript harness`.

- [ ] **Step 10: Commit**

````bash
git add -- packages/retroplug/src/script/nodes/lsdjSerialOut.ts \
  packages/retroplug/src/script/nodes/lsdjSyncSettings.ts \
  packages/retroplug/test/script/nodes/lsdj-serial-out.test.ts \
  packages/native/test/engine-graph/BuildGraphGoldens.test.cpp \
  packages/native/test/engine-graph/ProtocolNodes.test.cpp packages/native/test/engine-graph/ScriptBench.test.cpp \
  packages/native/test/engine-graph/ScriptNodes.test.cpp packages/retroplug/src/backend.ts \
  packages/retroplug/src/graph/buildGraph.ts packages/retroplug/src/graph/protocolNodes.ts \
  packages/retroplug/src/lsdjArduinoboy.ts packages/retroplug/src/script/bundle.ts \
  packages/retroplug/src/script/nodes/lsdjSync.ts packages/retroplug/test-native/dsp-bench.test.ts \
  packages/retroplug/test-native/lsdj-mastersync.test.ts packages/retroplug/test-native/lsdj-midiout.test.ts \
  packages/retroplug/test-native/parity/baseline/blocks-tiny-lsdj-master.json \
  packages/retroplug/test-native/parity/baseline/blocks-tiny-lsdj.json \
  packages/retroplug/test-native/parity/baseline/lsdj-modes.json \
  packages/retroplug/test-native/parity/blocks-tiny-lsdj.test.ts \
  packages/retroplug/test-native/parity/blocks-tiny.ts packages/retroplug/test/dsp/lsdj-arduinoboy.test.ts \
  packages/retroplug/test/graph/build-graph-role-goldens.test.ts packages/retroplug/test/graph/build-graph.test.ts \
  packages/retroplug/test/graph/golden/build-graph-roles.json packages/retroplug/test/graph/protocol-nodes.test.ts \
  packages/retroplug/test/script/bundle-no-zod.test.ts packages/retroplug/test/script/nodes/lsdj-sync.test.ts \
  packages/retroplug/test/systems/store-graph.test.ts
git commit -F - <<'EOF'
feat(lsdj): rp.lsdj.serial-out decodes MI.OUT and master sync after the Game Boy, each message at its byte's frame

An lsdj-sync role is two nodes. rp.lsdj.sync, before its Game Boy, keeps the host-to-cart direction (the routed MIDI
and the launches in, serial in and buttons out) and loses its serialOut input and midiOut output. rp.lsdj.serial-out,
sys-<id>/lsdj-sync/out, placed after the systems on a Game Boy only, reads the Game Boy's serialOut over a forward
wire in the block the core captured it and runs the mode's decoder: each message goes out at the frame the decoder
gives it, into the rp.host.midi-out input rp.lsdj.sync held, under the same system and in the same order. Both take
the role's config through one schema (lsdjSyncSettings.ts), the MI.MAP controller's forced mode included; the
decoder reads only `mode`, reads nothing in the other modes, keeps MI.OUT's state across a mode change and drops
master sync's outside it. It registers last in the bundle, span label 23. protocolNodes.ts gains the decoder's spec and
an `out` field naming it on rp.lsdj.sync's; buildGraph places each `out` after the systems with its before-node's
settings, and the documents hold no feedback wire.

Master sync's bytes are this block's, so MasterSyncState.lastFrames goes: a byte at frame a sends at a, and a run's
stop goes at its mark, lastAt + stop + 1, before the byte at or past it or at the end of the block holding it, a mark
at the block's length carrying to the next block's frame 0. The same rule (gap > stop), so the same messages in order.
Decoder tests hold one absolute byte stream to the same messages at the same absolute frames in 1-frame, FL ragged,
FL buffer, long ragged and 1024-frame blocks, for master sync and MI.OUT.

Re-recorded: parity groups lsdj-modes (midiOut, masterSync-44100, masterSync-48000), blocks-tiny-lsdj-master (all
six) and blocks-tiny-lsdj (midiOut-1, -fl, -fl256). A checker over the old and new baselines and dumps found every
case's segments, SRAM, work RAM and dumped lanes identical; in the twelve moved cases MIDI out's bytes in order
identical to the old list with an empty suffix (85, 85, 42, 85 x6 and 38 x3 messages), every message in the same block
or an earlier one; every other case's digest identical. The role goldens re-record 24 of 37 cases, all and only those
with an LSDj on a Game Boy, each gaining its decoder and wires and losing the feedback wire, midi-out's inputs and
settings unchanged. ProtocolNodes.test.cpp's three digests with MI.OUT or master-sync traffic re-record (counts
unchanged), its header saying they differ from the kernel's by design. lsdj-mastersync holds consecutive clocks within
3% of a tempo period at every split and both rates (each run's first within 30%), which a decoder sending at a block's
frame 0 fails in the ragged and 1024-frame splits.
EOF
````

### Task 5: the docs say what was built

Docs and comments only, no behaviour change. The specs, the LSDj guide and AGENTS.md describe the two LSDj nodes, the
merge by frame and `apu-render` as built; spec/07 drops the items this change closed (the master-sync frames, the
decoders' allocations) and records what it leaves (the core's serial byte timing moving with the split, the SDL
standalone sending host MIDI at block time); and the design gets its Status line and a section 12, "Found while
building". The historical plans and the B3b design are records and are not edited.

**Files:**
- Modify: `AGENTS.md`, `docs/lsdj-serial-out-design.md`, `docs/lsdj.md`, `docs/modular-graph-design.md`, `docs/sdl-standalone.md`, `packages/native/sdl/main.cpp`, `packages/native/src/host/rpc/EngineRpcService.hpp`, `packages/native/test/engine-graph/HostIo.test.cpp`, `packages/retroplug/cli/sdk-types.d.ts`, `packages/retroplug/src/audioDriver.ts`, `packages/retroplug/src/graph/defineNode.ts`, `packages/retroplug/src/systemsStore.ts`, `packages/retroplug/test/systems/store-graph.test.ts`, `spec/01-architecture.md`, `spec/02-native-host.md`, `spec/03-ts-layer.md`, `spec/04-roles-protocol-nodes.md`, `spec/06-build-test.md`, `spec/07-remaining-work.md`, `spec/08-profiling.md`, `spec/13-subsystems.md`, `tools/run-profile.sh`

- [ ] **Step 1: Update the docs and the comments**

````diff
diff --git a/AGENTS.md b/AGENTS.md
index 5a450c5f..78c616d4 100644
--- a/AGENTS.md
+++ b/AGENTS.md
@@ -45,7 +45,11 @@ The rules below are the parts that don't fit those.
   controller session and each role's protocol node, in the documents `buildGraph` builds, read from its role goldens
   and run in its plan order, `test/engine-graph/ProtocolNodes.test.cpp`) send exactly the digests the DSP kernel they
   replaced was recorded sending in the cases it runs, a throw stopping only its own node the one difference those
-  reach, checked (the B3b design's section 9 lists the others, which no case sends a message through); and every
+  reach, checked (the B3b design's section 9 lists the others, which no case sends a message through), but for the
+  host MIDI out of MI.OUT and master sync, which differs by design: LSDj's decoder, `rp.lsdj.serial-out`, after its
+  Game Boy, sends each message at its byte's frame in the block the core captured it, where the kernel sent it at
+  frame 0 of the block after, so the three cases that carry it hold that decoder's recorded digests, every system
+  input in them still the kernel's ([docs/lsdj-serial-out-design.md](docs/lsdj-serial-out-design.md)); and every
   document in `buildGraph`'s goldens, handed to the engine with real cores, plans and runs with no node faulting, no
   script node throwing and no message dropped: the B1 translator's matrix a block each (the 33-system `perSystem` ones
   are refused, as they must be), and `golden/build-graph-roles.json`, `buildGraph`'s own record of documents with every protocol node
@@ -53,7 +57,7 @@ The rules below are the parts that don't fit those.
   `buildGraph`'s, held by `pnpm test graph` to explicit tables, to the B1 translator's goldens and to the role
   goldens, which `RP_GOLDEN_UPDATE=1 pnpm test graph/build-graph-role-goldens` re-records). A hidden
   bench times a script node per block, and the protocol nodes' plan, at 1, 64 and 512 frames
-  (`build/bin/retroplug-engine-graph-test "[bench]"`), and a profile build adds five `[profile]` cases
+  (`build/bin/retroplug-engine-graph-test "[bench]"`), and a profile build adds six `[profile]` cases
   (`tools/run-profile.sh unit`).
   `retroplug-dynparams-test` is the odd one out: it compiles DPF's format-neutral core (`DistrhoPlugin.cpp`)
   plus its OWN `DistrhoPluginInfo.h`, building no plugin format at all, to drive
diff --git a/docs/lsdj-serial-out-design.md b/docs/lsdj-serial-out-design.md
index c98f261a..5a31e0ac 100644
--- a/docs/lsdj-serial-out-design.md
+++ b/docs/lsdj-serial-out-design.md
@@ -1,7 +1,6 @@
 # LSDj's serial out as its own node: host MIDI at each byte's frame
 
-**Date:** 2026-10-28 · **Branch:** `feature/modularize` · **Status:** designed (prototype next, then a plan, as for
-B3b and the master-sync fix)
+**Date:** 2026-10-28 · **Branch:** `feature/modularize` · **Status:** built: section 10's five commits
 
 LSDj sends host MIDI two ways, both decoded from what it clocks out of its link port: MI.OUT (lsdj-sync mode
 `midiOut`, Arduinoboy's Mode_LSDJ_Midiout: notes, CCs, program changes and realtime) and master sync (mode
@@ -204,3 +203,44 @@ Accepted on a run with the fixtures present and `RP_FAIL_ON_SKIP=1`, plus the su
 - **The capture mode as a Game Boy node setting**, Phase C as spec/07 says: a mode switch still publishes the
   document and sets the capture through `ConfigField::SerialOutCapture`, so for a block the two can disagree, as
   the master-sync design's section 10 accepted.
+
+## 12. Found while building
+
+- **No node of RetroPlug's can hand the merge an unsorted input, so the guard needed one of its own.** Section 8 had
+  a case pin the guard. Every node pushes through `rp::graph::pushMessage`, which raises a frame pushed behind a
+  later one, and so does the test feed: in the old case whose input went back in frame, its queue had raised that
+  frame before `rp.host.midi-out` saw it. [HostIo.test.cpp](../packages/native/test/engine-graph/HostIo.test.cpp)
+  pins the guard with a test-only node, `rp.test.raw-feed`, that writes its queue's items directly and out of order,
+  as a node on the C ABI may.
+- **`apu-render` is recorded after the spans it holds.** A span is stored as it closes, so a block's `apu-render`,
+  opened at the plan's start, follows its script nodes' process spans in the trace. The trace-event JSON gives each
+  span its start and length, so nothing that reads it depends on the order.
+- **LSDj's first clock of a run comes early.** Section 8's spacing check has to allow it. In every split and at both
+  rates a run's clock gaps follow LSDj's own repeating pattern of six, 846 to 874 frames at 44.1 kHz (921 to 951 at
+  48), within 1.8% of the tempo's period, while its first tick after START comes at 0.78 of a period (673 frames at
+  44.1 kHz). [lsdj-mastersync.test.ts](../packages/retroplug/test-native/lsdj-mastersync.test.ts) holds the gaps
+  within 3% of the period, which still refuses a clock moved to its block's start (a 1024-frame block is 19% past
+  the period), and a run's first gap within 30%. The split moves the whole stream by up to 32 frames and its gaps
+  not at all, as section 8 said of the core.
+- **No re-recorded case gained a message.** Section 7 expected each case's last block, decoded at last, to add what
+  it held. In all twelve the new MIDI out is the old one's messages in the same order with nothing after them, each
+  a block earlier than before, but for one stop in each 1-frame master-sync case, which stays in its block; a
+  checker over the old and new baselines and their dumps found every lane, SRAM and work RAM of the three groups,
+  and every other case's whole digest, unchanged.
+- **A hand-edited `lsdj-sync` on another system loses its host MIDI input.** The decoder is placed only on a Game
+  Boy, as the feedback wire was, but `rp.host.midi-out`'s input for the system had been `rp.lsdj.sync`'s, placed on
+  any system. Such a system (no ROM provider attaches `lsdj-sync` to anything but a Game Boy) has its `rp.lsdj.sync`
+  placed and no decoder, so `rp.host.midi-out` has no input for it, where it had one that sent nothing;
+  [build-graph.test.ts](../packages/retroplug/test/graph/build-graph.test.ts) pins it, and no golden holds one.
+- **The controller's order case checks the merge.**
+  [ProtocolNodes.test.cpp](../packages/native/test/engine-graph/ProtocolNodes.test.cpp)'s case for a controller's
+  launch beside two LSDjs' host MIDI held the system tags in input order; under the merge the host's list is ordered
+  by frame and then input, the launch first at frame 0, so the case checks that. Its digest re-recorded with the
+  LSDj modes' and the controller session's, every count unchanged.
+- **The decoder reads nothing outside its two modes.** Section 3 said its input is empty there; it does not read it
+  at all, a test handing it one that throws, so its call costs the mode check. In `ScriptBench`'s MIDI-sync
+  structure, whose stand-in clocks the idle line into it, it reads none of it.
+- **A spec's `out` is a string.** [protocolNodes.ts](../packages/retroplug/src/graph/protocolNodes.ts)'s `out` names
+  a `PROTOCOL_NODES` key, but typing it as one would make the `as const` table refer to its own keys, so `outNodeOf`
+  casts it and [protocol-nodes.test.ts](../packages/retroplug/test/graph/protocol-nodes.test.ts) holds every `out`
+  to a key whose spec writes no system input.
diff --git a/docs/lsdj.md b/docs/lsdj.md
index 66ec50dc..7bad8dc9 100644
--- a/docs/lsdj.md
+++ b/docs/lsdj.md
@@ -88,11 +88,12 @@ The two config surfaces are distinct and easy to confuse:
   `"Keyboard"`, `"AnalogIn"`, `"AnalogOut"`. Part of the song data.
 - **The `lsdj-sync` role config**: RetroPlug's own behaviour, its `mode` one of `"off"`, `"midiSync"`,
   `"midiSyncArduinoboy"`, `"midiMap"`, `"keyboard"`, `"keyboardMidi"`, `"midiPassthrough"`, `"midiOut"`,
-  `"masterSync"` (`LSDJ_MODE_VALUES` in [settingsEnums.ts](../packages/retroplug/src/settingsEnums.ts), run
-  by the role's protocol node, [lsdjSync.ts](../packages/lsdj/ts/lsdjSyncNode.ts)). A feature
-  role: it is set with the store's `setRoleConfig` and handed to that node as its settings in the graph the
-  project publishes, while native's `applyRoleConfig` takes only the `sameboy` and `mesen` system roles (any
-  other kind returns false).
+  `"masterSync"` (`LSDJ_MODE_VALUES` in [settingsEnums.ts](../packages/retroplug/src/settingsEnums.ts), run by
+  the role's two protocol nodes: [lsdjSync.ts](../packages/lsdj/ts/lsdjSyncNode.ts) before the
+  Game Boy, host to cart, and [lsdjSerialOut.ts](../packages/lsdj/ts/lsdjSerialOutNode.ts)
+  after it, MI.OUT and master sync from the cart to the host). A feature role: it is set with the store's
+  `setRoleConfig` and handed to both nodes as their settings in the graph the project publishes, while
+  native's `applyRoleConfig` takes only the `sameboy` and `mesen` system roles (any other kind returns false).
 
 A test that builds through a `DirectGraph` has no store, so it hands the graph a system's roles and the
 MIDI routing itself, each call a publish of the graph that places their nodes. Until a routing mode is
@@ -101,7 +102,7 @@ set, no host MIDI reaches any system. From
 
 ```ts
 graph.setMidiRouting("sendToAll");                                                 // host MIDI to every system
-graph.setPipeline(id, [{ kind: "lsdj-sync", config: { mode: "masterSync" } }]);    // places sys-<id>/lsdj-sync, an rp.lsdj.sync
+graph.setPipeline(id, [{ kind: "lsdj-sync", config: { mode: "masterSync" } }]);    // places rp.lsdj.sync and rp.lsdj.serial-out
 be.setSerialOutCapture(id, "listen"); // the store sets it ("drive" for midiOut, "listen" for masterSync); a DirectGraph caller does it itself
 ```
 
@@ -112,9 +113,10 @@ MIDI sync), so spell out what the test depends on.
 The rest of the native-tier surface these tests use:
 `audio.renderAudioPerSystem(ms)` (per-system buffers in slot order — the thing that proves link
 sync, where a mixed buffer cannot), `audio.setTransport(bool)` / `audio.setBpm(n)` (simulated host
-transport -> the role's MIDI clock), `audio.drainMidiOut()` (the host MIDI the LSDj nodes and the controller session sent since the
-last drain, each message with its system and frame, LSDj's MI.OUT among them, decoded from the captured
-serial out), `be.readMemory(id, region)` (WRAM/SRAM reads), and `graph.remove(id)`
+transport -> the role's MIDI clock), `audio.drainMidiOut()` (the host MIDI the LSDj nodes and the controller
+session sent since the last drain, each message with its system and its frame in the block it went out in,
+LSDj's MI.OUT and master sync among them, decoded from the captured serial out in the block it was captured,
+each at its byte's frame), `be.readMemory(id, region)` (WRAM/SRAM reads), and `graph.remove(id)`
 ([directGraph.ts](../packages/retroplug/src/graph/directGraph.ts)). The `audio` calls are declared in
 [audioDriver.ts](../packages/retroplug/src/audioDriver.ts) and `readMemory` in
 [backend.ts](../packages/retroplug/src/backend.ts); never call `audio.startAudio()` in a test — the
@@ -580,13 +582,28 @@ mode selected" indicator on the aboy build. Read the SYNC field text (or the
 
 Serial-out capture is a mode the store sets from the `lsdj-sync` mode (`serialOutCaptureFor` in
 [systemsStore.ts](../packages/retroplug/src/systemsStore.ts)): `drive` for MI.OUT, `listen` for master sync.
-The captured bytes reach the system's `rp.lsdj.sync` node over the Game Boy node's `serialOut` wire, each at
-the frame its core completed it, and `audio.drainMidiOut()` returns what the decoders made of them.
+The captured bytes reach the system's `rp.lsdj.serial-out` node, placed after the Game Boy, over a forward
+wire from the Game Boy node's `serialOut`, in the block the core captured them, each at the frame its core
+completed it, and the decoders send each message in that same block, at its byte's frame:
+
+- **MI.OUT**: a message at the frame of the captured byte whose bits complete it; a command whose bits
+  straddle two blocks goes in the later one, at its completing byte's frame there.
+- **Master sync**: a `0xF8` at each byte's frame, a run's song-row NoteOn and `0xFA` at its first byte's, and
+  the run's `0xFC` at its 200 ms mark, the first frame the silence since the run's newest byte passes 200 ms
+  (`lastAt + stop + 1`, `stop` the 200 ms in frames at the block's rate, so a gap of exactly 200 ms is no
+  stop), in the block that holds the mark, ahead of a byte at or past it, which then opens a fresh run.
+
+`audio.drainMidiOut()` returns what the decoders made of them, each message with its frame in the
+`renderAudio` block it went out in (`renderBlocks` adds the block's index). The core's own byte timing
+moves by tens of frames with the host's block split, so a test holds each message to its byte, not to one
+absolute time shared by every split.
 See [lsdj-midiout.test.ts](../packages/retroplug/test-native/lsdj-midiout.test.ts),
 which authors SYNC=MI.OUT with `N` commands, plays, and asserts a transport START, a clock
-stream and NoteOns come back out as host MIDI, and
+stream and NoteOns come back out as host MIDI, at frames inside their blocks and mostly past frame 0, and
 [lsdj-mastersync.test.ts](../packages/retroplug/test-native/lsdj-mastersync.test.ts), which holds master
-sync to one story (start, clocks, stop, start again) in three block splits at 44.1 and 48 kHz.
+sync to one story (start, clocks, stop, start again) in three block splits at 44.1 and 48 kHz, and its
+clocks to LSDj's tempo in absolute frames in every split: consecutive clocks of a run within 3% of a tempo
+period (about 861 frames at 44.1 kHz, 937.5 at 48), a run's first gap, which LSDj sends early, within 30%.
 
 ### Synthetic external-clock serial (subtle but load-bearing)
 
diff --git a/docs/modular-graph-design.md b/docs/modular-graph-design.md
index 03fafb0c..362469ae 100644
--- a/docs/modular-graph-design.md
+++ b/docs/modular-graph-design.md
@@ -270,15 +270,18 @@ block to deliver). Data between two members of one coupled group needs a feedbac
 steps as one unit, so a same-block wire inside it would have no order. Messages on a feedback events or
 bytes wire keep the frames of the block that produced them, so a reader sees last block's coordinates,
 and a frame can lie past the end of a shorter reading block. The Game Boy's `serialOut` into its
-`rp.lsdj.sync` node (the DSP kernel's input until B3b) is the first feedback wire to carry frames other
-than 0: each byte at the frame its core completed it, which master sync times its stop by
+`rp.lsdj.sync` node (the DSP kernel's input until B3b) was the first feedback wire to carry frames other
+than 0: each byte at the frame its core completed it, which master sync timed its stop by
 ([lsdj-master-sync-fix-design.md](lsdj-master-sync-fix-design.md)).
 
-LSDj needs this today: `lsdj.sync` feeds the GB's `serialIn`, and the GB's `serialOut` comes back
-to `lsdj.sync` for MI.OUT. The current engine already gives that return path exactly one block of latency
-(it gathers `serialOutLog_` for the next block), so the graph makes existing behaviour visible. A wire
-flag was chosen over Reaktor's auto-inserted delay because the delay's position matters: the sync-to-GB
-edge must stay in the same block.
+LSDj was the case that called for this: `lsdj.sync` feeds the GB's `serialIn`, and the GB's `serialOut`
+came back to `lsdj.sync` for MI.OUT. The engine before the graph gave that return path exactly one block of
+latency (it gathered `serialOutLog_` for the next block), so the graph made existing behaviour visible. A
+wire flag was chosen over Reaktor's auto-inserted delay because the delay's position matters: the sync-to-GB
+edge must stay in the same block. That loop has since gone: LSDj's decoders moved into a node of their own,
+`rp.lsdj.serial-out`, placed after the Game Boy, which reads its `serialOut` over a forward wire in the block
+the core captured it ([lsdj-serial-out-design.md](lsdj-serial-out-design.md)), so no document the stores
+build holds a feedback wire.
 
 ### 3.4 Channels and layouts
 
diff --git a/docs/sdl-standalone.md b/docs/sdl-standalone.md
index bd847f1e..86cdca1e 100644
--- a/docs/sdl-standalone.md
+++ b/docs/sdl-standalone.md
@@ -55,7 +55,10 @@ Design:
 ### P2 — MIDI output — ✅ DONE
 `audioCb` drains `engine.midiOut()` to the `RtMidiOut` virtual port each block (mirrors
 [PluginDSP.cpp:178-186](../packages/native/plugin/PluginDSP.cpp#L178)) and mirrors it to the selected
-hardware output port, if any. Hardware-verified: LSDj Master-Sync clock (`0xF8`) reaches `aseqdump`.
+hardware output port, if any. It sends each message at once, its frame in the block dropped (RtMidi's out
+takes no timestamp), where the plugin hands the DAW each at its frame; scheduling it is the standalone's own
+work ([07-remaining-work.md](../spec/07-remaining-work.md)). Hardware-verified: LSDj Master-Sync clock
+(`0xF8`) reaches `aseqdump`.
 `RETROPLUG_MIDI_LOG` dumps in + out bytes.
 
 ### MIDI device selection — ✅ DONE
diff --git a/packages/native/sdl/main.cpp b/packages/native/sdl/main.cpp
index 5a20e61e..be941ce2 100644
--- a/packages/native/sdl/main.cpp
+++ b/packages/native/sdl/main.cpp
@@ -909,9 +909,10 @@ void renderAudioBlock(AppState& a, float* out, int frames) {
     a.svc.engine.processBlock(static_cast<std::uint32_t>(frames), a.audioPtrs.data(), static_cast<std::size_t>(N));
 
     // Host MIDI out (LSDj MI.OUT / Master Sync, the controller's launches) → the RtMidi out port, then clear
-    // the block's queue. Mirrors PluginDSP::run's writeMidiEvent drain (there it goes to the DAW). No length
-    // cap: MidiIo sends whatever it is given, and a control surface's LED traffic is sysex, so capping at 3
-    // bytes would silently drop exactly the messages that matter.
+    // the block's queue. Mirrors PluginDSP::run's writeMidiEvent drain (there it goes to the DAW), but at once,
+    // each message's frame dropped: RtMidi's out takes no timestamp, so a message goes when its block is rendered
+    // (spec/07-remaining-work.md). No length cap: MidiIo sends whatever it is given, and a control surface's LED
+    // traffic is sysex, so capping at 3 bytes would silently drop exactly the messages that matter.
     for (const auto& mo : a.svc.engine.midiOut())
         if (!mo.data.empty()) a.midi.send(mo.data.data(), mo.data.size());
     a.svc.engine.clearMidiOut();
diff --git a/packages/native/src/host/rpc/EngineRpcService.hpp b/packages/native/src/host/rpc/EngineRpcService.hpp
index 2f441f44..280178f7 100644
--- a/packages/native/src/host/rpc/EngineRpcService.hpp
+++ b/packages/native/src/host/rpc/EngineRpcService.hpp
@@ -89,8 +89,8 @@ public:
     // false for any other string. Control-plane, via the invoker's config path (ConfigField::SerialOutCapture):
     // the TS store calls it when a system's lsdj-sync mode changes.
     bool            setSerialOutCapture(std::uint32_t id, std::optional<std::string> mode);
-    // Drain the host MIDI out (the LSDj sync nodes' decoders), accumulated across renderAudio blocks
-    // since the last drain. For headless tests; the plugin drains Engine::midiOut() to the DAW directly.
+    // Drain the host MIDI out (LSDj's serial-out decoders, a controller's launches), accumulated across renderAudio
+    // blocks since the last drain. For headless tests; the plugin drains Engine::midiOut() to the DAW directly.
     std::vector<RpcMidiOut> drainMidiOut();
     // Render a scripted run of host blocks the way PluginDSP::run renders each one: stage the block's MIDI
     // at its frames, drain the command ring, apply its transport, processBlock(frames, lanes, outputs),
diff --git a/packages/native/test/engine-graph/HostIo.test.cpp b/packages/native/test/engine-graph/HostIo.test.cpp
index 5c779aef..d6285b3c 100644
--- a/packages/native/test/engine-graph/HostIo.test.cpp
+++ b/packages/native/test/engine-graph/HostIo.test.cpp
@@ -913,8 +913,9 @@ TEST_CASE("the Game Boy's serialOut carries what its core captured, each byte at
           "[engine-graph][host-io]") {
     // mGB, with MI.OUT capture armed, has a byte clocked out of its serial port every eight frames (it holds the
     // port in external-clock receive). The port carries this block's bytes a byte a message, each at the frame its core
-    // completed it, here into a cart mirror whose sink records them. (buildGraph wires it into its lsdj-sync node as a
-    // feedback wire: MI.OUT's one block, and the times master sync's stop is timed from.)
+    // completed it, here into a cart mirror whose sink records them. (buildGraph wires it forward into its LSDj's
+    // decoder, rp.lsdj.serial-out, which reads it in the same block: MI.OUT's bytes, and the frames master sync sends
+    // its clocks at and times its stop from.)
     Engine e(kRate);
     SameBoySystem* const gb = serialOutToCart(e);
     std::vector<std::pair<std::uint32_t, std::uint8_t>> carried;
diff --git a/packages/retroplug/cli/sdk-types.d.ts b/packages/retroplug/cli/sdk-types.d.ts
index 559abaab..51987479 100644
--- a/packages/retroplug/cli/sdk-types.d.ts
+++ b/packages/retroplug/cli/sdk-types.d.ts
@@ -429,8 +429,8 @@ export interface AudioDriver {
    *  lands when that block renders at least 1024 frames (a render's first block is the lesser of 1024 frames and
    *  the whole render). */
   stageMidiIn(bytes: Uint8Array | number[], frame?: number): boolean;
-  /** Drain the host MIDI out (an LSDj sync node's MI.OUT or master-sync decoder, a controller driving a real Game
-   *  Boy) since the last drain (each entry is one message). */
+  /** Drain the host MIDI out (LSDj's serial-out decoder in MI.OUT or master sync, a controller driving a real Game
+   *  Boy) since the last drain (each entry is one message, at its frame in the block it went out in). */
   drainMidiOut(): { system: number; frame: number; data: Uint8Array }[];
   /** Messages the engine's graph has dropped so far, a full queue on one of its wires each (cumulative, never
    *  reset): what a test holds to zero to know nothing it staged was lost on the way to a core. */
diff --git a/packages/retroplug/src/audioDriver.ts b/packages/retroplug/src/audioDriver.ts
index a8860ca6..87dc033d 100644
--- a/packages/retroplug/src/audioDriver.ts
+++ b/packages/retroplug/src/audioDriver.ts
@@ -160,10 +160,10 @@ export interface AudioDriver {
    *  no controller attached. Separate from stageMidiIn because a pad press is a NoteOn, and a
    *  translator like LSDj's MidiMap would read one on the musical stream as a row launch. */
   stageControllerIn(bytes: Uint8Array | number[]): boolean;
-  /** Drain the host MIDI out (e.g. an LSDj sync node's MI.OUT decoder, on its `midiOut` port),
-   *  accumulated across the renderAudio blocks since the last drain. Each entry is one MIDI message
-   *  with its source system + block-frame. The plugin drains this to the DAW directly; this is the
-   *  headless test path. */
+  /** Drain the host MIDI out (e.g. LSDj's serial-out decoder, rp.lsdj.serial-out, in MI.OUT or master
+   *  sync, on its `midiOut` port), accumulated across the renderAudio blocks since the last drain. Each
+   *  entry is one MIDI message with its source system + block-frame. The plugin drains this to the DAW
+   *  directly; this is the headless test path. */
   drainMidiOut(): { system: number; frame: number; data: Uint8Array }[];
   /** Render `blocks` as the plugin renders each host block (stage MIDI, drain edits, transport,
    *  processBlock at `outputs` lanes, take the MIDI out), for the parity suite. Throws with the reason
diff --git a/packages/retroplug/src/graph/defineNode.ts b/packages/retroplug/src/graph/defineNode.ts
index 3070d149..71a14e70 100644
--- a/packages/retroplug/src/graph/defineNode.ts
+++ b/packages/retroplug/src/graph/defineNode.ts
@@ -150,7 +150,8 @@ export function startValue(stream: ControlStream): number {
 //   counted; an emit onto a full stream overwrites its last change and returns false. A port the layout does not
 //   have, or not of the method's kind, throws a TypeError.
 // - A feedback input carries last block's messages at their frames in that block, so a frame can lie at or past
-//   this block's end. The master-sync decoder B3b ports reads one: a Game Boy's serialOut, each byte at its frame.
+//   this block's end. No production node reads one: LSDj's serial-out decoder, the one that reads a system's output,
+//   takes its Game Boy's serialOut over a forward wire, each byte at its frame in this block.
 // - Promise reactions and queueMicrotask callbacks run at the end of the block, after every node's process: at
 //   most 256 a block (the rest at the next block's end), under one call deadline between them. Every Block method
 //   throws inside one, as in a hook: a block exists only during its node's process call. A reaction's own throw
diff --git a/packages/retroplug/src/systemsStore.ts b/packages/retroplug/src/systemsStore.ts
index c380be87..65f875be 100644
--- a/packages/retroplug/src/systemsStore.ts
+++ b/packages/retroplug/src/systemsStore.ts
@@ -543,7 +543,7 @@ export class SystemsStore {
     this.entries = replaceById(this.entries, id, { ...e, roles, built });
     // A feature role stays pure TS, EXCEPT lsdj-sync's serial-out capture mode, set on any change to an lsdj-sync the
     // pipeline places: drive for MI.OUT, listen for master sync, none otherwise (the decoders themselves run in the
-    // system's lsdj-sync node, which the publish below hands the new mode).
+    // role's rp.lsdj.serial-out node, after the Game Boy, which the publish below hands the new mode).
     if (roleKind === "lsdj-sync") this.syncSerialOutCapture(id);
     // A role with a protocol node is placed with its config as that node's settings, so its edit publishes; an edit
     // the document does not carry (a model or palette, an asset override) leaves it as it was.
@@ -1029,8 +1029,8 @@ export class SystemsStore {
 // How a system with these roles captures its serial out: both LSDj-to-host MIDI-out modes need it. MidiOut
 // (SYNC=MI.OUT) drives, playing the Arduinoboy that clocks LSDj as the serial slave; MasterSync (SYNC=LSDJ) only
 // listens to the clock LSDj sends as the master. Null for no capture, which is also what an lsdj-sync the pipeline
-// leaves out gets, whatever its mode: what a capture delivers is decoded by the system's lsdj-sync node, which is not
-// running, and a driving core would clock the serial port under the bytes the placed role writes.
+// leaves out gets, whatever its mode: what a capture delivers is decoded by the role's rp.lsdj.serial-out node, which
+// is not placed, and a driving core would clock the serial port under the bytes the placed role writes.
 function serialOutCaptureFor(roles: RoleInstance[]): SerialOutCapture | null {
   const mode = (placedRole(roles, "lsdj-sync")?.config as { mode?: LsdjSyncMode } | undefined)?.mode;
   if (mode === LsdjSyncMode.MidiOut) return "drive";
diff --git a/packages/retroplug/test/systems/store-graph.test.ts b/packages/retroplug/test/systems/store-graph.test.ts
index ed4ffa81..498af5d7 100644
--- a/packages/retroplug/test/systems/store-graph.test.ts
+++ b/packages/retroplug/test/systems/store-graph.test.ts
@@ -753,9 +753,10 @@ test("an adopt whose core does not build logs nothing about the system it never
 });
 
 test("a left-out lsdj-sync arms no serial-out capture: not at a build, nor on an edit", () => {
-  // Capture is for the system's lsdj-sync node, which decodes what the core sends (MI.OUT drives, master sync listens).
-  // On an mGB cart whose project stored lsdj-sync, mgb wins serialIn and that node is not placed, so nothing would read
-  // the capture, and a driving core would clock the serial port under mgb's bytes.
+  // Capture is for the lsdj-sync role's decoder, rp.lsdj.serial-out, which decodes what the core sends (MI.OUT
+  // drives, master sync listens). On an mGB cart whose project stored lsdj-sync, mgb wins serialIn and the role's
+  // nodes are not placed, so nothing would read the capture, and a driving core would clock the serial port under
+  // mgb's bytes.
   const { be, sys } = project();
   be.seed("/roms/mgb.gb", lsdjRom("MGB"));
   const roles: RoleInstance[] = [
diff --git a/spec/01-architecture.md b/spec/01-architecture.md
index be53e655..54df2904 100644
--- a/spec/01-architecture.md
+++ b/spec/01-architecture.md
@@ -358,12 +358,12 @@ Each script type is a node type of the package `rp.script`, and each script node
 native shim the planner creates, activates and destroys like any node; its JS instance is made on the
 block thread when its plan is adopted, before that plan's first `process`, and released there at the
 start of a later block once the shim is destroyed (at once while no audio thread runs). The
-production bundle's seven are the MIDI routing, a protocol node for each role that has one (mGB, the N8
-FIFO, risa, smsggdj and LSDj sync) and the controller session: each takes its settings from the
-document, and its inputs and outputs are real wires from the host I/O nodes and to each system node's
-inputs. Native owns no role logic; in particular, **the drift-exact PPQ tick clock lives entirely in
-JS** (`walkTicks`). The nodes and the role model are specified in
-[04-roles-protocol-nodes.md](04-roles-protocol-nodes.md); the runtime under them in
+production bundle's eight are the MIDI routing, a protocol node for each role that has one (mGB, the N8
+FIFO, risa, smsggdj and LSDj sync), the controller session and LSDj's serial-out decoder: each takes its
+settings from the document, and its inputs and outputs are real wires from the host I/O nodes and to each
+system node's inputs, or, for the decoder, from its Game Boy's output. Native owns no role logic; in
+particular, **the drift-exact PPQ tick clock lives entirely in JS** (`walkTicks`). The nodes and the role
+model are specified in [04-roles-protocol-nodes.md](04-roles-protocol-nodes.md); the runtime under them in
 [02-native-host.md](02-native-host.md).
 
 The audio side runs on **both** threads over its lifetime (the control thread on the pull-path
@@ -373,17 +373,18 @@ different stack throws a spurious overflow. The control side does the same.
 
 `Engine::processBlock` runs the plan: `rp.host.midi-in` and `rp.host.controller-in` take what the
 hosts staged; the routing node fans the host MIDI out to the systems, the controller session reads the
-surface, and each system's protocol nodes read their routed MIDI (risa and smsggdj read only the transport; an LSDj's also reads its Game
-Boy's `serialOut` of last block over a feedback wire) and send that system node its inputs; the system nodes
-apply those inputs and run their cores; `rp.n8.cart-mirror` hands a physical N8 what the first NES was
-sent, and `rp.host.midi-out` / `controller-out` hand the host the script nodes' MIDI and control-surface
-output; the
-mixing nodes and `rp.host.audio-out` fill the host's buffers. Then
+surface, and each system's protocol nodes read their routed MIDI (risa and smsggdj read only the
+transport) and send that system node its inputs; the system nodes apply those inputs and run their cores;
+an LSDj's serial-out decoder reads what its Game Boy captured on its link port in that block and decodes
+it to host MIDI, each message at its byte's frame; `rp.n8.cart-mirror` hands a physical N8 what the first
+NES was sent, and `rp.host.midi-out` / `controller-out` hand the host the script nodes' MIDI (merged by
+frame) and control-surface output; the mixing nodes and `rp.host.audio-out` fill the host's buffers. Then
 `registry_.publishSystem(...)` runs for each system: the one place every driver funnels the block
 ([`Engine.cpp`](../packages/native/src/host/engine/Engine.cpp); the full order is in
 [02-native-host.md](02-native-host.md)). The host MIDI out (LSDj's MI.OUT and master sync, the
 controller's launches to a real Game Boy), through `rp.host.midi-out`, is drained after the block by the
-plugin (to the DAW) and the SDL standalone (to its MIDI out port).
+plugin (to the DAW, each message at its frame) and the SDL standalone (to its MIDI out port, at once,
+without the frame).
 
 ---
 
diff --git a/spec/02-native-host.md b/spec/02-native-host.md
index c725f066..4a0f1807 100644
--- a/spec/02-native-host.md
+++ b/spec/02-native-host.md
@@ -251,16 +251,15 @@ staging). Then, in order:
    `controllerOut_` cleared for the plan's output nodes to fill.
 3. `GraphHost::run(frames, transport, outputs, n)`: silence every host output, then run the plan, in
    the planner's order (topological, ties broken by document order):
-   1. **The host inputs and the script nodes.** `rp.host.midi-in` and `rp.host.controller-in` take what
-      the hosts staged (the host MIDI stable-sorted by frame). Then the script nodes, run by the
-      [script host](../packages/native/src/host/script/ScriptHost.hpp)
+   1. **The host inputs and the script nodes that feed the systems.** `rp.host.midi-in` and
+      `rp.host.controller-in` take what the hosts staged (the host MIDI stable-sorted by frame). Then the
+      script nodes, run by the [script host](../packages/native/src/host/script/ScriptHost.hpp)
       ([04-roles-protocol-nodes.md](04-roles-protocol-nodes.md)): the MIDI routing (`rp.midi.route`) fans
       the host MIDI out to the systems; the controller session reads the control surface; and each
       system's protocol nodes read what they need (mGB, N8 and LSDj their routed MIDI; risa and smsggdj only the
-      transport) at the **block-start** `ppq_` and, for
-      an LSDj, its Game Boy's `serialOut` of last block (a feedback wire), and send that system node's
-      `serialIn`, `midi`, `bytes` and `buttons` inputs, their host MIDI out to `rp.host.midi-out` and the
-      session's control-surface out to `rp.host.controller-out`.
+      transport) at the **block-start** `ppq_`, and send that system node's `serialIn`, `midi`, `bytes` and
+      `buttons` inputs, the session its host MIDI out to `rp.host.midi-out` and its control-surface out to
+      `rp.host.controller-out`.
    2. **The system nodes** ([SystemNodes.cpp](../packages/native/src/host/graph/SystemNodes.cpp):
       `rp.sameboy.gb`, `rp.mesen.nes`, `rp.mesen.gba`, `rp.mesen.sms`), each a `SystemNode<T>` over a
       core's `prepareForBlock` / `stepIfBelowTarget` / `finishBlock`. Each applies its inputs first
@@ -271,18 +270,23 @@ staging). Then, in order:
       adds into them) and sets the lane count from which of its outputs are connected
       ([10-multichannel-audio-out.md](10-multichannel-audio-out.md) section 1); a Game Boy then writes
       its `serialOut`.
-   3. **The cart mirror and the host outputs.** `rp.n8.cart-mirror`, wired from the first NES in slot order,
+   3. **The script nodes that read the systems.** An LSDj's serial-out decoder (`rp.lsdj.serial-out`)
+      reads its Game Boy's `serialOut` of this block, over a forward wire, and sends its host MIDI out to
+      `rp.host.midi-out`, each message at its byte's frame.
+   4. **The cart mirror and the host outputs.** `rp.n8.cart-mirror`, wired from the first NES in slot order,
       hands what that system was sent to `coreByteSink_` for a physical N8 (a message staged straight
       at it, `StageSystemMidi`, reaches the sink too, at drain); `rp.host.midi-out` fills
-      `midiOut_`, frame-sorted, which the plugin drains to the DAW after the block (the SDL standalone to its MIDI out
-      port); `rp.host.controller-out` appends
-      to `controllerOut_`, which the SDL standalone drains to the Launchpad link.
-   4. **The mixing nodes**, `rp.core.split` and `rp.core.add`: the routing, as wiring (below).
-   5. **`rp.host.audio-out`**, which copies its buses into the host's buffers (it reaches them
+      `midiOut_`, its inputs merged by frame, which the plugin drains to the DAW after the block, each
+      message at its frame (the SDL standalone to its MIDI out port at once, without the frame);
+      `rp.host.controller-out` appends to `controllerOut_`, which the SDL standalone drains to the Launchpad
+      link.
+   5. **The mixing nodes**, `rp.core.split` and `rp.core.add`: the routing, as wiring (below).
+   6. **`rp.host.audio-out`**, which copies its buses into the host's buffers (it reaches them
       through `rp_host.get_interface("rp.host.audio-buffers/1")`).
 4. `ScriptHost::endBlock()`: the promise jobs the block's script nodes queued, at most 256 (the rest
    wait for the next block's end), then, in a profile build, the allocation window's block counts; a
-   profile build closes the block's `apu-render` span just before ([08-profiling.md](08-profiling.md)).
+   profile build closes the block's `apu-render` span, the plan's whole run with the script nodes' spans
+   inside it, just before ([08-profiling.md](08-profiling.md)).
    What was staged and no node of the plan took (a plan with no `rp.host.midi-in` or `controller-in`)
    is dropped, not carried into the next block.
 5. `registry_.publishSystem(...)` for each system of the plan, in slot order: copy its
@@ -314,7 +318,7 @@ it:
 | Node | Ports | Does |
 |---|---|---|
 | `rp.host.midi-in` | out `midi` (events: 2048 messages, 64 KiB) | takes the staged host MIDI, stable-sorted by frame without allocating: the plugin stages two runs in frame order (a block's DAW events, then each moved automation slot's CC at frame 0), which it merges onto its queue, a tie to the earlier run; more runs are sorted in place first, by insertion |
-| `rp.host.midi-out` | in `in1` to `inN` (events), an input per entry of its settings `{"systems": [<id>, ...]}`, at most 128 | each input's messages land in `Engine::midiOut` tagged with that entry's system, input by input, each in queue order, with the list kept frame-sorted across inputs (each message at the later of its frame and the last handed out in the block); two entries may name one system, and an empty list is a node with no inputs, which sends nothing. A settings change that keeps the inputs is a swap, one that moves them a rebuild |
+| `rp.host.midi-out` | in `in1` to `inN` (events), an input per entry of its settings `{"systems": [<id>, ...]}`, at most 128 | each input's messages land in `Engine::midiOut` tagged with that entry's system, the inputs merged by frame: each step hands out the head with the earliest frame, the lower input on a tie, so with every frame 0 it is the inputs in turn, each in queue order; each message goes at the later of its frame and the last handed out in the block, a guard that keeps the list frame-sorted whatever an input's queue holds (a node on the C ABI writes its own). It allocates nothing, a head index per input, and costs messages times inputs. Two entries may name one system, and an empty list is a node with no inputs, which sends nothing. A settings change that keeps the inputs is a swap, one that moves them a rebuild |
 | `rp.host.controller-in` | out `midi` (events: 1024 messages, 16 KiB), out `connected` (control) | takes the staged control-surface traffic, all at frame 0; `connected` is 1 while a surface is attached, from frame 0 of the block (a change at frame 0 on the block the flag flips, since a control output's start is the last block's end) |
 | `rp.host.controller-out` | in `midi` | appends each message to `Engine::controllerOut` |
 | `rp.n8.cart-mirror` | in `midi` (events), in `bytes` | settings `{"system": <id>}`; hands the core-byte sink every MIDI message, then every byte message with its flush flag, in queue order, skipping empty ones; nothing while no sink is set |
@@ -352,11 +356,12 @@ completed past the block's end at its last frame (the core clears its log at its
 master), and `drive` also clocks a transfer waiting on an external clock, a bit an audio frame, taking a
 byte every 8 frames while the Game Boy holds the port for one, idle bytes included (the Arduinoboy's part
 in MI.OUT). Only MI.OUT drives: a stopped LSDj master waits on an external clock for another master, and
-clocked, it turns slave and ignores START. The port's capacity is
-fixed at 8256 messages and 8256 bytes, a byte every 8 frames over the largest block a host can run
-(65536 frames) with slack, so the feedback wire to its `rp.lsdj.sync` node keeps its front across every swap; a CGB
-clocking the port itself on its fast clock can outrun it in a very long block, and what does not fit is
-dropped and counted.
+clocked, it turns slave and ignores START. An LSDj's serial-out decoder, `rp.lsdj.serial-out`, placed
+after the systems, reads the port over a forward wire in the same block. The port's capacity is fixed at
+8256 messages and 8256 bytes, a byte every 8 frames over the largest block a host can run (65536 frames)
+with slack, rather than sized from the plan's block size, so a feedback wire from it would keep its front
+across every swap; a CGB clocking the port itself on its fast clock can outrun it in a very long block, and
+what does not fit is dropped and counted.
 
 An events or bytes output may declare its queue's capacity (`rp::bytes_out::sized<rp::capacity{...}>` in
 C++, `events.out(capacity)` / `bytes.out(capacity)` in TS), by default 256 messages and 4096 bytes, at
@@ -409,31 +414,33 @@ Structure arrives as a graph document TS builds
 The document `buildGraph` writes:
 
 - **Order.** `rp.host.midi-in`, `rp.host.controller-in`, then the script nodes: the MIDI routing (when the
-  model has a routing mode), the controller session (when one is enabled) and each system's protocol
-  nodes, in slot order and then role order; then the unlinked systems by slot, then the link groups in
-  order of first appearance (members by slot, one `link` wire chaining each member to the next), then
-  `rp.n8.cart-mirror` when there is a NES, `rp.host.midi-out` and `rp.host.controller-out`, then the
-  mixing nodes, then `rp.host.audio-out`. A protocol node runs before its system because the system reads
-  its outputs (a Game Boy's `serialOut` reaches its `rp.lsdj.sync` over a feedback wire, which orders
-  nothing), and the planner breaks ties by document order (`Planner.cpp` `order()`), so this is also the
-  order the plan runs in: every script node ahead of every system, which a profile build's `apu-render`
-  span relies on. Node ids are `midi-in`, `controller-in`, `routing`, `controller`, `cart`, `midi-out`,
-  `controller-out`, `out`, `sys-<id>`, a system's protocol nodes `sys-<id>/<role-kind>`, and names derived
-  from a system id (`split-<id>`, `stem-<id>-<k>`) or a host channel (`lane-<c>`), never from position, so
-  a system and its nodes keep their instances across edits. Link groups are Game Boys only, bucketed by
-  nonzero link-group id in slot order, a group of fewer than two dissolved.
+  model has a routing mode), the controller session (when one is enabled) and each system's protocol nodes,
+  in slot order and then role order; then the unlinked systems by slot, then the link groups in order of
+  first appearance (members by slot, one `link` wire chaining each member to the next), then the script
+  nodes that read a system, in slot order (an LSDj's `rp.lsdj.serial-out`, on a Game Boy), then
+  `rp.n8.cart-mirror` when there is a NES, `rp.host.midi-out` and `rp.host.controller-out`, then the mixing
+  nodes, then `rp.host.audio-out`. A protocol node runs before its system because the system reads its
+  outputs, and LSDj's decoder after it because it reads the system's `serialOut` in the same block: a script
+  node reads only nodes placed before it (or a feedback wire, which orders nothing, and which no document
+  `buildGraph` builds holds), and the planner breaks ties by document order (`Planner.cpp` `order()`), so
+  this is also the order the plan runs in. Node ids are `midi-in`, `controller-in`, `routing`, `controller`,
+  `cart`, `midi-out`, `controller-out`, `out`, `sys-<id>`, a system's protocol nodes `sys-<id>/<role-kind>`
+  (one placed after its system `sys-<id>/<role-kind>/out`), and names derived from a system id
+  (`split-<id>`, `stem-<id>-<k>`) or a host channel (`lane-<c>`), never from position, so a system and its
+  nodes keep their instances across edits. Link groups are Game Boys only, bucketed by nonzero link-group id
+  in slot order, a group of fewer than two dissolved.
 - **The script nodes** ([04-roles-protocol-nodes.md](04-roles-protocol-nodes.md)). `routing`
   (`rp.midi.route`, settings the routing mode and the system ids in slot order, which positional MIDI
   routing reads) takes `midi-in.midi` and has an output per system, `sys-<id>`, into each of that system's
   protocol nodes that reads MIDI. `controller` (`rp.controller.session`) takes `controller-in.midi` and
   `.connected`, its `controllerOut` to `controller-out`, its `launches` into the target system's
-  `rp.lsdj.sync` when its target is a system. Each protocol node's outputs go to its system node's inputs
-  of the same name; each Game Boy's `serialOut` goes back into its `rp.lsdj.sync` as a feedback wire (one
-  block of latency on the serial-out bytes MI.OUT and master sync decode); the first NES's protocol nodes'
-  `midi` and `bytes` go to the cart mirror as well; and `midi-out` takes the controller's `midiOut` and
-  then each `rp.lsdj.sync`'s, in slot order, each input under its system. Every script node reshapes, so a
-  structure change keeps each surviving instance (the routing node's ports follow the systems) and its
-  wires stay bound.
+  `rp.lsdj.sync` when its target is a system. Each protocol node's outputs go to its system node's inputs of
+  the same name; each LSDj's Game Boy's `serialOut` goes forward into its `rp.lsdj.serial-out`, which reads
+  the bytes MI.OUT and master sync decode in the block the core captured them; the first NES's protocol
+  nodes' `midi` and `bytes` go to the cart mirror as well; and `midi-out` takes the controller's `midiOut`
+  and then each `rp.lsdj.serial-out`'s, in slot order, each input under its system, in the order the kernel
+  emitted. Every script node reshapes, so a structure change keeps each surviving instance (the routing
+  node's ports follow the systems) and its wires stay bound.
 - **Routing as wiring.** For each host channel it lists the contributions in the order they sum:
   unlinked systems by slot, then the link groups by first appearance, L before R within a system. One
   contribution is wired straight to `audio-out`; several go through one mono `rp.core.add` in that order
diff --git a/spec/03-ts-layer.md b/spec/03-ts-layer.md
index 4c3def1d..0e4bc3b1 100644
--- a/spec/03-ts-layer.md
+++ b/spec/03-ts-layer.md
@@ -256,13 +256,15 @@ systems ([modular-graph-phase-b3b-design.md](../docs/modular-graph-phase-b3b-des
 - `controller` (`rp.controller.session`) when a controller is enabled, its target resolved and its app
   config already parsed by the publisher, fed from `controller-in`;
 - each system's protocol nodes (`sys-<id>/<role-kind>`, settings the role's parsed config), fed its routed
-  MIDI, each output wired to that system node's input of the same name, and each Game Boy's `serialOut`
-  back into its `rp.lsdj.sync` as a feedback wire, so MI.OUT reaches it a block after the core sent it;
-  the MI.MAP controller's cart has its `rp.lsdj.sync` forced to the mode the app needs;
+  MIDI, each output wired to that system node's input of the same name; the MI.MAP controller's cart has
+  its `lsdj-sync` nodes forced to the mode the app needs;
+- after the systems, each LSDj's serial-out decoder (`sys-<id>/lsdj-sync/out`, `rp.lsdj.serial-out`, on a
+  Game Boy, with its `rp.lsdj.sync`'s settings), fed that Game Boy's `serialOut` over a forward wire, so
+  MI.OUT and master sync reach it in the block the core captured them;
 - `cart` (`rp.n8.cart-mirror`, settings `{ system }`), fed what the first NES's protocol nodes send its
   `midi` and `bytes`, when there is a NES;
 - `midi-out` (`rp.host.midi-out`, settings `{ systems }`), its inputs the controller's `midiOut` and then
-  each `rp.lsdj.sync`'s in slot order, and `controller-out` (`rp.host.controller-out`), fed from the
+  each `rp.lsdj.serial-out`'s in slot order, and `controller-out` (`rp.host.controller-out`), fed from the
   controller's `controllerOut`.
 
 [`EngineGraph`](../packages/retroplug/src/engineGraph.ts) is one control-plane context's publisher:
diff --git a/spec/04-roles-protocol-nodes.md b/spec/04-roles-protocol-nodes.md
index 2b93fc39..f4dc4943 100644
--- a/spec/04-roles-protocol-nodes.md
+++ b/spec/04-roles-protocol-nodes.md
@@ -6,7 +6,8 @@ per-kind blob. Everything specific to a system (a backend's emulator knobs, or a
 feature like LSDj sync or mGB passthrough) is a role, so the cores stay free of backend/LSDj/mGB
 knowledge. Cores are built **bare** (the legacy build said so with a `setSniffDefaultRoles(false)` call; there is none now); every feature behaviour lives in
 TypeScript, and a role whose work is the audio thread's runs as a **protocol node**: a TS script node
-of its own in the engine's graph, which `buildGraph` places for each system that has the role.
+of its own in the engine's graph (two for `lsdj-sync`, one each way between the host and the cart),
+which `buildGraph` places for each system that has the role.
 
 This is the sharpest expression of the thesis (*"native owns bytes and cores; TypeScript owns
 meaning"*): the same node code runs over a `Block` stand-in in the unit tests and on the audio thread
@@ -174,7 +175,7 @@ for LSDj, the seed hook. What each does on the audio thread is its node's
 | `nes-n8-midi` | `rp.n8.midi-to-fifo` | none |
 | `risa-sync` | `rp.risa.sync` | none |
 | `sms-sync` | `rp.sms.sync` | `machine`: `"sms"` or `"gg"` |
-| `lsdj-sync` | `rp.lsdj.sync` | `mode` (an `LsdjSyncMode`, default `midiSync`), `tempoDivisor` 1-8, `autoStart` |
+| `lsdj-sync` | `rp.lsdj.sync`, and `rp.lsdj.serial-out` after its Game Boy | `mode` (an `LsdjSyncMode`, default `midiSync`), `tempoDivisor` 1-8, `autoStart` |
 
 The MIDI routing and the control-surface controller are **project settings**, not roles
 ([05-data-persistence.md](05-data-persistence.md)): `buildGraph` places a node for each from the
@@ -206,19 +207,25 @@ An edit to a role with no node (an asset override) changes no document. Then `ma
 systems `onChange` → the project's `onSystemsChange`
 ([projectStore.ts:215](../packages/retroplug/src/projectStore.ts#L215)) for the structure's
 observers. The core is never told, with one exception: an `lsdj-sync` mode change sets the Game Boy's
-serial-out capture (`setSerialOutCapture`), which the two MIDI-out modes decode: `drive` for MI.OUT, where
-the core plays the Arduinoboy that clocks LSDj, and `listen` for master sync, where LSDj clocks itself.
+serial-out capture (`setSerialOutCapture`), which `rp.lsdj.serial-out` decodes in the two MIDI-out modes:
+`drive` for MI.OUT, where the core plays the Arduinoboy that clocks LSDj, and `listen` for master sync, where
+LSDj clocks itself.
 
 ## The protocol nodes
 
-Seven TS node types ([src/script/nodes/](../packages/retroplug/src/script/nodes)), each a `defineNode`
+Eight TS node types ([src/script/nodes/](../packages/retroplug/src/script/nodes)), each a `defineNode`
 in the script-node bundle ([script/bundle.ts](../packages/retroplug/src/script/bundle.ts)): the MIDI
-routing, a protocol node for each role that has one, and the control-surface session. Each is its
-protocol's glue around pure functions that stay where they were (the Arduinoboy and master-sync
-decoders in `lsdjArduinoboy.ts`, `routeTarget` in `midiRouting.ts`, `risaLocate` / `risaArmPacket` in
-`risaSync.ts`, `syncLevelsFor` in `smsSync.ts`, `walkTicks` in `ticks.ts`, the LSDj keyboard map and
-link bytes), and each is held to the kernel it replaced by the recorded digests of
-[ProtocolNodes.test.cpp](../packages/native/test/engine-graph/ProtocolNodes.test.cpp).
+routing, a protocol node for each role that has one, the control-surface session, and LSDj's serial-out
+decoder, the `lsdj-sync` role's second node. Each is its protocol's glue around pure functions that stay
+where they were (the Arduinoboy and master-sync decoders in `lsdjArduinoboy.ts`, `routeTarget` in
+`midiRouting.ts`, `risaLocate` / `risaArmPacket` in `risaSync.ts`, `syncLevelsFor` in `smsSync.ts`,
+`walkTicks` in `ticks.ts`, the LSDj keyboard map and link bytes), and each is held to the kernel it
+replaced by the recorded digests of
+[ProtocolNodes.test.cpp](../packages/native/test/engine-graph/ProtocolNodes.test.cpp), but for the host
+MIDI out of MI.OUT and master sync, which `rp.lsdj.serial-out` sends at each byte's frame in the block the
+core captured it, where the kernel sent it at frame 0 of the block after: that differs by design
+([lsdj-serial-out-design.md](../docs/lsdj-serial-out-design.md)), and the cases carrying it hold the
+decoder's recorded digests.
 
 | Type | Placed for | Inputs | Outputs | Settings |
 |---|---|---|---|---|
@@ -227,17 +234,18 @@ link bytes), and each is held to the kernel it replaced by the recorded digests
 | `rp.n8.midi-to-fifo` | `nes-n8-midi` | `midiIn` | `midi` | none |
 | `rp.risa.sync` | `risa-sync` | none (the transport) | `bytes` | none |
 | `rp.sms.sync` | `sms-sync` | none (the transport) | `bytes` | `machine` |
-| `rp.lsdj.sync` | `lsdj-sync` | `midi`, `launches`, `serialOut` | `serialIn`, `buttons`, `midiOut` | `mode`, `tempoDivisor`, `autoStart` |
+| `rp.lsdj.sync` | `lsdj-sync` | `midi`, `launches` | `serialIn`, `buttons` | `mode`, `tempoDivisor`, `autoStart` |
+| `rp.lsdj.serial-out` | `lsdj-sync`, after its Game Boy | `serialOut` | `midiOut` | `rp.lsdj.sync`'s (it reads `mode`) |
 | `rp.controller.session` | the project's controller (`controller`) | `controllerIn` (events), `controllerConnected` (control) | `controllerOut`, `launches`, `midiOut` | `app`, `target`, `systemId` (resolved: 0 is none), `appConfig` (parsed), `songRowTicks`, `anchorRows`, `anchorSeq` |
 
-**How they are written.** Against the `Block` API, as the bench's pass-through is: indexed loops over
-the input `Message` views, `send` on the node's own ports, state and the tick counter on the instance
-(made in `create`), nothing allocated per block. Measured, the routing, mGB, N8 and LSDj MIDI-sync nodes
-allocate nothing in a warm block, JS or native ([08-profiling.md](08-profiling.md)); what still allocates is
-work that makes something: risa's arm packet on a start or a seek, the controller session's surface diff and
-launches, and the LSDj node's two MIDI-out decoders' emitted messages. Every type declares `reshapes`, so a
-settings change keeps the instance and its state, even one that changes its ports (the routing node's
-outputs follow the systems); a system removed and added again gets fresh nodes.
+**How they are written.** Against the `Block` API, as the bench's pass-through is: indexed loops over the
+input `Message` views, `send` on the node's own ports, state and the tick counter on the instance (made in
+`create`), nothing allocated per block. Measured, the routing, mGB, N8 and LSDj MIDI-sync nodes, and
+LSDj's serial-out decoder in master sync and MI.OUT, allocate nothing in a warm block, JS or native
+([08-profiling.md](08-profiling.md)); what still allocates is work that makes something: risa's arm packet
+on a start or a seek, and the controller session's surface diff and launches. Every type declares
+`reshapes`, so a settings change keeps the instance and its state, even one that changes its ports (the
+routing node's outputs follow the systems); a system removed and added again gets fresh nodes.
 
 **What each does per block:**
 
@@ -260,19 +268,31 @@ outputs follow the systems); a system removed and added again gets fresh nodes.
 - **`rp.sms.sync`** clocks smsggdj: while playing, a 2-bit counter advanced each 24-PPQN tick, each step a
   level word at the tick's frame that the system holds on the sync lines (controller port 2's TR + TH on a
   Master System, the EXT port on a Game Gear, by `machine`).
-- **`rp.lsdj.sync`** runs its `mode`, reading its routed MIDI and then the controller's launches as one
-  inbox, in that order: MidiSync (a 24-PPQN `0xF8` clock at each tick's frame, `tempoDivisor`-subdivided,
-  `autoStart` tapping START on the transport's rise), MidiSyncArduinoboy (notes 24/25 play and stop,
-  26-29 set the divisor, 30 and up a row byte, `0xFA`/`0xFC` on transport edges, its clocks at frame 0),
-  MidiMap (a NoteOn a row byte, the matching NoteOff `0xFE`, `0xFF` clocks at frame 0), KeyboardMidi (PS/2
-  scancodes, sliding the octave cursor), MidiPassthrough (every byte verbatim), MidiOut (ArduinoboyMaster:
-  last block's serial-out bytes decoded to host MIDI), and MasterSync (LSDj clocks itself: a `0xF8` a byte,
-  the first of a run after the song-row NoteOn and `0xFA`, and `0xFC` once 200 ms pass with no byte, timed
-  on the bytes' frames); Off and Keyboard send nothing. Its serial-in bytes go as runs of same-frame bytes,
-  a run a message; a button press as `[button, down]` on `buttons` at frame 0; its host MIDI at frame 0. Its
-  scratch is one bag kept across mode changes, master sync's sub-object dropped outside that mode so coming
-  back starts clean. `serialOut` is a feedback wire from its Game Boy: last block's captured bytes, each at
-  the frame its core completed it.
+- **`rp.lsdj.sync`**, the `lsdj-sync` role's host-to-cart direction, runs its `mode`, reading its routed
+  MIDI and then the controller's launches as one inbox, in that order: MidiSync (a 24-PPQN `0xF8` clock at
+  each tick's frame, `tempoDivisor`-subdivided, `autoStart` tapping START on the transport's rise),
+  MidiSyncArduinoboy (notes 24/25 play and stop, 26-29 set the divisor, 30 and up a row byte, `0xFA`/`0xFC`
+  on transport edges, its clocks at frame 0), MidiMap (a NoteOn a row byte, the matching NoteOff `0xFE`,
+  `0xFF` clocks at frame 0), KeyboardMidi (PS/2 scancodes, sliding the octave cursor) and MidiPassthrough
+  (every byte verbatim); Off, Keyboard, MidiOut and MasterSync send nothing here. Its serial-in bytes go as
+  runs of same-frame bytes, a run a message; a button press as `[button, down]` on `buttons` at frame 0. Its
+  scratch is one bag kept across mode changes.
+- **`rp.lsdj.serial-out`**, the role's cart-to-host direction, placed after the systems, reads its Game
+  Boy's `serialOut` over a forward wire, in the block the core captured it, each byte at the frame its core
+  completed it, and decodes it in two modes: MidiOut (ArduinoboyMaster MI.OUT: the flag-gated bit framing,
+  then the byte protocol, each message at the frame of the captured byte whose bits complete it, so a
+  command straddling two blocks goes at its completing byte's frame in the later one) and MasterSync (LSDj
+  clocks itself: a `0xF8` at each byte's frame, the first of a run after the song-row NoteOn and `0xFA` at
+  the same frame, and one `0xFC` at the run's mark, the first frame its silence passes 200 ms at the block's
+  rate, `lastAt + stop + 1`, sent before the byte at or past the mark or at the end of the block the mark
+  falls in, a mark at or past the block's end carried to the next block). In every other mode it reads
+  nothing and sends nothing, and the core captures nothing to read. Each message goes on `midiOut` at that
+  frame, from one of three arrays made with the instance, one a message length. MI.OUT's bit accumulator and
+  pending command are kept on the instance across mode changes; master sync's state is dropped outside that
+  mode, so coming back opens a fresh run. Its settings are the role's config through the schema
+  `rp.lsdj.sync` takes too
+  ([nodes/lsdjSyncSettings.ts](../packages/lsdj/ts/lsdjSyncSettings.ts)); it reads only
+  `mode`.
 - **`rp.controller.session`** runs a `ControllerSession` ([controller/session.ts](../packages/retroplug/src/controller/session.ts))
   for the app its settings name (`controller/apps/index.ts`, the apps' runtime, zod-free): the surface's
   traffic and the block's clock in (its tick derived from ppq at 24 PPQN), its LED bytes out on
@@ -328,8 +348,7 @@ build gate), fails on a bundle that pulls a zod module, or a role or config modu
 ## Store → graph
 
 [`buildGraph`](../packages/retroplug/src/graph/buildGraph.ts) places, in document order and ahead of the
-systems (the planner breaks ties by document order, so they run first, and a profile build's `apu-render`
-span relies on it):
+systems (the planner breaks ties by document order, so they run before them):
 
 1. `routing`, the `rp.midi.route` node, when the model has a MIDI routing mode (a project always has one;
    a `DirectGraph`, or a `SystemsStore` used without a project, routes none until a mode is set), fed from
@@ -338,17 +357,29 @@ span relies on it):
    `controller-in.midi` and `.connected`, its `controllerOut` to `controller-out`;
 3. each system's protocol nodes, in slot order and then the order of its roles, each `sys-<id>/<kind>`
    with the role's parsed config as its settings: what reads MIDI is fed from `routing.sys-<id>`, each
-   output wired to the system node's input of the same name, a Game Boy's `serialOut` back into its
-   `rp.lsdj.sync` as a feedback wire (one block late: what MI.OUT and master sync decode), and the
-   controller's `launches` into the target system's `rp.lsdj.sync` when the target is `"system"`.
+   output wired to the system node's input of the same name, and the controller's `launches` into the
+   target system's `rp.lsdj.sync` when the target is `"system"`.
+
+After the systems and their link wires, and before the cart mirror and the host outputs, it places the
+node a role's type names to run after its system (its `out`), in slot order: `rp.lsdj.serial-out` for an
+`lsdj-sync` on a Game Boy, `sys-<id>/lsdj-sync/out`, with the settings its `rp.lsdj.sync` got (the MI.MAP
+controller's forced mode included), fed `sys-<id>.serialOut` over a forward wire, so it reads what the core
+captured in the same block. It is placed whatever the mode, so a mode switch is a change of settings and
+never of the document, and only on a Game Boy, the one system with a serial out (a hand-edited `lsdj-sync`
+on another system places its `rp.lsdj.sync` alone, and host MIDI out takes no input for it). The rule under
+both: a script node reads only nodes placed before it in the document, or a feedback wire, which orders
+nothing, so the plan runs the script nodes in document order. No document `buildGraph` builds holds a
+feedback wire (`test/graph/build-graph.test.ts` checks it), and
+`test/graph/build-graph-role-goldens.test.ts` holds every wire of its role goldens to run forward.
 
 Which role kind is placed as which node, and how each type is wired, is one pure, zod-free table,
 [graph/protocolNodes.ts](../packages/retroplug/src/graph/protocolNodes.ts) (`protocolNodeOf`,
-`protocolPipeline`): both stores and a `DirectGraph` read it, with or without a role registry, and a role
-with no node is left out, so an edit to it (a model, a palette, an asset override) leaves the document as
-it was. `test/system-settings/registry.test.ts` holds it to what `registerDspRoles` registers, and
-`test/graph/protocol-nodes.test.ts` to the ports each node module declares and to the bundle's node types;
-`golden/build-graph-roles.json` records the documents it makes `buildGraph` build.
+`protocolPipeline`, and `outNodeOf` for a type's `out`): both stores and a `DirectGraph` read it, with or
+without a role registry, and a role with no node is left out, so an edit to it (a model, a palette, an
+asset override) leaves the document as it was. `test/system-settings/registry.test.ts` holds it to what
+`registerDspRoles` registers, and `test/graph/protocol-nodes.test.ts` to the ports each node module
+declares and to the bundle's node types; `golden/build-graph-roles.json` records the documents it makes
+`buildGraph` build.
 
 **One writer per system input.** Two roles whose nodes write the same system input (a project stored with `mgb`
 whose ROM path now holds LSDj gains `lsdj-sync`, both writing `serialIn`; or a hand-edited file naming a kind twice)
@@ -369,10 +400,11 @@ not read, as an edit already did, and names it in the log.
 The rest of the document follows from those nodes:
 
 - **Host MIDI out**: `rp.host.midi-out` in its systems form, its inputs in the order the kernel emitted:
-  the controller's `midiOut` first (under its resolved target), then each `rp.lsdj.sync`'s in slot order,
-  each under its system; with no producer, an empty list and no inputs. The node keeps the host's list
-  frame-sorted across its inputs (each message at the later of its frame and the last handed out in the block, as the
-  kernel's one queue clamped), which `Engine::midiOut` promises and CLAP and LV2 require.
+  the controller's `midiOut` first (under its resolved target), then each `rp.lsdj.serial-out`'s in slot
+  order, each under its system; with no producer, an empty list and no inputs. The node merges its inputs
+  by frame, a tie to the lower input, so two LSDjs in master sync each keep their clocks' times, and sends
+  each message at the later of its frame and the last it handed out in the block, a guard that keeps the
+  host's list frame-sorted whatever an input holds, which `Engine::midiOut` promises and CLAP and LV2 require.
 - **The N8 cart mirror** (`cart`, `rp.n8.cart-mirror`) for the first NES in slot order, fed what that
   system's protocol nodes send its `midi` and `bytes` inputs.
 - **The controller** ([graph/controller.ts](../packages/retroplug/src/graph/controller.ts)): its target
@@ -380,9 +412,10 @@ The rest of the document follows from those nodes:
   with no system its host MIDI is tagged `NO_SYSTEM` (4294967295) and the node takes 0, its none. The
   publisher hands it the app config already parsed by the app's own schema (`parseAppConfig`,
   [controller/registry.ts](../packages/retroplug/src/controller/registry.ts)) and the timing table it read
-  from the cart's battery, on the control plane. The `rp.lsdj.sync` node of the cart the MI.MAP app drives
-  is forced to `midiMap`, or to `off` when the cart's own SYNC setting is not MI.MAP
-  (`controllerSyncOverride`): that node's settings alone, so nothing reaches the `.rplg`.
+  from the cart's battery, on the control plane. The `lsdj-sync` nodes of the cart the MI.MAP app drives
+  are forced to `midiMap`, or to `off` when the cart's own SYNC setting is not MI.MAP
+  (`controllerSyncOverride`), its `rp.lsdj.serial-out` with its `rp.lsdj.sync`, so a forced cart sends no
+  host MIDI. The override is those nodes' settings alone, so nothing reaches the `.rplg`.
 
 Where nothing produces for a system input, or no controller is enabled, the input or the host I/O node is
 left unwired. The plugin loads nothing for any of this
@@ -394,14 +427,12 @@ publishes carries every node and its settings.
 Only the pieces below are open; the [07-remaining-work.md](07-remaining-work.md) doc is the authoritative
 inventory of remaining work.
 
-- **Raw Keyboard mode is the one unbuilt LSDj sync mode.** `rp.lsdj.sync` runs every other mode:
-  MidiSync, MidiSyncArduinoboy, MidiMap, KeyboardMidi, MidiPassthrough, and the two host-out directions
-  **MidiOut** (ArduinoboyMaster MI.OUT, serial out decoded to host MIDI) and **MasterSync**, with a
-  live-apply menu. Raw **Keyboard** (mode 4) still sends nothing: it needs a per-block `keys` feed, an
-  input on the node that nothing in the running host would feed yet, and per-block host `buttons` the same.
-- **The MI.OUT and master-sync decoders allocate.** Their emitted arrays cost about 3 JS allocations for
-  every host MIDI message they decode, the one per-block JS allocation left on a production path; it is
-  the decoders' own change, in `lsdjArduinoboy.ts`.
+- **Raw Keyboard mode is the one unbuilt LSDj sync mode.** The `lsdj-sync` role runs every other mode, with
+  a live-apply menu: `rp.lsdj.sync` MidiSync, MidiSyncArduinoboy, MidiMap, KeyboardMidi and MidiPassthrough,
+  and `rp.lsdj.serial-out` the two host-out directions, **MidiOut** (ArduinoboyMaster MI.OUT, serial out
+  decoded to host MIDI) and **MasterSync**. Raw **Keyboard** (mode 4) still sends nothing: it needs a
+  per-block `keys` feed, an input on `rp.lsdj.sync` that nothing in the running host would feed yet, and
+  per-block host `buttons` the same.
 - **kit-patch UI-thread behaviour.** LSDj kit-patch runs entirely on the UI / control thread: it
   compiles the kit (over the native `KitCompiler` primitives) and the `lsdj-assets` role folds it into the
   ROM in memory. It has **no protocol node** and never reaches the audio thread. A role-owned settings UI
@@ -420,8 +451,9 @@ TypeScript ([packages/retroplug/src/](../packages/retroplug/src/)):
 - [dspRoles.ts](../packages/retroplug/src/dspRoles.ts): the roles that have a protocol node (kind, schema)
   + the LSDj `onConstruct` seed.
 - [romProviders.ts](../packages/retroplug/src/romProviders.ts): feature-role attachment by ROM identity.
-- [script/nodes/](../packages/retroplug/src/script/nodes): the seven node types, `clock.ts` (the tick
-  counter) and `ports.ts` (the capacities); [ticks.ts](../packages/retroplug/src/ticks.ts): `walkTicks`.
+- [script/nodes/](../packages/retroplug/src/script/nodes): the eight node types, `lsdjSyncSettings.ts` (the
+  `lsdj-sync` schema both its nodes take), `clock.ts` (the tick counter) and `ports.ts` (the capacities);
+  [ticks.ts](../packages/retroplug/src/ticks.ts): `walkTicks`.
 - [graph/protocolNodes.ts](../packages/retroplug/src/graph/protocolNodes.ts) /
   [graph/controller.ts](../packages/retroplug/src/graph/controller.ts) /
   [graph/buildGraph.ts](../packages/retroplug/src/graph/buildGraph.ts): store → graph, the protocol node per
diff --git a/spec/06-build-test.md b/spec/06-build-test.md
index 3091e763..d2afcbb8 100644
--- a/spec/06-build-test.md
+++ b/spec/06-build-test.md
@@ -193,7 +193,12 @@ The suite is 30 files: a file per group, and four with no baseline of their own
   second); MI.OUT's decoded notes depend on the host sample rate. Details in design section 2.5
   ([modular-graph-phase-b0-design.md](../docs/modular-graph-phase-b0-design.md)). Two more were pinned and
   fixed together: LSDj master sync silent at 48 kHz, and its decoder reading a stopped LSDj as clocks in small
-  blocks ([lsdj-master-sync-fix-design.md](../docs/lsdj-master-sync-fix-design.md)).
+  blocks ([lsdj-master-sync-fix-design.md](../docs/lsdj-master-sync-fix-design.md)). The host MIDI out of the
+  MI.OUT and master-sync cases (`lsdj-modes`' `midiOut` and two `masterSync`, all six of
+  `blocks-tiny-lsdj-master`, `blocks-tiny-lsdj`'s three `midiOut-*`) was re-recorded when their decoder moved
+  after the Game Boy ([lsdj-serial-out-design.md](../docs/lsdj-serial-out-design.md) section 7): the same
+  messages in the same order, each at its byte's frame in the block the core captured it, every lane, SRAM and
+  work RAM as they were.
 - LSDj, risa, nanoloop and the controller cases need the sibling fixtures (`RETROPLUG_RESOURCES_DIR`), so
   those groups skip in CI, counted in the skip baseline. CI therefore runs only the 152 fixture-free cases
   (every MIDI-out, controller-in, LSDj, risa and GBA case is fixture-only, LSDj's link-cable cases among
@@ -213,15 +218,27 @@ Each pnpm script first builds its runner via `cmake-build.js` (mock → `tjs-cli
 invocation is self-contained. The mock tier is decoupled from the C++ plugin build entirely — it
 needs only the `tjs` binary and esbuild, which is what lets the whole application layer (project /
 systems / paths / recent / config / SRAM / kits) be tested without an emulator in the loop.
-The script nodes are tested there too, each over a `Block` stand-in (`test/script/nodes/`, a file per
-type, where the role tests that drove the DSP kernel moved when it was deleted), with the zod-free
-settings schema held to zod's own output (`test/graph/settings-schema.test.ts`) and the script-node bundle
-held free of zod (`test/script/bundle-no-zod.test.ts`, which builds it with the build's own options in a
-`node` child (the runner's `RP_NODE`) and reads esbuild's metafile, with a 200 KB bound on the bundle). The protocol
-nodes' port names are held to their modules by `test/graph/protocol-nodes.test.ts`, and `buildGraph`'s documents with
-roles to `golden/build-graph-roles.json` by `test/graph/build-graph-role-goldens.test.ts`, which `RP_GOLDEN_UPDATE=1`
+The script nodes are tested there too, each over a `Block` stand-in (`test/script/nodes/`, a file per type, where
+the role tests that drove the DSP kernel moved when it was deleted; `lsdj-serial-out.test.ts` holds MI.OUT's and
+master sync's messages each at its frame in its own block), and so are the pure functions they run (`test/dsp/`,
+where `lsdj-arduinoboy.test.ts` feeds the MI.OUT and master-sync decoders one byte stream cut into 1-frame, FL-style
+ragged, FL's split 256-frame, longer ragged and 1024-frame blocks and holds every cut to the same messages at the
+same absolute frames, so the decoders add no dependence on the split), with the zod-free settings schema held to
+zod's own output (`test/graph/settings-schema.test.ts`) and the script-node bundle held free of zod
+(`test/script/bundle-no-zod.test.ts`, which builds it with the build's own options in a `node` child (the runner's
+`RP_NODE`) and reads esbuild's metafile, with a 200 KB bound on the bundle). The protocol nodes' port names are held
+to their modules by `test/graph/protocol-nodes.test.ts`, and `buildGraph`'s documents with roles to
+`golden/build-graph-roles.json` by `test/graph/build-graph-role-goldens.test.ts`, which `RP_GOLDEN_UPDATE=1`
 re-records.
 
+In the real-host tier, beside parity, a few files hold timing to tolerances the baseline's bits cannot state:
+`lsdj-mastersync` holds master sync to one story in 1-frame, FL-style ragged and 1024-frame blocks at 44.1 and
+48 kHz, and its clocks to LSDj's tempo in absolute frames in every split (consecutive clocks of a run within 3%
+of a tempo period, a run's first gap, which LSDj sends early, within 30%), which a decoder sending at a block's
+frame 0 fails in the ragged and 1024-frame splits; `lsdj-midiout` holds MI.OUT's messages to frames inside their
+blocks, most past frame 0. `dsp-bench`, in a profile host only, holds an mGB window and a playing LSDj's
+master-sync and MI.OUT windows to zero allocations a block ([08-profiling.md](08-profiling.md)).
+
 The native and UI runners each hand every test a fresh temp dir as `RETROPLUG_USER_CONFIG_DIR`
 (isolated real disk). The native runner also injects the resources dirs so tests can locate ROMs by
 absolute path; the script nodes need no injection, since every Engine embeds their bundle. The UI runner aliases `import ... from "ui-harness"` to the build's
@@ -236,7 +253,7 @@ Root [package.json](../package.json). Each builds its CMake target(s) first, the
 | `test` | `tjs-cli` | Mock-backend TS suite. |
 | `test:native` | `retroplug-host` | Real-host suite. |
 | `test:ui` | `retroplug-ui-test` | LVGL React UI suite. |
-| `test:plugin` | ten Catch2 binaries (`-plugin-test`, `-dynparams-test`, `-classid-test`, `-audio-test`, `-watcher-test`, `-midi-test`, `-launchpad-test`, `-lottie-test`, `-graph-test`, `-engine-graph-test`) | Pure-C++ unit checks (no TS runner; exit code is pass/fail): the per-context routing behind PluginUI's `__rp_*` window hooks ([ContextTargets.hpp](../packages/native/plugin/ContextTargets.hpp), proven on two live `JSContext`s so concurrent instances never cross-route), DPF's dynamic-parameter diff classifier, the class-id counter sync that keeps the DAW-hosted editor from rendering blank, the per-channel audio split, the efsw file watcher, MIDI in/out, the Launchpad scanner, the Lottie decoder, the node-graph engine's foundations (`retroplug-graph-test`), and the engine on that graph (`retroplug-engine-graph-test`): each system node rendering what its core renders driven directly, bit for bit, its inputs included; the engine placing, re-arming and freeing what the documents TS builds say; the host I/O nodes and `rp.n8.cart-mirror` moving what the hosts stage and drain (the staging sorted and taken with no allocation); TS nodes behind the C ABI in the script host (marshalling, the JS-owned audio copies, promise jobs, reshape, creation at adoption and release at reclaim, a throw's count (a failed write onto what a node holds among them, while one node's prototype setters charge no other, its prototype methods are adopted by none and it replaces no method of the frozen `Block` prototype), the CPU-time call deadline, an audio thread running them while the control thread replans; a profile build adds five `[profile]` cases, which `tools/run-profile.sh unit` runs); the protocol script nodes, in the documents `buildGraph` builds (read from its role goldens, the systems stood in for by probes, the script nodes run in `buildGraph`'s plan order), held to the digests of what the DSP kernel they replaced was recorded sending, block by block ([ProtocolNodes.test.cpp](../packages/native/test/engine-graph/ProtocolNodes.test.cpp): every routing, every LSDj sync mode, risa and smsggdj sync, a controller session, state across structure changes, a throw stopping only its own node, and a block that fills `rp.host.midi-in` reaching an mGB and a NES whole; and, checked directly, a controller's session kept through a system added behind it, and every structure's script nodes running in `buildGraph`'s order), a Game Boy's serial input hashed byte by byte so how a node groups same-frame bytes moves no recorded value; `rp.host.midi-out`'s systems form, input by input under each input's system, keeping the host's MIDI out frame-sorted (`HostIo.test.cpp`'s raised-frame case); and every document in `buildGraph`'s goldens planned on the engine with real cores and run for a block with no node faulting (`Engine::faultCount`), no script node throwing and no message dropped, the 33-system `perSystem` ones refused for having more buses than `rp.host.audio-out` takes; the role goldens (`golden/build-graph-roles.json`, `buildGraph`'s own record of documents with every protocol node type, a controller each way and every MIDI routing) run twenty blocks each, the transport playing, host MIDI and a control surface's press staged, and a controller must send its surface something and, driving a real Game Boy, the host its launch. A hidden bench in it, `ScriptBench`, times a script node per block, and the protocol nodes in their plan (an LSDj in MIDI sync, and one decoding MI.OUT), at 1, 64 and 512 frames (`build/bin/retroplug-engine-graph-test "[bench]"`). |
+| `test:plugin` | ten Catch2 binaries (`-plugin-test`, `-dynparams-test`, `-classid-test`, `-audio-test`, `-watcher-test`, `-midi-test`, `-launchpad-test`, `-lottie-test`, `-graph-test`, `-engine-graph-test`) | Pure-C++ unit checks (no TS runner; exit code is pass/fail): the per-context routing behind PluginUI's `__rp_*` window hooks ([ContextTargets.hpp](../packages/native/plugin/ContextTargets.hpp), proven on two live `JSContext`s so concurrent instances never cross-route), DPF's dynamic-parameter diff classifier, the class-id counter sync that keeps the DAW-hosted editor from rendering blank, the per-channel audio split, the efsw file watcher, MIDI in/out, the Launchpad scanner, the Lottie decoder, the node-graph engine's foundations (`retroplug-graph-test`), and the engine on that graph (`retroplug-engine-graph-test`): each system node rendering what its core renders driven directly, bit for bit, its inputs included; the engine placing, re-arming and freeing what the documents TS builds say; the host I/O nodes and `rp.n8.cart-mirror` moving what the hosts stage and drain (the staging sorted and taken with no allocation); TS nodes behind the C ABI in the script host (marshalling, the JS-owned audio copies, promise jobs, reshape, creation at adoption and release at reclaim, a throw's count (a failed write onto what a node holds among them, while one node's prototype setters charge no other, its prototype methods are adopted by none and it replaces no method of the frozen `Block` prototype), the CPU-time call deadline, an audio thread running them while the control thread replans; a profile build adds six `[profile]` cases, which `tools/run-profile.sh unit` runs); the protocol script nodes, in the documents `buildGraph` builds (read from its role goldens, the systems stood in for by probes, the script nodes run in `buildGraph`'s plan order), held to the digests of what the DSP kernel they replaced was recorded sending, block by block, but for the host MIDI out of MI.OUT and master sync, which `rp.lsdj.serial-out` sends at each byte's frame by design, three cases holding that decoder's recorded digests ([ProtocolNodes.test.cpp](../packages/native/test/engine-graph/ProtocolNodes.test.cpp): every routing, every LSDj sync mode, risa and smsggdj sync, a controller session, state across structure changes, a throw stopping only its own node, and a block that fills `rp.host.midi-in` reaching an mGB and a NES whole; and, checked directly, a controller's session kept through a system added behind it, and every structure's script nodes running in `buildGraph`'s order), a Game Boy's serial input hashed byte by byte so how a node groups same-frame bytes moves no recorded value; `rp.host.midi-out`'s systems form, each message under its input's system, its inputs merged by frame with ties in input order, and nothing handed out earlier than a frame already handed out whatever an input's queue holds (`HostIo.test.cpp`, the guard through a test node that writes its own queue out of order); and every document in `buildGraph`'s goldens planned on the engine with real cores and run for a block with no node faulting (`Engine::faultCount`), no script node throwing and no message dropped, the 33-system `perSystem` ones refused for having more buses than `rp.host.audio-out` takes; the role goldens (`golden/build-graph-roles.json`, `buildGraph`'s own record of documents with every protocol node type, a controller each way and every MIDI routing) run twenty blocks each, the transport playing, host MIDI and a control surface's press staged, and a controller must send its surface something and, driving a real Game Boy, the host its launch. A hidden bench in it, `ScriptBench`, times a script node per block, and the protocol nodes in their plan (an LSDj in MIDI sync, and one decoding MI.OUT, its decoder after the systems), at 1, 64 and 512 frames (`build/bin/retroplug-engine-graph-test "[bench]"`). |
 | `screenshot` | `retroplug-jack` | Boots the standalone headlessly → `/tmp/retroplug.png`. |
 | `sdl:smoke` | `retroplug-sdl` | Headless SDL-standalone boot + shutdown; needs no display server or audio server. In CI. |
 | `sdl:pipewire` | `retroplug-sdl` | Stands up a private PipeWire server and asserts WHICH output device the PortAudio backend opens. Not in CI (the runners have no PipeWire). |
diff --git a/spec/07-remaining-work.md b/spec/07-remaining-work.md
index 9385c148..3a7f48b6 100644
--- a/spec/07-remaining-work.md
+++ b/spec/07-remaining-work.md
@@ -125,16 +125,12 @@ a mode, `listen` for master sync and `drive` for MI.OUT, and the decoder sends a
 ([modular-graph-phase-b3b-design.md](../docs/modular-graph-phase-b3b-design.md)) split the kernel into one
 protocol script node per role beside the MIDI routing and the controller session, moved `kernelProjection`
 into `buildGraph`, took zod out of the script-node bundle (78 KB, from 660), gave `rp.host.midi-out` each
-input's system from its settings, and deleted the kernel. What is left, in order:
-
-- **Master-sync clocks at their byte's frame**, its own change, with its own re-record: master sync sends each
-  clock at frame 0 of the block after its byte, so in a large block its clocks bunch where they could keep
-  their spacing, one block late. Each LSDj node has its own `midiOut` port now, so its queue no longer orders
-  another's; but this would be the first producer of host MIDI past frame 0. `rp.host.midi-out` already keeps the
-  host's list frame-sorted across its inputs (each message goes out at the later of its frame and the last
-  one handed out in the block), so a host-side sort is no longer the alternative. But the clamp raises a later
-  input's earlier frames to an earlier input's, so a second LSDj in master sync would have its clocks bunched behind the first's: that change
-  has to turn the clamp into a merge by frame, or its re-record would bake the bunching into the baseline.
+input's system from its settings, and deleted the kernel. Then LSDj's serial out got a node of its own
+([lsdj-serial-out-design.md](../docs/lsdj-serial-out-design.md)): `rp.lsdj.serial-out`, after the Game Boy,
+decodes MI.OUT and master sync in the block the core captured the bytes and sends each message at its byte's
+frame, `rp.host.midi-out` merges its inputs by frame, the decoders allocate nothing, and the profile's
+`apu-render` spans the plan. What is left, in order:
+
 - **The capture mode as a Game Boy node setting** (Phase C, with the other live knobs): carried in the
   published document rather than through `ConfigField::SerialOutCapture` plus a publish, it would make a mode
   switch atomic and retire the master-sync design's first accepted limit (its section 10).
@@ -149,15 +145,19 @@ input's system from its settings, and deleted the kernel. What is left, in order
   (2 s of CPU) every block and is never bypassed, as a `process` that hangs would be. A context per node
   in the same runtime (its own intrinsics and prototypes) closes all of it, at a per-context memory cost,
   and names the node a job ran in (`JS_ExecutePendingJob` reports the job's context); decide it before
-  nodes users write (Phase C, the Reaktor-scale aim). B3b made the project's nodes many, not one: seven is the
-  number of node types, and a project places the routing node, a controller when enabled, and one node per protocol
-  role.
+  nodes users write (Phase C, the Reaktor-scale aim). B3b made the project's nodes many, not one: eight is the
+  number of node types, and a project places the routing node, a controller when enabled, one node per protocol
+  role, and a second for each LSDj on a Game Boy, its serial-out decoder.
+- **Left by LSDj's serial-out node, as they are** (its design's section 11):
+  - **The core's serial byte timing moves with the block split.** The frame SameBoy completes a captured byte at
+    moves by tens of frames from one split to another (up to 32 in `lsdj-mastersync`, where the whole stream shifts
+    and its clock gaps are the same in every split), which predates the decoder's move. The decoder adds no split
+    dependence of its own, so the tests hold each message to its byte, not to one absolute time shared by every
+    split. A SameBoy matter, its own change if wanted.
+  - **The SDL standalone sends host MIDI at block time.** Its RtMidi out takes no timestamp, so `audioCb` sends
+    each block's `Engine::midiOut` at once as the block is rendered, its frames dropped, where the plugin hands the
+    DAW each message at its frame (`writeMidiEvent`). Scheduling it is the standalone's own work.
 - **Found while building B3b, left as they are:**
-  - The MI.OUT and master-sync decoders (`arduinoboyDecodeSerialOut`, `arduinoboyMasterSyncBlock` in
-    [lsdjArduinoboy.ts](../packages/lsdj/ts/lsdjArduinoboy.ts)) build each message they emit as a new
-    array, about 3 JS allocations for every host MIDI message they decode: the one per-block JS allocation
-    left on a production path (every other protocol node allocates nothing in a warm block,
-    [08-profiling.md](08-profiling.md)). Emitting into a reused buffer is the decoders' own change.
   - `defineNode` keeps to ES2015's library (`Set`, `Object.entries`, `includes`), so the script-node bundle
     holds no zod but is not yet MicroQuickJS-ready; `settingsSchema.ts` already keeps to ES5's.
   - `routeBlockInto` in [midiRouting.ts](../packages/retroplug/src/midiRouting.ts) has no caller since B3b:
@@ -180,14 +180,11 @@ input's system from its settings, and deleted the kernel. What is left, in order
     ([graph/protocolNodes.ts](../packages/retroplug/src/graph/protocolNodes.ts)) in those readers. Not fixed.
   - **The controller's MI.MAP override changes only the document.** `controllerSyncOverride`
     ([graph/controller.ts](../packages/retroplug/src/graph/controller.ts), applied in `buildGraph`) runs an LSDj's
-    node in `midiMap` for a cart reading MidiMap, but `serialOutCaptureFor` (`systemsStore.ts`) and
+    nodes in `midiMap` for a cart reading MidiMap, but `serialOutCaptureFor` (`systemsStore.ts`) and
     `__rp_syncLatencyMs` still follow the stored `lsdj-sync` mode: an LSDj stored in MI.OUT whose cart reads MidiMap
-    keeps capture "drive" while its node runs `midiMap`. Pre-existing: `kernelProjection` behaved the same.
-  - A profile build's `apu-render` span begins where the last script node's `process` ended, which is right
-    while every script node runs before the systems, as `buildGraph` places them; a script node placed after
-    a system would need it revisited.
+    keeps capture "drive" while its nodes run `midiMap`. Pre-existing: `kernelProjection` behaved the same.
   - A few test-native case names still say "the kernel" (in `dsp-lsdj-midisync`, `dsp-risa-sync`,
-    `dsp-sms-sync`, `lsdj-midiout` and `plugin-controlplane`), where they mean the protocol nodes. Renaming
+    `dsp-sms-sync` and `plugin-controlplane`), where they mean the protocol nodes. Renaming
     a case is harmless but was kept out of B3b, which changed no test's identity it did not have to (the one case
     `dsp-serial.test.ts` renamed, because its name described the deleted kernel, is the exception).
 - **Found while building B3a, left as they are:**
@@ -210,10 +207,10 @@ input's system from its settings, and deleted the kernel. What is left, in order
   - A Game Boy that clocks its serial port itself, a CGB on its fast clock, can send more in a block past
     about 12000 frames (6000 in double speed) than its fixed `serialOut` holds; the rest is dropped and
     counted ([SystemNodes.cpp](../packages/native/src/host/graph/SystemNodes.cpp) `kSerialOutCapacity`).
-  - `rp.lsdj.sync`'s `midiOut` (the kernel node's, before B3b) is sized for a decoded MI.OUT stream at its
-    densest up to 8192-frame blocks (1024 messages, a captured byte every 8 frames), while `serialOut` is
-    sized for 65536: in a larger block the decoded stream can pass 1024 messages, and the rest is dropped and
-    counted ([nodes/ports.ts](../packages/retroplug/src/script/nodes/ports.ts) `MIDI_OUT`).
+  - `rp.lsdj.serial-out`'s `midiOut` (the kernel node's before B3b, then `rp.lsdj.sync`'s) is sized for a
+    decoded MI.OUT stream at its densest up to 8192-frame blocks (1024 messages, a captured byte every 8 frames),
+    while `serialOut` is sized for 65536: in a larger block the decoded stream can pass 1024 messages, and the
+    rest is dropped and counted ([nodes/ports.ts](../packages/retroplug/src/script/nodes/ports.ts) `MIDI_OUT`).
   - `rp.host.midi-in`'s sort at the source merges two frame runs in linear time, which is what real
     plugin hosts stage (the DAW's events, then the automation CCs at frame 0); a staging of three or more
     runs falls back to an in-place insertion sort, quadratic in what was staged, on the block thread. The
@@ -224,7 +221,8 @@ input's system from its settings, and deleted the kernel. What is left, in order
   - A feedback source whose queue capacity changes while an audio thread runs gets a fresh, empty front:
     the block in flight is lost, and `droppedMessages` never counts it, since the carry and its count run
     only while quiescent ([Planner.cpp](../packages/native/src/graph/Planner.cpp) `front`). No port changes
-    its capacity live today: the Game Boy's `serialOut`, the one feedback source, is fixed.
+    its capacity live today, and no document `buildGraph` builds holds a feedback wire: the Game Boy's `serialOut`,
+    fixed, was the one source until LSDj's decoder moved after it.
   - `retroplug-engine-graph-test` replaces the global `operator new` with a counting allocator
     ([CountingAllocator.cpp](../packages/native/test/graph/CountingAllocator.cpp)) that allocates through
     `_aligned_malloc` on Windows, so a core or QuickJS freeing what `new` made with `free()` would crash
diff --git a/spec/08-profiling.md b/spec/08-profiling.md
index 5fba0dab..f99bfd81 100644
--- a/spec/08-profiling.md
+++ b/spec/08-profiling.md
@@ -45,13 +45,15 @@ its place in the payload buffer moves, and the pool and the buffer grow only pas
 
 **The protocol nodes** ([src/script/nodes/](../packages/retroplug/src/script/nodes), since B3b) are
 written that way: each reads the `Message` views it is handed, sends on its own ports, and keeps its state
-and tick counter on its instance, so the routing, mGB, N8 and LSDj MIDI-sync nodes allocate **nothing** in
-a warm block, JS or native, and `dsp-bench` and `nes-bench` measure 0 allocations a block (section 9). What
-still allocates is work that makes something:
+and tick counter on its instance, so the routing, mGB, N8 and LSDj MIDI-sync nodes, and LSDj's serial-out
+decoder (`rp.lsdj.serial-out`) in master sync and MI.OUT, allocate **nothing** in a warm block, JS or
+native, and `dsp-bench` and `nes-bench` measure 0 allocations a block (section 9). The decoders
+([lsdjArduinoboy.ts](../packages/lsdj/ts/lsdjArduinoboy.ts)) hand each message over byte by byte,
+`emit(frame, len, b0, b1, b2)`, and the node sends it from one of three arrays it made once, one a message
+length. What still allocates is work that makes something:
 
 | Site | Allocates |
 |---|---|
-| the MI.OUT and master-sync decoders' `emit([...])` ([lsdjArduinoboy.ts](../packages/lsdj/ts/lsdjArduinoboy.ts)), run by `rp.lsdj.sync` in its two MIDI-out modes | about 3 JS objects for every host MIDI message decoded: the one per-block allocation left on a production path |
 | `rp.risa.sync`'s arm ([risaSync.ts](../packages/retroplug/src/risaSync.ts)) | two objects on a start or a seek: `risaLocate`'s object and `risaArmPacket`'s array |
 | `rp.controller.session`'s surface diff and launches ([controller/session.ts](../packages/retroplug/src/controller/session.ts)) | per LED change and per launch, while a surface is in use |
 
@@ -62,7 +64,9 @@ Hoisting those to the kernel's structure change cut it to **~32** (−81%). The
 measured **33.81**: it still built the kernel's `BlockInput` afresh every block (an input object, a
 `midiIn` array of `{ frame, data }` objects with `data` copied to a plain array, empty `buttons` / `keys`
 arrays, a `{ system, byte }` object per MI.OUT byte). B3b's nodes take the marshalled inputs as they are,
-and the count is 0.
+and the count is 0. The MI.OUT and master-sync decoders built each message they emitted as a new array
+until their emit took its bytes as arguments: 3.56 allocations a block in `dsp-bench`'s master-sync window
+and 0.44 in its MI.OUT one, both 0 since.
 
 ## 4. Approach
 
@@ -222,10 +226,16 @@ past `renderAudio`'s 1024-frame block).
   spans, recorded only while armed. Every script node's process is a span **named by its type**
   (`rp.midi.route`, `rp.mgb.midi`, ...), holding `marshal` (its audio copies in and the transport) and
   `js-call` (its JS, inside which each input it reads is a `marshal` span); the Engine adds `apu-render` (the
-  plan from where its script nodes end) and `state-publish`. The labels are fixed: `script-node` (0),
+  plan's whole run, from where it starts to where it ends, so every script node's process span lies inside
+  it, wherever the planner runs the node, before a system or after one, and its figure includes the script
+  nodes' few microseconds beside the cores') and `state-publish`. A span is stored as it closes, so a block's
+  `apu-render` follows the spans inside it in the list; the trace-event JSON gives each span its start and
+  length, so nothing that reads it depends on that order. The labels are fixed: `script-node` (0),
   `marshal`, `js-call`, `apu-render`, `state-publish` (1 to 4), then the bundle's types from
   `DSP_SPAN_TYPE_BASE` (16) up, in the order it registered them, each named by its id (the production
-  bundle's seven are 16 to 22). By type rather than node id, as the DSP kernel's per-role spans were per kind
+  bundle's eight are 16 to 23: `rp.midi.route`, `rp.mgb.midi`, `rp.n8.midi-to-fifo`, `rp.risa.sync`,
+  `rp.sms.sync`, `rp.lsdj.sync`, `rp.controller.session` and `rp.lsdj.serial-out`, registered last so the
+  others kept their labels). By type rather than node id, as the DSP kernel's per-role spans were per kind
   until B3b made each role a node: a type's label is fixed when the bundle loads, where an id comes and goes
   with every document, so two systems' nodes of one type are two spans of one label a block, in plan order.
   Every label is below `kDspSpanLabels` (1024), so the name table stays bounded: a type past it is traced
@@ -255,10 +265,16 @@ past `renderAudio`'s 1024-frame block).
   `rp.n8.midi-to-fifo`, and `rp.midi.route`, whose reshape freed its port arrays when its outputs grew). That count
   must be above 0; pre-making those arrays at create time would move the allocation out of the block, and the
   assertion would need another trigger. (The runtime's peak heap cannot be the proof: it is never reset, and the
-  bundle's load puts ~300 KB in it before the window opens.) Skips (a counted `skip`) under a non-profile host so the
-  normal sweep stays fast and green;
+  bundle's load puts ~300 KB in it before the window opens.) Skips (a counted `skip`) under a non-profile host so
+  the normal sweep stays fast and green;
   [test-native/nes-bench.test.ts](../packages/retroplug/test-native/nes-bench.test.ts) is its NES twin, on
-  the Mesen core and the `nes-n8-midi` role's node, and does the same.
+  the Mesen core and the `nes-n8-midi` role's node, and does the same. After the mGB window, the mGB systems gone,
+  dsp-bench has a window each on a real LSDj, playing, its `rp.lsdj.serial-out` decoding what it clocks out: master
+  sync (a stock LSDj in SYNC=LSDj, captured in `listen`) and MI.OUT (an Arduinoboy build in SYNC=MI.OUT, captured in
+  `drive`), each over `min(RP_BENCH_BLOCKS, 400)` blocks after the same warmup, held to the mGB window's zero, to
+  more clocks than half its tempo sends (so LSDj played through it), and proved live by the same second window.
+  They need the fixture tree's LSDj ROMs (`RETROPLUG_RESOURCES_DIR`): without them the case skips there, after the
+  mGB window has been held to its zero.
 - **Build knob + scripts** — `RETROPLUG_PROFILE` ([CMakeLists.txt](../CMakeLists.txt), mirrors
   `RETROPLUG_SANITIZE`), `tools/run-profile.sh [stats|trace|unit|dhat|callgrind|massif]` (separate
   `build-prof/` RelWithDebInfo dir), and `pnpm profile`.
@@ -273,11 +289,13 @@ modes are out.
   **bit-identical run-to-run** (only `xRT`/`gcMs` wall-clock vary). First measured result (Profile A, 1
   mGB core, 2000 blocks): **~171 allocs/block**, `freesPerBlock` ≈ allocs (balanced churn),
   `liveHeapDelta` ≈ 0 (not a leak), `maxBlockAllocs` 279.
-- The profile build's own unit checks are the five Catch2 `[profile]` cases of `retroplug-engine-graph-test`
+- The profile build's own unit checks are the six Catch2 `[profile]` cases of `retroplug-engine-graph-test`
   (a script node's span labelled by its type, holding `marshal` then `js-call` and closed every way a call
   ends; nodes of one type sharing its label and each type labelled in load order; the calls refused while
   the audio thread runs and the trace reads reading nothing then; a type past the name table traced as
-  `script-node`; and the embedded bundle's types labelled 16 to 22 in order). Nothing else runs them:
+  `script-node`; the embedded bundle's types labelled 16 to 23 in order, `rp.lsdj.serial-out` 23; and an
+  Engine's `apu-render` spanning its plan's whole run, each block's holding the process spans of a script node
+  before its Game Boy and of one after it). Nothing else runs them:
   `tools/run-profile.sh unit`, or by hand `cmake --build build-prof --target retroplug-engine-graph-test -j$(nproc)`
   then
   `build-prof/bin/retroplug-engine-graph-test "[profile]"`.
@@ -305,7 +323,18 @@ modes are out.
   `marshal` spans nest inside those figures); the means over every `js-call` and `marshal` span in the trace, the
   two nodes' together, are about 0.95 and 0.11 us, against 1428 us of `apu-render` and 2.0 of `state-publish`;
   nes-bench's has `rp.midi.route` at 1.09 us and `rp.n8.midi-to-fifo` at 0.53, against 2151 of `apu-render` and 3.7 of
-  `state-publish`. The script nodes are under 0.2% of a block's work; the cores are the rest.
+  `state-publish`. The script nodes are under 0.2% of a block's work; the cores are the rest. (That `apu-render`
+  began where the last script node's `process` ended; it spans the whole plan since LSDj's decoder moved after its
+  Game Boy, so a trace's figure holds the script nodes' microseconds too.)
+- **On LSDj's serial-out decoder.** dsp-bench's two LSDj windows read **0 allocs/block** and 0 bytes,
+  `maxBlockAllocs` 0, over 400 blocks each: master sync with 476 host MIDI messages decoded, every one a clock, and
+  MI.OUT with 60 (40 clocks, 20 NoteOns); the same run's mGB window reads 0 with a peak heap of 311912 bytes at
+  16.4× real time. With the decoders' array per message (the emit before it took each message's bytes as arguments)
+  the two windows read 3.56 and 0.44 allocations a block. `ScriptBench`'s protocol-node plan
+  (`build/bin/retroplug-engine-graph-test "[bench]"`, five script nodes, the decoder placed after the systems) reads
+  0.00 JS allocations a block in every row, where its MI.OUT rows read 0.23, 15 and 120 at 1, 64 and 512 frames with
+  that emit: the script nodes alone take 0.87, 1.03 and 1.83 us a block with the LSDj in MIDI sync, and 0.85, 3.86 and
+  23.84 us decoding MI.OUT at a byte every 8 frames.
 - **Ship path clean:** dsp-bench skips under the default host; the full `pnpm test:native`
   sweep and `pnpm test` stay green; the default `build/` and shipped plugin never
   define `RETROPLUG_PROFILE` (zero overhead).
@@ -330,7 +359,7 @@ Native ([packages/native/src/](../packages/native/src/)):
 TypeScript ([packages/retroplug/](../packages/retroplug/)):
 - [src/audioDriver.ts](../packages/retroplug/src/audioDriver.ts) — `renderAudio` /
   `stageMidiIn` / `setBpm` / `setTransport` (the bench's injection surface).
-- [src/script/nodes/](../packages/retroplug/src/script/nodes) / [src/lsdjArduinoboy.ts](../packages/lsdj/ts/lsdjArduinoboy.ts) / [src/midiRouting.ts](../packages/retroplug/src/midiRouting.ts): the protocol nodes the benchmarks drive, and the decoders that are the per-block allocation site left (§3).
+- [src/script/nodes/](../packages/retroplug/src/script/nodes) / [src/lsdjArduinoboy.ts](../packages/lsdj/ts/lsdjArduinoboy.ts) / [src/midiRouting.ts](../packages/retroplug/src/midiRouting.ts): the protocol nodes the benchmarks drive, and the MI.OUT and master-sync decoders `rp.lsdj.serial-out` runs (§3).
 - [test-native/audio-render.test.ts](../packages/retroplug/test-native/audio-render.test.ts) —
   the setup template to copy.
 - [scripts/run-native-tests.mjs](../packages/retroplug/scripts/run-native-tests.mjs) — how a
diff --git a/spec/13-subsystems.md b/spec/13-subsystems.md
index 72220d89..344648c6 100644
--- a/spec/13-subsystems.md
+++ b/spec/13-subsystems.md
@@ -186,8 +186,9 @@ script nodes** (Phase B3a,
 [docs/modular-graph-phase-b3b-design.md](../docs/modular-graph-phase-b3b-design.md)): a TS node per role
 (`rp.mgb.midi`, `rp.lsdj.sync`, ...), the MIDI routing and the controller session, run by the script host
 every Engine embeds, each with its settings in the published graph, its inputs and outputs wires from the
-host I/O nodes and to its system node's inputs, and each Game Boy's `serialOut` coming back into its LSDj
-node on a feedback wire ([04-roles-protocol-nodes.md](04-roles-protocol-nodes.md)). What is left is
+host I/O nodes and to its system node's inputs, and each LSDj's Game Boy's `serialOut` going forward into
+its serial-out decoder, `rp.lsdj.serial-out`, placed after the systems
+([04-roles-protocol-nodes.md](04-roles-protocol-nodes.md)). What is left is
 [07-remaining-work.md](07-remaining-work.md) section 5.
 
 - Code: `packages/native/src/graph/` (C++), `packages/native/src/host/graph/` (the engine's nodes),
diff --git a/tools/run-profile.sh b/tools/run-profile.sh
index a134e30d..4b0c9ad8 100755
--- a/tools/run-profile.sh
+++ b/tools/run-profile.sh
@@ -10,10 +10,11 @@
 # allocator + the dsp* profiling RPCs are compiled in, and -g is global so valgrind attributes into
 # quickjs.c. The load-bearing build/ and the shipped plugin are untouched (they never define the macro).
 #
-#   stats     — in-process allocation counters; prints one JSON metrics line (deterministic, fast).
+#   stats     - in-process allocation counters; prints a JSON metrics line per window (deterministic, fast):
+#               dsp-bench's mGB window, then, with the fixture tree's LSDj ROMs, its master-sync and MI.OUT ones.
 #   trace     — per-role runtime spans (spec/08 Tier B) → build-prof/trace.json (Chrome trace-event JSON;
 #               load in ui.perfetto.dev or speedscope). Knob: RP_BENCH_TRACE_BLOCKS (default 64).
-#   unit      - the [profile] Catch2 cases of retroplug-engine-graph-test (spans, refusals), built here.
+#   unit      - the [profile] Catch2 cases of retroplug-engine-graph-test (spans, refusals, apu-render), built here.
 #   dhat      — valgrind DHAT allocation census → build-prof/valgrind/dhat.out.json (load in the viewer).
 #   callgrind — deterministic instruction counts → callgrind_annotate top-N.
 #   massif    — heap size over time → ms_print.
````

- [ ] **Step 2: Check**

Run: `pnpm check:docs && pnpm typecheck && pnpm test`
Expected: `check-doc-links: 60 file(s) clean`, `packages/retroplug typecheck: Done`, then `# 167 test file(s) passed (jobs=16)`.

Run: `cmake --build build -j$(nproc) && pnpm test:cli-ts`
Expected: a clean build (a header comment changed, so the plugin recompiles and prints its existing `-Wformat-truncation` warning in `PluginDSP.cpp`), then `PASS: retroplug-cli TypeScript harness` (the CLI embeds `sdk-types.d.ts`, whose comment changed).

Run: `git diff HEAD | python3 -c "import sys; print(sum('\u2014' in l for l in sys.stdin if l.startswith('+')))"`
Expected: `0` (no emdash on any added line).

- [ ] **Step 3: Commit**

````bash
git add -- AGENTS.md docs/lsdj-serial-out-design.md docs/lsdj.md docs/modular-graph-design.md docs/sdl-standalone.md \
  packages/native/sdl/main.cpp packages/native/src/host/rpc/EngineRpcService.hpp \
  packages/native/test/engine-graph/HostIo.test.cpp packages/retroplug/cli/sdk-types.d.ts \
  packages/retroplug/src/audioDriver.ts packages/retroplug/src/graph/defineNode.ts \
  packages/retroplug/src/systemsStore.ts packages/retroplug/test/systems/store-graph.test.ts spec/01-architecture.md \
  spec/02-native-host.md spec/03-ts-layer.md spec/04-roles-protocol-nodes.md spec/06-build-test.md \
  spec/07-remaining-work.md spec/08-profiling.md spec/13-subsystems.md tools/run-profile.sh
git commit -F - <<'EOF'
docs: rp.lsdj.serial-out, the merge by frame and apu-render, as built

The docs and comments say what the four commits before this one left: an lsdj-sync role places two nodes,
rp.lsdj.sync before its Game Boy and rp.lsdj.serial-out after it, reading serialOut over a forward wire in the
block the core captured it and sending each message at its byte's frame, master sync's stop at its 200 ms mark;
eight script node types, span labels 16 to 23; rp.host.midi-out merging its inputs by frame, ties in input order,
the guard after; apu-render spanning the plan with the script nodes' spans inside it; the decoders allocating
nothing, with dsp-bench's LSDj windows and their figures. spec/07 drops the master-sync, decoder-allocation and
apu-render items and records what this change leaves (the core's byte timing moving with the split, the SDL
standalone sending host MIDI at block time). The design's status is built, and its section 12 records what
building it found. No behaviour change.
EOF
````

### Task 6: Verify everything

The acceptance run, on the final tree. Nothing to commit unless a step fails and is fixed; a fix is its own commit,
named for what it fixes. Every Expected is what the prototype's acceptance run printed on its final tree.

- [ ] **Step 1: The tree and the build**

Run: `git status --short && cmake --build build -j$(nproc)`
Expected: only ` m deps/sameboy`, then a clean build (the plugin's existing `-Wformat-truncation` warning in `PluginDSP.cpp` only if it recompiles).

- [ ] **Step 2: The suites**

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=16)`, none skipped, and `git status` shows no baseline changed.

Run: `pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=16)`, with `46 case(s) SKIPPED in 10 file(s): parity/blocks-tiny-lsdj(9), parity/blocks-tiny-lsdj-master(6), parity/blocks-tiny-risa(3), parity/controller(1), parity/gba(2), parity/lsdj-link(6), parity/lsdj-modes(8), parity/lsdj-sync(8), parity/render-blocks(1), parity/risa(2)`, as CI runs it.

Run: `RETROPLUG_RESOURCES_DIR=/nonexistent-rp-resources RISA_SRC=/nonexistent-rp-risa pnpm test:native`
Expected: `# 154 native test file(s) passed (jobs=16)`, with `136 case(s) SKIPPED in 60 file(s)` and the skip check clean.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native`
Expected: `# 154 native test file(s) passed (jobs=16)`, with `2 case(s) SKIPPED in 2 file(s): dsp-bench(1), nes-bench(1)`.

Run: `pnpm test`
Expected: `# 167 test file(s) passed (jobs=16)`.

Run: `pnpm test:plugin`
Expected: `# 10 plugin test binary(ies) passed (jobs=16)` (engine-graph `All tests passed (15598 assertions in 120 test cases)`).

Run: `pnpm test:ui`
Expected: `# 39 UI test file(s) passed (jobs=16)`, with `2 case(s) SKIPPED in 2 file(s): lsdj-hd(1), lsdj-overlay(1)`.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:ui`
Expected: `# 39 UI test file(s) passed (jobs=16)`, none skipped.

Run: `pnpm test:node && pnpm typecheck && pnpm check:docs`
Expected: `ℹ pass 12` / `ℹ fail 0`, `packages/retroplug typecheck: Done`, `check-doc-links: 61 file(s) clean` (the prototype's 60, and this plan).

- [ ] **Step 3: The sanitizers**

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources tools/run-sanitizer.sh thread && RETROPLUG_RESOURCES_DIR=/workspaces/resources tools/run-sanitizer.sh address`
Expected: `==> host passed under thread sanitizer`, then `==> host passed under address sanitizer` (with the fixtures, so `dsp-threaded` runs: a system added and removed while the audio thread plays).

Run: `cmake --build build-tsan --target retroplug-engine-graph-test -j$(nproc) && TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1 second_deadlock_stack=1" build-tsan/bin/retroplug-engine-graph-test "[protocol-nodes]"`
Expected: `All tests passed (7547 assertions in 11 test cases)`, no ThreadSanitizer report.

- [ ] **Step 4: The live hosts**

Each of these one at a time, with nothing rebuilding the plugin meanwhile:

Run: `pnpm reaper:editor-reopen`
Expected: a `PASS:` line (the reopened editor still shows the loaded project).

Run: `pnpm reaper:editor-autoload`
Expected: a `PASS:` line (the editor shows the autoloaded project).

Run: `pnpm reaper:two-instances-load`
Expected: a `PASS:` line (a ROM loaded through the second instance's editor left both instances live; the prototype's heartbeat counted 206 before the second load and 1098 at the end of the watch).

Run: `pnpm reaper:params`
Expected: a `PASS:` line ending `count unchanged at 2725.`

Run: `pnpm reaper:params-clap`
Expected: a `PASS:` line ending `count unchanged at 644.`

Then the whole Reaper leg at once. Its fixture step authors the LSDj fixtures from `../resources/roms/lsdj/...`, which
does not exist from this worktree (the sibling resources are at `/workspaces/resources`), and `RP_SUITE_NO_BUILD`
skips the build AND every fixture. So author the fixtures by hand with explicit ROM paths, make sure no Reaper or
jackd of yours is left, and run the suite without its own build or fixture step. The plugin build in it is the one the
first step made.

Run: `node tools/author-rplg.js && node tools/author-nes-rplg.js && node tools/author-risa-rplg.js /workspaces/resources/roms/risa/risa-v2.3.0/risa-2.3.0-pal.nes && node tools/author-sms-rplg.js && node tools/author-sms-rplg.js resources/roms/smsggdj_v0_45.gg && node tools/author-lsdj-rplg.js midi-metro /workspaces/resources/roms/lsdj/lsdj9_4_2.gb && node tools/author-lsdj-rplg.js midi-drift /workspaces/resources/roms/lsdj/lsdj9_4_2.gb && node tools/author-lsdj-rplg.js arduinoboy-metro /workspaces/resources/roms/lsdj/lsdj9_3_3-arduinoboy.gb`
Expected: an `authored build/<name>.rplg.zip` line for each (eight).

Run: `pgrep -a reaper; pgrep -a jackd; RP_SUITE_NO_BUILD=1 pnpm reaper:all`
Expected: no leftover process listed, then `ALL 15 REAPER CHECKS PASSED`. If a render fails, run that one check alone once (its own `pnpm reaper:<name>`) and report both results: concurrency in this container has flaked before.

Run: `pnpm sdl:smoke && pnpm sdl:pipewire`
Expected: `SDL smoke OK`, then `SDL PipeWire check: 12 passed, 0 failed` (the wide-output leg ran; where wireplumber will not start it prints `SKIPPED: wide-output checks need wireplumber` and fewer checks pass).

- [ ] **Step 5: The consumers, against this tree's CLI**

Their suites read `src/` and write beside the ROMs, so each runs from a scratch copy and leaves its repository
untouched:

Run: `R=$PWD; rm -rf /tmp/so-bt && mkdir -p /tmp/so-bt/build /tmp/so-bt/retroplug-cli && cp -r /workspaces/bliptoaster/src /tmp/so-bt/ && cp /workspaces/bliptoaster/build/*.nes /workspaces/bliptoaster/build/*.dbg /workspaces/bliptoaster/build/*.map /tmp/so-bt/build/ && cp -r /workspaces/bliptoaster/retroplug-cli/tests /tmp/so-bt/retroplug-cli/ && (cd /tmp/so-bt && "$R/build/bin/retroplug-cli" test retroplug-cli/tests --rom build/bliptoaster.nes); rm -rf /tmp/so-bt`
Expected: `PASS: 57/57 test file(s) ok` (BlipToaster at ed0c843; the count follows BlipToaster's own tests).

Run: `R=$PWD; rm -rf /tmp/so-vj && mkdir -p /tmp/so-vj/build /tmp/so-vj/retroplug-cli && cp -r /workspaces/nesvj/src /tmp/so-vj/ && cp /workspaces/nesvj/build/*.nes /workspaces/nesvj/build/*.dbg /workspaces/nesvj/build/*.map /tmp/so-vj/build/ && cp -r /workspaces/nesvj/retroplug-cli/tests /tmp/so-vj/retroplug-cli/ && (cd /tmp/so-vj && "$R/build/bin/retroplug-cli" test retroplug-cli/tests --rom build/nesvj.nes); rm -rf /tmp/so-vj`
Expected: `PASS: 19/19 test file(s) ok`.

Run: `pnpm test:cli-ts`
Expected: `PASS: retroplug-cli TypeScript harness`.

- [ ] **Step 6: The profile build and the benches, for the record**

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources tools/run-profile.sh stats && tools/run-profile.sh stats nes-bench`
Expected: dsp-bench's three windows, `DSP-BENCH {"profile":"A","cores":1,"warmupBlocks":200,"blocks":2000,"allocsPerBlock":0,"bytesPerBlock":0,"freesPerBlock":0,"liveHeapDelta":0,"peakLive":311912,"maxBlockAllocs":0,"maxBlockBytes":0,"gcMs":0.07,"gcFreedBytes":0,"xRT":16.2}`, `DSP-BENCH {"workload":"lsdj-masterSync","warmupBlocks":200,"blocks":400,"allocsPerBlock":0,"bytesPerBlock":0,"maxBlockAllocs":0,"maxBlockBytes":0,"midiOut":476,"clocks":476,"noteOns":0}` and `DSP-BENCH {"workload":"lsdj-midiOut","warmupBlocks":200,"blocks":400,"allocsPerBlock":0,"bytesPerBlock":0,"maxBlockAllocs":0,"maxBlockBytes":0,"midiOut":60,"clocks":40,"noteOns":20}`; then nes-bench's `DSP-BENCH {"core":"mesen","rom":"bliptoaster.nes","profile":"A","blocks":2000,"allocsPerBlock":0,"bytesPerBlock":0,"freesPerBlock":0,"liveHeapDelta":0,"peakLive":309912,"maxBlockAllocs":0,"gcMs":0.07,"gcFreedBytes":0,"xRT":10.59,"usPerBlock":2193.05}`: no allocation in any block of any window. Without `RETROPLUG_RESOURCES_DIR` the LSDj windows skip after the mGB window passes.

Run: `tools/run-profile.sh trace`
Expected: `build-prof/trace.json` written, with `rp.midi.route`, `rp.mgb.midi`, `marshal`, `js-call`, `apu-render` and `state-publish` among its span names, and each block's `apu-render` enclosing its script nodes' spans. The trace's scene places no LSDj (it is taken after the mGB window), so `rp.lsdj.serial-out` is not among them.

Run: `build/bin/retroplug-engine-graph-test "[bench]" && build/bin/retroplug-graph-test "[bench]"`
Expected: `All tests passed (672 assertions in 1 test case)`, then the planner bench's `All tests passed (156 assertions in 1 test case)`, each with its table (the prototype's: the protocol nodes at 0.865, 1.039 and 1.833 us a block in MIDI sync and 0.884, 3.909 and 24.301 in MI.OUT at 1, 64 and 512 frames, every row with 0 JS and 0 native allocations).

Run: `git status --short`
Expected: only ` m deps/sameboy`.

