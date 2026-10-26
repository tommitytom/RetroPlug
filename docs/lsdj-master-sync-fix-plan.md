# LSDj master sync: listening to LSDj instead of clocking it - implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for
> tracking.

**Goal:** LSDj master sync captures only the clock LSDj sends instead of clocking a stopped LSDj into a slave, and
its decoder sends a clock per byte and 0xFC after a 200 ms silence timed on the bytes' own frames, so every
master-sync case plays, restarts after a stop and sends the same messages at any block split and rate; B0's sixth and
eighth recorded defects are fixed and the eight master-sync parity cases re-recorded.

**Architecture:** Per [lsdj-master-sync-fix-design.md](lsdj-master-sync-fix-design.md). First the frames: the Game
Boy node's `serialOut` stamps each byte at the frame its core completed it, and the kernel hands the frames to a
system stage beside the bytes (`SerialOutEvent.frame`, `SystemCtx.serialOutFrames`), changing no behaviour. Then the
fix: `SameBoySystem`'s capture flag becomes a mode (`SerialOutCapture`: `Listen` captures what the Game Boy clocks
itself, `Drive` also clocks an external-clock transfer, MI.OUT's path), carried as `"listen"` / `"drive"` in the
build spec and `setSerialOutCapture`, and picked by the store from the lsdj-sync mode; the master-sync decoder
becomes the firmware's (every byte a clock, a silence the stop), with its state in a sub-object the lsdj-sync
dispatcher drops whenever the mode is not master sync.

**Tech stack:** C++20 (CMake), Catch2, TypeScript on txiki (`pnpm test` mock tier, `pnpm test:native` real host),
the parity baseline.

**Provenance:** every code block below was built and run before this plan was written, on the scratch branch
`proto/master-sync` in this worktree, on top of the revised design commit 11db878b: one commit per task, with GCC
13.3 on linux-x64:
- Task 1: `0e6e206e` feat(engine): the Game Boy's serialOut stamps each byte at the frame its core completed it, and the kernel hands the frames to a system stage beside the bytes
- Task 2: `6d319acc` fix(lsdj): master sync listens to the clock LSDj sends instead of clocking a stopped LSDj into a slave, and a 200 ms silence is its stop; B0's sixth and eighth recorded defects fixed, the master-sync parity cases re-recorded

A code block that changes files is the prototype's diff for them, so `git apply` it from the step's text (or apply
it hunk by hunk with the Edit tool); `git checkout <commit> -- <files>` of the step's files from the commit above
gives the same result once a step's files are complete (two files, `lsdj-mastersync.test.ts` and
`KernelNode.test.cpp`, are split over two steps each: check those out only at their last step). The parity baselines
Task 2 re-records are written by a command, not shown. Every expected pass, test count and figure is what the
prototype's runs printed, except where a step says it is the expected failure of missing code (a member, a type or a
behaviour that does not exist yet). A superseded first prototype, the decoder-only gap rule of the first design
(c8135ed4), is on `proto/master-sync-gap`; nothing here uses it.

**Ground rules for whoever executes this:**
- Commit after each task (the commit commands are given). No `Co-Authored-By` or "generated with" trailer.
- Do not push, and do not open a PR.
- Never use an emdash character in code, comments, docs or commit messages; use " - ", ":" or ";". Some diffs below
  carry emdashes on lines the prototype did not write (context and removed lines of older code): leave those be.
- Run every command from the worktree root with absolute paths or a subshell (`(cd x && ...)`); never `cd` the
  session itself.
- Don't `rm -rf build` to fix CMake; investigate. Never use bare `git stash`, and never switch branches with
  uncommitted work in the tree.
- `deps/sameboy` shows as modified after any configure. That is the tracked audio patch: expected, leave it. Never
  `git apply` a diff made with `git diff` over the whole tree: it carries that submodule's dirty marker.
- The derived bundle C arrays (`build/native/*bundle_data.c`) are never committed.
- Task 1 changes no behaviour: every parity baseline and every recorded digest holds. Task 2 re-records exactly the
  eight master-sync cases (`blocks-tiny-lsdj-master`'s six, `lsdj-modes`' `masterSync-44100` and
  `masterSync-48000`) and the KernelNode C++ test's LSDj-modes digest; anything else that moves is a real change to
  the audio: stop and find it. A recording happens only on the baseline's target, `linux-x64-gnu13` (this
  devcontainer).
- The fixture groups and the LSDj tests need `RETROPLUG_RESOURCES_DIR=/workspaces/resources`; without it they skip.
  Pass it on the same command line: a shell `export` does not survive between commands.
- `node packages/retroplug/scripts/run-native-tests.mjs` (what `pnpm test:native` runs) takes ONE filter: an exact
  slug (`parity/lsdj-modes`) or a prefix in the dash form. Run several files as separate commands, or the whole
  suite.
- `pnpm check:docs` counts the tracked docs: 56 in the prototype, which had no plan file, so 57 once this plan is
  committed.

---

## File structure

| Path | Responsibility |
|---|---|
| `packages/native/src/host/graph/SystemNodes.cpp` | the Game Boy's `serialOut`: each byte at the frame its core completed it, clamped into the block |
| `packages/retroplug/src/dspKernel.ts`, `src/script/kernelNode.ts` | `SerialOutEvent.frame`, `SystemCtx.serialOutFrames`, the kernel node copying message frames |
| `packages/native/src/system/sameboy/SameBoySystem.{hpp,cpp}` | `SerialOutCapture` (`Off` / `Listen` / `Drive`): the external-clock path under `Drive` only |
| `packages/native/src/host/engine/Engine.{hpp,cpp}`, `src/host/rpc/{BackendTypes.hpp,EngineRpcService.hpp,EngineRpcService.cpp}` | the mode on the wire: the build spec's `serialOutCapture` string, `setSerialOutCapture(id, mode)`, `ConfigField::SerialOutCapture` as 0 / 1 / 2 |
| `packages/retroplug/src/{backend,realBackend,systemsStore}.ts`, `testing/mockBackend.ts` | `SerialOutCapture = "listen" \| "drive"`, the store's `serialOutCaptureFor` |
| `packages/retroplug/src/lsdjArduinoboy.ts`, `src/dspRoles.ts` | the master-sync decoder (`MASTER_SYNC_STOP_MS`), its state dropped outside the mode |
| `packages/retroplug/test/dsp/lsdj-arduinoboy.test.ts`, `test/script/kernel-node.test.ts`, `test/systems/{build-spec-wire,store-graph}.test.ts` | the decoder, the frames through the kernel, the mode on the wire and in the store |
| `packages/native/test/engine-graph/{HostIo,SystemNode,KernelNode,EngineGraph}.test.cpp` | the stamped frames, `Listen` never clocking, the mode through `buildCore` and the live call, the kernel's re-recorded digest |
| `packages/retroplug/test-native/lsdj-mastersync.test.ts` | one story at 44.1 and 48 kHz in three splits, a restart included |
| `packages/retroplug/test-native/parity/{lsdj-modes,blocks-tiny-lsdj-master}.test.ts`, `parity/baseline/{lsdj-modes,blocks-tiny-lsdj-master}.json` | the eight master-sync cases, re-recorded, the 48 kHz ones audible |
| `AGENTS.md`, `docs/*`, `spec/*` | the defects fixed, the capture modes, the stop rule |

## Design sections by task

| Design section | Task |
|---|---|
| 3 decisions: measured on the bytes' own frames | 1 |
| 4 the core: capture modes | 2 (steps 1 to 6) |
| 5 the decoder | 2 (steps 7 to 10) |
| 6 what moves | 2 (steps 10 to 13) |
| 7 tests | 1 (the frames), 2 (the rest) |
| 8 docs | 2 (step 14) |
| 9 landing | the task order; Task 3 is the acceptance run |

---

### Task 1: The frames on the wire and through the kernel

The core already stamps each captured byte with its frame (`SameBoySystem::serialOutLog_`); the Game Boy node pushes
every byte at frame 0. Stamp it at the frame the core completed it, clamped to the block's last frame (the core runs
an instruction at a time, so it can complete a byte past the block's end, and with the external-clock path a byte
completed in the block's last sample is stamped `frames`), and carry it through the kernel to a system stage. Nothing
reads the frames yet: MI.OUT's decoder ignores them, and master sync's still counts bytes, so every parity baseline
and every kernel digest holds.

**Files:**
- Modify: `packages/native/src/host/graph/SystemNodes.cpp`, `packages/retroplug/src/dspKernel.ts`, `packages/retroplug/src/script/kernelNode.ts`
- Test: `packages/native/test/engine-graph/HostIo.test.cpp`, `packages/native/test/engine-graph/SystemNode.test.cpp`, `packages/native/test/engine-graph/KernelNode.test.cpp`, `packages/retroplug/test/dsp/lsdj-arduinoboy.test.ts`, `packages/retroplug/test/script/kernel-node.test.ts`

- [ ] **Step 1: The native tests expect each byte at its frame**

The HostIo and SystemNode cases compare what the port carries with the core's log stamped as the node will stamp it
(`serialStamped`, and the same clamp inline); the sync-trace case's expected lines group bytes by frame, as
`HostSyncTrace` writes them; the KernelNode rig's stand-in Game Boy gives its bytes frames (`Captured`), master
sync's run 300 frames apart, which changes none of its recorded digests.

````diff
diff --git a/packages/native/test/engine-graph/HostIo.test.cpp b/packages/native/test/engine-graph/HostIo.test.cpp
index ec0acc61..ad757a13 100644
--- a/packages/native/test/engine-graph/HostIo.test.cpp
+++ b/packages/native/test/engine-graph/HostIo.test.cpp
@@ -190,6 +190,14 @@ std::vector<std::uint8_t> serialLog(const SameBoySystem& gb) {
     return log;
 }
 
+// The same bytes as the Game Boy's serialOut stamps them in a block of `frames`: each at the frame its core completed
+// it, a byte completed past the block's end at its last frame.
+std::vector<std::pair<std::uint32_t, std::uint8_t>> serialStamped(const SameBoySystem& gb, std::uint32_t frames) {
+    std::vector<std::pair<std::uint32_t, std::uint8_t>> log;
+    for (const auto& [frame, byte] : gb.serialOutLog_) log.emplace_back(std::min(frame, frames - 1), byte);
+    return log;
+}
+
 // Set RETROPLUG_SYNC_TRACE, or clear it (null), for the engines constructed after: each reads it as it is built.
 void setSyncTraceEnv(const char* value) {
 #if defined(_WIN32)
@@ -640,29 +648,34 @@ TEST_CASE("a cart mirror names one of the document's NES systems, its settings r
     CHECK(e.cartMirrorSystemId() == 1);
 }
 
-TEST_CASE("the Game Boy's serialOut carries what its core captured, each byte at frame 0", "[engine-graph][host-io]") {
+TEST_CASE("the Game Boy's serialOut carries what its core captured, each byte at the frame its core completed it",
+          "[engine-graph][host-io]") {
     // mGB, with MI.OUT capture armed, has a byte clocked out of its serial port every eight frames (it holds the
-    // port in external-clock receive). The port carries this block's bytes at frame 0, here into a cart mirror whose
-    // sink records them. (buildGraph wires it into the kernel node as a feedback wire: MI.OUT's one block.)
+    // port in external-clock receive). The port carries this block's bytes a byte a message, each at the frame its core
+    // completed it, here into a cart mirror whose sink records them. (buildGraph wires it into the kernel node as a
+    // feedback wire: MI.OUT's one block, and the times master sync's stop is timed from.)
     Engine e(kRate);
     SameBoySystem* const gb = serialOutToCart(e);
-    std::vector<std::uint8_t> carried;
-    bool eachAtZero = true;
+    std::vector<std::pair<std::uint32_t, std::uint8_t>> carried;
+    bool eachAlone = true;
     e.setCoreByteSink([&](std::uint32_t frame, const std::uint8_t* d, std::size_t n, bool) {
-        eachAtZero = eachAtZero && frame == 0 && n == 1;
-        carried.insert(carried.end(), d, d + n);
+        eachAlone = eachAlone && n == 1;
+        for (std::size_t i = 0; i < n; ++i) carried.emplace_back(frame, d[i]);
     });
     std::size_t captured = 0;
+    bool spread = false;
     for (int b = 0; b < 100; ++b) {
         carried.clear();
         block(e);
-        const std::vector<std::uint8_t> log = serialLog(*gb);
+        const auto log = serialStamped(*gb, kFrames);
         INFO("block " << b);
         CHECK(carried == log);
         captured += log.size();
+        spread = spread || (log.size() > 1 && log.front().first != log.back().first);
     }
-    CHECK(eachAtZero);
+    CHECK(eachAlone);
     CHECK(captured > 0);
+    CHECK(spread);  // the frames are the core's, not one frame for the whole block
 }
 
 TEST_CASE("the Game Boy's serialOut carries the largest block a host runs without a drop", "[engine-graph][host-io]") {
@@ -737,8 +750,8 @@ TEST_CASE("a system node's bytes input reaches the engine's sync trace while it
           "[engine-graph][host-io]") {
     // The engine opens its trace at construction when RETROPLUG_SYNC_TRACE names a file, and its destructor writes
     // what is left. A Game Boy's serialOut feeds a NES's bytes input, so each block's line holds what the Game Boy
-    // captured that block, at frame 0, as the NES node recorded it. 256-frame blocks keep that under the trace's
-    // 48 bytes a block.
+    // captured that block, each byte at the frame its core completed it, as the NES node recorded it. 256-frame
+    // blocks keep that under the trace's 48 bytes a block.
     const std::string path =
         (std::filesystem::temp_directory_path() / ("rp-engine-graph-sync-trace-" + std::to_string(std::random_device{}()) + ".log"))
             .string();
@@ -748,7 +761,8 @@ TEST_CASE("a system node's bytes input reaches the engine's sync trace while it
         std::optional<std::string> was;
         ~Restore() { setSyncTraceEnv(was ? was->c_str() : nullptr); }
     } restore{was ? std::optional<std::string>(was) : std::nullopt};
-    // The trace part of each block's line, " 0:<hex>" or "", for what the Game Boy captured.
+    // The trace part of each block's line, " <frame>:<hex>" for each run of bytes at one frame, or "", for what the
+    // Game Boy captured.
     const auto run = [&](bool traced) {
         setSyncTraceEnv(traced ? path.c_str() : nullptr);
         std::vector<std::string> lines;
@@ -763,13 +777,15 @@ TEST_CASE("a system node's bytes input reaches the engine's sync trace while it
                     .ok);
         for (int b = 0; b < 200; ++b) {
             block(e, 256);
-            std::string hex;
-            for (const std::uint8_t byte : serialLog(*gb)) {
+            std::string line;
+            const auto log = serialStamped(*gb, 256);
+            for (std::size_t i = 0; i < log.size(); ++i) {
+                if (i == 0 || log[i].first != log[i - 1].first) line += " " + std::to_string(log[i].first) + ":";
                 char two[3];
-                std::snprintf(two, sizeof two, "%02X", byte);
-                hex += two;
+                std::snprintf(two, sizeof two, "%02X", log[i].second);
+                line += two;
             }
-            lines.push_back(hex.empty() ? "" : " 0:" + hex);
+            lines.push_back(line);
         }
         return lines;
     };
diff --git a/packages/native/test/engine-graph/KernelNode.test.cpp b/packages/native/test/engine-graph/KernelNode.test.cpp
index ced349a1..c1d53382 100644
--- a/packages/native/test/engine-graph/KernelNode.test.cpp
+++ b/packages/native/test/engine-graph/KernelNode.test.cpp
@@ -115,6 +115,16 @@ std::string settingsJson(const Structure& s) {
 
 // ---- one block's input -------------------------------------------------------------------------------------------
 
+// What one Game Boy captured last block, as its serialOut stamps it: each byte at the frame its core completed it, here
+// the first at `first` and each next `stride` frames on (MI.OUT's port clocks a byte every eight frames; a playing LSDj
+// master clocks one every few hundred).
+struct Captured {
+    std::uint32_t system;
+    Bytes         bytes;
+    std::uint32_t stride = 8;
+    std::uint32_t first = 7;
+};
+
 struct BlockIn {
     std::uint32_t                                     frames = 1024;
     double                                            ppq = 0.0;
@@ -123,7 +133,7 @@ struct BlockIn {
     std::vector<std::pair<std::uint32_t, Bytes>>      midi;        // as a host stages it, in any frame order
     std::vector<Bytes>                                controller;  // control-surface traffic, at frame 0
     bool                                              connected = false;
-    std::vector<std::pair<std::uint32_t, Bytes>>      serialOut;   // what each Game Boy captured last block
+    std::vector<Captured>                             serialOut;   // what each Game Boy captured last block
 };
 
 Bytes on(int channel, int key, int velocity = 100) {
@@ -309,7 +319,7 @@ struct KernelRig {
         return *t;
     }
 
-    // One block: what the host staged, each Game Boy's MI.OUT a byte a message at frame 0 as its serialOut carries it,
+    // One block: what the host staged, each Game Boy's MI.OUT a byte a message at its frame as its serialOut carries it,
     // then the block as the engine runs it (the host outputs cleared first, as the engine clears them), and every
     // output recorded.
     void block(const BlockIn& in) {
@@ -319,9 +329,10 @@ struct KernelRig {
         for (const System& s : structure.systems) {
             if (!s.serialOut) continue;
             Feed& feed = node<Feed>("mi-out-" + std::to_string(s.id));
-            for (const auto& [id, captured] : in.serialOut)
-                if (id == s.id)
-                    for (const std::uint8_t b : captured) feed.next[Feed::Bytes].push_back({0, {b}, 0});
+            for (const Captured& c : in.serialOut)
+                if (c.system == s.id)
+                    for (std::size_t i = 0; i < c.bytes.size(); ++i)
+                        feed.next[Feed::Bytes].push_back({c.first + static_cast<std::uint32_t>(i) * c.stride, {c.bytes[i]}, 0});
         }
         midiOut.clear();
         controllerOut.clear();
@@ -533,12 +544,12 @@ TEST_CASE("the kernel node drives each LSDj sync mode, MI.OUT and master sync am
     BlockIn playing{.frames = 1024, .ppq = 1024.0 / kBeat, .playing = true};
     rig.run(playing, 30);
     // MI.OUT: framed protocol bytes, a frame split over two captured bytes; master sync: a run, a quiet block, then a
-    // stopped LSDj's idle flood, then a run again.
+    // stopped LSDj's idle flood, then a run again (a playing LSDj's bytes a few hundred frames apart, its flood's 8).
     Bytes flood;
     for (int i = 0; i < 40; ++i) flood.push_back(static_cast<std::uint8_t>(i & 0x7f));
     playing.ppq = 31 * 1024.0 / kBeat;
-    for (auto captured : {std::vector<std::pair<std::uint32_t, Bytes>>{{7, {framed(0x7d), framed(0x7f), framed(0x70), framed(0x69), 0x7f}},
-                                                                       {8, {5, 5, 5}}},
+    for (auto captured : {std::vector<Captured>{{7, {framed(0x7d), framed(0x7f), framed(0x70), framed(0x69), 0x7f}},
+                                                {8, {5, 5, 5}, 300}},
                           {{7, {0x80, framed(0x74), framed(64)}}, {8, {}}},
                           {{8, flood}},
                           {{8, flood}, {7, {framed(0x7e)}}},
diff --git a/packages/native/test/engine-graph/SystemNode.test.cpp b/packages/native/test/engine-graph/SystemNode.test.cpp
index 71605d65..b19c88c7 100644
--- a/packages/native/test/engine-graph/SystemNode.test.cpp
+++ b/packages/native/test/engine-graph/SystemNode.test.cpp
@@ -675,11 +675,13 @@ TEST_CASE("a linked Game Boy takes its inputs in prepare, before its group steps
     }
 }
 
-TEST_CASE("the Game Boy's serialOut carries what its core captured this block, at frame 0, and leaves the log",
+TEST_CASE("the Game Boy's serialOut carries what its core captured this block, each byte at the frame its core "
+          "completed it, and leaves the log",
           "[engine-graph][system-node][inputs]") {
     // mGB holds its serial port in external-clock receive, so once MI.OUT capture is armed the engine clocks a byte
-    // out of it every eight frames. The node copies the core's log rather than draining it: the engine's MI.OUT
-    // gather reads the same log after the plan, and the core clears it at its next prepareForBlock.
+    // out of it every eight frames. Each byte goes out a byte a message at the frame the core completed it, a byte
+    // completed past the block's end at the block's last frame. The node copies the core's log rather than draining
+    // it: the core clears it at its next prepareForBlock.
     NodeRig rig(kRate);
     auto core = mgb(1);
     core->setSerialOutCapture(true);
@@ -695,15 +697,15 @@ TEST_CASE("the Game Boy's serialOut carries what its core captured this block, a
     for (std::size_t b = 0; b < 160; ++b) {
         const std::uint32_t frames = kBlocks[b % kBlocks.size()];
         rig.host->processBlock(frames, t, graph.ptrs.data(), 2);
-        std::vector<std::uint8_t> log, carried;
-        for (const auto& entry : gb->serialOutLog_) log.push_back(entry.second);
-        bool eachAtZero = true;
+        std::vector<std::pair<std::uint32_t, std::uint8_t>> log, carried;
+        for (const auto& [frame, byte] : gb->serialOutLog_) log.emplace_back(std::min(frame, frames - 1), byte);
+        bool eachAlone = true;
         for (const auto& m : probe.bytes) {
-            eachAtZero = eachAtZero && m.frame == 0 && m.data.size() == 1;
-            if (!m.data.empty()) carried.push_back(m.data[0]);
+            eachAlone = eachAlone && m.data.size() == 1;
+            if (!m.data.empty()) carried.emplace_back(m.frame, m.data[0]);
         }
         INFO("block " << b);
-        CHECK(eachAtZero);
+        CHECK(eachAlone);
         CHECK(carried == log);
         captured += log.size();
     }
````

- [ ] **Step 2: Run them to see the frame cases fail**

Run: `cmake --build build --target retroplug-engine-graph-test -j$(nproc) && build/bin/retroplug-engine-graph-test "[host-io],[system-node],[kernel-node]"`
Expected: FAIL in three cases, each the port's bytes at frame 0 against the log's frames: HostIo's "the Game Boy's
serialOut carries what its core captured, each byte at the frame its core completed it" (`carried == log`),
SystemNode's "the Game Boy's serialOut carries what its core captured this block, each byte at the frame its core
completed it, and leaves the log" (`carried == log`), and the sync-trace case (`traced == expected`, every traced line
` 0:<hex>` where the expected one groups bytes by frame). The KernelNode cases pass, their digests as recorded.

- [ ] **Step 3: Stamp each byte at its frame**

`writeQueues` takes the block's frame count from `finish`.

````diff
diff --git a/packages/native/src/host/graph/SystemNodes.cpp b/packages/native/src/host/graph/SystemNodes.cpp
index 51f491a7..63ed8c4f 100644
--- a/packages/native/src/host/graph/SystemNodes.cpp
+++ b/packages/native/src/host/graph/SystemNodes.cpp
@@ -117,9 +117,9 @@ struct SystemNode {
                     std::memset(b.output(o).u.audio.channels[c], 0, b.frames() * sizeof(float));
         for (std::size_t l = 0; l < n; ++l) std::memset(lanes[l], 0, b.frames() * sizeof(float));
         core->finishBlock(info(b), lanes.data(), n);
-        static_cast<Self*>(this)->writeQueues();
+        static_cast<Self*>(this)->writeQueues(b.frames());
     }
-    void writeQueues() noexcept {}
+    void writeQueues(std::uint32_t) noexcept {}
 
     // Each input in queue order, the inputs in the order the kernel stage the kernel node replaced fanned its sinks
     // out: serialIn, midi, bytes, buttons. The order matters on the NES, where MIDI and bytes land in the same FIFO
@@ -208,12 +208,15 @@ struct GameBoy : SystemNode<GameBoy, SameBoySystem> {
         return 8;
     }
 
-    // MI.OUT (design section 3.2): the bytes the core captured on its serial port this block, each at frame 0, as the
-    // engine's gather once handed them to the kernel, without their frames. Copied, not drained: the core clears the
-    // log at its next prepareForBlock.
-    void writeQueues() noexcept {
+    // MI.OUT (design section 3.2): the bytes the core captured on its serial port this block, each at the frame the core
+    // completed it, which master sync's decoder times its stop by (docs/lsdj-master-sync-fix-design.md). The core runs an
+    // instruction at a time, so it can complete a byte past the block's end; that byte is stamped at the block's last
+    // frame, so a block's messages always lie inside it. Copied, not drained: the core clears the log at its next
+    // prepareForBlock.
+    void writeQueues(std::uint32_t frames) noexcept {
         if (!connected[kSerialOut]) return;
-        for (const auto& entry : core->serialOutLog_) outputs.serialOut.push(0, std::span<const std::uint8_t>(&entry.second, 1));
+        for (const auto& entry : core->serialOutLog_)
+            outputs.serialOut.push(std::min(entry.first, frames - 1), std::span<const std::uint8_t>(&entry.second, 1));
     }
 
     // The coupled group's other members, in member order: the cable the serial callbacks read and write.
````

- [ ] **Step 4: Run the native suite**

Run: `cmake --build build --target retroplug-engine-graph-test -j$(nproc) && build/bin/retroplug-engine-graph-test`
Expected: `All tests passed (10427 assertions in 107 test cases)`.

- [ ] **Step 5: The TS tests hand the kernel frames**

A probe system role records what its ctx holds: each byte's frame beside it, index for index, and only its own
system's. The existing kernel-level literals gain their frames (MI.OUT's a byte every eight frames, master sync's 900
apart, which a later task's decoder reads as two clocks, as the old one does).

````diff
diff --git a/packages/retroplug/test/dsp/lsdj-arduinoboy.test.ts b/packages/retroplug/test/dsp/lsdj-arduinoboy.test.ts
index 0b65911e..25b35bee 100644
--- a/packages/retroplug/test/dsp/lsdj-arduinoboy.test.ts
+++ b/packages/retroplug/test/dsp/lsdj-arduinoboy.test.ts
@@ -15,7 +15,8 @@ import {
 } from "../../src/lsdjArduinoboy";
 import { RoleRegistry } from "../../src/systemRoles";
 import { registerDspRoles } from "../../src/dspRoles";
-import { DspKernel, type BlockInput } from "../../src/dspKernel";
+import { z } from "../../src/configSchema";
+import { DspKernel, type BlockInput, type SystemCtx } from "../../src/dspKernel";
 
 // Feed a run of protocol bytes through a fresh decoder, returning the emitted MIDI messages.
 function decode(bytes: number[]): number[][] {
@@ -139,15 +140,34 @@ test("lsdj-sync mode 7: kernel fans serialOut to the role, which decodes it to e
     project: [{ kind: "midi-routing", config: { mode: "sendToAll" } }],
     systems: [{ id: 1, pipeline: [{ kind: "lsdj-sync", config: { mode: "midiOut" } }] }],
   });
-  const frame = (cmd: number) => ({ system: 1, byte: 0x80 | (cmd & 0x7f) });
+  // A byte every eight frames, as SameBoy clocks MI.OUT's port (one bit an audio frame).
+  const framed = (cmd: number, at: number) => ({ system: 1, frame: at, byte: 0x80 | (cmd & 0x7f) });
   const out = k.processBlock({
     ...baseDyn(),
-    serialOut: [frame(0x7d), frame(0x7f), frame(0x70), frame(0x69)], // START, clock, NoteOn ch0 note 0x69
+    serialOut: [framed(0x7d, 7), framed(0x7f, 15), framed(0x70, 23), framed(0x69, 31)], // START, clock, NoteOn ch0 note 0x69
   });
   expect(out.midiOut.map((m) => m.data)).toEqual([[0xfa], [0xf8], [0x90, 0x69, 0x7f]]);
   expect(out.serialIn.length).toBe(0); // mode 7 is a pure decoder — it never feeds LSDj serial-in
 });
 
+test("the kernel hands a system stage its own serial-out bytes, each byte's frame beside it", () => {
+  // Master sync times its stop from its last byte, so the frames must reach the stage index for index.
+  const seen: { bytes: number[]; frames: number[] }[] = [];
+  const reg = new RoleRegistry();
+  reg.registerRole({
+    kind: "probe", category: "feature", schema: z.object({}),
+    dsp: (c: SystemCtx) => void seen.push({ bytes: [...c.serialOut], frames: [...c.serialOutFrames] }),
+  });
+  const k = new DspKernel(reg);
+  k.setSystems({ systems: [{ id: 1, pipeline: [{ kind: "probe", config: {} }] }, { id: 2, pipeline: [] }] });
+  k.processBlock({
+    ...baseDyn(),
+    serialOut: [{ system: 1, frame: 3, byte: 0x10 }, { system: 2, frame: 4, byte: 0x20 }, { system: 1, frame: 900, byte: 0x11 }],
+  });
+  k.processBlock(baseDyn());
+  expect(seen).toEqual([{ bytes: [0x10, 0x11], frames: [3, 900] }, { bytes: [], frames: [] }]);
+});
+
 // --- Master Sync (mode 8, SYNC=LSDJ): each captured byte → one MIDI clock -------------------------
 
 // Run per-block serial-out through a fresh master-sync decoder; each element is one block's bytes.
@@ -200,7 +220,7 @@ test("lsdj-sync mode 8: kernel fans serialOut to the master-sync role → clock
     project: [{ kind: "midi-routing", config: { mode: "sendToAll" } }],
     systems: [{ id: 1, pipeline: [{ kind: "lsdj-sync", config: { mode: "masterSync" } }] }],
   });
-  const out = k.processBlock({ ...baseDyn(), serialOut: [{ system: 1, byte: 5 }, { system: 1, byte: 6 }] });
+  const out = k.processBlock({ ...baseDyn(), serialOut: [{ system: 1, frame: 0, byte: 5 }, { system: 1, frame: 900, byte: 6 }] });
   expect(out.midiOut.map((m) => m.data)).toEqual([[0x90, 5, 0x7f], [0xfa], [0xf8], [0xf8]]);
   expect(out.serialIn.length).toBe(0); // master sync is a pure decoder — no serial-in
 });
diff --git a/packages/retroplug/test/script/kernel-node.test.ts b/packages/retroplug/test/script/kernel-node.test.ts
index 421e5b33..c7b24601 100644
--- a/packages/retroplug/test/script/kernel-node.test.ts
+++ b/packages/retroplug/test/script/kernel-node.test.ts
@@ -97,7 +97,7 @@ test("each sink call goes onto its system's port in emission order, host MIDI ou
   );
   const { block, sent } = blockOf(1024, true, {
     midiIn: [{ frame: 4, data: [0x90, 60, 100] }],
-    [`sys-${0x01020304}.serialOut`]: [{ frame: 0, data: [5] }, { frame: 0, data: [5] }],
+    [`sys-${0x01020304}.serialOut`]: [{ frame: 0, data: [5] }, { frame: 900, data: [5] }],
   });
   node.process(block);
   const at = (port: string) => sent.filter((s) => s.port === port).map((s) => [s.frame, ...s.data, s.flags]);
````

- [ ] **Step 6: Run them to see them fail**

Run: `(cd packages/retroplug && npx tsc --noEmit -p .)`
Expected: FAIL: `frame` is not a known property of `SerialOutEvent`, and `serialOutFrames` does not exist on
`SystemCtx`.

- [ ] **Step 7: Carry the frames through the kernel**

````diff
diff --git a/packages/retroplug/src/dspKernel.ts b/packages/retroplug/src/dspKernel.ts
index e1f99b8d..b4bcbcf7 100644
--- a/packages/retroplug/src/dspKernel.ts
+++ b/packages/retroplug/src/dspKernel.ts
@@ -46,10 +46,12 @@ export interface KeyEvent {
 
 /** One raw serial-out byte a core emitted on its serial port (LSDj MI.OUT), targeted at a system.
  *  A Game Boy node's `serialOut` reaches the kernel node over a feedback wire, so a block's bytes
- *  arrive in the NEXT block (one-block latency); the kernel fans them per-system into `ctx.serialOut`
- *  for the mode-7 decoder. */
+ *  arrive in the NEXT block (one-block latency), and `frame` is where the byte fell in the block that
+ *  produced it: the previous one. The kernel fans them per-system into `ctx.serialOut` and
+ *  `ctx.serialOutFrames` for the MI.OUT and master-sync decoders. */
 export interface SerialOutEvent {
   system: number;
+  frame: number;
   byte: number;
 }
 
@@ -159,10 +161,13 @@ export interface SystemCtx {
   midi: MidiEvent[];
   keys: KeyEvent[];
   buttons: ButtonEvent[];
-  /** The raw serial-out bytes this system emitted LAST block (LSDj MI.OUT), in order: the mode-7
-   *  Arduinoboy decoder's input. Empty unless the system's core has serial-out capture armed and the
+  /** The raw serial-out bytes this system emitted LAST block (LSDj MI.OUT), in order: the MI.OUT and
+   *  master-sync decoders' input. Empty unless the system's core has serial-out capture armed and the
    *  kernel node has its `serialOut` input (a Game Boy). */
   serialOut: number[];
+  /** Each `serialOut` byte's frame in the block that produced it (the previous block), index for index:
+   *  master sync times its stop, a silence, from its last byte. */
+  serialOutFrames: number[];
   block: BlockInfo;
   state: Record<string, unknown>;
   pushSerialIn(frame: number, byte: number): void;
@@ -280,6 +285,7 @@ interface SystemSlot {
   keys: KeyEvent[];
   buttons: ButtonEvent[];
   serialOut: number[];
+  serialOutFrames: number[];
   stages: StageRun[];
 }
 
@@ -403,7 +409,7 @@ export class DspKernel {
     // for a new one would leave that ctx routing into inboxes nothing reads.
     this.inboxes.length = 0;
     for (const sys of struct.systems) {
-      const slot: SystemSlot = { id: sys.id, inbox: [], keys: [], buttons: [], serialOut: [], stages: [] };
+      const slot: SystemSlot = { id: sys.id, inbox: [], keys: [], buttons: [], serialOut: [], serialOutFrames: [], stages: [] };
       sys.pipeline.forEach((stage, stageIndex) => {
         const rt = this.registry.roleType(stage.kind);
         if (!isSystemStage(rt)) return;
@@ -448,6 +454,7 @@ export class DspKernel {
       slot.keys.length = 0;
       slot.buttons.length = 0;
       slot.serialOut.length = 0;
+      slot.serialOutFrames.length = 0;
     }
 
     // Fan this block's keys/buttons to their target system (shared refs — read-only downstream),
@@ -465,7 +472,10 @@ export class DspKernel {
     const serialOut = b.serialOut;
     for (let i = 0; i < serialOut.length; i++) {
       const slot = this.slotById.get(serialOut[i].system);
-      if (slot) slot.serialOut.push(serialOut[i].byte);
+      if (slot) {
+        slot.serialOut.push(serialOut[i].byte);
+        slot.serialOutFrames.push(serialOut[i].frame);
+      }
     }
 
     // Project scope: routing fans the global midiIn into the positional persistent inboxes. The
@@ -506,6 +516,7 @@ export class DspKernel {
       keys: slot.keys,
       buttons: slot.buttons,
       serialOut: slot.serialOut,
+      serialOutFrames: slot.serialOutFrames,
       block: this.block,
       state: this.stageState(id, stageIndex),
       pushSerialIn: (frame, byte) => this.sink.pushSerialIn(id, frame, byte),
diff --git a/packages/retroplug/src/script/kernelNode.ts b/packages/retroplug/src/script/kernelNode.ts
index abf1620c..be43675d 100644
--- a/packages/retroplug/src/script/kernelNode.ts
+++ b/packages/retroplug/src/script/kernelNode.ts
@@ -122,8 +122,9 @@ function midiEvents(messages: readonly Message[]): MidiEvent[] {
 }
 
 // The block's input, built as the native kernel stage built it: the transport, controllerConnected only when true,
-// midiIn, empty buttons and keys (no host feeds them yet), every system's MI.OUT bytes from last block in slot order,
-// and controllerIn only when a surface sent something. Fresh every block, as the stage's was.
+// midiIn, empty buttons and keys (no host feeds them yet), every system's MI.OUT bytes from last block in slot order
+// (each with its frame in that block), and controllerIn only when a surface sent something. Fresh every block, as the
+// stage's was.
 function blockInput(b: Block, systems: readonly SystemPorts[]): BlockInput {
   const t = b.transport;
   const input = {
@@ -143,8 +144,8 @@ function blockInput(b: Block, systems: readonly SystemPorts[]): BlockInput {
     if (port === null) continue;
     const messages = b.messages(port);
     for (let i = 0; i < messages.length; i++) {
-      const data = messages[i].data;
-      for (let j = 0; j < data.length; j++) serialOut.push({ system: systems[s].id, byte: data[j] });
+      const m = messages[i];
+      for (let j = 0; j < m.data.length; j++) serialOut.push({ system: systems[s].id, frame: m.frame, byte: m.data[j] });
     }
   }
   input.serialOut = serialOut;
````

- [ ] **Step 8: Run the TS suites**

Run: `(cd packages/retroplug && npx tsc --noEmit -p .) && pnpm test dsp/lsdj-arduinoboy && pnpm test script/kernel-node && pnpm test`
Expected: typecheck clean; `# 23 passed` (lsdj-arduinoboy, the probe case among them), `# 11 passed`
(kernel-node), then `# 163 test file(s) passed`.

- [ ] **Step 9: Parity holds**

The kernel node is in the embedded bundle, which the host build regenerates.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity/blocks-tiny-lsdj-master`
Expected: `# 7 passed, 0 skipped, 0 failed`.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity/lsdj-modes`
Expected: `# 9 passed, 0 skipped, 0 failed`.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity/lsdj-sync`
Expected: all pass (MI.OUT and the other LSDj modes, unchanged).

- [ ] **Step 10: Commit**

````bash
git add packages/native/src/host/graph/SystemNodes.cpp packages/native/test/engine-graph/HostIo.test.cpp packages/native/test/engine-graph/KernelNode.test.cpp packages/native/test/engine-graph/SystemNode.test.cpp packages/retroplug/src/dspKernel.ts packages/retroplug/src/script/kernelNode.ts packages/retroplug/test/dsp/lsdj-arduinoboy.test.ts packages/retroplug/test/script/kernel-node.test.ts
git commit -F - <<'EOF'
feat(engine): the Game Boy's serialOut stamps each byte at the frame its core completed it, and the kernel hands the frames to a system stage beside the bytes
EOF
````

---

### Task 2: Capture modes, the firmware's stop, the re-record

The cause (design section 2): with capture armed, the core clocks any transfer waiting on an external clock, one bit
per audio frame. MI.OUT needs that; master sync does not, and a stopped stock LSDj waiting on an external clock for
another master turns slave when clocked, floods its port with the captured bits and ignores START (from boot at 48
kHz). So capture becomes a mode, master sync listens, and its decoder takes the firmware's stop, a silence. Native
first, then the TS side of the mode, then the decoder, then the parity re-record and the docs, in one commit.

**Files:**
- Modify: `packages/native/src/system/sameboy/SameBoySystem.hpp`, `packages/native/src/system/sameboy/SameBoySystem.cpp`, `packages/native/src/host/engine/Engine.hpp`, `packages/native/src/host/engine/Engine.cpp`, `packages/native/src/host/rpc/BackendTypes.hpp`, `packages/native/src/host/rpc/EngineRpcService.hpp`, `packages/native/src/host/rpc/EngineRpcService.cpp`, `packages/retroplug/src/backend.ts`, `packages/retroplug/src/realBackend.ts`, `packages/retroplug/src/systemsStore.ts`, `packages/retroplug/testing/mockBackend.ts`, `packages/retroplug/src/lsdjArduinoboy.ts`, `packages/retroplug/src/dspRoles.ts`, `AGENTS.md`, `docs/lsdj.md`, `docs/modular-graph-design.md`, `docs/modular-graph-phase-b0-design.md`, `docs/modular-graph-phase-b3a-design.md`, `spec/02-native-host.md`, `spec/03-ts-layer.md`, `spec/04-roles-dsp-kernel.md`, `spec/06-build-test.md`, `spec/07-remaining-work.md`
- Test: `packages/native/test/engine-graph/EngineGraph.test.cpp`, `packages/native/test/engine-graph/SystemNode.test.cpp`, `packages/native/test/engine-graph/HostIo.test.cpp`, `packages/native/test/engine-graph/KernelNode.test.cpp`, `packages/retroplug/test/systems/build-spec-wire.test.ts`, `packages/retroplug/test/systems/store-graph.test.ts`, `packages/retroplug/test/dsp/lsdj-arduinoboy.test.ts`, `packages/retroplug/test/script/kernel-node.test.ts`, `packages/retroplug/test-native/lsdj-mastersync.test.ts`, `packages/retroplug/test-native/lsdj-midiout.test.ts`, `packages/retroplug/test-native/lsdj-midiout-build-flag.test.ts`, `packages/retroplug/test-native/parity/render-blocks.test.ts`, `packages/retroplug/test-native/parity/lsdj-modes.test.ts`, `packages/retroplug/test-native/parity/blocks-tiny-lsdj-master.test.ts`, `packages/retroplug/test-native/parity/blocks-tiny-lsdj.test.ts`, `packages/retroplug/test-native/parity/baseline/lsdj-modes.json`, `packages/retroplug/test-native/parity/baseline/blocks-tiny-lsdj-master.json`

- [ ] **Step 1: The native tests name the modes**

The build-spec case builds one Game Boy per mode and refuses a name that is no mode, then drives the live call
(`setSerialOutCapture` through rpcpp, drained into the engine) with a mode, a bad name (refused, nothing changes)
and null (off). A new SystemNode case shows `Listen` never clocks a Game Boy waiting on an external clock (mGB, whose
port `Drive` clocks a byte every eight frames, sends nothing). The existing mGB captures say `Drive`.

````diff
diff --git a/packages/native/test/engine-graph/EngineGraph.test.cpp b/packages/native/test/engine-graph/EngineGraph.test.cpp
index bba12e52..212f44df 100644
--- a/packages/native/test/engine-graph/EngineGraph.test.cpp
+++ b/packages/native/test/engine-graph/EngineGraph.test.cpp
@@ -335,9 +335,10 @@ TEST_CASE("buildCore and setGraph over the RPC layer", "[engine-graph][b2]") {
 }
 
 TEST_CASE("a build spec's serialOutCapture crosses the wire under the name TS sends", "[engine-graph][b2]") {
-    // A loaded LSDj in a MIDI-out mode captures serial out only through this flag, and reflect-cpp skips a
+    // A loaded LSDj in a MIDI-out mode captures serial out only through this field, and reflect-cpp skips a
     // field it does not know, so a name that drifted between realBackend.ts's specParams and
-    // BackendConstructSpec would drop it with nothing failing. The spec is decoded as every host decodes it: a
+    // BackendConstructSpec would drop it with nothing failing. Each mode arrives as itself, and a name that is no
+    // mode refuses the build rather than building a core that captures nothing. The spec is decoded as every host decodes it: a
     // live JS object, through rpcpp's QuickJS codec, into the registered buildCore.
     struct Js {
         JSRuntime* rt  = JS_NewRuntime();
@@ -373,15 +374,44 @@ TEST_CASE("a build spec's serialOutCapture crosses the wire under the name TS se
         JS_FreeValue(js.ctx, answer);
         return built;
     };
-    REQUIRE(buildCore(1, ", serialOutCapture: true"));
-    REQUIRE(buildCore(2, ""));
-    REQUIRE(e.setGraph(request({{1, kGb}, {2, kGb}})).ok);
-    const auto* armed = dynamic_cast<const SameBoySystem*>(e.findSystem(1));
-    const auto* bare = dynamic_cast<const SameBoySystem*>(e.findSystem(2));
-    REQUIRE(armed != nullptr);
-    REQUIRE(bare != nullptr);
-    CHECK(armed->serialOutCaptureEnabled());
-    CHECK_FALSE(bare->serialOutCaptureEnabled());
+    REQUIRE(buildCore(1, ", serialOutCapture: 'drive'"));
+    REQUIRE(buildCore(2, ", serialOutCapture: 'listen'"));
+    REQUIRE(buildCore(3, ""));
+    CHECK_FALSE(buildCore(4, ", serialOutCapture: 'loud'"));
+    REQUIRE(e.setGraph(request({{1, kGb}, {2, kGb}, {3, kGb}})).ok);
+    const auto mode = [&](SystemId id) {
+        const auto* sb = dynamic_cast<const SameBoySystem*>(e.findSystem(id));
+        REQUIRE(sb != nullptr);
+        return sb->serialOutCapture();
+    };
+    CHECK(mode(1) == SerialOutCapture::Drive);
+    CHECK(mode(2) == SerialOutCapture::Listen);
+    CHECK(mode(3) == SerialOutCapture::Off);
+    CHECK_FALSE(e.coreLive(4));
+
+    // The live call takes the same names, and null for off; a name that is no mode is refused and changes nothing.
+    const auto setCapture = [&](SystemId id, const std::string& name) {
+        const std::string src = "({jsonrpc: '2.0', id: 2, method: 'setSerialOutCapture', params: [" + std::to_string(id) + ", " +
+                                name + "]})";
+        JSValue message = JS_Eval(js.ctx, src.c_str(), src.size(), "<setSerialOutCapture>", JS_EVAL_TYPE_GLOBAL);
+        std::optional<rpcpp::QuickJSOut> reply;
+        if (!JS_IsException(message)) reply = server.processMessage(message);
+        JS_FreeValue(js.ctx, message);
+        if (!reply) return false;
+        JSValue answer = reply->materialize(js.ctx);
+        JSValue result = JS_GetPropertyStr(js.ctx, answer, "result");
+        const bool accepted = JS_IsBool(result) && JS_ToBool(js.ctx, result);
+        JS_FreeValue(js.ctx, result);
+        JS_FreeValue(js.ctx, answer);
+        invoker.drainInto(e);
+        return accepted;
+    };
+    CHECK(setCapture(3, "'listen'"));
+    CHECK(mode(3) == SerialOutCapture::Listen);
+    CHECK_FALSE(setCapture(3, "'loud'"));
+    CHECK(mode(3) == SerialOutCapture::Listen);
+    CHECK(setCapture(1, "null"));
+    CHECK(mode(1) == SerialOutCapture::Off);
 }
 
 TEST_CASE("a render into buffers the planned graph does not write is refused", "[engine-graph][b2]") {
diff --git a/packages/native/test/engine-graph/HostIo.test.cpp b/packages/native/test/engine-graph/HostIo.test.cpp
index ad757a13..3b625c3a 100644
--- a/packages/native/test/engine-graph/HostIo.test.cpp
+++ b/packages/native/test/engine-graph/HostIo.test.cpp
@@ -174,7 +174,7 @@ Order stableOrder(const std::vector<std::uint32_t>& frames) {
 // wired to the mirror's bytes input: the core-byte sink sees every byte the port carries. The Game Boy's core.
 SameBoySystem* serialOutToCart(Engine& e) {
     auto core = mgb(1);
-    core->setSerialOutCapture(true);
+    core->setSerialOutCapture(SerialOutCapture::Drive);
     SameBoySystem* const gb = core.get();
     REQUIRE(e.parkCore(std::move(core)));
     REQUIRE(e.parkCore(bliptoaster(2)));
@@ -768,7 +768,7 @@ TEST_CASE("a system node's bytes input reaches the engine's sync trace while it
         std::vector<std::string> lines;
         Engine e(kRate);
         auto core = mgb(1);
-        core->setSerialOutCapture(true);
+        core->setSerialOutCapture(SerialOutCapture::Drive);
         SameBoySystem* const gb = core.get();
         REQUIRE(e.parkCore(std::move(core)));
         REQUIRE(e.parkCore(bliptoaster(2)));
diff --git a/packages/native/test/engine-graph/SystemNode.test.cpp b/packages/native/test/engine-graph/SystemNode.test.cpp
index b19c88c7..00ae4192 100644
--- a/packages/native/test/engine-graph/SystemNode.test.cpp
+++ b/packages/native/test/engine-graph/SystemNode.test.cpp
@@ -684,7 +684,7 @@ TEST_CASE("the Game Boy's serialOut carries what its core captured this block, e
     // it: the core clears it at its next prepareForBlock.
     NodeRig rig(kRate);
     auto core = mgb(1);
-    core->setSerialOutCapture(true);
+    core->setSerialOutCapture(SerialOutCapture::Drive);
     SameBoySystem* const gb = core.get();
     REQUIRE(rig.set(R"({"nodes":[{"id":"gb","type":"rp.sameboy.gb","settings":{"core":)" + std::to_string(rig.park(std::move(core))) +
                     R"(}},{"id":"probe","type":"rp.test.probe"},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[2]}}],
@@ -712,6 +712,29 @@ TEST_CASE("the Game Boy's serialOut carries what its core captured this block, e
     CHECK(captured > 0);
 }
 
+TEST_CASE("a Game Boy whose capture listens is never clocked: mGB, waiting on an external clock, sends nothing",
+          "[engine-graph][system-node][inputs]") {
+    // Listen captures only the bytes a Game Boy clocks out itself (master sync's LSDj). mGB holds its port in
+    // external-clock receive, which Drive clocks a bit an audio frame (the case above); listening, nothing clocks it.
+    NodeRig rig(kRate);
+    auto core = mgb(1);
+    core->setSerialOutCapture(SerialOutCapture::Listen);
+    SameBoySystem* const gb = core.get();
+    REQUIRE(rig.set(R"({"nodes":[{"id":"gb","type":"rp.sameboy.gb","settings":{"core":)" + std::to_string(rig.park(std::move(core))) +
+                    R"(}},{"id":"probe","type":"rp.test.probe"},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[2]}}],
+                        "wires":[{"from":"gb.mix","to":"out.out.1-2"},{"from":"gb.serialOut","to":"probe.bytes"}]})")
+                .ok);
+    Probe& probe = rig.node<Probe>("probe");
+    Lanes graph(2);
+    const rp_transport t{kRate, 120.0, 0.0, 0};
+    for (std::size_t b = 0; b < 160; ++b) {
+        rig.host->processBlock(kBlocks[b % kBlocks.size()], t, graph.ptrs.data(), 2);
+        INFO("block " << b);
+        CHECK(gb->serialOutLog_.empty());
+        CHECK(probe.bytes.empty());
+    }
+}
+
 TEST_CASE("a system node records what its bytes input feeds the core in the engine's sync trace", "[engine-graph][system-node][inputs]") {
     // As the kernel stage recorded its core-bytes sink: the bytes input only, an empty message skipped, and a long
     // MIDI message (which also goes down the core's byte path) not at all.
````

- [ ] **Step 2: Run them to see them fail**

Run: `cmake --build build --target retroplug-engine-graph-test -j$(nproc)`
Expected: FAIL to compile: `SerialOutCapture` is not declared, and `SameBoySystem` has no `serialOutCapture()`.

- [ ] **Step 3: Capture as a mode, natively**

`SerialOutCapture` lives beside `SameBoySystem`; `serialStart` captures under either mode (it reads
`serialOutCaptureEnabled()`), the external-clock path in `writeAudioSample` under `Drive` only. The wire carries the
mode as a string, parsed in one place (`serialOutCaptureMode`), and the config field as its value.

````diff
diff --git a/packages/native/src/host/engine/Engine.cpp b/packages/native/src/host/engine/Engine.cpp
index 43a53b48..faa188b2 100644
--- a/packages/native/src/host/engine/Engine.cpp
+++ b/packages/native/src/host/engine/Engine.cpp
@@ -830,9 +830,13 @@ void Engine::applyConfigField(SystemBase* sys, std::uint8_t field, double value)
         case ConfigField::FastBoot:
             sb->setFastBoot(value != 0.0);  // deferred to the next restart
             break;
-        case ConfigField::SerialOutCapture:
-            sb->setSerialOutCapture(value != 0.0);  // live — arms LSDj MI.OUT serial-out capture
+        case ConfigField::SerialOutCapture: {
+            // Live: the capture mode (LSDj MI.OUT or master sync) as its SerialOutCapture value. Out of range is off.
+            const auto mode = static_cast<std::uint32_t>(value);
+            sb->setSerialOutCapture(mode <= static_cast<std::uint32_t>(SerialOutCapture::Drive) ? static_cast<SerialOutCapture>(mode)
+                                                                                              : SerialOutCapture::Off);
             break;
+        }
         // The display group. Each is value-guarded like the knobs above (the store re-sends the whole
         // role config on every edit, so an unguarded apply would re-push all five on every keystroke)
         // and then re-pushes the group, which is a handful of cheap core setters.
diff --git a/packages/native/src/host/engine/Engine.hpp b/packages/native/src/host/engine/Engine.hpp
index 95806419..733a61c8 100644
--- a/packages/native/src/host/engine/Engine.hpp
+++ b/packages/native/src/host/engine/Engine.hpp
@@ -30,7 +30,7 @@ enum class ConfigField : std::uint8_t {
     Gain = 0, ReloadOnRomChange = 1,
     Model = 2, Highpass = 3, LinkGroup = 4, FastBoot = 5,     // SameBoy
     NesRegion = 6, NesRemoveSpriteLimit = 7,                  // Mesen (NES)
-    SerialOutCapture = 8,                                     // SameBoy (LSDj MI.OUT)
+    SerialOutCapture = 8,                                     // SameBoy (LSDj MI.OUT, master sync): a SerialOutCapture
     NesApuLatencyMs = 9,                                      // Mesen (NES) — APU flush window as latency (ms)
     // SameBoy display group — all live (they land on the next rendered frame), all routed through
     // SameBoySystem::applyDisplayConfig.
diff --git a/packages/native/src/host/rpc/BackendTypes.hpp b/packages/native/src/host/rpc/BackendTypes.hpp
index 0abbdb89..95412a45 100644
--- a/packages/native/src/host/rpc/BackendTypes.hpp
+++ b/packages/native/src/host/rpc/BackendTypes.hpp
@@ -108,9 +108,9 @@ struct BackendConstructSpec {
     // .sav resolution; only the loaded bytes differ. Honoured by SameBoy and by Mesen's NES, GBA and SMS / GG.
     std::optional<rfl::Bytestring> romBytes;
     std::optional<std::string>               settings;  // backend "system"-role config JSON (construct-time)
-    // Arm a Game Boy's serial-out capture (LSDj MI.OUT) before the core first runs, as a later
-    // setSerialOutCapture would. Ignored by every other core.
-    std::optional<bool>                      serialOutCapture;
+    // Arm a Game Boy's serial-out capture before the core first runs, as a later setSerialOutCapture
+    // would: "drive" (LSDj MI.OUT) or "listen" (master sync), absent for off. Ignored by every other core.
+    std::optional<std::string>               serialOutCapture;
 };
 
 // buildCore's answer (docs/modular-graph-phase-b2-design.md section 2.5): the node type the built core is
diff --git a/packages/native/src/host/rpc/EngineRpcService.cpp b/packages/native/src/host/rpc/EngineRpcService.cpp
index c9dcf0e7..44f96a94 100644
--- a/packages/native/src/host/rpc/EngineRpcService.cpp
+++ b/packages/native/src/host/rpc/EngineRpcService.cpp
@@ -74,6 +74,16 @@ SystemBuildSpec toBuildSpec(const BackendConstructSpec& spec) {
     return out;
 }
 
+// A serial-out capture mode as TS names it on the wire (ConstructSpec.serialOutCapture, setSerialOutCapture): absent is
+// off, and an unknown name is no mode at all.
+std::optional<SerialOutCapture> serialOutCaptureMode(const std::optional<std::string>& name) {
+    if (!name) return SerialOutCapture::Off;
+    if (*name == "listen") return SerialOutCapture::Listen;
+    if (*name == "drive") return SerialOutCapture::Drive;
+    std::fprintf(stderr, "[engine] \"%s\" is not a serial-out capture mode\n", name->c_str());
+    return std::nullopt;
+}
+
 } // namespace
 
 EngineRpcService::EngineRpcService(Engine& engine, SystemFactory& factory, QueuedInvoker& invoker)
@@ -103,10 +113,11 @@ std::optional<RpcBuiltCore> EngineRpcService::buildCore(BackendConstructSpec spe
         std::fprintf(stderr, "[engine] buildCore: system %u is still live\n", static_cast<unsigned>(id));
         return std::nullopt;
     }
+    const std::optional<SerialOutCapture> capture = serialOutCaptureMode(spec.serialOutCapture);
+    if (!capture) return std::nullopt;
     auto sys = factory_.build(id, toBuildSpec(spec), engine_.sampleRate());
     if (!sys) return std::nullopt;  // unknown backend / unreadable or unrecognised ROM
-    if (spec.serialOutCapture.value_or(false))
-        if (auto* sb = dynamic_cast<SameBoySystem*>(sys.get())) sb->setSerialOutCapture(true);
+    if (auto* sb = dynamic_cast<SameBoySystem*>(sys.get())) sb->setSerialOutCapture(*capture);
     RpcBuiltCore facts{rp::host::systemNodeType(sys->kind()), 0, 0};
     if (auto* nes = dynamic_cast<MesenNesSystem*>(sys.get())) facts.exportMode = nes->channelExportMode();
     if (auto* sb = dynamic_cast<SameBoySystem*>(sys.get())) facts.linkGroup = sb->config_.linkGroupId;
@@ -460,10 +471,12 @@ bool EngineRpcService::stageControllerIn(std::vector<std::uint8_t> bytes) {
     return invoker_.stageControllerMidi(std::move(bytes));  // a pad press is 3 bytes; the ring carries 4 inline
 }
 
-bool EngineRpcService::setSerialOutCapture(std::uint32_t id, bool on) {
-    // Control-plane arm/disarm through the invoker's config path (SameBoy-only in Engine::applyConfigField;
-    // a no-op on a Mesen/GBA system). The store gates existence on its own list — accept optimistically.
-    invoker_.applyConfigField(id, static_cast<std::uint8_t>(ConfigField::SerialOutCapture), on ? 1.0 : 0.0);
+bool EngineRpcService::setSerialOutCapture(std::uint32_t id, std::optional<std::string> mode) {
+    // Control-plane, through the invoker's config path (SameBoy-only in Engine::applyConfigField; a no-op on a
+    // Mesen/GBA system). The store gates existence on its own list, so a known mode is accepted optimistically.
+    const std::optional<SerialOutCapture> capture = serialOutCaptureMode(mode);
+    if (!capture) return false;
+    invoker_.applyConfigField(id, static_cast<std::uint8_t>(ConfigField::SerialOutCapture), static_cast<double>(*capture));
     return true;
 }
 
diff --git a/packages/native/src/host/rpc/EngineRpcService.hpp b/packages/native/src/host/rpc/EngineRpcService.hpp
index 479f5a44..a4b06a4c 100644
--- a/packages/native/src/host/rpc/EngineRpcService.hpp
+++ b/packages/native/src/host/rpc/EngineRpcService.hpp
@@ -85,9 +85,10 @@ public:
     // The control-surface twin: staged as `controllerIn` rather than musical MIDI, so a headless test
     // can drive a Launchpad app without a Launchpad. Harness-facet only, like stageMidiIn.
     bool            stageControllerIn(std::vector<std::uint8_t> bytes);
-    // Arm/disarm a SameBoy's serial-out capture (LSDj MI.OUT). Control-plane, via the invoker's config
-    // path (ConfigField::SerialOutCapture) — the TS store calls it when a system's lsdj-sync mode is MIDIOUT.
-    bool            setSerialOutCapture(std::uint32_t id, bool on);
+    // Set a SameBoy's serial-out capture: "drive" (LSDj MI.OUT), "listen" (master sync), or absent for off;
+    // false for any other string. Control-plane, via the invoker's config path (ConfigField::SerialOutCapture):
+    // the TS store calls it when a system's lsdj-sync mode changes.
+    bool            setSerialOutCapture(std::uint32_t id, std::optional<std::string> mode);
     // Drain the MIDI-out the kernel emitted (LSDj MI.OUT decoder), accumulated across renderAudio blocks
     // since the last drain. For headless tests; the plugin drains Engine::midiOut() to the DAW directly.
     std::vector<RpcMidiOut> drainMidiOut();
diff --git a/packages/native/src/system/sameboy/SameBoySystem.cpp b/packages/native/src/system/sameboy/SameBoySystem.cpp
index 5ad7b060..8cce43fa 100644
--- a/packages/native/src/system/sameboy/SameBoySystem.cpp
+++ b/packages/native/src/system/sameboy/SameBoySystem.cpp
@@ -283,7 +283,7 @@ void SameBoySystem::onActivate(double sampleRate) {
 
     // Reset the serial shift-register state so a re-activation (e.g. a
     // model-change restart, which routes through onDeactivate→onActivate)
-    // doesn't carry over half-shifted bytes. Serial-out capture is armed
+    // doesn't carry over half-shifted bytes. Serial-out capture's mode is set
     // separately by the host via setSerialOutCapture.
     serialIn_.clear();
     serialBitsRemaining_ = 0;
@@ -618,7 +618,9 @@ void SameBoySystem::writeAudioSample(int16_t left, int16_t right) {
     // the OUT bit must be sampled full-duplex as the GB shifts it during GB_run, which only the
     // per-sample hook sees. Serial IN (mGB / LSDJ KEYBD scancodes) is delivered separately, per byte,
     // by the slave pump in stepIfBelowTarget. Bypassed when a link peer exists (the cable owns that).
-    if (linkPeers_.empty() && serialOutEnabled_ && gb_) {
+    // Drive only: master sync listens, since clocking a stopped LSDj master's wait for another master
+    // makes it a slave that ignores START (SerialOutCapture).
+    if (linkPeers_.empty() && serialOutCapture_ == SerialOutCapture::Drive && gb_) {
         const auto sc = gb_->io_registers[GB_IO_SC];
         if ((sc & 0x81) == 0x80) {
             const bool outBit = (gb_->io_registers[GB_IO_SB] & 0x80) != 0;
diff --git a/packages/native/src/system/sameboy/SameBoySystem.hpp b/packages/native/src/system/sameboy/SameBoySystem.hpp
index 24d37290..a2430473 100644
--- a/packages/native/src/system/sameboy/SameBoySystem.hpp
+++ b/packages/native/src/system/sameboy/SameBoySystem.hpp
@@ -18,6 +18,14 @@
 struct GB_gameboy_s;
 using GB_gameboy_t_fwd = GB_gameboy_s;
 
+// What serial-out capture does (setSerialOutCapture). Listen: capture the bits the Game Boy clocks out itself, an
+// internal-clock transfer (SC=0x81), which is how LSDj as master sync's master sends its clock. Drive: that, and
+// clock a transfer waiting on an external clock (SC=0x80) one bit an audio frame, capturing its bits too: the
+// Arduinoboy's part in MI.OUT, where LSDj is the serial slave. Drive clocks whatever waits, so it is for MI.OUT only:
+// a stopped LSDj in SYNC=LSDj waits on an external clock for another master, and clocked, it takes the idle bytes
+// for that master's clock and stops answering START (docs/lsdj-master-sync-fix-design.md).
+enum class SerialOutCapture : std::uint8_t { Off = 0, Listen = 1, Drive = 2 };
+
 class SameBoySystem final : public SystemBase {
 public:
     // Construct from a config + ROM bytes. The ROM data is moved in (the
@@ -146,13 +154,15 @@ public:
     bool serialBitFromPeer() const;
     void serialBroadcastBit() const;
 
-    // True when serial-out capture is armed. The per-bit serial callback reads
-    // this to decide whether to accumulate LSDJ's outgoing bytes.
-    bool serialOutCaptureEnabled() const { return serialOutEnabled_; }
-    // Arm/disarm serial-out capture. The host drives the gate through this
-    // (e.g. when a system's TS lsdj-sync mode is MIDIOUT). Stays armed across a
-    // model-change restart (onActivate no longer clears it).
-    void setSerialOutCapture(bool on) { serialOutEnabled_ = on; }
+    // True when serial-out capture is armed, in either mode. The per-bit serial
+    // callback reads this to decide whether to accumulate LSDJ's outgoing bytes.
+    bool serialOutCaptureEnabled() const { return serialOutCapture_ != SerialOutCapture::Off; }
+    SerialOutCapture serialOutCapture() const { return serialOutCapture_; }
+    // Set serial-out capture's mode. The host drives it through this (the TS
+    // store picks it from a system's lsdj-sync mode: Drive for MI.OUT, Listen for
+    // master sync). Stays set across a model-change restart (onActivate no
+    // longer clears it).
+    void setSerialOutCapture(SerialOutCapture mode) { serialOutCapture_ = mode; }
     void captureSerialOutBit(bool bit);
 
     // TODO: Not a fan of the public vars
@@ -226,15 +236,13 @@ public:
     // host to drain (LSDJ MI.OUT → host MIDI).
     std::uint8_t serialOutByte_ = 0;
     int          serialOutBits_ = 0;
-    bool         serialOutEnabled_ = false; // armed via setSerialOutCapture()
-
-    // Diagnostic raw-byte log. Every completed serial-out byte while
-    // serialOutEnabled_ is also appended here keyed by the in-block
-    // sample offset at byte-completion time (approximated by
-    // audioFrameCount_). The CLI drains this per block into a per-system
-    // raw byte log so master-mode verification has ground truth even
-    // when the byte→MIDI decoder doesn't yet translate a given LSDJ
-    // output value. Cleared at block boundaries inside prepareForBlock.
+    SerialOutCapture serialOutCapture_ = SerialOutCapture::Off; // set via setSerialOutCapture()
+
+    // Every serial-out byte completed while capture is armed, keyed by the
+    // in-block sample offset at byte-completion time (audioFrameCount_, which
+    // can run past the block by the core's overshoot). The Game Boy node's
+    // serialOut port carries it each block (host/graph/SystemNodes.cpp).
+    // Cleared at block boundaries inside prepareForBlock.
     std::vector<std::pair<std::uint32_t, std::uint8_t>> serialOutLog_;
 
 private:
````

- [ ] **Step 4: Run the native suite**

Run: `cmake --build build --target retroplug-engine-graph-test -j$(nproc) && build/bin/retroplug-engine-graph-test`
Expected: `All tests passed`, 108 test cases (the kernel's digests still as recorded: its decoder has not changed).

- [ ] **Step 5: The TS tests name the modes**

The wire test carries each mode under the build spec's key and through `setSerialOutCapture`, null for off; the store
test walks MI.OUT (`drive`), a rebuild, master sync (`listen`), a rebuild, and MIDI sync (null). `reset` rebuilds
under a new id, which the test follows.

````diff
diff --git a/packages/retroplug/test/systems/build-spec-wire.test.ts b/packages/retroplug/test/systems/build-spec-wire.test.ts
index d7a90546..269b9703 100644
--- a/packages/retroplug/test/systems/build-spec-wire.test.ts
+++ b/packages/retroplug/test/systems/build-spec-wire.test.ts
@@ -37,20 +37,25 @@ const spec = (extra: Partial<ConstructSpec> = {}): ConstructSpec => ({
   ...extra,
 });
 
-test("buildCore carries serialOutCapture under that exact key", () => {
+test("buildCore carries serialOutCapture under that exact key, each mode as its name", () => {
   const { sent, client } = recording();
-  client.buildCore(spec({ serialOutCapture: true }), 7);
-  expect(sent.length).toBe(1);
-  expect(sent[0].method).toBe("buildCore");
-  const params = sent[0].params[0] as Record<string, unknown>;
-  expect(params.id).toBe(7);
-  expect(params.serialOutCapture).toBe(true);
+  client.buildCore(spec({ serialOutCapture: "drive" }), 7);
+  client.buildCore(spec({ serialOutCapture: "listen" }), 8);
+  expect(sent.map((r) => r.method)).toEqual(["buildCore", "buildCore"]);
+  const params = sent.map((r) => r.params[0] as Record<string, unknown>);
+  expect(params.map((p) => [p.id, p.serialOutCapture])).toEqual([[7, "drive"], [8, "listen"]]);
 });
 
-test("buildCore does not arm the capture for a spec without the flag", () => {
+test("buildCore does not arm the capture for a spec without the field", () => {
   const { sent, client } = recording();
   client.buildCore(spec(), 7);
-  client.buildCore(spec({ serialOutCapture: false }), 8);
-  expect(sent.length).toBe(2);
-  for (const r of sent) expect((r.params[0] as Record<string, unknown>).serialOutCapture ?? false).toBe(false);
+  expect(sent.length).toBe(1);
+  expect("serialOutCapture" in (sent[0].params[0] as Record<string, unknown>)).toBe(false);
+});
+
+test("setSerialOutCapture carries the mode, and null for off", () => {
+  const { sent, client } = recording();
+  client.setSerialOutCapture(3, "listen");
+  client.setSerialOutCapture(3, null);
+  expect(sent.map((r) => [r.method, ...r.params])).toEqual([["setSerialOutCapture", 3, "listen"], ["setSerialOutCapture", 3, null]]);
 });
diff --git a/packages/retroplug/test/systems/store-graph.test.ts b/packages/retroplug/test/systems/store-graph.test.ts
index 22a52474..4115a624 100644
--- a/packages/retroplug/test/systems/store-graph.test.ts
+++ b/packages/retroplug/test/systems/store-graph.test.ts
@@ -323,14 +323,22 @@ test("an adopt outside a load places its core at once", () => {
   expect(since(be, 0)).toEqual([{ slots: [id], tap: {} }]);
 });
 
-test("a system in an LSDj MIDI-out mode is built capturing its serial out", () => {
+test("a system in an LSDj MIDI-out mode is built capturing its serial out: MI.OUT drives, master sync listens", () => {
   const { be, sys } = project();
-  const id = sys.addSystem("/roms/lsdj.gb")!;
-  expect(be.buildCalls[be.buildCalls.length - 1].serialOutCapture).toBe(undefined); // midiSync by default
+  let id = sys.addSystem("/roms/lsdj.gb")!;
+  const lastBuild = () => be.buildCalls[be.buildCalls.length - 1].serialOutCapture;
+  const lastLive = () => be.serialOutCaptureCalls[be.serialOutCaptureCalls.length - 1];
+  expect(lastBuild()).toBe(undefined); // midiSync by default
   sys.setRoleConfig(id, "lsdj-sync", { mode: LsdjSyncMode.MidiOut });
-  expect(be.serialOutCaptureCalls[be.serialOutCaptureCalls.length - 1]).toEqual({ id, on: true }); // live
-  sys.reset(id); // a rebuild: the new core is armed at the build
-  expect(be.buildCalls[be.buildCalls.length - 1].serialOutCapture).toBe(true);
+  expect(lastLive()).toEqual({ id, mode: "drive" }); // live
+  id = sys.reset(id)!; // a rebuild, under a new id: the new core is armed at the build
+  expect(lastBuild()).toBe("drive");
+  sys.setRoleConfig(id, "lsdj-sync", { mode: LsdjSyncMode.MasterSync });
+  expect(lastLive()).toEqual({ id, mode: "listen" });
+  id = sys.reset(id)!;
+  expect(lastBuild()).toBe("listen");
+  sys.setRoleConfig(id, "lsdj-sync", { mode: LsdjSyncMode.MidiSync });
+  expect(lastLive()).toEqual({ id, mode: null });
 });
 
 test("a systems store on its own publishes its systems at the stereo routing", () => {
````

- [ ] **Step 6: The mode on the TS side**

`SerialOutCapture` is a type in `backend.ts`; the store picks it from the lsdj-sync mode (`serialOutCaptureFor`), and
the build hook and the live re-arm both send it. The native tests that arm capture directly name their mode
(`drive` for MI.OUT, `listen` for master sync).

````diff
diff --git a/packages/retroplug/src/backend.ts b/packages/retroplug/src/backend.ts
index 7015ab2c..74e3ab70 100644
--- a/packages/retroplug/src/backend.ts
+++ b/packages/retroplug/src/backend.ts
@@ -141,10 +141,11 @@ export interface Backend {
    *  failure. */
   applyRoleConfig(id: number, kind: string, config: Record<string, unknown>): boolean;
 
-  /** Arm/disarm capture of a system's outgoing serial bytes (LSDj MI.OUT). The store calls this when a
-   *  system's `lsdj-sync` mode is (or leaves) MIDIOUT — armed, native feeds the raw bytes to the DSP
-   *  kernel's `ctx.serialOut` for the Arduinoboy decoder. SameBoy-only (a no-op on other cores). */
-  setSerialOutCapture(id: number, on: boolean): boolean;
+  /** Set capture of a system's outgoing serial bytes (SerialOutCapture), or null for off. The store calls this when
+   *  a system's `lsdj-sync` mode changes: armed, native feeds the raw bytes to the DSP kernel's `ctx.serialOut` for
+   *  the MI.OUT and master-sync decoders. False for a mode native does not know. SameBoy-only (a no-op on other
+   *  cores). */
+  setSerialOutCapture(id: number, mode: SerialOutCapture | null): boolean;
 
   // --- Live emulator input ------------------------------------------------
   // The UI→DSP direction for game input: a joypad button transition on the focused system. Native queues
@@ -742,6 +743,12 @@ export interface FileBrowserOpts {
   directory?: boolean;
 }
 
+/** What a Game Boy's serial-out capture does. `listen`: capture the bytes it clocks out itself (LSDj as master sync's
+ *  master). `drive`: that, and clock a transfer waiting on an external clock, capturing its bytes too (the
+ *  Arduinoboy's part in LSDj MI.OUT). Only MI.OUT may drive: a stopped LSDj master, clocked, takes the idle bytes for
+ *  another master's clock and stops answering START (docs/lsdj-master-sync-fix-design.md). */
+export type SerialOutCapture = "listen" | "drive";
+
 /** What TS hands the native builder: concrete paths only — everything is resolved
  *  TS-side before the call. `savPath`/`statePath` are the exact files to load-from +
  *  auto-save-to / boot-from, or `null` for fresh / cold. */
@@ -774,9 +781,9 @@ export interface ConstructSpec {
   /** Seed savestate bytes (a zip-import blob or a duplicate's captured state). When set, native
    *  boots from these instead of reading `statePath`. Rides as a Uint8Array (rfl::Bytestring). */
   stateBytes?: Uint8Array;
-  /** Arm a Game Boy's serial-out capture (LSDj MI.OUT) before the core first runs, as setSerialOutCapture
-   *  would on a running one. Omitted (off) for every system without an lsdj-sync role in a capturing mode. */
-  serialOutCapture?: boolean;
+  /** Arm a Game Boy's serial-out capture before the core first runs, as setSerialOutCapture would on a running
+   *  one. Omitted (off) for every system without an lsdj-sync role in a capturing mode. */
+  serialOutCapture?: SerialOutCapture;
   /** The system's backend ("system"-role) config as JSON, so a loaded non-default setting
    *  (SameBoy model/highpass/…) is applied at CONSTRUCT rather than via a post-build restart
    *  that would nuke the just-restored savestate. Omitted for a fresh build (backend defaults
diff --git a/packages/retroplug/src/realBackend.ts b/packages/retroplug/src/realBackend.ts
index 45ab9304..dba2a8f1 100644
--- a/packages/retroplug/src/realBackend.ts
+++ b/packages/retroplug/src/realBackend.ts
@@ -70,7 +70,7 @@ const specParams = (spec: ConstructSpec, id: number): Record<string, unknown> =>
   if (spec.sramBytes) p.sramBytes = spec.sramBytes;
   if (spec.stateBytes) p.stateBytes = spec.stateBytes;
   if (spec.settings != null) p.settings = spec.settings;
-  if (spec.serialOutCapture) p.serialOutCapture = true;
+  if (spec.serialOutCapture) p.serialOutCapture = spec.serialOutCapture;
   return p;
 };
 
@@ -111,7 +111,7 @@ export function createEmulatorClient(): EmulatorBackend {
     applySystemSetting: (id, key, value) =>
       call("applySystemSetting", id, key, typeof value === "boolean" ? (value ? 1 : 0) : value) as boolean,
     applyRoleConfig: (id, kind, config) => call("applyRoleConfig", id, kind, JSON.stringify(config)) as boolean,
-    setSerialOutCapture: (id, on) => call("setSerialOutCapture", id, on) as boolean,
+    setSerialOutCapture: (id, mode) => call("setSerialOutCapture", id, mode) as boolean,
     pressButton: (id, button, down) => call("pressButton", id, button, down) as boolean,
     stageSystemMidi: (id, bytes) => call("stageSystemMidi", id, bytes) as boolean,
     readState: (id) => bytesOrNull(call("readState", id)),
diff --git a/packages/retroplug/src/systemsStore.ts b/packages/retroplug/src/systemsStore.ts
index 207ca280..0f240b54 100644
--- a/packages/retroplug/src/systemsStore.ts
+++ b/packages/retroplug/src/systemsStore.ts
@@ -10,7 +10,7 @@
 // duplicateSystem / reload orchestration and the DSP list handlers
 // (PluginDSP.cpp:406-458), with every path derived by the pure kernels.
 
-import type { ConstructSpec, ControlPlaneBackend, HostBackend } from "./backend";
+import type { ConstructSpec, ControlPlaneBackend, HostBackend, SerialOutCapture } from "./backend";
 import type { GraphSystem } from "./graph/buildGraph";
 import { EngineGraph } from "./engineGraph";
 import { detectPlatform, romHasBattery, ROM_SNIFF_LEN, SEGA_SNIFF_LEN, defaultCoreFor, type Platform, type Core } from "./platform";
@@ -868,7 +868,8 @@ export class SystemsStore {
         if (rt?.onConstruct) s = rt.onConstruct(s, this.backend, r.config);
       }
     // A core is built with its serial-out capture already armed, so no command has to chase it.
-    return serialOutCaptureFor(roles) ? { ...s, serialOutCapture: true } : s;
+    const capture = serialOutCaptureFor(roles);
+    return capture ? { ...s, serialOutCapture: capture } : s;
   }
 
   // Write a system's live battery to its own `.sav` before its core is destroyed. Every teardown path
@@ -924,10 +925,9 @@ export class SystemsStore {
     return id;
   }
 
-  // Arm/disarm native serial-out capture (LSDj MI.OUT) for a running system from its lsdj-sync mode. Both
-  // LSDj→host MIDI-out modes need it: MidiOut (SYNC=MI.OUT) and MasterSync (SYNC=LSDJ). Called only when the
-  // mode changes (setRoleConfig): a core that is built or rebuilt carries its capture in the build spec instead
-  // (applyConstructHooks). A no-op for a non-LSDj system (no lsdj-sync role → nothing to arm).
+  // Set native serial-out capture for a running system from its lsdj-sync mode (serialOutCaptureFor). Called
+  // only when the mode changes (setRoleConfig): a core that is built or rebuilt carries its capture in the build
+  // spec instead (applyConstructHooks). A no-op for a non-LSDj system (no lsdj-sync role → nothing to arm).
   private syncSerialOutCapture(id: number): void {
     const roles = findById(this.entries, id)?.roles;
     if (!roles?.some((r) => r.kind === "lsdj-sync")) return;
@@ -935,9 +935,12 @@ export class SystemsStore {
   }
 }
 
-// Whether a system with these roles captures its serial out: both LSDj-to-host MIDI-out modes need it,
-// MidiOut (SYNC=MI.OUT) and MasterSync (SYNC=LSDJ).
-function serialOutCaptureFor(roles: RoleInstance[]): boolean {
+// How a system with these roles captures its serial out: both LSDj-to-host MIDI-out modes need it. MidiOut
+// (SYNC=MI.OUT) drives, playing the Arduinoboy that clocks LSDj as the serial slave; MasterSync (SYNC=LSDJ) only
+// listens to the clock LSDj sends as the master. Null for no capture.
+function serialOutCaptureFor(roles: RoleInstance[]): SerialOutCapture | null {
   const mode = (roles.find((r) => r.kind === "lsdj-sync")?.config as { mode?: LsdjSyncMode } | undefined)?.mode;
-  return mode === LsdjSyncMode.MidiOut || mode === LsdjSyncMode.MasterSync;
+  if (mode === LsdjSyncMode.MidiOut) return "drive";
+  if (mode === LsdjSyncMode.MasterSync) return "listen";
+  return null;
 }
diff --git a/packages/retroplug/test-native/lsdj-midiout-build-flag.test.ts b/packages/retroplug/test-native/lsdj-midiout-build-flag.test.ts
index 5bab9b75..d3ca830d 100644
--- a/packages/retroplug/test-native/lsdj-midiout-build-flag.test.ts
+++ b/packages/retroplug/test-native/lsdj-midiout-build-flag.test.ts
@@ -47,9 +47,9 @@ function playMidiOut(build: (spec: ConstructSpec) => ConstructSpec): MidiOutRun
   let spec: ConstructSpec | null = null;
   let liveArms = 0;
   be.buildCore = (s, id) => buildCore((spec = build(s)), id);
-  be.setSerialOutCapture = (id, on) => {
+  be.setSerialOutCapture = (id, mode) => {
     liveArms++;
-    return setSerialOutCapture(id, on);
+    return setSerialOutCapture(id, mode);
   };
   let id: number | null = null;
   try {
@@ -76,7 +76,7 @@ function playMidiOut(build: (spec: ConstructSpec) => ConstructSpec): MidiOutRun
 test("an adopted LSDj in MIDI-out mode is built capturing, and its NoteOns reach the host", () => {
   if (!be.fileExists(ABOY)) skip(`lsdj-midiout-build-flag: aboy LSDj ROM not found at ${ABOY}`);
   const run = playMidiOut((spec) => spec);
-  expect(run.spec.serialOutCapture, "the build flag").toBe(true);
+  expect(run.spec.serialOutCapture, "the build flag").toBe("drive");
   expect(run.liveArms, "setSerialOutCapture calls").toBe(0);
   console.log(`[lsdj-midiout-build-flag] built capturing: ${run.noteOns} NoteOns`);
   expect(run.noteOns).toBeGreaterThan(0);
diff --git a/packages/retroplug/test-native/lsdj-midiout.test.ts b/packages/retroplug/test-native/lsdj-midiout.test.ts
index fa902621..a763c886 100644
--- a/packages/retroplug/test-native/lsdj-midiout.test.ts
+++ b/packages/retroplug/test-native/lsdj-midiout.test.ts
@@ -71,7 +71,7 @@ test("MIDIOUT (mode 7): a real LSDj in SYNC=MI.OUT emits Arduinoboy MIDI the ker
   expect(graph.setPipeline(id, lsdjSync(MIDIOUT))).toBeTruthy();
 
   // Arm serial-out capture (what the store does when a system's lsdj-sync mode is MIDIOUT).
-  expect(be.setSerialOutCapture(id, true)).toBeTruthy();
+  expect(be.setSerialOutCapture(id, "drive")).toBeTruthy();
 
   audio.renderAudio(6000); // reach the song screen
   audio.drainMidiOut(); // discard any boot-time noise
diff --git a/packages/retroplug/test-native/parity/render-blocks.test.ts b/packages/retroplug/test-native/parity/render-blocks.test.ts
index 402d60c8..d52e6f4b 100644
--- a/packages/retroplug/test-native/parity/render-blocks.test.ts
+++ b/packages/retroplug/test-native/parity/render-blocks.test.ts
@@ -188,7 +188,7 @@ test("MIDI out through renderBlocks is what renderAudio + drainMidiOut yields, t
     ).toBeTruthy();
     expect(graph.setProjectStages([{ kind: "midi-routing", config: { mode: "sendToAll" } }])).toBeTruthy();
     expect(graph.setPipeline(id, [{ kind: "lsdj-sync", config: { mode: "midiOut" } }])).toBeTruthy();
-    expect(be.setSerialOutCapture(id, true)).toBeTruthy();
+    expect(be.setSerialOutCapture(id, "drive")).toBeTruthy();
     render(6000); // reach the song screen; its output is not compared
     const out: Msg[] = [];
     const tap = () => {
diff --git a/packages/retroplug/testing/mockBackend.ts b/packages/retroplug/testing/mockBackend.ts
index 344a3b8a..ecac2325 100644
--- a/packages/retroplug/testing/mockBackend.ts
+++ b/packages/retroplug/testing/mockBackend.ts
@@ -4,7 +4,7 @@
 // "disk". This is what lets the whole application layer be tested with `tjs run`
 // and nothing else.
 
-import type { ApuState, ApuSquareState, Backend, BreakHitBatch, BuiltCore, SetGraphRequest, SetGraphResult, BreakInfo, Breakpoint, CallFrame, ConstructSpec, CoreTransportStats, CpuRegister, DebugEvent, DisasmLine, ExpansionAudioState, FileBrowserOpts, FrameData, PngImageData, PpuState, ProfiledFunction, TraceLine, ZipEntry } from "../src/backend";
+import type { ApuState, ApuSquareState, Backend, BreakHitBatch, BuiltCore, SetGraphRequest, SetGraphResult, BreakInfo, Breakpoint, CallFrame, ConstructSpec, CoreTransportStats, CpuRegister, DebugEvent, DisasmLine, ExpansionAudioState, FileBrowserOpts, FrameData, PngImageData, PpuState, ProfiledFunction, SerialOutCapture, TraceLine, ZipEntry } from "../src/backend";
 import { detectPlatform } from "../src/platform";
 import { savFromJson } from "../src/lsdj";
 
@@ -100,7 +100,7 @@ export class MockBackend implements Backend {
   /** Live-config applies recorded for assertions. */
   readonly applySettingCalls: { id: number; key: string; value: number | boolean }[] = [];
   readonly applyRoleCalls: { id: number; kind: string; config: Record<string, unknown> }[] = [];
-  readonly serialOutCaptureCalls: { id: number; on: boolean }[] = [];
+  readonly serialOutCaptureCalls: { id: number; mode: SerialOutCapture | null }[] = [];
   readonly pressButtonCalls: { id: number; button: number; down: boolean }[] = [];
   /** Control MIDI aimed at one system (backend.stageSystemMidi), in call order. */
   readonly stageSystemMidiCalls: { id: number; bytes: number[] }[] = [];
@@ -632,9 +632,9 @@ export class MockBackend implements Backend {
     return true;
   }
 
-  setSerialOutCapture(id: number, on: boolean): boolean {
+  setSerialOutCapture(id: number, mode: SerialOutCapture | null): boolean {
     this.log.push("setSerialOutCapture");
-    this.serialOutCaptureCalls.push({ id, on });
+    this.serialOutCaptureCalls.push({ id, mode });
     return true;
   }
 
````
````diff
diff --git a/packages/retroplug/test-native/lsdj-mastersync.test.ts b/packages/retroplug/test-native/lsdj-mastersync.test.ts
--- a/packages/retroplug/test-native/lsdj-mastersync.test.ts
+++ b/packages/retroplug/test-native/lsdj-mastersync.test.ts
@@ -61,7 +61,7 @@ test("Master Sync (mode 8): a stock LSDj in SYNC=LSDj clocks the host — byte
 
   expect(graph.setProjectStages(SEND_TO_ALL)).toBeTruthy();
   expect(graph.setPipeline(id, lsdjSync(MASTER_SYNC))).toBeTruthy();
-  expect(be.setSerialOutCapture(id, true)).toBeTruthy(); // what the store does for mode 8
+  expect(be.setSerialOutCapture(id, "listen")).toBeTruthy(); // what the store does for mode 8
 
   audio.renderAudio(6000); // reach the song screen
   audio.drainMidiOut(); // discard boot noise
````

Run: `(cd packages/retroplug && npx tsc --noEmit -p .) && pnpm test systems/build-spec-wire && pnpm test systems/store-graph`
Expected: typecheck clean; `# 3 passed`, then `# 30 passed`. (Before this step's code: `Type '"drive"' is not
assignable to type 'boolean'`.)

- [ ] **Step 7: The decoder's tests**

The unit cases feed the decoder a byte timeline cut into a block partition, one block late as the wire does: every
byte a clock; a silence longer than 200 ms closing the run with one 0xFC and a shorter one not; nothing before a byte
or while stopped; the 0xFC in the first block that starts more than 200 ms after the last byte (block 9 in 1024-frame
blocks, block 8921 in 1-frame ones, for a byte at frame 100); the silence in time, not frames; one timeline decoding to
the same messages in 1-frame, `FL_RAGGED`, `FL_BUFFER`, 1024- and 8192-frame blocks; and leaving master sync and coming
back starting clean. The kernel-node case shows the stop timed from the byte's frame (block 8 for frame 0, block 9 for
frame 1000). The end-to-end case runs a real LSDj from power-on through START, stop and START again at both rates in
three splits; the first case's stop assertion becomes "one 0xFC, then nothing". The KernelNode rig's master sync
becomes a run, a silence and a run.

````diff
diff --git a/packages/retroplug/test/dsp/lsdj-arduinoboy.test.ts b/packages/retroplug/test/dsp/lsdj-arduinoboy.test.ts
index 25b35bee..2183ecfd 100644
--- a/packages/retroplug/test/dsp/lsdj-arduinoboy.test.ts
+++ b/packages/retroplug/test/dsp/lsdj-arduinoboy.test.ts
@@ -168,48 +168,100 @@ test("the kernel hands a system stage its own serial-out bytes, each byte's fram
   expect(seen).toEqual([{ bytes: [0x10, 0x11], frames: [3, 900] }, { bytes: [], frames: [] }]);
 });
 
-// --- Master Sync (mode 8, SYNC=LSDJ): each captured byte → one MIDI clock -------------------------
-
-// Run per-block serial-out through a fresh master-sync decoder; each element is one block's bytes.
-function masterSync(blocks: number[][]): number[][] {
-  const out: number[][] = [];
+// --- Master Sync (mode 8, SYNC=LSDJ): each byte a clock, a silence the stop ------------------------------------
+
+const RATE = 44100;
+/** MASTER_SYNC_STOP_MS (200 ms) at RATE. */
+const STOP_FRAMES = 8820;
+const START = [0xfa];
+const STOP = [0xfc];
+const CLOCK = [0xf8];
+const row = (r: number): number[] => [0x90, r, 0x7f];
+const clocksOf = (n: number): number[][] => Array.from({ length: n }, () => CLOCK);
+
+/** A byte timeline: [absolute frame, byte], ascending. */
+type Timeline = [number, number][];
+/** A playing LSDj's clock bytes at tempo 128, 860 frames apart at 44.1 kHz: `n` of `byte`, the first at `at`. */
+const clocks = (at: number, n: number, byte = 9): Timeline => Array.from({ length: n }, (_, i): [number, number] => [at + i * 860, byte]);
+
+/** `total` frames in blocks of `size` (the last one short). */
+const even = (size: number) => (total: number): number[] => {
+  const out: number[] = [];
+  for (let at = 0; at < total; at += size) out.push(Math.min(size, total - at));
+  return out;
+};
+/** `total` frames in blocks cycling through `sizes` (the last one short). */
+const cycle = (sizes: number[]) => (total: number): number[] => {
+  const out: number[] = [];
+  for (let at = 0, i = 0; at < total; i++) {
+    out.push(Math.min(sizes[i % sizes.length], total - at));
+    at += out[out.length - 1];
+  }
+  return out;
+};
+// The FL-style splits the parity matrix runs (test-native/parity/blocks-tiny.ts): ragged, and a 256-frame buffer
+// split at automation points.
+const FL_RAGGED = [33, 1, 222, 1, 7, 4096, 2];
+const FL_BUFFER = [256, 33, 1, 222, 255, 1, 1, 255, 7, 249];
+
+/** Decode `timeline` in the blocks `split` cuts `total` frames into, fed as the feedback wire feeds the decoder: a
+ *  block's bytes reach it with the next block, at their frames in the block that produced them. Each message with the
+ *  index of the block it went out in. */
+function masterSync(timeline: Timeline, split: (total: number) => number[], total: number, rate = RATE): { block: number; data: number[] }[] {
+  const out: { block: number; data: number[] }[] = [];
   const state: MasterSyncState = {};
-  for (const b of blocks) arduinoboyMasterSyncBlock(b, state, (d) => out.push(d));
+  let start = 0;
+  let next = 0;
+  let bytes: number[] = [];
+  let frames: number[] = [];
+  split(total).forEach((n, block) => {
+    arduinoboyMasterSyncBlock(bytes, frames, { frames: n, sampleRate: rate }, state, (data) => out.push({ block, data }));
+    bytes = [];
+    frames = [];
+    for (; next < timeline.length && timeline[next][0] < start + n; next++) {
+      frames.push(timeline[next][0] - start);
+      bytes.push(timeline[next][1]);
+    }
+    start += n;
+  });
   return out;
 }
+const sent = (out: { data: number[] }[]): number[][] => out.map((m) => m.data);
 
-test("MasterSync: first byte → NoteOn(row) + start then a clock; the rest of the block → clocks", () => {
-  expect(masterSync([[5, 5, 5]])).toEqual([[0x90, 5, 0x7f], [0xfa], [0xf8], [0xf8], [0xf8]]);
+test("MasterSync: every byte is a clock, and the first opens the run with the song-row NoteOn and 0xFA", () => {
+  expect(sent(masterSync(clocks(100, 3, 5), even(1024), 4096))).toEqual([row(5), START, CLOCK, CLOCK, CLOCK]);
 });
 
-test("MasterSync: clocks continue across blocks without re-announcing the start", () => {
-  // First byte announces the run; later blocks are clock-only (one 0xF8 per byte).
-  expect(masterSync([[9], [9], [9]])).toEqual([[0x90, 9, 0x7f], [0xfa], [0xf8], [0xf8], [0xf8]]);
+test("MasterSync: a silence longer than 200 ms closes the run with one 0xFC, a shorter one does not", () => {
+  // 8000 frames between the first two bytes, then 21900 before a third, then the end.
+  const out = sent(masterSync([[100, 9], [8100, 9], [30000, 7]], even(1024), 50000));
+  expect(out).toEqual([row(9), START, CLOCK, CLOCK, STOP, row(7), START, CLOCK, STOP]);
 });
 
-test("MasterSync: empty (between-tick) blocks emit nothing", () => {
-  expect(masterSync([[], [], []])).toEqual([]);
+test("MasterSync: before any byte, and while stopped, nothing is sent", () => {
+  expect(sent(masterSync([], even(1024), 20000))).toEqual([]);
+  expect(sent(masterSync([[100, 9]], even(1024), 40000))).toEqual([row(9), START, CLOCK, STOP]);
 });
 
-// A flooded block (> the idle threshold) is LSDj's stopped-idle link handshake, not tempo clocking.
-const flood = (): number[] => Array.from({ length: 40 }, (_, i) => i & 0x7f);
-
-test("MasterSync: a flooded block after a run → exactly one transport stop (0xFC), no clocks", () => {
-  const out = masterSync([[3], flood(), flood()]);
-  expect(out.slice(0, 3)).toEqual([[0x90, 3, 0x7f], [0xfa], [0xf8]]); // start bookend + first clock
-  expect(out.filter((m) => m[0] === 0xf8).length).toBe(1); // the flood emits NO clocks
-  expect(out.filter((m) => m[0] === 0xfc).length).toBe(1); // one stop, not one per flooded block
-  expect(out[out.length - 1]).toEqual([0xfc]);
+test("MasterSync: the 0xFC goes out in the first block that starts more than 200 ms after the last byte", () => {
+  const stopBlock = (out: { block: number; data: number[] }[]) => out.find((m) => m.data[0] === 0xfc)?.block;
+  // A byte at frame 100: the stop is due after frame 100 + STOP_FRAMES = 8920.
+  expect(stopBlock(masterSync([[100, 9]], even(1024), 20000))).toBe(Math.floor((100 + STOP_FRAMES) / 1024) + 1); // 9216
+  expect(stopBlock(masterSync([[100, 9]], even(1), 10000))).toBe(100 + STOP_FRAMES + 1);
 });
 
-test("MasterSync: a flood before any run emits nothing (idle at rest, never started)", () => {
-  expect(masterSync([flood(), flood()])).toEqual([]);
+test("MasterSync: the silence is time, so 10000 frames is a stop at 44.1 kHz and not at 96 kHz", () => {
+  const timeline: Timeline = [[100, 1], [10100, 1]];
+  expect(sent(masterSync(timeline, even(1024), 12000, 44100))).toEqual([row(1), START, CLOCK, STOP, row(1), START, CLOCK]);
+  expect(sent(masterSync(timeline, even(1024), 12000, 96000))).toEqual([row(1), START, CLOCK, CLOCK]);
 });
 
-test("MasterSync: after a flood stop, a new tempo byte restarts the run with a fresh NoteOn + start", () => {
-  const out = masterSync([[3], flood(), [7]]);
-  const fc = out.findIndex((m) => m[0] === 0xfc);
-  expect(out.slice(fc + 1)).toEqual([[0x90, 7, 0x7f], [0xfa], [0xf8]]);
+test("MasterSync: one timeline decodes to the same messages however the host cuts its blocks", () => {
+  // A run of 80 clocks, LSDj stopped for about a second, a run of 40, and LSDj stopped again.
+  const timeline: Timeline = [...clocks(5000, 80), ...clocks(120000, 40, 3)];
+  const expected = [row(9), START, ...clocksOf(80), STOP, row(3), START, ...clocksOf(40), STOP];
+  for (const split of [even(1), cycle(FL_RAGGED), cycle(FL_BUFFER), even(1024), even(8192)])
+    expect(sent(masterSync(timeline, split, 170000))).toEqual(expected);
 });
 
 test("lsdj-sync mode 8: kernel fans serialOut to the master-sync role → clock + start MIDI", () => {
@@ -221,6 +273,21 @@ test("lsdj-sync mode 8: kernel fans serialOut to the master-sync role → clock
     systems: [{ id: 1, pipeline: [{ kind: "lsdj-sync", config: { mode: "masterSync" } }] }],
   });
   const out = k.processBlock({ ...baseDyn(), serialOut: [{ system: 1, frame: 0, byte: 5 }, { system: 1, frame: 900, byte: 6 }] });
-  expect(out.midiOut.map((m) => m.data)).toEqual([[0x90, 5, 0x7f], [0xfa], [0xf8], [0xf8]]);
-  expect(out.serialIn.length).toBe(0); // master sync is a pure decoder — no serial-in
+  expect(out.midiOut.map((m) => m.data)).toEqual([row(5), START, CLOCK, CLOCK]);
+  expect(out.serialIn.length).toBe(0); // master sync is a pure decoder: no serial-in
+});
+
+test("lsdj-sync: leaving master sync drops its state, so coming back opens a fresh run", () => {
+  const reg = new RoleRegistry();
+  registerDspRoles(reg);
+  const k = new DspKernel(reg);
+  const mode = (m: string) => k.setSystems({ systems: [{ id: 1, pipeline: [{ kind: "lsdj-sync", config: { mode: m } }] }] });
+  const run = (serialOut: BlockInput["serialOut"]) => k.processBlock({ ...baseDyn(), serialOut }).midiOut.map((m) => m.data);
+  mode("masterSync");
+  expect(run([{ system: 1, frame: 100, byte: 5 }])).toEqual([row(5), START, CLOCK]); // a run opens
+  mode("off");
+  run([]);
+  mode("masterSync");
+  // Kept, the stale open run would swallow this run's NoteOn and 0xFA.
+  expect(run([{ system: 1, frame: 0, byte: 7 }])).toEqual([row(7), START, CLOCK]);
 });
diff --git a/packages/retroplug/test/script/kernel-node.test.ts b/packages/retroplug/test/script/kernel-node.test.ts
index c7b24601..c22c8b69 100644
--- a/packages/retroplug/test/script/kernel-node.test.ts
+++ b/packages/retroplug/test/script/kernel-node.test.ts
@@ -113,6 +113,22 @@ test("each sink call goes onto its system's port in emission order, host MIDI ou
   expect(order).toEqual(["sys-2.midi", "midiOut", "sys-4.bytes", "sys-1.serialIn"]);
 });
 
+test("a Game Boy's serial-out bytes reach the kernel at their frames: master sync times its stop from the byte", () => {
+  // A run's one byte at frame 0 or at frame 1000 of a 1024-frame block: the 0xFC goes out in the first block that
+  // starts more than 200 ms (8820 frames) after the byte, which only the frame the node hands across can tell.
+  const stopsIn = (frame: number) => {
+    const node = create(settings({ systems: [{ id: 3, pipeline: [{ kind: "lsdj-sync", config: { mode: "masterSync" } }], serialOut: true }] }));
+    for (let b = 0; b < 16; b++) {
+      const { block, sent } = blockOf(1024, true, b === 0 ? { "sys-3.serialOut": [{ frame, data: [5] }] } : {});
+      node.process(block);
+      if (sent.some((s) => s.port === "midiOut" && s.data[s.data.length - 1] === 0xfc)) return b;
+    }
+    return -1;
+  };
+  expect(stopsIn(0)).toBe(8); // the byte 1024 frames before block 0's start: 1024 + 8 * 1024 > 8820
+  expect(stopsIn(1000)).toBe(9); // 24 frames before it: 24 + 9 * 1024 > 8820
+});
+
 test("a system's serial-in bytes go a run of same-frame bytes to a message, each run sent before the next frame's", () => {
   const node = create(
     settings({
````
````diff
diff --git a/packages/retroplug/test-native/lsdj-mastersync.test.ts b/packages/retroplug/test-native/lsdj-mastersync.test.ts
--- a/packages/retroplug/test-native/lsdj-mastersync.test.ts
+++ b/packages/retroplug/test-native/lsdj-mastersync.test.ts
@@ -1,9 +1,12 @@
 // LSDj Master Sync (Arduinoboy "Mode 2 — LSDJ as MIDI Master Sync", lsdj-sync mode 8) — the end-to-end
 // functional proof on a real LSDj core. A stock LSDj set to SYNC=LSDj, once playing, self-clocks as the
 // serial MASTER and streams one byte out its link port per MIDI-clock tick. Native captures those bytes
-// (armed via setSerialOutCapture — the internal-clock serialStart path, same serialOutLog_ MI.OUT uses,
-// no peer needed) and feeds them to the DSP kernel, where the mode-8 decoder turns each byte into a 0xF8
-// clock (+ a song-row NoteOn/0xFA at run start, 0xFC on idle). So the host follows LSDj's tempo.
+// (setSerialOutCapture's `listen`: the internal-clock serialStart path, same serialOutLog_ MI.OUT uses, no
+// peer needed, and never `drive`, which would clock a stopped LSDj's wait for another master and turn it
+// slave) and feeds them to the DSP kernel, where the mode-8 decoder turns each byte into a 0xF8 clock (+ a
+// song-row NoteOn/0xFA at run start) and a 200 ms silence into 0xFC. So the host follows LSDj's tempo. The
+// decoder times the silence on the bytes' own frames, so what it decodes must not depend on how the host cuts
+// its blocks, at either rate: the second case holds it to that.
 //
 // This resolves the "SYNC=LSDJ is a continuous ~4500 bytes/sec raw link protocol" folklore empirically:
 // a stock LSDj-master emits a TEMPO-LOCKED byte stream (~50/sec = ~130 BPM at 24 PPQN, tens/sec — NOT
@@ -14,6 +17,8 @@ import { createRealBackend } from "../src/realBackend";
 import { createAudioDriver } from "../src/audioDriver";
 import { savFrom, type SavInput } from "../src/lsdjSav";
 import { directGraph } from "../src/graph/directGraph";
+import { cycle, even } from "./parity/rig";
+import { FL_RAGGED } from "./parity/blocks-tiny";
 
 // Structure for this file's directly built systems (docs/modular-graph-phase-b2-design.md section 2.4).
 const graph = directGraph();
@@ -89,14 +94,87 @@ test("Master Sync (mode 8): a stock LSDj in SYNC=LSDj clocks the host — byte
   expect(clocks > 20).toBeTruthy();
   expect(clocksPerSec > 10 && clocksPerSec < 500).toBeTruthy();
 
-  // Stop (START toggles): the byte stream goes idle, and the decoder emits a transport stop after the
-  // idle threshold.
+  // Stop (START toggles): LSDj plays on until it sees the press, then goes quiet, and 200 ms of silence is one
+  // transport stop.
   audio.pressButton(id, START, true);
   audio.renderAudio(80);
   audio.pressButton(id, START, false);
   audio.renderAudio(1200);
   const afterStop = messages(audio.drainMidiOut()).map((m) => m[0]);
-  console.log(`[lsdj-mastersync] stop: 0xFC=${afterStop.filter((s) => s === 0xfc).length} suppressedClocks=${afterStop.filter((s) => s === 0xf8).length}`);
-  expect(afterStop.includes(0xfc)).toBeTruthy(); // stopped LSDj floods the idle handshake → one 0xFC
-  expect(afterStop.filter((s) => s === 0xf8).length).toBe(0); // and the flood emits no spurious clocks
+  const stop = afterStop.indexOf(0xfc);
+  console.log(`[lsdj-mastersync] stop: 0xFC=${afterStop.filter((s) => s === 0xfc).length} clocksBefore=${afterStop.slice(0, Math.max(stop, 0)).filter((s) => s === 0xf8).length}`);
+  expect(afterStop.filter((s) => s === 0xfc).length).toBe(1);
+  expect(afterStop.slice(stop + 1).length).toBe(0); // nothing while LSDj is stopped
+  graph.remove(id);
+});
+
+// One run of decoded MIDI as its transport story: "on" (the song-row NoteOn), "start", "stop", and a clock run as its
+// length.
+function story(sent: number[][]): (string | number)[] {
+  const out: (string | number)[] = [];
+  for (const m of sent) {
+    if (m[0] === 0xf8) {
+      if (typeof out[out.length - 1] === "number") (out[out.length - 1] as number)++;
+      else out.push(1);
+    } else out.push(m[0] === 0xfa ? "start" : m[0] === 0xfc ? "stop" : (m[0] & 0xf0) === 0x90 ? "on" : `?${m[0]}`);
+  }
+  return out;
+}
+
+test("Master Sync tells one story at 44.1 and 48 kHz in 1-frame, FL-style ragged and 1024-frame blocks, a restart included", () => {
+  const be = createRealBackend();
+  if (!be.fileExists(LSDJ)) skip(`lsdj-mastersync: LSDj ROM not found at ${LSDJ}`);
+  const audio = createAudioDriver();
+  const splits: [string, (n: number) => number[]][] = [
+    ["1", (n) => even(n, 1)],
+    ["fl", (n) => cycle(n, FL_RAGGED)],
+    ["1024", (n) => even(n, 1024)],
+  ];
+  for (const rate of [44100, 48000]) {
+    expect(audio.setSampleRate(rate)).toBeTruthy();
+    const frames = (ms: number) => Math.round((rate * ms) / 1000);
+    const stories = splits.map(([name, split], i) => {
+      const id = 10 + i;
+      expect(graph.add({ romPath: LSDJ, platform: "gb", core: "sameboy", embeddedRom: "", savPath: null, statePath: null, sramBytes: savFrom(MASTER_SONG) }, id)).toBeTruthy();
+      expect(graph.setProjectStages(SEND_TO_ALL)).toBeTruthy();
+      expect(graph.setPipeline(id, lsdjSync(MASTER_SYNC))).toBeTruthy();
+      expect(be.setSerialOutCapture(id, "listen")).toBeTruthy();
+      const sent: number[][] = [];
+      const render = (ms: number) => {
+        for (const m of audio.renderBlocks(2, split(frames(ms)).map((n) => ({ frames: n }))).midiOut) sent.push(Array.from(m.data));
+      };
+      const tap = () => {
+        render(10);
+        audio.pressButton(id, START, true);
+        render(80);
+        audio.pressButton(id, START, false);
+      };
+      render(6000); // power-on to the song screen
+      tap(); // play
+      render(1500);
+      tap(); // stop
+      render(1200);
+      tap(); // play again
+      render(1500);
+      // One frame more: a block's bytes reach the decoder with the block after it, so without it a split whose last
+      // block is long (a ragged one's can be 4096 frames) would leave that block's clocks undecoded.
+      for (const m of audio.renderBlocks(2, [{ frames: 1 }]).midiOut) sent.push(Array.from(m.data));
+      graph.remove(id);
+      const s = story(sent);
+      console.log(`[lsdj-mastersync] ${rate} ${name}: ${JSON.stringify(s)}`);
+      return s;
+    });
+    // Nothing from the boot; a run, its stop, then a fresh run: no clock while stopped, and the restart opens with its
+    // own NoteOn and 0xFA.
+    const shape = (s: (string | number)[]) => s.map((t) => (typeof t === "number" ? "clocks" : t));
+    for (const s of stories) expect(shape(s)).toEqual(["on", "start", "clocks", "stop", "on", "start", "clocks"]);
+    // About 51 clocks a second at tempo 128, and within one of each other across the splits: the core's own timing
+    // moves by tens of frames with the split, so a byte at a render's edge can land on either side of it.
+    for (const at of [2, 6]) {
+      const counts = stories.map((s) => s[at] as number);
+      for (const n of counts) expect(n > 60 && n < 110).toBeTruthy();
+      expect(Math.max(...counts) - Math.min(...counts) <= 1).toBeTruthy();
+    }
+  }
+  expect(audio.setSampleRate(44100)).toBeTruthy(); // the engine's default, as the next file finds it
 });
````
````diff
diff --git a/packages/native/test/engine-graph/KernelNode.test.cpp b/packages/native/test/engine-graph/KernelNode.test.cpp
--- a/packages/native/test/engine-graph/KernelNode.test.cpp
+++ b/packages/native/test/engine-graph/KernelNode.test.cpp
@@ -543,16 +543,15 @@ TEST_CASE("the kernel node drives each LSDj sync mode, MI.OUT and master sync am
     rig.block({.ppq = 0.0, .playing = true, .midi = {{200, on(0, 31)}, {200, on(0, 7)}}});
     BlockIn playing{.frames = 1024, .ppq = 1024.0 / kBeat, .playing = true};
     rig.run(playing, 30);
-    // MI.OUT: framed protocol bytes, a frame split over two captured bytes; master sync: a run, a quiet block, then a
-    // stopped LSDj's idle flood, then a run again (a playing LSDj's bytes a few hundred frames apart, its flood's 8).
-    Bytes flood;
-    for (int i = 0; i < 40; ++i) flood.push_back(static_cast<std::uint8_t>(i & 0x7f));
+    // MI.OUT: framed protocol bytes, a frame split over two captured bytes; master sync: a run (a playing LSDj's bytes
+    // a few hundred frames apart), a stopped LSDj's silence, whose 200 ms (8820 frames) pass in the ninth quiet block
+    // and send its stop, then a run again.
     playing.ppq = 31 * 1024.0 / kBeat;
     for (auto captured : {std::vector<Captured>{{7, {framed(0x7d), framed(0x7f), framed(0x70), framed(0x69), 0x7f}},
                                                 {8, {5, 5, 5}, 300}},
                           {{7, {0x80, framed(0x74), framed(64)}}, {8, {}}},
-                          {{8, flood}},
-                          {{8, flood}, {7, {framed(0x7e)}}},
+                          {}, {}, {}, {}, {}, {}, {},
+                          {{7, {framed(0x7e)}}},
                           {{8, {7}}}}) {
         playing.serialOut = captured;
         rig.block(playing);
````

- [ ] **Step 8: Run them to see them fail**

Run: `(cd packages/retroplug && npx tsc --noEmit -p .)`
Expected: FAIL: `arduinoboyMasterSyncBlock` expects 3 arguments, got 5.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native lsdj-mastersync`
Expected: FAIL in both cases: with capture listening, a stopped LSDj goes quiet, so the old decoder (which waits for
a flood) never sends 0xFC, and every story is `["on","start",N]`.

- [ ] **Step 9: The firmware's decoder, and its state dropped outside the mode**

````diff
diff --git a/packages/retroplug/src/dspRoles.ts b/packages/retroplug/src/dspRoles.ts
index 89d141a9..8292dd93 100644
--- a/packages/retroplug/src/dspRoles.ts
+++ b/packages/retroplug/src/dspRoles.ts
@@ -184,18 +184,24 @@ const arduinoboyMaster: SystemBehavior = (c) => {
 };
 
 // Master Sync (mode 8, == Arduinoboy firmware Mode 2, SYNC=LSDJ): LSDj self-clocks as the serial master
-// and streams one byte per MIDI-clock tick out its link port; we turn each captured byte into a 0xF8
-// clock (+ a song-row NoteOn/0xFA at run start, 0xFC on idle), so the host follows LSDj's tempo. Unlike
-// mode 7 this runs EVERY block — an empty serial-out block is how the idle stop is detected.
+// and streams one byte per MIDI-clock tick out its link port, which the core captures by listening (never
+// driving its clock); we turn each byte into a 0xF8 clock (+ a song-row NoteOn/0xFA at run start) and a
+// 200 ms silence into one 0xFC, so the host follows LSDj's tempo. Unlike mode 7 this runs EVERY block: a
+// block with no bytes is how the silence passes. Its state is a sub-object the dispatcher drops whenever
+// the mode is not master sync.
 const masterSync: SystemBehavior = (c) => {
-  const st = c.state as MasterSyncState;
-  arduinoboyMasterSyncBlock(c.serialOut, st, (data) => c.emitMidiOut(0, data));
+  const st = c.state as { masterSync?: MasterSyncState };
+  arduinoboyMasterSyncBlock(c.serialOut, c.serialOutFrames, c.block, (st.masterSync ??= {}), (data) => c.emitMidiOut(0, data));
 };
 
 // lsdj-sync: dispatch on the configured mode (LsdjSyncMode). Off(0)/Keyboard(4) emit nothing here — 4
 // needs the host `keys` feed (a later phase). MidiSync's 0xF8 stream carries no 0xFA; the START-arm that
 // begins LSDj is a user action, not part of the clock (only Arduinoboy mode bookends with 0xFA/0xFC).
 const lsdjSync: SystemBehavior = (c) => {
+  // A stage's state survives a config change, so master sync's would go stale while another mode runs: dropped
+  // here, coming back starts clean rather than from a stale run or a stale last byte.
+  const held = c.state as { masterSync?: MasterSyncState };
+  if (held.masterSync !== undefined && c.config.mode !== LsdjSyncMode.MasterSync) held.masterSync = undefined;
   switch (c.config.mode as LsdjSyncMode) {
     case LsdjSyncMode.MidiSync: { // MidiSync
       // Optional auto-arm (autoStart): SYNC=MIDI LSDj only follows the clock once START has parked it in
diff --git a/packages/retroplug/src/lsdjArduinoboy.ts b/packages/retroplug/src/lsdjArduinoboy.ts
index 046c408f..1d2949e5 100644
--- a/packages/retroplug/src/lsdjArduinoboy.ts
+++ b/packages/retroplug/src/lsdjArduinoboy.ts
@@ -151,34 +151,42 @@ export function arduinoboyDecodeSerialOut(bytes: number[], state: ArduinoboyStat
 // transport bookends the firmware sends: a song-row NoteOn + 0xFA on the first byte of a run, 0xFC when
 // the stream goes idle (LSDj stopped).
 
-// The play↔stop discriminator is byte DENSITY, not presence. A PLAYING LSDj master clocks one byte per
-// MIDI tick — sparse, ~1-2 bytes/block even at fast tempos (24 PPQN → tens of clocks/sec). A STOPPED
-// master doesn't go silent: it floods a continuous link handshake (~100+ bytes/block, thousands/sec).
-// So a block with more than this many bytes is the idle flood (LSDj stopped), not tempo clocking — the
-// empirical resolution of the old "SYNC=LSDJ is a ~4500 bytes/sec stream" folklore (that was LSDj idle).
-const MASTER_SYNC_IDLE_FLOOD = 16;
-
-/** Cross-block Master Sync state (persisted in the role's `ctx.state`). */
+// LSDj's clock is the bytes it clocks out itself, which the core captures in its `listen` mode, never `drive`: a
+// stopped LSDj master waits on an external clock for another master, and clocked, it turns slave and ignores START
+// (docs/lsdj-master-sync-fix-design.md). So every byte is a clock, and a stopped LSDj goes quiet, which makes the
+// firmware's stop a silence. A playing master clocks one byte per 24-PPQN tick, 62.5 ms apart at LSDj's slowest tempo
+// (40 BPM), so a silence this long is a stop: about three times that gap. It is measured on the bytes' own frames, so
+// where the 0xFC lands does not depend on how the host cuts its blocks.
+const MASTER_SYNC_STOP_MS = 200;
+
+/** Cross-block Master Sync state (the lsdj-sync stage's `masterSync` sub-object, dropped whenever the mode is not
+ *  master sync, so coming back starts clean). */
 export interface MasterSyncState {
-  started?: boolean; // a run is in progress (0xFA sent; 0xFC pending on the play→stop transition)
+  started?: boolean; // a run is in progress (0xFA sent; 0xFC pending on the silence that ends it)
   channel?: number; // MIDI channel for the song-row NoteOn (default 0)
+  lastAt?: number; // the run's newest byte: frames from the current block's start (negative: an earlier block)
+  lastFrames?: number; // the last call's block length: where this call's bytes, from that block, sit
 }
 
-/** Decode one block's worth of Master Sync serial-out bytes into host MIDI. Call once per block with the
- *  system's `ctx.serialOut`: a sparse block is tempo clocking; a flooded block is LSDj's stopped-idle
- *  handshake (→ one 0xFC on the play→stop edge); an empty block is a between-tick gap (nothing to do). */
-export function arduinoboyMasterSyncBlock(bytes: number[], state: MasterSyncState, emit: MidiEmit): void {
-  const ch = state.channel ?? 0;
-
-  if (bytes.length > MASTER_SYNC_IDLE_FLOOD) {
-    // Idle flood → LSDj has stopped. Emit a single transport stop on the play→stop edge and drop the noise.
-    if (state.started) {
-      emit([0xfc]);
-      state.started = false;
-    }
-    return;
-  }
+/** The block a Master Sync call runs in: its length and rate. */
+export interface MasterSyncBlock {
+  frames: number;
+  sampleRate: number;
+}
 
+/** Decode the Master Sync serial-out bytes that reached this block into host MIDI. Call once per block with the
+ *  system's `ctx.serialOut` and `ctx.serialOutFrames`, which are LAST block's bytes at their frames in it (the
+ *  feedback wire's one block), and this block's length and rate. Each byte is a tempo clock, the first of a run
+ *  opened with LSDj's song row; a run ends with one 0xFC once MASTER_SYNC_STOP_MS pass with no byte. */
+export function arduinoboyMasterSyncBlock(
+  bytes: number[],
+  frames: number[],
+  block: MasterSyncBlock,
+  state: MasterSyncState,
+  emit: MidiEmit,
+): void {
+  const ch = state.channel ?? 0;
+  const from = state.lastFrames ?? block.frames; // the length of the block these bytes came from
   for (let i = 0; i < bytes.length; i++) {
     if (!state.started) {
       // First tick of a run: the byte value is LSDj's song row → NoteOn, then transport start, then this
@@ -188,5 +196,14 @@ export function arduinoboyMasterSyncBlock(bytes: number[], state: MasterSyncStat
       emit([0xfa]);
     }
     emit([0xf8]); // one MIDI clock per tempo byte
+    state.lastAt = frames[i] - from;
+  }
+  // Every byte before this block's start is known: a run whose newest byte lies further back than the stop's
+  // silence has ended.
+  if (state.started && -(state.lastAt ?? 0) > Math.round((block.sampleRate * MASTER_SYNC_STOP_MS) / 1000)) {
+    emit([0xfc]);
+    state.started = false;
   }
+  if (state.started) state.lastAt = (state.lastAt ?? 0) - block.frames;
+  state.lastFrames = block.frames;
 }
````

Run: `(cd packages/retroplug && npx tsc --noEmit -p .) && pnpm test dsp/lsdj-arduinoboy && pnpm test script/kernel-node`
Expected: typecheck clean; `# 24 passed`, then `# 12 passed`.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native lsdj-mastersync`
Expected: `# 2 passed`, printing for every rate and split `["on","start",82,"stop","on","start",80]`, and for the first
case `play: starts=1 clocks=209 noteOns=1 clocks/sec=51.2`, `stop: 0xFC=1 clocksBefore=1`.

- [ ] **Step 10: Re-record the kernel node's LSDj digest**

Run: `cmake --build build --target retroplug-engine-graph-test -j$(nproc) && build/bin/retroplug-engine-graph-test "the kernel node drives each LSDj sync mode*"`
Expected: FAIL with `sent {269, 0, 0, 4, 16, 0, 0xef0d11f87eff9493ULL}, recorded {251, 0, 0, 4, 15, 0, 0x0a10ec5906e1dd97ULL}`:
seven more blocks of MIDI-sync clocks, and the final run's 0xFC once its silence passes, which the old decoder never
sent without a flood. Record the sent values, and say so in the file's header:

````diff
diff --git a/packages/native/test/engine-graph/KernelNode.test.cpp b/packages/native/test/engine-graph/KernelNode.test.cpp
--- a/packages/native/test/engine-graph/KernelNode.test.cpp
+++ b/packages/native/test/engine-graph/KernelNode.test.cpp
@@ -9,7 +9,9 @@
 // sorts it and its output taken as a frame-ordered queue carries it, and found them equal from c323c7a8, where the node
 // was proven equal, to e3de2e46, the last commit before the stage's deletion. The values are the node's output in
 // those comparisons: the B3a prototype recorded them from the same cases, and they hold here as recorded. So a changed
-// value is a change to what the kernel does: a deliberate one is re-recorded from the values a failing case prints.
+// value is a change to what the kernel does: a deliberate one is re-recorded from the values a failing case prints. One
+// was: the LSDj modes case, when master sync's stop became a silence rather than a flood
+// (docs/lsdj-master-sync-fix-design.md).
 //
 // A serial input is counted and hashed byte by byte, each byte as the one-byte message (its frame, flags 0, one byte)
 // the node sent when the values were recorded: it now sends a run of same-frame bytes as one message, which the system
@@ -566,7 +568,7 @@ TEST_CASE("the kernel node drives each LSDj sync mode, MI.OUT and master sync am
     }
     rig.block({.ppq = fast.ppq, .tempo = 174.0, .playing = false, .midi = {{0, on(0, 25)}, {0, on(0, 26)}}});
     rig.run({.frames = 2048, .ppq = fast.ppq}, 2);
-    rig.matches({251, 0, 0, 4, 15, 0, 0x0a10ec5906e1dd97ULL});  // the buttons: autoStart's START press and release
+    rig.matches({269, 0, 0, 4, 16, 0, 0xef0d11f87eff9493ULL});  // the buttons: autoStart's START press and release
     rig.finish();
 }
 
````

Run: `cmake --build build --target retroplug-engine-graph-test -j$(nproc) && build/bin/retroplug-engine-graph-test`
Expected: `All tests passed (10924 assertions in 108 test cases)`.

- [ ] **Step 11: The parity cases: every master-sync case plays**

The 48 kHz cases become audible (`[0, 1]`), and the comments that pinned the defects say what the cases hold now.

````diff
diff --git a/packages/retroplug/test-native/parity/blocks-tiny-lsdj-master.test.ts b/packages/retroplug/test-native/parity/blocks-tiny-lsdj-master.test.ts
index 8853e15e..41062164 100644
--- a/packages/retroplug/test-native/parity/blocks-tiny-lsdj-master.test.ts
+++ b/packages/retroplug/test-native/parity/blocks-tiny-lsdj-master.test.ts
@@ -1,7 +1,7 @@
 // LSDj as the clock master in 1-frame blocks and FL-style ragged splits (blocks-tiny.ts has the lists):
-// lsdj-modes' masterSync scene, LSDj's own clock decoded into host MIDI out, with a known defect pinned
-// (masterSync below). Split from blocks-tiny-lsdj.test.ts so the two run in parallel: together they were the
-// slowest file in the suite. Needs the sibling LSDj ROMs, so these skip in CI.
+// lsdj-modes' masterSync scene, LSDj's own clock decoded into host MIDI out. Split from blocks-tiny-lsdj.test.ts so
+// the two run in parallel: together they were the slowest file in the suite. Needs the sibling LSDj ROMs, so these
+// skip in CI.
 import { runGroup, type ParityCase } from "./group";
 import { TINY_SPLITS } from "./blocks-tiny";
 import { lsdj, lsdjCase, songSav, tap } from "./lsdj";
@@ -9,22 +9,12 @@ import { frames, ROMS, type Blocks } from "./scenes";
 
 /** lsdj-modes' masterSync scene at both of its rates: LSDj as the clock master (SYNC=LSDj, lsdj-sync mode
  *  masterSync), its serial out decoded into host MIDI out. START, 1.5 s playing, START again, then 1.2 s
- *  stopped, so the stopped LSDj's idle flood is in the case after the stop as well as before the first START.
- *
- *  KNOWN DEFECT, pinned as it is (design section 2.5 of modular-graph-phase-b0-design.md): master sync breaks
- *  in small blocks. The decoder (arduinoboyMasterSyncBlock, lsdjArduinoboy.ts) tells a stopped LSDj from a
- *  playing one by how many serial-out bytes one block carries: more than 16 is the stopped LSDj's idle flood,
- *  dropped, with one 0xFC on the play-to-stop edge. A playing LSDj sends a byte a tick and a stopped one about
- *  5000 a second, so whether a block passes 16 depends on the block's length, not on LSDj. In 1-frame blocks
- *  none does: every byte of the flood is decoded as a clock, before the first START and after the stop, no
- *  0xFC is ever sent, and the run the boot's bytes open never closes, so START gets no NoteOn / 0xFA. In `fl`
- *  and `fl256` the flood's blocks fall on both sides of the threshold, so a stopped LSDj opens and closes a run
- *  (NoteOn, 0xFA, clocks, 0xFC) every few blocks. At 44.1 kHz that is 10497 MIDI messages in `1`, 316 in `fl`
- *  and 894 in `fl256`, against 101 in 1024-frame blocks (lsdj-modes' masterSync-44100). At 48 kHz LSDj never
- *  plays (lsdj-modes' masterSync-48000, the rate defect), so those cases are silent and their MIDI out is the
- *  decoded flood alone. Fixing the decoder is its own change, with a deliberate re-record of these cases, and
- *  it may move lsdj-modes' masterSync-44100 and masterSync-48000 as well: the same threshold decides their boot
- *  noise (the 16-byte test that lets a run open before START). */
+ *  stopped. The core only listens to the bytes LSDj clocks out itself, and the decoder sends a clock per byte and
+ *  0xFC after a 200 ms silence, timed on the bytes' frames, so every split here sends what lsdj-modes'
+ *  1024-frame masterSync-44100 sends: the run's NoteOn, 0xFA and clocks, and one 0xFC
+ *  (docs/lsdj-master-sync-fix-design.md). Until that fix these cases pinned two defects: a decoder that counted
+ *  bytes per block, and a core that clocked a stopped LSDj into a slave, flooding its port and, at 48 kHz, keeping
+ *  it from ever playing. */
 function masterSync(sr: number, split: string, blocks: Blocks): ParityCase {
   return lsdjCase(
     `masterSync-${sr}-${split}`,
@@ -32,7 +22,7 @@ function masterSync(sr: number, split: string, blocks: Blocks): ParityCase {
     (rig) => lsdj(rig, ROMS.lsdj, songSav("Lsdj"), { mode: "masterSync" }),
     [...tap(0, sr, blocks), { blocks: blocks(frames(sr, 1500)) }, ...tap(0, sr, blocks), { blocks: blocks(frames(sr, 1200)) }],
     2,
-    sr === 44100 ? [0, 1] : [], // 48 kHz: silent, the rate defect
+    [0, 1],
     sr,
     blocks,
   );
diff --git a/packages/retroplug/test-native/parity/blocks-tiny-lsdj.test.ts b/packages/retroplug/test-native/parity/blocks-tiny-lsdj.test.ts
index 3c27fa07..2fc72f88 100644
--- a/packages/retroplug/test-native/parity/blocks-tiny-lsdj.test.ts
+++ b/packages/retroplug/test-native/parity/blocks-tiny-lsdj.test.ts
@@ -2,7 +2,7 @@
 // the scenes): SYNC=MIDI following the host clock, each tick at its own frame; the Arduinoboy protocol, its
 // clocks at a block's frame 0, its play / stop notes, 0xFA and 0xFC on the transport's edges; MI.OUT, LSDj's
 // serial out decoded into host MIDI out a block later, so the recorded block indices pin that one-block latency
-// at every block size here. Master sync, with a known defect pinned, is blocks-tiny-lsdj-master.test.ts. Tempo
+// at every block size here. Master sync is blocks-tiny-lsdj-master.test.ts. Tempo
 // and transport points after a segment's start sit on 1-frame blocks, where FL puts them (it splits a buffer
 // at each one), and the sync notes on block edges. Needs the sibling LSDj
 // ROMs, so these skip in CI.
diff --git a/packages/retroplug/test-native/parity/lsdj-modes.test.ts b/packages/retroplug/test-native/parity/lsdj-modes.test.ts
index 61113235..1ee458dc 100644
--- a/packages/retroplug/test-native/parity/lsdj-modes.test.ts
+++ b/packages/retroplug/test-native/parity/lsdj-modes.test.ts
@@ -49,13 +49,11 @@ const cases: ParityCase[] = [
     ...tap(),
     play(2000),
   ]),
-  // LSDj as the clock master, captured as MIDI out: start, clocks, the idle-flood stop. The RATE is the cause of
-  // the 48 kHz defect (the sixth in design section 2.5 of modular-graph-phase-b0-design.md: 48 kHz at 1024 frames
-  // is also broken; 44.1 kHz at 512 plays). 1024 frames is chosen separately: at 512 the boot-time bytes stay
-  // under the decoder's 16-bytes-per-block flood threshold and open a run that never closes before START, so
-  // the real run gets no NoteOn / 0xFA. At 44.1 kHz in 1024 it works (about 51 clocks/s, and it plays). That
-  // threshold is the eighth defect there; fixing it may move this case and masterSync-48000 (blocks-tiny-lsdj-master
-  // pins it in small blocks).
+  // LSDj as the clock master, captured as MIDI out: start, clocks (about 51 a second), and the stop a 200 ms
+  // silence sends. The core only listens to the bytes LSDj clocks out itself (docs/lsdj-master-sync-fix-design.md);
+  // until it did, it clocked a stopped LSDj into a slave, which kept this mode from playing at 48 kHz (the sixth
+  // defect in design section 2.5 of modular-graph-phase-b0-design.md), and its decoder counted bytes per block (the
+  // eighth). The two cases keep the block sizes they were recorded with: 1024 frames here, 512 at 48 kHz.
   lsdjCase(
     "masterSync-44100",
     ROMS.lsdj,
@@ -66,10 +64,7 @@ const cases: ParityCase[] = [
     44100,
     by1024,
   ),
-  // KNOWN DEFECT, recorded as it is (design section 2.5): at 48 kHz and above, with serial-out capture armed,
-  // master sync is broken: the only MIDI out is boot noise and nothing follows START, and it is silent
-  // (mode off plays at 48 kHz).
-  lsdjCase("masterSync-48000", ROMS.lsdj, (rig) => lsdj(rig, ROMS.lsdj, songSav("Lsdj"), { mode: "masterSync" }), [...tap(), play(1500), ...tap(), play(1200)], 2, []),
+  lsdjCase("masterSync-48000", ROMS.lsdj, (rig) => lsdj(rig, ROMS.lsdj, songSav("Lsdj"), { mode: "masterSync" }), [...tap(), play(1500), ...tap(), play(1200)]),
 ];
 
 runGroup("lsdj-modes", cases);
````

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity/lsdj-modes`
Expected: FAIL in `masterSync-44100` and `masterSync-48000` only (their audio, RAM and MIDI out moved); the other
seven cases pass.

- [ ] **Step 12: Re-record the two groups, and check what moved**

Keep the old baselines to compare against, then record:

````bash
cp packages/retroplug/test-native/parity/baseline/blocks-tiny-lsdj-master.json /tmp/btlm.before.json
cp packages/retroplug/test-native/parity/baseline/lsdj-modes.json /tmp/lm.before.json
RETROPLUG_RESOURCES_DIR=/workspaces/resources RP_PARITY_UPDATE=1 pnpm test:native parity/blocks-tiny-lsdj-master
RETROPLUG_RESOURCES_DIR=/workspaces/resources RP_PARITY_UPDATE=1 pnpm test:native parity/lsdj-modes
````

Expected: every case `(recording)`, `# 7 passed` and `# 9 passed` (a recording refuses silence on an audible lane,
so the 48 kHz cases' recording is itself the proof they play).

The check, a one-off script (not committed): every changed field must belong to an allowed case.

````python
# /tmp/parity-diff2.py: compare two parity baselines; fail if anything outside the allowed cases moved.
import json, sys
def walk(a, b, path, out):
    if isinstance(a, dict) and isinstance(b, dict):
        for k in sorted(set(a) | set(b)): walk(a.get(k), b.get(k), path + [k], out)
    elif isinstance(a, list) and isinstance(b, list) and len(a) == len(b):
        for i, (x, y) in enumerate(zip(a, b)): walk(x, y, path + [i], out)
    elif a != b: out.append(path)
before, after = json.load(open(sys.argv[1])), json.load(open(sys.argv[2]))
allowed = set(sys.argv[3].split(","))
changed = []; walk(before, after, [], changed)
bad = [p for p in changed if not (len(p) >= 2 and p[0] == "cases" and p[1] in allowed)]
fields = {}
for p in changed:
    if p not in bad: fields.setdefault(p[1], set()).add(p[2] if len(p) > 2 else "")
for c in sorted(fields): print("moved", c, sorted(fields[c]))
for p in bad: print("BAD", "/".join(map(str, p)))
print("unmoved allowed:", sorted(allowed - set(fields)))
sys.exit(1 if bad else 0)
````

Run: `python3 /tmp/parity-diff2.py /tmp/btlm.before.json packages/retroplug/test-native/parity/baseline/blocks-tiny-lsdj-master.json masterSync-44100-1,masterSync-44100-fl,masterSync-44100-fl256,masterSync-48000-1,masterSync-48000-fl,masterSync-48000-fl256 && python3 /tmp/parity-diff2.py /tmp/lm.before.json packages/retroplug/test-native/parity/baseline/lsdj-modes.json masterSync-44100,masterSync-48000`
Expected: exit 0; each of the eight cases `moved <case> ['midiOut', 'ram', 'segments']`, no `BAD` line, and
`unmoved allowed: []` twice. Every one of the eight now records 85 MIDI messages (`midiOut.count`): the song-row
NoteOn, 0xFA, 82 clocks and one 0xFC, where they recorded 10497, 316, 894, 42861, 1000, 3373, 101 and 17.

- [ ] **Step 13: Every parity case, strictly**

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity`
Expected: `# 30 native test file(s) passed`, 250 cases, none skipped.

- [ ] **Step 14: Docs**

B0 design section 2.5 marks the sixth and eighth defects fixed (with the cause) and gives the seventh its lead;
AGENTS.md counts three pinned defects; `spec/06`, `spec/07`, the B3a and main designs, `spec/02`, `spec/03`,
`spec/04` and `docs/lsdj.md` follow.

````diff
diff --git a/AGENTS.md b/AGENTS.md
index 5e440975..da67ff9c 100644
--- a/AGENTS.md
+++ b/AGENTS.md
@@ -317,7 +317,7 @@ order-dependent cases and an ignored stimulus. Three traps, all met building it:
 fixed state (`RETROPLUG_DETERMINISTIC=1`, which the runner sets for `parity/` only; the SMS VDP's random start
 scanline was the last source); mGB ignores MIDI for its first ~1.5 s, so a case sends nothing before
 `BOOT_MS`; and a second instance of one ROM path gets a fresh `-2.sav` battery slot, so two systems that each
-need a battery each need their own copy (`rig.rom(src, prefix)`). Five engine defects are pinned on purpose (design section 2.5 of [docs/modular-graph-phase-b0-design.md](docs/modular-graph-phase-b0-design.md)); fixing one is its own change with a re-record, and the next due is LSDj master sync reading a stopped LSDj as clocks in small blocks, to be fixed right after B3a and before B3b. The fixture groups need
+need a battery each need their own copy (`rig.rom(src, prefix)`). Three engine defects are pinned on purpose (design section 2.5 of [docs/modular-graph-phase-b0-design.md](docs/modular-graph-phase-b0-design.md)); fixing one is its own change with a re-record, as the LSDj master-sync fix was ([docs/lsdj-master-sync-fix-design.md](docs/lsdj-master-sync-fix-design.md)). The fixture groups need
 `RETROPLUG_RESOURCES_DIR` pointed at the sibling resources tree, and CI runs only the 152 fixture-free cases
 (every LSDj case, its MIDI-out and link-cable ones among them, and every controller-in, risa and GBA case is
 fixture-only), so a Phase B step is accepted only on a run with the fixtures present and `RP_FAIL_ON_SKIP=1`.
diff --git a/docs/lsdj.md b/docs/lsdj.md
index 7e30717a..ef9a5aa0 100644
--- a/docs/lsdj.md
+++ b/docs/lsdj.md
@@ -101,7 +101,7 @@ From [lsdj-mastersync.test.ts](../packages/retroplug/test-native/lsdj-mastersync
 ```ts
 graph.setProjectStages([{ kind: "midi-routing", config: { mode: "sendToAll" } }]); // host MIDI to every system
 graph.setPipeline(id, [{ kind: "lsdj-sync", config: { mode: "masterSync" } }]);    // what the kernel runs for `id`
-be.setSerialOutCapture(id, true); // the store arms it for midiOut / masterSync; a DirectGraph caller does it itself
+be.setSerialOutCapture(id, "listen"); // the store sets it ("drive" for midiOut, "listen" for masterSync); a DirectGraph caller does it itself
 ```
 
 The rest of the native-tier surface these tests use:
@@ -573,13 +573,15 @@ mode selected" indicator on the aboy build. Read the SYNC field text (or the
 
 ### Master mode (MI.OUT) verification
 
-A role opts into serial-out capture via `RomRole::wantsSerialOut()`;
-`LsdjSyncRole` enables it when its config is `ArduinoboyMaster`. From a TS test,
-drain the captured bytes with `be.drainSerialOut(id)` (raw GB serial-out — ground
-truth) and `be.drainMidiOut(id)` (the decoder's MIDI output).
+Serial-out capture is a mode the store sets from the `lsdj-sync` mode (`serialOutCaptureFor` in
+[systemsStore.ts](../packages/retroplug/src/systemsStore.ts)): `drive` for MI.OUT, `listen` for master sync.
+The captured bytes reach the DSP kernel over the Game Boy node's `serialOut` wire, each at the frame its
+core completed it, and `audio.drainMidiOut()` returns what the decoders made of them.
 See [lsdj-midiout.test.ts](../packages/retroplug/test-native/lsdj-midiout.test.ts),
 which authors SYNC=MI.OUT with `N` commands, plays, and asserts a transport START, a clock
-stream and NoteOns come back out as host MIDI.
+stream and NoteOns come back out as host MIDI, and
+[lsdj-mastersync.test.ts](../packages/retroplug/test-native/lsdj-mastersync.test.ts), which holds master
+sync to one story (start, clocks, stop, start again) in three block splits at 44.1 and 48 kHz.
 
 ### Synthetic external-clock serial (subtle but load-bearing)
 
@@ -596,7 +598,7 @@ play that role:
   (SameBoy's serial bit callbacks fire only as internal-clock master):
 
   ```cpp
-  if (linkPeers_.empty() && serialOutEnabled_ && gb_) {
+  if (linkPeers_.empty() && serialOutCapture_ == SerialOutCapture::Drive && gb_) {
       const auto sc = gb_->io_registers[GB_IO_SC];
       if ((sc & 0x81) == 0x80) {
           const bool outBit = (gb_->io_registers[GB_IO_SB] & 0x80) != 0;
@@ -606,6 +608,13 @@ play that role:
   }
   ```
 
+  **Drive only, never master sync.** A stopped stock LSDj in SYNC=LSDj also waits on an external clock,
+  for another master. Clocked, it takes the idle `0xFF` bytes for a master's clock, turns slave and
+  ignores START, and every bit clocked is captured: the "idle flood" (one bit a frame, about 5000 bytes a
+  second) master sync once had to detect, and what kept it silent at 48 kHz. So master sync's capture
+  listens, taking only the bytes LSDj clocks out itself (`serialStart`, internal clock), and its decoder's
+  stop is a 200 ms silence ([lsdj-master-sync-fix-design.md](lsdj-master-sync-fix-design.md)).
+
 - **Serial-IN delivery (mGB, LSDJ KEYBD)** lives in `stepIfBelowTarget`, a
   per-byte slave pump: while `SC=0x80` and the FIFO is non-empty, it pops a byte
   and clocks all 8 bits with `GB_serial_set_data_bit` before `GB_run`. This is
diff --git a/docs/modular-graph-design.md b/docs/modular-graph-design.md
index 91a5786a..bb703793 100644
--- a/docs/modular-graph-design.md
+++ b/docs/modular-graph-design.md
@@ -1113,8 +1113,9 @@ kernel's input and output are real wires (host I/O nodes, input ports on every s
 `serialOut`), and the kernel runs unchanged as one node, `rp.dsp.kernel`, in the new script-node runtime, with its
 structure as its settings; the sealed kernel, its fan-out, the kernel RPCs and the native kernel node are gone. B3b
 is next: it splits that node into one protocol node per role, moves `kernelProjection` into `buildGraph`, takes zod
-out of the bundle and deletes the kernel. Between the two, LSDj master sync's stopped-LSDj detector, which B3a's
-tiny-block parity groups pinned as a defect, is fixed as its own change, before B3b ports the decoder (B3a §10).
+out of the bundle and deletes the kernel. Between the two, LSDj master sync was fixed as its own change, before B3b
+ports the decoder ([lsdj-master-sync-fix-design.md](lsdj-master-sync-fix-design.md)): its stopped-LSDj detector, which
+B3a's tiny-block parity groups pinned as a defect, turned out to be reading the core's own clocking of a stopped LSDj.
 
 ### Phase C: nodes own their knowledge
 
diff --git a/docs/modular-graph-phase-b0-design.md b/docs/modular-graph-phase-b0-design.md
index 8bbb8b67..4e7199ff 100644
--- a/docs/modular-graph-phase-b0-design.md
+++ b/docs/modular-graph-phase-b0-design.md
@@ -179,36 +179,34 @@ always passes 8 outputs and cannot hit it; the SDL standalone (2 channels by def
 can. The baseline pins `nes-channelSplit-o2/o4`, `nes-pinSplit-o2` and `nes-stereoPinSplit-o2/o4` as they
 are; fixing it is its own change with a re-record.
 
-A sixth defect is recorded as it is, not fixed (§1): **LSDj master sync is broken at 48 kHz and above while
-serial-out capture is armed.** The RATE is the cause: 48 kHz at 1024 frames is also broken, and 44.1 kHz at 512
-plays. Measured at 48 kHz in 512-frame blocks: all 17 captured MIDI messages fall inside the boot segment
-(boot noise) and nothing follows START, and the render is silent, while mode `off` plays at 48 kHz. The
-cases use 1024 frames at 44.1 kHz for a separate reason: at 512 the boot-time bytes stay under the decoder's
-16-bytes-per-block flood threshold and open a run that never closes before START, so the real run gets no
-NoteOn / 0xFA. At 44.1 kHz in 1024-frame blocks it works as `test-native/lsdj-mastersync.test.ts` expects:
-start, about 51 clocks/s (77 messages in the 1.5 s after the first START) and audio. The baseline records
-`masterSync-44100` (audible) and `masterSync-48000` (silent); fixing it is its own change with a re-record.
+A sixth defect was recorded as it was (§1), and has since been fixed: **LSDj master sync was broken at 48 kHz and
+above while serial-out capture was armed.** Measured at 48 kHz in 512-frame blocks: all 17 captured MIDI messages
+fell inside the boot segment and nothing followed START, and the render was silent, while mode `off` played at 48
+kHz; 44.1 kHz in 1024-frame blocks worked. The cause, found while fixing the eighth defect
+([lsdj-master-sync-fix-design.md](lsdj-master-sync-fix-design.md)): with capture armed, the core clocked any transfer
+waiting on an external clock, one bit per audio frame, which MI.OUT needs and master sync does not. A stopped LSDj
+master waits on one for another master, so, clocked, it turned slave: it flooded its port with the bits we clocked
+(the eighth defect's "idle flood") and ignored START, from boot at 48 kHz and from the first stop at 44.1 kHz.
+Master sync now only listens (capture's `listen` mode, MI.OUT keeping `drive`), and `masterSync-48000` plays,
+re-recorded with the eighth defect's fix.
 
 A seventh defect is recorded as it is, not fixed (§1): **MI.OUT decoding depends on the host sample rate.**
 Measured: the decoded NoteOns are 0x52-0x6D plus program changes the song never sends at 48 kHz, and
 0x69-0x6D at 44.1 kHz; neither matches the song's N values (48-72), so the capture is partly an artefact.
-The `midiOut` case pins what is captured today (at 48 kHz); fixing it is its own change with a re-record.
-
-An eighth defect is recorded as it is, not fixed (§1): **in small host blocks LSDj master sync decodes a
-stopped LSDj as clocks.** The decoder (`arduinoboyMasterSyncBlock`, `lsdjArduinoboy.ts`) calls a block that
-carries more than 16 serial-out bytes (`MASTER_SYNC_IDLE_FLOOD`) the stopped LSDj's idle flood, which it drops,
-with one 0xFC on the play-to-stop edge. A stopped LSDj sends about 5000 bytes a second, so whether a block
-passes 16 depends on the block's length, not on LSDj. No 1-frame block does: every byte of the flood is decoded
-as a clock, before the first START and after the stop, no 0xFC is ever sent, and the run the boot's bytes open
-never closes, so START gets no NoteOn / 0xFA. In the FL-style splits (`blocks-tiny.ts`) the flood's blocks fall
-on both sides of the threshold, so a stopped LSDj opens and closes a run (NoteOn, 0xFA, clocks, 0xFC) every few
-blocks. Measured on `lsdj-modes`' master-sync scene at 44.1 kHz: 10497 MIDI messages in 1-frame blocks, 316 in
-`fl` and 894 in `fl256`, against 101 in 1024-frame blocks (`masterSync-44100`). The same threshold decides the
-boot noise the sixth defect's two cases record (and is why `masterSync-44100` uses 1024-frame blocks), so a fix
-may move them too.
-The baseline pins `blocks-tiny-lsdj-master`'s `masterSync-44100-{1,fl,fl256}` (audible) and
-`masterSync-48000-{1,fl,fl256}` (silent, the sixth defect, so their 42861, 1000 and 3373 messages are the flood
-alone); fixing it is its own change with a re-record.
+The `midiOut` case pins what is captured today (at 48 kHz); fixing it is its own change with a re-record. A
+likely cause, unconfirmed: MI.OUT's capture clocks LSDj one bit per audio frame, so its bit rate is the host's
+sample rate ([lsdj-master-sync-fix-design.md](lsdj-master-sync-fix-design.md) section 2).
+
+An eighth defect was recorded as it was (§1), and has since been fixed: **in small host blocks LSDj master sync
+decoded a stopped LSDj as clocks.** The decoder (`arduinoboyMasterSyncBlock`, `lsdjArduinoboy.ts`) took a block
+carrying more than 16 serial-out bytes (`MASTER_SYNC_IDLE_FLOOD`) for a stopped LSDj's idle flood, dropped it, and
+sent one 0xFC, so what it decoded depended on the block's length, not on LSDj: on `lsdj-modes`' master-sync scene at
+44.1 kHz, 10497 MIDI messages in 1-frame blocks, 316 in `fl` and 894 in `fl256`, against 101 in 1024-frame blocks
+(`masterSync-44100`). The flood was the sixth defect's clocking, so the fix
+([lsdj-master-sync-fix-design.md](lsdj-master-sync-fix-design.md)) removed it at its source and gave the decoder the
+firmware's stop, a 200 ms silence timed on the bytes' own frames. Every master-sync case, `blocks-tiny-lsdj-master`'s
+six and `lsdj-modes`' two, now sends the same 85 messages at both rates and in every split: the song-row NoteOn,
+0xFA, 82 clocks and one 0xFC.
 
 ## 3. The case matrix
 
@@ -229,7 +227,7 @@ suite is 23 test files with `determinism`, `render-blocks` and `comparator`); th
 | `edits-live`, `edits-structure` | gain through a held note, live role knobs; reset, duplicate, add, remove, replace mid-run, a split losing and regaining its single system, a pin split left | edit timing, per-frame gain smoothing, the tap restore; fix 2 is covered by its unit test in `test/systems/bliptoaster-assets.test.ts`, not by this case (duplicate-mid-run duplicates an mGB, which has no construct hooks) |
 | `sync-sms` | smsggdj SYNC IN24 on SMS and GG: start, bpm change, stop, locate, a short block at a loop wrap whose end is off the 24-PPQN tick grid (so its locate shows); at 48 kHz / 512 and at 44.1 kHz / 8192-frame blocks | transport-driven sync |
 | `lsdj-sync` *(fixture)* | SYNC=MIDI at divisors 1/2/4/8 and with autoStart; Arduinoboy at 1024 (divisors 1 and 4) and 512, ending with a transport stop | lsdj-sync's clock modes; the Arduinoboy defect |
-| `lsdj-modes` *(fixture)* | off, keyboard (inert: the role emits nothing), midiMap (clocked by the transport), keyboardMidi, midiPassthrough (raw clocks into a dense SYNC=MIDI song), midiOut (play / stop / play; the sample-rate defect), masterSync at 44.1 kHz (works) and 48 kHz (the defect) | every other lsdj-sync mode; MI.OUT and master clock as MIDI out |
+| `lsdj-modes` *(fixture)* | off, keyboard (inert: the role emits nothing), midiMap (clocked by the transport), keyboardMidi, midiPassthrough (raw clocks into a dense SYNC=MIDI song), midiOut (play / stop / play; the sample-rate defect), masterSync at 44.1 and 48 kHz (the 48 kHz one pinned the sixth defect until the master-sync fix) | every other lsdj-sync mode; MI.OUT and master clock as MIDI out |
 | `lsdj-link` *(fixture)* | leader and follower on one cable, in blocks of 512 and of every other size `blocks-*` uses (64, 199, 1024, 8192 and the cycle) | link-cable sync; lockstep stepping across partitions (the follower's lanes differ in each) |
 | `risa` *(fixture)* | the transport script, also under PinSplit | risa-sync |
 | `gba` *(fixture)* | nanoloop booting under Stereo, and under TwoPerInstance with START pressed (silent until played) | the GBA core at all |
diff --git a/docs/modular-graph-phase-b3a-design.md b/docs/modular-graph-phase-b3a-design.md
index 66737d8a..31c8e396 100644
--- a/docs/modular-graph-phase-b3a-design.md
+++ b/docs/modular-graph-phase-b3a-design.md
@@ -1,7 +1,8 @@
 # Phase B3a: real ports and the script-node runtime
 
 **Date:** 2026-10-25 · **Branch:** `feature/modularize` · **Status:** built (5002c4b4 to 1ed9a283, then the branch
-review's cleanup, §8); B3b not yet built, and the LSDj master-sync fix comes before it (§10).
+review's cleanup, §8); the LSDj master-sync fix landed after it ([lsdj-master-sync-fix-design.md](lsdj-master-sync-fix-design.md),
+§10); B3b not yet built.
 
 Phase B of [modular-graph-design.md](modular-graph-design.md) moves the engine onto the graph in steps,
 each held bit for bit to the B0 parity baseline ([modular-graph-phase-b0-design.md](modular-graph-phase-b0-design.md)).
@@ -599,9 +600,9 @@ none. Its play and stop notes sit on a block's first frame, so a one-block delay
 22ae5cc4). LSDj master sync was recorded in the same splits, at 44.1 and 48 kHz, with its defect pinned (955299e2, B0's
 eighth): in small blocks its decoder reads a stopped LSDj's idle flood as clocks (§10). The pinned cases are
 `blocks-tiny-lsdj-master`'s `masterSync-44100-{1,fl,fl256}`, which play and stop, and `masterSync-48000-{1,fl,fl256}`,
-silent (B0's sixth defect), so their messages are the flood alone. The fix, right after B3a and before B3b, re-records
-these cases, and possibly `lsdj-modes`' `masterSync-44100` and `masterSync-48000`, whose boot noise the same threshold
-decides.
+silent (B0's sixth defect), so their messages are the flood alone. The fix, right after B3a and before B3b, re-recorded
+these cases and `lsdj-modes`' two: the flood was the core clocking a stopped LSDj, which also caused the sixth defect,
+so every master-sync case now plays and sends the same 85 messages ([lsdj-master-sync-fix-design.md](lsdj-master-sync-fix-design.md)).
 
 Segments are short, since 1-frame blocks multiply the block count. B3a is held to strict parity: every
 group, with the fixtures present and `RP_FAIL_ON_SKIP=1`, and every commit of B3a held it bit for bit.
@@ -745,7 +746,7 @@ needed.
 - The `apu-render` span starts where the last script node's `process` ended, which is right while every script node
   runs before the systems, as in B3a; a protocol node placed after a system would need it revisited.
 
-Before B3b starts, the LSDj master-sync fix lands on its own (§10).
+Before B3b started, the LSDj master-sync fix landed on its own (§10).
 
 ## 10. Found while building
 
@@ -777,10 +778,11 @@ Before B3b starts, the LSDj master-sync fix lands on its own (§10).
   bytes (`MASTER_SYNC_IDLE_FLOOD`) the stopped LSDj's idle flood. A stopped LSDj sends a few thousand bytes a second,
   so no block of fewer than about 150 frames at 44.1 kHz ever holds 16, and the handshake decodes as clocks: 10497
   MIDI messages in 1-frame blocks against 101 in 1024-frame ones; and at a fast tempo an 8192-frame block can pass 16
-  while LSDj plays and read as stopped. It is pinned as it is (§7.1). By the user's decision it is fixed right after
-  B3a, before B3b ports the decoder, as its own change re-recording exactly the cases it moves (`lsdj-modes`'
-  `masterSync-44100` and `masterSync-48000` may move too, since the same threshold decides their boot noise). The fix
-  needs a window across blocks, trading spurious clocks at the stop edge against a few milliseconds of clock latency.
+  while LSDj plays and read as stopped. It was pinned as it was (§7.1) and, by the user's decision, fixed right after
+  B3a, before B3b ports the decoder, as its own change ([lsdj-master-sync-fix-design.md](lsdj-master-sync-fix-design.md)).
+  Building that fix found the flood was not LSDj's: the core clocked a stopped LSDj master's wait for another
+  master, which turned it slave, flooded its port and kept it from starting again (and, at 48 kHz, from ever playing,
+  B0's sixth defect). Master sync now only listens, and its stop is a 200 ms silence timed on the bytes' frames.
 - **The NES path cannot see one block of delay at 1-frame blocks.** BlipToaster's output does not change when every
   event of the edge segment moves by one frame (a lone note-on moved by a frame changes it about once every 67 frames
   at 48 kHz), and in 1-frame blocks a frame is exactly one block of latency, so `blocks-tiny-nes`' `1` cases are blind
diff --git a/spec/02-native-host.md b/spec/02-native-host.md
index bd6370da..b8b5fc95 100644
--- a/spec/02-native-host.md
+++ b/spec/02-native-host.md
@@ -208,7 +208,7 @@ DTO worth spelling out — it mirrors the TS `ConstructSpec`:
 | `sramBytes` / `stateBytes` | `optional<Bytestring>` | zip-import seed bytes (win over the on-disk paths) |
 | `romBytes` | `optional<Bytestring>` | ROM bytes to load instead of reading `romPath` (a patched image, e.g. LSDj asset overrides; honoured by SameBoy and by Mesen's NES, GBA and SMS / Game Gear), `romPath` still travelling for the watcher and `.sav` resolution |
 | `settings` | `optional<string>` | the backend "system"-role config JSON, decoded only by the matching backend |
-| `serialOutCapture` | `optional<bool>` | arm a Game Boy's serial-out capture (LSDj MI.OUT) before its first block; ignored by every other core |
+| `serialOutCapture` | `optional<string>` | arm a Game Boy's serial-out capture before its first block: `"drive"` (LSDj MI.OUT) or `"listen"` (master sync); any other name refuses the build; ignored by every other core |
 
 ## `Engine` + `QueuedInvoker`: the mutation path
 
@@ -339,10 +339,15 @@ The order shows only on the NES, where MIDI and bytes land in the same FIFO: a f
 discards the MIDI queued ahead of it. A Game Boy's `midi` input has no effect (SameBoy's `onMidi`
 queues into a list nothing reads); mGB and LSDj hear MIDI through `serialIn`. The Game Boy also has a
 `serialOut` output (bytes): after `finishBlock`, while it is connected, the node copies the bytes its
-core captured on the serial port this block, each a message at frame 0 (the core clears its log at
-its next `prepareForBlock`). Capture runs only while armed (the build-time `serialOutCapture` flag, or
-`setSerialOutCapture`; TS arms it for LSDj's two MIDI-out modes), and then takes a byte every 8 frames
-while the Game Boy holds the port for an external clock, idle bytes included. The port's capacity is
+core captured on the serial port this block, each a message at the frame the core completed it, a byte
+completed past the block's end at its last frame (the core clears its log at its next
+`prepareForBlock`). Capture runs only while armed (the build-time `serialOutCapture`, or
+`setSerialOutCapture`; TS arms it for LSDj's two MIDI-out modes), in one of two modes
+(`SerialOutCapture`): `listen` takes the bytes the Game Boy clocks out itself (LSDj as master sync's
+master), and `drive` also clocks a transfer waiting on an external clock, a bit an audio frame, taking a
+byte every 8 frames while the Game Boy holds the port for one, idle bytes included (the Arduinoboy's part
+in MI.OUT). Only MI.OUT drives: a stopped LSDj master waits on an external clock for another master, and
+clocked, it turns slave and ignores START. The port's capacity is
 fixed at 8256 messages and 8256 bytes, a byte every 8 frames over the largest block a host can run
 (65536 frames) with slack, so the feedback wire to the kernel keeps its front across every swap; a CGB
 clocking the port itself on its fast clock can outrun it in a very long block, and what does not fit is
diff --git a/spec/03-ts-layer.md b/spec/03-ts-layer.md
index 6bae13c3..ac5dfe32 100644
--- a/spec/03-ts-layer.md
+++ b/spec/03-ts-layer.md
@@ -76,7 +76,7 @@ suffix, never looks for a sibling, never classifies.*
 | `romPath` / `embeddedRom` | The ROM file to slurp, or a binary-baked marker (e.g. `"mgb"`) with `romPath = ""` |
 | `platform` / `core` | What the ROM targets + which factory builds it — TS classifies via [`platform.ts`](../packages/retroplug/src/platform.ts) |
 | `savPath` / `statePath` | The exact battery file (load-from + auto-save-to) and savestate to boot from, or `null` |
-| `serialOutCapture?` | Arm a Game Boy's LSDj MI.OUT capture before the core first runs |
+| `serialOutCapture?` | Arm a Game Boy's serial-out capture before the core first runs: `"drive"` (LSDj MI.OUT) or `"listen"` (master sync) |
 | `sramBytes?` / `stateBytes?` | Seed bytes (zip-import blob, carried battery, role-synthesized sav) that override what native reads from disk; `savPath` stays the auto-save target |
 | `settings?` | The system-role config as JSON, applied **at construct** so a restored savestate isn't nuked by a post-build restart |
 
@@ -124,8 +124,8 @@ roles }`, plus the derived `battery` and `built` facts, never serialized); the p
   per-system `setGain` / `setReloadOnRomChange` (→ `applySystemSetting`) and `setRoleConfig`
   ([systemsStore.ts:536](../packages/retroplug/src/systemsStore.ts#L536)), where
   **only a `"system"`-category role's config crosses to the live core** (`applyRoleConfig`);
-  a feature-role's config stays pure TS, but for `lsdj-sync`, whose mode arms or disarms the Game Boy's
-  serial-out capture (`setSerialOutCapture`).
+  a feature-role's config stays pure TS, but for `lsdj-sync`, whose mode sets the Game Boy's serial-out
+  capture (`setSerialOutCapture`: `"drive"` for MI.OUT, `"listen"` for master sync, null otherwise).
 - **`duplicateSystem`** ([systemsStore.ts:298](../packages/retroplug/src/systemsStore.ts#L298))
   is TS orchestration with **no native duplicate method**: it pulls `readState(id)` (state
   includes SRAM), then `buildCore` seeded with those `stateBytes` under a fresh
diff --git a/spec/04-roles-dsp-kernel.md b/spec/04-roles-dsp-kernel.md
index abea5763..9f7ee057 100644
--- a/spec/04-roles-dsp-kernel.md
+++ b/spec/04-roles-dsp-kernel.md
@@ -162,7 +162,7 @@ behaviours over the per-system context:
 | Role | Scope | Behaviour |
 |---|---|---|
 | `mgb` | system | forward every host-MIDI byte verbatim into the system's serial input |
-| `lsdj-sync` | system | dispatches on `config.mode` (LsdjSyncMode): MidiSync (24-PPQN `0xF8` clock, `tempoDivisor`-subdivided), MidiSyncArduinoboy (note-driven play/divisor + `0xFA`/`0xFC` transport bookends), MidiMap (row bytes + `0xFE`), KeyboardMidi (PS/2 scancodes), MidiPassthrough (raw bytes → serial), MidiOut (ArduinoboyMaster: emulator serial-out → decode → `emitMidiOut`), MasterSync (LSDj self-clock → host MIDI); Off/Keyboard emit nothing. Keeps cross-block scratch via `ctx.state`. Has the `lsdjSeedSav` `onConstruct` hook |
+| `lsdj-sync` | system | dispatches on `config.mode` (LsdjSyncMode): MidiSync (24-PPQN `0xF8` clock, `tempoDivisor`-subdivided), MidiSyncArduinoboy (note-driven play/divisor + `0xFA`/`0xFC` transport bookends), MidiMap (row bytes + `0xFE`), KeyboardMidi (PS/2 scancodes), MidiPassthrough (raw bytes → serial), MidiOut (ArduinoboyMaster: emulator serial-out → decode → `emitMidiOut`), MasterSync (LSDj self-clock → host MIDI: a `0xF8` per byte, the first of a run after the song-row NoteOn and `0xFA`, and `0xFC` once 200 ms pass with no byte, timed on the bytes' frames); Off/Keyboard emit nothing. Keeps cross-block scratch via `ctx.state`. Has the `lsdjSeedSav` `onConstruct` hook |
 | `midi-routing` | project | fans the block's global `midiIn` into per-system inboxes, reusing the pure `routeBlock` decision ([midiRouting.ts](../packages/retroplug/src/midiRouting.ts)) |
 
 ## Which config reaches the live core
@@ -188,8 +188,9 @@ role the kernel runs, publishes the graph (`setOnGraphChange` → `ProjectStore.
 kernel node's settings hold the whole `KernelStructure` (the role's config rides inside them); then
 `markDirty` fires the systems `onChange` → the project's `onSystemsChange`
 ([projectStore.ts:210](../packages/retroplug/src/projectStore.ts#L210)) for the structure's
-observers. The core is never told, with one exception: an `lsdj-sync` mode change arms or disarms the
-Game Boy's serial-out capture (`setSerialOutCapture`), which the two MIDI-out modes decode.
+observers. The core is never told, with one exception: an `lsdj-sync` mode change sets the Game Boy's
+serial-out capture (`setSerialOutCapture`), which the two MIDI-out modes decode: `drive` for MI.OUT, where
+the core plays the Arduinoboy that clocks LSDj, and `listen` for master sync, where LSDj clocks itself.
 
 ## The DSP kernel
 
@@ -203,7 +204,7 @@ reads its inputs and writes frame-tagged sinks.
 | Type | Arrives | Contents |
 |---|---|---|
 | `KernelStructure` | at each structure change, via `setSystems`: the kernel node's settings (below) | `{ project?: RoleInstance[], systems: { id, pipeline: RoleInstance[] }[] }` |
-| `BlockInput` | every block, via `processBlock`, built by the kernel node from its inputs | `BlockInfo` (`frames`, `sampleRate`, `tempo`, `ppqStart`, `transport`, `controllerConnected`) + `midiIn` (global), `serialOut` (each Game Boy's MI.OUT bytes of last block), `controllerIn`, `buttons`, `keys` |
+| `BlockInput` | every block, via `processBlock`, built by the kernel node from its inputs | `BlockInfo` (`frames`, `sampleRate`, `tempo`, `ppqStart`, `transport`, `controllerConnected`) + `midiIn` (global), `serialOut` (each Game Boy's MI.OUT bytes of last block, each with its frame in that block), `controllerIn`, `buttons`, `keys` |
 
 **System order is authoritative**: positional MIDI routing maps a channel to a system by its index in
 `systems`, so the list defines both the set and the order. `DspKernel` keeps ONE persistent `Block`
@@ -338,7 +339,8 @@ control's value from frame 0 is non-zero (`startValue`: its start, or a change a
 `rp.host.controller-in` signals a flip); `midiIn`, and `controllerIn` when a surface sent something,
 copied from their ports as `{ frame, data }` with `data` a plain array (a role may keep one across
 blocks); each Game Boy's `serialOut` bytes, concatenated in the order the settings list the systems
-(slot order) and tagged with its id; and empty `buttons` and `keys` (no host feeds them yet). Its
+(slot order) and tagged with its id and each byte's frame in last block; and empty `buttons` and `keys`
+(no host feeds them yet). Its
 `SinkTarget` sends each sink call onto a port, in emission order:
 
 | Sink call | Port |
diff --git a/spec/06-build-test.md b/spec/06-build-test.md
index f507ffed..9ad4c9a5 100644
--- a/spec/06-build-test.md
+++ b/spec/06-build-test.md
@@ -186,14 +186,14 @@ The suite is 30 files: a file per group, and four with no baseline of their own
   passes) gives the files to compare against. An
   intentional audio change re-records the groups it moves in the same commit, and that diff is the record
   of what moved.
-- **Recorded defects.** The baseline pins five engine defects on purpose; fixing each is its own change
+- **Recorded defects.** The baseline pins three engine defects on purpose; fixing each is its own change
   with a deliberate re-record: Arduinoboy MIDI sync plays only in some host block partitions (at divisor
   1 silent at 128 to 768 frames, in 1-frame blocks and in `fl256`, playing in `fl`, most of whose blocks
   carry almost no clock tick; at divisor 4 silent at 1024 too); a single NES under a split routing on a host narrower than the split arms the capture tap and drifts (~25 frames per block at 48 kHz / 512, about 49 ms per
-  second); LSDj master sync is broken at 48 kHz and above while serial-out capture is armed; MI.OUT's
-  decoded notes depend on the host sample rate; in small host blocks master sync decodes a stopped LSDj's
-  idle flood as clocks. Details in design section 2.5
-  ([modular-graph-phase-b0-design.md](../docs/modular-graph-phase-b0-design.md)).
+  second); MI.OUT's decoded notes depend on the host sample rate. Details in design section 2.5
+  ([modular-graph-phase-b0-design.md](../docs/modular-graph-phase-b0-design.md)). Two more were pinned and
+  fixed together: LSDj master sync silent at 48 kHz, and its decoder reading a stopped LSDj as clocks in small
+  blocks ([lsdj-master-sync-fix-design.md](../docs/lsdj-master-sync-fix-design.md)).
 - LSDj, risa, nanoloop and the controller cases need the sibling fixtures (`RETROPLUG_RESOURCES_DIR`), so
   those groups skip in CI, counted in the skip baseline. CI therefore runs only the 152 fixture-free cases
   (every MIDI-out, controller-in, LSDj, risa and GBA case is fixture-only, LSDj's link-cable cases among
diff --git a/spec/07-remaining-work.md b/spec/07-remaining-work.md
index 92673ac9..b3e8527f 100644
--- a/spec/07-remaining-work.md
+++ b/spec/07-remaining-work.md
@@ -124,19 +124,12 @@ every system node, the Game Boy's `serialOut`, `rp.n8.cart-mirror`), the kernel
 node, `rp.dsp.kernel`, in the script host every Engine embeds, and the sealed native kernel node, its
 fan-out, the `dspSetSystems` / `dspLoadKernel` / `compileScript` RPCs and the boot-time kernel load are
 gone. Staging host MIDI allocates nothing within the room reserved at construction (one flat arena per
-staging). What is left, in order:
-
-- **The master-sync fix, before B3b.** The eighth item of design section 2.5 of
-  [modular-graph-phase-b0-design.md](../docs/modular-graph-phase-b0-design.md), the last of its five recorded
-  defects: LSDj master sync's decoder
-  ([lsdjArduinoboy.ts](../packages/retroplug/src/lsdjArduinoboy.ts), `arduinoboyMasterSyncBlock`) calls a
-  block carrying more than `MASTER_SYNC_IDLE_FLOOD` (16) serial-out bytes a stopped LSDj's idle flood, so
-  what it decodes depends on the host block's length: in small blocks a stopped LSDj's flood reads as
-  clocks, and a fast tempo in long blocks could pass 16 and read as stopped. The fix needs a window across
-  blocks, trading spurious clocks at the stop edge against a few milliseconds of clock latency. It is its
-  own change and re-records exactly the cases it moves (`blocks-tiny-lsdj-master`'s six `masterSync-*`, and
-  possibly `lsdj-modes`' `masterSync-44100` and `masterSync-48000`, whose boot noise the same threshold
-  decides), and it lands before B3b ports the decoder into a protocol node.
+staging). After it, LSDj master sync was fixed on its own
+([lsdj-master-sync-fix-design.md](../docs/lsdj-master-sync-fix-design.md)): a Game Boy's serial-out capture is
+a mode, `listen` for master sync and `drive` for MI.OUT, and the decoder sends a clock per byte and a stop after a
+200 ms silence timed on the bytes' frames, which fixed B0's sixth and eighth recorded defects and let a stopped LSDj
+start again. What is left, in order:
+
 - **B3b.** One protocol node per role (`mgb.midi`, `lsdj.sync`, `risa.sync`, `sms.sync`,
   `n8.midi-to-fifo`, the MIDI routing, the Launchpad controller), each with its own ports;
   `kernelProjection`'s special cases (the synthesized `midi-routing` and Launchpad stages, the forced
````

Run: `pnpm check:docs`
Expected: `check-doc-links: 57 file(s) clean` (56 in the prototype, which had no plan file).

- [ ] **Step 15: The TS and plugin suites, then commit**

Run: `pnpm typecheck && pnpm test && pnpm test:plugin`
Expected: clean; `# 163 test file(s) passed`; `# 10 plugin test binary(ies) passed`.

````bash
git add AGENTS.md docs/lsdj.md docs/modular-graph-design.md docs/modular-graph-phase-b0-design.md docs/modular-graph-phase-b3a-design.md spec/02-native-host.md spec/03-ts-layer.md spec/04-roles-dsp-kernel.md spec/06-build-test.md spec/07-remaining-work.md packages/native/src/host/engine/Engine.cpp packages/native/src/host/engine/Engine.hpp packages/native/src/host/rpc/BackendTypes.hpp packages/native/src/host/rpc/EngineRpcService.cpp packages/native/src/host/rpc/EngineRpcService.hpp packages/native/src/system/sameboy/SameBoySystem.cpp packages/native/src/system/sameboy/SameBoySystem.hpp packages/native/test/engine-graph/EngineGraph.test.cpp packages/native/test/engine-graph/HostIo.test.cpp packages/native/test/engine-graph/KernelNode.test.cpp packages/native/test/engine-graph/SystemNode.test.cpp packages/retroplug/src/backend.ts packages/retroplug/src/dspRoles.ts packages/retroplug/src/lsdjArduinoboy.ts packages/retroplug/src/realBackend.ts packages/retroplug/src/systemsStore.ts packages/retroplug/testing/mockBackend.ts packages/retroplug/test-native/lsdj-mastersync.test.ts packages/retroplug/test-native/lsdj-midiout-build-flag.test.ts packages/retroplug/test-native/lsdj-midiout.test.ts packages/retroplug/test-native/parity/baseline/blocks-tiny-lsdj-master.json packages/retroplug/test-native/parity/baseline/lsdj-modes.json packages/retroplug/test-native/parity/blocks-tiny-lsdj-master.test.ts packages/retroplug/test-native/parity/blocks-tiny-lsdj.test.ts packages/retroplug/test-native/parity/lsdj-modes.test.ts packages/retroplug/test-native/parity/render-blocks.test.ts packages/retroplug/test/dsp/lsdj-arduinoboy.test.ts packages/retroplug/test/script/kernel-node.test.ts packages/retroplug/test/systems/build-spec-wire.test.ts packages/retroplug/test/systems/store-graph.test.ts
git commit -F - <<'EOF'
fix(lsdj): master sync listens to the clock LSDj sends instead of clocking a stopped LSDj into a slave, and a 200 ms silence is its stop; B0's sixth and eighth recorded defects fixed, the master-sync parity cases re-recorded
EOF
````

---

### Task 3: Verify everything

The acceptance run on the final tree. Nothing to commit unless a step fails and is fixed; a fix is its own commit,
named for what it fixes.

- [ ] **Step 1: The tree and the build**

Run: `git status --short && cmake --build build -j$(nproc)`
Expected: only ` m deps/sameboy`, then a clean build.

- [ ] **Step 2: The suites**

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity`
Expected: `# 30 native test file(s) passed`, none skipped.

Run: `pnpm test:native parity`
Expected: `# 30 native test file(s) passed`, with `46 case(s) SKIPPED in 10 file(s): parity/blocks-tiny-lsdj(9), parity/blocks-tiny-lsdj-master(6), parity/blocks-tiny-risa(3), parity/controller(1), parity/gba(2), parity/lsdj-link(6), parity/lsdj-modes(8), parity/lsdj-sync(8), parity/render-blocks(1), parity/risa(2)`, as CI runs it.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native`
Expected: `# 154 native test file(s) passed`, the only skips `dsp-bench` and `nes-bench` (instrumentation off outside a
profile build).

Run: `pnpm typecheck && pnpm check:docs && pnpm test && pnpm test:plugin`
Expected: clean; `57 file(s) clean`; `# 163 test file(s) passed`; `# 10 plugin test binary(ies) passed`
(engine-graph `All tests passed (10924 assertions in 108 test cases)`).

- [ ] **Step 3: The thread sanitizer and the CLI harness**

These two were not run on the prototype (B3a's acceptance ran both clean, and this change adds no thread crossing:
the capture mode is set on the control thread only at build, before the core is parked, and live through the command
ring the audio thread drains, as the flag was).

Run: `tools/run-sanitizer.sh thread`
Expected: clean.

Run: `pnpm test:cli-ts`
Expected: PASS.
