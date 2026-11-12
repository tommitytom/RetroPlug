# sdk-n8 port Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port the local branch `feat/sdk-n8` onto `feature/modularize`, as [sdk-n8-port-design.md](sdk-n8-port-design.md)
decides it: the N8 lists a directory in pages of 32 records, the CLI's N8 tools share one `pickN8Port` and refuse a
`romName` that is a path, an N8 session resyncs after a call that failed on the line, the n8 pack exports the device a
session script opens (`@retroplug/n8/device`), the N8 bridges survive a failed FIFO write, the CLI SDK exports
`connectN8` and `keepAlive`, the TAP harness records an error nothing caught instead of dying on it or waiting on it,
and the CLI launcher ends a session that threw with exit 1 and its text. The last task is the docs.

**Architecture:** The N8 half is the n8 pack's (`packages/n8`): the one Edio's `listDir` pages its `CMD_F_DIR_GET`s
(and `FakeN8` answers the range asked), the RPC facet's `N8Rpc::Session` carries a `dirty` flag that the next call
clears by draining the line and re-running the handshake (`N8Rpc::ready` / `N8Rpc::settle`), and the TS half gains
`pickN8Port`, a `romName` guard in the client's `load()`, the bridge's `failureWatch` and a new module `ts/device.ts`
(`openN8Device` over `createN8Client`, exported as `./device`). The shell's SDK (`packages/retroplug/cli/sdk.ts`)
wraps `openN8Device` over its own RPC call as `connectN8`, re-exports the session's `keepAlive`, declares both in
`cli/sdk-types.d.ts` and holds the declaration to the implementation by type identity in `cli/sdkSurfaceCheck.ts`;
`check-boundaries.mjs` gains the one `SHELL_IMPORTS` entry that lets the shell import `@retroplug/n8/device`. The
harness (`packages/testing/ts/harness.ts`) installs an `unhandledrejection` listener and wrapped timers on its first
`test()`, and races each case against an abort its records trigger. The launcher (`packages/native/cli/main.cpp`)
checks for a pending exception and for txiki's stop request after every pump, in the bounded loop and the keep-alive
loop. The ordering is binding: the launcher's `pumpOnce` (Task 8) lands after the harness (Task 7) and the bridge's
`failureWatch` (Task 5), or a test file with a stray rejection dies with no TAP and a bridge exits on its first failed
write.

**Tech Stack:** C++20 with Catch2 (`retroplug-n8-test` and `retroplug-serial-test` through `pnpm test:n8`,
`retroplug-engine-graph-test`), the Pico SDK image builds (`pnpm device:check`, `pnpm device:box`), TypeScript on
txiki.js with the repo's TAP runners (`pnpm test`, `pnpm test:native`, `pnpm test:ui`) and `retroplug-cli`'s own
end-to-end check (`tools/run-cli-ts-tests.sh`), `pnpm typecheck`, `pnpm check:boundaries`, `pnpm check:docs`. No
submodule pointer moves, nothing is re-recorded.

**Spec:** [sdk-n8-port-design.md](sdk-n8-port-design.md). Its items 1 to 9 are Tasks 1 to 9 here, in that order. The
port map behind it is `.superpowers/sdkn8/port-map.md` (gitignored). The consumer the names and shapes are kept for is
nesvj's `retroplug-cli/tests/nesvj-raw.ts`.

**Provenance:** every code block below was applied to this worktree at `fc2578945` in this plan's order, test first and
then the implementation, and built and run before the plan was written: linux-x64, GCC 13.3, Node 26, a Release
`build/`, with the sibling fixture tree at `/workspaces/resources` where a step names it. There is no prototype branch,
so afterward the worktree was restored to its starting state and the plan was replayed by text: each step's patch
applied in order to copies of the touched files at `fc2578945`, each task's result byte-identical to the tree that was
validated. Every Expected line is quoted from a real run, and a failing-test step's output is what the code without
that step's fix produces. `jobs=16` in the runners' summaries is this machine's (half its 32 threads).

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

These are the repo's rules this port must keep. They are not optional and no step repeats them.

- **Never change directory**, not even in a subshell: prefix git with `git -C /workspaces/RetroPlug-main/cuttlefish`
  and pnpm with `pnpm -C /workspaces/RetroPlug-main/cuttlefish`, and give every other path absolute. Never switch or
  create a branch, never `git stash`, never `git reset --hard`, never push.
- **`deps/sameboy` is modified in a fresh tree and that is expected** (its tracked patches are applied at configure).
  Never stage, reset or stash it, and never touch `deps/dpf.js` or any other submodule. `git status --porcelain` lists
  it; `git add` only the paths a step names.
- **Never edit an existing `docs/*-plan.md` or design document**, this port's design included: its as-built and
  execution sections are the controller's, written after acceptance.
- **Never `rm -rf build`** (or any `build*/`). Build a target with
  `cmake --build /workspaces/RetroPlug-main/cuttlefish/build --target <t> -j$(nproc)`: the default is single-threaded.
- **The em dash (U+2014) never appears** in code, comments, docs or commit messages. Use " - ", ":" or ";". A line that
  already holds one is left as it is rather than rewritten.
- **A commit message is a subject line, a blank line, then a body saying why**, committed with `git commit -F -`. No
  `Co-Authored-By`, no "generated with" line, no attribution of any kind.
- **Match the surrounding code**: names, comment density and wrapping (comments explain WHY, in full sentences, at
  about 116-120 columns, and say what IS, not change history).
- **The hardware is absolute.** A real NES with an Everdrive N8 Pro, a Zoom L6, a capture card and MIDI interfaces are
  attached to this container, and nothing a step runs may open them: no `/dev/ttyACM*`, `/dev/ttyUSB*`,
  `/dev/serial/*`, `/dev/snd` (but `seq` and `timer`), `/dev/video*`, `/dev/hidraw*` or `/dev/bus/usb/*`. The N8 tests
  run against `FakeN8` and the fake serial port; never pass a real port, and never run an N8 tool (`n8-load`, `n8-play`,
  `n8-bridge`, `n8-sync`) or `retroplug-n8-hwtest`. Every command that launches `retroplug-cli`, the test host, a
  `pnpm test*` suite, a Catch2 binary, a device build or `tools/run-cli-ts-tests.sh` runs under the guard wrapper,
  `/workspaces/RetroPlug-main/cuttlefish/.superpowers/sdkn8/guarded.sh <command...>`, as every command below is
  written: it preloads the tracked device guard, self-tests it, refuses those paths before the syscall and prints any
  refusal it logged. **A refusal it prints is a finding**: stop and find what tried to open a device. Never run
  `retroplug-sdl`, any `sdl:*` or `reaper:*` script, or the verification kit (the controller runs acceptance).
- **No Python.** The tracked `tools/run-cli-ts-tests.sh` reads the render golden and hashes the rendered WAV with
  three `python3` snippets. Run it through its python-free copy instead: `node
  /workspaces/RetroPlug-main/cuttlefish/.superpowers/sdkn8/nopy.mjs` writes
  `.superpowers/sdkn8/run-cli-ts-tests.nopy.sh` from the tracked script as it stands, with REPO pinned to this worktree
  and those three snippets replaced by node one-liners printing the same text; every other line is the tracked
  script's. That copy does not build `retroplug-cli` first (`pnpm test:cli-ts` does), so each step that runs it builds
  the binary itself.
- **Nothing is re-recorded**: no `RP_*_UPDATE` variable, no golden, no parity baseline, no digest. `edio-golden.json`
  in particular stays byte-identical; a golden change would be a bug to find, not a step.
- **Never send repository content to an external service.**

---

## Task 1: the N8 lists a directory in pages of 32 records

`Edio::listDir` asks for every record of a directory in one `CMD_F_DIR_GET`, and the real N8 refuses that past some
size: a 181-entry directory answered "dir read error 0x82" on its first record, where 32 at a time lists it in 116 ms
(found and verified on the cart when `feat/sdk-n8` was written). Everything that lists reaches it: `n8ListDir`
(`n8-load --ls`, a script's `listDir`), every file read (`Ops::readFile` lists the file's directory first) and the cart
jobs' battery lookup. The listing goes in pages of 32; a listing of 32 or fewer is one page, the same bytes as one
request, so `edio-golden.json` (whose three listings are of one or two records) is unchanged. `FakeN8`, which
`retroplug-n8-test`, `retroplug-engine-graph-test` and the test host share, answered a `DIR_GET` with its whole listing
whatever range was asked, so with the paging a fake directory of more than 32 entries misframes the command after the
listing; it answers the range asked, and nothing more (no invented threshold for the silicon's refusal).

**Files** (line numbers are the file at `fc2578945`):

- Modify: `packages/n8/native/src/n8/Edio.cpp:350-395` (`Edio::listDir`).
- Modify: `packages/n8/native/src/n8/Edio.hpp:232-234` (`listDir`'s comment).
- Modify: `packages/n8/native/test/FakeN8.hpp:263-271` (the `CMD_F_DIR_GET` branch of `FakeN8Port::parse`).
- Test: `packages/n8/native/test/Edio.test.cpp:591` (a case after "listDir hands its visitor each entry").
- Test: `packages/n8/native/test/Ops.test.cpp:568` (a case before "the cart-SRAM paths").
- Test: `packages/retroplug/test-native/n8-session.test.ts:69` (a case before "n8-load boots a ROM").

**Interfaces:**

- Consumes: `Edio::listDir(std::string_view, F&& visit, std::uint16_t* count)` and `rp::n8::readFile(Edio&, path,
  buffer, capacity, size*)` (`Ops.hpp`), unchanged; `FakeN8`'s `files` map; the test host's fake cart on port
  `fake-n8` and `n8LoadTool` (`@retroplug/n8/cli`).
- Produces: no new interface. `listDir`'s wire behaviour changes (a `DIR_GET` a page of at most 32 records), and
  `FakeN8Port` answers `DIR_GET`'s `start` / `amount` (little-endian at bytes 4 and 6 of the frame).

### Steps

- [ ] **Step 1: write the failing case**

```patch
diff --git a/packages/n8/native/test/Edio.test.cpp b/packages/n8/native/test/Edio.test.cpp
index 1988e8dd2..781e26498 100644
--- a/packages/n8/native/test/Edio.test.cpp
+++ b/packages/n8/native/test/Edio.test.cpp
@@ -590,6 +590,34 @@ TEST_CASE("listDir hands its visitor each entry, a 255-byte name whole, and the
     CHECK(empty.written == fromHex("2bd4c53a01010078" "2bd410ef" "2bd4c639"));
 }
 
+TEST_CASE("listDir pulls a listing of more than 32 records in pages of 32: 32 and then the rest", "[n8][edio-frame]") {
+    // One CMD_F_DIR_GET for every record fails on silicon past some size (a 181-entry directory answered "dir read error
+    // 0x82" on its first record), and 32 at a time lists it. Each page is a request of its own, and the visitor sees the
+    // records in order across them.
+    FakeSerialPort port;
+    port.queueStatus(0xA500);  // DIR_LD's status
+    port.queue({40, 0});        // DIR_SIZE: 40 records
+    std::vector<std::string> want;
+    for (int i = 0; i < 40; ++i) {
+        const auto letter = static_cast<std::uint8_t>('A' + i);
+        port.queue({0x00, 0x04, 0, 0, 0, 0, 0, 0, 0, 0x20, 0x01, 0x00, letter});  // a 4-byte file, a 1-byte name
+        want.emplace_back(1, static_cast<char>(letter));
+    }
+    std::vector<std::string> seen;
+    std::uint16_t            count = 0;
+    const auto visit = [&](const rp::n8::DirEntry& e) { seen.emplace_back(e.name); };
+    REQUIRE(Edio(port).listDir("x", visit, &count).ok());
+    CHECK(count == 40);
+    CHECK(seen == want);
+    // Each DIR_GET's arguments (start, amount, the longest name asked for): 32 records from 0, then the last 8 from 32.
+    std::vector<std::vector<int>> pages;
+    const auto&                   w = port.written;
+    for (std::size_t i = 0; i + 10 <= w.size(); ++i)
+        if (w[i] == 0x2B && w[i + 1] == 0xD4 && w[i + 2] == Edio::CMD_F_DIR_GET && w[i + 3] == 0x37)
+            pages.push_back({w[i + 4] | (w[i + 5] << 8), w[i + 6] | (w[i + 7] << 8), w[i + 8] | (w[i + 9] << 8)});
+    CHECK(pages == std::vector<std::vector<int>>{{0, 32, 255}, {32, 8, 255}});
+}
+
 TEST_CASE("a link that goes down inside a wait fails it at once, as LinkDown, not a timeout", "[n8][edio-frame]") {
     FakeSerialPort port;
     port.downAfterReads = 1;   // the first read serves half the reply, then the link goes
```

- [ ] **Step 2: build it and watch it fail**

```bash
cmake --build /workspaces/RetroPlug-main/cuttlefish/build --target retroplug-n8-test -j$(nproc)
/workspaces/RetroPlug-main/cuttlefish/.superpowers/sdkn8/guarded.sh \
  /workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-n8-test \
  "listDir pulls a listing of more than 32 records in pages of 32: 32 and then the rest"
```

Expected: exit 42, one request for all 40 records:

```
/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/Edio.test.cpp:618: FAILED:
  CHECK( pages == std::vector<std::vector<int>>{{0, 32, 255}, {32, 8, 255}} )
with expansion:
  { { 0, 40, 255 } }
  ==
  { { 0, 32, 255 }, { 32, 8, 255 } }

===============================================================================
test cases: 1 | 1 failed
assertions: 4 | 3 passed | 1 failed
```

- [ ] **Step 3: page the listing**

```patch
diff --git a/packages/n8/native/src/n8/Edio.cpp b/packages/n8/native/src/n8/Edio.cpp
index 115eb14b7..2613e2bd9 100644
--- a/packages/n8/native/src/n8/Edio.cpp
+++ b/packages/n8/native/src/n8/Edio.cpp
@@ -350,7 +350,7 @@ Status Edio::fifoTxString(std::string_view str) {
 Status Edio::listDir(std::string_view path, void (*visit)(void* ctx, const DirEntry& entry), void* ctx,
                      std::uint16_t* count) {
     if (count) *count = 0;
-    // Load the directory into the N8's buffer (sorted), then pull every record in one range read.
+    // Load the directory into the N8's buffer (sorted), then pull its records in pages of kDirPage.
     begin(CMD_F_DIR_LD);
     put8(1);  // sorted
     if (Status s = sendPath(path); !s.ok()) return s;
@@ -363,33 +363,40 @@ Status Edio::listDir(std::string_view path, void (*visit)(void* ctx, const DirEn
     if (count) *count = records;
     if (records == 0) return {};
 
-    begin(CMD_F_DIR_GET);
-    put16(0);  // start index
-    put16(records);
-    put16(static_cast<std::uint16_t>(kNameMax));  // max name length
-    if (Status s = send(); !s.ok()) return s;
-    for (std::uint16_t i = 0; i < records; ++i) {
-        std::uint8_t resp = 0;
-        if (Status s = rx8(resp); !s.ok()) return s;
-        if (resp != 0) {
-            Status s = fail(Code::Device, Where::DirEntry);
-            s.device = resp;
-            return s;
+    // One request for every record fails on silicon past some size: a 181-entry directory answered "dir read error
+    // 0x82" on its first record, where pages of 32 list it (in 116 ms). A listing of 32 or fewer is one page, the same
+    // bytes as one request. The index is 32-bit, so start + kDirPage cannot wrap.
+    constexpr std::uint32_t kDirPage = 32;
+    for (std::uint32_t start = 0; start < records; start += kDirPage) {
+        const auto amount = static_cast<std::uint16_t>(std::min<std::uint32_t>(kDirPage, records - start));
+        begin(CMD_F_DIR_GET);
+        put16(static_cast<std::uint16_t>(start));
+        put16(amount);
+        put16(static_cast<std::uint16_t>(kNameMax));  // max name length
+        if (Status s = send(); !s.ok()) return s;
+        for (std::uint16_t i = 0; i < amount; ++i) {
+            std::uint8_t resp = 0;
+            if (Status s = rx8(resp); !s.ok()) return s;
+            if (resp != 0) {
+                Status s = fail(Code::Device, Where::DirEntry);
+                s.device = resp;
+                return s;
+            }
+            DirEntry      e;
+            std::uint16_t dateWord = 0, timeWord = 0, nameLen = 0;
+            std::uint8_t  attrib = 0;
+            if (Status s = rx32(e.size); !s.ok()) return s;
+            if (Status s = rx16(dateWord); !s.ok()) return s;  // unused
+            if (Status s = rx16(timeWord); !s.ok()) return s;  // unused
+            if (Status s = rx8(attrib); !s.ok()) return s;
+            if (Status s = rx16(nameLen); !s.ok()) return s;
+            if (nameLen > kNameMax) return fail(Code::TooLarge, Where::Name);
+            if (Status s = readData(reinterpret_cast<std::uint8_t*>(name_), nameLen); !s.ok()) return s;
+            name_[nameLen] = '\0';
+            e.name         = std::string_view(name_, nameLen);
+            e.isDir        = (attrib & 0x10) != 0;  // FatFs AM_DIR
+            if (visit) visit(ctx, e);
         }
-        DirEntry      e;
-        std::uint16_t dateWord = 0, timeWord = 0, nameLen = 0;
-        std::uint8_t  attrib = 0;
-        if (Status s = rx32(e.size); !s.ok()) return s;
-        if (Status s = rx16(dateWord); !s.ok()) return s;  // unused
-        if (Status s = rx16(timeWord); !s.ok()) return s;  // unused
-        if (Status s = rx8(attrib); !s.ok()) return s;
-        if (Status s = rx16(nameLen); !s.ok()) return s;
-        if (nameLen > kNameMax) return fail(Code::TooLarge, Where::Name);
-        if (Status s = readData(reinterpret_cast<std::uint8_t*>(name_), nameLen); !s.ok()) return s;
-        name_[nameLen] = '\0';
-        e.name         = std::string_view(name_, nameLen);
-        e.isDir        = (attrib & 0x10) != 0;  // FatFs AM_DIR
-        if (visit) visit(ctx, e);
     }
     return {};
 }
diff --git a/packages/n8/native/src/n8/Edio.hpp b/packages/n8/native/src/n8/Edio.hpp
index 93797d53a..014a81261 100644
--- a/packages/n8/native/src/n8/Edio.hpp
+++ b/packages/n8/native/src/n8/Edio.hpp
@@ -229,9 +229,10 @@ public:
     // (how the on-device menu receives a path, edlink DeviceIO.FifoTxString).
     Status fifoTxString(std::string_view s);
 
-    // List an SD-card directory, sorted: loaded into the N8's buffer, then every record pulled. `visit(ctx, entry)` is
-    // called once an entry, its name in this object's own buffer, so a listing has no capacity of its own; `count`,
-    // when given, gets the number of records the N8 said it holds.
+    // List an SD-card directory, sorted: loaded into the N8's buffer, then its records pulled 32 at a time (one request
+    // for a whole large directory fails on silicon). `visit(ctx, entry)` is called once an entry, its name in this
+    // object's own buffer, so a listing has no capacity of its own; `count`, when given, gets the number of records
+    // the N8 said it holds.
     Status listDir(std::string_view path, void (*visit)(void* ctx, const DirEntry& entry), void* ctx,
                    std::uint16_t* count = nullptr);
     template <class F>
```

- [ ] **Step 4: rebuild, run the case and the binary**

```bash
cmake --build /workspaces/RetroPlug-main/cuttlefish/build --target retroplug-n8-test -j$(nproc)
/workspaces/RetroPlug-main/cuttlefish/.superpowers/sdkn8/guarded.sh \
  /workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-n8-test \
  "listDir pulls a listing of more than 32 records in pages of 32: 32 and then the rest"
/workspaces/RetroPlug-main/cuttlefish/.superpowers/sdkn8/guarded.sh \
  /workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-n8-test
```

Expected: the case passes, and so does the whole binary, the golden case ("Edio framing matches the golden byte for
byte") among them, untouched:

```
===============================================================================
All tests passed (4 assertions in 1 test case)
```

```
===============================================================================
All tests passed (1472 assertions in 105 test cases)
```

- [ ] **Step 5: write the cases that read past the first page through the fake cart**

An `Ops` case reads a file on the second page, and the test host's `n8-session` lists 40 files and gets the last one
back through the real facet. Both are red against the paging until the fake answers the range asked: its whole listing
answers the first page, the second page's request is answered with another whole listing, and those records are left
in the line for the next command to read as its reply.

```patch
diff --git a/packages/n8/native/test/Ops.test.cpp b/packages/n8/native/test/Ops.test.cpp
index 119d4e8d7..e0a1345f5 100644
--- a/packages/n8/native/test/Ops.test.cpp
+++ b/packages/n8/native/test/Ops.test.cpp
@@ -566,6 +566,27 @@ TEST_CASE("writeFile and readFile round-trip a file; NotFound, TooLarge with the
     CHECK(dev.filesOpen == 0);  // the cancel came between two blocks, and closed the file
 }
 
+TEST_CASE("readFile finds a file on a directory listing's second page and the next command starts clean", "[n8][ops]") {
+    // A listing is pulled 32 records at a time (Edio::listDir), so a file past the first 32 is found, and the command
+    // after the listing framed, only when each page answers the records asked for and no more.
+    FakeN8     dev;
+    FakeN8Port port(dev);
+    Edio       edio(port);
+    for (int i = 0; i < 40; ++i) {
+        char name[16];
+        std::snprintf(name, sizeof name, "big/f%02d.bin", i);
+        dev.files[name] = pattern(static_cast<std::size_t>(10 + i), static_cast<std::uint8_t>(i));
+    }
+    Bytes       buf(64);
+    std::size_t size = 0;
+    const Status read = rp::n8::readFile(edio, "big/f39.bin", buf.data(), buf.size(), &size);
+    CHECK(text(read) == "");
+    REQUIRE(read.ok());
+    CHECK(Bytes(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(size)) == dev.files["big/f39.bin"]);
+    std::uint8_t code = 0;
+    CHECK(edio.status(code).ok());
+}
+
 TEST_CASE("the cart-SRAM paths: a battery written straight to SRAM reads back, and a dump reads all of it",
           "[n8][ops]") {
     FakeN8      dev;
diff --git a/packages/retroplug/test-native/n8-session.test.ts b/packages/retroplug/test-native/n8-session.test.ts
index ec2f8f89c..082acc1bf 100644
--- a/packages/retroplug/test-native/n8-session.test.ts
+++ b/packages/retroplug/test-native/n8-session.test.ts
@@ -67,6 +67,28 @@ test("n8-load reads the card: info, free space, mkdir, a file put, listed, got b
   s.backend.deleteFile(local("got.bin"));
 });
 
+test("n8-load lists a directory of more than one listing page, and gets a file from its second page", () => {
+  // The host pulls a listing 32 records at a time (Edio::listDir): 40 files are two pages, and a file read lists its
+  // directory first, so the read is framed right only when each page answered the records asked for.
+  const name = (i: number): string => `f${String(i).padStart(2, "0")}.bin`;
+  const n8 = createN8Client(makeCall()).openN8("fake-n8");
+  try {
+    n8.connect();
+    for (let i = 0; i < 40; i++) n8.writeFile(`big/${name(i)}`, new Uint8Array(i + 1).fill(i));
+  } finally {
+    n8.close();
+  }
+  const listing = n8Load("--ls", "big");
+  expect(listing.length).toBe(41);
+  expect(listing[0]).toBe("big (40 entries):");
+  expect(listing.slice(1)).toEqual(Array.from({ length: 40 }, (_, i) => `  ${String(i + 1).padStart(8)}  ${name(i)}`));
+  expect(n8Load("--get-file", "big/f39.bin", local("f39.bin"))).toEqual([
+    `read 40 bytes of big/f39.bin -> ${local("f39.bin")}`,
+  ]);
+  expect(Array.from(s.backend.readFile(local("f39.bin")) ?? [])).toEqual(new Array(40).fill(39));
+  s.backend.deleteFile(local("f39.bin"));
+});
+
 test("n8-load boots a ROM with its battery through the menu, and the running cart answers as a game", () => {
   const rom = new Uint8Array(16 + 32768);
   rom.set([0x4e, 0x45, 0x53, 0x1a, 2, 0, 0x10, 0]); // iNES, mapper 1
```

- [ ] **Step 6: build and run both, and watch them fail**

```bash
cmake --build /workspaces/RetroPlug-main/cuttlefish/build --target retroplug-n8-test -j$(nproc)
/workspaces/RetroPlug-main/cuttlefish/.superpowers/sdkn8/guarded.sh \
  /workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-n8-test \
  "readFile finds a file on a directory listing's second page and the next command starts clean"
/workspaces/RetroPlug-main/cuttlefish/.superpowers/sdkn8/guarded.sh \
  pnpm -C /workspaces/RetroPlug-main/cuttlefish test:native n8-session
```

Expected: the `Ops` case exits 42 with the file open answered by a listing record:

```
/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/Ops.test.cpp:583: FAILED:
  CHECK( text(read) == "" )
with expansion:
  "Edio: unexpected status response (0a00)"
  ==
  ""

/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/Ops.test.cpp:584: FAILED:
  REQUIRE( read.ok() )
with expansion:
  false

===============================================================================
test cases: 1 | 1 failed
assertions: 2 | 2 failed
```

and `n8-session`'s new case fails the same way at `--get-file` (its `--ls` reads the right 40, since the first reply
holds them all), the rest passing:

```
not ok 3 - n8-load lists a directory of more than one listing page, and gets a file from its second page
  ---
  Edio: unexpected status response (0100)
```

```
# 4 passed, 0 skipped, 1 failed

# 1/1 native test file(s) FAILED: n8-session
```

- [ ] **Step 7: the fake answers the range asked**

```patch
diff --git a/packages/n8/native/test/FakeN8.hpp b/packages/n8/native/test/FakeN8.hpp
index 25e7de5bd..66bd4e816 100644
--- a/packages/n8/native/test/FakeN8.hpp
+++ b/packages/n8/native/test/FakeN8.hpp
@@ -261,7 +261,13 @@ private:
                 const auto n = static_cast<std::uint16_t>(dirList_.size());
                 reply({static_cast<std::uint8_t>(n), static_cast<std::uint8_t>(n >> 8)});
             } else if (cmd == Edio::CMD_F_DIR_GET) {
-                for (const auto& [name, size, isDir] : dirList_) {
+                // The records asked for, `amount` of them from `start`: a listing comes a page at a time
+                // (Edio::listDir), and a page answered with more than it asked for misframes the command after it.
+                const std::size_t start  = c[4] | (c[5] << 8);
+                const std::size_t amount = c[6] | (c[7] << 8);
+                const std::size_t end    = std::min(dirList_.size(), start + amount);
+                for (std::size_t i = std::min(start, end); i < end; ++i) {
+                    const auto& [name, size, isDir] = dirList_[i];
                     const auto s = static_cast<std::uint32_t>(size);
                     reply({0, static_cast<std::uint8_t>(s), static_cast<std::uint8_t>(s >> 8),
                            static_cast<std::uint8_t>(s >> 16), static_cast<std::uint8_t>(s >> 24), 0, 0, 0, 0,
```

- [ ] **Step 8: rebuild and run both again**

```bash
cmake --build /workspaces/RetroPlug-main/cuttlefish/build --target retroplug-n8-test -j$(nproc)
/workspaces/RetroPlug-main/cuttlefish/.superpowers/sdkn8/guarded.sh \
  /workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-n8-test \
  "readFile finds a file on a directory listing's second page and the next command starts clean"
/workspaces/RetroPlug-main/cuttlefish/.superpowers/sdkn8/guarded.sh \
  pnpm -C /workspaces/RetroPlug-main/cuttlefish test:native n8-session
```

Expected:

```
===============================================================================
All tests passed (4 assertions in 1 test case)
```

```
ok 3 - n8-load lists a directory of more than one listing page, and gets a file from its second page
ok 4 - n8-load boots a ROM with its battery through the menu, and the running cart answers as a game
ok 5 - a session does not open the port the streaming link holds, in the serial client's words
# 5 passed, 0 skipped, 0 failed

# 1 native test file(s) passed (jobs=16)
```

- [ ] **Step 9: the wider checks this reaches**

`FakeN8` is shared with the engine's N8 adapters, and `retroplug-n8` is `device: true`, so the change also compiles
into the RP2350 images under `-Werror=stack-usage=1024` and the link trap (the loop adds nothing to the stack and
allocates nothing).

```bash
/workspaces/RetroPlug-main/cuttlefish/.superpowers/sdkn8/guarded.sh pnpm -C /workspaces/RetroPlug-main/cuttlefish test:n8
cmake --build /workspaces/RetroPlug-main/cuttlefish/build --target retroplug-engine-graph-test -j$(nproc)
/workspaces/RetroPlug-main/cuttlefish/.superpowers/sdkn8/guarded.sh \
  /workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-engine-graph-test "[n8-adapters]"
/workspaces/RetroPlug-main/cuttlefish/.superpowers/sdkn8/guarded.sh pnpm -C /workspaces/RetroPlug-main/cuttlefish device:check
/workspaces/RetroPlug-main/cuttlefish/.superpowers/sdkn8/guarded.sh pnpm -C /workspaces/RetroPlug-main/cuttlefish device:box
```

Expected: `test:n8` ends with the two binaries' summaries,

```
All tests passed (1476 assertions in 106 test cases)
```

```
All tests passed (17 assertions in 2 test cases)
```

the adapters with `All tests passed (47 assertions in 1 test case)`, and both device builds exit 0: `device:check`
recompiles the pack (`[3/9] Building CXX object packages/n8/CMakeFiles/retroplug-n8.dir/src/n8/Edio.cpp.o`) and links
`rp-device-check.elf`, its trap probe refusing to link as it must, and `device:box` ends with
`== native: flash 199920, data 32084, bss 114368 bytes; ...` after the `pio` image.

- [ ] **Step 10: commit**

```bash
git -C /workspaces/RetroPlug-main/cuttlefish add -- \
  packages/n8/native/src/n8/Edio.cpp \
  packages/n8/native/src/n8/Edio.hpp \
  packages/n8/native/test/FakeN8.hpp \
  packages/n8/native/test/Edio.test.cpp \
  packages/n8/native/test/Ops.test.cpp \
  packages/retroplug/test-native/n8-session.test.ts
git -C /workspaces/RetroPlug-main/cuttlefish commit -F - <<'MSG'
fix(sdk-n8): list an N8 directory in pages of 32 records

Edio::listDir asked for every record of a directory in one CMD_F_DIR_GET, which the real N8 refuses past some size: a
181-entry directory answered "dir read error 0x82" on its first record, where 32 at a time lists it in 116 ms (found
on the cart by feat/sdk-n8). Everything that lists goes through it: n8ListDir (n8-load --ls), every file read, since
Ops::readFile lists the file's directory first, and the cart jobs' battery lookup. A listing of 32 or fewer is one
page, the same bytes as one request, so edio-golden.json is unchanged.

FakeN8 answered a DIR_GET with its whole listing whatever range was asked, so with pages a fake directory of more than
32 entries left records in the line for the command after it; it answers the range now, and models no refusal
threshold, since the silicon's is not known. An Edio case pins the split, an Ops case reads a file on the second page,
and test:native's n8-session lists and reads past 32 entries through the facet.
MSG
```

---

## Task 2: one `pickN8Port`, and a `romName` that is a path refused

The CLI's three N8 tools each carried a private copy of the port picker (`bridge.ts`, `cli/n8Load.ts`, `cli/n8Play.ts`,
in two wordings), and the device Task 4 adds needs the same one: `@retroplug/n8` exports `pickN8Port`, worded as two
of the three were ("serial ports: ..."). The client's `load()` built `usb-games/<romName>` from whatever name it was
given, so a name with a separator or a dot directory uploaded somewhere else on the card; it refuses one before
anything is sent. `n8-load` always passes a base name, and the SDK's device shares the guard through the client.

**Files** (line numbers are the file at `fc2578945`):

- Modify: `packages/n8/ts/index.ts:166` (`pickN8Port`, after `N8Client`) and `:276` (the guard, in `load()`).
- Modify: `packages/n8/ts/bridge.ts:6`, `:16-26` and `:47`; `packages/n8/ts/cli/n8Load.ts:7-24`, `:79-88` and `:319`;
  `packages/n8/ts/cli/n8Play.ts:12`, `:19-28` and `:156` (the copies, and their callers).
- Test: `packages/n8/ts/test/session-client.test.ts:19` (the import) and `:152` (two cases before "the direct writes").

**Interfaces:**

- Consumes: `N8Port` (`{ port: string; isN8: boolean }`, `index.ts`) and the client's `load(opts: LoadOptions)`.
- Produces: `export function pickN8Port(ports: N8Port[], explicit: string | undefined): string` from `@retroplug/n8`
  (Task 4's `openN8Device` uses it), and `load()` throwing `loadRom: romName must be a plain file name, not "<name>"`
  for a name holding `/` or `\`, or one that is `.` or `..`.

### Steps

- [ ] **Step 1: write the failing `romName` case**

```patch
diff --git a/packages/n8/ts/test/session-client.test.ts b/packages/n8/ts/test/session-client.test.ts
index a18ef42ba..2a1b60a78 100644
--- a/packages/n8/ts/test/session-client.test.ts
+++ b/packages/n8/ts/test/session-client.test.ts
@@ -150,6 +150,17 @@ test("load boots through the host's bootRom: an upload to usb-games, or the SD p
   expect(sent.length).toBe(3);
 });
 
+test("load refuses a romName that is not a plain file name, before anything is sent", () => {
+  // The name lands in usb-games/: a separator or a dot directory would put the upload somewhere else on the card.
+  const { call, sent } = fakeHost({ n8BootRom: okReply({ value: 69 }) });
+  const n8 = createN8Client(call).openN8("p");
+  for (const romName of ["../EDN8/x.nes", "a/b.nes", "a\\b.nes", ".", ".."])
+    expect(() => n8.load({ romBytes: new Uint8Array(16), romName })).toThrow(
+      `loadRom: romName must be a plain file name, not "${romName}"`,
+    );
+  expect(sent.map((s) => s[0])).toEqual(["n8Open"]);
+});
+
 test("the direct writes are a memWR and a read-back memRD, and fail on a mismatch", () => {
   let mem = new Uint8Array(0);
   const echo = fakeHost({
```

- [ ] **Step 2: run it and watch it fail**

```bash
/workspaces/RetroPlug-main/cuttlefish/.superpowers/sdkn8/guarded.sh \
  pnpm -C /workspaces/RetroPlug-main/cuttlefish test n8/session-client
```

Expected: the build lines, then

```
not ok 4 - load refuses a romName that is not a plain file name, before anything is sent
  ---
  expected function to throw, but it did not
```

```
# 6 passed, 0 skipped, 1 failed

# 1/1 test file(s) FAILED: n8/session-client
```

- [ ] **Step 3: refuse the name in `load()`**

```patch
diff --git a/packages/n8/ts/index.ts b/packages/n8/ts/index.ts
index 603d2f467..2786350da 100644
--- a/packages/n8/ts/index.ts
+++ b/packages/n8/ts/index.ts
@@ -273,6 +273,10 @@ function openSession(call: N8Call, port: string): N8 {
       if (!bootPath) {
         if (!opts.romBytes || !opts.romName)
           throw new Error("loadRom: pass romBytes + romName to upload, or sdPath to boot an existing SD ROM");
+        // An upload name, not a path: it lands in usb-games/, and a separator or a dot directory would put it
+        // somewhere else on the card.
+        if (/[/\\]/.test(opts.romName) || opts.romName === "." || opts.romName === "..")
+          throw new Error(`loadRom: romName must be a plain file name, not "${opts.romName}"`);
         bootPath = `usb-games/${opts.romName}`;
       }
       const upload = !opts.sdPath;
```

- [ ] **Step 4: run it again**

Same command. Expected:

```
ok 4 - load refuses a romName that is not a plain file name, before anything is sent
ok 5 - the direct writes are a memWR and a read-back memRD, and fail on a mismatch
ok 6 - assertGameRegion allows the game region and blocks the N8 OS region
ok 7 - mapConfig reads the FPGA config block and decodes it
# 7 passed, 0 skipped, 0 failed

# 1 test file(s) passed (jobs=16)
```

- [ ] **Step 5: write the failing `pickN8Port` case**

```patch
diff --git a/packages/n8/ts/test/session-client.test.ts b/packages/n8/ts/test/session-client.test.ts
index 2a1b60a78..d6010b4d5 100644
--- a/packages/n8/ts/test/session-client.test.ts
+++ b/packages/n8/ts/test/session-client.test.ts
@@ -16,6 +16,7 @@ import {
   N8_OS_REGION,
   SIZE_SRM_GAME,
   assertGameRegion,
+  pickN8Port,
   type N8Call,
 } from "../index";
 
@@ -161,6 +162,22 @@ test("load refuses a romName that is not a plain file name, before anything is s
   expect(sent.map((s) => s[0])).toEqual(["n8Open"]);
 });
 
+test("pickN8Port takes the port asked for, else the first N8, and names the ports there are when none is one", () => {
+  const ports = [
+    { port: "/dev/ttyS0", isN8: false },
+    { port: "/dev/ttyACM1", isN8: true },
+    { port: "/dev/ttyACM2", isN8: true },
+  ];
+  expect(pickN8Port(ports, undefined)).toBe("/dev/ttyACM1");
+  expect(pickN8Port(ports, "/dev/ttyS0")).toBe("/dev/ttyS0");
+  expect(() => pickN8Port([{ port: "/dev/ttyS0", isN8: false }], undefined)).toThrow(
+    "no Everdrive N8 Pro found (VID:PID 38df:0017). Plug it in, or pass --serial <port>.\nserial ports: /dev/ttyS0",
+  );
+  expect(() => pickN8Port([], undefined)).toThrow(
+    "no Everdrive N8 Pro found (VID:PID 38df:0017). Plug it in, or pass --serial <port>.\nno serial ports detected",
+  );
+});
+
 test("the direct writes are a memWR and a read-back memRD, and fail on a mismatch", () => {
   let mem = new Uint8Array(0);
   const echo = fakeHost({
```

- [ ] **Step 6: run it and watch it fail**

Same command. Expected: the file does not build, since the export does not exist:

```
✘ [ERROR] No matching export in "packages/n8/ts/index.ts" for import "pickN8Port"

    packages/n8/ts/test/session-client.test.ts:19:2:
      19 │   pickN8Port,
         ╵   ~~~~~~~~~~


# BUILD FAILED: n8/session-client
```

- [ ] **Step 7: one `pickN8Port`, used by the three tools**

```patch
diff --git a/packages/n8/ts/bridge.ts b/packages/n8/ts/bridge.ts
index 20643abac..df27fff0f 100644
--- a/packages/n8/ts/bridge.ts
+++ b/packages/n8/ts/bridge.ts
@@ -3,7 +3,7 @@
 // It lives in the n8 package and is published as `@retroplug/n8/bridge` so risa's n8-sync can drive the same loop,
 // which keeps n8 free of every cart (risa depends on n8, not the other way about).
 import type { CliHost } from "@retroplug/core/cliTool";
-import { createN8Client, type N8Port } from "./index";
+import { createN8Client, pickN8Port } from "./index";
 
 // txiki provides this libuv-backed global at runtime (deps/.../txiki timers); the TS lib config omits it.
 declare function setInterval(handler: () => void, ms: number): number;
@@ -13,18 +13,6 @@ const flag = (args: string[], name: string): string | undefined => {
   return i >= 0 ? args[i + 1] : undefined;
 };
 
-// The port to talk to: an explicit --serial, else the first attached N8 (USB VID:PID 38df:0017).
-function pickPort(ports: N8Port[], explicit: string | undefined): string {
-  if (explicit) return explicit;
-  const n8 = ports.find((p) => p.isN8);
-  if (!n8)
-    throw new Error(
-      "no Everdrive N8 Pro found (VID:PID 38df:0017). Plug it in, or pass --serial <port>.\n" +
-        (ports.length ? `serial ports: ${ports.map((p) => p.port).join(", ")}` : "no serial ports detected"),
-    );
-  return n8.port;
-}
-
 export function printInputs(host: CliHost): void {
   const inputs = host.devices.midi().listInputs();
   if (inputs.length === 0) {
@@ -44,7 +32,7 @@ export function runBridge(host: CliHost, args: string[], clientName: string,
   const lookaheadMs = Math.max(0, Number(flag(args, "--lookahead-ms") ?? 0) | 0);
 
   const client = createN8Client(host.devices.rpc);
-  const n8 = client.openN8(pickPort(client.listPorts(), flag(args, "--serial")));
+  const n8 = client.openN8(pickN8Port(client.listPorts(), flag(args, "--serial")));
   n8.connect(); // throws if the N8 doesn't answer the handshake
 
   const midi = host.devices.midi();
diff --git a/packages/n8/ts/cli/n8Load.ts b/packages/n8/ts/cli/n8Load.ts
index 2e3db1351..7c4836d17 100644
--- a/packages/n8/ts/cli/n8Load.ts
+++ b/packages/n8/ts/cli/n8Load.ts
@@ -6,6 +6,7 @@
 import type { CliHost, CliTool } from "@retroplug/core/cliTool";
 import {
   createN8Client,
+  pickN8Port,
   baseName,
   assertGameRegion,
   decodeMapConfig,
@@ -21,7 +22,6 @@ import {
   type ChrWindow,
   type N8,
   type N8MapConfig,
-  type N8Port,
   type LoadOptions,
   menuScreenToRgba,
   decodeSniffer,
@@ -76,17 +76,6 @@ function positional(args: string[]): string | undefined {
   return undefined;
 }
 
-function pickPort(ports: N8Port[], explicit: string | undefined): string {
-  if (explicit) return explicit;
-  const n8 = ports.find((p) => p.isN8);
-  if (!n8)
-    throw new Error(
-      "no Everdrive N8 Pro found (VID:PID 38df:0017). Plug it in, or pass --serial <port>.\n" +
-        (ports.length ? `ports: ${ports.map((p) => p.port).join(", ")}` : "no serial ports detected"),
-    );
-  return n8.port;
-}
-
 function readOrThrow(host: CliHost, path: string, what: string): Uint8Array {
   const data = host.files.readFile(path);
   if (!data) throw new Error(`cannot read ${what}: ${path}`);
@@ -316,7 +305,7 @@ export const n8LoadTool: CliTool = {
     const putBytes = putFile != null ? readOrThrow(host, putFile, "file") : undefined;
 
     const client = createN8Client(host.devices.rpc);
-    const n8 = client.openN8(pickPort(client.listPorts(), serialPort));
+    const n8 = client.openN8(pickN8Port(client.listPorts(), serialPort));
     try {
       n8.connect(); // throws if the N8 doesn't answer the handshake
 
diff --git a/packages/n8/ts/cli/n8Play.ts b/packages/n8/ts/cli/n8Play.ts
index 14ebcf97d..dcdff5a5a 100644
--- a/packages/n8/ts/cli/n8Play.ts
+++ b/packages/n8/ts/cli/n8Play.ts
@@ -9,24 +9,13 @@
 // which is what lets a check be split across commands (hold a note with one call, push a SysEx with the
 // next, capture the whole time).
 import type { CliHost, CliTool } from "@retroplug/core/cliTool";
-import { createN8Client, ADDR_EXP_VOL, type N8Port } from "../index";
+import { createN8Client, ADDR_EXP_VOL, pickN8Port } from "../index";
 
 const flag = (args: string[], name: string): string | undefined => {
   const i = args.indexOf(name);
   return i >= 0 ? args[i + 1] : undefined;
 };
 
-function pickPort(ports: N8Port[], explicit: string | undefined): string {
-  if (explicit) return explicit;
-  const n8 = ports.find((p) => p.isN8);
-  if (!n8)
-    throw new Error(
-      "no Everdrive N8 Pro found (VID:PID 38df:0017). Plug it in, or pass --serial <port>.\n" +
-        (ports.length ? `serial ports: ${ports.map((p) => p.port).join(", ")}` : "no serial ports detected"),
-    );
-  return n8.port;
-}
-
 /** Blocking delay. The session runtime has no synchronous sleep, and a note hold has to span a real audio
  *  capture, so a hardware-test tool spins. Holds here are seconds at most. */
 function spin(ms: number): void {
@@ -153,7 +142,7 @@ function run(host: CliHost, args: string[]): void {
   const plan = expVolPlan(flag(args, "--exp-vol"), mapper);
 
   const client = createN8Client(host.devices.rpc);
-  const n8 = client.openN8(pickPort(client.listPorts(), flag(args, "--serial")));
+  const n8 = client.openN8(pickN8Port(client.listPorts(), flag(args, "--serial")));
   n8.connect();
 
   // Expansion audio (VRC6/VRC7/N163/S5B/MMC5) is mixed by the FPGA, not the 2A03 - if this is 0 the
diff --git a/packages/n8/ts/index.ts b/packages/n8/ts/index.ts
index 2786350da..449250038 100644
--- a/packages/n8/ts/index.ts
+++ b/packages/n8/ts/index.ts
@@ -163,6 +163,20 @@ export interface N8Client {
   openN8(port: string): N8;
 }
 
+/** The port to talk to: `explicit` when one was asked for (a --serial), else the first port flagged as an N8 Pro (USB
+ *  VID:PID 38df:0017). Throws, naming the ports there are, when none is one. Shared by the CLI's N8 tools and the
+ *  device a session script opens. */
+export function pickN8Port(ports: N8Port[], explicit: string | undefined): string {
+  if (explicit) return explicit;
+  const n8 = ports.find((p) => p.isN8);
+  if (!n8)
+    throw new Error(
+      "no Everdrive N8 Pro found (VID:PID 38df:0017). Plug it in, or pass --serial <port>.\n" +
+        (ports.length ? `serial ports: ${ports.map((p) => p.port).join(", ")}` : "no serial ports detected"),
+    );
+  return n8.port;
+}
+
 // The reply read tolerantly, field by field, as the UI read the hook object it replaces: a host one version apart must
 // not take the submenu down.
 function readConfig(raw: unknown): N8LinkConfig {
```

- [ ] **Step 8: run it again, and the wider checks**

```bash
/workspaces/RetroPlug-main/cuttlefish/.superpowers/sdkn8/guarded.sh \
  pnpm -C /workspaces/RetroPlug-main/cuttlefish test n8/session-client
/workspaces/RetroPlug-main/cuttlefish/.superpowers/sdkn8/guarded.sh pnpm -C /workspaces/RetroPlug-main/cuttlefish test n8
/workspaces/RetroPlug-main/cuttlefish/.superpowers/sdkn8/guarded.sh pnpm -C /workspaces/RetroPlug-main/cuttlefish test risa
pnpm -C /workspaces/RetroPlug-main/cuttlefish/packages/n8 typecheck
pnpm -C /workspaces/RetroPlug-main/cuttlefish/packages/risa typecheck
pnpm -C /workspaces/RetroPlug-main/cuttlefish check:boundaries
/workspaces/RetroPlug-main/cuttlefish/.superpowers/sdkn8/guarded.sh \
  pnpm -C /workspaces/RetroPlug-main/cuttlefish test:native n8-session
```

Expected: `# 8 passed, 0 skipped, 0 failed` for `n8/session-client`; `# 9 test file(s) passed (jobs=16)` for the n8
package's files and `# 15 test file(s) passed (jobs=16)` for risa's (its `n8-sync` drives the shared bridge); both
typechecks print only their `tsc -p tsconfig.json` line; `check-boundaries: the packages hold their boundaries (0
allowed by EXCEPTIONS, 0 warning(s))`; and `# 1 native test file(s) passed (jobs=16)`.

- [ ] **Step 9: commit**

```bash
git -C /workspaces/RetroPlug-main/cuttlefish add -- \
  packages/n8/ts/index.ts \
  packages/n8/ts/bridge.ts \
  packages/n8/ts/cli/n8Load.ts \
  packages/n8/ts/cli/n8Play.ts \
  packages/n8/ts/test/session-client.test.ts
git -C /workspaces/RetroPlug-main/cuttlefish commit -F - <<'MSG'
fix(sdk-n8): one pickN8Port, and refuse a romName that is a path

The three N8 tools each kept a private copy of the port picker, in two wordings, and the device a session script opens
needs the same one. @retroplug/n8 exports pickN8Port, worded as two of the copies were, and the bridge, n8-load and
n8-play use it.

The client's load() built usb-games/<romName> from any name, so one holding a separator or a dot directory uploaded
somewhere else on the card. It is refused before anything is sent, in loadRom's words, so n8-load and the SDK's device
share the guard.
MSG
```

---

## Task 3: an N8 session resyncs after a call that failed on the line

A session call that fails part-way can leave the line out of step with the protocol: the rest of a reply still
arriving after the call gave up on it (a listing streams record by record, a menu load can answer after its timeout),
or the N8 still inside a command, waiting for file data that never came. Nothing recovered from that: the next call
read the late bytes as its own reply. `N8Rpc::Session` gains a `dirty` flag, set by `settle` when a call's status says
it failed on the line (anything but ok, a file a listing did not hold, and the refusals made before the first byte).
The next call goes through `ready`, which drains the line until it has been quiet for one 20 ms read (20 reads at most,
about 0.4 s), drops what the OS holds and re-runs the handshake (0.3 s), and when the line does not answer the
handshake, answers that failure without sending its own command; the session stays dirty, so the call after tries
again. It sits in the facet, so `n8-load`, `n8-play`, the bridges and Task 4's device all recover from a hiccup. The
two calls that put nothing on the line (`n8FlushInput`, `n8SetReadTimeout`) keep `find`.

The resync changes what one existing case met: "a call refused before it reached the port is tooLarge in the
protocol's words, whatever the port said last" made the port's last call fail with a handshake on a dead port, which
now leaves the session dirty, so the refused call's resync answered for the port before the refusal could. Its port's
last words come from a failed flush instead (`FakeN8`'s `failOnFlushInput`), which is no session call on the line, so
the case still holds what it was written for.

**Files** (line numbers are the file at `fc2578945`):

- Modify: `packages/n8/native/src/n8/desktop/N8Rpc.cpp:17-24` (`Session`), `:43` (`leavesLineInStep`, before
  `notOpen`), `:129-133` (`ready` and `settle`, after `find`) and `:164-380` (every session method on the line).
- Modify: `packages/n8/native/src/n8/desktop/N8Rpc.hpp:27-29` (the header's paragraph), `:33` (`<string_view>`) and
  `:149` (the two private members).
- Test: `packages/n8/native/test/N8Provider.test.cpp:11-13` (`<algorithm>`, `<deque>`), `:457-470` (the refusal case)
  and `:470` (two port models and two cases at the end).

**Interfaces:**

- Consumes: `Edio::connect()`, `retroplug::PortTransport::read` / `flushInput` / `failure()`, the file's `replyOf`,
  `notOpen`, `codeName`; `FakeN8`'s `failOnFlushInput` and `FakeN8Port` (test).
- Produces: `N8Rpc::Session* N8Rpc::ready(std::int32_t handle, N8Reply& refused)` and `static N8Reply
  N8Rpc::settle(Session&, const Status&, std::string_view path = {})`, private; `bool Session::dirty`; the file-local
  `bool leavesLineInStep(const Status&)`. No reply's shape changes; a call after a failure may now answer the
  handshake's failure (`linkDown`, `timeout` or `badReply`) instead of its own.

### Steps

- [ ] **Step 1: write the failing cases**

`LinePort` models a line: the N8 answers a command only once it is written, a flush drops only what has arrived, and
late bytes arrive once the line has gone quiet, ahead of anything asked for after them. `DeadPort` dies after its
handshake: every read fails in the port, in its words, and every write is taken and recorded.

```patch
diff --git a/packages/n8/native/test/N8Provider.test.cpp b/packages/n8/native/test/N8Provider.test.cpp
index 426c7f84d..c7ace3505 100644
--- a/packages/n8/native/test/N8Provider.test.cpp
+++ b/packages/n8/native/test/N8Provider.test.cpp
@@ -8,8 +8,10 @@
 
 #include <catch2/catch_test_macros.hpp>
 
+#include <algorithm>
 #include <cstddef>
 #include <cstdint>
+#include <deque>
 #include <filesystem>
 #include <fstream>
 #include <memory>
@@ -468,3 +470,127 @@ TEST_CASE("a call refused before it reached the port is tooLarge in the protocol
     CHECK(refused.status == "tooLarge");
     CHECK(refused.message.rfind("Edio: a string longer than 65535 bytes (", 0) == 0);
 }
+
+namespace {
+// A port that behaves as a line does, so a call that gave up part-way leaves it out of step: the N8 answers a command
+// only once it is written (CMD_STATUS with <0> A5, CMD_MEM_RD with `nextMemRd`), a flush drops only what has arrived,
+// and `late` bytes - the rest of a reply the caller gave up on - arrive once the line has gone quiet (the caller's own
+// timeout), ahead of anything asked for after them.
+struct LinePort : retroplug::PortTransport {
+    std::deque<std::uint8_t>  arrived;
+    std::vector<std::uint8_t> late, nextMemRd;
+    bool                      wentQuiet = false;
+
+    std::size_t write(const std::uint8_t* data, std::size_t size) override {
+        const int cmd = size >= 4 && data[0] == 0x2B ? data[2] : -1;
+        if (cmd == rp::n8::Edio::CMD_STATUS) arrived.insert(arrived.end(), {0x00, 0xA5});
+        if (cmd == rp::n8::Edio::CMD_MEM_RD) {
+            arrived.insert(arrived.end(), nextMemRd.begin(), nextMemRd.end());
+            nextMemRd.clear();
+        }
+        return size;
+    }
+    std::size_t read(std::uint8_t* buffer, std::size_t size, std::uint32_t) override {
+        if (wentQuiet && !late.empty()) {
+            arrived.insert(arrived.begin(), late.begin(), late.end());
+            late.clear();
+        }
+        if (arrived.empty()) {
+            wentQuiet = wentQuiet || !late.empty();
+            return 0;
+        }
+        std::size_t n = 0;
+        for (; n < size && !arrived.empty(); ++n) {
+            buffer[n] = arrived.front();
+            arrived.pop_front();
+        }
+        return n;
+    }
+    void flushInput() override { arrived.clear(); }
+};
+
+// A port that died after its handshake: every write is taken (a write to a gone device can still appear to go out),
+// every read fails in the port, in its words, and what the session wrote is kept, one command a write.
+struct DeadPort : retroplug::PortTransport {
+    bool                      dead = false;
+    std::vector<std::uint8_t> commands;  // each write's command byte
+    std::deque<std::uint8_t>  arrived;
+    std::string               failed;
+
+    std::size_t write(const std::uint8_t* data, std::size_t size) override {
+        failed.clear();
+        if (size >= 4 && data[0] == 0x2B) commands.push_back(data[2]);
+        if (!dead && size >= 4 && data[0] == 0x2B && data[2] == rp::n8::Edio::CMD_STATUS)
+            arrived.insert(arrived.end(), {0x00, 0xA5});
+        return size;
+    }
+    std::size_t read(std::uint8_t* buffer, std::size_t size, std::uint32_t) override {
+        failed = dead ? "read failed: Input/output error" : "";
+        std::size_t n = 0;
+        for (; !dead && n < size && !arrived.empty(); ++n) {
+            buffer[n] = arrived.front();
+            arrived.pop_front();
+        }
+        return n;
+    }
+    void        flushInput() override { arrived.clear(); }
+    std::string failure() const override { return failed; }
+};
+}  // namespace
+
+TEST_CASE("the call after one that failed part-way drains what was still arriving and handshakes before it runs",
+          "[n8-rpc]") {
+    FakeN8        dev;
+    N8Host        host(factoryFor(dev), twoPorts(), cleanDir("rp-n8-rpc-test").string());
+    LinePort*     line = nullptr;
+    rp::n8::N8Rpc rpc(host, [&line](const std::string&) -> std::unique_ptr<retroplug::PortTransport> {
+        auto port = std::make_unique<LinePort>();
+        line      = port.get();
+        return port;
+    }, twoPorts());
+    const auto h = static_cast<std::int32_t>(rpc.n8Open("/dev/ttyACM0").value);
+    REQUIRE(rpc.n8Handshake(h, 0).status == "ok");
+    const auto ssr = static_cast<std::uint32_t>(rp::n8::Edio::ADDR_SSR);
+
+    // A sniff that gets 10 of its 512 bytes and gives up; the other 502 arrive after it did.
+    line->nextMemRd.assign(10, 0x11);
+    line->late.assign(502, 0x11);
+    CHECK(rpc.n8MemRD(h, ssr, 512).status == "timeout");
+
+    // The next call's own reply. With nothing in between, the late bytes come first and are taken for it.
+    line->nextMemRd.assign(512, 0x22);
+    const rp::n8::N8Reply next  = rpc.n8MemRD(h, ssr, 512);
+    const auto            bytes = vec(next.bytes);
+    CHECK(next.status == "ok");
+    CHECK(std::count(bytes.begin(), bytes.end(), 0x22) == 512);
+}
+
+TEST_CASE("on a dead port the call after a failure answers the handshake's failure in the port's words and is not sent",
+          "[n8-rpc]") {
+    FakeN8        dev;
+    N8Host        host(factoryFor(dev), twoPorts(), cleanDir("rp-n8-rpc-test").string());
+    DeadPort*     port = nullptr;
+    rp::n8::N8Rpc rpc(host, [&port](const std::string&) -> std::unique_ptr<retroplug::PortTransport> {
+        auto p = std::make_unique<DeadPort>();
+        port   = p.get();
+        return p;
+    }, twoPorts());
+    const auto h = static_cast<std::int32_t>(rpc.n8Open("/dev/ttyACM0").value);
+    REQUIRE(rpc.n8Handshake(h, 0).status == "ok");
+    const auto ssr = static_cast<std::uint32_t>(rp::n8::Edio::ADDR_SSR);
+
+    port->dead = true;
+    port->commands.clear();
+    const rp::n8::N8Reply first = rpc.n8MemRD(h, ssr, 512);
+    CHECK(first.status == "linkDown");
+    CHECK(first.message == "read failed: Input/output error");
+    // Each call after it re-runs the handshake, which a dead port cannot answer: the call answers that failure, in the
+    // port's words, and its own command never goes out onto a line that did not answer.
+    for (int i = 0; i < 2; ++i) {
+        const rp::n8::N8Reply again = rpc.n8MemRD(h, ssr, 512);
+        CHECK(again.status == "linkDown");
+        CHECK(again.message == "read failed: Input/output error");
+    }
+    CHECK(port->commands == std::vector<std::uint8_t>{rp::n8::Edio::CMD_MEM_RD, rp::n8::Edio::CMD_STATUS,
+                                                      rp::n8::Edio::CMD_STATUS});
+}
```

- [ ] **Step 2: build it and watch both fail**

```bash
cmake --build /workspaces/RetroPlug-main/cuttlefish/build --target retroplug-n8-test -j$(nproc)
/workspaces/RetroPlug-main/cuttlefish/.superpowers/sdkn8/guarded.sh \
  /workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-n8-test "[n8-rpc]"
```

Expected: exit 42. The dead port's next calls are sent onto the line that failed, and the late bytes are taken for the
next sniff's reply:

```
-------------------------------------------------------------------------------
on a dead port the call after a failure answers the handshake's failure in the
port's words and is not sent
-------------------------------------------------------------------------------
/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8Provider.test.cpp:568
...............................................................................

/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8Provider.test.cpp:594: FAILED:
  CHECK( port->commands == std::vector<std::uint8_t>{rp::n8::Edio::CMD_MEM_RD, rp::n8::Edio::CMD_STATUS, rp::n8::Edio::CMD_STATUS} )
with expansion:
  { 25, 25, 25 } == { 25, 16, 16 }

-------------------------------------------------------------------------------
the call after one that failed part-way drains what was still arriving and
handshakes before it runs
-------------------------------------------------------------------------------
/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8Provider.test.cpp:541
...............................................................................

/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8Provider.test.cpp:565: FAILED:
  CHECK( std::count(bytes.begin(), bytes.end(), 0x22) == 512 )
with expansion:
  10 == 512 (0x200)

===============================================================================
test cases:  11 |   9 passed | 2 failed
assertions: 113 | 111 passed | 2 failed
```

- [ ] **Step 3: mark a session dirty on a failure, and resync it before its next call**

```patch
diff --git a/packages/n8/native/src/n8/desktop/N8Rpc.cpp b/packages/n8/native/src/n8/desktop/N8Rpc.cpp
index f720b5407..d355e5fde 100644
--- a/packages/n8/native/src/n8/desktop/N8Rpc.cpp
+++ b/packages/n8/native/src/n8/desktop/N8Rpc.cpp
@@ -21,6 +21,10 @@ struct N8Rpc::Session {
     std::string                               portName;
     std::unique_ptr<retroplug::PortTransport> port;  // before edio, which holds a reference to it
     Edio                                      edio;
+    // A call failed on the line, which may have left it out of step with the protocol: the rest of a reply still
+    // arriving, or the N8 still inside a command. The next call resyncs first (N8Rpc::ready), and only a handshake that
+    // answers clears it.
+    bool dirty = false;
 };
 
 namespace {
@@ -40,6 +44,15 @@ N8Reply replyOf(const Status& status, const retroplug::PortTransport& port, std:
     return r;
 }
 
+// Whether a call that answered `status` left the line as it found it: it succeeded, it was refused before its first byte
+// (the refusals portSaid names, and a file larger than its buffer, refused after a listing that ended in step), or its
+// file was not on a listing that ended in step. Anything else may have stopped part-way through a reply or a command.
+bool leavesLineInStep(const Status& status) {
+    if (status.ok() || status.code == Code::NotFound) return true;
+    return status.code == Code::TooLarge && (status.where == Where::Length || status.where == Where::BatteryPath ||
+                                             status.where == Where::Size || status.where == Where::Buffer);
+}
+
 N8Reply notOpen(std::int32_t handle) {
     N8Reply r;
     r.status  = codeName(Code::LinkDown);
@@ -131,6 +144,36 @@ N8Rpc::Session* N8Rpc::find(std::int32_t handle) {
     return it == sessions_.end() ? nullptr : it->second.get();
 }
 
+N8Rpc::Session* N8Rpc::ready(std::int32_t handle, N8Reply& refused) {
+    Session* s = find(handle);
+    if (!s) {
+        refused = notOpen(handle);
+        return nullptr;
+    }
+    if (!s->dirty) return s;
+    // Read until the line has been quiet for one read (20 ms; 20 reads, about 0.4 s, at most), then drop what the OS
+    // holds: the rest of a reply the caller gave up on can still be arriving, record by record. A read the port fails
+    // ends it too, and the handshake below meets that failure in the port's words.
+    std::uint8_t sink[4096];
+    for (int i = 0; i < 20 && s->port->read(sink, sizeof sink, 20) > 0; ++i) {
+    }
+    s->port->flushInput();
+    // A drain cannot tell a line in step from one where the N8 is still inside a command (a file write waiting for
+    // data that never came); the handshake can, since only a line in step answers it.
+    const Status st = s->edio.connect();
+    if (!st.ok()) {
+        refused = replyOf(st, *s->port);
+        return nullptr;
+    }
+    s->dirty = false;
+    return s;
+}
+
+N8Reply N8Rpc::settle(Session& session, const Status& status, std::string_view path) {
+    if (!leavesLineInStep(status)) session.dirty = true;
+    return replyOf(status, *session.port, path);
+}
+
 std::vector<retroplug::N8PortDto> N8Rpc::n8ListPorts() {
     return lister_ ? lister_() : std::vector<retroplug::N8PortDto>{};
 }
@@ -162,130 +205,147 @@ N8Reply N8Rpc::n8Open(std::string port) {
 bool N8Rpc::n8Close(std::int32_t handle) { return sessions_.erase(handle) > 0; }
 
 N8Reply N8Rpc::n8Handshake(std::int32_t handle, std::uint32_t timeoutMs) {
-    Session* s = find(handle);
-    if (!s) return notOpen(handle);
+    N8Reply  r;
+    Session* s = ready(handle, r);
+    if (!s) return r;
     std::uint8_t code = 0;
-    N8Reply      r    = replyOf(s->edio.connect(&code, timeoutMs), *s->port);
+    r                 = settle(*s, s->edio.connect(&code, timeoutMs));
     r.value           = code;
     return r;
 }
 
 N8Reply N8Rpc::n8Status(std::int32_t handle) {
-    Session* s = find(handle);
-    if (!s) return notOpen(handle);
+    N8Reply  r;
+    Session* s = ready(handle, r);
+    if (!s) return r;
     std::uint8_t code = 0;
-    N8Reply      r    = replyOf(s->edio.status(code), *s->port);
+    r                 = settle(*s, s->edio.status(code));
     r.value           = code;
     return r;
 }
 
 N8Reply N8Rpc::n8SysInfo(std::int32_t handle) {
-    Session* s = find(handle);
-    if (!s) return notOpen(handle);
+    N8Reply  r;
+    Session* s = ready(handle, r);
+    if (!s) return r;
     std::uint8_t info[Edio::SIZE_SYS_INFO];
     const Status st = s->edio.sysInfo(info);
-    N8Reply      r  = replyOf(st, *s->port);
+    r               = settle(*s, st);
     if (st.ok()) r.bytes = bytesOf(info, sizeof info);
     return r;
 }
 
 N8Reply N8Rpc::n8Vdc(std::int32_t handle) {
-    Session* s = find(handle);
-    if (!s) return notOpen(handle);
+    N8Reply  r;
+    Session* s = ready(handle, r);
+    if (!s) return r;
     std::uint8_t vdc[Edio::SIZE_VDC];
     const Status st = s->edio.vdc(vdc);
-    N8Reply      r  = replyOf(st, *s->port);
+    r               = settle(*s, st);
     if (st.ok()) r.bytes = bytesOf(vdc, sizeof vdc);
     return r;
 }
 
 N8Reply N8Rpc::n8FreeSpace(std::int32_t handle) {
-    Session* s = find(handle);
-    if (!s) return notOpen(handle);
+    N8Reply  r;
+    Session* s = ready(handle, r);
+    if (!s) return r;
     std::uint64_t bytesFree = 0;
-    N8Reply       r         = replyOf(s->edio.freeSpace(bytesFree), *s->port);
+    r                       = settle(*s, s->edio.freeSpace(bytesFree));
     r.value                 = bytesFree;
     return r;
 }
 
 N8Reply N8Rpc::n8DirMake(std::int32_t handle, std::string path) {
-    Session* s = find(handle);
-    return s ? replyOf(s->edio.dirMake(path), *s->port, path) : notOpen(handle);
+    N8Reply  r;
+    Session* s = ready(handle, r);
+    return s ? settle(*s, s->edio.dirMake(path), path) : r;
 }
 
 N8Reply N8Rpc::n8FileDelete(std::int32_t handle, std::string path) {
-    Session* s = find(handle);
-    return s ? replyOf(s->edio.fileDelete(path), *s->port, path) : notOpen(handle);
+    N8Reply  r;
+    Session* s = ready(handle, r);
+    return s ? settle(*s, s->edio.fileDelete(path), path) : r;
 }
 
 N8Reply N8Rpc::n8MemRD(std::int32_t handle, std::uint32_t addr, std::uint32_t size) {
-    Session* s = find(handle);
-    if (!s) return notOpen(handle);
+    N8Reply  r;
+    Session* s = ready(handle, r);
+    if (!s) return r;
     std::vector<std::uint8_t> buf(size);
     const Status              st = s->edio.memRD(addr, buf.data(), buf.size());
-    N8Reply                   r  = replyOf(st, *s->port);
+    r                            = settle(*s, st);
     if (st.ok()) r.bytes = bytesOf(buf.data(), buf.size());
     return r;
 }
 
 N8Reply N8Rpc::n8MemWR(std::int32_t handle, std::uint32_t addr, rfl::Bytestring data) {
-    Session* s = find(handle);
-    return s ? replyOf(s->edio.memWR(addr, dataOf(data), data.size()), *s->port) : notOpen(handle);
+    N8Reply  r;
+    Session* s = ready(handle, r);
+    return s ? settle(*s, s->edio.memWR(addr, dataOf(data), data.size())) : r;
 }
 
 N8Reply N8Rpc::n8FifoWR(std::int32_t handle, rfl::Bytestring data) {
-    Session* s = find(handle);
-    return s ? replyOf(s->edio.fifoWR(dataOf(data), data.size()), *s->port) : notOpen(handle);
+    N8Reply  r;
+    Session* s = ready(handle, r);
+    return s ? settle(*s, s->edio.fifoWR(dataOf(data), data.size())) : r;
 }
 
 N8Reply N8Rpc::n8FifoTxString(std::int32_t handle, std::string text) {
-    Session* s = find(handle);
-    return s ? replyOf(s->edio.fifoTxString(text), *s->port, text) : notOpen(handle);
+    N8Reply  r;
+    Session* s = ready(handle, r);
+    return s ? settle(*s, s->edio.fifoTxString(text), text) : r;
 }
 
 N8Reply N8Rpc::n8ListDir(std::int32_t handle, std::string path) {
-    Session* s = find(handle);
-    if (!s) return notOpen(handle);
+    N8Reply  r;
+    Session* s = ready(handle, r);
+    if (!s) return r;
     std::vector<N8DirEntryDto> entries;
     const Status               st = s->edio.listDir(path, [&](const DirEntry& e) {
         entries.push_back({wellFormed(e.name), e.size, e.isDir});
     });
-    N8Reply r = replyOf(st, *s->port, path);
+    r = settle(*s, st, path);
     if (st.ok()) r.entries = std::move(entries);
     return r;
 }
 
 N8Reply N8Rpc::n8FileOpen(std::int32_t handle, std::string path, std::int32_t mode) {
-    Session* s = find(handle);
-    return s ? replyOf(s->edio.fileOpen(path, static_cast<std::uint8_t>(mode)), *s->port, path) : notOpen(handle);
+    N8Reply  r;
+    Session* s = ready(handle, r);
+    return s ? settle(*s, s->edio.fileOpen(path, static_cast<std::uint8_t>(mode)), path) : r;
 }
 
 N8Reply N8Rpc::n8FileRead(std::int32_t handle, std::uint32_t size) {
-    Session* s = find(handle);
-    if (!s) return notOpen(handle);
+    N8Reply  r;
+    Session* s = ready(handle, r);
+    if (!s) return r;
     std::vector<std::uint8_t> buf(size);
     const Status              st = s->edio.fileRead(buf.data(), buf.size());
-    N8Reply                   r  = replyOf(st, *s->port);
+    r                            = settle(*s, st);
     if (st.ok()) r.bytes = bytesOf(buf.data(), buf.size());
     return r;
 }
 
 N8Reply N8Rpc::n8FileWrite(std::int32_t handle, rfl::Bytestring data) {
-    Session* s = find(handle);
-    return s ? replyOf(s->edio.fileWrite(dataOf(data), data.size()), *s->port) : notOpen(handle);
+    N8Reply  r;
+    Session* s = ready(handle, r);
+    return s ? settle(*s, s->edio.fileWrite(dataOf(data), data.size())) : r;
 }
 
 N8Reply N8Rpc::n8FileClose(std::int32_t handle) {
-    Session* s = find(handle);
-    return s ? replyOf(s->edio.fileClose(), *s->port) : notOpen(handle);
+    N8Reply  r;
+    Session* s = ready(handle, r);
+    return s ? settle(*s, s->edio.fileClose()) : r;
 }
 
 N8Reply N8Rpc::n8ReadData(std::int32_t handle, std::uint32_t size, std::uint32_t timeoutMs) {
-    Session* s = find(handle);
-    if (!s) return notOpen(handle);
+    N8Reply  r;
+    Session* s = ready(handle, r);
+    if (!s) return r;
     std::vector<std::uint8_t> buf(size);
     const Status              st = s->edio.readData(buf.data(), buf.size(), timeoutMs);
-    N8Reply                   r  = replyOf(st, *s->port);
+    r                            = settle(*s, st);
     if (st.ok()) r.bytes = bytesOf(buf.data(), buf.size());
     return r;
 }
@@ -305,31 +365,35 @@ N8Reply N8Rpc::n8SetReadTimeout(std::int32_t handle, std::uint32_t ms) {
 }
 
 N8Reply N8Rpc::n8MenuTest(std::int32_t handle) {
-    Session* s = find(handle);
-    return s ? replyOf(Menu(s->edio).test(), *s->port) : notOpen(handle);
+    N8Reply  r;
+    Session* s = ready(handle, r);
+    return s ? settle(*s, Menu(s->edio).test()) : r;
 }
 
 N8Reply N8Rpc::n8MenuInstall(std::int32_t handle, std::string path) {
-    Session* s = find(handle);
-    if (!s) return notOpen(handle);
+    N8Reply  r;
+    Session* s = ready(handle, r);
+    if (!s) return r;
     std::uint16_t index = 0;
-    N8Reply       r     = replyOf(Menu(s->edio).install(path, &index), *s->port, path);
+    r                   = settle(*s, Menu(s->edio).install(path, &index), path);
     r.value             = index;
     return r;
 }
 
 N8Reply N8Rpc::n8MenuStart(std::int32_t handle) {
-    Session* s = find(handle);
-    return s ? replyOf(Menu(s->edio).start(), *s->port) : notOpen(handle);
+    N8Reply  r;
+    Session* s = ready(handle, r);
+    return s ? settle(*s, Menu(s->edio).start()) : r;
 }
 
 N8Reply N8Rpc::n8MenuVramDump(std::int32_t handle) {
-    Session* s = find(handle);
-    if (!s) return notOpen(handle);
+    N8Reply  r;
+    Session* s = ready(handle, r);
+    if (!s) return r;
     std::uint8_t vram[Menu::kVramBytes];
     std::uint8_t palette[Menu::kPaletteBytes];
     const Status st = Menu(s->edio).vramDump(vram, palette);
-    N8Reply      r  = replyOf(st, *s->port);
+    r               = settle(*s, st);
     if (st.ok()) {
         r.bytes = bytesOf(vram, sizeof vram);
         const rfl::Bytestring tail = bytesOf(palette, sizeof palette);
@@ -339,13 +403,15 @@ N8Reply N8Rpc::n8MenuVramDump(std::int32_t handle) {
 }
 
 N8Reply N8Rpc::n8MenuReset(std::int32_t handle) {
-    Session* s = find(handle);
-    return s ? replyOf(Menu(s->edio).reset(), *s->port) : notOpen(handle);
+    N8Reply  r;
+    Session* s = ready(handle, r);
+    return s ? settle(*s, Menu(s->edio).reset()) : r;
 }
 
 N8Reply N8Rpc::n8ReadFile(std::int32_t handle, std::string path) {
-    Session* s = find(handle);
-    if (!s) return notOpen(handle);
+    N8Reply  r;
+    Session* s = ready(handle, r);
+    if (!s) return r;
     std::vector<std::uint8_t> buf;
     const FileSink            sink{[](void* ctx, std::size_t size) {
                             auto& b = *static_cast<std::vector<std::uint8_t>*>(ctx);
@@ -355,24 +421,26 @@ N8Reply N8Rpc::n8ReadFile(std::int32_t handle, std::string path) {
                         &buf};
     std::size_t  size = 0;
     const Status st   = readFile(s->edio, path, sink, &size);
-    N8Reply      r    = replyOf(st, *s->port, path);
+    r                 = settle(*s, st, path);
     if (st.ok()) r.bytes = bytesOf(buf.data(), size);
     return r;
 }
 
 N8Reply N8Rpc::n8WriteFile(std::int32_t handle, std::string path, rfl::Bytestring data) {
-    Session* s = find(handle);
-    return s ? replyOf(writeFile(s->edio, path, ByteView{dataOf(data), data.size()}), *s->port, path) : notOpen(handle);
+    N8Reply  r;
+    Session* s = ready(handle, r);
+    return s ? settle(*s, writeFile(s->edio, path, ByteView{dataOf(data), data.size()}), path) : r;
 }
 
 N8Reply N8Rpc::n8BootRom(std::int32_t handle, std::string path, rfl::Bytestring rom, rfl::Bytestring srm,
                          bool upload) {
-    Session* s = find(handle);
-    if (!s) return notOpen(handle);
+    N8Reply  r;
+    Session* s = ready(handle, r);
+    if (!s) return r;
     std::uint16_t index = 0;
     const Status  st    = bootRom(s->edio, path, ByteView{dataOf(rom), rom.size()}, ByteView{dataOf(srm), srm.size()},
                                   upload ? Upload::Always : Upload::Never, &index);
-    N8Reply       r     = replyOf(st, *s->port, path);
+    r                   = settle(*s, st, path);
     r.value             = index;
     return r;
 }
diff --git a/packages/n8/native/src/n8/desktop/N8Rpc.hpp b/packages/n8/native/src/n8/desktop/N8Rpc.hpp
index adb14f793..2c29c65a5 100644
--- a/packages/n8/native/src/n8/desktop/N8Rpc.hpp
+++ b/packages/n8/native/src/n8/desktop/N8Rpc.hpp
@@ -26,11 +26,18 @@
 // it opens it, so a second open fails busy, and a COM port is exclusive on Windows already. Every session call answers
 // an N8Reply. A link that failed keeps its port, so it stays exclusive, until it is disconnected or reconnected, and
 // an instance whose restore was refused as busy stays down until Connect.
+//
+// A session call that failed on the line (not one refused before its first byte, nor a file a listing did not hold)
+// marks its session dirty, since it can have stopped part-way: the rest of a reply still arriving, or the N8 still
+// inside a command. The next call on it first drains the line until it is quiet (about 0.4 s at most) and re-runs the
+// handshake (0.3 s), and answers the handshake's failure, sending nothing of its own, when the line does not answer.
+// So every session (n8-load, n8-play, the bridges, a script's device) recovers from a hiccup on the next call.
 
 #include <cstdint>
 #include <map>
 #include <memory>
 #include <string>
+#include <string_view>
 #include <vector>
 
 #include <rfl/Bytestring.hpp>
@@ -147,6 +154,13 @@ public:
 private:
     struct Session;
     Session* find(std::int32_t handle);
+    // The session `handle` names, ready for a call: when a call before it failed on the line, what was still arriving
+    // is drained and the handshake re-run first. Null, with `refused` set to what the call answers instead, for a
+    // handle that is not open or a line that did not answer the handshake (the session stays dirty, so the next call
+    // tries again, and this call's own command is never sent).
+    Session* ready(std::int32_t handle, N8Reply& refused);
+    // A call's reply, its session marked dirty when the call failed on the line.
+    static N8Reply settle(Session& session, const Status& status, std::string_view path = {});
 
     retroplug::N8Host&                                   host_;
     retroplug::N8Link::PortFactory                       factory_;
```

- [ ] **Step 4: rebuild and run the facet's cases**

Same commands as Step 2. Expected: both new cases pass, and the refusal case now meets the resync (exit 42):

```
-------------------------------------------------------------------------------
a call refused before it reached the port is tooLarge in the protocol's words,
whatever the port said last
-------------------------------------------------------------------------------
/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8Provider.test.cpp:459
...............................................................................

/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8Provider.test.cpp:470: FAILED:
  CHECK( refused.status == "tooLarge" )
with expansion:
  "linkDown" == "tooLarge"

/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8Provider.test.cpp:471: FAILED:
  CHECK( refused.message.rfind("Edio: a string longer than 65535 bytes (", 0) == 0 )
with expansion:
  18446744073709551615 (0xffffffffffffffff)
  ==
  0

===============================================================================
test cases:  11 |  10 passed | 1 failed
assertions: 113 | 111 passed | 2 failed
```

- [ ] **Step 5: give the refusal case its port's last words without a failed call on the line**

```patch
diff --git a/packages/n8/native/test/N8Provider.test.cpp b/packages/n8/native/test/N8Provider.test.cpp
index c7ace3505..77abc3890 100644
--- a/packages/n8/native/test/N8Provider.test.cpp
+++ b/packages/n8/native/test/N8Provider.test.cpp
@@ -458,13 +458,21 @@ TEST_CASE("portSaid is empty for every refusal made before the port, and the por
 
 TEST_CASE("a call refused before it reached the port is tooLarge in the protocol's words, whatever the port said last",
           "[n8-rpc]") {
-    FakeN8        dev;
-    N8Host        host(factoryFor(dev), twoPorts(), cleanDir("rp-n8-rpc-test").string());
-    rp::n8::N8Rpc rpc(host, [](const std::string&) -> std::unique_ptr<retroplug::PortTransport> {
-        return std::make_unique<GonePort>();
+    FakeN8                       dev;
+    N8Host                       host(factoryFor(dev), twoPorts(), cleanDir("rp-n8-rpc-test").string());
+    retroplug::test::FakeN8Port* port = nullptr;
+    rp::n8::N8Rpc rpc(host, [&](const std::string&) -> std::unique_ptr<retroplug::PortTransport> {
+        auto p = std::make_unique<retroplug::test::FakeN8Port>(dev);
+        port   = p.get();
+        return p;
     }, twoPorts());
     const auto h = static_cast<std::int32_t>(rpc.n8Open("/dev/ttyACM0").value);
-    REQUIRE(rpc.n8Handshake(h, 0).status == "linkDown");  // the port's last call failed, and says so still
+    REQUIRE(rpc.n8Handshake(h, 0).status == "ok");
+    // The port's last call failed, and says so still. A flush, not a failed call on the line: that would leave the
+    // session dirty, and the refused call's resync would then answer for the port before the refusal could.
+    dev.failOnFlushInput = 1;
+    REQUIRE(rpc.n8FlushInput(h).status == "ok");
+    REQUIRE(port->failure() == "fake port: flushInput failed");
 
     const auto refused = rpc.n8MenuInstall(h, std::string(0x10000, 'x'));
     CHECK(refused.status == "tooLarge");
```

- [ ] **Step 6: rebuild and run the facet's cases and the binary**

```bash
cmake --build /workspaces/RetroPlug-main/cuttlefish/build --target retroplug-n8-test -j$(nproc)
/workspaces/RetroPlug-main/cuttlefish/.superpowers/sdkn8/guarded.sh \
  /workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-n8-test "[n8-rpc]"
/workspaces/RetroPlug-main/cuttlefish/.superpowers/sdkn8/guarded.sh \
  /workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-n8-test
```

Expected:

```
===============================================================================
All tests passed (115 assertions in 11 test cases)
```

```
All tests passed (1490 assertions in 108 test cases)
```

(Each new case guards its half: with the drain loop's bound set to 0 the late-bytes case fails `"badReply" == "ok"`
and `0 == 512`, the handshake reading the late bytes; with a failed handshake not refusing the call the dead-port case
sees `{ 25, 16, 25, 16, 25 }`; and with a failed handshake clearing `dirty` it sees `{ 25, 16, 25 }`. Checked while
this plan was written; not a step.)

- [ ] **Step 7: the wider checks this reaches**

```bash
/workspaces/RetroPlug-main/cuttlefish/.superpowers/sdkn8/guarded.sh pnpm -C /workspaces/RetroPlug-main/cuttlefish test:n8
/workspaces/RetroPlug-main/cuttlefish/.superpowers/sdkn8/guarded.sh \
  pnpm -C /workspaces/RetroPlug-main/cuttlefish test:native n8-session
/workspaces/RetroPlug-main/cuttlefish/.superpowers/sdkn8/guarded.sh \
  pnpm -C /workspaces/RetroPlug-main/cuttlefish test:native nes-core-bytes
```

Expected: `All tests passed (1490 assertions in 108 test cases)` and `All tests passed (17 assertions in 2 test
cases)`; `# 5 passed, 0 skipped, 0 failed` for `n8-session` (its running-game `menu.test()` timeout now leaves the
session dirty, and the `writeSramDirect` after it resyncs first); `# 10 passed, 0 skipped, 0 failed` for
`nes-core-bytes`.

- [ ] **Step 8: commit**

```bash
git -C /workspaces/RetroPlug-main/cuttlefish add -- \
  packages/n8/native/src/n8/desktop/N8Rpc.cpp \
  packages/n8/native/src/n8/desktop/N8Rpc.hpp \
  packages/n8/native/test/N8Provider.test.cpp
git -C /workspaces/RetroPlug-main/cuttlefish commit -F - <<'MSG'
fix(sdk-n8): resync an N8 session after a call that failed on the line

A session call that fails part-way can leave the line out of step: the rest of a reply still arriving after the call
gave up on it, or the N8 still inside a command waiting for data that never came. Nothing recovered from that, so the
next call read the late bytes as its own reply. A failed call (not one refused before its first byte, nor a file a
listing did not hold) now marks its session dirty, and the next call first drains the line until it is quiet (about
0.4 s at most) and re-runs the handshake (0.3 s); when the line does not answer, the call answers the handshake's
failure without sending itself, and the session stays dirty. It is in the facet, so n8-load, n8-play, the bridges and
a script's device all recover.

The case holding a refusal to the protocol's words made its port's last call fail with a handshake on a dead port,
which the resync now answers for; a failed flush, which is no call on the line, gives the port the same last words.
Two cases model a line with late bytes and a dead port, each red without the half it guards.
MSG
```

---

## Task 4: the N8 device a session script opens, `@retroplug/n8/device`

The SDK's `connectN8` (Task 6) needs a device that belongs to the n8 pack and runs over the facet, since the Edio
exists once, in C++. `packages/n8/ts/device.ts` is `openN8Device(call, opts)`: the explicit port or the first N8 the
host lists, the handshake (the session closed again if it throws), and the branch's surface mapped onto the client's
session: `listDir` (with `/` meaning the card's root, `""`, and each entry its three fields), `readFile`, `writeFile`,
`makeDir`, `deleteFile`, `load`, `sniffRaw` (the 512-byte sniffer region), `writeFifo`, `close` and `port`. A closed
device refuses every call in its own words ("N8Device.<method>: the device is closed"), which also covers the zero-byte
writes the client never sends: without it such a call after `close()` would "succeed" doing nothing. The package's
`exports` gains `./device`.

**Files** (line numbers are the file at `fc2578945`):

- Create: `packages/n8/ts/device.ts`.
- Modify: `packages/n8/package.json:18` (the `./device` entry).
- Test: `packages/n8/ts/test/device.test.ts` (new).

**Interfaces:**

- Consumes: `createN8Client`, `pickN8Port` (Task 2), `ADDR_SSR`, `SNIFFER_REGION_SIZE`, `N8Call`, `N8DirEntry` and
  `N8Port` from `./index`; `LoadOptions` from `./load`.
- Produces, from `@retroplug/n8/device`: `interface N8ConnectOptions { serial?: string; timeoutMs?: number }`,
  `type N8LoadOptions = LoadOptions`, the re-exported `N8DirEntry`, `interface N8Device { readonly port: string;
  load(opts: N8LoadOptions): { bootPath: string; mapIndex: number }; listDir(path: string): N8DirEntry[];
  readFile(path: string): Uint8Array; writeFile(path: string, bytes: Uint8Array): void; makeDir(path: string): void;
  deleteFile(path: string): void; sniffRaw(): Uint8Array; writeFifo(bytes: Uint8Array): void; close(): void }` and
  `function openN8Device(call: N8Call, opts?: N8ConnectOptions): N8Device`. Task 6 re-exports the types and wraps the
  function.

### Steps

- [ ] **Step 1: write the failing test**

```patch
diff --git a/packages/n8/ts/test/device.test.ts b/packages/n8/ts/test/device.test.ts
new file mode 100644
index 000000000..cabc3f900
--- /dev/null
+++ b/packages/n8/ts/test/device.test.ts
@@ -0,0 +1,121 @@
+// The N8 a session script opens (@retroplug/n8/device, packages/n8/ts/device.ts: the SDK's connectN8), over a fake RPC
+// call: the port it picks and the handshake it runs, the session it closes when the handshake fails, the card and cart
+// calls it maps onto the session's, and a closed device refusing every call in its own words, the zero-byte writes the
+// client never sends included. The facet end to end, against the fake cart, is test:native's n8-sdk.
+import { test, expect } from "@retroplug/testing";
+import { openN8Device } from "../device";
+import { ADDR_SSR, SNIFFER_REGION_SIZE, type N8Call, type N8Port } from "../index";
+
+interface Reply {
+  status: string;
+  deviceStatus: number;
+  message: string;
+  bytes?: Uint8Array;
+  value?: number;
+  entries?: { name: string; size: number; isDir: boolean; extra?: number }[];
+}
+const okReply = (more: Partial<Reply> = {}): Reply => ({ status: "ok", deviceStatus: 0, message: "", ...more });
+
+// A host listing `ports` that opens every session as handle 3 and answers each method from `answers` (a reply, or a
+// function of the call's params), recording every call.
+function fakeHost(
+  answers: Record<string, Reply | ((...params: unknown[]) => Reply)>,
+  ports: N8Port[] = [{ port: "/dev/ttyACM0", isN8: true }],
+): { call: N8Call; sent: unknown[][] } {
+  const sent: unknown[][] = [];
+  const call: N8Call = (method, ...params) => {
+    sent.push([method, ...params]);
+    if (method === "n8ListPorts") return ports;
+    if (method === "n8Open") return okReply({ value: 3 });
+    if (method === "n8Close") return true;
+    const a = answers[method];
+    if (a === undefined) return okReply();
+    return typeof a === "function" ? a(...params) : a;
+  };
+  return { call, sent };
+}
+
+test("it opens the first N8 the host lists, or the port asked for, and runs the handshake", () => {
+  const ports = [
+    { port: "/dev/ttyS0", isN8: false },
+    { port: "/dev/ttyACM3", isN8: true },
+  ];
+  const found = fakeHost({}, ports);
+  expect(openN8Device(found.call).port).toBe("/dev/ttyACM3");
+  expect(found.sent).toEqual([["n8ListPorts"], ["n8Open", "/dev/ttyACM3"], ["n8Handshake", 3, 300]]);
+  const asked = fakeHost({}, ports);
+  expect(openN8Device(asked.call, { serial: "/dev/ttyS0", timeoutMs: 1000 }).port).toBe("/dev/ttyS0");
+  expect(asked.sent).toEqual([["n8ListPorts"], ["n8Open", "/dev/ttyS0"], ["n8Handshake", 3, 1000]]);
+  expect(() => openN8Device(fakeHost({}, []).call)).toThrow("no Everdrive N8 Pro found (VID:PID 38df:0017)");
+});
+
+test("a handshake that is not answered closes the session and throws its failure", () => {
+  const unanswered: Reply = {
+    status: "timeout",
+    deviceStatus: 0,
+    message: "Edio: serial read timeout (no N8 response)",
+  };
+  const { call, sent } = fakeHost({ n8Handshake: unanswered });
+  expect(() => openN8Device(call)).toThrow("Edio: serial read timeout (no N8 response)");
+  expect(sent.map((s) => s[0])).toEqual(["n8ListPorts", "n8Open", "n8Handshake", "n8Close"]);
+});
+
+test("the card, the boot, the sniffer and the FIFO are the session's calls, and a listing is its three fields", () => {
+  const { call, sent } = fakeHost({
+    n8ListDir: okReply({
+      entries: [
+        { name: "a.raw", size: 4096, isDir: false, extra: 1 },
+        { name: "d", size: 0, isDir: true },
+      ],
+    }),
+    n8ReadFile: okReply({ bytes: new Uint8Array([1, 2]) }),
+    n8MemRD: (_h, _addr, size) => okReply({ bytes: new Uint8Array(size as number).fill(7) }),
+    n8BootRom: okReply({ value: 30 }),
+  });
+  const dev = openN8Device(call);
+  expect(dev.listDir("/")).toEqual([
+    { name: "a.raw", size: 4096, isDir: false },
+    { name: "d", size: 0, isDir: true },
+  ]);
+  dev.listDir("/clips");
+  expect(Array.from(dev.readFile("/clip.raw"))).toEqual([1, 2]);
+  dev.writeFile("/clips/b.raw", new Uint8Array([3]));
+  dev.makeDir("/clips");
+  dev.deleteFile("/clips/b.raw");
+  expect(dev.sniffRaw().length).toBe(SNIFFER_REGION_SIZE);
+  dev.writeFifo(new Uint8Array([0x90, 0x3c, 0x7f]));
+  expect(dev.load({ romBytes: new Uint8Array(16), romName: "clip.bin" })).toEqual({
+    bootPath: "usb-games/clip.bin",
+    mapIndex: 30,
+  });
+  expect(sent.slice(3)).toEqual([
+    ["n8ListDir", 3, ""], // "/" is the root, which the card names ""
+    ["n8ListDir", 3, "/clips"],
+    ["n8ReadFile", 3, "/clip.raw"],
+    ["n8WriteFile", 3, "/clips/b.raw", new Uint8Array([3])],
+    ["n8DirMake", 3, "/clips"],
+    ["n8FileDelete", 3, "/clips/b.raw"],
+    ["n8MemRD", 3, ADDR_SSR, SNIFFER_REGION_SIZE],
+    ["n8FifoWR", 3, new Uint8Array([0x90, 0x3c, 0x7f])],
+    ["n8BootRom", 3, "usb-games/clip.bin", new Uint8Array(16), new Uint8Array(0), true],
+  ]);
+});
+
+test("every call after close throws the device's own words, a zero-byte write included, and sends nothing", () => {
+  const { call, sent } = fakeHost({});
+  const dev = openN8Device(call);
+  dev.close();
+  dev.close(); // once is enough: the second sends nothing
+  const calls: [string, () => unknown][] = [
+    ["load", () => dev.load({ sdPath: "games/a.bin" })],
+    ["listDir", () => dev.listDir("/")],
+    ["readFile", () => dev.readFile("/a")],
+    ["writeFile", () => dev.writeFile("/a", new Uint8Array(0))],
+    ["makeDir", () => dev.makeDir("/d")],
+    ["deleteFile", () => dev.deleteFile("/a")],
+    ["sniffRaw", () => dev.sniffRaw()],
+    ["writeFifo", () => dev.writeFifo(new Uint8Array(0))],
+  ];
+  for (const [name, f] of calls) expect(f).toThrow(`N8Device.${name}: the device is closed`);
+  expect(sent.map((s) => s[0])).toEqual(["n8ListPorts", "n8Open", "n8Handshake", "n8Close"]);
+});
```

- [ ] **Step 2: run it and watch it fail**

```bash
/workspaces/RetroPlug-main/cuttlefish/.superpowers/sdkn8/guarded.sh \
  pnpm -C /workspaces/RetroPlug-main/cuttlefish test n8/device
```

Expected: the runner's boundary check refuses the import of a module that does not exist yet:

```
refused: packages/n8/ts/test/device.test.ts:6:30: cannot be resolved: ../device names no file
check-boundaries: 1 refusal(s), 0 stale exception(s) under .
```

- [ ] **Step 3: the device, and its export**

```patch
diff --git a/packages/n8/package.json b/packages/n8/package.json
index 7a095b67a..46d294504 100644
--- a/packages/n8/package.json
+++ b/packages/n8/package.json
@@ -15,7 +15,8 @@
     "./protocol": "./ts/protocol.ts",
     "./ui": "./ts/ui/index.ts",
     "./cli": "./ts/cli/index.ts",
-    "./bridge": "./ts/bridge.ts"
+    "./bridge": "./ts/bridge.ts",
+    "./device": "./ts/device.ts"
   },
   "dependencies": {
     "@retroplug/core": "workspace:*",
diff --git a/packages/n8/ts/device.ts b/packages/n8/ts/device.ts
new file mode 100644
index 000000000..3db422526
--- /dev/null
+++ b/packages/n8/ts/device.ts
@@ -0,0 +1,94 @@
+// The Everdrive N8 Pro as a session script opens it: the SDK's connectN8 (packages/retroplug/cli/sdk.ts) is
+// openN8Device over the app's RPC call, so a consumer script uploads a file, boots a ROM or reads the live sniffer
+// without spawning `retroplug-cli n8-load` and parsing what it printed. Deliberately a small surface over the session
+// createN8Client opens (the facet's, n8/desktop/N8Rpc.hpp, where the one Edio runs): the SDK's declaration file is
+// written by hand (packages/retroplug/cli/sdk-types.d.ts), and every type exposed here is one more to keep exact there.
+// Every call is synchronous, as every facet call is, so none of it needs the event loop; a call that fails on the line
+// leaves the next to resync the session in the host (N8Rpc::ready).
+
+import { createN8Client, pickN8Port, ADDR_SSR, SNIFFER_REGION_SIZE, type N8Call, type N8DirEntry } from "./index";
+import type { LoadOptions } from "./load";
+
+export type { N8DirEntry } from "./index";
+
+export interface N8ConnectOptions {
+  /** Serial port to open. Omitted = the first port whose VID:PID is the N8 Pro's (38df:0017). */
+  serial?: string;
+  /** Handshake timeout, ms. */
+  timeoutMs?: number;
+}
+
+/** What load() uploads and boots: ROM bytes to usb-games/<romName>, or a ROM already on the card, and a battery. */
+export type N8LoadOptions = LoadOptions;
+
+export interface N8Device {
+  /** The serial port in use. */
+  readonly port: string;
+  /** Upload and boot a ROM by driving the N8's menu. Only works AT the menu: with a game running the menu is not there
+   *  to answer, and the call times out (2 s). Power-cycle first (the cart cannot reset the console). NOT a way to ask
+   *  "is a game running?": its first act is to send the menu's probe bytes ("*t") through the cart FIFO, and a running
+   *  game reads those as its own input. `romName` must be a plain file name. */
+  load(opts: N8LoadOptions): { bootPath: string; mapIndex: number };
+  /** List a directory on the card ("/" or "" is the root). */
+  listDir(path: string): N8DirEntry[];
+  /** Read a whole file off the card. Slow: a USB round trip a 512-byte block. */
+  readFile(path: string): Uint8Array;
+  /** Write a whole file to the card, creating missing parent directories. No reboot needed. */
+  writeFile(path: string, bytes: Uint8Array): void;
+  makeDir(path: string): void;
+  /** Delete a file or an empty directory. Permanent. */
+  deleteFile(path: string): void;
+  /** The live sniffer region (512 bytes: mapper registers, the $4000-$401F mirror at +$080, OAM...), the same bytes
+   *  `n8-load --sniff-raw` writes. A system-region read, NOT a cart DMA, so it never disturbs a running game. */
+  sniffRaw(): Uint8Array;
+  /** Push raw bytes into the cart FIFO, where a running ROM reads them at $40F0. */
+  writeFifo(bytes: Uint8Array): void;
+  /** Release the port. Any later call on this device throws. */
+  close(): void;
+}
+
+/** Open the N8 on `call` (the explicit port, else the first N8 the host lists) and run the connect handshake. Throws
+ *  if there is no N8 or it does not answer, which is also what another process holding the (exclusive) port looks
+ *  like; a session whose handshake failed is closed first. */
+export function openN8Device(call: N8Call, opts: N8ConnectOptions = {}): N8Device {
+  const client = createN8Client(call);
+  const n8 = client.openN8(pickN8Port(client.listPorts(), opts.serial));
+  try {
+    n8.connect(opts.timeoutMs);
+  } catch (e) {
+    n8.close();
+    throw e;
+  }
+
+  // Closed here, not only in the host: the host answers a closed handle with its own words ("no open N8 session"), and
+  // a zero-byte write never reaches it at all, so without this a call after close() could "succeed" doing nothing.
+  let closed = false;
+  const guard = <T>(what: string, f: () => T): T => {
+    if (closed) throw new Error(`N8Device.${what}: the device is closed`);
+    return f();
+  };
+
+  return {
+    port: n8.port,
+    load: (o) =>
+      guard("load", () => {
+        const { bootPath, mapIndex } = n8.load(o);
+        return { bootPath, mapIndex };
+      }),
+    listDir: (path) =>
+      guard("listDir", () =>
+        n8.listDir(path === "/" ? "" : path).map(({ name, size, isDir }) => ({ name, size, isDir })),
+      ),
+    readFile: (path) => guard("readFile", () => n8.readFile(path)),
+    writeFile: (path, bytes) => guard("writeFile", () => n8.writeFile(path, bytes)),
+    makeDir: (path) => guard("makeDir", () => n8.edio.dirMake(path)),
+    deleteFile: (path) => guard("deleteFile", () => n8.edio.fileDelete(path)),
+    sniffRaw: () => guard("sniffRaw", () => n8.edio.memRD(ADDR_SSR, SNIFFER_REGION_SIZE)),
+    writeFifo: (bytes) => guard("writeFifo", () => n8.edio.fifoWR(bytes)),
+    close: () => {
+      if (closed) return;
+      closed = true;
+      n8.close();
+    },
+  };
+}
```

- [ ] **Step 4: run it again, and the wider checks**

```bash
/workspaces/RetroPlug-main/cuttlefish/.superpowers/sdkn8/guarded.sh \
  pnpm -C /workspaces/RetroPlug-main/cuttlefish test n8/device
/workspaces/RetroPlug-main/cuttlefish/.superpowers/sdkn8/guarded.sh pnpm -C /workspaces/RetroPlug-main/cuttlefish test n8
pnpm -C /workspaces/RetroPlug-main/cuttlefish/packages/n8 typecheck
pnpm -C /workspaces/RetroPlug-main/cuttlefish check:boundaries
```

Expected:

```
# n8/device
TAP version 13
1..4
ok 1 - it opens the first N8 the host lists, or the port asked for, and runs the handshake
ok 2 - a handshake that is not answered closes the session and throws its failure
ok 3 - the card, the boot, the sniffer and the FIFO are the session's calls, and a listing is its three fields
ok 4 - every call after close throws the device's own words, a zero-byte write included, and sends nothing
# 4 passed, 0 skipped, 0 failed

# 1 test file(s) passed (jobs=16)
```

then `# 10 test file(s) passed (jobs=16)`, the typecheck's `tsc -p tsconfig.json` line alone, and `check-boundaries:
the packages hold their boundaries (0 allowed by EXCEPTIONS, 0 warning(s))`.

- [ ] **Step 5: commit**

```bash
git -C /workspaces/RetroPlug-main/cuttlefish add -- \
  packages/n8/ts/device.ts \
  packages/n8/package.json \
  packages/n8/ts/test/device.test.ts
git -C /workspaces/RetroPlug-main/cuttlefish commit -F - <<'MSG'
feat(sdk-n8): the N8 device a session script opens, @retroplug/n8/device

A consumer script reached the N8 only by spawning n8-load and parsing what it printed. openN8Device is the small
surface feat/sdk-n8 gave it (the card's files, a ROM booted at the menu, the sniffer region and raw FIFO bytes, all
synchronous), but over the pack's own client and facet rather than a second Edio in TS, since the Edio exists once.
It picks the port with pickN8Port, closes the session when the handshake fails, and refuses every call once closed in
its own words, the zero-byte writes the client never sends included, which would otherwise "succeed" doing nothing.
The SDK's connectN8 wraps it over the process's RPC call.
MSG
```

---

## Task 5: an N8 bridge survives a failed FIFO write

`n8-bridge` and risa's `n8-sync` share the poll loop in `packages/n8/ts/bridge.ts`, whose `forward` wrote to the cart
FIFO from a 1 ms `setInterval` with no try/catch. The launcher drops a timer's throw today, along with the rest of that
tick's queue; once Task 8 makes an uncaught error end the session, one failed write would end a live set. `forward`
logs a failure (the first and every hundredth), counts it in the status line, and gives up (closing the session and
exiting 1) only when `failureWatch` says the cart is gone: a RUN of failures, one that restarts after a gap longer than
the window and is fatal only once it has lasted 3 s AND held 5 failures, because MIDI can be idle for seconds and a
hiccup, a quiet stretch and a second hiccup must not read as 3 s of failing.

**Files** (line numbers are the file at `fc2578945`):

- Modify: `packages/n8/ts/bridge.ts:56-63` (the counters and `forward`), `:77` (the status line) and `:85` (`failureWatch`, before
  `COMMON_FLAGS`; the lines are Task 2's tree's, `:44-51`, `:65` and `:73` there).
- Test: `packages/n8/ts/test/bridgeWatch.test.ts` (new).

**Interfaces:**

- Consumes: `CliHost`'s `process.exit` and the session's `n8.close()` / `n8.edio.fifoWR`.
- Produces: `export function failureWatch(fatalMs: number, minFailures: number): { failed(now: number): boolean;
  succeeded(): void }` from `@retroplug/n8/bridge`; the status line gains `, N failed` once a write has failed.

### Steps

- [ ] **Step 1: write the failing test**

```patch
diff --git a/packages/n8/ts/test/bridgeWatch.test.ts b/packages/n8/ts/test/bridgeWatch.test.ts
new file mode 100644
index 000000000..f443772d1
--- /dev/null
+++ b/packages/n8/ts/test/bridgeWatch.test.ts
@@ -0,0 +1,37 @@
+// The N8 bridges' rule for "the cart is gone" (failureWatch, packages/n8/ts/bridge.ts, which n8-bridge and risa's
+// n8-sync share): a RUN of failed FIFO writes, not two isolated ones around a quiet stretch of MIDI, which is what a
+// rule counting failures over wall time would exit on, mid-set.
+import { test, expect } from "@retroplug/testing";
+import { failureWatch } from "../bridge";
+
+test("two hiccups either side of a quiet stretch are not fatal", () => {
+  const w = failureWatch(3000, 5);
+  expect(w.failed(1_000)).toBe(false);
+  expect(w.failed(6_000)).toBe(false); // 5 s later, nothing written between: a new run
+});
+
+test("a sustained run of failures is fatal once it has lasted the window and held enough of them", () => {
+  const w = failureWatch(3000, 5);
+  const verdicts = [0, 800, 1600, 2400, 3200].map((t) => w.failed(10_000 + t));
+  expect(verdicts).toEqual([false, false, false, false, true]);
+});
+
+test("a long window with too few failures is not fatal", () => {
+  const w = failureWatch(3000, 5);
+  expect([0, 2900, 5800].map((t) => w.failed(t))).toEqual([false, false, false]);
+});
+
+test("a success in between ends the run", () => {
+  const w = failureWatch(3000, 5);
+  for (const t of [0, 500, 1000, 1500]) w.failed(t);
+  w.succeeded();
+  expect(w.failed(3600)).toBe(false);
+});
+
+test("a burst, a long quiet stretch, then one more failure is a new run, not five in a row", () => {
+  // Without the run restarting after a gap, the burst's four and the late one add up to five failures spanning ten
+  // seconds: fatal, for a cart that only hiccuped twice.
+  const w = failureWatch(3000, 5);
+  for (const t of [0, 100, 200, 300]) w.failed(t);
+  expect(w.failed(10_000)).toBe(false);
+});
```

- [ ] **Step 2: run it and watch it fail**

```bash
/workspaces/RetroPlug-main/cuttlefish/.superpowers/sdkn8/guarded.sh \
  pnpm -C /workspaces/RetroPlug-main/cuttlefish test n8/bridgeWatch
```

Expected:

```
    packages/n8/ts/test/bridgeWatch.test.ts:5:9:
      5 │ import { failureWatch } from "../bridge";
        ╵          ~~~~~~~~~~~~


# BUILD FAILED: n8/bridgeWatch
Build failed with 1 error:
packages/n8/ts/test/bridgeWatch.test.ts:5:9: ERROR: No matching export in "packages/n8/ts/bridge.ts" for import "failureWatch"
```

- [ ] **Step 3: the watch, and the forward that uses it**

```patch
diff --git a/packages/n8/ts/bridge.ts b/packages/n8/ts/bridge.ts
index df27fff0f..054c89a4a 100644
--- a/packages/n8/ts/bridge.ts
+++ b/packages/n8/ts/bridge.ts
@@ -44,8 +44,29 @@ export function runBridge(host: CliHost, args: string[], clientName: string,
   const queue: { due: number; bytes: number[] }[] = [];
   let msgCount = 0;
   let byteCount = 0;
+  // A write that fails is logged and counted here, not thrown out of the poll: an error a timer callback throws ends
+  // the session (the launcher prints it and exits 1), and one USB hiccup must not end a live set. What IS fatal is the
+  // cart being gone, a run of failures lasting FATAL_AFTER_MS with no write between, since a bridge forwarding into
+  // nothing would otherwise run on looking healthy.
+  const FATAL_AFTER_MS = 3000;
+  const watch = failureWatch(FATAL_AFTER_MS, 5);
+  let failCount = 0;
   const forward = (bytes: number[]): void => {
-    n8.edio.fifoWR(Uint8Array.from(bytes));
+    try {
+      n8.edio.fifoWR(Uint8Array.from(bytes));
+    } catch (e) {
+      failCount++;
+      const fatal = watch.failed(Date.now());
+      if (failCount === 1 || failCount % 100 === 0)
+        console.error(`N8 bridge: a write failed (${failCount} so far): ${e instanceof Error ? e.message : String(e)}`);
+      if (fatal) {
+        console.error(`N8 bridge: no write has succeeded for ${FATAL_AFTER_MS} ms, so the N8 is gone; exiting`);
+        n8.close();
+        host.process.exit(1);
+      }
+      return;
+    }
+    watch.succeeded();
     msgCount++;
     byteCount += bytes.length;
   };
@@ -62,7 +83,10 @@ export function runBridge(host: CliHost, args: string[], clientName: string,
     while (queue.length > 0 && queue[0].due <= now) forward(queue.shift()!.bytes);
   }, 1);
 
-  setInterval(() => console.log(`forwarded ${msgCount} messages / ${byteCount} bytes`), 2000);
+  setInterval(
+    () => console.log(`forwarded ${msgCount} messages / ${byteCount} bytes${failCount ? `, ${failCount} failed` : ""}`),
+    2000,
+  );
 
   console.log(
     `bridging ${midiIn || "all MIDI inputs"} -> Everdrive N8 on ${n8.port}` +
@@ -70,6 +94,33 @@ export function runBridge(host: CliHost, args: string[], clientName: string,
   );
 }
 
+/** When a run of write failures means the cart is gone. `failed(now)` records one and answers "give up?"; `succeeded()`
+ *  ends the run. A RUN, not isolated failures: MIDI can be idle for seconds and nothing is written then, so a hiccup, a
+ *  quiet stretch and a second hiccup must not read as `fatalMs` of failing. A failure more than `fatalMs` after the
+ *  previous one starts a new run, and a run is fatal only once it has lasted `fatalMs` AND held `minFailures`. */
+export function failureWatch(
+  fatalMs: number,
+  minFailures: number,
+): { failed(now: number): boolean; succeeded(): void } {
+  let since = 0;
+  let last = 0;
+  let count = 0;
+  return {
+    failed(now) {
+      if (count === 0 || now - last > fatalMs) {
+        since = now;
+        count = 0;
+      }
+      last = now;
+      count++;
+      return now - since > fatalMs && count >= minFailures;
+    },
+    succeeded() {
+      count = 0;
+    },
+  };
+}
+
 /** The flags both tools accept, for their help text. */
 export const COMMON_FLAGS = [
   "  --list             list the available MIDI input ports and exit",
```

- [ ] **Step 4: run it again, and the wider checks**

```bash
/workspaces/RetroPlug-main/cuttlefish/.superpowers/sdkn8/guarded.sh \
  pnpm -C /workspaces/RetroPlug-main/cuttlefish test n8/bridgeWatch
/workspaces/RetroPlug-main/cuttlefish/.superpowers/sdkn8/guarded.sh pnpm -C /workspaces/RetroPlug-main/cuttlefish test n8
/workspaces/RetroPlug-main/cuttlefish/.superpowers/sdkn8/guarded.sh pnpm -C /workspaces/RetroPlug-main/cuttlefish test risa
pnpm -C /workspaces/RetroPlug-main/cuttlefish/packages/n8 typecheck
pnpm -C /workspaces/RetroPlug-main/cuttlefish/packages/risa typecheck
```

Expected:

```
TAP version 13
1..5
ok 1 - two hiccups either side of a quiet stretch are not fatal
ok 2 - a sustained run of failures is fatal once it has lasted the window and held enough of them
ok 3 - a long window with too few failures is not fatal
ok 4 - a success in between ends the run
ok 5 - a burst, a long quiet stretch, then one more failure is a new run, not five in a row
# 5 passed, 0 skipped, 0 failed

# 1 test file(s) passed (jobs=16)
```

then `# 11 test file(s) passed (jobs=16)` for the n8 package, `# 15 test file(s) passed (jobs=16)` for risa, and both
typechecks clean. Nothing runs the bridge itself: it opens real MIDI and serial ports, which the hardware rule forbids.

- [ ] **Step 5: commit**

```bash
git -C /workspaces/RetroPlug-main/cuttlefish add -- \
  packages/n8/ts/bridge.ts \
  packages/n8/ts/test/bridgeWatch.test.ts
git -C /workspaces/RetroPlug-main/cuttlefish commit -F - <<'MSG'
fix(sdk-n8): keep an N8 bridge alive through a failed FIFO write

The bridges' forward wrote to the cart FIFO from a timer with no try/catch. The launcher drops a timer's throw today,
with the rest of that tick's queue, and once an uncaught error ends a session (the launcher change after this one) a
single failed write would end a live set. A failed write is logged (the first and every hundredth) and counted in the
status line, and the bridge exits only when failureWatch says the cart is gone: a run of failures that has lasted 3 s
and held 5, restarting after a gap, because MIDI can be idle for seconds and two hiccups around a quiet stretch are
not 3 s of failing. n8-bridge and risa's n8-sync share it through the one loop.
MSG
```

---

## Task 6: `connectN8` and `keepAlive` in the CLI SDK

A consumer script has the n8 facet already (the CLI starts the providers before it reads its first argument) but no
way to reach it, and keep-alive only as the undeclared `globalThis.__rp_keepAlive`. `cli/sdk.ts` exports `connectN8`,
`openN8Device` over this process's RPC call made when a script connects (the n8 pack cannot import the shell's
`makeCall`, so this is a one-line wrapper rather than a bare re-export), the device's four types, and the session's
`keepAlive` (a no-op where no host binds the hook). `cli/sdk-types.d.ts` declares them whole, and
`cli/sdkSurfaceCheck.ts` compares the declaration with the implementation by type IDENTITY rather than mutual
assignability, which passes a dropped `readonly`, an added optional parameter, a widened parameter and a required
argument made optional; the four types are exact and the two functions compared by signature. The usage check spells
no ROM extension, so `test/source-literals` stays clean. The shell's first import of the n8 pack needs one
`SHELL_IMPORTS` entry, with its reason. The SDK's bytecode is embedded in `retroplug-cli`, so the binary is rebuilt.

**Files** (line numbers are the file at `fc2578945`):

- Modify: `packages/retroplug/cli/sdk.ts:13` (the two imports) and `:32` (`connectN8` and the re-exports).
- Modify: `packages/retroplug/scripts/check-boundaries.mjs:164` (the `SHELL_IMPORTS` entry).
- Modify: `packages/retroplug/cli/sdk-types.d.ts:766` (the N8 block, at the end).
- Modify: `packages/retroplug/cli/sdkSurfaceCheck.ts:36-39` (the imports), `:52-56` (`Same`, `Exact`), `:79` (the six
  checks) and `:212` (`sdkN8Check`, at the end).
- Test: `packages/retroplug/test-native/n8-sdk.test.ts` (new).

**Interfaces:**

- Consumes: Task 4's `openN8Device`, `N8ConnectOptions`, `N8Device`, `N8LoadOptions`, `N8DirEntry` from
  `@retroplug/n8/device`; `makeCall` (`src/rpcClient.ts`); `keepAlive` (`cli/session.ts`).
- Produces, from `"retroplug-cli"`: `function connectN8(opts?: N8ConnectOptions): N8Device`, `function keepAlive():
  void` and the types `N8ConnectOptions`, `N8Device`, `N8LoadOptions`, `N8DirEntry`; in `sdkSurfaceCheck.ts`,
  `type Same<A, B>` and the identity `Exact`, used by every existing exact check. Tasks 7 and 8's `run-cli-ts-tests.sh`
  sections import `keepAlive` from the SDK.

### Steps

- [ ] **Step 1: write the failing test**

The test host starts the real N8 provider on `FakeN8`'s port `fake-n8`, so the SDK's device runs through the facet and
the one Edio exactly as it does in `retroplug-cli`.

```patch
diff --git a/packages/retroplug/test-native/n8-sdk.test.ts b/packages/retroplug/test-native/n8-sdk.test.ts
new file mode 100644
index 000000000..b802102d3
--- /dev/null
+++ b/packages/retroplug/test-native/n8-sdk.test.ts
@@ -0,0 +1,49 @@
+// The SDK's N8 end to end (connectN8, packages/retroplug/cli/sdk.ts, over @retroplug/n8/device): this host
+// (retroplug-host, packages/native/src/main.cpp) starts the real N8 provider on FakeN8's port, so a session script's
+// device runs through the facet and the one Edio exactly as on the CLI: the port it finds, the card's files, the
+// sniffer region, a FIFO write, a ROM booted at the menu, and a closed device refusing a call.
+import { test, expect } from "@retroplug/testing";
+import { connectN8 } from "../cli/sdk";
+
+test("connectN8 finds the fake cart, and the card's calls make, write, list, read and delete", () => {
+  const n8 = connectN8();
+  try {
+    expect(n8.port).toBe("fake-n8");
+    n8.makeDir("/sdk");
+    const data = new Uint8Array(1500).map((_, i) => (i * 7) & 0xff);
+    n8.writeFile("/sdk/a.bin", data);
+    expect(n8.listDir("/sdk")).toEqual([{ name: "a.bin", size: 1500, isDir: false }]);
+    expect(n8.listDir("/").some((e) => e.name === "sdk" && e.isDir)).toBe(true);
+    expect(Array.from(n8.readFile("/sdk/a.bin"))).toEqual(Array.from(data));
+    n8.deleteFile("/sdk/a.bin");
+    expect(n8.listDir("/sdk")).toEqual([]);
+    // A file the card does not hold, in the words a consumer's catch matches.
+    expect(() => n8.deleteFile("/sdk/a.bin")).toThrow("Edio: operation error 0x04");
+  } finally {
+    n8.close();
+  }
+});
+
+test("sniffRaw reads the sniffer region, writeFifo reaches the cart, and load boots at the menu", () => {
+  const n8 = connectN8({ serial: "fake-n8", timeoutMs: 1000 });
+  try {
+    expect(n8.sniffRaw().length).toBe(512);
+    n8.writeFifo(new Uint8Array([0x90, 0x3c, 0x7f])); // not a menu command: the menu skips it
+    const rom = new Uint8Array(16 + 16384);
+    rom.set([0x4e, 0x45, 0x53, 0x1a, 1, 0, 0, 0]); // iNES, mapper 0
+    expect(n8.load({ romBytes: rom, romName: "sdk-boot.bin" })).toEqual({
+      bootPath: "usb-games/sdk-boot.bin",
+      mapIndex: 69,
+    });
+    expect(n8.readFile("usb-games/sdk-boot.bin").length).toBe(rom.length);
+  } finally {
+    n8.close();
+  }
+});
+
+test("a closed device refuses every call in its own words", () => {
+  const n8 = connectN8();
+  n8.close();
+  expect(() => n8.listDir("/")).toThrow("N8Device.listDir: the device is closed");
+  expect(() => n8.writeFifo(new Uint8Array(0))).toThrow("N8Device.writeFifo: the device is closed");
+});
```

- [ ] **Step 2: run it and watch it fail**

```bash
/workspaces/RetroPlug-main/cuttlefish/.superpowers/sdkn8/guarded.sh \
  pnpm -C /workspaces/RetroPlug-main/cuttlefish test:native n8-sdk
```

Expected:

```
    packages/retroplug/test-native/n8-sdk.test.ts:6:9:
      6 │ import { connectN8 } from "../cli/sdk";
        ╵          ~~~~~~~~~


# BUILD FAILED: n8-sdk
Build failed with 1 error:
packages/retroplug/test-native/n8-sdk.test.ts:6:9: ERROR: No matching export in "packages/retroplug/cli/sdk.ts" for import "connectN8"

# 1/1 native test file(s) FAILED: n8-sdk
```

- [ ] **Step 3: export them from the SDK**

```patch
diff --git a/packages/retroplug/cli/sdk.ts b/packages/retroplug/cli/sdk.ts
index c0e90cfc8..875157675 100644
--- a/packages/retroplug/cli/sdk.ts
+++ b/packages/retroplug/cli/sdk.ts
@@ -10,6 +10,9 @@
 // written: it boots the control plane, runs the callback and exits with its outcome. A TEST file must not call it - it
 // would exit the process and preempt the harness, reporting a false pass - so a test uses `test()`/`bootSession()`, and
 // runSession refuses outright (exit 1, with a message) when this process has registered a test case.
+import { openN8Device, type N8ConnectOptions, type N8Device } from "@retroplug/n8/device";
+import { makeCall } from "../src/rpcClient";
+
 export { bootSession, hostArgs, runSession } from "./session";
 export type { Session } from "./session";
 
@@ -31,6 +34,15 @@ export { DirectGraph } from "../src/graph/directGraph";
 
 export { test, expect, skip } from "@retroplug/testing";
 
+// The Everdrive N8 Pro, and the event loop a script that awaits anything needs. The device is the n8 pack's
+// (@retroplug/n8/device), over this process's RPC call, made when a script connects; keepAlive is the session's own,
+// which the CLI's long-running tools call too.
+export function connectN8(opts?: N8ConnectOptions): N8Device {
+  return openN8Device(makeCall(), opts);
+}
+export type { N8ConnectOptions, N8Device, N8LoadOptions, N8DirEntry } from "@retroplug/n8/device";
+export { keepAlive } from "./session";
+
 // Audio analysis DSP for tuning/timbre verification (F2-F7). Pure TS over rendered PCM; prefer the decoded
 // Hz readouts (getExpansionAudioState().frequency / getApuState) for pitch when available - these are the
 // black-box path plus the ergonomic assertions on top.
```

- [ ] **Step 4: check the boundaries and watch rule 9 refuse the import**

```bash
pnpm -C /workspaces/RetroPlug-main/cuttlefish check:boundaries
```

Expected: exit 1, once for the import and once for the type re-export:

```
refused: packages/retroplug/cli/sdk.ts: imports "@retroplug/n8/device", which is not one of the contract modules the shell's src/ and cli/ may reach (rule 9): a cart's or a console's code is reached through the generated registries
refused: packages/retroplug/cli/sdk.ts: imports "@retroplug/n8/device", which is not one of the contract modules the shell's src/ and cli/ may reach (rule 9): a cart's or a console's code is reached through the generated registries
check-boundaries: 2 refusal(s), 0 stale exception(s) under .
```

- [ ] **Step 5: the boundary entry**

```patch
diff --git a/packages/retroplug/scripts/check-boundaries.mjs b/packages/retroplug/scripts/check-boundaries.mjs
index ca422016a..4ce50253e 100644
--- a/packages/retroplug/scripts/check-boundaries.mjs
+++ b/packages/retroplug/scripts/check-boundaries.mjs
@@ -162,6 +162,8 @@ const SHELL_IMPORTS = [
   { specifier: "@retroplug/tracker/workingSongBlob", reason: "the working-song image a project carries across a reboot" },
   // The test harness itself: the SDK serves it to a consumer's tests, and the ROM-test session imports it.
   { specifier: "@retroplug/testing", reason: "the TAP harness a session's own tests run through (the SDK serves it to a consumer)" },
+  // The SDK's Everdrive N8: a session script's device is the n8 pack's, over the process's RPC call.
+  { specifier: "@retroplug/n8/device", reason: "the N8 device the SDK hands a session script (connectN8)" },
 ];
 
 /** Rule 9's list for `ui/`: a list of its own, because the kit and the view contract are the UI's alone (src/ and cli/
```

- [ ] **Step 6: check the boundaries again, and typecheck, and watch the surface check refuse the undeclared exports**

```bash
pnpm -C /workspaces/RetroPlug-main/cuttlefish check:boundaries
pnpm -C /workspaces/RetroPlug-main/cuttlefish typecheck
```

Expected: `check-boundaries: the packages hold their boundaries (0 allowed by EXCEPTIONS, 0 warning(s))`, then the
typecheck fails in `packages/retroplug` (pnpm's colours stripped):

```
packages/retroplug typecheck: cli/sdkSurfaceCheck.ts:66:3 - error TS2344: Type '"cli/sdk.ts exports a value cli/sdk-types.d.ts does not declare"' does not satisfy the constraint 'true'.
packages/retroplug typecheck: Found 1 error in cli/sdkSurfaceCheck.ts:66
packages/retroplug typecheck: Failed
```

- [ ] **Step 7: declare them, and compare by identity**

```patch
diff --git a/packages/retroplug/cli/sdk-types.d.ts b/packages/retroplug/cli/sdk-types.d.ts
index e295075e8..ef6a31c4b 100644
--- a/packages/retroplug/cli/sdk-types.d.ts
+++ b/packages/retroplug/cli/sdk-types.d.ts
@@ -764,3 +764,60 @@ export declare function assertPitchInTune(mono: Float32Array, expectedHz: number
 export declare function spectralFingerprint(mono: Float32Array, opts?: { bands?: number; sampleRate?: number; fmin?: number; fmax?: number }): number[];
 /** Assert a render's fingerprint has not drifted from `golden` by more than `tol` dB in any band. */
 export declare function assertFingerprint(mono: Float32Array, golden: number[], tol?: number, opts?: { bands?: number; sampleRate?: number; fmin?: number; fmax?: number }): void;
+
+// ─── The Everdrive N8 Pro over USB (connectN8), and keeping the event loop alive. ────────────────────────────────────
+
+export interface N8ConnectOptions {
+  /** Serial port to open. Omitted = the first port whose VID:PID is the N8 Pro's (38df:0017). */
+  serial?: string;
+  /** Handshake timeout, ms. */
+  timeoutMs?: number;
+}
+export interface N8LoadOptions {
+  /** Local ROM bytes to upload to usb-games/<romName>. Omit to boot an existing SD path (sdPath). */
+  romBytes?: Uint8Array;
+  /** Upload target basename under usb-games/ (required with romBytes): a plain file name. */
+  romName?: string;
+  /** Boot a ROM already on the SD card by its SD path (instead of uploading). */
+  sdPath?: string;
+  /** Battery save (.srm) to restore via the menu's per-game slot before the game boots. */
+  srm?: Uint8Array;
+}
+export interface N8DirEntry {
+  name: string;
+  size: number;
+  isDir: boolean;
+}
+export interface N8Device {
+  /** The serial port in use. */
+  readonly port: string;
+  /** Upload and boot a ROM by driving the N8's menu. Only works AT the menu: with a game running it times out (2 s).
+   *  Power-cycle first (the cart cannot reset the console). NOT a way to ask "is a game running?": it first sends the
+   *  menu's probe bytes ("*t") through the cart FIFO, and a running game reads those as its own input. `romName` must
+   *  be a plain file name. */
+  load(opts: N8LoadOptions): { bootPath: string; mapIndex: number };
+  /** List a directory on the card ("/" or "" is the root). */
+  listDir(path: string): N8DirEntry[];
+  /** Read a whole file off the card. Slow: a USB round trip a 512-byte block. */
+  readFile(path: string): Uint8Array;
+  /** Write a whole file to the card, creating missing parent directories. No reboot needed. */
+  writeFile(path: string, bytes: Uint8Array): void;
+  makeDir(path: string): void;
+  /** Delete a file or an empty directory. Permanent. */
+  deleteFile(path: string): void;
+  /** The live sniffer region (512 bytes; the $4000-$401F write mirror is at +$080), what `n8-load --sniff-raw`
+   *  writes. Not a cart DMA, so it never disturbs a running game. */
+  sniffRaw(): Uint8Array;
+  /** Push raw bytes into the cart FIFO, where a running ROM reads them at $40F0. */
+  writeFifo(bytes: Uint8Array): void;
+  /** Release the port. Any later call on this device throws. */
+  close(): void;
+}
+/** Open the N8 and run the connect handshake. Throws if there is no N8 or it does not answer, which is also what
+ *  another process holding the (exclusive) port looks like. A call that fails on the line leaves the next one to drain
+ *  the line and re-run the handshake first. */
+export declare function connectN8(opts?: N8ConnectOptions): N8Device;
+/** Keep the session's event loop running until `tjs.exit()`. A session otherwise gets a short bounded run, so a timer
+ *  or an awaited child process (`tjs.spawn(...).wait()`) never completes. Call once, up front. An error nothing
+ *  catches still ends the session, kept alive or not, with exit 1 and its text. */
+export declare function keepAlive(): void;
diff --git a/packages/retroplug/cli/sdkSurfaceCheck.ts b/packages/retroplug/cli/sdkSurfaceCheck.ts
index b7e7657ed..9fdeffa4d 100644
--- a/packages/retroplug/cli/sdkSurfaceCheck.ts
+++ b/packages/retroplug/cli/sdkSurfaceCheck.ts
@@ -34,8 +34,13 @@ import {
   skip,
   interleaveStereoStreams,
   deinterleaveStereo,
+  connectN8,
+  keepAlive,
   type BreakHitBatch,
   type CoreTransportStats,
+  type N8Device,
+  type N8DirEntry,
+  type N8LoadOptions,
   type TimelineInvariant,
 } from "retroplug-cli";
 
@@ -50,10 +55,13 @@ import {
  *  either - `never extends true` is TRUE, so a `never` would quietly satisfy the assertion. */
 type Assert<T extends true> = T;
 type Covers<A, B, Msg extends string> = [A] extends [B] ? true : Msg;
+// IDENTITY, not mutual assignability. Assignability both ways passes a `readonly` dropped on one side, an optional
+// parameter added, a parameter type widened (method parameters are bivariant) and a required argument made optional:
+// each a declaration a consumer compiles against and the implementation does not honour. Two generic functions are
+// mutually assignable only when the types are identical, which is the standard way to ask the compiler that question.
+type Same<A, B> = (<T>() => T extends A ? 1 : 2) extends (<T>() => T extends B ? 1 : 2) ? true : false;
 type Exact<A, B, Msg extends string> =
-  Covers<A, B, `${Msg}: the declaration describes something the implementation does not`> extends true
-    ? Covers<B, A, `${Msg}: the implementation has something the declaration does not`>
-    : `${Msg}: the declaration describes something the implementation does not`;
+  Same<A, B> extends true ? true : `${Msg}: the declaration and the implementation are not the same type`;
 
 // Values: the NAME sets, two-way, so the declaration can neither omit an export nor invent one. This is
 // deliberately not a comparison of the types behind those names, because the declaration is a curated
@@ -77,6 +85,14 @@ export type _TimelineInvariant = Assert<Exact<Sdk.TimelineInvariant, Dts.Timelin
 export type _PngImageData = Assert<Exact<Sdk.PngImageData, Dts.PngImageData, "PngImageData">>;
 export type _LoadResult = Assert<Exact<Sdk.LoadResult, Dts.LoadResult, "LoadResult">>;
 export type _NoteOpts = Assert<Exact<Sdk.NoteOpts, Dts.NoteOpts, "NoteOpts">>;
+// The N8 surface is declared WHOLE, not as a subset, so all four are exact, and the two functions by signature: the
+// name sets above only prove they exist.
+export type _N8ConnectOptions = Assert<Exact<Sdk.N8ConnectOptions, Dts.N8ConnectOptions, "N8ConnectOptions">>;
+export type _N8LoadOptions = Assert<Exact<Sdk.N8LoadOptions, Dts.N8LoadOptions, "N8LoadOptions">>;
+export type _N8DirEntry = Assert<Exact<Sdk.N8DirEntry, Dts.N8DirEntry, "N8DirEntry">>;
+export type _N8Device = Assert<Exact<Sdk.N8Device, Dts.N8Device, "N8Device">>;
+export type _connectN8 = Assert<Exact<typeof Sdk.connectN8, typeof Dts.connectN8, "connectN8">>;
+export type _keepAlive = Assert<Exact<typeof Sdk.keepAlive, typeof Dts.keepAlive, "keepAlive">>;
 
 // The class, the spec, the graph type and the three callables are compared shape by shape rather than name by name.
 // The class is one-way (the declaration is a curated view: `setRouting` widens the real routing union to `string`),
@@ -210,3 +226,20 @@ export function sdkSurfaceCheck(): void {
   // The lossy-transport knob is a role config, so it is only checked for shape.
   s.project.systems.setRoleConfig(id, descriptor.core, { fifo: "hardware", fifoBytesPerSecond: 20000 });
 }
+
+// The N8 surface as a session script uses it: upload, boot, read the live state, push to the FIFO. Never called.
+export function sdkN8Check(): void {
+  keepAlive();
+  const n8: N8Device = connectN8({ serial: hostArgs()[0], timeoutMs: 1000 });
+  const entries: N8DirEntry[] = n8.listDir("/");
+  void entries.map((e) => e.name + e.size + String(e.isDir));
+  const load: N8LoadOptions = { romBytes: new Uint8Array(16), romName: "boot-me", srm: new Uint8Array(8) };
+  const booted = n8.load(load);
+  void (booted.bootPath + booted.mapIndex);
+  n8.writeFile("/clips/a.raw", n8.readFile("/clip.raw"));
+  n8.makeDir("/clips");
+  n8.deleteFile("/clips/a.raw");
+  n8.writeFifo(n8.sniffRaw());
+  void n8.port;
+  n8.close();
+}
```

- [ ] **Step 8: typecheck, run the test, and the wider checks**

```bash
pnpm -C /workspaces/RetroPlug-main/cuttlefish typecheck
/workspaces/RetroPlug-main/cuttlefish/.superpowers/sdkn8/guarded.sh \
  pnpm -C /workspaces/RetroPlug-main/cuttlefish test:native n8-sdk
/workspaces/RetroPlug-main/cuttlefish/.superpowers/sdkn8/guarded.sh \
  pnpm -C /workspaces/RetroPlug-main/cuttlefish test source-literals
cmake --build /workspaces/RetroPlug-main/cuttlefish/build --target retroplug-cli -j$(nproc)
node /workspaces/RetroPlug-main/cuttlefish/.superpowers/sdkn8/nopy.mjs
/workspaces/RetroPlug-main/cuttlefish/.superpowers/sdkn8/guarded.sh \
  /workspaces/RetroPlug-main/cuttlefish/.superpowers/sdkn8/run-cli-ts-tests.nopy.sh
```

Expected: the typecheck exits 0; then

```
# n8-sdk
TAP version 13
1..3
ok 1 - connectN8 finds the fake cart, and the card's calls make, write, list, read and delete
ok 2 - sniffRaw reads the sniffer region, writeFifo reaches the cart, and load boots at the menu
ok 3 - a closed device refuses every call in its own words
# 3 passed, 0 skipped, 0 failed

# 1 native test file(s) passed (jobs=16)
```

`# 4 passed, 0 skipped, 0 failed` for `source-literals`, `[100%] Built target retroplug-cli`, and the CLI check ends
`PASS: retroplug-cli TypeScript harness` (its step 5 imports the rebuilt SDK). (The identity check bites: with
`readonly` dropped from the declared `N8Device.port`, the typecheck fails with `"N8Device: the declaration and the
implementation are not the same type"`, which mutual assignability let through. Checked while this plan was written;
not a step.)

- [ ] **Step 9: commit**

```bash
git -C /workspaces/RetroPlug-main/cuttlefish add -- \
  packages/retroplug/cli/sdk.ts \
  packages/retroplug/cli/sdk-types.d.ts \
  packages/retroplug/cli/sdkSurfaceCheck.ts \
  packages/retroplug/scripts/check-boundaries.mjs \
  packages/retroplug/test-native/n8-sdk.test.ts
git -C /workspaces/RetroPlug-main/cuttlefish commit -F - <<'MSG'
feat(sdk-n8): connectN8 and keepAlive in the CLI SDK

A consumer script had the n8 facet in its process but no way to reach it, and keep-alive only as an undeclared global,
so a timer or an awaited child process never completed. The SDK exports connectN8 (the n8 pack's device over this
process's RPC call), its four types, and the session's keepAlive, all declared whole in sdk-types.d.ts. nesvj's
nesvj-raw.ts, written against feat/sdk-n8, runs against these names and shapes unchanged.

The surface check compared the declaration with the implementation by mutual assignability, which passes a dropped
readonly, an added optional parameter, a widened parameter and a required argument made optional; it compares by type
identity now, and the N8 types and the two functions are held exact. The shell's first import of the n8 pack is one
SHELL_IMPORTS entry with its reason, and test:native's n8-sdk drives the SDK's device against the fake cart.
MSG
```

---

## Task 7: the TAP harness records what nothing caught

`packages/testing/ts/harness.ts` is the one harness of `pnpm test`, `test:native`, `test:ui` and the CLI SDK, and it
left an error nothing caught to the runtime: an unawaited rejection inside a case passed (the harness exited before it
was dispatched), a case awaiting a background task that died first hung under `keepAlive()`, a stray rejection under
`tjs run` killed the file with no further output, and a timer's throw was lost or blamed on whoever was running. The
harness takes the branch's final form, installed by the first `test()` so a process that registers no case (every CLI
command, whose dispatcher reaches the module through the SDK) keeps the runtime's own handling: an
`unhandledrejection` listener (cancelled with `preventDefault`) and wrapped timers record each error and print it to
stderr at once; one during a case fails that case at once, the case being raced against an abort the record
triggers; a timer's throw is blamed on the case that created the timer, or fails the file naming that case once it has
reported; one outside every case fails the file; and a loop turn after each case lets what it left pending be
dispatched while it still counts as that case's. `registeredCases()` stays.

The suites were run whole with this harness (and the fixture tree) while this plan was written, and surfaced no latent
stray: no case reported a rejection or a timer's throw, and the counts are the base's plus Tasks 4 to 6's new files. A
stray appearing on another tree is a real leak in that test: fix the test, name it in the commit, and do not weaken the
harness.

**Files** (line numbers are the file at `fc2578945`):

- Modify: `packages/testing/ts/harness.ts:19` (the records, `installHooks`, `turn`), `:34` (the first `test()`
  installs the hooks) and `:153-186` (`runAll`).
- Test: `tools/run-cli-ts-tests.sh:386` (sections 6e and 6f, before section 7).

**Interfaces:**

- Consumes: Task 6's `keepAlive` from `"retroplug-cli"` (the sections' test files); txiki's `addEventListener` and
  its `unhandledrejection` event (`ev.reason`, `ev.preventDefault()`), `setTimeout`, `setInterval`.
- Produces: no export changes (`test`, `skip`, `expect`, `registeredCases` as before). A case fails, and a file fails
  with `# FAIL: uncaught errors that no test could report:`, on a recorded error; each record prints `# harness: <what>
  <where>: <first line>` to stderr.

### Steps

- [ ] **Step 1: write the failing sections**

```patch
diff --git a/tools/run-cli-ts-tests.sh b/tools/run-cli-ts-tests.sh
index a408eb308..c2dcb9095 100755
--- a/tools/run-cli-ts-tests.sh
+++ b/tools/run-cli-ts-tests.sh
@@ -383,6 +383,76 @@ else
 	echo "skip - render leg: the smsggdj ROM or the render golden is absent"
 fi
 
+# 6e. The harness RECORDS a rejection nothing handled, rather than leaving it to the runtime: uncancelled, txiki makes
+#     it fatal, and whether a given leak killed the file with no TAP at all (every other case lost) or passed unseen
+#     depended on whether it was dispatched before the harness had exited. A rejection handled a loop turn late counts,
+#     as in Node.
+RJ="$TMP/rejections"
+mkdir -p "$RJ/src"
+cat >"$RJ/src/late.test.ts" <<'EOF'
+import { test, keepAlive } from "retroplug-cli";
+keepAlive();
+test("handles its rejection a turn late", async () => {
+  const p = Promise.reject(new Error("late-handled"));
+  await new Promise((r) => setTimeout(r, 1));
+  await p.catch(() => {});
+});
+test("a clean case after it", () => {});
+EOF
+cat >"$RJ/src/outside.test.ts" <<'EOF'
+import { test } from "retroplug-cli";
+test("registered first", () => {});
+Promise.reject(new Error("stray rejection at top level"));
+EOF
+out="$TMP/test-rj.log"
+timeout 30 "$CLI" test "$RJ/src" -j1 >"$out" 2>&1
+check "files with stray rejections fail the run" 1 $?
+contains "a late-handled rejection fails ITS case" "$out" "not ok 1 - handles its rejection a turn late"
+contains "...and the file's other cases still report" "$out" "ok 2 - a clean case after it"
+contains "a rejection outside every case fails the file, saying so" "$out" "outside any test"
+
+# 6f. Recording an error is not waiting for it. A case awaiting work that died first (a background task that threw
+#     before signalling) used to wait for ever, since the record was read only once the case returned, which under
+#     keepAlive() hung the run; and a timer's throw was blamed on whichever case was running when it fired. `timeout`
+#     turns a regression into a 124 rather than a hung suite.
+RW="$TMP/recorded"
+mkdir -p "$RW/src"
+cat >"$RW/src/dies.test.ts" <<'EOF'
+import { test, keepAlive } from "retroplug-cli";
+keepAlive();
+test("awaits a background task that dies before signalling", async () => {
+  let done!: () => void;
+  const finished = new Promise<void>((r) => (done = r));
+  void (async () => {
+    await Promise.resolve();
+    throw new Error("background task died");
+    done();
+  })();
+  await finished;
+});
+test("a clean case after it", () => {});
+EOF
+cat >"$RW/src/blame.test.ts" <<'EOF'
+import { test, keepAlive } from "retroplug-cli";
+keepAlive();
+test("leaves a timer that throws later", () => {
+  setTimeout(() => {
+    throw new Error("from the first test");
+  }, 20);
+});
+test("innocent, and busy when that timer fires", async () => {
+  await new Promise((r) => setTimeout(r, 60));
+});
+EOF
+out="$TMP/test-rw.log"
+timeout 30 "$CLI" test "$RW/src" -j1 >"$out" 2>&1
+check "a case awaiting a dead task fails the run instead of hanging it" 1 $?
+contains "...and fails THAT case, with the reason" "$out" "background task died"
+contains "...and the file's other cases still report" "$out" "ok 2 - a clean case after it"
+contains "a timer's throw does not fail the innocent case running when it fires" "$out" \
+	"ok 2 - innocent, and busy when that timer fires"
+contains "...it is blamed on the case that created it" "$out" "from test 1, after it had finished"
+
 # 7. Stripping is position-preserving: output length must equal input length, so a stack trace from the
 #    emitted .js points at the right line of the .ts.
 for f in pass.test fail.test helper; do
```

- [ ] **Step 2: run the CLI check and watch them fail**

```bash
cmake --build /workspaces/RetroPlug-main/cuttlefish/build --target retroplug-cli -j$(nproc)
node /workspaces/RetroPlug-main/cuttlefish/.superpowers/sdkn8/nopy.mjs
/workspaces/RetroPlug-main/cuttlefish/.superpowers/sdkn8/guarded.sh \
  /workspaces/RetroPlug-main/cuttlefish/.superpowers/sdkn8/run-cli-ts-tests.nopy.sh
```

Expected: exit 1 after about 35 s (`dies.test.ts` hangs until its `timeout 30`), every earlier section passing and
these failing:

```
FAIL - files with stray rejections fail the run (expected exit 1, got 0)
FAIL - a late-handled rejection fails ITS case (output did not contain: not ok 1 - handles its rejection a turn late)
FAIL - a rejection outside every case fails the file, saying so (output did not contain: outside any test)
FAIL - a case awaiting a dead task fails the run instead of hanging it (expected exit 1, got 124)
FAIL - ...and fails THAT case, with the reason (output did not contain: background task died)
FAIL - ...and the file's other cases still report (output did not contain: ok 2 - a clean case after it)
FAIL - ...it is blamed on the case that created it (output did not contain: from test 1, after it had finished)
FAIL: 7 check(s) failed
```

- [ ] **Step 3: the harness records what nothing caught**

```patch
diff --git a/packages/testing/ts/harness.ts b/packages/testing/ts/harness.ts
index 76b308014..a32705187 100644
--- a/packages/testing/ts/harness.ts
+++ b/packages/testing/ts/harness.ts
@@ -18,6 +18,102 @@ type TestFn = () => void | Promise<void>;
 const cases: { name: string; fn: TestFn }[] = [];
 let scheduled = false;
 
+// Errors nothing handled - a rejection, or a throw inside a timer callback - recorded instead of left to the runtime.
+// Uncancelled, txiki makes one fatal, and a host that ends on it (the CLI) ends the file with no TAP at all, every
+// OTHER case lost too; whether a given leak did that or passed unseen depended on whether it was dispatched before or
+// after the harness had already exited. Recorded:
+//
+//   - one that happens while a case runs FAILS THAT CASE AT ONCE: the case is raced against an abort, so a case
+//     awaiting work that died first (a background task that threw before signalling) does not wait for ever, which
+//     with keepAlive() hung the whole run;
+//   - a timer's throw is blamed on the case that CREATED the timer. If that case has already reported, the FILE fails,
+//     naming it, rather than whichever innocent case happens to be running;
+//   - one outside every case fails the file;
+//   - every one is printed to stderr the moment it is recorded, so a file that never reaches its summary (a bounded
+//     session that runs out of pumps) still says what went wrong.
+//
+// A rejection handled LATE - after the event loop turned with it pending - counts: it was unhandled when the runtime
+// looked, as it is in Node. All of this is installed by the first test() and not before: a process that registers no
+// case (a CLI command, whose dispatcher reaches this module through the SDK) keeps the runtime's own handling, so an
+// error there is left fatal and the launcher reports it.
+//
+// A timer that fires after the file's last loop turn is not seen: the harness has exited. A timer created before the
+// first test() is not wrapped. Real timers in a CLI test also need keepAlive() at all: without it the session's bounded
+// pumps end before they fire.
+interface Stray {
+  reason: string;
+  during: number; // the case it belongs to, -1 for none
+  late: boolean; // it belongs to a case that had already reported
+}
+const strays: Stray[] = [];
+let current = -1;
+let abortCase: ((e: unknown) => void) | null = null;
+const ABORTED = { rpAbort: true };
+
+/** Text for anything thrown, never itself throwing: a reason that cannot be stringified (an object with no prototype)
+ *  must not turn a recorded error back into a fatal one inside the listener. */
+function describe(r: unknown): string {
+  try {
+    return r instanceof Error ? `${r.message}\n${r.stack ?? ""}` : String(r);
+  } catch {
+    return "(a thrown value that cannot be turned into text)";
+  }
+}
+
+function record(owner: number, what: string, reason: string): void {
+  const late = owner >= 0 && owner !== current;
+  strays.push({ reason: `${what}: ${reason}`, during: owner, late });
+  const where =
+    owner < 0 ? "outside any test" : late ? `from test ${owner + 1}, after it had finished` : `in test ${owner + 1}`;
+  try {
+    console.error(`# harness: ${what} ${where}: ${reason.split("\n")[0]}`);
+  } catch {
+    // never let the report itself throw
+  }
+  if (!late && owner >= 0 && abortCase) abortCase(ABORTED);
+}
+
+type TimerFn = (f: unknown, ms?: number, ...a: unknown[]) => unknown;
+
+function installHooks(): void {
+  const g = globalThis as unknown as {
+    addEventListener?: (t: string, f: (e: { reason: unknown; preventDefault(): void }) => void) => void;
+  } & Record<string, TimerFn | undefined>;
+  g.addEventListener?.("unhandledrejection", (ev) => {
+    ev.preventDefault();
+    // A rejection carries no trace of who created it, so it belongs to the case running now, which may be the one
+    // AFTER the culprit; its message says so.
+    record(current, "unhandled rejection (during, or left by a test before, this one)", describe(ev.reason));
+  });
+  // A throw inside a timer callback is not a rejection, and txiki gives JS no event for it: it is fatal natively. Wrap
+  // the timers: the callback runs in a try, and the case that created the timer is captured when it is created.
+  // clearTimeout / clearInterval keep working because the original's handle is returned.
+  for (const name of ["setTimeout", "setInterval"] as const) {
+    const orig = g[name];
+    if (!orig) continue;
+    g[name] = (f: unknown, ms?: number, ...a: unknown[]) => {
+      const owner = current;
+      return orig(
+        typeof f !== "function"
+          ? f
+          : (...args: unknown[]) => {
+              try {
+                (f as (...x: unknown[]) => void)(...args);
+              } catch (e) {
+                record(owner, "uncaught error in a timer", describe(e));
+              }
+            },
+        ms,
+        ...a,
+      );
+    };
+  }
+}
+
+/** One turn of the event loop: long enough for the runtime to dispatch rejections left pending. */
+const turn = (): Promise<void> =>
+  new Promise((r) => (globalThis as unknown as { setTimeout(f: () => void, ms: number): unknown }).setTimeout(r, 0));
+
 /** How many cases this process has registered. The CLI's `runSession` refuses to run when one has: a session file
  *  run by path registers none, and a test file calling it would exit the process before the harness reported, which
  *  the runner reads as a pass. */
@@ -31,6 +127,7 @@ export function test(name: string, fn: TestFn): void {
   // top-level test() calls) has finished registering.
   if (!scheduled) {
     scheduled = true;
+    installHooks();
     Promise.resolve().then(runAll);
   }
 }
@@ -151,36 +248,66 @@ function exit(code: number): void {
 }
 
 async function runAll(): Promise<void> {
+  // One turn FIRST, so a rejection left by the module body itself (a throw after the test() calls) is recorded as
+  // outside every case rather than blamed on the first one.
+  await turn();
   const out: string[] = ["TAP version 13", `1..${cases.length}`];
   let failed = 0;
   let skipped = 0;
   for (let i = 0; i < cases.length; i++) {
     const { name, fn } = cases[i];
+    current = i;
+    let thrown: unknown = undefined;
+    let threw = false;
     try {
-      await fn();
-      out.push(`ok ${i + 1} - ${name}`);
+      const aborted = new Promise<never>((_, reject) => (abortCase = reject));
+      await Promise.race([Promise.resolve().then(fn), aborted]);
+      await turn(); // let what the case left pending be dispatched while it still counts as this case's
     } catch (e) {
-      // The skip check comes FIRST: below, a non-Error would fall through to String(e) and be
-      // formatted as a failure.
-      if (isSkip(e)) {
-        skipped++;
-        // TAP13's real directive. Uppercase to match Catch2's own TAP reporter, and because it keeps
-        // the output greppable by the same literal these suites have always logged.
-        out.push(`ok ${i + 1} - ${name} # SKIP ${e.rpSkipReason}`);
-        continue;
-      }
-      failed++;
-      // QuickJS's Error.stack doesn't prefix the message, so lead with it.
-      const detail = e instanceof Error ? `${e.message}\n${e.stack ?? ""}` : String(e);
-      out.push(`not ok ${i + 1} - ${name}`, "  ---");
-      for (const line of detail.split("\n")) out.push(`  ${line}`);
-      out.push("  ...");
+      thrown = e;
+      threw = true;
+    }
+    abortCase = null;
+    const mine = strays.filter((s) => s.during === i && !s.late);
+    // A skip is a skip only if nothing ELSE went wrong in the case. The skip check comes before any String(e): a
+    // non-Error would otherwise be formatted as a failure.
+    if (threw && isSkip(thrown) && mine.length === 0) {
+      skipped++;
+      // TAP13's real directive. Uppercase to match Catch2's own TAP reporter, and because it keeps the output greppable
+      // by the same literal these suites have always logged.
+      out.push(`ok ${i + 1} - ${name} # SKIP ${thrown.rpSkipReason}`);
+      continue;
     }
+    if (!threw && mine.length === 0) {
+      out.push(`ok ${i + 1} - ${name}`);
+      continue;
+    }
+    failed++;
+    const parts: string[] = [];
+    // QuickJS's Error.stack doesn't prefix the message, so lead with it. The abort itself is not news.
+    if (threw && thrown !== ABORTED && !isSkip(thrown)) parts.push(describe(thrown));
+    for (const s of mine) parts.push(s.reason);
+    out.push(`not ok ${i + 1} - ${name}`, "  ---");
+    for (const line of parts.join("\n").split("\n")) out.push(`  ${line}`);
+    out.push("  ...");
+  }
+  current = -1;
+  await turn();
+  // What belongs to no case that could still report - outside every case, or a case that had already reported - fails
+  // the FILE. Listed once each: one throw can surface as more than one rejection (a module body's, and the import
+  // chain's).
+  const fileLevel = [...new Set(strays.filter((s) => s.during < 0 || s.late).map((s) => s.reason))];
+  if (fileLevel.length) {
+    out.push("# FAIL: uncaught errors that no test could report:");
+    for (const r of fileLevel) for (const line of r.split("\n")) out.push(`#   ${line}`);
   }
-  // For a human reading one file's block. The runner derives its own counts from the result lines
-  // above rather than parsing this, so there is one source of truth and this can never disagree.
-  out.push(`# ${cases.length - failed - skipped} passed, ${skipped} skipped, ${failed} failed`);
+  // For a human reading one file's block. The runner derives its own counts from the result lines above rather than
+  // parsing this, so there is one source of truth and this can never disagree.
+  out.push(
+    `# ${cases.length - failed - skipped} passed, ${skipped} skipped, ${failed} failed` +
+      (fileLevel.length ? `, and the file failed (${fileLevel.length} error(s) outside the tests)` : ""),
+  );
   console.log(out.join("\n"));
   // A skip is not a failure: the exit code still answers "did anything break?".
-  exit(failed ? 1 : 0);
+  exit(failed || fileLevel.length ? 1 : 0);
 }
```

- [ ] **Step 4: rebuild the CLI (it embeds the harness through the SDK) and run the check again**

Same commands as Step 2. Expected: exit 0, and

```
ok   - files with stray rejections fail the run
ok   - a late-handled rejection fails ITS case
ok   - ...and the file's other cases still report
ok   - a rejection outside every case fails the file, saying so
ok   - a case awaiting a dead task fails the run instead of hanging it
ok   - ...and fails THAT case, with the reason
ok   - ...and the file's other cases still report
ok   - a timer's throw does not fail the innocent case running when it fires
ok   - ...it is blamed on the case that created it
```

```
PASS: retroplug-cli TypeScript harness
```

- [ ] **Step 5: the three TAP suites, with the fixtures**

```bash
RETROPLUG_RESOURCES_DIR=/workspaces/resources /workspaces/RetroPlug-main/cuttlefish/.superpowers/sdkn8/guarded.sh \
  pnpm -C /workspaces/RetroPlug-main/cuttlefish test
RETROPLUG_RESOURCES_DIR=/workspaces/resources /workspaces/RetroPlug-main/cuttlefish/.superpowers/sdkn8/guarded.sh \
  pnpm -C /workspaces/RetroPlug-main/cuttlefish test:native
RETROPLUG_RESOURCES_DIR=/workspaces/resources /workspaces/RetroPlug-main/cuttlefish/.superpowers/sdkn8/guarded.sh \
  pnpm -C /workspaces/RetroPlug-main/cuttlefish test:ui
```

Expected: each exits 0, with no `# harness:` line anywhere in its output:

```
# 241 test file(s) passed (jobs=16)
```

```
# 176 native test file(s) passed (jobs=16)
#   2 case(s) SKIPPED in 2 file(s): dsp-bench(1), nes-bench(1)
```

```
# 48 UI test file(s) passed (jobs=16)
```

- [ ] **Step 6: commit**

```bash
git -C /workspaces/RetroPlug-main/cuttlefish add -- \
  packages/testing/ts/harness.ts \
  tools/run-cli-ts-tests.sh
git -C /workspaces/RetroPlug-main/cuttlefish commit -F - <<'MSG'
fix(sdk-n8): the TAP harness records what nothing caught

The harness left an error nothing caught to the runtime: an unawaited rejection in a case passed, since the harness
exited before it was dispatched; a case awaiting a background task that died first hung under keepAlive(); a stray
rejection under tjs run killed the file with no TAP; and a timer's throw was lost, or blamed on whichever case was
running. It now records a rejection nothing handled (cancelling it) and a throw in a timer callback, prints each at
once, fails the running case at once by racing it against an abort, blames a timer's throw on the case that created
the timer, and fails the file for anything outside every case or left by a case that had already reported.

The hooks go in with the first test(), so a process that registers no case keeps the runtime's own handling. The
whole of pnpm test, test:native and test:ui ran with it and surfaced no stray; run-cli-ts-tests.sh's new sections 6e
and 6f check each rule through the real binary, ahead of the launcher change that makes an uncaught error fatal.
MSG
```

---

## Task 8: the launcher ends a session that threw, with exit 1 and its text

txiki turns an error nothing caught (a throw in an async module body, a rejection nothing cancelled, a throw in a
timer or another callback it invoked) into a pending exception and a request to stop ITS loop. `retroplug-cli` drives
the loop itself and looked at neither: a plain session that threw said only "session did not complete", a kept-alive
one pumped for ever (and wedged a `retroplug-cli test` run), and a timer's throw was lost with exit 0. `pumpOnce`
pumps, then prints a pending exception and ends the session with exit 1, or, when the exception is gone but the stop
request came (a sibling callback's try/catch clears it in the same turn), says so and exits 1; the stop request's
async callback is taken over to see it. Both loops use it. The startup override of `tjs.exit` never takes (txiki's
`tjs` is frozen), and its failed set leaves an exception pending that `pumpOnce` would take for the session's, so it
is cleared; the teardown a normal exit therefore skips is its own later change (spec/07, Task 9). This lands after
Tasks 5 and 7, which keep the bridges and the test files from dying on what this now makes fatal.

**Files** (line numbers are the file at `fc2578945`):

- Modify: `packages/native/cli/main.cpp:75` (`g_stopRequested`), `:257-268` (the override's comment and the cleared
  exception) and `:303-337` (`pumpOnce`, the two loops and the exit).
- Test: `tools/run-cli-ts-tests.sh:386` (section 6g, before section 7; the line is the file at `fc2578945`, Task 7's
  sections 6e and 6f sitting above it by then).

**Interfaces:**

- Consumes: `TJS_GetRuntime(ctx)->stop` (txiki's `private.h`, which `TjsHostRuntime.hpp` includes), `uv_stop`,
  `JS_HasException`, `JS_GetException`, `tjs_dump_error`; Task 6's `keepAlive` (the section's files).
- Produces: no interface. A session that threw exits 1 with the error printed, kept alive or not; one whose error was
  cleared prints `error: a callback threw, and its error was cleared before it could be reported` and exits 1.

### Steps

- [ ] **Step 1: write the failing section**

```patch
diff --git a/tools/run-cli-ts-tests.sh b/tools/run-cli-ts-tests.sh
index c2dcb9095..a53785ec8 100755
--- a/tools/run-cli-ts-tests.sh
+++ b/tools/run-cli-ts-tests.sh
@@ -453,6 +453,44 @@ contains "a timer's throw does not fail the innocent case running when it fires"
 	"ok 2 - innocent, and busy when that timer fires"
 contains "...it is blamed on the case that created it" "$out" "from test 1, after it had finished"
 
+# 6g. An error nothing caught ends a session, kept alive or not, with exit 1 and its text. txiki turns one (a throw in
+#     an async module body, a rejection nothing cancelled, a throw in a timer) into a pending exception and a request to
+#     stop ITS loop, and the launcher drives the loop itself, so it used to be dropped: a plain session said only that it
+#     did not complete, a kept-alive one pumped for ever (and under `test` wedged the whole run), and a timer's throw was
+#     lost with exit 0.
+UC="$TMP/uncaught"
+mkdir -p "$UC/src"
+printf 'import { keepAlive } from "retroplug-cli";\nkeepAlive();\nthrow new Error("setup bug after keepAlive");\n' \
+	>"$UC/src/ka.test.ts"
+out="$TMP/run-ka.log"
+timeout 20 "$CLI" run "$UC/src/ka.test.ts" >"$out" 2>&1
+check "an uncaught error after keepAlive() exits 1, not a hang" 1 $?
+contains "...and prints the error" "$out" "setup bug after keepAlive"
+out="$TMP/test-ka.log"
+timeout 20 "$CLI" test "$UC/src" >"$out" 2>&1
+check "a test file that throws after keepAlive() fails the run, not hangs it" 1 $?
+printf 'throw new Error("plain setup bug");\n' >"$UC/plain.ts"
+out="$TMP/run-plain.log"
+timeout 20 "$CLI" run "$UC/plain.ts" >"$out" 2>&1
+check "an uncaught error without keepAlive() exits 1" 1 $?
+contains "...and prints the error, not only that the session did not complete" "$out" "plain setup bug"
+printf '%s\n' 'declare const tjs: { exit(code: number): void };' 'import { keepAlive } from "retroplug-cli";' \
+	'keepAlive();' 'setTimeout(() => {' '  throw new Error("thrown in a timer");' '}, 5);' \
+	'setTimeout(() => tjs.exit(0), 200);' >"$UC/timer.ts"
+out="$TMP/run-timer.log"
+timeout 20 "$CLI" run "$UC/timer.ts" >"$out" 2>&1
+check "a timer's throw ends the session with exit 1, not a lost error and exit 0" 1 $?
+contains "...and prints the error" "$out" "thrown in a timer"
+#     An error a sibling callback's try/catch cleared before it was reported: the session ran on and exited 0. txiki's
+#     stop request survives where the exception does not.
+printf '%s\n' 'declare const tjs: { exit(code: number): void };' 'import { keepAlive } from "retroplug-cli";' \
+	'keepAlive();' 'setTimeout(() => {' '  throw new Error("lost");' '}, 5);' 'setTimeout(() => {' \
+	'  try {' '    JSON.parse("{");' '  } catch {}' '}, 5);' 'setTimeout(() => tjs.exit(0), 200);' >"$UC/lost.ts"
+out="$TMP/run-lost.log"
+timeout 20 "$CLI" run "$UC/lost.ts" >"$out" 2>&1
+check "an error cleared by a sibling callback still ends the session (exit 1)" 1 $?
+contains "...and says why" "$out" "a callback threw, and its error was cleared before it could be reported"
+
 # 7. Stripping is position-preserving: output length must equal input length, so a stack trace from the
 #    emitted .js points at the right line of the .ts.
 for f in pass.test fail.test helper; do
```

- [ ] **Step 2: run the CLI check and watch it fail**

```bash
cmake --build /workspaces/RetroPlug-main/cuttlefish/build --target retroplug-cli -j$(nproc)
node /workspaces/RetroPlug-main/cuttlefish/.superpowers/sdkn8/nopy.mjs
/workspaces/RetroPlug-main/cuttlefish/.superpowers/sdkn8/guarded.sh \
  /workspaces/RetroPlug-main/cuttlefish/.superpowers/sdkn8/run-cli-ts-tests.nopy.sh
```

Expected: exit 1 once the two kept-alive runs have hung to their `timeout 20`, every other section passing and these
failing:

```
FAIL - an uncaught error after keepAlive() exits 1, not a hang (expected exit 1, got 124)
FAIL - ...and prints the error (output did not contain: setup bug after keepAlive)
FAIL - a test file that throws after keepAlive() fails the run, not hangs it (expected exit 1, got 124)
FAIL - ...and prints the error, not only that the session did not complete (output did not contain: plain setup bug)
FAIL - a timer's throw ends the session with exit 1, not a lost error and exit 0 (expected exit 1, got 0)
FAIL - ...and prints the error (output did not contain: thrown in a timer)
FAIL - an error cleared by a sibling callback still ends the session (exit 1) (expected exit 1, got 0)
FAIL - ...and says why (output did not contain: a callback threw, and its error was cleared before it could be reported)
FAIL: 8 check(s) failed
```

(the plain throw already exits 1, `ok   - an uncaught error without keepAlive() exits 1`, but says only `session did
not complete (tjs.exit never called)`).

- [ ] **Step 3: `pumpOnce`**

```patch
diff --git a/packages/native/cli/main.cpp b/packages/native/cli/main.cpp
index 6968c8898..d3673415d 100644
--- a/packages/native/cli/main.cpp
+++ b/packages/native/cli/main.cpp
@@ -74,6 +74,12 @@ bool endsWithJs(const char* s) {
 struct ExitState { int code = 0; bool set = false; };
 ExitState g_exit;
 
+// Set when txiki asks its loop to stop. Every such request on the session runtime is an error path (an exception in a
+// callback the runtime invoked, a job that failed, a rejection nothing cancelled), and the request survives where the
+// exception may not: a sibling callback in the same loop turn that runs any try/catch clears the pending exception, and
+// the error would be lost with the session running on. See pumpOnce.
+bool g_stopRequested = false;
+
 JSValue jsExit(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
     int code = 0;
     if (argc >= 1) JS_ToInt32(ctx, &code, argv[0]);
@@ -255,7 +261,10 @@ int main(int argc, char** argv) try {
     });
 
     // globalThis.tjs.exit — the session sets the process exit code through it. Override any
-    // txiki-provided exit so we record the code and return it (rather than terminating mid-pump).
+    // txiki-provided exit so we record the code and return it (rather than terminating mid-pump). With txiki present
+    // the override does not take: txiki's `tjs` is frozen, so the set fails and txiki's own exit, which ends the
+    // process with the code, stays in place, and a session exits through it. The failed set leaves an exception
+    // pending, cleared here so the pump's check below does not take it for the session's.
     {
         JSValue tjsObj = JS_GetPropertyStr(ctx, global, "tjs");
         if (JS_IsUndefined(tjsObj) || JS_IsNull(tjsObj)) {
@@ -263,7 +272,8 @@ int main(int argc, char** argv) try {
             tjsObj = JS_NewObject(ctx);
             JS_SetPropertyStr(ctx, global, "tjs", JS_DupValue(ctx, tjsObj));
         }
-        JS_SetPropertyStr(ctx, tjsObj, "exit", JS_NewCFunction(ctx, jsExit, "exit", 1));
+        if (JS_SetPropertyStr(ctx, tjsObj, "exit", JS_NewCFunction(ctx, jsExit, "exit", 1)) < 0)
+            JS_FreeValue(ctx, JS_GetException(ctx));
         JS_FreeValue(ctx, tjsObj);
     }
 
@@ -302,16 +312,40 @@ int main(int argc, char** argv) try {
 
     std::signal(SIGINT, onSigint);
 
+    // One turn of the loop, and the session's end when something threw that nothing caught. txiki turns such an error (a
+    // throw in an async module body, a rejection nothing cancelled, a throw in a timer or another callback it invoked)
+    // into a pending exception and a request to stop ITS loop; this launcher drives the loop itself, so unless it looks
+    // the error is dropped: a plain session would say only that it did not complete, a kept-alive one would pump for
+    // ever (wedging any `retroplug-cli test` run it is part of), and a timer's throw would be lost. The stop request is
+    // an async handle whose callback stops txiki's loop; it is taken over to be seen as well, since it survives a sibling
+    // callback's try/catch clearing the exception. A test file's errors reach its harness first
+    // (packages/testing/ts/harness.ts records them and fails the case), so what ends a session here is its own.
+    TJS_GetRuntime(ctx)->stop.async_cb = [](uv_async_t* handle) {
+        g_stopRequested = true;
+        uv_stop(handle->loop);
+    };
+    bool       threw    = false;
+    const auto pumpOnce = [&] {
+        host.pump();
+        if (JS_HasException(ctx)) {
+            tjs_dump_error(ctx);
+            threw = true;
+        } else if (g_stopRequested) {
+            std::fprintf(stderr, "error: a callback threw, and its error was cleared before it could be reported\n");
+            threw = true;
+        }
+    };
+
     // Drive the job loop. ES modules evaluate async in QuickJS, so the module body (the dispatcher + the tool)
     // runs during these pumps. A normal command runs to completion + calls tjs.exit within the bounded
     // backstop; a long-running session calls __rp_keepAlive() during setup, which drops us into the unbounded
     // loop below so it can react to MIDI until tjs.exit or Ctrl-C.
-    for (int i = 0; i < 20000 && !g_exit.set && !g_sigint && !g_keepAlive; ++i) host.pump();
+    for (int i = 0; i < 20000 && !g_exit.set && !g_sigint && !g_keepAlive && !threw; ++i) pumpOnce();
 
-    if (g_keepAlive) {
-        // Long-running session (e.g. a live MIDI bridge): pump until it exits or Ctrl-C. The 250us cadence
+    if (g_keepAlive && !threw) {
+        // Long-running session (e.g. a live MIDI bridge): pump until it exits, throws or Ctrl-C. The 250us cadence
         // matches the native bridge loop; the session's own setInterval poll fires as the loop pumps.
-        while (!g_exit.set) {
+        while (!g_exit.set && !threw) {
             if (g_sigint) {
                 if (JS_IsUndefined(g_onInterrupt)) break;
                 // Hand the first Ctrl-C to the session; a second one gets the default action.
@@ -323,15 +357,17 @@ int main(int argc, char** argv) try {
                 JS_FreeValue(ctx, g_onInterrupt);
                 g_onInterrupt = JS_UNDEFINED;
             }
-            host.pump();
+            pumpOnce();
             std::this_thread::sleep_for(std::chrono::microseconds(250));
         }
-        JS_FreeValue(ctx, g_onInterrupt);
-        g_onInterrupt = JS_UNDEFINED;
-    } else if (!g_exit.set && !g_sigint) {
+    } else if (!threw && !g_exit.set && !g_sigint) {
         std::fprintf(stderr, "session did not complete (tjs.exit never called)\n");
         return 1;
     }
+    JS_FreeValue(ctx, g_onInterrupt);
+    g_onInterrupt = JS_UNDEFINED;
+    // A session that threw has said what (pumpOnce) and fails, kept alive or not.
+    if (threw) return 1;
     // A Ctrl-C (g_sigint) without tjs.exit is a clean exit 0 for a session that registered no interrupt hook; one
     // that did ends the process itself (the render tool exits 1).
     return g_exit.set ? g_exit.code : 0;
```

- [ ] **Step 4: rebuild and run the whole CLI check again**

Same commands as Step 2. Expected: exit 0; the new section

```
ok   - an uncaught error after keepAlive() exits 1, not a hang
ok   - ...and prints the error
ok   - a test file that throws after keepAlive() fails the run, not hangs it
ok   - an uncaught error without keepAlive() exits 1
ok   - ...and prints the error, not only that the session did not complete
ok   - a timer's throw ends the session with exit 1, not a lost error and exit 0
ok   - ...and prints the error
ok   - an error cleared by a sibling callback still ends the session (exit 1)
ok   - ...and says why
```

and, with every other section (the kit sessions' async exit, the render leg with its golden hash and its Ctrl-C, 6e
and 6f) still passing, 76 `ok` lines and

```
PASS: retroplug-cli TypeScript harness
```

- [ ] **Step 5: commit**

```bash
git -C /workspaces/RetroPlug-main/cuttlefish add -- \
  packages/native/cli/main.cpp \
  tools/run-cli-ts-tests.sh
git -C /workspaces/RetroPlug-main/cuttlefish commit -F - <<'MSG'
fix(sdk-n8): end a CLI session that threw, with exit 1 and its text

txiki turns an error nothing caught into a pending exception and a request to stop its loop, and retroplug-cli drives
the loop itself and looked at neither: a plain session that threw said only that it did not complete, a kept-alive one
pumped for ever and wedged any test run it was in, and a timer's throw was lost with exit 0. pumpOnce checks after
every pump, in the bounded loop and the keep-alive loop: a pending exception is printed and the session exits 1, and
txiki's stop request, taken over because it survives a sibling callback's try/catch clearing the exception, ends it the
same way, saying so.

The startup override of tjs.exit never takes, since txiki's tjs is frozen, and its failed set left an exception
pending that the check would take for the session's; it is cleared. The teardown a normal exit therefore skips is
recorded in spec/07 as its own change. This follows the harness and the bridges' failure watch, which keep a test file
and a live set from dying on what is now fatal.
MSG
```

---

## Task 9: the docs

[spec/14](../spec/14-hosts-harnesses.md)'s "retroplug-cli as a test harness" gains the real N8 and the event loop, and
what an error nothing caught does; [spec/13](../spec/13-subsystems.md)'s N8 entry the paged listing, the resync and the
bridges' watch; [spec/09](../spec/09-cli-debugging.md), which names the SDK's session exports, `keepAlive()` and
`connectN8()`; [spec/07](../spec/07-remaining-work.md) two known bugs this port found and leaves: a normal CLI exit skips
the teardown, and the test host drops an uncaught error. AGENTS.md is unchanged. The design document's as-built and
execution sections are the controller's.

**Files** (line numbers are the file at `fc2578945`):

- Modify: `spec/07-remaining-work.md:47` (two checklist lines, after "An Apply past a read-only config") and `:363`
  (two entries, before `## Engine` at `:364`).
- Modify: `spec/09-cli-debugging.md:51` (after the session runtime bullet's last line).
- Modify: `spec/13-subsystems.md:103` (a bullet before "What the cart allows").
- Modify: `spec/14-hosts-harnesses.md:204` (two paragraphs, at the end).

**Interfaces:**

- Consumes: the eight commits Tasks 1 to 8 made, and `packages/n8/ts/device.ts` being tracked (Task 4), which the
  link check requires.
- Produces: nothing code uses.

### Steps

- [ ] **Step 1: the doc changes**

```patch
diff --git a/spec/07-remaining-work.md b/spec/07-remaining-work.md
index 65f3b3895..5bae3b887 100644
--- a/spec/07-remaining-work.md
+++ b/spec/07-remaining-work.md
@@ -45,6 +45,8 @@ than work.
 - [ ] [Settings > MIDI misses a claim made while open](#settings--midi-misses-a-claim-made-while-open)
 - [ ] [The audio draft's sibling-backend cache](#the-audio-drafts-sibling-backend-cache)
 - [ ] [An Apply past a read-only config](#an-apply-past-a-read-only-config)
+- [ ] [A normal CLI exit skips the teardown](#a-normal-cli-exit-skips-the-teardown)
+- [ ] [The test host drops an uncaught error](#the-test-host-drops-an-uncaught-error)
 
 **Engine**
 - [ ] [DAW automation of node params](#daw-automation-of-node-params)
@@ -361,6 +363,25 @@ that [F5b design](../docs/modular-graph-phase-f5b-design.md) section 14 summaris
 `applyAudioDraft` calls `audioDevice.apply` regardless, so an Apply still reaches the device. (Found by the final review
 that [F5b design](../docs/modular-graph-phase-f5b-design.md) section 14 summarises; recorded only here.)
 
+### A normal CLI exit skips the teardown
+
+`retroplug-cli`'s override of `tjs.exit` never takes: txiki's `tjs` is frozen, so the set in
+[cli/main.cpp](../packages/native/cli/main.cpp) fails (the launcher clears the exception it leaves) and a session's
+`tjs.exit` is txiki's own, which ends the process at once. So `g_exit` is never set and the launcher's `Teardown`
+(`engine.clearGraph()`, then `rp_stop_providers`) never runs on a normal exit: the plans are not reclaimed while the
+providers' links are alive, and the providers are not stopped. The fix needs an exit the launcher sees (a `tjs.exit`
+wrapped before txiki freezes `tjs`, or an exit hook) and a look at what each provider's stop does at a process's end.
+([sdk-n8 port design](../docs/sdk-n8-port-design.md) section 2.)
+
+### The test host drops an uncaught error
+
+The test host's loop ([src/main.cpp](../packages/native/src/main.cpp)) has the fault the CLI's launcher had before its
+`pumpOnce`: it pumps txiki's loop without looking for a pending exception or a stop request, so an error nothing caught
+outside the harness's reach (a module body's throw before its first `test()`, a timer created before it) is dropped,
+and the file ends as "tests did not complete" with no reason, or not at all while kept alive. Its `tjs.exit` override
+does not take either. The fix is the CLI's `pumpOnce`, then a whole `pnpm test:native` run for the strays it
+surfaces. ([sdk-n8 port design](../docs/sdk-n8-port-design.md) section 3.)
+
 ## Engine
 
 ### DAW automation of node params
diff --git a/spec/09-cli-debugging.md b/spec/09-cli-debugging.md
index bdcf31927..c56fd8399 100644
--- a/spec/09-cli-debugging.md
+++ b/spec/09-cli-debugging.md
@@ -49,6 +49,12 @@ TS session at build time; the binary runs the JS). See the CLI scaffold in
   their types, and the project's graph carries them and their settings), and `runSession(main)` runs the
   body and reports the exit code. `hostArgs()` reads the session's
   argv (hung off the `Symbol.for("plugin")` namespace — `tjs.args` is a read-only txiki accessor).
+  `keepAlive()` keeps the loop running until `tjs.exit`, for a session that awaits a timer or a child process, and an
+  error nothing catches ends any session with exit 1 and its text
+  ([spec/14](14-hosts-harnesses.md#retroplug-cli-as-a-test-harness)).
+- **The real N8**: `connectN8()` opens an Everdrive N8 Pro for a session script: its card's files, a ROM booted at the
+  menu, the live sniffer region and raw FIFO bytes, synchronous calls over the n8 pack's facet
+  ([spec/14](14-hosts-harnesses.md#retroplug-cli-as-a-test-harness)).
 - **Event scripting** — [cli/timeline.ts](../packages/retroplug/cli/timeline.ts): a fluent,
   typed `Timeline` (`midi`/`sysex`/`note`/`press`/`tap`/`bpm`/`transport`/`screenshot` at absolute ms)
   whose `build()` is pure (stable ms-sort), and `renderTimeline(session, timeline, { durationMs, warmupMs })`
diff --git a/spec/13-subsystems.md b/spec/13-subsystems.md
index 02db8bd5d..490c7a5b4 100644
--- a/spec/13-subsystems.md
+++ b/spec/13-subsystems.md
@@ -100,6 +100,15 @@ Everdrive N8 Pro's cart FIFO, so the console itself plays. One-way, host to cart
   standalone, the headless host and the CLI, whose sessions use the facet's session half; a product's `PROVIDERS` list
   says which packs it starts). The Pico box (`pico/n8-host`) is the same Edio on TinyUSB. BlipToaster's MIDI policy
   (`rp.bliptoaster.midi-policy`) is a node of the box's plan; the desktop's N8 graphs do not use the policy node.
+- A listing is pulled 32 records a request (`Edio::listDir`): one request for a whole large directory fails on silicon
+  (a 181-entry one answered "dir read error 0x82"), so every listing, every file read (which lists its directory first)
+  and the cart jobs' battery lookup reach past 32 entries; `FakeN8` answers the range asked for. A session call that
+  fails on the line marks its session dirty in the facet, and the next call first drains the line (about 0.4 s at most)
+  and re-runs the handshake (0.3 s), answering the handshake's failure without sending itself when the line does not
+  answer (`N8Rpc::ready`), so the CLI's tools, the bridges and a session script's `connectN8()`
+  ([spec/14](14-hosts-harnesses.md#retroplug-cli-as-a-test-harness)) recover from a hiccup. The bridges
+  (`packages/n8/ts/bridge.ts`, `n8-bridge` and risa's `n8-sync`) log and count a failed FIFO write and exit only
+  after a run of failures lasting 3 s (`failureWatch`).
 - What the cart allows, and the pack accepts: a cart's battery is reachable only at the N8 menu, through the SD card's
   save file `EDN8/gamedata/<rom>/bram.srm`, and a dump takes about a minute for 64 KB; a running game refuses every cart
   job (a direct cart-memory read wedges it and leaves the menu deaf), and a restore goes before a `rom.upload` so the
diff --git a/spec/14-hosts-harnesses.md b/spec/14-hosts-harnesses.md
index 1c94456b9..675145de7 100644
--- a/spec/14-hosts-harnesses.md
+++ b/spec/14-hosts-harnesses.md
@@ -202,3 +202,29 @@ loaded on demand through `__rp_loadTsStripper`, so no other command pays for it.
 erasable syntax works, and `enum`, `namespace` and constructor parameter properties are refused with `file:line:col`.
 Wiring ts-blank-space's optional `onError` is what enforces that; omit it and an `enum` passes through as invalid
 JavaScript ([tools/run-cli-ts-tests.sh](../tools/run-cli-ts-tests.sh) checks the refusal).
+
+**The real N8 and the event loop.** A session script reaches a real Everdrive N8 Pro through the SDK's `connectN8()`
+([cli/sdk.ts](../packages/retroplug/cli/sdk.ts)): the n8 pack's device (`openN8Device`,
+[packages/n8/ts/device.ts](../packages/n8/ts/device.ts), exported as `@retroplug/n8/device`) over the process's RPC
+call, so a consumer uploads a file, boots a ROM at the menu, reads the live sniffer region or pushes raw FIFO bytes
+without spawning `n8-load` and parsing what it printed. Every call is synchronous, over the N8 facet the CLI's own N8
+tools use ([spec/13](13-subsystems.md#the-everdrive-n8-bridge)); a closed device refuses each call in its own words,
+and a call that failed on the line leaves the next to resync the session in the host. `keepAlive()` is the session's
+own: a session otherwise gets a bounded run of pumps, so a timer or an awaited `tjs.spawn(...).wait()` never completes,
+and a script that awaits anything calls it once, up front, and ends with `tjs.exit`. Both are declared whole in
+[cli/sdk-types.d.ts](../packages/retroplug/cli/sdk-types.d.ts), which `cli/sdkSurfaceCheck.ts` holds to the
+implementation by type identity, not mutual assignability (which passes a dropped `readonly`).
+
+**An error nothing caught.** It ends a session, kept alive or not, with exit 1 and its text. txiki turns one (a throw in
+an async module body, a rejection nothing cancelled, a throw in a timer) into a pending exception and a request to stop
+its loop, and the launcher ([packages/native/cli/main.cpp](../packages/native/cli/main.cpp)) drives that loop itself, so
+after every pump it looks for both: the exception, which it prints, and the stop request, which it takes over because
+it survives a sibling callback's try/catch clearing the exception. In a test file the harness
+([packages/testing/ts/harness.ts](../packages/testing/ts/harness.ts), the one `pnpm test`, `test:native` and
+`test:ui` run too) takes over first, from its first `test()`: it records a rejection nothing handled, or a throw in a
+timer callback, prints it to stderr at once, fails the running case at once (each case is raced against an abort, so
+one awaiting work that died first does not wait for ever), blames a timer's throw on the case that created the timer
+(failing the file, naming it, when that case has already reported), and fails the file for anything outside every case,
+while the rest of the file still reports. A rejection carries no trace of its creator, so it is blamed on the case
+running, whose failure says it may be one before; one handled a loop turn late counts as unhandled, as in Node.
+[tools/run-cli-ts-tests.sh](../tools/run-cli-ts-tests.sh) checks each of these through the real binary.
```

- [ ] **Step 2: check the docs**

```bash
pnpm -C /workspaces/RetroPlug-main/cuttlefish check:docs
```

Expected: the one warning being a plan record's pre-existing dead link:

```
WARN  docs/launchpad-plan.md:299  dead link in a plan record  ../packages/retroplug/ui/screens/menu/midiDevices.ts

check-doc-links: 111 file(s) clean, 1 warning(s)
```

- [ ] **Step 3: check that no em dash came in, anywhere in the port**

```bash
git -C /workspaces/RetroPlug-main/cuttlefish diff fc2578945 -- . ':!deps/sameboy' | grep '^+' | grep -c $'\u2014'
```

Expected: `0` (the `grep -c` exits 1, which is what "none" looks like).

- [ ] **Step 4: commit**

```bash
git -C /workspaces/RetroPlug-main/cuttlefish add -- \
  spec/07-remaining-work.md \
  spec/09-cli-debugging.md \
  spec/13-subsystems.md \
  spec/14-hosts-harnesses.md
git -C /workspaces/RetroPlug-main/cuttlefish commit -F - <<'MSG'
docs(sdk-n8): the N8 device, the resync and an uncaught error in the specs

spec/14's CLI harness section says how a session script reaches the real N8 (connectN8, the n8 pack's device over the
facet) and keeps its loop running (keepAlive), and what an error nothing caught does, in a session and in a test file.
spec/13's N8 entry records the paged listing, the session resync and the bridges' failure watch, and spec/09 names the
two new SDK exports beside the session's others.

spec/07 records the two things the port found and leaves for their own changes: a normal CLI exit skips the
launcher's teardown, because the tjs.exit override never takes, and the test host's loop drops an uncaught error the
way the CLI's did before pumpOnce.
MSG
```
