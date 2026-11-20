# N8 menu guard config watch - implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** At the N8 menu the guard writes nothing to the cart's FIFO and leaves the menu on the FPGA config block's
start pattern, so a game started from the cart's own browser no longer ends at ERROR:40.

**Architecture:** `N8MenuGuard` (n8-wire, header-only, no I/O) replaces its at-menu probe and three-miss leave with a
`readConfig` request every 20 ms and a `config(now, block)` input: the first block after entering is the reference; a
changed (or all-zero) block, then the same non-zero block for 500 ms, is `leftMenu`. `N8Link` (desktop) does the read on
a quiet turn. The Pico box never turns its guard at the menu, so it only loses its now-unreachable `leftMenu` branch.
`FakeN8` learns a browser start (block zero, then the game's; a FIFO byte meanwhile is ERROR:40) for the regression.

**Tech stack:** C++20, Catch2 (`retroplug-n8-test`, `retroplug-device-mode-test`), CMake.

**Design:** [n8-menu-guard-config-watch-design.md](n8-menu-guard-config-watch-design.md). Read it first.

## Rules for every task

- Work in `/workspaces/cuttlefish` on `feature/modularize`. Commit at the end of each task; no `Co-Authored-By` or any
  attribution in commit messages. Never push. Never use bare `git stash`.
- `deps/sameboy` shows as modified after a configure: expected, never stage or reset it. Stage files by name.
- Never use the em dash character in code, comments, docs or commit messages; use " - ", a colon or a semicolon.
- Comments explain why, in full sentences, in the files' existing voice; no "step N" or task references.
- Build a test binary by name with `-j$(nproc)`:
  - `cmake --build build --target retroplug-n8-test -j$(nproc)`, run `build/bin/retroplug-n8-test "<tag or name>"`
  - `cmake --build build --target retroplug-device-mode-test -j$(nproc)`, run
    `build/bin/retroplug-device-mode-test "[box-link]"`
  - the whole N8 suite: `pnpm test:n8` (must exit 0)
- Windows builds these targets with `/W4 /WX`: no narrowing without a cast, no unused variables or parameters.
- Hardware is never touched by Tasks 1-6.

## File map

| File | Change |
|---|---|
| `packages/n8-wire/native/src/transport/N8MenuGuard.hpp` | `readConfig`, `config()`, `kConfigNs`, `kSettleNs`, `kConfigBytes`; at-menu probe and `kLeaveMisses` removed; header comment |
| `packages/n8/native/test/N8MenuGuard.test.cpp` | the three-miss case replaced by the config-watch cases; header comment |
| `packages/n8/native/test/FakeN8.hpp` | `startFromBrowser()`: block zero during a start, a FIFO byte then is ERROR:40 |
| `packages/n8/native/test/N8CartJobs.test.cpp` | the fake's own case; the regression; the cases that leaned on at-menu probes |
| `packages/n8/native/src/n8/desktop/N8Link.{hpp,cpp}` | `readMenuConfig()` on a `readConfig` turn |
| `pico/n8-host/BoxCartLink.cpp` | the `leftMenu` branch removed |
| `packages/n8/native/test/n8-sd-hwtest.cpp` | `menuguard ... follow`: streams on through the menu, logs every state change |
| `spec/13-subsystems.md`, `spec/07-remaining-work.md` | the rule, the corrected cause, the settled items deleted |

---

### Task 1: The guard's config watch

**Files:**
- Modify: `packages/n8-wire/native/src/transport/N8MenuGuard.hpp`
- Test: `packages/n8/native/test/N8MenuGuard.test.cpp`

- [ ] **Step 1: Replace the three-miss case with the config-watch cases (failing)**

In `N8MenuGuard.test.cpp`, delete the whole `TEST_CASE("menu guard: the menu's 'k' enters the menu, and three
unanswered probes in a row leave it", ...)` and put these in its place. Add `#include <array>` to the includes.

```cpp
namespace {

// A config block as the FPGA hands it over USB (16 bytes at 0x1800020): map_idx first.
using Block = std::array<std::uint8_t, N8MenuGuard::kConfigBytes>;
constexpr Block kPowerOnMenu = {0xFF, 0, 0, 0, 0, 0, 0, 0x80};
constexpr Block kZeros       = {};
constexpr Block kVrc6Game    = {0x18, 0x65, 0x04, 0x8B, 0, 0, 0, 0x9E, 0x14};
constexpr Block kFme7Game    = {0x45, 0x65, 0x02, 0x81, 0x01, 0, 0, 0x8E, 0x14};

// A guard at the menu, entered by the menu's answer to its probe at T0.
N8MenuGuard atMenu() {
    N8MenuGuard g;
    REQUIRE(g.turn(T0, nullptr, true).writeProbe);
    const std::uint8_t k = N8MenuGuard::kAnswer;
    g.hear(&k, 1);
    REQUIRE(g.turn(T0 + 20 * kMs, nullptr, true).enteredMenu);
    return g;
}

// The turn at `at`, the block handed back when it asked for one.
N8MenuGuard::Turn read(N8MenuGuard& g, std::int64_t at, const Block& block) {
    const N8MenuGuard::Turn t = g.turn(at, nullptr, true);
    if (t.readConfig) g.config(at, block.data());
    return t;
}

}  // namespace

TEST_CASE("menu guard: at the menu nothing is written, and the config block is asked for every 20 ms",
          "[n8][menu-guard]") {
    N8MenuGuard g = atMenu();
    // The turn that entered asked nothing more; from here, a read every 20 ms and never a probe, however long.
    int reads = 0;
    for (std::int64_t at = T0 + 21 * kMs; at < T0 + 3000 * kMs; at += 1 * kMs) {
        const N8MenuGuard::Turn t = read(g, at, kPowerOnMenu);
        CHECK_FALSE(t.writeProbe);
        CHECK(t.hold);
        CHECK_FALSE(t.leftMenu);
        if (t.readConfig) ++reads;
    }
    CHECK(reads >= 140);  // 2979 ms at one per 20 ms
    CHECK(reads <= 150);
    CHECK(g.atMenu());
}

TEST_CASE("menu guard: a different game's block leaves the menu once it has held for 500 ms", "[n8][menu-guard]") {
    N8MenuGuard g = atMenu();
    std::int64_t at = T0 + 40 * kMs;
    read(g, at, kPowerOnMenu);  // the reference
    at += 20 * kMs;
    read(g, at, kZeros);  // Start Game: the OS clears the block
    at += 100 * kMs;
    const std::int64_t settledFrom = at;
    N8MenuGuard::Turn t = read(g, at, kVrc6Game);  // the game's block
    CHECK_FALSE(t.leftMenu);
    while (at < settledFrom + 480 * kMs) {
        at += 20 * kMs;
        t = read(g, at, kVrc6Game);
        CHECK_FALSE(t.leftMenu);  // still settling
    }
    for (int i = 0; i < 3 && !t.leftMenu; ++i) {
        at += 20 * kMs;
        t = read(g, at, kVrc6Game);
    }
    CHECK(t.leftMenu);
    CHECK_FALSE(t.hold);
    CHECK_FALSE(g.atMenu());
}

TEST_CASE("menu guard: the same game started again, through zeros to the identical block, leaves the menu",
          "[n8][menu-guard]") {
    // A RESET leaves the block at the game's config, and starting that game again ends on the very same bytes: only the
    // clear in between shows the start.
    N8MenuGuard g = atMenu();
    std::int64_t at = T0 + 40 * kMs;
    read(g, at, kFme7Game);  // the reference: what the RESET left
    at += 20 * kMs;
    read(g, at, kZeros);
    N8MenuGuard::Turn t;
    for (int i = 0; i < 40 && !t.leftMenu; ++i) {
        at += 20 * kMs;
        t = read(g, at, kFme7Game);
    }
    CHECK(t.leftMenu);
    CHECK(at - (T0 + 60 * kMs) >= 500 * kMs);
}

TEST_CASE("menu guard: a block that never changes, or never settles, keeps the guard at the menu", "[n8][menu-guard]") {
    N8MenuGuard g = atMenu();
    std::int64_t at = T0 + 40 * kMs;
    SECTION("unchanged") {
        for (; at < T0 + 5000 * kMs; at += 20 * kMs) CHECK_FALSE(read(g, at, kFme7Game).leftMenu);
    }
    SECTION("back to zeros inside the settle") {
        read(g, at, kPowerOnMenu);
        at += 20 * kMs;
        read(g, at, kZeros);
        for (int round = 0; round < 6; ++round) {  // 400 ms of the game's block, then zeros again: never 500 steady
            for (int i = 0; i < 20; ++i) {
                at += 20 * kMs;
                CHECK_FALSE(read(g, at, kVrc6Game).leftMenu);
            }
            at += 20 * kMs;
            CHECK_FALSE(read(g, at, kZeros).leftMenu);
        }
    }
    SECTION("a block that keeps changing") {
        read(g, at, kPowerOnMenu);
        for (int i = 0; i < 100; ++i) {
            at += 20 * kMs;
            CHECK_FALSE(read(g, at, i % 2 ? kVrc6Game : kFme7Game).leftMenu);
        }
    }
    CHECK(g.atMenu());
}

TEST_CASE("menu guard: a read never handed back changes nothing", "[n8][menu-guard]") {
    N8MenuGuard g = atMenu();
    std::int64_t at = T0 + 40 * kMs;
    read(g, at, kPowerOnMenu);
    at += 20 * kMs;
    read(g, at, kZeros);
    // Every read from here fails: the guard asks on its cadence and decides nothing.
    for (int i = 0; i < 100; ++i) {
        at += 20 * kMs;
        const N8MenuGuard::Turn t = g.turn(at, nullptr, true);
        CHECK(t.readConfig);
        CHECK_FALSE(t.leftMenu);
    }
    CHECK(g.atMenu());
}

TEST_CASE("menu guard: entering the menu again starts from a fresh reference", "[n8][menu-guard]") {
    N8MenuGuard g = atMenu();
    std::int64_t at = T0 + 40 * kMs;
    read(g, at, kPowerOnMenu);
    at += 20 * kMs;
    read(g, at, kZeros);  // a start begun...
    CHECK(g.report(false));  // ...and another probe said a game (a submission's), then the menu again
    CHECK(g.report(true));
    // The first block after the new entry is the reference: the game's block, steady, is not a change from it.
    for (int i = 0; i < 60; ++i) {
        at += 20 * kMs;
        CHECK_FALSE(read(g, at, kVrc6Game).leftMenu);
    }
    CHECK(g.atMenu());
}
```

Also update the file's header comment: replace "the menu's 'k' entering the menu and three misses in a row leaving it"
with "the menu's 'k' entering the menu, where nothing is written and the config block's start pattern leaves it".

And in `TEST_CASE("menu guard: the menu's 'k' heard while a framed window is stalled enters the menu, the hold ending
in it", ...)`, replace everything from the comment "// The stall is forgotten: at the menu the cadence is the menu's,
..." to the end of the case with:

```cpp
    // The stall is forgotten: at the menu nothing is written, not a probe and not a re-send, and the stalled window is
    // no longer asked about; the config block is read instead.
    bool askedConfig = false;
    for (std::int64_t at = T0 + 121 * kMs; at < T0 + 1500 * kMs; at += 5 * kMs) {
        t = g.turn(at, &f.s, true);
        CHECK(t.hold);
        CHECK_FALSE(t.writeProbe);
        CHECK_FALSE(t.enteredMenu);
        CHECK_FALSE(t.reprobeLink);
        askedConfig = askedConfig || t.readConfig;
    }
    CHECK(askedConfig);
}
```

- [ ] **Step 2: Run them to see them fail**

Run: `cmake --build build --target retroplug-n8-test -j$(nproc)`
Expected: compile errors (`kConfigBytes`, `config`, `readConfig` do not exist).

- [ ] **Step 3: Implement the guard's watch**

In `N8MenuGuard.hpp`:

1. Add `#include <cstring>` after `#include <cstdint>`.
2. Replace the "at the menu, every 250 ms whatever the link ..." bullet in the top comment with:

```cpp
// - at the menu, never. The N8 OS reads its MCU's status replies from the FIFO the host writes into (edn8-pro-pub
//   edio/everdrive.c, ed_check_status), so a byte arriving while it works - a game started from its browser above all
//   - is read as a status and stops it at ERROR:40 (measured: docs/n8-menu-guard-config-watch-design.md). The turn
//   asks for the FPGA config block instead (readConfig, every 20 ms), which the MCU answers without the FIFO: every
//   start from the browser clears the block, then writes the game's, so a block changed from the one read on
//   entering (all zeros always counts), then the same non-zero block for half a second, is a game, and the menu is
//   left (the driver probes the link again). A start the reads miss leaves the guard at the menu until a reconnect.
```

3. Replace the `kLeaveMisses` constant and its comment with (keep `kStallMisses` and its half of the comment):

```cpp
    // How often the config block is read at the menu, how long a changed block must read the same before the menu is
    // left (the OS is still at work when the block changes, and the link probe that leaving sets off is a FIFO write),
    // and the block's size (16 bytes at 0x1800020).
    static constexpr std::int64_t kConfigNs    = 20'000'000;
    static constexpr std::int64_t kSettleNs    = 500'000'000;
    static constexpr std::size_t  kConfigBytes = 16;
    // How many stall probes a framed link may hear nothing at all to before it gives the window up.
    static constexpr int kStallMisses = 2;
```

4. In `struct Turn`, add after `writeProbe`:

```cpp
        bool readConfig  = false;  // at the menu: read the FPGA config block and hand it to config()
```

and change `leftMenu`'s comment to `// a game started at the menu (the config block): its link is probed again`.

5. Add after `hear()`:

```cpp
    // The config block a readConfig turn asked for (kConfigBytes), read at `now`. A read that failed is not handed
    // back: the guard asks again on its cadence, and nothing else changes.
    void config(std::int64_t now, const std::uint8_t* block) {
        if (!atMenu_) return;
        if (!haveRef_) {
            std::memcpy(ref_, block, kConfigBytes);
            haveRef_ = true;
            return;
        }
        bool zero = true;
        for (std::size_t i = 0; i < kConfigBytes && zero; ++i) zero = block[i] == 0;
        if (zero || std::memcmp(block, ref_, kConfigBytes) != 0) changed_ = true;
        if (!changed_) return;
        if (zero) {  // the OS is between clearing the block and writing the game's
            settleFromNs_ = 0;
            return;
        }
        if (settleFromNs_ == 0 || std::memcmp(block, settle_, kConfigBytes) != 0) {
            std::memcpy(settle_, block, kConfigBytes);
            settleFromNs_ = now;
            return;
        }
        if (now - settleFromNs_ >= kSettleNs) leaveDue_ = true;
    }
```

6. In `report()`, replace `if (menu) menuMisses_ = 0;` with a fresh watch on every change:

```cpp
        haveRef_      = false;
        changed_      = false;
        leaveDue_     = false;
        settleFromNs_ = 0;
        configDueNs_  = 0;
```

7. In `turn()`, keep the `menuAnswered_` block but drop its `menuMisses_ = 0;` line, and replace the whole
   `if (atMenu_) { ... }` block with:

```cpp
        if (atMenu_) {
            if (leaveDue_) {
                t.leftMenu = report(false);
                return t;
            }
            // The turn that entered asks nothing more: its probe's answer may still be on its way to a read.
            if (!t.enteredMenu && now >= configDueNs_) {
                configDueNs_ = now + kConfigNs;
                t.readConfig = true;
            }
            t.hold = true;  // nothing goes out while it says menu, not even a probe
            return t;
        }
```

8. In the private members: delete `menuMisses_` and its comment line's mention ("Probes in a row the menu left
   unanswered (it is left at kLeaveMisses), and" becomes "Stall probes in a row that heard nothing at all (the link is
   given up at kStallMisses)"), and add:

```cpp
    // The config watch at the menu: the block read on entering, whether one has differed from it since, the block a
    // settle is timing and since when (0: none), when the next read is due, and a settle that has run its course.
    std::uint8_t  ref_[kConfigBytes]    = {};
    std::uint8_t  settle_[kConfigBytes] = {};
    bool          haveRef_      = false;
    bool          changed_      = false;
    bool          leaveDue_     = false;
    std::int64_t  settleFromNs_ = 0;
    std::int64_t  configDueNs_  = 0;
```

- [ ] **Step 4: Run the guard's cases**

Run: `cmake --build build --target retroplug-n8-test -j$(nproc) && build/bin/retroplug-n8-test "menu guard:*"`
Expected: every `menu guard:` case (this file's) passes. N8Link's own `[menu-guard]` cases in N8CartJobs.test.cpp may
fail until Task 3, which owns them: note which, and move on.

- [ ] **Step 5: Commit**

```bash
git add packages/n8-wire/native/src/transport/N8MenuGuard.hpp packages/n8/native/test/N8MenuGuard.test.cpp
git commit -m "n8-wire: the menu guard writes nothing at the menu and leaves it on the config block

A probe landing while the N8 OS works is read as its MCU status (ERROR:40), and a game started from
the cart's browser is a long run of such work. At the menu the guard now asks for the FPGA config
block every 20 ms; every browser start clears it before writing the game's, so a changed block that
then reads the same for 500 ms is a game. The three-miss leave goes."
```

---

### Task 2: FakeN8 models a game started from the browser

**Files:**
- Modify: `packages/n8/native/test/FakeN8.hpp`
- Test: `packages/n8/native/test/N8CartJobs.test.cpp`

- [ ] **Step 1: The fake's own case (failing)**

Append to `N8CartJobs.test.cpp`, after the `"FakeN8: a console reset drops what the game owed, ..."` case:

```cpp
TEST_CASE("FakeN8: a game started from the browser clears the config block, and a FIFO byte during it is ERROR:40",
          "[n8][fake]") {
    using retroplug::test::FakeN8Port;
    FakeN8     dev;
    FakeN8Port port(dev);
    Edio       edio(port);
    std::uint8_t cfg[16] = {};
    SECTION("a clean start") {
        dev.startFromBrowser(24, 80);
        REQUIRE(edio.memRD(Edio::ADDR_CFG, cfg, sizeof cfg).ok());
        CHECK(cfg[0] == 0);  // cleared while the OS loads the game
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        REQUIRE(edio.memRD(Edio::ADDR_CFG, cfg, sizeof cfg).ok());
        CHECK(cfg[0] == 24);
        std::lock_guard<std::mutex> l(dev.mu);
        CHECK_FALSE(dev.menu);
        CHECK_FALSE(dev.error40);
    }
    SECTION("a byte written during the start") {
        dev.startFromBrowser(24, 80);
        const std::uint8_t probe[4] = {0xF0, 0x2A, 0x74, 0xF7};
        REQUIRE(edio.fifoWR(probe, sizeof probe).ok());
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        REQUIRE(edio.memRD(Edio::ADDR_CFG, cfg, sizeof cfg).ok());
        std::lock_guard<std::mutex> l(dev.mu);
        CHECK(dev.error40);
        CHECK(dev.menu);  // the game never started
    }
}
```

- [ ] **Step 2: Run it to see it fail**

Run: `cmake --build build --target retroplug-n8-test -j$(nproc)`
Expected: compile error, `startFromBrowser` is not a member of `FakeN8`.

- [ ] **Step 3: Implement the model**

In `FakeN8.hpp`:

1. In the top comment, after "...it can be silent for a boot time)," add: "and a game started from the cart's own
   browser (startFromBrowser: the config block reads zero while the OS loads it, and a FIFO byte arriving then is
   read as one of the OS's MCU status replies, ERROR:40, and the game never starts),".

2. In `struct FakeN8`, after `resetAt`:

```cpp
    // A game started from the cart's own browser (startFromBrowser) and not yet running: the OS has cleared the config
    // block and loads the game until startUntil, then writes the game's block and jumps to it. The OS reads its MCU's
    // status replies from the FIFO the host writes (edn8-pro-pub edio/everdrive.c, ed_check_status), so a host byte
    // arriving meanwhile is taken for one: ERROR:40, and the game never starts.
    bool                     startingFromBrowser = false;
    std::chrono::steady_clock::time_point startUntil{};
    std::uint16_t            startMapIndex = 0;
```

3. After `resetToMenu()`:

```cpp
    // The user picks a game in the cart's browser and presses Start Game: `startMs` of loading, then the game runs with
    // `mapIndex` in the config block.
    void startFromBrowser(std::uint16_t mapIndex, int startMs = 100) {
        std::lock_guard<std::mutex> l(mu);
        startingFromBrowser = true;
        startUntil          = std::chrono::steady_clock::now() + std::chrono::milliseconds(startMs);
        startMapIndex       = mapIndex;
    }
```

4. In `FakeN8Port`, after `catchUpReset()`:

```cpp
    // A browser start whose load time has passed: the game runs (its link parser fresh), or, with an error on the way,
    // the OS stays on ERROR:40 at the menu.
    void catchUpStart() {
        if (!dev_.startingFromBrowser || std::chrono::steady_clock::now() < dev_.startUntil) return;
        dev_.startingFromBrowser = false;
        if (dev_.error40) return;
        dev_.menu      = false;
        dev_.resetMenu = false;
        dev_.cfg[0]    = static_cast<std::uint8_t>(dev_.startMapIndex);
        dev_.cfg[2]    = static_cast<std::uint8_t>((dev_.startMapIndex >> 8) << 4);
        menu_.clear();
        msg_.clear();
        link_    = Idle;
        linkOn_  = false;
        ackOwed_ = false;
        acks_.clear();
    }
```

5. Call it: in `read()`, right after `catchUpReset();`, add `catchUpStart();`. In `parse()`, as its first statement,
   add `catchUpStart();` (parse runs under the lock for every write).

6. In the memory-read helper (the function holding `if (dev_.menu && !dev_.resetMenu && addr == kCfg) return 0xFF;`),
   add before that line:

```cpp
        if (dev_.startingFromBrowser && addr >= kCfg && addr < kCfgEnd) return 0;  // cleared while the OS loads
```

7. In `fifoByte()`, as its first statement:

```cpp
        if (dev_.menu && dev_.startingFromBrowser) {  // taken for an MCU status reply mid-start
            dev_.error40 = true;
            return;
        }
```

- [ ] **Step 4: Run the fake's case, then the whole binary**

Run: `cmake --build build --target retroplug-n8-test -j$(nproc) && build/bin/retroplug-n8-test "FakeN8: a game started from the browser*"`
Expected: PASS (both sections).

- [ ] **Step 5: Commit**

```bash
git add packages/n8/native/test/FakeN8.hpp packages/n8/native/test/N8CartJobs.test.cpp
git commit -m "FakeN8: a game started from the cart's browser, and a FIFO byte during it is ERROR:40"
```

---

### Task 3: N8Link reads the block at the menu

**Files:**
- Modify: `packages/n8/native/src/n8/desktop/N8Link.hpp`, `packages/n8/native/src/n8/desktop/N8Link.cpp`
- Test: `packages/n8/native/test/N8CartJobs.test.cpp`

- [ ] **Step 1: The regression (failing)**

Append to `N8CartJobs.test.cpp` (after the `"leaving the menu re-probes the link, ..."` case):

```cpp
TEST_CASE("a game started from the cart's browser while the link waits at the menu starts clean, and is streamed",
          "[n8][menu-guard]") {
    // The N8 OS takes a host byte that arrives while it starts a game for its MCU's status and stops at ERROR:40 (2 of
    // 2 on the lab's cart with the guard probing the menu). At the menu the link writes nothing and leaves on the
    // config block: cleared by the start, then the game's.
    Rig rig;
    const bool withLink = GENERATE(true, false);
    rig.dev.linkRom = withLink;  // BlipToaster, or a game without the link
    rig.connectGame();
    rig.dev.resetToMenu();
    rig.push({0x90, 0x3C, 0x7F});  // a stream for the reset to meet
    REQUIRE(waitUntil([&] { return rig.host.link().atMenu(); }));
    std::this_thread::sleep_for(std::chrono::milliseconds(300));  // the menu idles under the guard
    const std::size_t writes = rig.dev.count("fifo:");
    rig.dev.startFromBrowser(69, 150);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    {
        std::lock_guard<std::mutex> l(rig.dev.mu);
        CHECK_FALSE(rig.dev.error40);
        CHECK_FALSE(rig.dev.menu);
    }
    CHECK(rig.dev.count("fifo:") == writes);  // nothing at all reached the FIFO while it started
    REQUIRE(waitUntil([&] { return !rig.host.link().atMenu(); }));
    if (withLink) {
        REQUIRE(waitUntil([&] { return rig.host.link().reliable(); }));  // the link probe that leaving set off
    } else {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));  // the link probe's 100 ms, unanswered
        REQUIRE_FALSE(rig.host.link().reliable());
    }
    std::size_t applied = 0;
    {
        std::lock_guard<std::mutex> l(rig.dev.mu);
        applied = rig.dev.applied.size();
    }
    rig.push({0x90, 0x3E, 0x7F});
    if (withLink) {
        REQUIRE(waitUntil([&] {
            std::lock_guard<std::mutex> l(rig.dev.mu);
            return rig.dev.applied.size() == applied + 1;
        }));
    } else {
        REQUIRE(waitUntil([&] { return rig.dev.countContaining("90 3E 7F") >= 1; }));  // raw, its status written
    }
}
```

If `GENERATE` is not yet used in the file, add `#include <catch2/generators/catch_generators.hpp>` to its includes.

- [ ] **Step 2: Run it to see it fail**

Run: `cmake --build build --target retroplug-n8-test -j$(nproc) && build/bin/retroplug-n8-test "a game started from the cart's browser while the link waits*"`
Expected: FAIL. Before this task the link turns `readConfig` into nothing, so it never leaves the menu (the
`waitUntil(!atMenu)` fails); on the old guard it was the `count("fifo:") == writes` check (its probes).

- [ ] **Step 3: Implement the read**

In `N8Link.hpp`, beside `menuProbeAwaited`'s declaration in the private section:

```cpp
    // serial thread: the guard's config read at the menu (a readConfig turn), on a turn quiet for a read; a failed read
    // is reported at most once a second and decides nothing
    void        readMenuConfig(std::int64_t now);
```

and a member beside `controlRetryAtNs_`:

```cpp
    std::int64_t configFailLogAtNs_ = 0;  // serial thread: when a failed config read at the menu may be reported again
```

In `N8Link.cpp`, in `menuHold()`, before `if (turn.writeProbe) writeMenuProbe(now);`:

```cpp
    if (turn.readConfig) readMenuConfig(now);
```

and add after `menuProbeAwaited()`:

```cpp
// The guard watches the FPGA config block at the menu, where a write of ours could be read by the working OS as its
// MCU's status (ERROR:40): a read is the MCU's to answer and never reaches the FIFO. Only on a turn quiet for a read, so
// a late 'k' cannot land inside the reply; a turn that is not leaves the read to the guard's next cadence.
void N8Link::readMenuConfig(std::int64_t now) {
    if (!quietForRead(now)) return;
    std::uint8_t           block[rp::N8MenuGuard::kConfigBytes];
    const rp::n8::Status   s = edio_->memRD(rp::n8::Edio::ADDR_CFG, block, sizeof block);
    if (s.ok()) {
        guard_.config(now, block);
        return;
    }
    if (now < configFailLogAtNs_) return;
    configFailLogAtNs_ = now + 1'000'000'000;
    std::fprintf(stderr, "[n8link] a config read at the menu failed: %s\n", failureText(s, *edio_).c_str());
}
```

(`failureText(const Status&, Edio&)` is `PortTransport.hpp`'s; `N8Link.cpp` already includes it through `N8Link.hpp`.
If the compiler cannot find it, add `#include "n8/desktop/PortTransport.hpp"`.)

- [ ] **Step 4: Run the regression, then every guard case of the link**

Run: `cmake --build build --target retroplug-n8-test -j$(nproc) && build/bin/retroplug-n8-test "[menu-guard]"`
Expected: the regression passes (both generator values). Other `[menu-guard]` cases that leaned on the at-menu probe
fail now; fix each in Step 5.

- [ ] **Step 5: Bring the cases that leaned on at-menu probes to the new rule**

The rule each must now state: at the menu the link writes nothing, not even a probe; it leaves only on the config
block. Expected changes (fix any other failing `[menu-guard]` case by the same rule, never by loosening a check on what
reaches the menu):

- `"a menu whose answer comes late is still the menu: one unanswered probe does not leave it"`: rename to
  `"a menu is never left without a change in the config block, however late its answers"`, keep the body, and add
  after the loop: `CHECK(rig.dev.countExact("fifo:F0 2A 74 F7") == probesAtEntry);` where
  `const std::size_t probesAtEntry = rig.dev.countExact("fifo:F0 2A 74 F7");` is taken right after the
  `REQUIRE(waitUntil(... atMenu()))`. Its comment becomes: "At the menu the guard writes nothing: a byte arriving while
  the OS works is taken for its MCU status (ERROR:40). However late or absent its answers, the menu is left only on the
  config block."
- `"leaving the menu re-probes the link, so a game the user starts by hand streams framed"`: it flips `menu` and
  `cfg[0]` by hand. It should pass as is (the block reads 0xFF at a power-on menu, then 69, held 500 ms); if it times
  out, raise its `waitUntil` to `waitUntil(..., 5000)`, and change the comment "The guard's next probe goes unanswered,
  and leaving the menu runs the link probe again" to "The guard sees the config block change and settle, and leaving the
  menu runs the link probe again".
- Any case that counts `fifo:F0 2A 74 F7` writes while the cart sits at its menu and expects more than the entering
  probe: it now expects none after the entry.

Run: `build/bin/retroplug-n8-test "[menu-guard]"` until it passes, then `pnpm test:n8`. Expected: exit 0.

- [ ] **Step 6: Commit**

```bash
git add packages/n8/native/src/n8/desktop/N8Link.hpp packages/n8/native/src/n8/desktop/N8Link.cpp packages/n8/native/test/N8CartJobs.test.cpp
git commit -m "N8Link: at the menu, read the config block for the guard and write nothing

A game started from the cart's browser while the link waited at the menu ended at ERROR:40 on the
lab's cart. The serial thread now answers the guard's readConfig turns with a memRD of the block on
a quiet turn; the cases that leaned on probes at the menu state the new rule."
```

---

### Task 4: The box loses its unreachable leave

**Files:**
- Modify: `pico/n8-host/BoxCartLink.cpp`

- [ ] **Step 1: Remove the branch**

In `BoxCartLink::drain()`, replace

```cpp
    if (turn.reprobeLink || (turn.leftMenu && linkAllowed_)) {
        // A stalled window two probes heard nothing to (a game without the link), or a game the user started from the
        // menu: the link is probed again at the next drain, and a game with it re-syncs framed.
        if (turn.reprobeLink) discard();
```

with

```cpp
    // The guard never turns here at the menu (the main loop disconnects the link the moment it enters, and boots the
    // staged ROM), so it never asks for the config block and never leaves the menu: only a stalled window comes back.
    if (turn.reprobeLink) {
        // A stalled window two probes heard nothing to (a game without the link): the link is probed again at the next
        // drain, and a game with it re-syncs framed.
        discard();
```

keeping the rest of the block (`sender_ = nullptr; probe_ = Probe::Pending; return; }`) as it is.

- [ ] **Step 2: Run the box's cases**

Run: `cmake --build build --target retroplug-device-mode-test -j$(nproc) && build/bin/retroplug-device-mode-test "[box-link]"`
Expected: all pass. If a case drove the box's guard at the menu and expected a leave, it described a path the main
loop never takes: report it rather than changing the main loop.

- [ ] **Step 3: Commit**

```bash
git add pico/n8-host/BoxCartLink.cpp
git commit -m "box: the cart link's leave-the-menu branch goes, as its guard never turns at the menu"
```

---

### Task 5: menuguard follows the link through the menu

**Files:**
- Modify: `packages/n8/native/test/n8-sd-hwtest.cpp` (the `menuguard` op)

- [ ] **Step 1: Add the `follow` flag**

In the `menuguard` op:

1. Replace the arguments' parse with:

```cpp
        const int         seconds = static_cast<int>(std::strtol(argv[2], nullptr, 0));
        // [raw]: hold the link raw; [follow]: stream on through the menu for the whole run, logging every change of
        // where the cart runs and how it is streamed, rather than stopping 5 s after the guard trips (games started
        // from the cart's own browser, the config watch's hardware check).
        bool        raw = false, follow = false;
        std::string gport;
        for (int i = 3; i < argc; ++i) {
            const std::string a = argv[i];
            if (a == "raw") raw = true;
            else if (a == "follow") follow = true;
            else gport = a;
        }
        if (gport.empty()) gport = findN8Port();
        if (seconds <= 0 || gport.empty()) {
            std::fprintf(stderr, "usage: %s menuguard <seconds> [port] [raw] [follow] (no Everdrive N8 found?)\n",
                         argv[0]);
            return 2;
        }
```

2. Add `bool wasReliable = link.reliable();` beside `wasMenu`, and in the loop after the `isConnected` check:

```cpp
            if (link.reliable() != wasReliable) {
                wasReliable = link.reliable();
                say(wasReliable ? "link: framed" : "link: raw");
            }
```

3. Change the loop condition to
   `while (ms() < seconds * 1000.0 && (follow || tripMs < 0 || ms() < tripMs + 5000.0))`.

4. Change the final `return` to `return follow ? 0 : (tripMs >= 0 && after == 0 ? 0 : 1);`.

5. Update the usage comment line at the top of the file to
   `//   retroplug-n8-hwtest menuguard <seconds> [port] [raw] [follow] # the menu guard, streaming while a person presses RESET`.

- [ ] **Step 2: Build it**

Run: `cmake --build build --target retroplug-n8-hwtest -j$(nproc)`
Expected: builds with no warning.

- [ ] **Step 3: Commit**

```bash
git add packages/n8/native/test/n8-sd-hwtest.cpp
git commit -m "n8-hwtest: menuguard follow streams on through the menu and logs every change"
```

---

### Task 6: The docs

**Files:**
- Modify: `spec/13-subsystems.md`, `spec/07-remaining-work.md`

- [ ] **Step 1: spec/13, the rule**

In "A cart at its menu is not streamed to" (line ~126 on), replace the clause

"and at the menu every 250 ms whatever the link, leaving it when three probes in a row go unanswered for 100 ms each (a
game booted from the cart's own browser; one miss is only a slow turn of the menu's), which re-runs the link probe."

with

"and at the menu never: there the guard reads the FPGA config block every 20 ms instead, which the cart's MCU answers
without the FIFO, and leaves the menu (re-running the link probe) once the block has changed from the one read on
entering, or read all zeros, and then read the same non-zero bytes for 500 ms. Every game started from the cart's own
browser clears the block before writing the game's, the same game started again after a RESET included
([the config watch](../docs/n8-menu-guard-config-watch-design.md)); a start the reads miss leaves the guard at the menu
until a reconnect."

- [ ] **Step 2: spec/13, what is left exposed and the raw-link limit**

In "What is left exposed":

- Replace "At an OS that is up, only a `*` followed by a byte the menu takes as a command (a GM hi-hat, `9n 2A 73`, is
  `*s` and starts the app) or rejects (ERROR:40, which stops the OS) does harm; a stream without `0x2A` is harmless
  there." with "At an OS that is up and idle, a `*` followed by a byte the menu takes as a command (a GM hi-hat,
  `9n 2A 73`, is `*s` and starts the app) does harm. While the OS works (a folder load, a game start, its boot) any
  byte can: it reads its MCU's status replies from the FIFO the host writes (edn8-pro-pub `edio/everdrive.c`,
  `ed_check_status`), so a host byte there is taken for one and stops it at ERROR:40."
- Delete the sentences from "A menu that is busy and answers nothing for three probes in a row" through "A probe before
  every chunk is the only full fix ([spec/07](07-remaining-work.md#the-n8-menus-residual-window))."

In "The raw-link limit", after "breaks it", add: "(the booting OS reads its MCU's status from the FIFO, and a note byte
there is taken for one)". After "That is what was measured." replace the rest of that paragraph up to "`retroplug-n8-hwtest
bootwindow` measures it." with: "A RESET during a pause in the stream is protected too: the next note's frames and the
stall's probes reached the booting OS within about 370 ms of the RESET, 6 of 6 clean (one with 10% test loss). A menu at
ERROR:40 still answers `*t` with `k`, and a second RESET brings back a clean menu. `retroplug-n8-hwtest bootwindow` and
`pausereset` measure it."

- [ ] **Step 3: spec/07**

- Delete the checklist line `- [ ] [The N8 menu's residual window](#the-n8-menus-residual-window)` and the whole
  `### The N8 menu's residual window` entry.
- Replace the body of `### The N8 menu's back-channel` with:

"The menu guard tells a menu from a game by the menu's answer to its probe, a `k` on the back-channel outside an ack,
and at the menu writes nothing and watches the FPGA config block
([spec/13](13-subsystems.md#the-everdrive-n8-bridge), [the config watch](../docs/n8-menu-guard-config-watch-design.md),
whose section 1 has what the lab's cart showed: an idle menu silent and answering in 7-13 ms, browsing harmless, a
probe during a browser start ERROR:40, the block's clear on every start). Still owed on the cart: the config watch's
acceptance (its section 5: the block's clear timed at 10 ms on three ROM sizes, then five browser starts each of
BlipToaster and Akumajou Densetsu under `retroplug-n8-hwtest menuguard ... follow`, framed and raw, no ERROR); a job
submitted right after a power-cycle or a RESET to the menu, accepted and promptly; and that the probe a raw stream
carries four times a second costs a running BlipToaster nothing (`retroplug-n8-hwtest timing busy --raw --guard`
against the same run without `--guard`) and stalls nothing."

- In spec/13, the link `[spec/07](07-remaining-work.md#the-n8-menus-residual-window)` was deleted with its sentence in
  Step 2; check none is left: `grep -n residual-window spec/*.md` prints nothing.

- [ ] **Step 4: Check the docs**

Run: `grep -n 'residual-window\|three probes in a row\|kLeaveMisses' spec/*.md packages/n8-wire packages/n8/native -r`
Expected: nothing, except history in `docs/*-plan.md` files (not searched).
Run: `grep -cP '\x{2014}' spec/13-subsystems.md spec/07-remaining-work.md` (the em dash, by code point) Expected: `0` for both.

- [ ] **Step 5: Commit**

```bash
git add spec/13-subsystems.md spec/07-remaining-work.md
git commit -m "spec: the menu guard watches the config block at the menu; the settled N8 checks go

The cause of the raw-link ERROR:40 is the OS reading its MCU status from the host's FIFO, not a '*'
pair. The residual-window entry goes: the busy menu's false leave is gone with the at-menu probe, the
paused framed RESET ran clean 6 of 6, and the cause is the OS's whatever the ROM."
```

---

### Task 7: Hardware gate and acceptance (needs the user at the console)

Run by the controller, not a subagent. The N8 is on the PC as `/dev/ttyACM1` (re-run `.devcontainer/hw-usb-perms.sh`
if the node is missing); the console is the NTSC Famicom; grab frames with
`build/bin/retroplug-cli grab-frame <png> --size 720x480 --standard NTSC --frames 15`. Navigate the menu one step at a
time, a frame grab between steps. Record everything in `.superpowers/menucheck/results.md`.

- [ ] **Step 1: The gate.** `build/bin/retroplug-n8-hwtest cfgwatch 300 10 /dev/ttyACM1` while the user starts, from a
  power-on menu, three ROMs of different sizes (one at a time, a power-cycle between): BlipToaster
  (`usb-games/pico/bliptoaster.nes`, 288 KB), Akumajou Densetsu (384 KB) and the smallest `.nes` on the card. Each
  start's all-zero block must last well past 20 ms (at least two 10 ms reads). If any is shorter, stop and lower
  `kConfigNs` before Step 2.
- [ ] **Step 2: Acceptance.** Boot BlipToaster (`n8-load --sd-path usb-games/pico/bliptoaster.nes`), run
  `build/bin/retroplug-n8-hwtest menuguard 600 /dev/ttyACM1 follow`, then five rounds of: RESET, the guard at the
  menu, start BlipToaster from the browser. Each must log `left the menu` then `link: framed`, and play (a frame grab
  shows the monitor). Then five rounds starting Akumajou Densetsu: `left the menu`, the link raw, the game running.
  No ERROR on screen in any round.
- [ ] **Step 3:** On a pass, delete the acceptance clause from spec/07's back-channel entry, update the memory file
  `project_n8_menu_probe_breaks_game_start.md`, and commit:
  `git commit -m "spec/07: the menu guard's config watch passed on the cart"`.
