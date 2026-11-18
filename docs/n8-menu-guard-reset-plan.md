# The N8 menu guard after a console reset Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The menu guard tells a menu the console's RESET reached from a game, which it cannot today: it reads the FPGA
config block's map_idx, and a reset leaves that at the game's mapper, so the guard streams into the menu until the OS
stops on "ERROR:40". [n8-menu-guard-reset-design.md](../docs/n8-menu-guard-reset-design.md) replaces the signal with
the menu's own answer to `'*t'`, which this plan builds: the fake learns the reset menu, the guard asks the menu, the
job check asks it too, and the documents say so. It closes [spec/07](../spec/07-remaining-work.md)'s "The menu guard
misses a console reset".

**Architecture:** Task 1 is the fake and the red cases. `FakeN8` gains `resetToMenu()` (the menu with the config block
left at the game's mapper, as the real cart showed; each press also starts the cart's parsers afresh), an `error40` flag
(a `'*'` followed by a byte that is not a command, after which the menu stops answering), `menuAnswerDelayMs` (a menu
slow to answer), `syncAckLast` (the last header a sync's ack names), and BlipToaster's ack for bytes that come unframed
while its link is on (`F5 <next> <next>`, as `bliptoaster/src/midi/parse_asm.s` writes it); its menu already skips every
byte that is not a `'*'`, so it answers `'*t'` inside an `F0 .. F7` wrapper. The four red cases are tagged
`[!shouldfail]`, so the suite stays green while they fail for their reason, and a fifth pins that a `'k'` inside an ack
is not the menu's answer. Task 2 is the guard, in two rounds. The first: `N8Link` writes the probe `F0 2A 74 F7`
(`N8Link::kMenuProbe`) into the stream between whole messages (every 250 ms raw, once the framed window has stopped
moving, and every 250 ms at the menu), reads the back-channel every turn with a scan that follows the ack parser's
states (`N8Link::BackChannelScan`: a `0x6B` outside `F5 <next> <last>` is the menu's answer), trips on a `'k'`, holds a
framed stall until a `'k'` or an ack, and leaves the menu when the menu stops answering; the connect seed (`MenuCheck`)
becomes N8Host's `askMenu`, the probe and a 100 ms wait. The config-block read, its forced read, its flushes and its
failure back-off go; the "trusted game second" stays only as the gap before a framed stall that heard an ack is asked
about again. The second round takes what a review of the first found: leaving the menu takes three unanswered probes in
a row, not one; two stall probes that hear nothing at all give the framed window up and probe the link again; the raw
probe waits for a message to end however long it stays open; a flush resets the scan, and a half-read ack is not quiet.
Task 3 is the job check: `N8Host::probeMenu` and a job's first step ask the menu with `askMenu` instead of reading
map_idx, the trip-only `reportMenuState(true)` stays, a raw link is quiet for a read only between two messages, and
`N8Link::noteMenuProbe()` books those probes on the raw stream's running-status tracker; spec/07's finished item goes in
the same commit. Task 4 is the documents.

**Tech Stack:** C++20 with Catch2 (`retroplug-n8-test`, `retroplug-engine-graph-test`, `retroplug-device-mode-test`,
the TSan build of `retroplug-n8-test` in `build-tsan`), the N8 pack's Edio/N8Link/N8Host over `FakeN8`, the repo's own
runners (`pnpm test:n8`, `pnpm test:native n8`), `retroplug-n8-hwtest` (compiled only: it needs the physical console),
and `pnpm check:docs`. No TypeScript changes.

**Spec:** [n8-menu-guard-reset-design.md](../docs/n8-menu-guard-reset-design.md). Its section 6 lists the tasks and
the acceptance; the acceptance run (the serial kit, then the rig check with a RESET press) is the controller's, after
this plan lands.

**Provenance:** every code block below was applied to this worktree at `6a4b3fb72` in this plan's order, test first and
then the implementation, and built and run before the plan was written: linux-x64, GCC 13.3 (`linux-x64-gnu13`), 32
cores, `RETROPLUG_RESOURCES_DIR` unset (no case here needs a fixture). The plan was revised once, after an Opus
review of its first form (`plan-review.md` beside this plan): the leave rule, the framed stall that hears nothing,
BlipToaster's ack for unframed bytes in the fake, the job probe on a raw stream's running status, the raw probe's
boundary, the scan across a flush, and the documents' bounds; those are task 2's Steps 5 to 8 and the lines they add
to tasks 1, 3 and 4.
Each time, the code was written and validated task by task, then the worktree was restored to `6a4b3fb72` and every
block re-applied from the text below, each test step's red and each implementation step's green re-run, and the wider
suites with them; every Expected line is quoted from the revised plan's pass. There is no prototype branch, so
afterward the worktree was restored to its starting state and the plan was replayed by text (`replay.mjs`): each
task's blocks applied in order to copies of the touched files at `6a4b3fb72`, the result byte-identical to the
validated cumulative diff. A count that moves by a few between runs (`retroplug-engine-graph-test`'s assertions, which
a timing case asserts per iteration) is quoted as one run printed it. Catch2 runs a binary's cases in a random order,
so a quoted failure list may come out in another order. Task 4 changes documents (and one comment) only and has no
failing test.

**How to apply a step's code.** Each code step gives one `patch` block, a `git apply`-able unified diff against the tree
as the steps before it left it. The blocks are fenced with four backticks, because a docs patch could carry
three-backtick lines of its own. Apply one by running this with the block's text (not its fences) between the heredoc's
first line and the closing `PATCH`, or by making the same hunks with the editor:

```
git -C /workspaces/RetroPlug-main/cuttlefish apply - <<'PATCH'
<the step's patch block>
PATCH
```

**Submodules.** No task touches one: `deps/sameboy` is dirty from the configure and no step touches it.

**Hardware.** A real NES with an Everdrive N8 Pro, a Zoom L6, a capture card and MIDI interfaces hang off this
container, so nothing in this plan may open one. Every command below that starts a Catch2 binary, a `pnpm test*` suite
or a build is written under the guarded wrapper, which preloads the tracked device guard
([tools/sdl-audio-guard](../tools/sdl-audio-guard)), self-tests it and refuses `/dev/snd`, `/dev/video*`,
`/dev/ttyACM*`, `/dev/ttyUSB*`, `/dev/serial/*`, `/dev/hidraw*` and `/dev/bus/usb/*` before the syscall:

```
timeout -k 10 <seconds> /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh <command...>
```

The wrapper is a work file of this plan's directory (`.superpowers/mgplan/guarded.sh`, gitignored); it takes any
command and returns its exit code, printing what the guard refused, if anything. Use 300 for one test binary, 1800 for
a build or a whole suite. A refusal it prints is a finding: stop and find what tried to open a device. If a run hits its
timeout, stop and find why before running anything else. N8 tests reach the cart only through `FakeN8` and fake serial
transports. `retroplug-n8-hwtest` is built in tasks 2 and 3 and NEVER run: it opens the real cart. Never run
`retroplug-sdl`, `retroplug-jack`, `jackd`, any `sdl:*` or `reaper:*` script, or the verification kits: the controller
runs acceptance. `/workspaces/bliptoaster` is read, never written: the fake's unframed-byte ack is modelled on its
`src/midi/parse_asm.s`.

**Where this plan departs from the design**, each because the design is wrong about the code or leaves a hole the
review found:

- **The connect seed and the job check send the wrapped probe, `F0 2A 74 F7`, not a plain `'*t'`** (design sections 2
  and 3). The design gives "nothing is streaming yet" as the reason a plain one is safe, but `N8Link::connect` reaches a
  game a previous connection streamed to (a port switch, the plugin's restore, the Settings toggle), and the job check
  runs between two chunks of a live stream: a game under running status `9n` reads a plain `2A 74` as a note-on (key 42,
  velocity 116) that nothing releases. The menu answers the wrapped form the same (design section 1), and a game ignores
  it, so one form serves all three uses.
- **Leaving the menu re-runs the link probe; it does not wait for the link probe to succeed, and it takes three
  unanswered probes in a row** (section 2: "when a probe goes unanswered for its window and the link probe (sync and
  ack) succeeds again"). Read literally, a game without the reliable link (any MIDI ROM but BlipToaster) started from
  the cart's browser would never leave the menu, and the headline `[menu-guard]` case pins that it does; "as today's
  leave-menu re-probe does" is what the code keeps. And one probe missing its 100 ms would stream into the menu for a
  slow turn of its loop (the lab's menu answers in about 20 ms, 27 ms the worst of 8, at a power-on menu), so the
  menu is left after three misses in a row, about 750 ms for a game booted by hand.
- **The "trusted game second" stays, as the gap before a framed stall that heard an ack is probed again** (section 2
  lists it among what goes). A game whose acks are lost or damaged keeps the window stopped while it acks, so without
  the gap the guard would write a probe on every turn that re-sends. Its exposure (a reset inside it streams for up to
  a second) is stated in spec/13 and spec/07.
- **A framed stall that hears nothing at all for two probes gives the window up and probes the link again** (section 2
  has a stall probe again each second for ever). A framed link asks only in a stall, so a RESET between songs and a
  game without the link started from the browser go unseen, and that game answers neither probe: the stream would be
  held until a reconnect. A game with the link re-syncs framed, any other is streamed raw.
- **spec/02 is corrected too** (section 6 lists spec/13, spec/07 and batch 4's design). Its job paragraph says "the cart
  reads its FPGA config block at submission", which task 3 makes untrue. And spec/07's "The N8 menu's back-channel"
  entry, whose last check is the cost of the periodic config reads, gets the probe's checks in their place: the design
  says that check goes with the reads (section 2), and the entry's other sentences describe the read too.
- **`retroplug-n8-hwtest` gains `menuguard ... raw` and `timing ... --guard`** (task 2). The design's rig check runs
  `menuguard` "framed BlipToaster, then BlipToaster held raw", and the leg had no way to hold the link raw; `timing`,
  which measures what a stream costs a running game, built a link without the guard, so it could not measure the
  probe's cost that spec/07 asks for.

## Ground rules

These are the repo's rules this plan must keep. They are not optional and no step repeats them.

- **Never leave the worktree**: prefix git with `git -C /workspaces/RetroPlug-main/cuttlefish` and pnpm with
  `pnpm -C /workspaces/RetroPlug-main/cuttlefish`, and give a build its directory
  (`cmake --build /workspaces/RetroPlug-main/cuttlefish/build`). Never `cd`, never switch branches, never `git stash`,
  never `git reset --hard`, never push.
- **`deps/sameboy` is modified in a fresh tree and that is expected** (its tracked patches are applied at configure).
  Never stage, reset, stash or check out a path in it, and never bump a submodule pointer: no task here touches one.
- **Never `rm -rf build`** (or any `build*/`). Build a target with `cmake --build <dir> --target <t> -j$(nproc)`: the
  default is single-threaded.
- **The em dash (U+2014) never appears** in code, comments, docs or commit messages. Use " - ", ":" or ";".
- **A commit message is a subject line, a blank line, then the body**, committed with `git commit -F -`. No
  `Co-Authored-By`, no "generated with" line, no attribution of any kind, no model names.
- **Match the surrounding code**: names, comment density and wrapping (comments explain WHY, in full sentences, at
  about 116-120 columns, and say what IS, not change history).
- **Never edit an existing `docs/*-plan.md`.** The design
  ([n8-menu-guard-reset-design.md](../docs/n8-menu-guard-reset-design.md)) gains its as-built and execution sections
  from the controller, not from a task here; task 4's one-line pointer in
  [known-bugs-batch-4-design.md](../docs/known-bugs-batch-4-design.md) section 5 is this work's own record.
- **When a commit finishes work listed in spec/07, it deletes that item there** (AGENTS.md): task 3 finishes "The menu
  guard misses a console reset", so task 3's commit carries its deletion and the residual line that replaces it.
- **Only a step's named paths are staged.** `git add` exactly what the step lists; a task's commit is its own
  `git diff` and nothing else.
- **Never use Python**, and never send repository content to an external service.

---

## Task 1: FakeN8's reset menu, and the cases that fail on today's guard

The fake models only a menu reached by power-on: its `peek` answers map_idx 255 whenever `menu` is set, which is what
the guard reads. The real cart (design section 1) leaves the block at the game's mapper after a RESET, answers `'*t'`
with `'k'` there (wrapped in a SysEx too), and stops on ERROR:40 when a `'*'` is followed by a byte that is not a
command. This task gives the fake that menu and writes the design's red cases against today's guard: they compile
against today's API and fail for the reason, each tagged `[!shouldfail]` so the commit leaves the suite green; tasks 2
and 3 drop the tags as they make them pass. A fifth case, green today and after, pins that a `'k'` inside an ack is not
the menu's answer, where the scan task 2 adds could get it wrong.

Three more knobs are for task 2's cases, and are fidelity the guard depends on: `menuAnswerDelayMs` (the menu's answer
queued like a late ack, which the fake otherwise writes inside the probe's own write), the ack BlipToaster writes for
bytes that arrive unframed while its link is on (its `_midiRead` drops such a byte and marks the ack due with `<last>`
at `<next>`, so the probe's four bytes in a stall are answered `F5 <next> <next>`; the fake's game used to ignore
them, so a stalled game could never answer the probe), and a RESET that starts the cart's parsers afresh (a `'*s'`
after one used to resume the game's frame parser where it stopped).

**Files** (line numbers are the file at `6a4b3fb72`):

- Modify: `packages/n8/native/test/FakeN8.hpp:1-15` (the header comment), `:48-50` (`resetMenu`, `error40`),
  `:76` (`syncAckLast`), `:85-88` (`menuAnswerDelayMs`, `consoleResets`, `resetToMenu()`), `:111` (the port's
  `resetsSeen_`), `:195-206` (`replyAfter`), `:338-341` (the unframed ack, at a FIFO write's end), `:373-380` (`peek`),
  `:387-436` (`fifoByte`: a RESET's clean slate, the late answer, ERROR:40, `'*s'` clears `resetMenu`, the link's
  unframed bytes, the sync's ack), `:485-491` (the port's new state).
- Test: `packages/n8/native/test/N8CartJobs.test.cpp:78` (`ByteAtATimePort`, beside `LateCfgPort`) and five cases
  after the last (`:1769`).

**Interfaces:**

- Consumes: `FakeN8` / `FakeN8Port`, `Rig`, `N8Link`'s public API (`setMenuCheck`, `connect`, `push`, `atMenu`,
  `reliable`, `linkOutstanding`), `N8CartLink::connected`.
- Produces: `FakeN8::resetToMenu()`, `resetMenu`, `error40`, `syncAckLast`, `menuAnswerDelayMs`, `consoleResets`; the
  port's `replyAfter` and its ack for unframed bytes; the test-local `ByteAtATimePort`. Tasks 2 and 3 consume the cases
  (they drop the tags) and the knobs.

### Steps

- [ ] **Step 1: write the fake's reset menu and the failing cases**

The cases, in `[menu-guard]`:

- **a framed stream to a menu the console's reset reached trips the guard, and nothing is written after**: BlipToaster
  framed, then `resetToMenu()` and one frame the menu never acknowledges. Today the guard reads map_idx 69 and never
  trips.
- **a raw stream to a menu the console's reset reached trips the guard within a probe's cadence**: a raw game, then the
  reset. Today nothing on a raw link can tell.
- **a framed stream of `'*'`-bearing messages after a reset never drives the menu to ERROR:40**: the reset, one clean
  frame to stop the window, time for the guard to trip, then 100 messages carrying `2A 40` (`'*@'`). Today they reach
  the menu, which stops on ERROR:40. The frame before the trip carries no `0x2A`: what is written before the guard can
  know is the exposure the design leaves (its section 4).
- **a job submitted at a menu the console's reset reached is taken**: today the job check reads map_idx and refuses the
  job as "a game runs" at the one place a battery file can be reached.
- **a `'k'` inside the reliable link's acks is not the menu's answer** (green today, and after): 110 frames, whose acks
  name `0x6B` as `<next>` and as `<last>`; and a sync whose ack names `0x6B` as `<last>`, over a port that hands back a
  byte a read (`ByteAtATimePort`), so the link probe, which goes on as soon as it has read `F5 <next>`, leaves that byte
  for the stream's first read.

````patch
diff --git a/packages/n8/native/test/FakeN8.hpp b/packages/n8/native/test/FakeN8.hpp
index cbe8f7f..32c1607 100644
--- a/packages/n8/native/test/FakeN8.hpp
+++ b/packages/n8/native/test/FakeN8.hpp
@@ -4,9 +4,12 @@
 // parses Edio's command frames as they are written (status, memory reads and writes, the SD directory and file API,
 // with a failure it can inject into a directory load, a file write, a file read or an expansion-volume write), counts
 // the files open on the card, keeps a 64 KB battery (logged if anything touches it) and the FPGA config block (map_idx
-// 255 while the menu shows), answers the menu's '*' commands while the menu is showing, and plays BlipToaster's end of
-// the reliable link while a game that speaks it runs (its acks late, never, or reporting every frame damaged, when a
-// case asks). Every event (a FIFO write, a battery read, a file block) lands in one ordered log.
+// 255 while a menu reached by power-on shows; a menu the console's RESET reached leaves it at the game's mapper, as the
+// real cart does), answers the menu's '*' commands while the menu is showing (skipping every byte that is not a '*', so
+// a '*t' wrapped in a SysEx is answered too, and stopping at ERROR:40 when a '*' is followed by a byte that is not a
+// command), and plays BlipToaster's end of the reliable link while a game that speaks it runs (its acks late, never, or
+// reporting every frame damaged, when a case asks, and an ack for bytes that came unframed, as the ROM writes one).
+// Every event (a FIFO write, a battery read, a file block) lands in one ordered log.
 //
 // Shared by retroplug-n8-test (Ops.test.cpp: the menu and the operations; N8CartJobs.test.cpp: the jobs and the pause,
 // against N8Host with no engine), retroplug-engine-graph-test (N8Adapters.test.cpp: the production adapters behind a
@@ -48,6 +51,12 @@ struct FakeN8 {
     Bytes                    cfg  = Bytes(Edio::SIZE_CFG, 0);
     std::map<std::string, Bytes> files;
     bool                     menu = true;           // the file browser is showing (it answers '*' commands)
+    // ... reached by the console's RESET (resetToMenu), not a power-on: the config block keeps the game's mapper, since
+    // the FPGA routes the OS mapper on a reset without the OS writing map_idx 255 (docs/n8-menu-guard-reset-design.md)
+    bool                     resetMenu = false;
+    // The menu read a '*' followed by a byte that is not one of its commands and stopped answering, as the N8 OS stops
+    // on "ERROR:40 unexpected io status": what a stream carrying 0x2A does to it sooner or later
+    bool                     error40 = false;
     bool                     linkRom = false;       // the running game answers the reliable link (BlipToaster)
     bool                     linkDeaf = false;      // ... until it stops answering (it read no frame and acks nothing)
     int                      linkAckDelayMs = 0;    // how long the game takes to write a frame's ack back
@@ -74,6 +83,7 @@ struct FakeN8 {
     std::vector<std::pair<std::size_t, std::size_t>> dirGets;
     std::vector<Bytes>       applied;               // messages the reliable link delivered to the game
     int                      syncs = 0;             // F4 syncs the game saw
+    int                      syncAckLast = -1;      // the last header a sync's ack names (-1: the seq before its own)
     std::vector<int>         frameSeqs;             // the seq of every intact frame it read
     int                      opens = 0, openNow = 0;
     int                      writes = 0;            // every write a port took a call for (one per frame, Edio.hpp)
@@ -83,8 +93,19 @@ struct FakeN8 {
     std::uint64_t            freeBytes = 0;         // what CMD_F_AVB answers
     Bytes                    vram    = Bytes(2048, 0), palette = Bytes(16, 0);  // what '*v' answers
     std::uint8_t             testReply = 'k';       // what '*t' answers
+    int                      menuAnswerDelayMs = 0; // how long the menu takes to write that answer (0: at once)
     std::uint8_t             installStatus = 0;     // what '*n' answers first (0x44: the menu is out of memory)
     int                      resets = 0;            // '*r's the menu took
+    int                      consoleResets = 0;     // RESET presses (resetToMenu): the cart's parsers start afresh
+
+    // The console's RESET button with a game running: the menu shows again, with USB and the port still up, and the
+    // config block left as the game had it, so only the menu's own answer to '*t' tells it from the game.
+    void resetToMenu() {
+        std::lock_guard<std::mutex> l(mu);
+        menu      = true;
+        resetMenu = true;
+        ++consoleResets;
+    }
 
     std::size_t count(const std::string& prefix) {
         std::lock_guard<std::mutex> l(mu);
@@ -110,6 +131,7 @@ struct FakeN8Port : PortTransport {
         std::lock_guard<std::mutex> l(dev_.mu);
         ++dev_.opens;
         ++dev_.openNow;
+        resetsSeen_ = dev_.consoleResets;
     }
     ~FakeN8Port() override {
         std::lock_guard<std::mutex> l(dev_.mu);
@@ -195,12 +217,14 @@ private:
         std::chrono::steady_clock::time_point due;
         std::deque<std::uint8_t>              bytes;
     };
-    void replyAck(std::initializer_list<std::uint8_t> bytes) {
-        if (dev_.linkAckDelayMs <= 0) {
+    void replyAck(std::initializer_list<std::uint8_t> bytes) { replyAfter(dev_.linkAckDelayMs, bytes); }
+    // A reply the cart writes `delayMs` from now (0: at once), queued with what the game owes, as an ack is.
+    void replyAfter(int delayMs, std::initializer_list<std::uint8_t> bytes) {
+        if (delayMs <= 0) {
             reply(bytes);
             return;
         }
-        acks_.push_back({std::chrono::steady_clock::now() + std::chrono::milliseconds(dev_.linkAckDelayMs),
+        acks_.push_back({std::chrono::steady_clock::now() + std::chrono::milliseconds(delayMs),
                          std::deque<std::uint8_t>(bytes)});
     }
     // Hand the host the acks the game has written: only those now due, or everything it still owes - what a command
@@ -338,6 +362,10 @@ private:
                 if (addr == kFifo) {
                     dev_.log.push_back("fifo:" + hex(data, size));
                     for (std::uint32_t i = 0; i < size; ++i) fifoByte(data[i]);
+                    if (ackOwed_) {  // what the game says about unframed bytes, once its FIFO has run empty
+                        ackOwed_ = false;
+                        replyAck({0xF5, next_, next_});
+                    }
                 } else {
                     for (std::uint32_t i = 0; i < size; ++i) poke(addr + i, data[i]);
                     if (addr >= kSrm && addr < kSrmEnd) dev_.log.push_back("srm-wr");
@@ -372,9 +400,9 @@ private:
     }
     std::uint8_t peek(std::uint32_t addr) {
         if (addr >= kSrm && addr < kSrmEnd) return dev_.sram[addr - kSrm];
-        // The menu runs on the OS's own mapper, 255.
-        if (dev_.menu && addr == kCfg) return 0xFF;
-        if (dev_.menu && addr == kCfg + 2) return 0;
+        // A menu reached by power-on runs on the OS's own mapper, 255; one a reset reached reads as the game left it.
+        if (dev_.menu && !dev_.resetMenu && addr == kCfg) return 0xFF;
+        if (dev_.menu && !dev_.resetMenu && addr == kCfg + 2) return 0;
         if (addr >= kCfg && addr < kCfgEnd) return dev_.cfg[addr - kCfg];
         return 0;
     }
@@ -385,11 +413,20 @@ private:
 
     // What the cart's side of the FIFO makes of a byte: the menu's commands, or the game's reliable link.
     void fifoByte(std::uint8_t b) {
+        if (resetsSeen_ != dev_.consoleResets) {  // a RESET: the menu and the next game read from a clean slate
+            resetsSeen_ = dev_.consoleResets;
+            menu_.clear();
+            msg_.clear();
+            link_   = Idle;
+            linkOn_ = false;
+            ackOwed_ = false;
+        }
         if (dev_.menu) {
+            if (dev_.error40) return;  // the OS has stopped on its error: it reads nothing more
             menu_.push_back(b);
             if (menu_[0] != '*') { menu_.clear(); return; }      // not a command: the menu skips it
             if (menu_.size() < 2) return;
-            if (menu_[1] == 't') { reply({dev_.testReply}); menu_.clear(); return; }
+            if (menu_[1] == 't') { replyAfter(dev_.menuAnswerDelayMs, {dev_.testReply}); menu_.clear(); return; }
             if (menu_[1] == 'v') {  // the screen: 2048 VRAM bytes, then 16 palette bytes
                 out_.insert(out_.end(), dev_.vram.begin(), dev_.vram.end());
                 out_.insert(out_.end(), dev_.palette.begin(), dev_.palette.end());
@@ -399,6 +436,7 @@ private:
             if (menu_[1] == 'r') { ++dev_.resets; menu_.clear(); return; }  // no ack: the console reboots
             if (menu_[1] == 's') {  // the game boots, and the FPGA's config block names its mapper
                 dev_.menu = false;
+                dev_.resetMenu = false;
                 dev_.booted = menuPath_;
                 dev_.cfg[0] = static_cast<std::uint8_t>(dev_.mapIndex);
                 dev_.cfg[2] = static_cast<std::uint8_t>((dev_.mapIndex >> 8) << 4);
@@ -416,21 +454,27 @@ private:
                 menu_.clear();
                 return;
             }
+            dev_.error40 = true;  // a '*' and then no command: "ERROR:40 unexpected io status"
             menu_.clear();
             return;
         }
         if (!dev_.linkRom || dev_.linkDeaf) return;
         // BlipToaster's end of the link, for intact traffic: a sync sets the next seq, a frame carrying it is applied,
-        // and each is acknowledged with F5 <next expected> <last header read>.
+        // and each is acknowledged with F5 <next expected> <last header read>. Once a sync has turned the link on, a
+        // byte outside a frame is dropped and said so: the ROM marks its ack due with <last> at <next> (bliptoaster
+        // src/midi/parse_asm.s, _midiRead), so a write of unframed bytes is answered F5 <next> <next>.
         switch (link_) {
             case Idle:
                 if (b == 0xF4) link_ = WantSync;
                 else if (b == 0xF5) link_ = WantSeq;
+                else if (linkOn_) ackOwed_ = true;
                 return;
             case WantSync:
                 next_ = b;
+                linkOn_ = true;
                 ++dev_.syncs;
-                replyAck({0xF5, next_, static_cast<std::uint8_t>((next_ - 1) & 0x7F)});
+                replyAck({0xF5, next_,
+                          static_cast<std::uint8_t>(dev_.syncAckLast >= 0 ? dev_.syncAckLast : (next_ - 1) & 0x7F)});
                 link_ = Idle;
                 return;
             case WantSeq:
@@ -485,6 +529,9 @@ private:
     std::deque<PendingAck>   acks_;  // what the game owes the link, in the order it will write it
     std::chrono::steady_clock::time_point chatterDue_{};  // the next unsolicited back-channel byte
     std::string              menuPath_;
+    int                      resetsSeen_ = 0;    // the dev's consoleResets this port's parsers have caught up with
+    bool                     linkOn_ = false;    // a sync has turned the game's link on
+    bool                     ackOwed_ = false;   // an unframed byte arrived with the link on: an ack is due
     enum { Idle, WantSync, WantSeq, InMsg, WantSum } link_ = Idle;
     std::uint8_t             next_ = 0, seq_ = 0;
     Bytes                    msg_;
diff --git a/packages/n8/native/test/N8CartJobs.test.cpp b/packages/n8/native/test/N8CartJobs.test.cpp
index e7734ba..c0e0d9f 100644
--- a/packages/n8/native/test/N8CartJobs.test.cpp
+++ b/packages/n8/native/test/N8CartJobs.test.cpp
@@ -78,6 +78,15 @@ private:
     bool              armed_ = false;
 };
 
+// A port that hands back one byte a read, as USB may split what the cart wrote: an ack then arrives a byte at a time.
+class ByteAtATimePort : public retroplug::test::FakeN8Port {
+public:
+    using FakeN8Port::FakeN8Port;
+    std::size_t read(std::uint8_t* b, std::size_t n, std::uint32_t timeoutMs) override {
+        return FakeN8Port::read(b, std::min<std::size_t>(n, 1), timeoutMs);
+    }
+};
+
 std::string tempDir() {
     const auto dir = std::filesystem::temp_directory_path() / "rp-n8-cartjobs-test";
     std::filesystem::create_directories(dir);
@@ -1767,3 +1776,159 @@ TEST_CASE("a menu whose chatter lands ahead of its answers is read as a menu, no
     CHECK(rig.dev.booted.empty());
     CHECK(rig.dev.count("fifo:90 2A 73") == 0);
 }
+
+TEST_CASE("a framed stream to a menu the console's reset reached trips the guard, and nothing is written after",
+          "[n8][menu-guard][!shouldfail]") {
+    // What the real cart did (docs/n8-menu-guard-reset-design.md section 1): RESET drops BlipToaster to the menu with
+    // USB and the framed link still up, and the config block keeps the game's mapper. The acks stop at once, and only
+    // the menu's own answer tells it from a game that has merely gone quiet.
+    Rig rig;
+    rig.dev.menu    = false;
+    rig.dev.linkRom = true;
+    rig.dev.cfg[0]  = 69;  // BlipToaster's mapper, which the reset leaves in the block
+    rig.dev.cfg[2]  = 0;
+    rig.connect();
+    REQUIRE(waitUntil([&] { return rig.host.link().reliable(); }));
+    rig.push({0x90, 0x3C, 0x7F});
+    REQUIRE(waitUntil([&] {
+        std::lock_guard<std::mutex> l(rig.dev.mu);
+        return rig.dev.applied.size() == 1;
+    }));
+    rig.dev.resetToMenu();
+    rig.push({0x90, 0x3E, 0x7F});  // a frame the menu never acknowledges: the window stops moving
+    REQUIRE(waitUntil([&] { return !rig.cart.connected(); }, 2000));
+    CHECK(rig.host.link().atMenu());
+    CHECK(rig.host.link().linkOutstanding() == 0);  // the stale window went with the trip
+    // Nothing of the stream goes to the cart's FIFO after the trip; the guard's own probe still does, which is how it
+    // sees a game the user starts from the menu.
+    const auto streamed = [&] { return rig.dev.count("fifo:") - rig.dev.count("fifo:F0 2A 74 F7"); };
+    const std::size_t before = streamed();
+    rig.push({0x90, 0x41, 0x7F});
+    std::this_thread::sleep_for(std::chrono::milliseconds(300));
+    CHECK(streamed() == before);
+    std::lock_guard<std::mutex> l(rig.dev.mu);
+    CHECK(rig.dev.menu);
+    CHECK_FALSE(rig.dev.error40);
+}
+
+TEST_CASE("a raw stream to a menu the console's reset reached trips the guard within a probe's cadence",
+          "[n8][menu-guard][!shouldfail]") {
+    // Raw MIDI has no acks to stop, so nothing the cart says on its own marks the reset: the guard has to ask.
+    using Clock = std::chrono::steady_clock;
+    Rig rig;
+    rig.connectGame();  // a game whose block names mapper 0, which the reset leaves there
+    rig.push({0x90, 0x3C, 0x7F});
+    REQUIRE(waitUntil([&] { return rig.dev.count("fifo:90 3C 7F") == 1; }));
+    rig.dev.resetToMenu();
+    const auto t0 = Clock::now();
+    REQUIRE(waitUntil([&] { return !rig.cart.connected(); }, 2000));
+    // One cadence is 250 ms; the rest is room for a loaded runner.
+    CHECK(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count() < 600);
+    CHECK(rig.host.link().atMenu());
+    rig.push({0x90, 0x2A, 0x73});  // '*s' if the menu ever read it
+    std::this_thread::sleep_for(std::chrono::milliseconds(50));
+    CHECK(rig.dev.count("fifo:90 2A 73") == 0);
+    std::lock_guard<std::mutex> l(rig.dev.mu);
+    CHECK(rig.dev.menu);
+    CHECK(rig.dev.booted.empty());
+    CHECK_FALSE(rig.dev.error40);
+}
+
+TEST_CASE("a framed stream of '*'-bearing messages after a reset never drives the menu to ERROR:40",
+          "[n8][menu-guard][!shouldfail]") {
+    // ERROR:40 is a '*' followed by a byte that is not one of the menu's commands, which a framed stream carries sooner
+    // or later (0x2A 0x40, '*@', in every message here); on the real cart the reset stream reached it within 21 s, and
+    // the OS stopped there. Once the guard has tripped no byte of the stream reaches the menu, so it never sees one.
+    Rig rig;
+    rig.dev.menu    = false;
+    rig.dev.linkRom = true;
+    rig.dev.cfg[0]  = 69;
+    rig.dev.cfg[2]  = 0;
+    rig.connect();
+    REQUIRE(waitUntil([&] { return rig.host.link().reliable(); }));
+    rig.push({0x90, 0x3C, 0x7F});
+    REQUIRE(waitUntil([&] {
+        std::lock_guard<std::mutex> l(rig.dev.mu);
+        return rig.dev.applied.size() == 1;
+    }));
+    rig.dev.resetToMenu();
+    // The frame that stops the window carries no '*': what is written before the guard can know is the exposure the
+    // design leaves (section 4), and this case is about what comes after.
+    rig.push({0x90, 0x3E, 0x7F});
+    // Time for the guard to see the window stop and ask; the checks below say whether it did.
+    (void)waitUntil([&] { return rig.host.link().atMenu(); }, 1000);
+    for (int i = 0; i < 100; ++i) {
+        rig.push({0x90, 0x2A, 0x40});
+        std::this_thread::sleep_for(std::chrono::milliseconds(5));
+    }
+    std::this_thread::sleep_for(std::chrono::milliseconds(200));
+    CHECK(rig.host.link().atMenu());
+    std::lock_guard<std::mutex> l(rig.dev.mu);
+    CHECK_FALSE(rig.dev.error40);
+    CHECK(rig.dev.menu);
+}
+
+TEST_CASE("a job submitted at a menu the console's reset reached is taken", "[n8][menu-guard][!shouldfail]") {
+    // The block keeps the game's mapper after a reset, so a check that reads it refuses the job as "a game runs" at the
+    // one place a battery file can be reached.
+    Rig rig;
+    rig.dev.menu   = false;
+    rig.dev.cfg[0] = 69;
+    rig.dev.cfg[2] = 0;
+    rig.dev.files[kGameBattery] = saveOf(1024, 21);
+    rig.connect();
+    rig.dev.resetToMenu();
+    Job job;
+    std::string why;
+    const bool taken = rig.submit(battery("dump"), job, &why);
+    CHECK(why == "");
+    REQUIRE(taken);
+    REQUIRE(waitUntil([&] { return job.finished(); }));
+    CHECK(job.state.load() == Job::Done);
+    CHECK(job.result == saveOf(1024, 21));
+}
+
+TEST_CASE("a 'k' inside the reliable link's acks is not the menu's answer", "[n8][menu-guard]") {
+    // The menu answers the guard's probe with a 'k' (0x6B), and an ack is F5 <next> <last> with both 7-bit, so a 'k'
+    // can stand inside one. Only a 'k' outside an ack is the menu's, and a game whose acks carry one keeps its stream.
+    SECTION("as <next> and <last> of a stream's acks") {
+        Rig rig;
+        rig.dev.menu    = false;
+        rig.dev.linkRom = true;
+        rig.dev.cfg[0]  = 69;
+        rig.dev.cfg[2]  = 0;
+        rig.connect();
+        REQUIRE(waitUntil([&] { return rig.host.link().reliable(); }));
+        // Frames 0 to 109: the acks of frames 0x6A and 0x6B name 0x6B as <next> and as <last>.
+        for (int i = 0; i < 110; ++i) rig.push({0x90, static_cast<std::uint8_t>(i & 0x7F), 0x40});
+        REQUIRE(waitUntil([&] {
+            std::lock_guard<std::mutex> l(rig.dev.mu);
+            return rig.dev.applied.size() == 110;
+        }));
+        std::this_thread::sleep_for(std::chrono::milliseconds(50));
+        CHECK_FALSE(rig.host.link().atMenu());
+        CHECK(rig.cart.connected());
+    }
+    SECTION("as <last> of the sync's ack, which the link probe leaves for the stream's first read") {
+        // The probe goes on as soon as it has read F5 <next>: a port that hands the ack over a byte at a time leaves
+        // its <last> in the input.
+        FakeN8 dev;
+        dev.menu        = false;
+        dev.linkRom     = true;
+        dev.syncAckLast = 0x6B;
+        N8Link link([&dev](const std::string&) -> std::unique_ptr<PortTransport> {
+            return std::make_unique<ByteAtATimePort>(dev);
+        });
+        link.setMenuCheck([](Edio&) { return 0; });  // the guard on, and a game at connect
+        REQUIRE(link.connect("/dev/fake"));
+        REQUIRE(waitUntil([&] { return link.reliable(); }));
+        std::this_thread::sleep_for(std::chrono::milliseconds(50));
+        CHECK_FALSE(link.atMenu());
+        const Bytes on{0x90, 0x3C, 0x7F};
+        CHECK(link.push(0, on.data(), on.size(), 48000.0));
+        REQUIRE(waitUntil([&] {
+            std::lock_guard<std::mutex> l(dev.mu);
+            return dev.applied.size() == 1;
+        }));
+    }
+}
````

- [ ] **Step 2: run them, and see them fail**

```
timeout -k 10 1800 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh cmake --build /workspaces/RetroPlug-main/cuttlefish/build --target retroplug-n8-test -j$(nproc)
timeout -k 10 300 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh /workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-n8-test "[menu-guard]"
```

Expected: exit 0, with the four tagged cases failing as expected, each for the reason the design gives (the guard does
not trip at a reset menu; the menu reaches ERROR:40; a job at a reset menu is refused as a game), and the `'k'` case
passing:

```
-------------------------------------------------------------------------------
a framed stream to a menu the console's reset reached trips the guard, and
nothing is written after
-------------------------------------------------------------------------------
/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8CartJobs.test.cpp:1780
...............................................................................

/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8CartJobs.test.cpp:1799: FAILED:
  REQUIRE( waitUntil([&] { return !rig.cart.connected(); }, 2000) )
with expansion:
  false

-------------------------------------------------------------------------------
a raw stream to a menu the console's reset reached trips the guard within a
probe's cadence
-------------------------------------------------------------------------------
/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8CartJobs.test.cpp:1814
...............................................................................

/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8CartJobs.test.cpp:1824: FAILED:
  REQUIRE( waitUntil([&] { return !rig.cart.connected(); }, 2000) )
with expansion:
  false

-------------------------------------------------------------------------------
a framed stream of '*'-bearing messages after a reset never drives the menu to
ERROR:40
-------------------------------------------------------------------------------
/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8CartJobs.test.cpp:1837
...............................................................................

/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8CartJobs.test.cpp:1865: FAILED:
  CHECK( rig.host.link().atMenu() )
with expansion:
  false

/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8CartJobs.test.cpp:1867: FAILED:
  CHECK_FALSE( rig.dev.error40 )
with expansion:
  !true

-------------------------------------------------------------------------------
a job submitted at a menu the console's reset reached is taken
-------------------------------------------------------------------------------
/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8CartJobs.test.cpp:1871
...............................................................................

/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8CartJobs.test.cpp:1884: FAILED:
  CHECK( why == "" )
with expansion:
  "the battery file can only be reached at the N8 menu: power-cycle to the menu
  first"
  ==
  ""

/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8CartJobs.test.cpp:1885: FAILED:
  REQUIRE( taken )
with expansion:
  false

===============================================================================
test cases: 11 |  7 passed | 4 failed as expected
assertions: 85 | 79 passed | 6 failed as expected
```

- [ ] **Step 3: the wider suites this task reaches**

`FakeN8` is shared by `retroplug-n8-test`, `retroplug-engine-graph-test` (`N8Adapters.test.cpp`) and the test host
(`retroplug-host`, which `pnpm test:native n8` runs), and every existing case keeps the fake's default, the power-on
menu; no existing case writes unframed bytes to a game whose link is on, so the new ack changes none of them. Run the
n8 binary three times: it has timing cases.

```
timeout -k 10 1800 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh cmake --build /workspaces/RetroPlug-main/cuttlefish/build --target retroplug-n8-test retroplug-engine-graph-test retroplug-device-mode-test retroplug-host -j$(nproc)
timeout -k 10 300 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh /workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-n8-test
timeout -k 10 300 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh /workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-n8-test
timeout -k 10 300 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh /workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-n8-test
timeout -k 10 1800 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh /workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-engine-graph-test
timeout -k 10 600 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh /workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-device-mode-test
timeout -k 10 1800 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh pnpm -C /workspaces/RetroPlug-main/cuttlefish test:n8
timeout -k 10 1800 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh pnpm -C /workspaces/RetroPlug-main/cuttlefish test:native n8
```

Expected: exit 0 for each (the n8 binary's three runs alike):

```
test cases:  128 |  124 passed | 4 failed as expected
assertions: 1759 | 1753 passed | 6 failed as expected
test cases:  128 |  124 passed | 4 failed as expected
assertions: 1759 | 1753 passed | 6 failed as expected
test cases:  128 |  124 passed | 4 failed as expected
assertions: 1759 | 1753 passed | 6 failed as expected
test cases:     342 |     331 passed | 11 skipped
assertions: 1702530 | 1702530 passed |  0 skipped
All tests passed (1504408 assertions in 96 test cases)
All tests passed (17 assertions in 2 test cases)
test cases:  128 |  124 passed | 4 failed as expected
assertions: 1759 | 1753 passed | 6 failed as expected
# 2 plugin test binary(ies) passed (jobs=16)
# 3 native test file(s) passed (jobs=16)
```

In order: the n8 binary three times, the engine-graph binary, the device-mode binary, `test:n8`'s two binaries
(`retroplug-serial-test`, then `retroplug-n8-test` once more) and its summary, and `test:native n8`'s summary (its
three files, `n8-cart-link`, `n8-sdk` and `n8-session`, pass 1, 3 and 5 cases).

- [ ] **Step 4: commit**

```
git -C /workspaces/RetroPlug-main/cuttlefish add packages/n8/native/test/FakeN8.hpp \
  packages/n8/native/test/N8CartJobs.test.cpp
git -C /workspaces/RetroPlug-main/cuttlefish commit -F - <<'MSG'
test(n8): FakeN8 models a menu the console's reset reached, and the guard's misses

On the real cart a console RESET drops a game to the N8 menu and leaves the
FPGA config block at the game's mapper, so the menu guard, which reads map_idx,
never trips; the menu answers '*t' with 'k' (inside a SysEx too) and stops on
ERROR:40 when a '*' is followed by a byte that is not a command. FakeN8 gains
that menu (resetToMenu, which also starts the cart's parsers afresh), the
error40 flag, a menu slow to answer, the last header a sync's ack names, and
BlipToaster's ack for bytes that arrive unframed while its link is on. Four
cases fail on today's guard and are tagged shouldfail until the guard and the
job check ask the menu: a framed stream and a raw stream to a reset menu trip
it, '*'-bearing frames after the trip never reach ERROR:40, and a job at a reset
menu is taken. A fifth pins that a 'k' inside an ack is not the menu's answer.
MSG
```

---

## Task 2: the guard asks the menu

The guard's signal becomes the menu's answer. **The probe** is `F0 2A 74 F7` (`N8Link::kMenuProbe`): a SysEx a MIDI
game ignores, whose `'*t'` the menu reads. It is written where the stream's own bytes are between two messages (on a
raw link `betweenMessages`, with no time bound, and `noteWireBytes` books it as a SysEx, so the stream's next
running-status chunk gets its status written back; a frame never leaves a message open), and is not counted in
`bytesForwarded()` (it is not the stream's, and `retroplug-n8-hwtest menuguard` counts the bytes written after a trip
by that number). **The answer** is a `0x6B` on the back-channel outside an ack: `N8Link::BackChannelScan` follows the
ack parser's states (`F5 <next> <last>`), the link reads the back-channel every turn on a raw link too (it read nothing
there before), the link probe hands the scan the state of the ack it stopped reading halfway, and a flush starts the
scan afresh. **When it asks:** raw, every 250 ms, with no read and no wait; framed, once the window has stopped moving
(`progressOverdue`), holding new frames and re-sends until a `'k'` (trip) or an ack (a game whose acks are lost or
damaged: resume, and no probe of a stall for a second; a live BlipToaster acks the probe's own unframed bytes, which
is what answers a stall), asking again each second that hears neither, and after two such seconds giving the window up
and probing the link again; at the menu, every 250 ms whatever the link, leaving it after three probes in a row go
unanswered for 100 ms each. A turn whose probe still awaits its answer, or that has read half an ack, is not quiet for
a read (OnControl's), and a paused job waits the probe out, since the `'k'` landing inside a reply would be taken for
its first byte. The connect seed is the same probe with a 100 ms wait: N8Host's `askMenu`, installed as the
`MenuCheck`. The config-block read goes with its machinery (`readMenuState`, the forced read and its one-second bound,
the per-read flushes, `menuFlushNext_`, `kMenuReadFailNs`, `kMenuReadMs` and the read's scoped timeout).

The task runs in two rounds of red and green. Steps 1 to 4 build the guard; Steps 5 to 8 are the cases its first form
fails (a late answer leaves the menu, a stall nothing answers holds the stream for good, a message left open past 250 ms
gets the probe inside it), with two cases that pin the stall's resume on a slow and on a damaged game, and the fixes.

Two existing things the guard turns red, and their fixes: the one-second case (`a menu that never goes quiet is read
anyway after about a second`) pinned the hold's bound, which a probe no longer needs (a chattering menu cannot keep a
write from going out), so it is rewritten in Step 1 to pin the opposite (the trip within the stall, and nothing but the
guard's probes written after it), on a menu the console's reset reached; and `N8CartLink.test.cpp`'s fake port kept a
refused write's failure through its later reads, against `PortTransport::failure`'s contract ("each call sets it
afresh"), which the raw path's new back-channel read took for a dead port, so its `read` and `flushInput` clear it
(Step 3).

**Files** (line numbers are the file at `6a4b3fb72`):

- Modify: `packages/n8/native/src/n8/desktop/N8Link.hpp:41-62` (OnControl's quiet rule; `MenuCheck`; the new
  `kMenuProbe`, `kMenuAnswer` and `BackChannelScan`), `:94-104` (`setMenuCheck`, `reportMenuState`), `:208-215`
  (`menuHold`, `writeMenuProbe`, `menuProbeAwaited`), `:229` (`betweenMessages`, Step 7), `:235` (`readBackChannel`),
  `:294-301` (the probe's state, and the miss counts of Step 7).
- Modify: `packages/n8/native/src/n8/desktop/N8Link.cpp:16-23` (the constants), `:69` (`flushPort` resets the scan,
  Step 7), `:117-133` (the seed), `:153` (`disconnect`), `:258-287` (`probeLink`), `:319-336` (`readBackChannel`),
  `:355-418` (`menuHold`, `writeMenuProbe`, `menuProbeAwaited`; `readMenuState` goes), `:454-462` (`enterMenu`,
  `leaveMenu`), `:479-486` (`quietForRead`), `:531-575` (`streamLoop`'s turn, and the job's wait), `:743`
  (`betweenMessages`, Step 7).
- Modify: `packages/n8/native/src/n8/desktop/N8Host.cpp:79-84` (`kMenuAnswerMs` and `askMenu` replace `kMenuReadMs`),
  `:191-206` (the menu check).
- Modify: `packages/n8/native/test/n8-sd-hwtest.cpp:23` and `:365-428` (`menuguard ... [raw]`, its stall shown by the
  outstanding count), `packages/n8/native/test/n8-timing.cpp:1-5`, `:180-190`, `:237-240` (`timing ... --guard`).
- Test: `packages/n8/native/test/N8CartJobs.test.cpp` (Step 1: the three guard cases' tags, and the one-second case
  rewritten; Step 3: the comments that named the config read; Step 5: five cases after the last),
  `packages/n8/native/test/N8CartLink.test.cpp:73-84` (the fake port's `failure`), `:204` (a comment).

**Interfaces:**

- Consumes: `N8ReliableSender::FRAME`, `progressOverdue`, `onBackChannel`; `N8Link::noteWireBytes`, `enterMenu` /
  `leaveMenu`, `probeLink`, `discardRing`; `Edio::fifoWR` and `rx8`; FakeN8's `menuAnswerDelayMs`, `linkDamaged`,
  `linkAckDelayMs` and its unframed-byte ack (task 1).
- Produces: `N8Link::kMenuProbe`, `N8Link::kMenuAnswer`, `N8Link::BackChannelScan` (public: task 3's `askMenu` reads
  with it), `N8Link::betweenMessages()` (private; task 3's quiet rule uses it); N8Host.cpp's `askMenu(Edio&)` (1 menu,
  0 a game, -1 the port failed) and `kMenuAnswerMs`, which task 3 uses for the job check. `MenuCheck`'s type is
  unchanged (its answer is now the menu's, not map_idx's), so every `setMenuCheck({})` and `LateCfgPort`'s
  config-reading check still compile and mean what they did.

### Steps

- [ ] **Step 1: drop the guard cases' tags, and rewrite the one-second case**

````patch
diff --git a/packages/n8/native/test/N8CartJobs.test.cpp b/packages/n8/native/test/N8CartJobs.test.cpp
index c0e0d9f..4b077ef 100644
--- a/packages/n8/native/test/N8CartJobs.test.cpp
+++ b/packages/n8/native/test/N8CartJobs.test.cpp
@@ -1714,7 +1714,7 @@ TEST_CASE("a job submitted under a stream into the menu is accepted, and the str
     CHECK_FALSE(rig.cart.connected());  // the menu: attached, streaming nothing
 }
 
-TEST_CASE("a menu that never goes quiet is read anyway after about a second", "[n8][menu-guard]") {
+TEST_CASE("a menu that never goes quiet is caught by its answer, and nothing is re-sent into it", "[n8][menu-guard]") {
     using Clock = std::chrono::steady_clock;
     Rig rig;
     rig.dev.menu    = false;
@@ -1729,31 +1729,33 @@ TEST_CASE("a menu that never goes quiet is read anyway after about a second", "[
         return rig.dev.applied.size() == 1;
     }));
     // The console resets to its menu, and the menu chatters on the back-channel: the quiet rule stamps the last heard
-    // on ANY byte, so the turn never goes quiet and only the hold's bound can end it.
+    // on ANY byte, so no turn is ever quiet. The guard's probe needs none, and a chattered byte is no answer and no
+    // ack.
+    rig.dev.resetToMenu();
     {
         std::lock_guard<std::mutex> l(rig.dev.mu);
-        rig.dev.menu      = true;
         rig.dev.chatterMs = 40;
     }
     rig.push({0x90, 0x3E, 0x7F});  // an unacknowledged frame: this is what a re-send would take into the menu
-    // A second message pushed while the hold waits for its read, a lookahead out so it is still in the ring (or in
-    // hand) at the trip: the trip drops it, and nothing may write it afterwards. It is what the FIFO count below is for
-    // - a frame the fake's menu branch never reads cannot show it.
+    // A second message pushed while the hold waits for the menu's answer, a lookahead out so it is still in the ring
+    // (or in hand) at the trip: the trip drops it, and nothing may write it afterwards. It is what the FIFO count below
+    // is for - a frame the fake's menu branch never reads cannot show it.
     rig.host.link().setLookaheadMs(1100);
     rig.push({0x90, 0x41, 0x7F});
     rig.host.link().setLookaheadMs(0);
     const auto t0 = Clock::now();
-    REQUIRE(waitUntil([&] { return !rig.cart.connected(); }, 4000));  // the trip, which is the read landing
+    REQUIRE(waitUntil([&] { return !rig.cart.connected(); }, 4000));  // the trip, which is the menu's 'k' heard
     const auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count();
-    CHECK(waited >= 900);    // it waited out the bound rather than reading on a hunch with a frame in flight
-    CHECK(waited < 2500);
-    // The frame was held, not re-sent, on every turn of the wait: the menu would have read a re-send as a command.
+    CHECK(waited < 900);  // as soon as the window stopped (the link's 100 ms timeout), not after a quiet turn
+    // The frame was held, not re-sent, while the probe waited: the menu would have read a re-send as a command.
     CHECK(rig.host.link().linkResends() == 0);
-    // And nothing more goes to the cart's FIFO after the trip. The fake's frameSeqs cannot say this (its menu branch
-    // returns before it reads a frame), so this counts what the host WROTE, which a re-send would add to.
-    const std::size_t writes = rig.dev.count("fifo:");
+    // And nothing of the stream goes to the cart's FIFO after the trip. The fake's frameSeqs cannot say this (its menu
+    // branch returns before it reads a frame), so this counts what the host WROTE, which a re-send would add to; the
+    // guard's own probes go on at the menu, and are not the stream's.
+    const auto streamed = [&] { return rig.dev.count("fifo:") - rig.dev.count("fifo:F0 2A 74 F7"); };
+    const std::size_t writes = streamed();
     std::this_thread::sleep_for(std::chrono::milliseconds(300));
-    CHECK(rig.dev.count("fifo:") == writes);  // nothing more was written after the trip
+    CHECK(streamed() == writes);  // nothing more was written after the trip
     CHECK(rig.dev.menu);
     CHECK(rig.dev.booted.empty());
 }
@@ -1778,7 +1780,7 @@ TEST_CASE("a menu whose chatter lands ahead of its answers is read as a menu, no
 }
 
 TEST_CASE("a framed stream to a menu the console's reset reached trips the guard, and nothing is written after",
-          "[n8][menu-guard][!shouldfail]") {
+          "[n8][menu-guard]") {
     // What the real cart did (docs/n8-menu-guard-reset-design.md section 1): RESET drops BlipToaster to the menu with
     // USB and the framed link still up, and the config block keeps the game's mapper. The acks stop at once, and only
     // the menu's own answer tells it from a game that has merely gone quiet.
@@ -1812,7 +1814,7 @@ TEST_CASE("a framed stream to a menu the console's reset reached trips the guard
 }
 
 TEST_CASE("a raw stream to a menu the console's reset reached trips the guard within a probe's cadence",
-          "[n8][menu-guard][!shouldfail]") {
+          "[n8][menu-guard]") {
     // Raw MIDI has no acks to stop, so nothing the cart says on its own marks the reset: the guard has to ask.
     using Clock = std::chrono::steady_clock;
     Rig rig;
@@ -1835,7 +1837,7 @@ TEST_CASE("a raw stream to a menu the console's reset reached trips the guard wi
 }
 
 TEST_CASE("a framed stream of '*'-bearing messages after a reset never drives the menu to ERROR:40",
-          "[n8][menu-guard][!shouldfail]") {
+          "[n8][menu-guard]") {
     // ERROR:40 is a '*' followed by a byte that is not one of the menu's commands, which a framed stream carries sooner
     // or later (0x2A 0x40, '*@', in every message here); on the real cart the reset stream reached it within 21 s, and
     // the OS stopped there. Once the guard has tripped no byte of the stream reaches the menu, so it never sees one.
````

- [ ] **Step 2: run them, and see them fail**

```
timeout -k 10 1800 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh cmake --build /workspaces/RetroPlug-main/cuttlefish/build --target retroplug-n8-test -j$(nproc)
timeout -k 10 300 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh /workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-n8-test "[menu-guard]"
```

Expected: FAIL, the three untagged reset cases and the rewritten one, for the reason this task fixes: the guard never
trips at a reset menu (framed and raw, and the rewritten case's chattering one), and the menu reads the `'*@'` frames
and stops on ERROR:40. The job case still fails as expected (task 3's):

```
-------------------------------------------------------------------------------
a menu that never goes quiet is caught by its answer, and nothing is re-sent
into it
-------------------------------------------------------------------------------
/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8CartJobs.test.cpp:1717
...............................................................................

/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8CartJobs.test.cpp:1747: FAILED:
  REQUIRE( waitUntil([&] { return !rig.cart.connected(); }, 4000) )
with expansion:
  false

-------------------------------------------------------------------------------
a job submitted at a menu the console's reset reached is taken
-------------------------------------------------------------------------------
/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8CartJobs.test.cpp:1873
...............................................................................

/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8CartJobs.test.cpp:1886: FAILED:
  CHECK( why == "" )
with expansion:
  "the battery file can only be reached at the N8 menu: power-cycle to the menu
  first"
  ==
  ""

/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8CartJobs.test.cpp:1887: FAILED:
  REQUIRE( taken )
with expansion:
  false

-------------------------------------------------------------------------------
a framed stream of '*'-bearing messages after a reset never drives the menu to
ERROR:40
-------------------------------------------------------------------------------
/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8CartJobs.test.cpp:1839
...............................................................................

/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8CartJobs.test.cpp:1867: FAILED:
  CHECK( rig.host.link().atMenu() )
with expansion:
  false

/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8CartJobs.test.cpp:1869: FAILED:
  CHECK_FALSE( rig.dev.error40 )
with expansion:
  !true

-------------------------------------------------------------------------------
a framed stream to a menu the console's reset reached trips the guard, and
nothing is written after
-------------------------------------------------------------------------------
/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8CartJobs.test.cpp:1782
...............................................................................

/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8CartJobs.test.cpp:1801: FAILED:
  REQUIRE( waitUntil([&] { return !rig.cart.connected(); }, 2000) )
with expansion:
  false

-------------------------------------------------------------------------------
a raw stream to a menu the console's reset reached trips the guard within a
probe's cadence
-------------------------------------------------------------------------------
/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8CartJobs.test.cpp:1816
...............................................................................

/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8CartJobs.test.cpp:1826: FAILED:
  REQUIRE( waitUntil([&] { return !rig.cart.connected(); }, 2000) )
with expansion:
  false

===============================================================================
test cases: 11 |  6 passed | 4 failed | 1 failed as expected
assertions: 79 | 72 passed | 5 failed | 2 failed as expected
```

- [ ] **Step 3: implement it**

````patch
diff --git a/packages/n8/native/src/n8/desktop/N8Host.cpp b/packages/n8/native/src/n8/desktop/N8Host.cpp
index 4a23321..3d40d91 100644
--- a/packages/n8/native/src/n8/desktop/N8Host.cpp
+++ b/packages/n8/native/src/n8/desktop/N8Host.cpp
@@ -79,9 +79,30 @@ constexpr int kProbeLinkBusy = -3;
 // a control thread (the DAW's) from waiting out the read's own 2 s timeout to learn it.
 constexpr int kMenuProbeMs = 1000;
 
-// How long the menu guard's own read of the config block may wait for the cart: it runs between chunks on the serial
-// thread, so a cart that stopped answering must cost the stream a fraction of the read timeout (2 s), not all of it.
-constexpr std::uint32_t kMenuReadMs = 100;
+// How long the N8 menu's answer to a probe may take: one USB round trip and a turn of the menu's loop. Past it nothing
+// answered, and the cart runs a game. The link waits as long for the answer to the probe it streams (N8Link.cpp's
+// kMenuAnswerNs).
+constexpr std::uint32_t kMenuAnswerMs = 100;
+
+// Whether the N8 menu runs, asked the one way that holds after a console RESET (which leaves the config block's map_idx
+// at the game's mapper): N8Link::kMenuProbe out, then up to kMenuAnswerMs for the menu's 'k'. 1 for the menu, 0 when
+// nothing answered (a game, which ignores the SysEx), -1 when the port failed. What else the cart says meanwhile is
+// stepped over, an ack whole (a 'k' inside one is not the menu's).
+int askMenu(Edio& edio) {
+    if (!edio.fifoWR(N8Link::kMenuProbe, sizeof N8Link::kMenuProbe).ok()) return -1;
+    N8Link::BackChannelScan scan;
+    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kMenuAnswerMs);
+    for (;;) {
+        const auto left =
+            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
+        if (left <= 0) return 0;
+        std::uint8_t         b = 0;
+        const rp::n8::Status s = edio.rx8(b, static_cast<std::uint32_t>(left));
+        if (s.code == rp::n8::Code::Timeout) return 0;
+        if (!s.ok()) return -1;
+        if (scan.feed(b) == N8Link::BackChannelScan::Heard::MenuAnswer) return 1;
+    }
+}
 
 // Why a battery job refuses a running game: the battery file lives on the SD card, which only the menu's file API
 // reaches (a running game has no menu), and the cart's own battery is never touched over USB (on the real cart one
@@ -188,22 +209,9 @@ N8Host::N8Host(N8Link::PortFactory factory, PortLister lister, std::string confi
     // A value a param changed while the link is up: the serial thread owns the port, so it writes the standing value
     // on its own turn, between MIDI chunks, and marks the generation it wrote. A throw leaves the generation behind,
     // so the next turn tries again (N8Link catches it: a config write must not take the MIDI link down).
-    // Where the cart runs, for the link's menu guard (N8Link::MenuCheck): the config block's map_idx, 255 while the N8
-    // OS's own menu shows. A system-region read, so it touches neither the cart's bus nor a running game's FIFO, and it
-    // takes a short timeout: it is made between chunks, and a cart silent for two seconds per turn would stall the
-    // stream. -1 (the block did not answer) leaves the link's state as it was.
-    link_.setMenuCheck([](Edio& edio) {
-        // The short timeout is this call's, not the Edio's from here on: a scope guard puts the table back however the
-        // read ends, a throw from the port included, so no later call inherits a 100 ms reply timeout.
-        struct ReadMs {
-            rp::n8::Timeouts& table;
-            rp::n8::Timeouts  saved;
-            ~ReadMs() { table = saved; }
-        } restore{edio.timeouts(), edio.timeouts()};
-        restore.table.readMs = kMenuReadMs;
-        const int mapper = runningMapper(edio);
-        return mapper < 0 ? -1 : (mapper == kMenuMapper ? 1 : 0);
-    });
+    // Where the cart runs, for the link's menu guard (N8Link::MenuCheck), asked at each connect: whether the menu
+    // answers its probe. -1 (the port failed) leaves the link's state as it was, and its own probes ask again.
+    link_.setMenuCheck([](Edio& edio) { return askMenu(edio); });
     link_.setOnControl([this](Edio& edio, bool backChannelQuiet) {
         // The config block a submission's refusal reads (probeMenu). Taken here, between chunks, for the reason the
         // volume is: a system-region read on the thread that owns the port, putting no byte in a running game's FIFO.
diff --git a/packages/n8/native/src/n8/desktop/N8Link.cpp b/packages/n8/native/src/n8/desktop/N8Link.cpp
index 41d73fc..62a2bc8 100644
--- a/packages/n8/native/src/n8/desktop/N8Link.cpp
+++ b/packages/n8/native/src/n8/desktop/N8Link.cpp
@@ -13,14 +13,12 @@ namespace retroplug {
 
 namespace {
 using steady = std::chrono::steady_clock;
-// How often the cart's at-menu state is read on a quiet turn while streaming (raw, framed and idle, or already at the
-// menu); how long a stream whose window has stopped moving waits for a quiet turn before it is read anyway (a menu that
-// chatters on the back-channel keeps the quiet rule fresh for ever, so only this bound ends it); and how long a read
-// that did not answer holds the next one off, so a port that keeps timing out costs the stream 100 ms a second, not
-// 100 ms in every 250. The bound is also the job probe's own deadline (N8Host's kMenuProbeMs).
-constexpr std::int64_t kMenuReadNs = 250'000'000;
-constexpr std::int64_t kMenuHoldNs = 1'000'000'000;
-constexpr std::int64_t kMenuReadFailNs = 1'000'000'000;
+// How often the guard asks where the cart runs (on a raw link, and at the menu whatever the link); how long the menu's
+// answer may take before the cart is taken for a game, which ignores the probe (N8Host's seed waits as long, its
+// kMenuAnswerMs); and how long a framed hold waits for an answer or an ack before it asks again.
+constexpr std::int64_t kMenuProbeNs  = 250'000'000;
+constexpr std::int64_t kMenuAnswerNs = 100'000'000;
+constexpr std::int64_t kMenuHoldNs   = 1'000'000'000;
 // RP_N8LINK_TRACE=1 logs every byte the link writes and every back-channel byte it reads (stderr) - read once.
 bool traceOn() {
     static const bool on = std::getenv("RP_N8LINK_TRACE") != nullptr;
@@ -115,17 +113,19 @@ bool N8Link::connect(const std::string& port) {
     wireStatus_      = 0;
     streamStatus_    = 0;
     // The cart's at-menu state, on the fresh port and before any producer may push: a menu reads the FIFO as commands,
-    // so the state decides whether anything may be streamed at all. A check that fails to answer leaves the state as it
-    // was (not the menu), which streams as the link always did.
+    // so the state decides whether anything may be streamed at all. A check whose port fails leaves the state as it was
+    // (not the menu), which streams as the link always did, and the stream's own probe asks again at its first turn.
     atMenu_.store(false, std::memory_order_relaxed);
-    menuHoldSince_  = 0;
-    menuReadDueNs_  = 0;
+    menuProbeAtNs_   = 0;
+    menuProbeDueNs_  = 0;
     menuGameUntilNs_ = 0;
-    menuFlushNext_  = false;  // a prior connection's unanswered read is not this one's input
+    menuAnswered_    = false;
+    backScan_        = {};
+    acksHeard_       = 0;
+    acksAtProbe_     = 0;
     if (menuCheck_) {
         try {
-            // What the cart said after the handshake is not this read's reply: a flush keeps a chattering cart's byte
-            // out of the block's first byte, which would read as a game (see readMenuState).
+            // What the cart said after the handshake is not this check's answer: a flush leaves its reply on its own.
             flushPort();
             atMenu_.store(menuCheck_(*edio_) == 1, std::memory_order_relaxed);
         } catch (const std::exception&) {
@@ -150,7 +150,7 @@ void N8Link::disconnect() {
     // Where the OLD cart ran says nothing about the next one: a status read while the link is down must not keep saying
     // "at the menu" for a port that has been closed.
     atMenu_.store(false, std::memory_order_relaxed);
-    menuHoldSince_   = 0;
+    menuProbeAtNs_   = 0;
     menuGameUntilNs_ = 0;
     rel_.reset();
     reliable_.store(false, std::memory_order_release);
@@ -256,11 +256,12 @@ bool N8Link::runPaused(PausedJob job) {
 }
 
 void N8Link::probeLink() {
-    // Whatever the port already holds goes before the sync: a menu read that timed out at connect leaves the tail of
-    // its reply in the input, and a config block's bytes can spell the ack's own F5 <next>, which would turn the
-    // framed link on for a ROM that reads no frames. Nothing is owed at either probe site (connect, before any frame;
-    // leaveMenu, after a read that consumed its own reply), so no real ack is dropped.
+    // Whatever the port already holds goes before the sync: a seed check that timed out at connect can leave the tail
+    // of its reply in the input, and bytes that spell the ack's own F5 <next> would turn the framed link on for a ROM
+    // that reads no frames. Nothing is owed at either probe site (connect, before any frame; leaveMenu, after a guard
+    // probe nothing answered), so no real ack is dropped; the scan starts afresh with the input.
     flushPort();
+    backScan_ = {};
     // F4 00 is the link's sync: a BlipToaster with the link answers it with an ack (F5 00) on the back-channel the
     // moment its FIFO runs empty; any other ROM ignores an undefined System Common status. ~100 ms covers a busy
     // ROM and the USB round trip with room to spare - and costs nothing after connect.
@@ -279,6 +280,9 @@ void N8Link::probeLink() {
                 t.resendDelayNs   = 0;           // the real round trip already applies
                 rel_ = std::make_unique<rp::N8ReliableSender>(t);
                 reliable_.store(true, std::memory_order_release);
+                // The ack's <last> was in this read, and goes with the rest of it, or is still to come, and then the
+                // scan must read it as the ack's: a 0x6B there is not the menu's answer.
+                backScan_.state = i + 1 < n ? 0 : 2;
                 return;
             }
             sawFrame = (buf[i] == rp::N8ReliableSender::FRAME);
@@ -317,13 +321,14 @@ void N8Link::writeFramed(std::vector<std::uint8_t>& out, const std::vector<int>&
 }
 
 void N8Link::serviceLink(std::int64_t now) {
-    serviceLinkAcks();
+    readBackChannel();
     serviceLinkResends(now);
 }
 
-// Acks in, and nothing out: the quiet test a turn's read is given must see what the cart has actually said, and a
-// reply read before it can have an ack land inside.
-void N8Link::serviceLinkAcks() {
+// What the cart has said, and nothing out: the reliable link's acks, and the menu's answer to the guard's probe. The
+// quiet test a turn's read is given must see what the cart has actually said, and a reply read before it can have an
+// ack or a 'k' land inside.
+void N8Link::readBackChannel() {
     const std::int64_t now = nowNs();
     std::uint8_t buf[64];
     const std::size_t n = readPort(buf, sizeof buf, 0);   // whatever the cart has said: never waits
@@ -332,7 +337,13 @@ void N8Link::serviceLinkAcks() {
         for (std::size_t i = 0; i < n; ++i) std::fprintf(stderr, " %02X", buf[i]);
         std::fprintf(stderr, "\n");
     }
-    if (n) rel_->onBackChannel(buf, n, now);
+    if (n && rel_) rel_->onBackChannel(buf, n, now);
+    if (!menuCheck_) return;
+    for (std::size_t i = 0; i < n; ++i) {
+        const BackChannelScan::Heard heard = backScan_.feed(buf[i]);
+        if (heard == BackChannelScan::Heard::Ack) ++acksHeard_;
+        else if (heard == BackChannelScan::Heard::MenuAnswer) menuAnswered_ = true;
+    }
 }
 
 // Whatever the reliable link is due to re-send at `now`, out. Held while the menu guard looks (menuHold): a cart at its
@@ -352,69 +363,67 @@ void N8Link::serviceLinkResends(std::int64_t now) {
     linkRttMinNs_.store(rel_->rttMinNs(), std::memory_order_relaxed);
 }
 
-// The cart's at-menu state for this turn (see MenuCheck). Two things ask for a read: the cadence, on a turn quiet
-// enough for one; and a framed window that has STOPPED MOVING - the oldest frame unacknowledged past the link's own
-// timeout - which is the console reset, and whose re-send a menu would read as one of its commands. The second holds
-// the stream until the read answers. A cart that chatters cannot postpone that for ever: the quiet rule stamps the last
-// heard on any byte, so its bound, not the silence, is what ends it (kMenuHoldNs), after a flush of what it said.
-bool N8Link::menuHold(std::int64_t now, bool quiet) {
+// The cart's at-menu state for this turn, asked with the guard's probe (kMenuProbe) and answered by the menu's 'k' on
+// the back-channel (readBackChannel), which trips the guard whenever it is heard: only the menu answers. When it asks:
+// - raw, every 250 ms between two messages, as part of the stream: no read and no wait, the answer is heard on a later
+//   turn. A raw link has no acks, so nothing else tells a menu the console's reset reached from a game.
+// - framed, only once the window has STOPPED MOVING (the oldest frame unacknowledged past the link's own timeout, which
+//   is the console reset), holding new frames and re-sends until a 'k' (the menu) or an ack (a game whose acks are
+//   merely lost or damaged: its stream goes on, and a stall is not asked about again for a second), and asking again
+//   each second that hears neither. A framed game never sees the probe outside a stall.
+// - at the menu, every 250 ms whatever the link: a game the user starts from the cart's own browser answers no probe,
+//   and one left unanswered for 100 ms leaves the menu (leaveMenu runs the link probe again).
+bool N8Link::menuHold(std::int64_t now) {
     if (!menuCheck_) return false;
+    if (menuAnswered_) {
+        menuAnswered_  = false;
+        menuProbeAtNs_ = 0;
+        reportMenuState(true);
+    }
     if (atMenu_.load(std::memory_order_relaxed)) {
-        menuHoldSince_ = 0;
-        // Still look, every cadence: a game the user starts from the cart's own browser is only visible in the config
-        // block, and nothing is outstanding at a menu, so a turn is always quiet for the read.
-        if (now >= menuReadDueNs_) readMenuState(now);
-        return atMenu_.load(std::memory_order_relaxed);  // nothing new goes out while it says menu
+        if (menuProbeAtNs_ != 0 && now - menuProbeAtNs_ >= kMenuAnswerNs) {
+            menuProbeAtNs_ = 0;
+            reportMenuState(false);
+            return atMenu_.load(std::memory_order_relaxed);
+        }
+        if (menuProbeAtNs_ == 0 && now >= menuProbeDueNs_) writeMenuProbe(now);
+        return true;  // nothing new goes out while it says menu
     }
     if (rel_ && rel_->progressOverdue(now) && now >= menuGameUntilNs_) {
-        const bool forced = menuHoldSince_ != 0 && now - menuHoldSince_ >= kMenuHoldNs;
-        if (quiet || forced) {
-            const bool answered = readMenuState(now);
-            menuHoldSince_ = 0;
-            if (atMenu_.load(std::memory_order_relaxed)) return true;
-            // The cart answered as a game: its acks are merely lost or damaged, and the stream goes on as it always
-            // did, at one read a second rather than one a turn. A read that did NOT answer waits the same (its own
-            // failure bound), so a window that keeps timing out costs the stream one read a second, not 100 ms a turn.
-            menuGameUntilNs_ = now + (answered ? kMenuHoldNs : kMenuReadFailNs);
+        if (menuProbeAtNs_ != 0 && acksHeard_ != acksAtProbe_) {
+            menuProbeAtNs_   = 0;
+            menuGameUntilNs_ = now + kMenuHoldNs;
             return false;
         }
-        if (menuHoldSince_ == 0) menuHoldSince_ = now;
-        return true;  // hold the re-send until the guard has looked
-    }
-    menuHoldSince_ = 0;
-    if (quiet && now >= menuReadDueNs_) readMenuState(now);
-    return atMenu_.load(std::memory_order_relaxed);
-}
-
-// One read of where the cart runs. True when it answered; a read that did not leaves the state as it was and holds the
-// next one off for a second (see kMenuReadFailNs).
-bool N8Link::readMenuState(std::int64_t now) {
-    menuReadDueNs_ = now + kMenuReadNs;
-    // Whatever the cart has already said goes first: the reply to this read must not begin with bytes a chattering cart
-    // left in the input, which would take the place of the reply's own (map_idx 255 behind one status byte reads as a
-    // game, and a game re-enables the stream into the menu). Every path flushes, not only the forced read: a raw link
-    // has nothing else draining its input between turns.
-    try {
-        flushPort();
-    } catch (const std::exception&) {
-        menuReadDueNs_ = now + kMenuReadFailNs;  // as a read that did not answer: the state stands, and the next waits
-        return false;
+        if (menuProbeAtNs_ == 0 || now - menuProbeAtNs_ >= kMenuHoldNs) writeMenuProbe(now);
+        return true;  // hold the re-send until the cart has answered
     }
-    int answer = -1;
-    try {
-        answer = menuCheck_(*edio_);
-    } catch (const std::exception&) {
-        return false;  // the port's failure is the stream's to report at its next write; the state stands
-    }
-    if (answer < 0) {
-        menuReadDueNs_ = now + kMenuReadFailNs;
-        // A read that timed out may have taken only the first bytes of its reply: the rest is in the port's input, and
-        // the next turn's ack parse would read its tail as link traffic. That turn flushes instead (see streamLoop).
-        menuFlushNext_ = true;
+    if (rel_) {
+        // The window moves (or a stall's ack was heard within the second): no probe of a stall is awaited any more.
+        menuProbeAtNs_ = 0;
         return false;
     }
-    reportMenuState(answer != 0);
-    return true;
+    if (menuProbeAtNs_ != 0 && now - menuProbeAtNs_ >= kMenuAnswerNs) menuProbeAtNs_ = 0;  // unanswered: a game
+    if (menuProbeAtNs_ == 0 && now >= menuProbeDueNs_ && atMessageBoundary(now)) writeMenuProbe(now);
+    return false;
+}
+
+// The guard's probe, out: unframed, at a point the caller has made sure is between two messages, and on a raw link
+// through the tracker as any message is, since a SysEx ends running status on the cart's wire too (the stream's next
+// chunk gets its status written back, see writeChunk). Not counted as forwarded: it is the link's question, not MIDI.
+void N8Link::writeMenuProbe(std::int64_t now) {
+    menuProbeAtNs_  = now;
+    menuProbeDueNs_ = now + kMenuProbeNs;
+    acksAtProbe_    = acksHeard_;
+    if (!rel_) noteWireBytes(kMenuProbe, sizeof kMenuProbe, now, false);
+    if (traceOn())
+        std::fprintf(stderr, "[n8link] %.3f tx F0 2A 74 F7 (the menu probe)\n", static_cast<double>(now) / 1e6);
+    if (writeLog_) writeLog_(kMenuProbe, sizeof kMenuProbe, now, false);
+    check(edio_->fifoWR(kMenuProbe, sizeof kMenuProbe));
+}
+
+bool N8Link::menuProbeAwaited(std::int64_t now) const {
+    return menuProbeAtNs_ != 0 && now - menuProbeAtNs_ < kMenuAnswerNs;
 }
 
 void N8Link::reportMenuState(bool menu) {
@@ -451,14 +460,16 @@ void N8Link::enterMenu() {
         releaseAfter_ = 0;
         releaseWanted_.store(false, std::memory_order_relaxed);
     }
-    menuHoldSince_   = 0;
+    // A probe of the guard's still awaited is the menu's to answer: whatever becomes of its 'k', it is not "nothing
+    // answered", which at the menu would read as a game.
+    menuProbeAtNs_   = 0;
     menuGameUntilNs_ = 0;
     if (traceOn()) std::fprintf(stderr, "[n8link] the cart is at its menu: the stream is dropped\n");
 }
 
 void N8Link::leaveMenu() {
     if (traceOn()) std::fprintf(stderr, "[n8link] the cart runs a game: streaming, and probing the link\n");
-    menuHoldSince_   = 0;
+    menuProbeAtNs_   = 0;
     menuGameUntilNs_ = 0;
     // A game started from the cart's own browser may speak the reliable link, and the connect-time probe could not have
     // told: the frames it would carry start from a sync here.
@@ -477,6 +488,9 @@ void N8Link::discardRing() {
 }
 
 bool N8Link::quietForRead(std::int64_t now) const {
+    // A probe of the guard's whose answer may still be coming: a 'k' read inside a reply would be taken for its first
+    // byte, and lost to the guard.
+    if (menuProbeAwaited(now)) return false;
     // Nothing outstanding, so no ack is owed (an ack the ROM repeats for a frame it already acknowledged is the one
     // exception, see OnControl). Or a cart that has said nothing for the pause's ack bound since the oldest outstanding
     // frame first went out: one at its menu (a console reset keeps USB and the link up, and the menu acknowledges
@@ -531,22 +545,15 @@ void N8Link::streamLoop() {
     bool have = false;
     while (running_.load(std::memory_order_acquire)) {
         try {
-            // Acks in first, then the quiet test, then the menu guard, then the re-sends it may hold: the control turn
-            // below can read a reply, so an ack that answered an outstanding frame must be behind us before it does,
-            // and the guard's own read must not have one land inside it.
-            if (menuFlushNext_) {
-                // A menu read timed out on its last turn, and the rest of its reply may still be arriving: parsed as
-                // acks its tail could point the window past a frame that is still on its way. One turn's flush drops
-                // what is there instead, and an ack it loses costs a re-send (see readMenuState).
-                menuFlushNext_ = false;
-                flushPort();
-            } else if (rel_) {
-                serviceLinkAcks();
-            }
-            const std::int64_t turnNs = nowNs();
-            const bool         quiet  = quietForRead(turnNs);
+            // The back-channel in first, then the menu guard (which may write its probe), then the quiet test (which a
+            // probe still awaiting its answer makes false), then the re-sends the guard may hold: the control turn
+            // below can read a reply, so an ack that answered an outstanding frame, and the menu's 'k', must be behind
+            // us before it does.
+            if (rel_ || menuCheck_) readBackChannel();
+            const std::int64_t turnNs   = nowNs();
+            const bool         menuHeld = menuHold(turnNs);
+            const bool         quiet    = quietForRead(turnNs);
             backChannelQuiet_.store(quiet, std::memory_order_relaxed);
-            const bool menuHeld = menuHold(turnNs, quiet);
             if (rel_ && !menuHeld) serviceLinkResends(turnNs);
             // A setting that lives in an atomic (see OnControl) is applied between chunks, on the thread that owns the
             // port. Best-effort, like the connect-time write: a config register must not take the MIDI link down. A
@@ -568,8 +575,13 @@ void N8Link::streamLoop() {
             // streaming turn, never inside a job's pause, which only the job's own traffic may use, and never at a
             // menu, which reads them as commands.
             if (!menuHeld) writeReleases(false);
-            // Jobs still run at the menu: the battery files and an upload are exactly what its file API is for.
+            // Jobs still run at the menu: the battery files and an upload are exactly what its file API is for. Not
+            // while the guard's probe awaits its answer, which the job's first read would take for its reply's.
             if (jobPending_.load(std::memory_order_acquire)) {
+                if (menuProbeAwaited(turnNs)) {
+                    std::this_thread::sleep_for(std::chrono::microseconds(200));
+                    continue;
+                }
                 runJob(chunk, have);
                 continue;
             }
diff --git a/packages/n8/native/src/n8/desktop/N8Link.hpp b/packages/n8/native/src/n8/desktop/N8Link.hpp
index 22620ea..ba3e0d6 100644
--- a/packages/n8/native/src/n8/desktop/N8Link.hpp
+++ b/packages/n8/native/src/n8/desktop/N8Link.hpp
@@ -41,9 +41,10 @@ public:
     // written: where a setting whose value lives in an atomic (N8Host's expansion volume and its config-block probe)
     // reaches the device without the caller taking a lock or building a std::function. Set once before any connect,
     // like OnConnected, so it is never mutated while the thread reads it. `backChannelQuiet` is false while the cart
-    // may be about to acknowledge an outstanding frame: anything that reads a reply off the port must wait for a quiet
-    // turn, since an ack landing inside that read is taken for the reply's first bytes and the reply's tail is then
-    // parsed as link traffic. A turn is quiet with nothing outstanding, or once the cart has said nothing for the
+    // may be about to acknowledge an outstanding frame, or to answer the guard's probe: anything that reads a reply off
+    // the port must wait for a quiet turn, since an ack (or the menu's 'k') landing inside that read is taken for the
+    // reply's first bytes and the reply's tail is then parsed as link traffic. A turn is quiet with nothing outstanding
+    // and no probe of the guard's awaiting its answer (100 ms at most), or once the cart has said nothing for the
     // pause's ack bound (250 ms) since the oldest outstanding frame first went out: a cart at its menu acknowledges
     // nothing, and would otherwise never give a read a turn. What it does not cover is an ack for no outstanding frame:
     // on a lossy link the ROM acknowledges each copy of a frame the host sent twice, and the second copy's ack can come
@@ -54,12 +55,44 @@ public:
     // keeps refusing a write cannot take the link's turns from the MIDI stream (a caller that cannot wait that out, a
     // submission's probe, asks for a quiet turn of its own through wakeControl).
     using OnControl = std::function<void(rp::n8::Edio&, bool backChannelQuiet)>;
-    // What the cart runs, read on the serial thread (and at connect, before the thread starts): 1 at the N8 menu, 0 in
-    // a game, -1 when the FPGA config block did not answer (the last state then stands). A menu reads the same FIFO a
-    // game takes MIDI from, so a streamed message can be read as one of its '*' commands (0x2A 0x73 is '*s'), which is
-    // why the link drops rather than streams while this says menu. N8Host installs one whose read has a short timeout:
-    // it runs between chunks, and a cart that stopped answering must not cost the stream two seconds a turn.
+    // Where the cart runs, asked at connect, before the serial thread starts: 1 when the N8 menu answers, 0 when
+    // nothing does (a game), -1 when the port failed (the last state then stands). A menu reads the same FIFO a game
+    // takes MIDI from, so a streamed message can be read as one of its '*' commands (0x2A 0x73 is '*s'), which is why
+    // the link drops rather than streams while the cart is at its menu. N8Host installs one that writes kMenuProbe and
+    // waits 100 ms for the 'k'. While streaming the link asks on its own, with the same probe written between chunks
+    // and the answer read off the back-channel (see menuHold).
     using MenuCheck = std::function<int(rp::n8::Edio&)>;
+    // The guard's question: '*t' inside a SysEx. The N8 menu skips every byte that is not a '*', so it reads the '*t'
+    // and answers 'k' (kMenuAnswer) on the back-channel, after a console RESET too, which leaves the FPGA config
+    // block's map_idx at the game's mapper (docs/n8-menu-guard-reset-design.md); a MIDI game ignores a SysEx it does
+    // not know, and BlipToaster, on the real cart, ignores this one.
+    static constexpr std::uint8_t kMenuProbe[4] = {0xF0, 0x2A, 0x74, 0xF7};
+    static constexpr std::uint8_t kMenuAnswer   = 0x6B;
+    // The back-channel, a byte at a time: the menu's answer is a 'k' OUTSIDE an ack. An ack is F5 <next> <last>, both
+    // 7-bit (N8ReliableSender::onBackChannel), so either can be a 0x6B, and the scan follows the same states to tell.
+    struct BackChannelScan {
+        enum class Heard { Nothing, Ack, MenuAnswer };
+        int state = 0;  // 0 between acks, 1 after F5, 2 after <next>
+        Heard feed(std::uint8_t b) {
+            if (b == rp::N8ReliableSender::FRAME) {
+                state = 1;
+                return Heard::Nothing;
+            }
+            if (b & 0x80) {
+                state = 0;
+                return Heard::Nothing;
+            }
+            if (state == 1) {
+                state = 2;
+                return Heard::Nothing;
+            }
+            if (state == 2) {
+                state = 0;
+                return Heard::Ack;
+            }
+            return b == kMenuAnswer ? Heard::MenuAnswer : Heard::Nothing;
+        }
+    };
 
     // What a paused job tells the link once it has run: resume streaming, or stop it (a ROM boot: the old stream is
     // stale, and a running game has no menu to come back to). Stop closes the port, as a disconnect does.
@@ -94,13 +127,13 @@ public:
     // Set once by the owner before any connect; connect() runs it on the caller's thread, while the link is the only
     // thing on the port.
     void          setOnConnected(OnConnected fn) { onConnected_ = std::move(fn); }
-    // Set once by the owner before any connect, like OnConnected: read at each connect (to seed the state) and on quiet
-    // turns while streaming. Null on a link with no cart behind it (N8CartLink.test.cpp's fake device), which then
-    // streams whatever it is given, as it always did.
+    // Set once by the owner before any connect, like OnConnected: asked at each connect to seed the state, and what
+    // turns the guard on (its probes while streaming). Null on a link with no cart behind it (N8CartLink.test.cpp's
+    // fake device), which then streams whatever it is given, as it always did.
     void          setMenuCheck(MenuCheck fn) { menuCheck_ = std::move(fn); }
-    // [serial thread] What a config-block read said about where the cart runs (a MenuCheck's answer, or N8Host's own
-    // menu probe): the guard takes it as its state at once, so a probe made for a submission trips the guard before
-    // that job's pause drains the ring into a menu.
+    // [serial thread] What a probe said about where the cart runs (the guard's own, or N8Host's for a submission): the
+    // guard takes it as its state at once, so a probe made for a submission trips the guard before that job's pause
+    // drains the ring into a menu.
     void          reportMenuState(bool menu);
     void          setOnControl(OnControl fn) { onControl_ = std::move(fn); }
     // [any] Run OnControl at the next quiet turn, whatever the wait a failed config write armed: the wake stands
@@ -208,10 +241,11 @@ private:
     void serialLoop();
     void streamLoop();                                   // serial thread: the probe, then stream until stopped
     // serial thread: the cart's at-menu state for this turn (see MenuCheck). True while the stream must put no new
-    // chunk, re-send or release on the wire: the cart is at its menu, or the window has stopped moving and the guard
-    // has not been able to look yet. The turn's quietForRead is the second argument.
-    bool        menuHold(std::int64_t now, bool quiet);
-    bool        readMenuState(std::int64_t now);                // serial thread: read it; false when it did not answer
+    // chunk, re-send or release on the wire: the cart is at its menu, or the window has stopped moving and the guard's
+    // probe has heard neither the menu's answer nor an ack yet.
+    bool        menuHold(std::int64_t now);
+    void        writeMenuProbe(std::int64_t now);              // serial thread: kMenuProbe out, between two messages
+    bool        menuProbeAwaited(std::int64_t now) const;     // serial thread: a probe's answer may still be coming
     void        enterMenu();                                    // serial thread: discard what is held, reset the link
     void        leaveMenu();                                    // serial thread: probe the link again
     void        discardRing();                                   // serial thread: drop what the ring holds, counted
@@ -232,7 +266,7 @@ private:
     void stopStreaming();                                // serial thread: stop and close the port (AfterJob::Stop)
     void probeLink();                                    // serial thread: sync + listen for an ack
     void serviceLink(std::int64_t now);                  // serial thread: read acks, send re-sends
-    void serviceLinkAcks();                              // serial thread: the cart's back-channel, read
+    void readBackChannel();                              // serial thread: the cart's back-channel: acks, the menu's 'k'
     // serial thread: whatever the reliable link is due to re-send at `now`, the turn's own timestamp: the same one the
     // guard judged, so a re-send can never slip past the hold by re-reading the clock a moment later.
     void serviceLinkResends(std::int64_t now);
@@ -291,14 +325,19 @@ private:
     // The earliest moment OnControl runs again after a throw (see the loop in streamLoop), and a wake a caller asked
     // for since the last quiet turn: the first is serial-thread state, the second is stored from any thread.
     std::int64_t               controlRetryAtNs_ = 0;
-    // The next periodic menu read, and when a re-send the guard must look before began to wait for a quiet turn (0:
-    // not waiting). Serial thread.
-    std::int64_t               menuReadDueNs_ = 0;
-    std::int64_t               menuHoldSince_ = 0;
-    std::int64_t               menuGameUntilNs_ = 0;  // a read answered "a game" here: no hold before this moment
-    // A menu read timed out and its reply may still be arriving: the next turn flushes the port instead of parsing what
-    // it said as acks (readMenuState, streamLoop). Serial thread, reset at each connect.
-    bool                       menuFlushNext_ = false;
+    // The guard's probe (menuHold): when the last one went out (0: none awaiting its answer), when the next is due, and
+    // the moment before which a stopped window is not probed again, because the last probe's stall heard an ack (a
+    // game). Serial thread, reset at each connect.
+    std::int64_t               menuProbeAtNs_   = 0;
+    std::int64_t               menuProbeDueNs_  = 0;
+    std::int64_t               menuGameUntilNs_ = 0;
+    // What the back-channel has said (readBackChannel): the menu's 'k', and how many whole acks against the count when
+    // the probe went out. The scan's state runs on across reads, since an ack can be split between two. Serial thread,
+    // reset at each connect.
+    bool                       menuAnswered_ = false;
+    BackChannelScan            backScan_;
+    std::uint64_t              acksHeard_   = 0;
+    std::uint64_t              acksAtProbe_ = 0;
     std::atomic<bool>          controlWanted_{false};
     std::atomic<bool>          backChannelQuiet_{true};  // the last turn's quietForRead, for other threads
 
diff --git a/packages/n8/native/test/N8CartJobs.test.cpp b/packages/n8/native/test/N8CartJobs.test.cpp
index 4b077ef..8fe4e9f 100644
--- a/packages/n8/native/test/N8CartJobs.test.cpp
+++ b/packages/n8/native/test/N8CartJobs.test.cpp
@@ -526,7 +526,7 @@ TEST_CASE("the menu guard reads the cart's state between chunks, so a streamed f
         rig.dev.linkRom = true;  // a game's link, stale after the console went back to its menu
         cfgTail(rig.dev);
         rig.dev.files[kGameBattery] = saveOf(1024, 3);
-        rig.connect();  // at the menu: the guard reads that off the config block before anything may stream
+        rig.connect();  // at the menu: the guard's seed hears its answer before anything may stream
         // A message pushed now would be read as one of the menu's commands, so it is dropped rather than framed, and
         // nothing is left outstanding for a re-send to carry into the menu.
         rig.push({0x90, 0x3C, 0x7F});
@@ -862,9 +862,8 @@ TEST_CASE("a cart reset to its menu with a frame its game never acknowledged sti
     }));
 
     // The console's reset button: the N8 goes back to its menu with USB still up, and the menu acknowledges nothing (it
-    // skips every byte that is not one of its commands). The guard reads that between chunks - the frame below is due
-    // to be re-sent, the cart has been silent, so it looks - and the stale window is reset there rather than re-sent
-    // into a command parser.
+    // skips every byte that is not one of its commands). The frame below stops the window, the guard asks, and the
+    // menu's answer resets the stale window there rather than re-sending it into a command parser.
     {
         std::lock_guard<std::mutex> l(rig.dev.mu);
         rig.dev.menu = true;
@@ -1587,8 +1586,8 @@ TEST_CASE("a seed menu read that timed out leaves nothing for the connect-time p
 
 TEST_CASE("a cart at its menu is not streamed to: its FIFO is a command parser there", "[n8][menu-guard]") {
     // The N8 menu reads the same FIFO a game reads MIDI from, so a data pair inside a message - 0x2A 0x73, say - is its
-    // '*s': start an app. The guard reads where the cart runs from the FPGA config block (a system-region read: no bus,
-    // no FIFO), and drops rather than streams while that says menu.
+    // '*s': start an app. The guard asks the menu where the cart runs ('*t' in a SysEx, which only the menu answers),
+    // and drops rather than streams while the answer says menu.
     Rig rig;
     rig.connect();  // at its menu: the fake's default
     // The connect-time link probe holds the serial thread for up to 100 ms, and a push waits in the ring behind it:
@@ -1661,7 +1660,7 @@ TEST_CASE("leaving the menu re-probes the link, so a game the user starts by han
         rig.dev.cfg[0] = 69;
         rig.dev.cfg[2] = 0;
     }
-    // The guard's next read sees a game, and leaving the menu runs the link probe again: the frames start.
+    // The guard's next probe goes unanswered, and leaving the menu runs the link probe again: the frames start.
     REQUIRE(waitUntil([&] { return rig.host.link().reliable(); }));
     rig.push({0x90, 0x3C, 0x7F});
     REQUIRE(waitUntil([&] {
@@ -1761,10 +1760,9 @@ TEST_CASE("a menu that never goes quiet is caught by its answer, and nothing is
 }
 
 TEST_CASE("a menu whose chatter lands ahead of its answers is read as a menu, not as a game", "[n8][menu-guard]") {
-    // The guard's read of the config block takes the reply's first bytes as they come. A cart that talks while the host
-    // is not reading leaves bytes in the input, and a reply read behind them is garbled: map_idx 255 with one status
-    // byte in front of it reads as a game, which would leave the guard streaming into the menu. Flushing the input
-    // before the read is what keeps the answer the cart's own.
+    // A cart that talks while the host is not reading leaves bytes in the input, ahead of the menu's 'k'. Taken for the
+    // answer, a status byte there would read as a game and leave the guard streaming into the menu: the answer is a
+    // 'k' wherever it lands among them.
     Rig rig;
     rig.dev.chatterMs      = 40;  // one byte whenever the cart owes the host no reply
     rig.dev.chatterGarbles = true;  // ... left in the input for the next read, as one said between reads is
diff --git a/packages/n8/native/test/N8CartLink.test.cpp b/packages/n8/native/test/N8CartLink.test.cpp
index 6a7ae71..de82205 100644
--- a/packages/n8/native/test/N8CartLink.test.cpp
+++ b/packages/n8/native/test/N8CartLink.test.cpp
@@ -73,12 +73,14 @@ struct FakePort : PortTransport {
     }
     std::size_t read(std::uint8_t* b, std::size_t n, std::uint32_t) override {
         std::lock_guard<std::mutex> l(dev_.mu);
+        failure_.clear();  // each call says afresh (PortTransport::failure): a refused write's words are its own
         std::size_t i = 0;
         while (i < n && !out_.empty()) { b[i++] = out_.front(); out_.pop_front(); }
         return i;
     }
     void flushInput() override {
         std::lock_guard<std::mutex> l(dev_.mu);
+        failure_.clear();
         out_.clear();
     }
 
@@ -201,7 +203,7 @@ TEST_CASE("N8CartLink: a push reaches the cart as FIFO writes, timed at the bloc
     FakeDevice dev;
     N8Host     host(factoryFor(dev), noPorts(), tempCfgDir());
     host.link().setLinkAllowed(false);  // no probe: the wire below is the pushes alone
-    host.link().setMenuCheck({});       // nor a menu guard: its own config reads would be among the wire this counts
+    host.link().setMenuCheck({});       // nor a menu guard: its own probes would be among the wire this counts
     REQUIRE(host.link().connect("fake"));
     N8CartLink link(host);
     REQUIRE(link.connected());
diff --git a/packages/n8/native/test/n8-sd-hwtest.cpp b/packages/n8/native/test/n8-sd-hwtest.cpp
index 96d12e8..616cdc0 100644
--- a/packages/n8/native/test/n8-sd-hwtest.cpp
+++ b/packages/n8/native/test/n8-sd-hwtest.cpp
@@ -20,7 +20,7 @@
 //   retroplug-n8-hwtest write    <local-src> <sd-path> [port]  # CMD_F_FWR: write a local file to the SD card
 //   retroplug-n8-hwtest fiford   <count> [timeout-ms] [port]   # read the cart's USB back-channel (CMD_USB_WR)
 //   retroplug-n8-hwtest link     <steps> [loss-permille] [port] # BlipToaster's reliable link, over N8Link
-//   retroplug-n8-hwtest menuguard <seconds> [port]  # the menu guard, streaming while a person presses RESET
+//   retroplug-n8-hwtest menuguard <seconds> [port] [raw] # the menu guard, streaming while a person presses RESET
 //   retroplug-n8-hwtest menuprobe <test|sysex|note> [port] # what the N8 OS menu does with bytes it did not ask for
 //   retroplug-n8-hwtest linkdbg  [port]                        # the link step by hand: sync, frames, acks, APU mirror
 //   retroplug-n8-hwtest timing   <idle|busy> [steps] [--raw]   # latency / jitter / load on the real console
@@ -366,17 +366,22 @@ int main(int argc, char** argv) {
     // the production one, streaming a short 2A03 note every 60 ms for <seconds> while a person presses the console's
     // RESET button (it keeps USB and the link up and drops the cart to its menu: the case a power-cycle cannot make).
     // Logs every change of the link's state with its time, then what was written after the guard tripped (expected: 0
-    // bytes) and how long the trip took after the cart stopped acknowledging. Boot a game that answers the probe first.
+    // bytes) and how long the trip took after the cart stopped acknowledging. Boot a game with the reliable link
+    // (BlipToaster) first.
+    // `raw` holds the link raw (no link probe, so BlipToaster gets plain MIDI): the raw path, whose only reset signal
+    // is the guard's own probe cadence.
     if (op == "menuguard") {
         const int         seconds = static_cast<int>(std::strtol(argv[2], nullptr, 0));
-        const std::string gport   = argc > 3 ? argv[3] : findN8Port();
+        const bool        raw     = std::string(argv[argc - 1]) == "raw";
+        const std::string gport   = argc > 3 && std::string(argv[3]) != "raw" ? argv[3] : findN8Port();
         if (seconds <= 0 || gport.empty()) {
-            std::fprintf(stderr, "usage: %s menuguard <seconds> [port] (no Everdrive N8 found?)\n", argv[0]);
+            std::fprintf(stderr, "usage: %s menuguard <seconds> [port] [raw] (no Everdrive N8 found?)\n", argv[0]);
             return 2;
         }
         N8Host host(
             [](const std::string& p) -> std::unique_ptr<PortTransport> { return std::make_unique<SerialTransport>(p); },
             [] { return std::vector<N8PortDto>{}; }, "/tmp");
+        if (raw) host.link().setLinkAllowed(false);
         host.setPort(gport);
         host.connect(true);
         N8Link& link = host.link();
@@ -403,7 +408,7 @@ int main(int argc, char** argv) {
         const std::uint8_t on[]  = {0x90, 69, 100};
         const std::uint8_t off[] = {0x80, 69, 0};
         bool          wasMenu = link.atMenu(), wasUp = link.isConnected(), stalled = false;
-        double        stallMs = -1, tripMs = -1, lastWriteMs = 0;
+        double        stallMs = -1, tripMs = -1, lastWriteMs = 0, outstandingSince = -1;
         std::uint64_t lastBytes = link.bytesForwarded(), bytesAtTrip = 0;
         int           step = 0;
         // Up to <seconds>, or 5 s past a trip: enough to see that nothing more is written once the guard has the menu.
@@ -415,10 +420,16 @@ int main(int argc, char** argv) {
             const std::uint64_t bytes = link.bytesForwarded();
             if (bytes != lastBytes) { lastBytes = bytes; lastWriteMs = ms(); }
             // The cart stopped acknowledging: frames outstanding past the link's 100 ms timeout (the reset, on a
-            // framed link). The first sight of it is when the console went away, near enough.
-            if (!stalled && link.reliable() && link.linkOutstanding() > 0 && link.linkResends() > 0) {
+            // framed link). The guard holds the re-sends in that stall, so it shows as the outstanding count staying
+            // up, not as a re-send; when it went up is when the console went away, near enough.
+            if (link.reliable() && link.linkOutstanding() > 0) {
+                if (outstandingSince < 0) outstandingSince = ms();
+            } else {
+                outstandingSince = -1;
+            }
+            if (!stalled && outstandingSince >= 0 && ms() - outstandingSince >= 100.0) {
                 stalled = true;
-                stallMs = ms();
+                stallMs = outstandingSince;
                 say("acks stopped (reset?)");
             }
             if (link.atMenu() != wasMenu) {
diff --git a/packages/n8/native/test/n8-timing.cpp b/packages/n8/native/test/n8-timing.cpp
index 1dc72e2..d54353e 100644
--- a/packages/n8/native/test/n8-timing.cpp
+++ b/packages/n8/native/test/n8-timing.cpp
@@ -1,7 +1,7 @@
 // retroplug-n8-hwtest timing - BlipToaster's MIDI latency, jitter and latency-under-load on a REAL console, in ms.
 //
 //   retroplug-n8-hwtest timing <idle|busy> [steps] [--raw] [--no-kick] [--pcm hw:L6,0] [--tick-ch 3] [--kick-ch 4] [--db -40]
-//                              [--wav out.wav] [--port /dev/ttyACM0]
+//                              [--wav out.wav] [--port /dev/ttyACM0] [--guard]
 //
 // Plays a 170 bpm arrangement through N8Link - the code the plugin streams to a cart with, over the reliable link
 // unless --raw - while capturing the console's analog out on the lab's Zoom L6, and times every hit from the moment
@@ -180,7 +180,7 @@ bool flag(int argc, char** argv, const char* name) {
 int runTiming(int argc, char** argv, const std::string& defaultPort) {
     if (argc < 3) {
         std::fprintf(stderr, "usage: %s timing <idle|busy> [steps] [--raw] [--pcm hw:L6,0] [--tick-ch 3] [--kick-ch 4] "
-                             "[--db -40] [--wav out.wav] [--port dev]\n", argv[0]);
+                             "[--db -40] [--wav out.wav] [--port dev] [--guard]\n", argv[0]);
         return 2;
     }
     const std::string mode  = argv[2];
@@ -188,6 +188,7 @@ int runTiming(int argc, char** argv, const std::string& defaultPort) {
     const int         steps = (argc > 3 && argv[3][0] != '-') ? std::atoi(argv[3]) : 128;
     const bool        raw   = flag(argc, argv, "--raw");
     const bool        noKick = flag(argc, argv, "--no-kick");   // no DMC: tells DMC-DMA effects from the rest
+    const bool        guard  = flag(argc, argv, "--guard");     // the menu guard on: what its probes cost the game
     const std::string pcmDev = arg(argc, argv, "--pcm", "hw:L6,0");
     const std::string port  = arg(argc, argv, "--port", defaultPort.c_str());
     const float       thr   = static_cast<float>(std::pow(10.0, std::atof(arg(argc, argv, "--db", "-40")) / 20.0));
@@ -237,6 +238,9 @@ int runTiming(int argc, char** argv, const std::string& defaultPort) {
     N8Link link(
         [](const std::string& p) -> std::unique_ptr<PortTransport> { return std::make_unique<SerialTransport>(p); });
     link.setLinkAllowed(!raw);
+    // The menu guard on, as the plugin runs it, so its probes are among the writes (a raw stream carries one every
+    // 250 ms; a framed one only in a stall). The connect seed is taken as a game, which is where this leg runs.
+    if (guard) link.setMenuCheck([](rp::n8::Edio&) { return 0; });
     link.setWriteLog([&](const std::uint8_t* d, std::size_t n, std::int64_t ns, bool resend) {
         if (resend) return;   // a hit's latency runs from its FIRST write: a re-send is part of it
         std::lock_guard<std::mutex> l(wm);
````

- [ ] **Step 4: run them, and see them pass**

```
timeout -k 10 1800 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh cmake --build /workspaces/RetroPlug-main/cuttlefish/build --target retroplug-n8-test -j$(nproc)
timeout -k 10 300 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh /workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-n8-test "[menu-guard]"
timeout -k 10 300 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh /workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-n8-test
```

Expected: PASS, the menu-guard cases and then the whole binary; the job case is task 3's, and fails as expected:

```
test cases: 11 | 10 passed | 1 failed as expected
assertions: 95 | 93 passed | 2 failed as expected
test cases:  128 |  127 passed | 1 failed as expected
assertions: 1769 | 1767 passed | 2 failed as expected
```

- [ ] **Step 5: write the cases the leave rule, the stall and the raw boundary need**

Five cases after the last, in `[menu-guard]`. Three fail against Step 3's guard, each for its own reason, which Step 7
fixes; two pin what Step 3 already does right and the fake could not show before task 1 taught it BlipToaster's ack for
unframed bytes:

- **a menu whose answer comes late is still the menu: one unanswered probe does not leave it**: a reset menu whose
  answers come 150 ms late. Step 3's guard leaves on the first miss, writes the link probe's `F4 00` and streams into
  the menu until the late `'k'` trips it again.
- **a framed link whose cart answers neither the menu's probe nor the link is streamed raw again, not held**: framed
  BlipToaster, then a game without the link comes up unseen. Step 3's guard probes the stall every second for ever, and
  the stream stays held.
- **the raw probe waits for a message to end, however long it stays open**: a SysEx head whose tail comes 700 ms
  later. Step 3's raw cadence uses the release's boundary rule, which counts a message open for 250 ms as closed, so
  the probe goes inside the SysEx.
- **a framed game that is slow or damaged resumes on its ack, and is asked at most once a second** (green against
  Step 3, and after): acks 150 ms late, and every frame reported damaged. Neither trips the guard, the stream goes on,
  and a damaged game sees at most a probe a second. The damaged section fails when the resume on an ack is taken out
  of `menuHold` (checked: `linkResends()` stops moving).

````patch
diff --git a/packages/n8/native/test/N8CartJobs.test.cpp b/packages/n8/native/test/N8CartJobs.test.cpp
index 8fe4e9f..9e70992 100644
--- a/packages/n8/native/test/N8CartJobs.test.cpp
+++ b/packages/n8/native/test/N8CartJobs.test.cpp
@@ -1932,3 +1932,132 @@ TEST_CASE("a 'k' inside the reliable link's acks is not the menu's answer", "[n8
         }));
     }
 }
+
+TEST_CASE("a menu whose answer comes late is still the menu: one unanswered probe does not leave it",
+          "[n8][menu-guard]") {
+    // The lab's menu answers '*t' in about 20 ms, well inside the probe's 100 ms, but a menu busy with a slow turn can
+    // miss that window now and then. Leaving the menu on one miss would stream into it until the next probe tripped the
+    // guard again: it takes three misses in a row.
+    Rig rig;
+    rig.connectGame();
+    rig.dev.resetToMenu();
+    REQUIRE(waitUntil([&] { return rig.host.link().atMenu(); }));
+    {
+        std::lock_guard<std::mutex> l(rig.dev.mu);
+        rig.dev.menuAnswerDelayMs = 150;  // every answer from here on comes after the probe's window
+    }
+    const auto streamed = [&] { return rig.dev.count("fifo:") - rig.dev.count("fifo:F0 2A 74 F7"); };
+    const std::size_t before = streamed();
+    bool left = false;
+    for (int i = 0; i < 300 && !left; ++i) {  // 1.5 s, six probes, each answered late
+        left = !rig.host.link().atMenu();
+        rig.push({0x90, 0x3C, 0x7F});
+        std::this_thread::sleep_for(std::chrono::milliseconds(5));
+    }
+    CHECK_FALSE(left);
+    CHECK(streamed() == before);  // no link probe (F4 00) and no note went to the menu
+}
+
+TEST_CASE("a framed link whose cart answers neither the menu's probe nor the link is streamed raw again, not held",
+          "[n8][menu-guard]") {
+    // A framed link asks only in a stall, so a RESET between songs and a game without the link started from the cart's
+    // own browser go unseen: the next frame stalls, and neither a 'k' nor an ack ever comes. Two stall probes that hear
+    // nothing (two seconds) give the window up and probe the link again, which that game does not answer.
+    Rig rig;
+    rig.dev.menu    = false;
+    rig.dev.linkRom = true;
+    rig.dev.cfg[0]  = 69;
+    rig.dev.cfg[2]  = 0;
+    rig.connect();
+    REQUIRE(waitUntil([&] { return rig.host.link().reliable(); }));
+    rig.push({0x90, 0x3C, 0x7F});
+    REQUIRE(waitUntil([&] {
+        std::lock_guard<std::mutex> l(rig.dev.mu);
+        return rig.dev.applied.size() == 1;
+    }));
+    {
+        std::lock_guard<std::mutex> l(rig.dev.mu);
+        rig.dev.linkRom = false;  // the game running now reads plain MIDI and acknowledges nothing
+    }
+    rig.push({0x90, 0x3E, 0x7F});  // the frame that stalls
+    REQUIRE(waitUntil([&] { return !rig.host.link().reliable(); }, 4000));
+    CHECK(rig.cart.connected());
+    CHECK_FALSE(rig.host.link().atMenu());
+    rig.push({0x90, 0x41, 0x7F});
+    REQUIRE(waitUntil([&] { return rig.dev.count("fifo:90 41 7F") == 1; }));  // raw, as that game reads it
+}
+
+TEST_CASE("the raw probe waits for a message to end, however long it stays open", "[n8][menu-guard]") {
+    // The probe is a SysEx of its own, so inside another message it would cut that one short. A node's bytes port can
+    // leave a SysEx open for a while; a release goes anyway after 250 ms (a held note is the worse harm), but a probe
+    // that waits costs nothing.
+    Rig rig;
+    rig.connectGame();
+    rig.push({0xF0, 0x7E, 0x7F});  // a SysEx head whose tail comes late
+    REQUIRE(waitUntil([&] { return rig.dev.count("fifo:F0 7E 7F") == 1; }));
+    const std::size_t probes = rig.dev.count("fifo:F0 2A 74 F7");
+    std::this_thread::sleep_for(std::chrono::milliseconds(700));  // past the release's bound, and two cadences
+    CHECK(rig.dev.count("fifo:F0 2A 74 F7") == probes);
+    rig.push({0x09, 0x01, 0xF7});  // the tail
+    REQUIRE(waitUntil([&] { return rig.dev.count("fifo:F0 2A 74 F7") > probes; }, 1000));
+    CHECK(lastLogIndex(rig.dev, "fifo:09 01 F7") < lastLogIndex(rig.dev, "fifo:F0 2A 74 F7"));
+}
+
+TEST_CASE("a framed game that is slow or damaged resumes on its ack, and is asked at most once a second",
+          "[n8][menu-guard]") {
+    // In a stall the guard holds every re-send, so the probe's own bytes are all a live BlipToaster reads: it drops
+    // them as unframed and acknowledges F5 <next> <next> (bliptoaster src/midi/parse_asm.s), and that ack is what
+    // resumes the stream. Neither game may trip the guard.
+    SECTION("acks that take longer than the link's timeout") {
+        Rig rig;
+        rig.dev.menu    = false;
+        rig.dev.linkRom = true;
+        rig.dev.cfg[0]  = 69;
+        rig.dev.cfg[2]  = 0;
+        rig.connect();
+        REQUIRE(waitUntil([&] { return rig.host.link().reliable(); }));
+        {
+            std::lock_guard<std::mutex> l(rig.dev.mu);
+            rig.dev.linkAckDelayMs = 150;  // from here on: the link probe's own ack must come inside its 100 ms
+        }
+        bool tripped = false;
+        for (int i = 0; i < 20; ++i) {
+            rig.push({0x90, static_cast<std::uint8_t>(0x30 + i), 0x7F});
+            std::this_thread::sleep_for(std::chrono::milliseconds(20));
+            tripped = tripped || rig.host.link().atMenu();
+        }
+        REQUIRE(waitUntil([&] {
+            std::lock_guard<std::mutex> l(rig.dev.mu);
+            return rig.dev.applied.size() == 20;
+        }));
+        CHECK_FALSE(tripped);
+        CHECK_FALSE(rig.host.link().atMenu());
+        CHECK(rig.dev.count("fifo:F0 2A 74 F7") >= 1);  // the last frame's ack came after the window had stopped
+    }
+    SECTION("every frame reported damaged") {
+        Rig rig;
+        rig.dev.menu    = false;
+        rig.dev.linkRom = true;
+        rig.dev.cfg[0]  = 69;
+        rig.dev.cfg[2]  = 0;
+        rig.connect();
+        REQUIRE(waitUntil([&] { return rig.host.link().reliable(); }));
+        rig.push({0x90, 0x3C, 0x7F});
+        REQUIRE(waitUntil([&] {
+            std::lock_guard<std::mutex> l(rig.dev.mu);
+            return rig.dev.applied.size() == 1;
+        }));
+        {
+            std::lock_guard<std::mutex> l(rig.dev.mu);
+            rig.dev.linkDamaged = true;
+        }
+        rig.push({0x90, 0x3E, 0x7F});
+        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
+        const std::uint64_t resends = rig.host.link().linkResends();
+        std::this_thread::sleep_for(std::chrono::milliseconds(500));
+        CHECK(rig.host.link().linkResends() > resends);  // the re-sends go on: the stall resumed on the probe's ack
+        CHECK_FALSE(rig.host.link().atMenu());
+        CHECK(rig.cart.connected());
+        CHECK(rig.dev.count("fifo:F0 2A 74 F7") <= 3);  // a probe a second, not one a re-send
+    }
+}
````

- [ ] **Step 6: run them, and see them fail**

```
timeout -k 10 1800 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh cmake --build /workspaces/RetroPlug-main/cuttlefish/build --target retroplug-n8-test -j$(nproc)
timeout -k 10 300 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh /workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-n8-test "[menu-guard]"
```

Expected: FAIL, the three cases, each for its reason: the late answer leaves the menu and streams into it (`left` is
true, and a stream byte went out), the silent game keeps the link framed and held past 4 s, and the probe is written
inside the open SysEx. The slow and damaged game's case passes; the job case fails as expected:

```
-------------------------------------------------------------------------------
a framed link whose cart answers neither the menu's probe nor the link is
streamed raw again, not held
-------------------------------------------------------------------------------
/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8CartJobs.test.cpp:1961
...............................................................................

/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8CartJobs.test.cpp:1983: FAILED:
  REQUIRE( waitUntil([&] { return !rig.host.link().reliable(); }, 4000) )
with expansion:
  false

-------------------------------------------------------------------------------
a job submitted at a menu the console's reset reached is taken
-------------------------------------------------------------------------------
/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8CartJobs.test.cpp:1871
...............................................................................

/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8CartJobs.test.cpp:1884: FAILED:
  CHECK( why == "" )
with expansion:
  "the battery file can only be reached at the N8 menu: power-cycle to the menu
  first"
  ==
  ""

/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8CartJobs.test.cpp:1885: FAILED:
  REQUIRE( taken )
with expansion:
  false

-------------------------------------------------------------------------------
a menu whose answer comes late is still the menu: one unanswered probe does not
leave it
-------------------------------------------------------------------------------
/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8CartJobs.test.cpp:1936
...............................................................................

/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8CartJobs.test.cpp:1957: FAILED:
  CHECK_FALSE( left )
with expansion:
  !true

/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8CartJobs.test.cpp:1958: FAILED:
  CHECK( streamed() == before )
with expansion:
  2 == 1

-------------------------------------------------------------------------------
the raw probe waits for a message to end, however long it stays open
-------------------------------------------------------------------------------
/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8CartJobs.test.cpp:1990
...............................................................................

/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8CartJobs.test.cpp:2000: FAILED:
  CHECK( rig.dev.count("fifo:F0 2A 74 F7") == probes )
with expansion:
  4 == 2

===============================================================================
test cases:  15 |  11 passed | 3 failed | 1 failed as expected
assertions: 126 | 120 passed | 4 failed | 2 failed as expected
```

- [ ] **Step 7: three misses to leave, two silent stalls to give the link up, and the strict boundary**

At the menu an unanswered probe counts a miss and only `kMenuLeaveMisses` (3) in a row leave; a heard `'k'` clears the
count. In a stall, a probe that has heard neither a `'k'` nor an ack for its second counts a stall miss, and at
`kStallMisses` (2) the window and the ring are given up (counted toward the release mark, as at the menu) and the link
probed again. The raw probe, and from task 3 the raw quiet rule, wait for `betweenMessages()`, which has no time bound.
A flush resets the scan, and a turn whose scan stands halfway through an ack is not quiet. The `menuHold` comment says
what BlipToaster does with the probe's bytes, which is what resumes a stall.

````patch
diff --git a/packages/n8/native/src/n8/desktop/N8Link.cpp b/packages/n8/native/src/n8/desktop/N8Link.cpp
index 62a2bc8..ba3bde1 100644
--- a/packages/n8/native/src/n8/desktop/N8Link.cpp
+++ b/packages/n8/native/src/n8/desktop/N8Link.cpp
@@ -15,10 +15,16 @@ namespace {
 using steady = std::chrono::steady_clock;
 // How often the guard asks where the cart runs (on a raw link, and at the menu whatever the link); how long the menu's
 // answer may take before the cart is taken for a game, which ignores the probe (N8Host's seed waits as long, its
-// kMenuAnswerMs); and how long a framed hold waits for an answer or an ack before it asks again.
+// kMenuAnswerMs); and how long a framed hold waits for an answer or an ack before it asks again. The lab's menu
+// answers in about 20 ms (27 ms the worst of 8, at a power-on menu), so the answer window has room to spare.
 constexpr std::int64_t kMenuProbeNs  = 250'000'000;
 constexpr std::int64_t kMenuAnswerNs = 100'000'000;
 constexpr std::int64_t kMenuHoldNs   = 1'000'000'000;
+// How many probes in a row the menu may leave unanswered before the cart is taken for a game (one slow turn of the
+// menu's loop must not stream into it: about 750 ms for a game booted by hand), and how many stall probes a framed link
+// may hear nothing at all to before it gives the window up (see menuHold).
+constexpr int kMenuLeaveMisses = 3;
+constexpr int kStallMisses     = 2;
 // RP_N8LINK_TRACE=1 logs every byte the link writes and every back-channel byte it reads (stderr) - read once.
 bool traceOn() {
     static const bool on = std::getenv("RP_N8LINK_TRACE") != nullptr;
@@ -65,6 +71,9 @@ std::size_t N8Link::readPort(std::uint8_t* buffer, std::size_t size, std::uint32
 }
 
 void N8Link::flushPort() {
+    // What the flush drops may end halfway through an ack: the scan starts afresh with the input, or the bytes after it
+    // (the menu's 'k' among them) would be read as that ack's.
+    backScan_ = {};
     serialPort_->flushInput();
     if (std::string said = serialPort_->failure(); !said.empty()) throw std::runtime_error(said);
 }
@@ -120,6 +129,8 @@ bool N8Link::connect(const std::string& port) {
     menuProbeDueNs_  = 0;
     menuGameUntilNs_ = 0;
     menuAnswered_    = false;
+    menuMisses_      = 0;
+    stallMisses_     = 0;
     backScan_        = {};
     acksHeard_       = 0;
     acksAtProbe_     = 0;
@@ -259,9 +270,8 @@ void N8Link::probeLink() {
     // Whatever the port already holds goes before the sync: a seed check that timed out at connect can leave the tail
     // of its reply in the input, and bytes that spell the ack's own F5 <next> would turn the framed link on for a ROM
     // that reads no frames. Nothing is owed at either probe site (connect, before any frame; leaveMenu, after a guard
-    // probe nothing answered), so no real ack is dropped; the scan starts afresh with the input.
+    // probe nothing answered), so no real ack is dropped.
     flushPort();
-    backScan_ = {};
     // F4 00 is the link's sync: a BlipToaster with the link answers it with an ack (F5 00) on the back-channel the
     // moment its FIFO runs empty; any other ROM ignores an undefined System Common status. ~100 ms covers a busy
     // ROM and the USB round trip with room to spare - and costs nothing after connect.
@@ -370,21 +380,31 @@ void N8Link::serviceLinkResends(std::int64_t now) {
 // - framed, only once the window has STOPPED MOVING (the oldest frame unacknowledged past the link's own timeout, which
 //   is the console reset), holding new frames and re-sends until a 'k' (the menu) or an ack (a game whose acks are
 //   merely lost or damaged: its stream goes on, and a stall is not asked about again for a second), and asking again
-//   each second that hears neither. A framed game never sees the probe outside a stall.
+//   each second that hears neither. A framed game never sees the probe outside a stall. A live BlipToaster drops the
+//   probe's bytes as unframed and acknowledges F5 <next> <next> for them (bliptoaster src/midi/parse_asm.s), so a game
+//   whose own acks were lost still answers the stall. Two stall probes that hear nothing at all are neither the menu
+//   nor this link's game: a game without the link, started from the cart's browser after a reset the guard never saw
+//   (a framed link asks only in a stall). The window and what the ring held for it go, counted, and the link is
+//   probed again: a game with the link re-syncs framed, any other is streamed raw.
 // - at the menu, every 250 ms whatever the link: a game the user starts from the cart's own browser answers no probe,
-//   and one left unanswered for 100 ms leaves the menu (leaveMenu runs the link probe again).
+//   and three left unanswered in a row leave the menu (leaveMenu runs the link probe again); a single miss is a slow
+//   turn of the menu's, and leaving on it would stream into the menu until the next probe.
 bool N8Link::menuHold(std::int64_t now) {
     if (!menuCheck_) return false;
     if (menuAnswered_) {
         menuAnswered_  = false;
         menuProbeAtNs_ = 0;
+        menuMisses_    = 0;
         reportMenuState(true);
     }
     if (atMenu_.load(std::memory_order_relaxed)) {
         if (menuProbeAtNs_ != 0 && now - menuProbeAtNs_ >= kMenuAnswerNs) {
             menuProbeAtNs_ = 0;
-            reportMenuState(false);
-            return atMenu_.load(std::memory_order_relaxed);
+            if (++menuMisses_ >= kMenuLeaveMisses) {
+                menuMisses_ = 0;
+                reportMenuState(false);
+                return atMenu_.load(std::memory_order_relaxed);
+            }
         }
         if (menuProbeAtNs_ == 0 && now >= menuProbeDueNs_) writeMenuProbe(now);
         return true;  // nothing new goes out while it says menu
@@ -392,9 +412,19 @@ bool N8Link::menuHold(std::int64_t now) {
     if (rel_ && rel_->progressOverdue(now) && now >= menuGameUntilNs_) {
         if (menuProbeAtNs_ != 0 && acksHeard_ != acksAtProbe_) {
             menuProbeAtNs_   = 0;
+            stallMisses_     = 0;
             menuGameUntilNs_ = now + kMenuHoldNs;
             return false;
         }
+        if (menuProbeAtNs_ != 0 && now - menuProbeAtNs_ >= kMenuHoldNs && ++stallMisses_ >= kStallMisses) {
+            menuProbeAtNs_ = 0;
+            stallMisses_   = 0;
+            discardRing();
+            rel_->reset();
+            linkOutstanding_.store(0, std::memory_order_relaxed);
+            probeLink();
+            return false;
+        }
         if (menuProbeAtNs_ == 0 || now - menuProbeAtNs_ >= kMenuHoldNs) writeMenuProbe(now);
         return true;  // hold the re-send until the cart has answered
     }
@@ -404,7 +434,7 @@ bool N8Link::menuHold(std::int64_t now) {
         return false;
     }
     if (menuProbeAtNs_ != 0 && now - menuProbeAtNs_ >= kMenuAnswerNs) menuProbeAtNs_ = 0;  // unanswered: a game
-    if (menuProbeAtNs_ == 0 && now >= menuProbeDueNs_ && atMessageBoundary(now)) writeMenuProbe(now);
+    if (menuProbeAtNs_ == 0 && now >= menuProbeDueNs_ && betweenMessages()) writeMenuProbe(now);
     return false;
 }
 
@@ -463,6 +493,8 @@ void N8Link::enterMenu() {
     // A probe of the guard's still awaited is the menu's to answer: whatever becomes of its 'k', it is not "nothing
     // answered", which at the menu would read as a game.
     menuProbeAtNs_   = 0;
+    menuMisses_      = 0;
+    stallMisses_     = 0;
     menuGameUntilNs_ = 0;
     if (traceOn()) std::fprintf(stderr, "[n8link] the cart is at its menu: the stream is dropped\n");
 }
@@ -470,6 +502,7 @@ void N8Link::enterMenu() {
 void N8Link::leaveMenu() {
     if (traceOn()) std::fprintf(stderr, "[n8link] the cart runs a game: streaming, and probing the link\n");
     menuProbeAtNs_   = 0;
+    stallMisses_     = 0;
     menuGameUntilNs_ = 0;
     // A game started from the cart's own browser may speak the reliable link, and the connect-time probe could not have
     // told: the frames it would carry start from a sync here.
@@ -489,8 +522,8 @@ void N8Link::discardRing() {
 
 bool N8Link::quietForRead(std::int64_t now) const {
     // A probe of the guard's whose answer may still be coming: a 'k' read inside a reply would be taken for its first
-    // byte, and lost to the guard.
-    if (menuProbeAwaited(now)) return false;
+    // byte, and lost to the guard. Nor halfway through an ack, whose tail would be the reply's first bytes.
+    if (menuProbeAwaited(now) || backScan_.state != 0) return false;
     // Nothing outstanding, so no ack is owed (an ack the ROM repeats for a frame it already acknowledged is the one
     // exception, see OnControl). Or a cart that has said nothing for the pause's ack bound since the oldest outstanding
     // frame first went out: one at its menu (a console reset keeps USB and the link up, and the menu acknowledges
@@ -752,6 +785,8 @@ std::size_t N8Link::wireDataBytes(std::uint8_t status) {
     }
 }
 
+bool N8Link::betweenMessages() const { return !wireSysex_ && wireExpect_ == 0; }
+
 bool N8Link::atMessageBoundary(std::int64_t now) const {
     if (!wireSysex_ && wireExpect_ == 0) return true;
     // A stream left open this long is one nothing is going to finish (a node that died mid-SysEx): its notes are
diff --git a/packages/n8/native/src/n8/desktop/N8Link.hpp b/packages/n8/native/src/n8/desktop/N8Link.hpp
index ba3e0d6..99fc21a 100644
--- a/packages/n8/native/src/n8/desktop/N8Link.hpp
+++ b/packages/n8/native/src/n8/desktop/N8Link.hpp
@@ -261,6 +261,10 @@ private:
     void        noteWireBytes(const std::uint8_t* data, std::size_t n, std::int64_t now, bool streamed);
     static std::size_t wireDataBytes(std::uint8_t status);
     bool        atMessageBoundary(std::int64_t now) const;
+    // serial thread: no message open on the wire at all, with no time bound: where the guard's probe may go, since a
+    // probe that waits for a long-open message costs only its delay (atMessageBoundary lets a release through after
+    // kMidMessageNs, a held note being the worse of its two harms).
+    bool        betweenMessages() const;
     void runJob(TimedChunk& chunk, bool& have);          // serial thread: pause, run the pending job, resume
     void drainForJob(TimedChunk& chunk, bool& have);     // serial thread: what was pushed before the pause goes out
     void stopStreaming();                                // serial thread: stop and close the port (AfterJob::Stop)
@@ -331,6 +335,10 @@ private:
     std::int64_t               menuProbeAtNs_   = 0;
     std::int64_t               menuProbeDueNs_  = 0;
     std::int64_t               menuGameUntilNs_ = 0;
+    // Probes in a row the menu left unanswered (it leaves at kMenuLeaveMisses), and stall probes in a row that heard
+    // nothing at all (the link is given up at kStallMisses). Serial thread, reset at each connect.
+    int                        menuMisses_  = 0;
+    int                        stallMisses_ = 0;
     // What the back-channel has said (readBackChannel): the menu's 'k', and how many whole acks against the count when
     // the probe went out. The scan's state runs on across reads, since an ack can be split between two. Serial thread,
     // reset at each connect.
````

Then three one-line corrections from the plan review's re-check: the fake acknowledges no real-time byte (the ROM skips
F8-FE), a frame header turns the fake's link on as a sync does, and a moving window clears a miss left from an earlier
stall, so the next stall still waits two seconds before it gives the link up:

````patch
diff --git a/packages/n8/native/test/FakeN8.hpp b/packages/n8/native/test/FakeN8.hpp
--- a/packages/n8/native/test/FakeN8.hpp
+++ b/packages/n8/native/test/FakeN8.hpp
@@ -467,7 +467,7 @@
             case Idle:
                 if (b == 0xF4) link_ = WantSync;
                 else if (b == 0xF5) link_ = WantSeq;
-                else if (linkOn_) ackOwed_ = true;
+                else if (linkOn_ && b < 0xF8) ackOwed_ = true;  // real-time bytes are skipped, never acknowledged
                 return;
             case WantSync:
                 next_ = b;
@@ -478,6 +478,7 @@
                 link_ = Idle;
                 return;
             case WantSeq:
+                linkOn_ = true;  // a frame header turns the link on, as a sync does
                 seq_ = b;
                 msg_.clear();
                 link_ = InMsg;
diff --git a/packages/n8/native/src/n8/desktop/N8Link.cpp b/packages/n8/native/src/n8/desktop/N8Link.cpp
--- a/packages/n8/native/src/n8/desktop/N8Link.cpp
+++ b/packages/n8/native/src/n8/desktop/N8Link.cpp
@@ -429,8 +429,10 @@
         return true;  // hold the re-send until the cart has answered
     }
     if (rel_) {
-        // The window moves (or a stall's ack was heard within the second): no probe of a stall is awaited any more.
+        // The window moves (or a stall's ack was heard within the second): no probe of a stall is awaited any more,
+        // and a miss left from an earlier stall does not count toward the next one's drop.
         menuProbeAtNs_ = 0;
+        stallMisses_   = 0;
         return false;
     }
     if (menuProbeAtNs_ != 0 && now - menuProbeAtNs_ >= kMenuAnswerNs) menuProbeAtNs_ = 0;  // unanswered: a game
````

- [ ] **Step 8: run them all, and see them pass**

```
timeout -k 10 1800 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh cmake --build /workspaces/RetroPlug-main/cuttlefish/build --target retroplug-n8-test -j$(nproc)
timeout -k 10 300 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh /workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-n8-test "[menu-guard]"
timeout -k 10 300 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh /workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-n8-test
```

Expected: PASS, the menu-guard cases and then the whole binary; the job case is task 3's, and fails as expected:

```
test cases:  15 |  14 passed | 1 failed as expected
assertions: 129 | 127 passed | 2 failed as expected
test cases:  132 |  131 passed | 1 failed as expected
assertions: 1803 | 1801 passed | 2 failed as expected
```

- [ ] **Step 9: the wider suites this task reaches**

The n8 binary three times (timing cases), the engine-graph binary (its N8Adapters case drives this link behind a real
Engine), the device-mode binary, the n8 runners, the TSan build of the n8 binary (the guard's state is the serial
thread's, and the probe is read across threads only through `atMenu_` and `backChannelQuiet_`), and the hardware test
compiled, never run:

```
timeout -k 10 1800 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh cmake --build /workspaces/RetroPlug-main/cuttlefish/build --target retroplug-n8-test retroplug-engine-graph-test retroplug-device-mode-test retroplug-host retroplug-n8-hwtest -j$(nproc)
timeout -k 10 300 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh /workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-n8-test
timeout -k 10 300 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh /workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-n8-test
timeout -k 10 300 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh /workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-n8-test
timeout -k 10 1800 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh /workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-engine-graph-test
timeout -k 10 600 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh /workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-device-mode-test
timeout -k 10 1800 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh pnpm -C /workspaces/RetroPlug-main/cuttlefish test:n8
timeout -k 10 1800 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh pnpm -C /workspaces/RetroPlug-main/cuttlefish test:native n8
timeout -k 10 1800 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh cmake --build /workspaces/RetroPlug-main/cuttlefish/build-tsan --target retroplug-n8-test -j$(nproc)
TSAN_OPTIONS="suppressions=/workspaces/RetroPlug-main/cuttlefish/packages/native/test/sanitizer/tsan.supp halt_on_error=1" timeout -k 10 1800 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh /workspaces/RetroPlug-main/cuttlefish/build-tsan/bin/retroplug-n8-test
```

Expected: exit 0 for each; `[100%] Built target retroplug-n8-hwtest`, and:

```
test cases:  132 |  131 passed | 1 failed as expected
assertions: 1803 | 1801 passed | 2 failed as expected
test cases:  132 |  131 passed | 1 failed as expected
assertions: 1803 | 1801 passed | 2 failed as expected
test cases:  132 |  131 passed | 1 failed as expected
assertions: 1803 | 1801 passed | 2 failed as expected
test cases:     342 |     331 passed | 11 skipped
assertions: 1702548 | 1702548 passed |  0 skipped
All tests passed (1504408 assertions in 96 test cases)
All tests passed (17 assertions in 2 test cases)
test cases:  132 |  131 passed | 1 failed as expected
assertions: 1803 | 1801 passed | 2 failed as expected
# 2 plugin test binary(ies) passed (jobs=16)
# 3 native test file(s) passed (jobs=16)
test cases:  132 |  131 passed | 1 failed as expected
assertions: 1803 | 1801 passed | 2 failed as expected
```

In order: the n8 binary three times, the engine-graph binary, the device-mode binary, `test:n8` and `test:native n8`
as in task 1, then the TSan run, with no ThreadSanitizer report. The job case is task 3's, so it still fails as
expected everywhere.

- [ ] **Step 10: commit**

```
git -C /workspaces/RetroPlug-main/cuttlefish add packages/n8/native/src/n8/desktop/N8Link.hpp \
  packages/n8/native/src/n8/desktop/N8Link.cpp packages/n8/native/src/n8/desktop/N8Host.cpp \
  packages/n8/native/test/N8CartJobs.test.cpp packages/n8/native/test/N8CartLink.test.cpp \
  packages/n8/native/test/n8-sd-hwtest.cpp packages/n8/native/test/n8-timing.cpp
git -C /workspaces/RetroPlug-main/cuttlefish commit -F - <<'MSG'
fix(n8): the menu guard asks the menu, so a console reset trips it

A console RESET leaves the FPGA config block at the game's mapper, so a guard
that read map_idx streamed into the menu until it stopped on ERROR:40. The
guard now asks: F0 2A 74 F7, a SysEx a MIDI game ignores whose '*t' the menu
answers with 'k', written between whole messages. Raw links probe every 250 ms
with no wait; framed links probe once the window stops moving and hold until a
'k' or an ack (BlipToaster acks the probe's unframed bytes), and two silent
stall probes give the window up and probe the link again; at the menu the probe
runs on, and three unanswered in a row leave it. The back-channel is read every
turn, and a 0x6B outside an F5 x y ack is the menu's answer. The connect seed is
the same probe with a 100 ms wait. The config read, its forced read, its flushes
and its failure back-off go. retroplug-n8-hwtest gains menuguard raw and
timing --guard, for the rig's raw leg and the probe's cost.
MSG
```

---

## Task 3: the job check asks the menu

`N8Host::probeMenu`, a submission's refusal, reads map_idx too, so a job submitted at a menu the console's reset reached
is refused as "a game is running", and so is the job's own first step (`atMenu` in `submitCartJob`). Both now ask the
menu with task 2's `askMenu`: the probe and a 100 ms wait for the `'k'`, on a quiet turn (a job may wait); the
trip-only `reportMenuState(true)` stays. Since the probe is bytes on the stream, a raw link is quiet for a read only
between two messages (`betweenMessages`), so it never cuts one short, and `N8Link::noteMenuProbe()` books each such
probe on the raw stream's running-status tracker, so the game's running status, which its `F0` ended, is written back
before the stream's next chunk that runs on it. `kMenuMapper` goes; `runningMapper` stays for the expansion volume's
Auto and the connect's booted-image check. The refusals say the probe's words: "the reliable link never went quiet for
the menu's probe: ..." and "the cart's port failed the menu's probe". The probe holds the serial thread, and so the
stream, for its 100 ms wait at a running game (a submission there is refused); that is documented in spec/13 rather
than made asynchronous, since a submission against a running game is a refusal the user asked for, and routing the job
probe through the stream's own probe and scan would put a second requester on the serial thread's guard state for that.

This task finishes spec/07's "The menu guard misses a console reset", so its commit deletes it (checklist line and
entry) for the one hardware line that is left, "The N8 menu's residual window", and spec/13's link to the deleted
anchor points at the design instead (task 4 rewrites that paragraph).

What it turns red, and the fixes: four cases took a job with a game's reliable link up by making the fake's config block
say 255 while a game ran (the sequence case, the probe-wait section and the two pause-bound sections). A job check that
asks the menu needs the fake's game to answer the probe instead, so `FakeN8` gains `gameAnswersProbe` (the running game
answers `'*t'` and, like the menu, acks nothing for the probe's bytes; the fake's combination, never a real cart's) and
those cases set it; the busy-link case's expected refusal changes words; the comments that named the config read say
the probe.

**Files** (line numbers are the file at `6a4b3fb72`):

- Modify: `packages/n8/native/src/n8/desktop/N8Host.cpp:66-80` (`kMenuMapper` goes; the probe's comments),
  `:207-228` (OnControl's probe), `:324` (`probeMenu`), `:435-441` (`submitCartJob`'s refusals), `:461-467` (the job's
  `atMenu`).
- Modify: `packages/n8/native/src/n8/desktop/N8Host.hpp:142-153` (`submitCartJob`'s doc), `:167-171` (`probeMenu`),
  `:192-197` (`menuProbeAnswer_`).
- Modify: `packages/n8/native/src/n8/desktop/N8Link.hpp:41-53` (OnControl's doc), `:104` (`noteMenuProbe`),
  `:111-115` (`backChannelQuiet`); `packages/n8/native/src/n8/desktop/N8Link.cpp:484` (`quietForRead`), and
  `noteMenuProbe` beside `menuProbeAwaited`.
- Modify: `spec/07-remaining-work.md:13` (the checklist line), `:114-123` (the entry, replaced);
  `spec/13-subsystems.md:129-130` (the link).
- Test: `packages/n8/native/test/FakeN8.hpp` (`gameAnswersProbe`), `packages/n8/native/test/N8CartJobs.test.cpp` (Step
  1: the job case's tag, three menu cases moved to a reset menu, and the running-status case; Step 3: the four cases,
  the busy-link refusal and comments), `packages/n8/native/test/N8CartLink.test.cpp:346-347` and
  `packages/native/test/engine-graph/N8Adapters.test.cpp:192-197` (comments).

**Interfaces:**

- Consumes: `askMenu`, `kMenuAnswerMs`, `N8Link::betweenMessages` (task 2), `N8Link::reportMenuState`,
  `N8Link::noteWireBytes`.
- Produces: `N8Link::noteMenuProbe()` (serial thread); `N8Host::probeMenu()` answering 1 (menu), 0 (game), -1 (the
  port failed), -2, -3; `N8Host::menuProbeAnswer_` (renamed from `menuProbeIndex_`); `FakeN8::gameAnswersProbe`.

### Steps

- [ ] **Step 1: drop the job case's tag, move three menu cases to a reset menu, and pin the running status**

Three cases that put the cart at its menu by hand while a game ran used the power-on kind (`menu = true`, which
`peek` answers with map_idx 255): the framed job after a reset, the job under a stream into the menu, and the job
submitted while the ring holds a push. They now press RESET (`resetToMenu()`), the menu this work is about, and a job
there is refused today. A new case pins that a submission's probe on a raw stream has the game's running status
written back: on today's code no probe goes out at all, and once task 3 sends one, the case fails when
`noteMenuProbe()` is not called (checked: `count("fifo:90")` stays 1).

````patch
diff --git a/packages/n8/native/test/N8CartJobs.test.cpp b/packages/n8/native/test/N8CartJobs.test.cpp
index 9e70992..f370525 100644
--- a/packages/n8/native/test/N8CartJobs.test.cpp
+++ b/packages/n8/native/test/N8CartJobs.test.cpp
@@ -864,10 +864,7 @@ TEST_CASE("a cart reset to its menu with a frame its game never acknowledged sti
     // The console's reset button: the N8 goes back to its menu with USB still up, and the menu acknowledges nothing (it
     // skips every byte that is not one of its commands). The frame below stops the window, the guard asks, and the
     // menu's answer resets the stale window there rather than re-sending it into a command parser.
-    {
-        std::lock_guard<std::mutex> l(rig.dev.mu);
-        rig.dev.menu = true;
-    }
+    rig.dev.resetToMenu();
     rig.push({0x80, 0x3C, 0x00});
     REQUIRE(waitUntil([&] { return rig.host.link().atMenu(); }));
     CHECK(rig.host.link().linkOutstanding() == 0);  // the stale frame went with the window
@@ -1632,9 +1629,9 @@ TEST_CASE("a job submitted while the ring holds a push, right after the console
     rig.connectGame();
     rig.push({0x90, 0x3C, 0x7F});
     REQUIRE(waitUntil([&] { return rig.dev.count("fifo:90 3C 7F") == 1; }));
+    rig.dev.resetToMenu();  // the console's reset button, a moment before the submission
     {
         std::lock_guard<std::mutex> l(rig.dev.mu);
-        rig.dev.menu = true;  // the console's reset button, a moment before the submission
         rig.dev.files[kGameBattery] = saveOf(1024, 13);
     }
     // Still in the ring when the job is taken: a lookahead this long is what the pause's drain would deliver first.
@@ -1687,10 +1684,7 @@ TEST_CASE("a job submitted under a stream into the menu is accepted, and the str
         std::lock_guard<std::mutex> l(rig.dev.mu);
         return rig.dev.applied.size() == 1;
     }));
-    {
-        std::lock_guard<std::mutex> l(rig.dev.mu);
-        rig.dev.menu = true;  // the console's reset button: the game is gone, and the link is still framed
-    }
+    rig.dev.resetToMenu();  // the console's reset button: the game is gone, and the link is still framed
     std::atomic<bool> streaming{true};
     std::thread feeder([&] {
         while (streaming.load(std::memory_order_relaxed)) {
@@ -1868,7 +1862,7 @@ TEST_CASE("a framed stream of '*'-bearing messages after a reset never drives th
     CHECK(rig.dev.menu);
 }
 
-TEST_CASE("a job submitted at a menu the console's reset reached is taken", "[n8][menu-guard][!shouldfail]") {
+TEST_CASE("a job submitted at a menu the console's reset reached is taken", "[n8][menu-guard]") {
     // The block keeps the game's mapper after a reset, so a check that reads it refuses the job as "a game runs" at the
     // one place a battery file can be reached.
     Rig rig;
@@ -2061,3 +2055,25 @@ TEST_CASE("a framed game that is slow or damaged resumes on its ack, and is aske
         CHECK(rig.dev.count("fifo:F0 2A 74 F7") <= 3);  // a probe a second, not one a re-send
     }
 }
+
+TEST_CASE("a submission's menu probe on a raw stream has the game's running status written back after it",
+          "[n8][menu-guard]") {
+    // The probe is a SysEx, and a game's MIDI parser loses running status at its F0. The stream's tracker writes the
+    // status back before a chunk that runs on it, but only for bytes it knows went out: a submission's probe is
+    // N8Host's, written on the serial thread outside the stream's own turns, and is booked all the same.
+    Rig rig;
+    rig.host.link().setMenuCheck({});  // no guard: its own probes would have the status written back anyway
+    rig.connectGame();
+    rig.push({0x90, 0x3C, 0x7F});
+    REQUIRE(waitUntil([&] { return rig.dev.count("fifo:90 3C 7F") == 1; }));
+    Job job;
+    std::string why;
+    CHECK_FALSE(rig.submit(battery("dump"), job, &why));  // a game: refused, once the probe has gone unanswered
+    CHECK(why == "the battery file can only be reached at the N8 menu: power-cycle to the menu first");
+    REQUIRE(rig.dev.count("fifo:F0 2A 74 F7") == 1);
+    rig.push({0x3E, 0x00});  // a note-off by running status, as a bytes port may hand it over
+    REQUIRE(waitUntil([&] { return rig.dev.count("fifo:3E 00") == 1; }));
+    CHECK(rig.dev.count("fifo:90") == 2);  // the note-on, and its status written back alone
+    CHECK(lastLogIndex(rig.dev, "fifo:F0 2A 74 F7") < lastLogIndex(rig.dev, "fifo:90"));
+    CHECK(lastLogIndex(rig.dev, "fifo:90") < lastLogIndex(rig.dev, "fifo:3E 00"));
+}
````

- [ ] **Step 2: run them, and see them fail**

```
timeout -k 10 1800 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh cmake --build /workspaces/RetroPlug-main/cuttlefish/build --target retroplug-n8-test -j$(nproc)
timeout -k 10 300 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh /workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-n8-test
```

Expected: FAIL, for the reason this task fixes: the guard has tripped at the reset menu (task 2), but the job check
reads map_idx (69, or 0 for a raw game) and refuses the job as a game, in the job case and the three moved ones; and the
running-status case sees no probe on the wire:

```
-------------------------------------------------------------------------------
a submission's menu probe on a raw stream has the game's running status written
back after it
-------------------------------------------------------------------------------
/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8CartJobs.test.cpp:2059
...............................................................................

/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8CartJobs.test.cpp:2073: FAILED:
  REQUIRE( rig.dev.count("fifo:F0 2A 74 F7") == 1 )
with expansion:
  0 == 1

-------------------------------------------------------------------------------
a job submitted under a stream into the menu is accepted, and the stream is
dropped there
-------------------------------------------------------------------------------
/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8CartJobs.test.cpp:1669
...............................................................................

/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8CartJobs.test.cpp:1702: FAILED:
  CHECK( accepted )
with expansion:
  false

/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8CartJobs.test.cpp:1704: FAILED:
  REQUIRE( waitUntil([&] { return job.finished(); }) )
with expansion:
  false

-------------------------------------------------------------------------------
a job submitted while the ring holds a push, right after the console resets to
its menu, writes nothing into the menu
-------------------------------------------------------------------------------
/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8CartJobs.test.cpp:1623
...............................................................................

/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8CartJobs.test.cpp:1641: FAILED:
  REQUIRE( rig.submit(battery("dump"), job) )
with expansion:
  false

-------------------------------------------------------------------------------
a job submitted at a menu the console's reset reached is taken
-------------------------------------------------------------------------------
/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8CartJobs.test.cpp:1865
...............................................................................

/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8CartJobs.test.cpp:1878: FAILED:
  CHECK( why == "" )
with expansion:
  "the battery file can only be reached at the N8 menu: power-cycle to the menu
  first"
  ==
  ""

/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8CartJobs.test.cpp:1879: FAILED:
  REQUIRE( taken )
with expansion:
  false

-------------------------------------------------------------------------------
a cart reset to its menu with a frame its game never acknowledged still takes a
job
-------------------------------------------------------------------------------
/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8CartJobs.test.cpp:848
...............................................................................

/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8CartJobs.test.cpp:882: FAILED:
  REQUIRE( rig.submit(battery("dump"), job, &why) )
with expansion:
  false

===============================================================================
test cases:  133 |  128 passed | 5 failed
assertions: 1796 | 1789 passed | 7 failed
```

- [ ] **Step 3: implement it**

````patch
diff --git a/packages/n8/native/src/n8/desktop/N8Host.cpp b/packages/n8/native/src/n8/desktop/N8Host.cpp
index 3d40d91..8ae963e 100644
--- a/packages/n8/native/src/n8/desktop/N8Host.cpp
+++ b/packages/n8/native/src/n8/desktop/N8Host.cpp
@@ -63,20 +63,15 @@ int runningMapper(Edio& edio) {
     return ((cfg[2] >> 4) << 8) | cfg[0];
 }
 
-// The mapper the N8 OS runs its own file browser on: the config block's map_idx while the menu shows (read on an N8
-// Pro; mapConfig.ts: "the OS clears it to force map 255"). A system-region read, so telling the menu from a game this
-// way touches neither the cart's bus nor a running game's FIFO.
-constexpr int kMenuMapper = 255;
-
 // What N8Host::probeMenu answers when the link went down while the probe was on its way to the serial thread, and when
 // its deadline passed with the reliable link still busy (a cart acknowledging the stream, so no turn was quiet for the
-// read).
+// probe's read).
 constexpr int kProbeLinkDown = -2;
 constexpr int kProbeLinkBusy = -3;
 
-// How long a submission waits for the serial thread's answer to its menu probe: a config-block read is one USB round
-// trip, taken between two MIDI chunks. A cart that has stopped answering gets its refusal either way, and this keeps
-// a control thread (the DAW's) from waiting out the read's own 2 s timeout to learn it.
+// How long a submission waits for the serial thread's answer to its menu probe: the probe is one USB write and at most
+// the menu's answer time (kMenuAnswerMs), taken between two MIDI chunks. A link that never gives it a quiet turn gets
+// its refusal either way, and this keeps a control thread (the DAW's) from waiting on it longer.
 constexpr int kMenuProbeMs = 1000;
 
 // How long the N8 menu's answer to a probe may take: one USB round trip and a turn of the menu's loop. Past it nothing
@@ -213,10 +208,10 @@ N8Host::N8Host(N8Link::PortFactory factory, PortLister lister, std::string confi
     // answers its probe. -1 (the port failed) leaves the link's state as it was, and its own probes ask again.
     link_.setMenuCheck([](Edio& edio) { return askMenu(edio); });
     link_.setOnControl([this](Edio& edio, bool backChannelQuiet) {
-        // The config block a submission's refusal reads (probeMenu). Taken here, between chunks, for the reason the
-        // volume is: a system-region read on the thread that owns the port, putting no byte in a running game's FIFO.
-        // Ahead of the volume, and a submission asks the link for a quiet turn of its own (N8Link::wakeControl), so a
-        // config write that keeps failing does not hold it up.
+        // The menu probe a submission's refusal asks for (probeMenu). Taken here, between chunks, on the thread that
+        // owns the port, as the volume is: kMenuProbe is a SysEx a running game ignores, so the stream's game hears
+        // nothing it would play. Ahead of the volume, and a submission asks the link for a quiet turn of its own
+        // (N8Link::wakeControl), so a config write that keeps failing does not hold it up.
         //
         // Both reads below wait for a quiet turn (N8Link::OnControl): an ack for an outstanding frame can arrive
         // inside a read, and would be taken for the reply's first bytes while the reply's tail was left in the input
@@ -225,13 +220,14 @@ N8Host::N8Host(N8Link::PortFactory factory, PortLister lister, std::string confi
         // covers a cart that keeps acknowledging.
         const unsigned probe = menuProbeGeneration_.load(std::memory_order_acquire);
         if (backChannelQuiet && probe != menuProbeApplied_.load(std::memory_order_relaxed)) {
-            const int mapper = runningMapper(edio);
-            menuProbeIndex_.store(mapper, std::memory_order_relaxed);
-            // The same read tells the guard the cart is at its menu, and it does so before the submission that asked
+            const int menu = askMenu(edio);
+            link_.noteMenuProbe();  // on a raw stream's wire too, where it ends the game's running status
+            menuProbeAnswer_.store(menu, std::memory_order_relaxed);
+            // The same answer tells the guard the cart is at its menu, and it does so before the submission that asked
             // for it can hand over a job: a pause's drain must not write the ring into a menu the probe has just seen.
-            // Only that way round: this read is not flushed first, as the guard's own are, so a chattering cart's byte
-            // could make a menu read as a game, and only the guard's flushed read may re-enable the stream.
-            if (mapper == kMenuMapper) link_.reportMenuState(true);
+            // Only that way round: a menu whose 'k' came later than this probe's wait reads as a game here, and only
+            // the guard's own probes may re-enable the stream.
+            if (menu == 1) link_.reportMenuState(true);
             menuProbeApplied_.store(probe, std::memory_order_release);
         }
         const unsigned generation = expVolGeneration_.load(std::memory_order_acquire);
@@ -329,7 +325,7 @@ int N8Host::probeMenu() {
         if (std::chrono::steady_clock::now() >= deadline) return link_.backChannelQuiet() ? -1 : kProbeLinkBusy;
         std::this_thread::sleep_for(std::chrono::milliseconds(1));
     }
-    return menuProbeIndex_.load(std::memory_order_relaxed);
+    return menuProbeAnswer_.load(std::memory_order_relaxed);
 }
 
 void N8Host::applyExpVol(Edio& edio, int value, int mapperHint) {
@@ -440,13 +436,13 @@ bool N8Host::submitCartJob(const rp_job_request& request, rp_job* job, const rp_
     // Where the cart is, before any pause: the transfers run at the menu only, so one a running game will refuse is
     // refused here, and the stream never pauses for it (every note-on pushed during such a pause was lost; note-offs
     // are owed, note-ons are not). The job checks again at its first step, for a cart that changed in between.
-    const int mapper = probeMenu();
-    if (mapper == kProbeLinkDown) return refuse("not connected");
-    if (mapper == kProbeLinkBusy)
-        return refuse("the reliable link never went quiet for the cart's config read: the cart kept acknowledging the "
+    const int menu = probeMenu();
+    if (menu == kProbeLinkDown) return refuse("not connected");
+    if (menu == kProbeLinkBusy)
+        return refuse("the reliable link never went quiet for the menu's probe: the cart kept acknowledging the "
                       "stream");
-    if (mapper < 0) return refuse("the cart's config block did not answer");
-    if (mapper != kMenuMapper) return refuse(dump || restore ? kBatteryAtGame : kUploadAtGame);
+    if (menu < 0) return refuse("the cart's port failed the menu's probe");
+    if (menu != 1) return refuse(dump || restore ? kBatteryAtGame : kUploadAtGame);
     if (!cartJob_.claim(reports, job, owner)) return refuse("busy: the cart is running another job");
 
     // The job's outcome, which `run` fills and `done` posts once streaming is back: both on the serial thread, one
@@ -466,12 +462,13 @@ bool N8Host::submitCartJob(const rp_job_request& request, rp_job* job, const rp_
     const auto checkStop = [stopReason] {
         if (const char* why = stopReason()) throw std::runtime_error(why);
     };
-    // The cart's state, read off its FPGA config block (a system-region read, which never touches the cart's bus and
-    // puts no byte in a running game's FIFO): the OS runs on its own mapper, 255, so that is the menu.
-    const auto atMenu = [](Edio& edio) {
-        const int mapper = runningMapper(edio);
-        if (mapper < 0) throw std::runtime_error("the cart's config block did not answer");
-        return mapper == kMenuMapper;
+    // The cart's state, asked of its menu (askMenu): the job's own port, and the one question a menu the console's
+    // reset reached answers as a menu.
+    const auto atMenu = [this](Edio& edio) {
+        const int menu = askMenu(edio);
+        link_.noteMenuProbe();  // the stream resumes after the job, on the wire this probe went onto
+        if (menu < 0) throw std::runtime_error("the cart's port failed the menu's probe");
+        return menu == 1;
     };
     using AfterJob = N8Link::AfterJob;
     N8Link::PausedJob paused;
diff --git a/packages/n8/native/src/n8/desktop/N8Host.hpp b/packages/n8/native/src/n8/desktop/N8Host.hpp
index 48c2113..0f7d873 100644
--- a/packages/n8/native/src/n8/desktop/N8Host.hpp
+++ b/packages/n8/native/src/n8/desktop/N8Host.hpp
@@ -142,16 +142,18 @@ public:
     //                              this host booted is running" then; another game of the same mapper is what it
     //                              cannot tell.
     //
-    // One job at a time ("busy: ..."), and none while the link is down ("not connected"). Every transfer first reads
-    // the cart's FPGA config block - the map_idx that tells the menu (255) from a running game - on the serial thread
+    // One job at a time ("busy: ..."), and none while the link is down ("not connected"). Every transfer first asks
+    // whether the cart's menu runs - N8Link::kMenuProbe, a SysEx whose '*t' only the menu answers, with a 'k' (the
+    // config block's map_idx cannot tell: a console reset leaves it at the game's mapper) - on the serial thread
     // between MIDI chunks, the expansion volume's path, and refuses there: a job a running game will not have pauses
-    // nothing, so the notes the block pushes meanwhile reach the cart. The read waits for a turn quiet for it
-    // (N8Link::OnControl: nothing outstanding on the reliable link, or a cart silent for a while, as one at its menu
-    // is), so an ack the cart owes for a frame it is answering now cannot land inside it (a cart silent past that
-    // bound is taken not to be answering at all, the risk the pause's drain already takes). Within a second: a link
-    // that keeps acknowledging the whole time is refused for that ("the reliable link never went quiet ..."), and a
-    // read that does not answer as such ("the cart's config block did not answer"); the job checks again at its first
-    // step, for a cart that changed in between.
+    // nothing, so the notes the block pushes meanwhile reach the cart. A game is what answers nothing within 100 ms.
+    // The probe waits for a turn quiet for it (N8Link::OnControl: nothing outstanding on the reliable link, or a cart
+    // silent for a while, as one at its menu is, and on a raw link between two messages), so an ack the cart owes for
+    // a frame it is answering now cannot land inside its read (a cart silent past that bound is taken not to be
+    // answering at all, the risk the pause's drain already takes). Within a second: a link that keeps acknowledging
+    // the whole time is refused for that ("the reliable link never went quiet ..."), and a probe the port fails as such
+    // ("the cart's port failed the menu's probe"); the job asks again at its first step, for a cart that changed in
+    // between.
     // [control], like abandonCartJobs: the node submitting is the job's owner, and a node leaving the plan abandons its
     // job, which fails at once (Removed) and stops at its next safe point.
     // The job is the host's record, finished through `reports` (rp.host.jobs/1); the request is borrowed for the call.
@@ -167,10 +169,10 @@ private:
     // link_.connect(port) unless the guard refuses it.
     void connectLink(const std::string& port);
 
-    // The cart's running map_idx, read on the serial thread that owns the port (a config-block read, between two MIDI
-    // chunks) and answered here: the map_idx, or -1 when the block did not answer, -2 when the link went down while
-    // the probe was asked, or -3 when its deadline passed with the link never quiet for the read. Each call asks
-    // afresh, since a game can boot between two jobs.
+    // Whether the cart's menu runs, asked on the serial thread that owns the port (the menu probe, between two MIDI
+    // chunks) and answered here: 1 for the menu, 0 for a game, or -1 when the port failed the probe, -2 when the link
+    // went down while the probe was asked, or -3 when its deadline passed with the link never quiet for it. Each call
+    // asks afresh, since a game can boot between two jobs.
     int probeMenu();
 
     // Write the expansion-audio master volume `value` through `edio` (see EXP_VOL_AUTO). `mapperHint` is the iNES
@@ -192,12 +194,12 @@ private:
     // re-arm.
     std::atomic<unsigned> expVolGeneration_{0};
     std::atomic<unsigned> expVolApplied_{0};  // serial thread writes, after a successful write
-    // The menu probe (probeMenu): a submission asks the serial thread for the cart's config block, and the answer lands
-    // here with the generation it answered. The serial thread stores the index, then the generation, so a reader that
-    // sees the generation sees the value.
+    // The menu probe (probeMenu): a submission asks the serial thread whether the menu runs, and the answer lands here
+    // with the generation it answered. The serial thread stores the answer, then the generation, so a reader that sees
+    // the generation sees the value.
     std::atomic<unsigned> menuProbeGeneration_{0};
     std::atomic<unsigned> menuProbeApplied_{0};
-    std::atomic<int>      menuProbeIndex_{-1};
+    std::atomic<int>      menuProbeAnswer_{-1};
     // The one capability job running on the cart, and what this host last booted there (rom.header / rom.title), which
     // an upload writes on the serial thread and those read on the control thread.
     rp::DeviceJobSlot         cartJob_;
diff --git a/packages/n8/native/src/n8/desktop/N8Link.cpp b/packages/n8/native/src/n8/desktop/N8Link.cpp
index ba3bde1..9241e70 100644
--- a/packages/n8/native/src/n8/desktop/N8Link.cpp
+++ b/packages/n8/native/src/n8/desktop/N8Link.cpp
@@ -452,6 +452,10 @@ void N8Link::writeMenuProbe(std::int64_t now) {
     check(edio_->fifoWR(kMenuProbe, sizeof kMenuProbe));
 }
 
+void N8Link::noteMenuProbe() {
+    if (!rel_) noteWireBytes(kMenuProbe, sizeof kMenuProbe, nowNs(), false);
+}
+
 bool N8Link::menuProbeAwaited(std::int64_t now) const {
     return menuProbeAtNs_ != 0 && now - menuProbeAtNs_ < kMenuAnswerNs;
 }
@@ -528,7 +532,9 @@ bool N8Link::quietForRead(std::int64_t now) const {
     // exception, see OnControl). Or a cart that has said nothing for the pause's ack bound since the oldest outstanding
     // frame first went out: one at its menu (a console reset keeps USB and the link up, and the menu acknowledges
     // nothing), or a game that has stopped answering, the same risk the pause's drain takes when it gives up waiting.
-    if (!rel_ || rel_->outstanding() == 0) return true;
+    // A raw link has no acks, but its stream may be inside a message, which a read's own probe (a SysEx) would cut.
+    if (!rel_) return betweenMessages();
+    if (rel_->outstanding() == 0) return true;
     const std::int64_t spoke = std::max(rel_->lastHeardNs(), rel_->oldestFirstSentNs());
     return spoke != rp::N8ReliableSender::kNever && now - spoke >= kAckWaitNs;
 }
diff --git a/packages/n8/native/src/n8/desktop/N8Link.hpp b/packages/n8/native/src/n8/desktop/N8Link.hpp
index 99fc21a..68c335f 100644
--- a/packages/n8/native/src/n8/desktop/N8Link.hpp
+++ b/packages/n8/native/src/n8/desktop/N8Link.hpp
@@ -38,7 +38,7 @@ public:
     // write must never fail a working MIDI link.
     using OnConnected = std::function<void(rp::n8::Edio&)>;
     // Ran on the serial thread each turn while the link is up, once the reliable link's acks are read and its re-sends
-    // written: where a setting whose value lives in an atomic (N8Host's expansion volume and its config-block probe)
+    // written: where a setting whose value lives in an atomic (N8Host's expansion volume and its menu probe)
     // reaches the device without the caller taking a lock or building a std::function. Set once before any connect,
     // like OnConnected, so it is never mutated while the thread reads it. `backChannelQuiet` is false while the cart
     // may be about to acknowledge an outstanding frame, or to answer the guard's probe: anything that reads a reply off
@@ -46,7 +46,9 @@ public:
     // reply's first bytes and the reply's tail is then parsed as link traffic. A turn is quiet with nothing outstanding
     // and no probe of the guard's awaiting its answer (100 ms at most), or once the cart has said nothing for the
     // pause's ack bound (250 ms) since the oldest outstanding frame first went out: a cart at its menu acknowledges
-    // nothing, and would otherwise never give a read a turn. What it does not cover is an ack for no outstanding frame:
+    // nothing, and would otherwise never give a read a turn. On a raw link a turn is quiet only between two messages:
+    // a read may come with a probe of its own (N8Host's menu probe writes a SysEx), which inside a message would cut
+    // it short. What it does not cover is an ack for no outstanding frame:
     // on a lossy link the ROM acknowledges each copy of a frame the host sent twice, and the second copy's ack can come
     // after the first has emptied the window. A read that meets one misreads a few bytes (an Auto that sees no
     // expansion mapper writes nothing; a probe that sees no menu refuses the job) and leaves the reply's last bytes in
@@ -135,6 +137,11 @@ public:
     // guard takes it as its state at once, so a probe made for a submission trips the guard before that job's pause
     // drains the ring into a menu.
     void          reportMenuState(bool menu);
+    // [serial thread] A probe (kMenuProbe) written outside the link's own turns - N8Host's for a submission, or a job's
+    // first step - is on the cart's wire all the same, and a game's parser loses running status at its F0: on a raw
+    // link the tracker books it as the SysEx it is, so the stream's next running-status chunk gets its status written
+    // back (see writeChunk).
+    void          noteMenuProbe();
     void          setOnControl(OnControl fn) { onControl_ = std::move(fn); }
     // [any] Run OnControl at the next quiet turn, whatever the wait a failed config write armed: the wake stands
     // through turns that are not quiet, and is spent by the first that is. A submission's menu probe is why this
@@ -143,8 +150,8 @@ public:
     void          wakeControl() { controlWanted_.store(true, std::memory_order_release); }
     // [any] Whether the serial thread's last turn was quiet for a read (see OnControl): what tells a probe that timed
     // out on a link that kept acknowledging from one whose read never answered. The last turn's answer, not the probe's
-    // whole second: a quiet turn with the probe pending always applies it, since runningMapper stores its answer before
-    // the generation is checked.
+    // whole second: a quiet turn with the probe pending always applies it, since the probe stores its answer before the
+    // generation is checked.
     bool          backChannelQuiet() const { return backChannelQuiet_.load(std::memory_order_relaxed); }
     // Hand the serial thread a job to run with streaming paused (see PausedJob). False, and nothing runs, while the
     // link is down or another job is pending. From the moment the thread takes it until it is done, isConnected()
diff --git a/packages/n8/native/test/FakeN8.hpp b/packages/n8/native/test/FakeN8.hpp
index 32c1607..257e57e 100644
--- a/packages/n8/native/test/FakeN8.hpp
+++ b/packages/n8/native/test/FakeN8.hpp
@@ -84,6 +84,10 @@ struct FakeN8 {
     std::vector<Bytes>       applied;               // messages the reliable link delivered to the game
     int                      syncs = 0;             // F4 syncs the game saw
     int                      syncAckLast = -1;      // the last header a sync's ack names (-1: the seq before its own)
+    // The running game answers the menu's '*t' with a 'k' as the menu does: the fake's combination, never a real
+    // cart's, which takes a job with a game's reliable link up. Like the menu it then acks nothing for the probe's
+    // bytes, which the job's next read would take for its reply.
+    bool                     gameAnswersProbe = false;
     std::vector<int>         frameSeqs;             // the seq of every intact frame it read
     int                      opens = 0, openNow = 0;
     int                      writes = 0;            // every write a port took a call for (one per frame, Edio.hpp)
@@ -362,6 +366,7 @@ private:
                 if (addr == kFifo) {
                     dev_.log.push_back("fifo:" + hex(data, size));
                     for (std::uint32_t i = 0; i < size; ++i) fifoByte(data[i]);
+                    if (ackOwed_ && dev_.gameAnswersProbe) ackOwed_ = false;
                     if (ackOwed_) {  // what the game says about unframed bytes, once its FIFO has run empty
                         ackOwed_ = false;
                         replyAck({0xF5, next_, next_});
@@ -458,6 +463,10 @@ private:
             menu_.clear();
             return;
         }
+        if (dev_.gameAnswersProbe) {  // a '*t' anywhere in what the game reads, the probe's SysEx included
+            if (gameStar_ && b == 't') reply({dev_.testReply});
+            gameStar_ = b == '*';
+        }
         if (!dev_.linkRom || dev_.linkDeaf) return;
         // BlipToaster's end of the link, for intact traffic: a sync sets the next seq, a frame carrying it is applied,
         // and each is acknowledged with F5 <next expected> <last header read>. Once a sync has turned the link on, a
@@ -526,6 +535,7 @@ private:
     Bytes                    file_;
     std::size_t              fileLeft_ = 0, blockLeft_ = 0;
     Bytes                    menu_;
+    bool                     gameStar_ = false;  // the game's last byte was a '*' (gameAnswersProbe)
     std::deque<PendingAck>   acks_;  // what the game owes the link, in the order it will write it
     std::chrono::steady_clock::time_point chatterDue_{};  // the next unsolicited back-channel byte
     std::string              menuPath_;
diff --git a/packages/n8/native/test/N8CartJobs.test.cpp b/packages/n8/native/test/N8CartJobs.test.cpp
index f370525..3305959 100644
--- a/packages/n8/native/test/N8CartJobs.test.cpp
+++ b/packages/n8/native/test/N8CartJobs.test.cpp
@@ -296,7 +296,7 @@ TEST_CASE("battery jobs refuse a running game before the stream pauses, so the g
     rig.dev.menu = false;  // a game runs
     rig.dev.files[kGameBattery] = saveOf(512, 3);
     rig.connect();
-    // Refused at submission, from the config block read between chunks: the stream never pauses, so a note pushed
+    // Refused at submission, from the menu probe made between chunks: the stream never pauses, so a note pushed
     // afterwards reaches the cart instead of being dropped for a job that was going to refuse anyway.
     for (const char* op : {"dump", "restore"}) {
         auto r = battery(op);
@@ -435,13 +435,12 @@ TEST_CASE("the reliable link keeps its sequence across a pause: no new sync, the
     rig.dev.menu = false;
     rig.dev.linkRom = true;  // BlipToaster, which answers the probe
     rig.dev.files[kGameBattery] = saveOf(1024, 3);
-    // The fake answers the config block as the menu does while a game runs. That is the one combination which takes a
-    // job with the reliable link up (a real cart's link belongs to a game, and a running game is refused before the
-    // pause), and it is what this case pins: the pause keeps the sequence state.
-    rig.dev.cfg[0] = 0xFF;  // map_idx 255: the menu, which the job's own first step checks again
-    rig.dev.cfg[2] = 0;
-    // This combination - a game's link with the config block saying the menu - is the fake's, and one the guard
-    // refuses: it seeds the state at connect. The case is the pause's sequence state, so the guard is off.
+    // The fake's game answers the menu's probe as the menu does. That is the one combination which takes a job with the
+    // reliable link up (a real cart's link belongs to a game, and a running game is refused before the pause), and it
+    // is what this case pins: the pause keeps the sequence state.
+    rig.dev.gameAnswersProbe = true;  // the menu's 'k', which the job's own first step asks for again
+    // This combination - a game's link with the menu's answer - is the fake's, and one the guard refuses: it seeds the
+    // state at connect. The case is the pause's sequence state, so the guard is off.
     rig.host.link().setMenuCheck({});
     rig.connect();
     REQUIRE(waitUntil([&] { return rig.host.link().reliable(); }));
@@ -476,29 +475,26 @@ TEST_CASE("the menu guard reads the cart's state between chunks, so a streamed f
         return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t).count();
     };
     // A game runs, the link is up, and the game writes a frame's ack a little after the host writes the frame, so the
-    // ack is still in the cart's queue when a submission's config read arrives: the cart's MCU puts what it owes ahead
-    // of the reply. Reading across that is what a probe must not do - the ack becomes the read's first bytes, and the
-    // reply's own tail is left in the input to be parsed as link traffic.
+    // ack is still in the cart's queue when a submission's probe arrives: the cart's MCU puts what it owes ahead of the
+    // reply. Reading across that is what a probe must not do - the ack becomes the read's, and is lost to the link.
     //
-    // The last three bytes of the config block are an ack that names a frame two ahead, so a tail parsed as link
-    // traffic would point the sequence past the frame the game is about to acknowledge.
+    // The volume's Auto reads the config block, whose last three bytes are an ack that names a frame two ahead, so a
+    // tail parsed as link traffic would point the sequence past the frame the game is about to acknowledge.
     const auto cfgTail = [](FakeN8& dev) {
         dev.cfg[13] = 0xF5;
         dev.cfg[14] = 0x7F;
         dev.cfg[15] = 0x00;
     };
 
-    SECTION("the probe waits, so the config read answers the menu") {
+    SECTION("the probe waits, so its read hears the menu's answer") {
         Rig rig;
         rig.dev.menu = false;
-        rig.dev.linkRom = true;  // BlipToaster, which answers the probe
-        rig.dev.cfg[0] = 0xFF;   // the config block says the menu, so the job is taken (the fake's combination)
-        rig.dev.cfg[2] = 0;
-        cfgTail(rig.dev);
+        rig.dev.linkRom = true;           // BlipToaster, which answers the probe
+        rig.dev.gameAnswersProbe = true;  // and the menu's probe too, so the job is taken (the fake's combination)
         rig.dev.linkAckDelayMs = 60;
         rig.dev.files[kGameBattery] = saveOf(1024, 3);
-        // The fake's inconsistent combination - a game's link with the config block saying the menu - is one the guard
-        // refuses, and it seeds that state at connect: off, for this section, which is the probe's own wait.
+        // The fake's inconsistent combination - a game's link with the menu's answer - is one the guard refuses, and it
+        // seeds that state at connect: off, for this section, which is the probe's own wait.
         rig.host.link().setMenuCheck({});
         rig.connect();
         REQUIRE(waitUntil([&] { return rig.host.link().reliable(); }));
@@ -524,7 +520,6 @@ TEST_CASE("the menu guard reads the cart's state between chunks, so a streamed f
     SECTION("a frame pushed at the menu goes nowhere, and the job's probe still reads it") {
         Rig rig;
         rig.dev.linkRom = true;  // a game's link, stale after the console went back to its menu
-        cfgTail(rig.dev);
         rig.dev.files[kGameBattery] = saveOf(1024, 3);
         rig.connect();  // at the menu: the guard's seed hears its answer before anything may stream
         // A message pushed now would be read as one of the menu's commands, so it is dropped rather than framed, and
@@ -792,10 +787,9 @@ TEST_CASE("the pause waits a bounded time for acks a silent cart never sends, an
         Rig rig;
         rig.dev.menu = false;
         rig.dev.linkRom = true;
-        // The config block says the menu shows, so the job is taken and the pause's ack wait runs (see the sequence
-        // case for why that combination is the fake's, not a real cart's).
-        rig.dev.cfg[0] = 0xFF;
-        rig.dev.cfg[2] = 0;
+        // The game answers the menu's probe, so the job is taken and the pause's ack wait runs (see the sequence case
+        // for why that combination is the fake's, not a real cart's).
+        rig.dev.gameAnswersProbe = true;
         // The fake's combination is one the guard refuses, and it seeds its state at connect: off, for this case.
         rig.host.link().setMenuCheck({});
         rig.connect();
@@ -821,8 +815,7 @@ TEST_CASE("the pause waits a bounded time for acks a silent cart never sends, an
         Rig rig;
         rig.dev.menu = false;
         rig.dev.linkRom = true;
-        rig.dev.cfg[0] = 0xFF;
-        rig.dev.cfg[2] = 0;
+        rig.dev.gameAnswersProbe = true;
         // The fake's combination is one the guard refuses, and it seeds its state at connect: off, for this case.
         rig.host.link().setMenuCheck({});
         rig.connect();
@@ -888,7 +881,7 @@ TEST_CASE("a cart reset to its menu with a frame its game never acknowledged sti
     CHECK_FALSE(rig.cart.connected());  // the menu, where streaming stays off
 }
 
-TEST_CASE("a probe on a link that never goes quiet is refused for that, not as a config block that did not answer",
+TEST_CASE("a probe on a link that never goes quiet is refused for that, not as a probe the port failed",
           "[n8][cart-jobs]") {
     using Clock = std::chrono::steady_clock;
     Rig rig;
@@ -899,8 +892,8 @@ TEST_CASE("a probe on a link that never goes quiet is refused for that, not as a
     rig.dev.linkAckDelayMs = 30;  // a game that answers, but not instantly
     rig.connect();
     REQUIRE(waitUntil([&] { return rig.host.link().reliable(); }));
-    // A stream the game keeps acknowledging: every turn has an ack in the last ack bound, so no turn is quiet for a
-    // config read, and the cart is answering as a game (its window moves), so the guard has nothing to look for either.
+    // A stream the game keeps acknowledging: every turn has an ack in the last ack bound, so no turn is quiet for the
+    // probe's read, and the cart is answering as a game (its window moves), so the guard has nothing to ask either.
     std::atomic<bool> streaming{true};
     std::thread feeder([&] {
         while (streaming.load(std::memory_order_relaxed)) {
@@ -915,8 +908,7 @@ TEST_CASE("a probe on a link that never goes quiet is refused for that, not as a
     CHECK_FALSE(rig.submit(battery("dump"), job, &why));
     streaming.store(false, std::memory_order_relaxed);
     feeder.join();
-    CHECK(why == "the reliable link never went quiet for the cart's config read: the cart kept acknowledging the "
-                 "stream");
+    CHECK(why == "the reliable link never went quiet for the menu's probe: the cart kept acknowledging the stream");
     CHECK(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count() >= 900);  // its deadline
     CHECK(rig.cart.connected());  // nothing paused
     CHECK(rig.dev.count("file-") == 0);
@@ -944,7 +936,7 @@ TEST_CASE("a submission's wake outlasts a turn the link is busy on, so a failing
     REQUIRE(waitUntil([&] { return rig.host.link().linkOutstanding() == 1; }));
     // The frame's ack is 60 ms out, so the turn the submission wakes is a busy one and the probe waits. The wake must
     // still stand when the link goes quiet: spent on the busy turn, whose write re-arms the wait, it would leave the
-    // probe a second behind it and refused at its deadline ("the cart's config block did not answer").
+    // probe a second behind it and refused at its deadline ("the cart's port failed the menu's probe").
     Job job;
     std::string why;
     const auto t0 = Clock::now();
@@ -1623,7 +1615,7 @@ TEST_CASE("a cart at its menu is not streamed to: its FIFO is a command parser t
 TEST_CASE("a job submitted while the ring holds a push, right after the console resets to its menu, writes nothing "
           "into the menu",
           "[n8][menu-guard]") {
-    // The job's own menu probe reads the config block, and that read is the guard's chance to see the reset: it must
+    // The job's own menu probe hears the menu's answer, and that is a chance for the guard to see the reset: it must
     // trip before the pause delivers what the ring still holds, or the menu reads a note-on as its own commands.
     Rig rig;
     rig.connectGame();
diff --git a/packages/n8/native/test/N8CartLink.test.cpp b/packages/n8/native/test/N8CartLink.test.cpp
index de82205..37cc39a 100644
--- a/packages/n8/native/test/N8CartLink.test.cpp
+++ b/packages/n8/native/test/N8CartLink.test.cpp
@@ -346,7 +346,7 @@ TEST_CASE("N8CartLink: a submission's menu probe takes a turn of its own, not th
     link.setExpansionVolume(64);
     REQUIRE(host.link().connect("fake"));
     // The connect-time write dies and the retry interval is a second. A probe that waited behind it would be refused
-    // "the cart's config block did not answer" after its own 1 s deadline, on a cart whose block answers.
+    // "the cart's port failed the menu's probe" after its own 1 s deadline, on a cart whose port answers.
     std::this_thread::sleep_for(std::chrono::milliseconds(50));
     rp::n8::test::JobRequest r;
     r.nodeId     = "cart";
diff --git a/packages/native/test/engine-graph/N8Adapters.test.cpp b/packages/native/test/engine-graph/N8Adapters.test.cpp
index f4b27ce..d1de8c2 100644
--- a/packages/native/test/engine-graph/N8Adapters.test.cpp
+++ b/packages/native/test/engine-graph/N8Adapters.test.cpp
@@ -189,11 +189,11 @@ TEST_CASE("the cart's production adapters behind a real Engine: a note, then the
             CHECK(title.id == 0);
             CHECK(title.error == "nothing this host booted is running");
 
-            // A dump while a game runs is refused at submission, from the config block read between chunks, and the
+            // A dump while a game runs is refused at submission, from the menu probe made between chunks, and the
             // stream never paused for it (the note-off the block carries reaches the cart).
             {
                 std::lock_guard<std::mutex> l(dev.mu);
-                dev.menu = false;  // the config block names mapper 0, a game
+                dev.menu = false;  // a game: nothing answers the menu's probe
             }
             const auto atGame = e.submitJob(request(RP_SYSTEM_SRAM_INTERFACE, "dump", "game.nes"));
             CHECK(atGame.id == 0);
diff --git a/spec/07-remaining-work.md b/spec/07-remaining-work.md
index 8c642ee..c2ab271 100644
--- a/spec/07-remaining-work.md
+++ b/spec/07-remaining-work.md
@@ -10,7 +10,7 @@ than work.
 
 **Before merge**
 - [ ] [The N8 menu's back-channel](#the-n8-menus-back-channel)
-- [ ] [The menu guard misses a console reset](#the-menu-guard-misses-a-console-reset)
+- [ ] [The N8 menu's residual window](#the-n8-menus-residual-window)
 - [ ] [The Launchpad real-device checklist](#the-launchpad-real-device-checklist)
 - [ ] [The Launchpad's Windows items on a real device](#the-launchpads-windows-items-on-a-real-device)
 - [ ] [The overlay strip placement on a real host](#the-overlay-strip-placement-on-a-real-host)
@@ -111,18 +111,15 @@ the back-channel stays silent, and that a job submitted right after a power-cycl
 and that the guard's periodic config reads cost a running BlipToaster nothing (`retroplug-n8-hwtest timing`, the
 heavy-chord stress) with no stall.
 
-### The menu guard misses a console reset
-
-On a real cart (2026-11-18, `retroplug-n8-hwtest menuguard`, BlipToaster streaming framed), a console RESET mid-stream
-was not caught. The cart stopped acknowledging at once, but the config block kept the game's mapper: map_idx read 69
-(`45 65 02 80 01 ...`) with the N8 OS running, where the power-on menu reads 255 (`FF 00 00 00 ...`). The guard
-therefore read "a game" every time and never tripped; about 35 KB of frames and 202 re-sends reached the menu in 21 s,
-and the OS stopped on "ERROR:40 unexpected io status". So map_idx 255 tells only a cold-booted menu from a game, and
-FakeN8 models every menu that way. The guard needs a signal that holds after a reset (one the OS answers and a game
-cannot, or treating a framed window that stops moving as "not a game" until a probe is answered again), proven with
-`menuguard` and a reset by hand; the raw path, which has no acks, has no reset signal at all. Once it trips on a reset,
-the windows spec/13 states (up to 250 ms raw, the frames written before the hold, the trusted second) are what is left
-to measure. ([spec/13](13-subsystems.md#the-everdrive-n8-bridge).)
+### The N8 menu's residual window
+
+What the link writes before the menu's `k` is heard reaches the menu: up to 250 ms of raw MIDI; framed, the frames
+written before a stall shows (100 ms), and up to a second more when a stall's probe has just heard an ack (the trusted
+second: a reset inside it streams frames and re-sends until it ends). A `*` in it followed by a command or by a byte the
+menu rejects (ERROR:40) does harm ([spec/13](13-subsystems.md#the-everdrive-n8-bridge)). Measure it on the cart with
+`retroplug-n8-hwtest menuguard` and a RESET press, framed BlipToaster and then BlipToaster held `raw` (a trip, and 0
+stream bytes after it), and time a reset menu's answer there too (`menuprobe test` after a RESET: a power-on menu
+answers in about 20 ms).
 
 ### The Launchpad real-device checklist
 
diff --git a/spec/13-subsystems.md b/spec/13-subsystems.md
index 6008933..80397db 100644
--- a/spec/13-subsystems.md
+++ b/spec/13-subsystems.md
@@ -126,8 +126,8 @@ Everdrive N8 Pro's cart FIFO, so the console itself plays. One-way, host to cart
 - **A cart at its menu is not streamed to.** The menu takes commands over the same FIFO a game reads MIDI from, so a
   data pair inside a message (`0x2A 0x73` is `*s`: start an app) can reach it. `N8Link` keeps the cart's at-menu state,
   read from the FPGA config block's map_idx (255 at a power-on menu only: after a console reset the block keeps the
-  game's mapper, so a reset menu reads as a game and is streamed to, see
-  [spec/07](07-remaining-work.md#the-menu-guard-misses-a-console-reset); a system-region read that touches neither the
+  game's mapper, which is why the guard asks the menu instead, see
+  [the design](../docs/n8-menu-guard-reset-design.md); a system-region read that touches neither the
   cart's bus nor a game's FIFO) - seeded at connect, and re-read about every 250 ms on a turn quiet for a read, with a
   100 ms timeout, and, on the first quiet turn after it stops moving (or when the hold's second trips), at once (the
   oldest frame unacknowledged past the link's own 100 ms timeout, which is the console reset), whose new frames and
````

- [ ] **Step 4: run them, and see them pass**

```
timeout -k 10 1800 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh cmake --build /workspaces/RetroPlug-main/cuttlefish/build --target retroplug-n8-test -j$(nproc)
timeout -k 10 300 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh /workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-n8-test "[menu-guard]"
timeout -k 10 300 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh /workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-n8-test
```

Expected: PASS, the menu-guard cases and then the whole binary, nothing left tagged:

```
All tests passed (142 assertions in 16 test cases)
All tests passed (1816 assertions in 133 test cases)
```

- [ ] **Step 5: the wider suites this task reaches, and the links**

The same as task 2's Step 9 (the n8 binary three times, the TSan build among them, and the hardware test compiled,
never run), and the doc links, since spec/07 and spec/13 change here:

```
timeout -k 10 1800 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh cmake --build /workspaces/RetroPlug-main/cuttlefish/build --target retroplug-n8-test retroplug-engine-graph-test retroplug-device-mode-test retroplug-host retroplug-n8-hwtest -j$(nproc)
timeout -k 10 300 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh /workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-n8-test
timeout -k 10 300 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh /workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-n8-test
timeout -k 10 300 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh /workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-n8-test
timeout -k 10 1800 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh /workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-engine-graph-test
timeout -k 10 600 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh /workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-device-mode-test
timeout -k 10 1800 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh pnpm -C /workspaces/RetroPlug-main/cuttlefish test:n8
timeout -k 10 1800 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh pnpm -C /workspaces/RetroPlug-main/cuttlefish test:native n8
timeout -k 10 1800 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh cmake --build /workspaces/RetroPlug-main/cuttlefish/build-tsan --target retroplug-n8-test -j$(nproc)
TSAN_OPTIONS="suppressions=/workspaces/RetroPlug-main/cuttlefish/packages/native/test/sanitizer/tsan.supp halt_on_error=1" timeout -k 10 1800 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh /workspaces/RetroPlug-main/cuttlefish/build-tsan/bin/retroplug-n8-test
timeout -k 10 600 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh pnpm -C /workspaces/RetroPlug-main/cuttlefish check:docs
```

Expected: exit 0 for each; `[100%] Built target retroplug-n8-hwtest`, and:

```
All tests passed (1816 assertions in 133 test cases)
All tests passed (1816 assertions in 133 test cases)
All tests passed (1816 assertions in 133 test cases)
test cases:     342 |     331 passed | 11 skipped
assertions: 1702488 | 1702488 passed |  0 skipped
All tests passed (1504408 assertions in 96 test cases)
All tests passed (17 assertions in 2 test cases)
All tests passed (1816 assertions in 133 test cases)
# 2 plugin test binary(ies) passed (jobs=16)
# 3 native test file(s) passed (jobs=16)
All tests passed (1816 assertions in 133 test cases)
```

In the same order as task 2's, the TSan run last, with no ThreadSanitizer report; then the link check, clean but for
the warning the tree already carries:

```
WARN  docs/launchpad-plan.md:299  dead link in a plan record  ../packages/retroplug/ui/screens/menu/midiDevices.ts

check-doc-links: 117 file(s) clean, 1 warning(s)
```

- [ ] **Step 6: commit**

```
git -C /workspaces/RetroPlug-main/cuttlefish add packages/n8/native/src/n8/desktop/N8Host.cpp \
  packages/n8/native/src/n8/desktop/N8Host.hpp packages/n8/native/src/n8/desktop/N8Link.hpp \
  packages/n8/native/src/n8/desktop/N8Link.cpp packages/n8/native/test/FakeN8.hpp \
  packages/n8/native/test/N8CartJobs.test.cpp packages/n8/native/test/N8CartLink.test.cpp \
  packages/native/test/engine-graph/N8Adapters.test.cpp spec/07-remaining-work.md spec/13-subsystems.md
git -C /workspaces/RetroPlug-main/cuttlefish commit -F - <<'MSG'
fix(n8): a cart job's menu check asks the menu, so a reset menu takes a job

N8Host::probeMenu and a job's first step read map_idx, which a console reset
leaves at the game's mapper, so a job submitted at a menu the reset reached was
refused as a running game. Both now write the menu's probe and wait 100 ms for
its 'k', on a quiet turn as before, and the probe still only ever trips the
guard. Since the probe is bytes on the stream, a raw link is quiet for a read
only between two messages, and the link books the probe on its running-status
tracker, so a game's running status the probe ended is written back. FakeN8's
game can answer the probe, the fake's way to take a job with a game's reliable
link up. spec/07's "The menu guard misses a console reset" goes, for the one
hardware line left: the residual window, the trusted second among it.
MSG
```

---

## Task 4: the documents

No test fails for a document, so this task is one step, the link check and the commit. spec/13's guard paragraph says
how the guard asks (the probe, the answer and the ack scan, the raw cadence and the running status it relies on, the
framed stall, its hold and BlipToaster's ack for the probe, the two silent stalls, the menu's own cadence and its three
misses, the seed and its 100 ms, the quiet rule and the job's wait, the job probe and its 100 ms at a game, the menu's
measured answer time) and what is left exposed (bytes before the `'k'`: up to 250 ms raw, the frames before a stall
shows framed, and the trusted second after a stall's probe heard an ack; only a `'*'` and a command or an ERROR:40 byte
harms). spec/07's "The N8 menu's back-channel" entry trades the config reads' checks for the probe's (a game writing
`0x6B`, the menu browsed while probed, `timing busy --raw --guard` for the probe's cost). spec/02's job paragraph stops
saying the cart's config block is read at submission. Batch 4's design gets the one-line pointer in its section 5, and
the hardware test's `menuprobe` comment points at the design rather than the spec/07 entry that went in task 3. The
design's as-built and execution sections are the controller's.

**Files** (line numbers are the file at `6a4b3fb72`):

- Modify: `spec/13-subsystems.md:126-158` (the N8 bridge's menu-guard bullet and its sub-bullet).
- Modify: `spec/07-remaining-work.md:98-112` (the back-channel entry).
- Modify: `spec/02-native-host.md:494-498` (the job check).
- Modify: `docs/known-bugs-batch-4-design.md:176-177` (section 5's pointer, after its last paragraph).
- Modify: `packages/n8/native/test/n8-sd-hwtest.cpp:449` (a comment).

### Steps

- [ ] **Step 1: write the documents**

````patch
diff --git a/docs/known-bugs-batch-4-design.md b/docs/known-bugs-batch-4-design.md
index 886ed8d..a52efc6 100644
--- a/docs/known-bugs-batch-4-design.md
+++ b/docs/known-bugs-batch-4-design.md
@@ -176,6 +176,9 @@ Where the cart runs is readable without touching its bus: the FPGA config block'
 **What stays in spec/07:** a single line under hardware: confirm on a cart that the menu writes nothing unprompted on
 the back-channel, which the quiet rule and the guard's read both assume.
 
+**Superseded:** map_idx tells only a power-on menu from a game; the guard asks the menu instead
+([n8-menu-guard-reset-design.md](n8-menu-guard-reset-design.md)).
+
 ## 6. Plan, execution and acceptance
 
 **Tasks**, each test-first, in this order:
diff --git a/packages/n8/native/test/n8-sd-hwtest.cpp b/packages/n8/native/test/n8-sd-hwtest.cpp
index 616cdc0..e6c82da 100644
--- a/packages/n8/native/test/n8-sd-hwtest.cpp
+++ b/packages/n8/native/test/n8-sd-hwtest.cpp
@@ -457,7 +457,7 @@ int main(int argc, char** argv) {
         return tripMs >= 0 && after == 0 ? 0 : 1;
     }
 
-    // What the N8 OS menu does with bytes it did not ask for (spec/07 "The menu guard misses a console reset"): 'test'
+    // What the N8 OS menu does with bytes it did not ask for (docs/n8-menu-guard-reset-design.md section 1): 'test'
     // sends '*t' and reports the answer ('k' is the menu); 'sysex' sends '*t' wrapped as F0 2A 74 F7, a SysEx a MIDI
     // game ignores, then reports its answer and a plain '*t' after it; 'note' sends one note-on (90 3C 40), a stray
     // byte run of the residual window's kind, then a plain '*t'. Run at a menu; check the screen after each.
diff --git a/spec/02-native-host.md b/spec/02-native-host.md
index 193d83e..d098944 100644
--- a/spec/02-native-host.md
+++ b/spec/02-native-host.md
@@ -491,11 +491,12 @@ node's job goes to its link, on the device's own thread: one at a time ("busy"),
 request and the report are C ([rp_jobs.h](../packages/node-sdk/native/src/graph/rp_jobs.h)): a request's strings are C
 strings (a device job whose strings hold a NUL byte is refused, as a C string would cut it), and a link reports
 progress, success, a bounded UTF-8 failure reason or an abandon through the engine's `rp.host.jobs/1` over an opaque
-`rp_job`. The cart reads its FPGA config block at submission (on that thread, between MIDI chunks, as the expansion
-volume reaches it, and only on a turn quiet for the read: nothing outstanding on the reliable link, or a cart silent
-for 250 ms since its oldest outstanding frame first went out, as one at its menu is), so a transfer a running game
-will refuse is refused without pausing the stream and dropping the block's notes; the submitting thread waits for that
-read, up to its 1 s bound. The cart node keeps books on the notes the cart holds: it re-sends at frame 0 the note-offs
+`rp_job`. The cart is asked at submission whether its menu runs (the menu's probe, on that thread, between MIDI
+chunks, as the expansion volume reaches it, and only on a turn quiet for its read: nothing outstanding on the reliable
+link, or a cart silent for 250 ms since its oldest outstanding frame first went out, as one at its menu is), so a
+transfer a running game will refuse is refused without pausing the stream and dropping the block's notes; the
+submitting thread waits for that answer, up to its 1 s bound. The cart node keeps books on the notes the cart holds:
+it re-sends at frame 0 the note-offs
 a pause dropped, and its destroy hands what is still sounding to the link (`CartLink::releaseNotes`), with the push
 count it recorded after its own last push (`pushMark`): the serial thread writes the note-offs behind THAT mark, and
 skips a key whose last note-on went out at or past it, so the node taking over keeps a key it struck while the old one
diff --git a/spec/07-remaining-work.md b/spec/07-remaining-work.md
index c2ab271..c636a0f 100644
--- a/spec/07-remaining-work.md
+++ b/spec/07-remaining-work.md
@@ -97,19 +97,16 @@ What `feature/modularize` owes before it merges to main.
 
 ### The N8 menu's back-channel
 
-The menu guard reads the cart's FPGA config block to tell a menu (map_idx 255) from a game, and drops the stream there
-([spec/13](13-subsystems.md#the-everdrive-n8-bridge)). The quiet rule that decides when that read may be made assumes
-the menu writes nothing unprompted on the back-channel: unsolicited bytes keep the quiet rule's `lastHeardNs` fresh, so
-a menu with stale frames reads as one that never goes quiet, and the guard only looks when the hold's one-second bound
-trips it. That bound now always ends the guard's own wait, so its read costs a second's delay and may be garbled by the
-chatter. A submission's probe is a different read: unflushed, and gated on a quiet turn of its own, so a chattering
-cart can still refuse it as busy, or garble the block into a game - which is what the check below is for. A cadence read
-is a third: the guard reads the config block about every 250 ms between chunks on every raw or idle-framed stream to a
-running game ([`N8Link.cpp`](../packages/n8/native/src/n8/desktop/N8Link.cpp)), a read no performance ran before this
-batch and one whose cost to a busy cart is unmeasured. Confirm on a cart at its menu, with nothing streamed to it, that
-the back-channel stays silent, and that a job submitted right after a power-cycle to the menu is accepted, and promptly;
-and that the guard's periodic config reads cost a running BlipToaster nothing (`retroplug-n8-hwtest timing`, the
-heavy-chord stress) with no stall.
+The menu guard tells a menu from a game by the menu's answer to its probe, a `k` on the back-channel outside an ack
+([spec/13](13-subsystems.md#the-everdrive-n8-bridge)), so a menu that wrote a `0x6B` unprompted would trip it with no
+reset, a game that writes one outside an ack would trip it too (and, writing it often, hold it at the menu), and a game
+that answered the probe would read as a menu. On the lab's cart the menu answered every probe, in about 20 ms, and a
+running BlipToaster ignored three ([the design](../docs/n8-menu-guard-reset-design.md) section 1). Confirm on a cart at
+its menu, with nothing streamed to it, that the back-channel stays silent between probes; that browsing the menu with
+the pad for a minute while the guard probes it shows nothing odd and no ERROR; that a job submitted right after a
+power-cycle or a RESET to the menu is accepted, and promptly; and that the probe a raw stream carries four times a
+second costs a running BlipToaster nothing (`retroplug-n8-hwtest timing busy --raw --guard` against the same run without
+`--guard`) and stalls nothing.
 
 ### The N8 menu's residual window
 
diff --git a/spec/13-subsystems.md b/spec/13-subsystems.md
index 80397db..9d22aea 100644
--- a/spec/13-subsystems.md
+++ b/spec/13-subsystems.md
@@ -125,37 +125,49 @@ Everdrive N8 Pro's cart FIFO, so the console itself plays. One-way, host to cart
   sniffer or FIFO reads.
 - **A cart at its menu is not streamed to.** The menu takes commands over the same FIFO a game reads MIDI from, so a
   data pair inside a message (`0x2A 0x73` is `*s`: start an app) can reach it. `N8Link` keeps the cart's at-menu state,
-  read from the FPGA config block's map_idx (255 at a power-on menu only: after a console reset the block keeps the
-  game's mapper, which is why the guard asks the menu instead, see
-  [the design](../docs/n8-menu-guard-reset-design.md); a system-region read that touches neither the
-  cart's bus nor a game's FIFO) - seeded at connect, and re-read about every 250 ms on a turn quiet for a read, with a
-  100 ms timeout, and, on the first quiet turn after it stops moving (or when the hold's second trips), at once (the
-  oldest frame unacknowledged past the link's own 100 ms timeout, which is the console reset), whose new frames and
-  re-sends wait for that read and at most a second. Every guard read flushes the input first, so a chattering cart's
-  bytes cannot garble the block; a job's menu probe reads unflushed, so it may trip the guard but never clear it. While
-  it says menu, `push` answers false and counts, the ring, the reliable window and any pending note release are
-  discarded, and nothing is written; leaving the menu re-runs the link probe (which flushes the input before its sync,
-  as does the connect-time one: a menu read that timed out leaves the tail of its reply in the input, and a config
-  block's bytes could otherwise be read as the link's ack, which would frame the stream for a ROM that reads no frames).
-  Jobs still run there (the SD card is reachable only at the menu), and a cart node's `connected` reads 0: what it
-  pushes there is refused, and a note-off it loses stays owed until a game reads connected again (a note-off to a game
-  that never sounded that note is harmless). Settings > N8's status row says "At the N8 menu (not streaming)".
-  - **The residual window, in its bounds.** The guard knows the cart's state only when a read lands, so bytes that go
-    out before it are not held back. The bounds in full: raw, up to 250 ms of streamed MIDI, one read cadence; framed,
-    up to 100 ms of frames and re-sends, the link's own timeout, which is when the guard's read becomes due; and up to a
-    second more while a read has just answered "a game" (the trusted-game window), a reset inside which streams until it
-    expires. A message inside any of the three reaches the menu as its own bytes, so a GM hi-hat (`9n 2A 73`) is `*s`
-    there, raw or framed, and starts the app. The framed reset case therefore cannot be made airtight from this side -
-    the first frames after a reset are written before anything can know a menu came back - and a config read per chunk,
-    at a config read's 100 ms bound between messages, is the only full fix
-    ([spec/07](07-remaining-work.md#the-n8-menus-residual-window)). The overdue hold holds the stream while the guard
-    looks, and its trigger is not a lost frame as such: it is the OLDEST outstanding frame unacknowledged 100 ms after
-    its first send. That covers a frame lost twice in a row, and equally a cart whose FIFO does not run empty for 100 ms
-    under a dense stream and so acks only then. Everything behind it waits for a quiet turn (250 ms of silence) or the
-    hold's one-second bound: the re-send, which used to go out at once, and the new chunks too.
-    That is deliberate, since a re-send decided while the window has stopped moving is what a menu would read, so a slow
-    recovery on a lossy NTSC link is that trade and not a new fault. The hardware check the quiet rule rests on is
-    [spec/07](07-remaining-work.md#the-n8-menus-back-channel).
+  and asks the menu for it ([the design](../docs/n8-menu-guard-reset-design.md)): the probe is `F0 2A 74 F7`
+  (`N8Link::kMenuProbe`), a SysEx a MIDI game ignores and whose `*t` the menu reads (it skips every byte that is not a
+  `*`), and the answer is the menu's `k` on the back-channel outside an ack (an ack is `F5 <next> <last>`, both 7-bit,
+  so a `0x6B` inside one is not it). The FPGA config block's map_idx cannot tell: it reads 255 at a menu reached by
+  power-on only, and a console reset leaves it at the game's mapper. The state is seeded at connect (the probe, with 100
+  ms for the answer, so a connect to a game takes 100 ms longer; the lab's menu answers in about 20 ms, 27 ms the worst
+  of 8), and the stream asks again by writing the probe where its own bytes are between two messages (a message left
+  open holds the probe back however long it stays open, and a frame never leaves one open): on a raw link every 250 ms,
+  with no read and no wait (the answer is heard on a later turn's back-channel read), which relies on a game's MIDI
+  parser ending running status at the `F0`, as MIDI's rule is (one that skipped the `F0` would hear note 42 at velocity
+  116 four times a second); on a framed link only once the window has STOPPED MOVING (the oldest frame unacknowledged
+  past the link's own 100 ms timeout, which is the console reset), holding new frames and re-sends until a `k` (the
+  menu) or an ack (a game whose acks are lost or damaged, which streams on and is not asked again for a second: a framed
+  BlipToaster drops the probe's bytes as unframed and acknowledges them `F5 <next> <next>`, which is what resumes a
+  stall for a game), asking again each second that hears neither, and after two such seconds giving up the window and
+  what the ring held (counted) and probing the link again (a game without the link, started from the cart's browser
+  after a reset the guard never saw, is then streamed raw); and at the menu every 250 ms whatever the link, leaving it
+  when three probes in a row go unanswered for 100 ms each (a game booted from the cart's own browser; one miss is only
+  a slow turn of the menu's), which re-runs the link probe. A `k` trips the guard whenever it is heard. A turn whose
+  probe still awaits its answer, or that has read half an ack, is not quiet for a read, and a job waits the probe out,
+  so a `k` that comes within the probe's 100 ms never lands inside another reply. A job's menu probe asks the same
+  question on a quiet turn (on a raw link, between two messages), and the stream's tracker books it as it books its own,
+  so a running status it ended is written back; it may trip the guard but never clears it, and at a running game, which
+  the submission is refused for, it holds the serial thread, and so the stream, for its 100 ms wait. While it says menu,
+  `push` answers false and counts, the ring, the reliable window and any pending note release are discarded, and nothing
+  but the probe is written; leaving the menu re-runs the link probe, which flushes the input before its sync, as does
+  the connect-time one, so only the sync's own ack turns the framed link on. Jobs still run there (the SD card is
+  reachable only at the menu), and a cart node's `connected` reads 0: what it pushes there is refused, and a note-off it
+  loses stays owed until a game reads connected again (a note-off to a game that never sounded that note is harmless).
+  Settings > N8's status row says "At the N8 menu (not streaming)".
+  - **What is left exposed.** The guard knows the cart is at its menu only once the `k` is heard, so bytes written
+    before it reach the menu: raw, up to one probe cadence (250 ms) of MIDI; framed, the frames written before the stall
+    shows (the link's 100 ms timeout), and up to a second more after a stall's probe has heard an ack (the trusted
+    second: a reset inside it streams frames and re-sends until it ends). Only a `*` followed by a byte the menu takes
+    as a command (a GM hi-hat, `9n 2A 73`, is `*s` and starts the app) or rejects (ERROR:40, which stops the OS) does
+    harm; a stream without `0x2A` is harmless. A probe before every chunk is the only full fix
+    ([spec/07](07-remaining-work.md#the-n8-menus-residual-window)). The framed hold's trigger is not a lost frame as
+    such: it is the OLDEST outstanding frame unacknowledged 100 ms after its first send. That covers a frame lost twice
+    in a row, and equally a cart whose FIFO does not run empty for 100 ms under a dense stream and so acks only then.
+    Everything behind it waits for the menu's answer or an ack: the re-send, which used to go out at once, and the new
+    chunks too. That is deliberate, since a re-send decided while the window has stopped moving is what a menu would
+    read, so a slow recovery on a lossy NTSC link is that trade and not a new fault. The hardware check the answer rests
+    on is [spec/07](07-remaining-work.md#the-n8-menus-back-channel).
 - **Prove a change:** `pnpm test:n8` (protocol, no hardware; `packages/n8/native/test`), the `NesN8FifoTiming` and
   `N8Host` Catch2 cases, `pnpm test n8` and `pnpm test:native n8-session` and `pnpm test:native n8-sdk` (`connectN8`
   over the fake N8); for the box, `pnpm device:box`, then the rig's `loop.sh`, `stress.sh` and `faults.sh` by hand, each
````

- [ ] **Step 2: check the links**

```
timeout -k 10 600 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh pnpm -C /workspaces/RetroPlug-main/cuttlefish check:docs
```

Expected: clean, with the one warning the tree already carries (`docs/launchpad-plan.md:299`, a plan record's dead
link):

```
WARN  docs/launchpad-plan.md:299  dead link in a plan record  ../packages/retroplug/ui/screens/menu/midiDevices.ts

check-doc-links: 117 file(s) clean, 1 warning(s)
```

- [ ] **Step 3: commit**

```
git -C /workspaces/RetroPlug-main/cuttlefish add spec/13-subsystems.md spec/07-remaining-work.md \
  spec/02-native-host.md docs/known-bugs-batch-4-design.md packages/n8/native/test/n8-sd-hwtest.cpp
git -C /workspaces/RetroPlug-main/cuttlefish commit -F - <<'MSG'
docs(n8): the menu guard asks the menu, and what that leaves exposed

spec/13 says how the guard asks the N8 menu where the cart runs (the probe, the
'k' outside an ack, the raw cadence and the running status it relies on, the
framed stall's hold and BlipToaster's ack for the probe, the two silent stalls,
the menu's cadence and its three misses, the seed and the job probe, and their
100 ms) and what is left exposed: the bytes written before the 'k', up to 250 ms
raw or the frames before a stall shows, and up to a second more after a stall's
probe heard an ack (the trusted second). spec/07's back-channel entry checks the
probe instead of the config reads it replaced, spec/02's job paragraph says the
cart is asked rather than read, and batch 4's design points at the new signal.
MSG
```
