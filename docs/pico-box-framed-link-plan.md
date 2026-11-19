# The Pico box's framed link and menu guard Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The Pico box streams to the cart through the framed link and the menu guard the desktop's `N8Link` runs,
from one shared implementation, so an NTSC console's DMC fetch loses nothing it cannot repair and a console RESET
comes back playing the staged ROM instead of streaming into the booting N8 OS.
[pico-box-framed-link-design.md](pico-box-framed-link-design.md) is the approved design this plan builds; it shrinks
[spec/07](../spec/07-remaining-work.md)'s "The box's cart stream has no framing and no menu guard" to its pending
hardware acceptance.

**Architecture:** Six tasks. Task 1 makes `rp::N8ReliableSender` (n8-wire) allocate nothing: its window becomes a ring
of 48 frames held inline and its emitter a template parameter, every byte it writes unchanged. Task 2 moves the menu
guard's decisions out of `N8Link::menuHold` into `rp::N8MenuGuard` (n8-wire, header-only, no I/O, no clock, no
allocation), with the back-channel scan beside them, and makes `N8Link` its driver, behaving exactly as before. Task 3
gives the box's `BoxCartLink` a back-channel read, the sender and the guard: a non-blocking link probe at a connect,
framed or raw drains (a framed one never past the window less one outstanding), `atMenu()`, `quietForRead()` and
the counters, held by `retroplug-device-mode-test` cases over FakeN8, a scripted cart and the link's ROM model. Task
4 wires it into the firmware (`main.cpp`'s boot on the menu's answer, the Edio reads gated on `quietForRead()`, no
connect without a running plan, the `N8_BOX_LINK` knob, the `[stats]` fields, the latency trace's back-channel read)
and records the image's sizes. Task 5 gives `retroplug-n8-hwtest timing` its `--midi` mode for the
latency runs (built, never run). Task 6 is the documents.

**Tech Stack:** C++20, Catch2 (`retroplug-n8-test`, `retroplug-device-mode-test`, `retroplug-mesen-test`,
`retroplug-engine-graph-test`, and the TSan build of `retroplug-n8-test` in `build-tsan`), the N8 pack's
Edio/N8Link/N8Host over `FakeN8`, the repo's runners (`pnpm test:n8`, `pnpm test:native`), the parity baseline
(`pnpm test:native parity`), the firmware build (`pnpm device:box`, the link trap), `retroplug-n8-hwtest` (compiled
only), and `pnpm check:docs`. No TypeScript changes.

**Spec:** [pico-box-framed-link-design.md](pico-box-framed-link-design.md). Its section 7's hardware acceptance (the
rig rewired for the box) and section 6's latency runs are the controller's, after this plan lands; so are the design's
as-built and execution sections.

**Provenance:** written at `7fa0d1e17` by reading the tree, with every signature it calls read from the header that
declares it. Nothing here was built or run while it was written (real hardware hangs off this container, and the
planning session ran nothing), so the red and green runs below are the code's first builds: an expected failure is
quoted as its kind (a compile error naming the missing header, a CHECK and its expansion), not as a count, and a
passing run is "exit 0 with Catch2's summary showing no failure". A step whose run disagrees with what is written here
is a finding: stop, say what was seen, and fix the plan's code rather than the test's expectation.

**How to apply a step's code.** A new or wholly rewritten file is given whole: write it with the editor. An edit inside
an existing file is given as an **old** block and a **new** block: the old block is the file's text at `7fa0d1e17`
(after the earlier tasks' edits, where they touch the same file), byte for byte, so the editor's exact replace finds
it once; replace it with the new block. Line numbers are the file at `7fa0d1e17`.

**Submodules.** No task touches one: `deps/sameboy` is dirty from the configure and no step touches it.

**Hardware.** A real NES with an Everdrive N8 Pro, a Zoom L6, a capture card, a CH345 MIDI cable and other MIDI
interfaces hang off this container, so nothing in this plan may open one. Every command below that starts a Catch2
binary, a `pnpm` suite or a build is written under the guarded wrapper, which preloads the tracked device guard
([tools/sdl-audio-guard](../tools/sdl-audio-guard)), self-tests it and refuses `/dev/snd`, `/dev/video*`,
`/dev/ttyACM*`, `/dev/ttyUSB*`, `/dev/serial/*`, `/dev/hidraw*` and `/dev/bus/usb/*` before the syscall:

```
timeout -k 10 <seconds> /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh <command...>
```

The wrapper is a work file (`.superpowers/mgplan/guarded.sh`, gitignored); it takes any command and returns its exit
code, printing what the guard refused, if anything. Use 300 for one test binary, 1800 for a build, a whole suite or
`pnpm device:box`. A refusal it prints is a finding: stop and find what tried to open a device. If a run hits its
timeout, stop and find why before running anything else. The box's cases reach the cart only through `FakeN8`, a
scripted writer and the ROM model; the N8 tests only through `FakeN8` and fake serial transports.
`retroplug-n8-hwtest` is built in tasks 2 and 5 and NEVER run: it opens the real cart, and `timing` opens the L6 and,
with `--midi`, the CH345. Never run `loop.sh`, `stress.sh`, `faults.sh`, `openocd`, `retroplug-sdl`, `retroplug-jack`,
`jackd`, any `sdl:*` or `reaper:*` script, or the verification kits; never flash anything. `pnpm device:box` builds
the firmware and flashes nothing. `/workspaces/bliptoaster` is read, never written.

**Where this plan decides what the design leaves open, or departs from it**, each for the reason given:

- **The sender's no-allocation case lives in `retroplug-device-mode-test` (`BoxHost.test.cpp`), not in
  `N8ReliableLink.test.cpp`** (design section 5, item 1). `retroplug-n8-test` deliberately links no replaced
  allocator ([packages/testing/native/CMakeLists.txt](../packages/testing/native/CMakeLists.txt): "so a test that only
  needs a header does not silently run under a replaced allocator ... which is what `retroplug-n8-test` used to do"),
  and the device-mode binary already links the counting allocator `BoxHost.test.cpp` uses. The sender's existing cases
  (in both `N8ReliableLink.test.cpp` files) run unedited.
- **The window is `std::array<Frame, 48>` with a head and a size; the emitter is `template <class EmitFn> ... const
  EmitFn& emit`.** The `Emit` alias goes: nothing outside the header names it (every caller passes a lambda).
- **`N8MenuGuard` keeps `N8Link`'s names public where N8Host uses them**: `N8Link::kMenuProbe` (a copy of the guard's
  bytes), `N8Link::kMenuAnswer` and `N8Link::BackChannelScan` (an alias of the guard's nested `Scan`). The guard takes
  the turn's time as int64 nanoseconds that are never 0, since 0 is its "no probe awaited", as `N8Link`'s was (a
  steady clock is never 0; the box's clock is microseconds since its boot, read seconds after it, times 1000).
- **The box's link gets its 64-bit time from its `Clock`**, which gains `monotonicUs` (main.cpp passes its existing
  `clock_us`, `time_us_64`), and **its back-channel from its `Writer`**, which gains `read` (main.cpp's `usb.read(data,
  size, 0)`: one USB pump). **The link probe and the guard run only when both are present.** Every `Writer` and
  `Clock` built before (no `read`, no `monotonicUs`) keeps the box's old raw path byte for byte, which is what keeps
  the existing `[box]` cases and the device-mode whole-pass case unedited and green.
- **The sender lives in `Storage` as raw aligned bytes, made by placement new when the probe's sync is acknowledged
  and forgotten (never destroyed: it is trivially destructible) at the next connect, a re-probe or the menu.**
  `Storage` stays all-zero, so the box's static `cart_ring` stays in `.bss` with no constructor run at boot; a sender
  object in it would have been constant-initialised to its non-zero defaults (`timeoutNs`, `lastAckNs_`) and moved
  about 3.5 KB into `.data`. The guard is a `Storage` member whose every default is 0.
- **The connect's probe never blocks the loop**: what the back-channel already holds is read and dropped without
  waiting (the box's `flushInput` waits for 20 ms of quiet), then `F4 00` goes out and the ack is listened for across
  the passes that follow, for 100 ms. A sync whose write failed is written again at the next drain (the failed write
  also counts toward main.cpp's `write_fail_streak`).
- **One guard turn a drain, at its start**, and a probe that is due goes at the front of the drain's first write. A
  drain always starts between two messages (a raw SysEx longer than a write is split only inside one drain, which
  finishes it), and every message the box writes carries its own status byte, so the design's "the probe waits for the
  next boundary" never has to wait on the box.
- **Frames are marked sent (`onSent`) with the clock read just before their write**, whether it succeeds, as
  `N8Link::writeFramed` marks them before its write; the re-sends use the turn's own time, as `N8Link` does.
- **Counting.** `notReady` takes the ring and the window's unacknowledged messages when the menu answers, at a
  re-probe and at a disconnect (design section 3: "counted `notReady`"; the window's were counted `forwarded` when
  written, and stay so). `unframed` counts a SysEx past 40 bytes and any message the sender drops (a System Common
  message, which the plan's policy drops first). `writes` counts every FIFO write: the sync, the probes and re-sends
  too. `resends` and `resent` are the sender's, summed across connects (the sender is made anew at each).
- **Flow control on the framed drain: no more than `kWindow - 1` frames outstanding** (an addition to design section
  3, step 4; the controller adds it to the design's as-built). Acks are read only at a drain's start, so a backlog
  framed to empty in one drain (the ring after a 1 s guard hold that ended on an ack, a 64-message block after the
  loop was held) would reach the 49th outstanding frame, where the sender gives its window up and re-syncs: a DMC
  loss inside that drain then loses its frames for good, note-offs included. The drain stops framing at 47
  outstanding and leaves the rest in the ring (queued, refused `full` only at 512 as before) for the next drain,
  which reads the acks first; it costs a pass only under such a backlog. Design section 4's give-up stays for a
  window nothing acknowledges. A task 3 case pushes 100 notes, drains once, and holds the bound and the delivery.
- **The note books count a message the link took and then dropped (the menu's `k`, a re-probe, a disconnect) as
  delivered, not as a refused note-off** (design section 4 says "booked as a refused note-off"). The link's books are
  rp.n8.nes's own, kept from its push answers (so `filtered` stays exact), and the node was told `true` for that push,
  so it owes nothing: books that marked the note-off owed would part from the node's, and a later note-off for that
  key would be counted as the node's own payment (`made`), reading `filtered` one low. After the menu's `k` and a
  disconnect the game holding the note is gone (a booted ROM holds none), which is HEAD's disconnect rule and the
  desktop's. The re-probe is the one case where the same game runs on: a note-off dropped there can leave that note
  sounding until the player sends its note-off again, an exposure only a game without the link meets (two silent
  stalls), which then streams raw.
- **The link never connects while the plan is refused or a node faulted** (design section 4): main.cpp passes
  `forward_ok && plan.plan() && !plan.faulted()` to `cart.setConnected`, so such a box writes no link probe and no guard
  probe, as it forwards nothing.
- **The volume write waits for the probe's verdict and for a quiet turn**, after the guard's turn, as `N8Link`'s
  `OnControl` runs after `menuHold`: its read-back would take an ack or the menu's `k` for the register.
- **The menu's answer disconnects the link at once in main.cpp** (forwarding stops, the stale UART stream is dropped
  at the reconnect, and the reconnect probes the link afresh), sets `menu_answered`, and the boot then skips
  `n8_probe` (whose `CMD_STATUS` need not read 0 after a RESET) and calls `boot_rom(0)`, straight to the `*t`
  handshake. A fresh mount clears `menu_answered`.
- **A menu the guard hears after a boot that failed is held, not booted again at once**: a failed install (a dirty
  menu heap, `0x44`) leaves `boot_rom` returning with `boot_error` set and the box forwarding into the menu, as it
  always did; the guard now hears that menu, and the box stops forwarding and retries the boot in 20 s
  (`menu_stuck_until`, the stuck menu's path, with its error LED) rather than in a loop of installs; the retry sets
  `menu_answered` too, so it goes straight to `*t` rather than asking `CMD_STATUS`, whose reading at that menu is
  unknown.
- **`fifo_write` times (`lat=`, the latency trace) and blinks the LED only for a write carrying a ring message**
  (`msgs != 0`): a raw link's idle probe every 250 ms would otherwise blink the LED at 4 Hz and pull `lat=` down. A
  failed write carrying MIDI disarms the timing, as at HEAD; a failed sync or probe does not.
- **FakeN8 models intact traffic only** (its own comment, at its link parser: "for intact traffic"; a lost sync byte
  leaves its `next_` at a status byte), so **the "bytes lost, every message in order" case drives the box's link into
  the link's ROM model** (`rp::n8::test::RomModel`, `LinkRomModel.hpp`, the oracle the sender's own soak is held to)
  through a writer that loses 1 to 3 byte bursts. Every other box case runs against FakeN8 through Edio over a
  `FakeN8Port`, or a scripted cart where FakeN8 cannot say a thing (an ack cut in two). All of them run on an injected
  clock: FakeN8 answers at once with its delays left at 0 (they run on the wall clock), so no case sleeps.
- **The device-mode whole pass is extended by a second case, not edited**: a framed pass (the connect's sync, its
  ack, a framed note and its ack) through the device-mode build of the link, under the counting allocator. Its
  record gains a scripted back-channel and the 64-bit clock, both off by default, so the existing case is unchanged.
- **`timing --midi` matches the port by its whole name**: the argument must equal exactly one rawmidi output's name as
  `amidi -l` prints it (the subdevice's name, else the device's), and must contain `CH345`; no match, two matches or a
  name without `CH345` is refused, after every output has been listed through the cards' control interfaces (which
  opens no MIDI port) and before anything is opened. `--guard` with `--midi` is refused (the box runs its own guard).
- **The latency trace gains the back-channel read's time** (`bcread` on the `[lat] passes` line): the one part of a
  pass design section 6 names that the trace did not already split out (`push>write` holds the guard and the framing
  for a message, `write` its write).
- **The firmware's sizes are recorded for `pnpm device:box`'s two images**, the pico2 PIO-USB and `RP_NATIVE_USB`
  builds; the `PICO_BOARD=pico` build design section 5, item 5 names is tried once at HEAD (task 1, step 1) and
  recorded only if it links. `tools/build-box.sh` builds no RP2040 image, and the box's `copy_to_ram` image (about
  350 KiB of SRAM at E4) is past an RP2040's 264 KB, so the HEAD build is expected to fail on RAM: the skip then rests
  on that log, not on inference. If it links instead, task 4 builds it after the change too and records both sizes.

- **Task 3's red run is a compile failure**, as new API's is: a stub build (the members declared, `drain` left raw)
  would show each case failing on an assertion first, at the cost of a second, throwaway version of the link to write
  and then replace; the cases' expectations are traced to the code instead, and a case that passes before its code
  exists cannot, since none compiles without it.

**Files:**

| File | Task | What changes |
|---|---|---|
| `packages/n8-wire/native/src/transport/N8ReliableLink.hpp` | 1 | rewritten: inline ring, template emitter |
| `packages/n8-wire/native/src/transport/N8MenuGuard.hpp` | 2 | new: the guard's decisions and the scan |
| `packages/n8-wire/native/CMakeLists.txt`, `packages/n8-wire/package.json` | 1, 2 | comments and description |
| `packages/n8/native/src/n8/desktop/N8Link.hpp`, `N8Link.cpp` | 2 | `N8Link` drives the guard |
| `packages/n8/native/src/n8/desktop/N8Host.cpp` | 2 | one comment |
| `packages/n8/native/test/N8MenuGuard.test.cpp`, `packages/n8/native/CMakeLists.txt` | 2 | the guard's own cases |
| `pico/n8-host/BoxCartLink.hpp`, `BoxCartLink.cpp` | 3 | rewritten: the probe, the framed drain, the guard |
| `packages/native/test/device-mode/BoxHost.test.cpp` | 1, 3 | the sender's case; the box link's cases |
| `packages/native/test/device-mode/DeviceModeEntries.hpp`, `DeviceModeNodes.cpp` | 3 | the device-mode link's back-channel |
| `packages/native/cmake/tests.cmake` | 3 | FakeN8.hpp on the device-mode test's path |
| `pico/n8-host/main.cpp`, `pico/n8-host/CMakeLists.txt`, `pico/n8-host/LatTrace.hpp` | 4 | the firmware's wiring |
| `packages/n8/native/test/n8-timing.cpp`, `n8-sd-hwtest.cpp` | 5 | `timing --midi` |
| `spec/07-remaining-work.md`, `spec/13-subsystems.md`, `spec/00-overview.md`, `pico/n8-host/README.md` | 6 | documents |

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
- **Windows builds our own targets with `/W4 /WX`**, the device-mode objects and test among them: every narrowing
  (a `std::size_t` into a `std::uint32_t`, an `int` into a `std::uint8_t`) is an explicit `static_cast`, as the code
  below writes it.
- **Never edit an existing `docs/*-plan.md`.** The design
  ([pico-box-framed-link-design.md](pico-box-framed-link-design.md)) gains its as-built and execution sections from
  the controller, not from a task here.
- **spec/07's "The box's cart stream has no framing and no menu guard" is shrunk, not deleted**: this work leaves its
  hardware acceptance (design section 7) and its latency runs (section 6) open, so task 6 rewrites the entry to just
  those, retitled, and its checklist line with it. It goes once the rig passes (AGENTS.md's delete-when-finished rule).
- **Only a step's named paths are staged.** `git add` exactly what the step lists; a task's commit is its own
  `git diff` and nothing else.
- **Never use Python**, and never send repository content to an external service.

---

## Task 1: the sender allocates nothing

The box's firmware links no heap, no exceptions and no RTTI (the link trap), and `N8ReliableSender` holds its window in
a `rp::FixedQueue<Frame>` (a `std::vector` reserved at construction) and takes its emitter as a `std::function`, which
allocates for a lambda capturing more than two references, so it cannot link there. This task makes the window a ring
of `kWindow` frames held inline and the emitter a template parameter. The protocol is untouched: every byte the sender
emits, in every order, is what it emitted before, which the sender's cases, the emulator's role cases and the parity
baseline hold. A new device-mode case makes a sender, fills its window, has it re-send twice and re-sync, and counts no
allocation; it fails today on the window's reservation and on each call's `std::function`.

**Files** (line numbers are the file at `7fa0d1e17`):

- Modify (rewrite whole): `packages/n8-wire/native/src/transport/N8ReliableLink.hpp` (341 lines).
- Modify: `packages/n8-wire/native/CMakeLists.txt:9-12` (the header-only comment that names `util/FixedQueue.hpp`),
  `packages/n8-wire/package.json:5` (the description, which says the link's `std::function` emit may allocate).
- Test: `packages/native/test/device-mode/BoxHost.test.cpp:23-58` (two includes), and one case appended after the
  last (`:1236`).

**Interfaces:**

- Consumes: `rp::test::beginCountingAllocations` / `endCountingAllocations` (`graph/CountingAllocator.hpp`, linked into
  `retroplug-device-mode-test` as `retroplug-testing-allocator`); the n8-wire include root, which the device-mode test
  has through `retroplug-n8`'s PUBLIC link.
- Produces: `rp::N8ReliableSender` with `template <class EmitFn> void send(const std::uint8_t*, std::size_t, const
  EmitFn&)`, `template <class EmitFn> void sendMessage(...)` and `template <class EmitFn> std::int64_t
  poll(std::int64_t nowNs, const EmitFn&, bool cartBusy = false)`; every other member as before; the `Emit` alias is
  gone. It includes no heap or exception header (`<array>`, `<cstddef>`, `<cstdint>`, `<cstring>`, `<limits>` only),
  so task 3's `BoxCartLink.cpp` can include it in the device-mode objects and the firmware. Its callers
  (`N8Link.cpp`, `NesN8FifoRole.cpp`, both `N8ReliableLink.test.cpp` files) compile unchanged.

### Steps

- [ ] **Step 1: record the firmware's sizes at the start**

Task 3 changes what the firmware links, so the image's sizes are taken first, for task 4's commit to compare against:

```
timeout -k 10 1800 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh pnpm -C /workspaces/RetroPlug-main/cuttlefish device:box > /workspaces/RetroPlug-main/cuttlefish/.superpowers/pbplan/box-size-before.log 2>&1; echo "exit $?"
grep '^== \(pio\|native\):' /workspaces/RetroPlug-main/cuttlefish/.superpowers/pbplan/box-size-before.log
```

Expected: `exit 0`, and two lines, `== pio: flash <n>, data <n>, bss <n> bytes; ...` and `== native: ...`
(`tools/build-box.sh`'s summary). Keep the log: task 4 quotes these two lines.

Then the RP2040 image design section 5, item 5 names, once, at HEAD (build only, flashing nothing): `tools/build-box.sh`
builds no `PICO_BOARD=pico` image, and the box's `copy_to_ram` image (about 350 KiB of SRAM) is past an RP2040's 264
KB, so this is expected to fail at its link, and that log is what the plan's skip rests on. Its build dir is this
plan's own, under `.superpowers/pbplan/`, never `pico/n8-host/build*` (`loop.sh`'s); the SDK and Pico-PIO-USB paths
are the ones `box-size-before.log` names (`== the firmware, pio (... Pico SDK <path>, ...)`), `/workspaces/pico-sdk`
and `/workspaces/Pico-PIO-USB` on this container:

```
timeout -k 10 1800 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh cmake -S /workspaces/RetroPlug-main/cuttlefish/pico/n8-host -B /workspaces/RetroPlug-main/cuttlefish/.superpowers/pbplan/build-rp2040-head -DPICO_BOARD=pico -DPICO_SDK_PATH=/workspaces/pico-sdk -DPICO_PIO_USB_PATH=/workspaces/Pico-PIO-USB -DCMAKE_BUILD_TYPE=Release -DPICO_NO_PICOTOOL=1 -DRP_EXPORT_PLAN_TOOL=/workspaces/RetroPlug-main/cuttlefish/build/export-plan/retroplug-export-plan -DRP_REFLECTCPP_SOURCE_DIR="$(sed -n 's/^reflectcpp_SOURCE_DIR:STATIC=//p' /workspaces/RetroPlug-main/cuttlefish/build/CMakeCache.txt)" > /workspaces/RetroPlug-main/cuttlefish/.superpowers/pbplan/box-rp2040-head.log 2>&1; echo "configure exit $?"
timeout -k 10 1800 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh cmake --build /workspaces/RetroPlug-main/cuttlefish/.superpowers/pbplan/build-rp2040-head -j$(nproc) >> /workspaces/RetroPlug-main/cuttlefish/.superpowers/pbplan/box-rp2040-head.log 2>&1; echo "build exit $?"
grep -n "overflow\|will not fit\|n8_host.elf" /workspaces/RetroPlug-main/cuttlefish/.superpowers/pbplan/box-rp2040-head.log | tail -5
```

Expected: `configure exit 0`, `build exit` non-zero, and the grep shows the linker's RAM overflow (its "region ... RAM
overflowed" or "will not fit in region" line). That is the evidence the skip rests on: write
`RP2040 (PICO_BOARD=pico): does not link at HEAD, RAM overflow` into `.superpowers/pbplan/box-rp2040.txt`. If it
fails for any other reason (a configure error, a compile error), that is a finding: say so and keep the log, rather
than taking it as the RAM failure. If it links instead, append its size line to `box-rp2040.txt`
(`arm-none-eabi-size -A` over `.superpowers/pbplan/build-rp2040-head/n8_host.elf`, the `.text`, `.data` and `.bss`
rows), and task 4 builds it after the change too.

- [ ] **Step 2: write the failing case**

In `packages/native/test/device-mode/BoxHost.test.cpp`, add two includes.

Old (lines 30-31):

```cpp
#include <filesystem>
#include <fstream>
```

New:

```cpp
#include <array>
#include <filesystem>
#include <fstream>
```

Old (lines 34-35):

```cpp
#include <random>
#include <sstream>
```

New:

```cpp
#include <optional>
#include <random>
#include <sstream>
```

Old (lines 58-59):

```cpp
#include "script/MqjsHost.hpp"

```

New:

```cpp
#include "script/MqjsHost.hpp"
#include "transport/N8ReliableLink.hpp"

```

Then append this case at the end of the file (after line 1236, the volume write's case):

```cpp

TEST_CASE("the reliable link's sender allocates nothing, as the box runs it: made, a full window, its re-sends and a "
          "re-sync",
          "[device-mode][box][n8-wire]") {
    using rp::N8ReliableSender;
    // The box's transport keeps what goes out in storage of its own and marks each frame sent as it leaves: an emitter
    // of five references, past the two a std::function holds without allocating.
    std::optional<N8ReliableSender> s;
    std::array<std::uint8_t, 4096>  wire{};
    std::size_t                     wireLen = 0;
    int                             framesOut = 0;
    std::int64_t                    now = 1'000'000'000;
    const auto emit = [&](const std::uint8_t* d, std::size_t n, int seq) {
        for (std::size_t i = 0; i < n && wireLen < wire.size(); ++i) wire[wireLen++] = d[i];
        if (seq >= 0) {
            ++framesOut;
            s->onSent(seq, now);
        }
    };
    N8ReliableSender::Timing t;
    t.resendDelayNs = 0;

    rp::test::beginCountingAllocations();
    s.emplace(t);  // made where the box makes it: inside a pass
    for (int i = 0; i < 48; ++i) {
        const std::uint8_t on[3] = {0x90, static_cast<std::uint8_t>(36 + i), 100};
        s->send(on, 3, emit);
    }
    const std::size_t full = s->outstanding();
    now += 100'000'000;  // nothing heard for the timeout: the whole window again
    s->poll(now, emit);
    const std::uint8_t ack[3] = {0xF5, 10, 20};  // ten arrived, and the ROM read 20: from 10 again, at once
    s->onBackChannel(ack, 3, now);
    s->poll(now, emit);
    const std::uint8_t sysex[5] = {0xF0, 0x7D, 0x42, 0x02, 0xF7};
    s->sendMessage(sysex, 5, emit);
    for (int i = 0; i < 10; ++i) {  // 39 out: the 49th outstanding frame gives the window up and re-syncs
        const std::uint8_t off[3] = {0x80, 60, 0};
        s->send(off, 3, emit);
    }
    const auto counted = rp::test::endCountingAllocations();

    CHECK(counted.allocations == 0);
    CHECK(counted.frees == 0);
    CHECK(full == 48);
    CHECK(s->resends() == 2);
    CHECK(s->framesResent() == 48 + 38);
    CHECK(s->framesSent() == 59);
    CHECK(framesOut == 59 + 48 + 38);
    CHECK(s->outstanding() == 1);
    // The re-sync, F4 <58>, ahead of the last frame (six bytes: F5 58 80 3C 00 <sum>).
    REQUIRE(wireLen >= 8);
    CHECK(wire[wireLen - 8] == N8ReliableSender::SYNC);
    CHECK(wire[wireLen - 7] == 58);
}
```

- [ ] **Step 3: run it and see it fail**

```
timeout -k 10 1800 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh cmake --build /workspaces/RetroPlug-main/cuttlefish/build --target retroplug-device-mode-test -j$(nproc)
timeout -k 10 300 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh /workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-device-mode-test "[n8-wire]"
```

Expected: the build succeeds; the run exits non-zero with the two count CHECKs failing, `CHECK( counted.allocations ==
0 )` and `CHECK( counted.frees == 0 )`, each with a non-zero expansion (the window's reservation at `emplace`, and a
`std::function` made and freed for each call's emitter). The protocol CHECKs pass.

- [ ] **Step 4: the sender with an inline window and a template emitter**

Write `packages/n8-wire/native/src/transport/N8ReliableLink.hpp` whole:

```cpp
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

// The host half of BlipToaster's reliable link (bliptoaster src/midi/main.c "The reliable link").
//
// On an NTSC console a DMC-DMA halt that lands on the ROM's `lda $40F0` repeats the read, and the N8 FIFO pops a
// byte per read, so a byte the CPU never sees is simply gone - a dropped drum hit, or a stuck note when it was a
// note-off. The ROM cannot avoid it, so the host frames what it sends and re-sends what does not arrive:
//
//   F4 <seq>            sync: the next frame carries seq (sent before the first frame, and after any reset)
//   F5 <seq> <message> <sum>  a frame: sum = (seq + message bytes) & 0x7F. The ROM reads the whole frame, checks its
//                       structure and sum, and applies it only if it is intact and carries the next seq
//
// and the ROM writes `F5 <next expected> <last header read>` on the cart's USB back-channel (CMD_USB_WR) whenever its
// FIFO runs empty after anything changed. An ack that names a frame we sent means it was lost - at once if the ROM has
// read that frame's header or a later one, otherwise once the frame is older than a round trip (on a real cart it may
// still be crossing USB) - so go back and re-send it and everything after it (the ROM skips frames behind a gap, which
// keeps delivery strictly in order, so a note-off can never overtake its note-on). Writes are never repeated by a DMA
// halt, so acks are not lost; the timeout is only a safety net for a cart that stops answering.
//
// ONE implementation, three transports: the emulator's cart FIFO (NesN8FifoRole, timed in emulated CPU time), and a
// physical N8 over USB, from the desktop (N8Link, wall-clock time) and from the Pico box (pico/n8-host's BoxCartLink,
// the box's microsecond clock). Which is the point - a user hears the same recovery in the plugin as on the cartridge.
// The transport supplies time and carries the bytes; everything protocol-shaped lives here. Not thread-safe: each
// transport drives its sender from one thread.
//
// It allocates nothing and includes no heap or exception header, so the box's firmware links it under the device
// build's trap on the heap, exceptions and RTTI: the window is a ring of kWindow frames held inline (about 3.4 KiB),
// and an emitter is a template parameter, called where it is rather than wrapped in a std::function, which allocates
// for a lambda that captures more than two references.

namespace rp {

class N8ReliableSender {
public:
    static constexpr std::uint8_t SYNC  = 0xF4;
    static constexpr std::uint8_t FRAME = 0xF5;
    // Outstanding frames before the oldest is given up on (with a re-sync). Under 64, so a 7-bit sequence number
    // always tells an old frame from a new one.
    static constexpr std::size_t kWindow = 48;
    // The longest message a frame may carry: the ROM checks a frame in a 40-byte buffer before applying it
    // (LINK_MSG_MAX in main.c - an N163 wave upload is 37). A longer SysEx is not one the ROM acts on; it is dropped.
    static constexpr std::size_t kMaxMessage = 40;

    struct Timing {
        // Wait this long after a frame is shown lost before re-sending it. Emulator: the hardware round trip, modelled,
        // so a recovered hit lands as late in the plugin as on the cart. Hardware: 0 - the real latency applies.
        std::int64_t resendDelayNs = 0;
        // Nothing heard from the cart for this long while a frame is outstanding: re-send anyway. A safety net only -
        // the ROM answers every burst it reads, damaged or not, so a loss is normally reported by an ack. Measured
        // from the later of the frame going out and the last ack, so a cart that is merely busy is not flooded.
        std::int64_t timeoutNs = 100'000'000;
    };

    // Bytes for the transport: send(), sendMessage() and poll() take an emitter, any callable as
    // emit(const std::uint8_t* data, std::size_t n, int seq). seq >= 0 marks the first byte of a frame, which the
    // transport reports back through onSent() once it is actually on its way (released into the FIFO / written to
    // USB); -1 for anything else. `data` is the sender's own storage, valid for the call; an emitter may call onSent()
    // from inside it.

    N8ReliableSender() : timing_(Timing{}) {}
    explicit N8ReliableSender(Timing t) : timing_(t) {}
    void setTiming(Timing t) { timing_ = t; }

    // Forget everything in flight and sync again before the next frame - after a transport barrier (a flush, a
    // reset, a savestate load), where the ROM's idea of the sequence and ours may differ.
    void reset() {
        clearWindow();
        synced_ = false;
        status_ = 0;
        msgLen_ = 0;
        msgTooLong_ = false;
        inSysex_ = false;
        ackState_ = 0;
        resendAt_ = kNever;
        pendingSyncLen_ = 0;
    }

    // Host MIDI in - any run of bytes, possibly several messages, running status, SysEx, real-time. Each complete
    // channel message or SysEx goes out as one frame; real-time bytes (F8-FF) pass through unframed, since the ROM
    // takes them at any point; System Common messages the ROM ignores (F1-F3, F6) are dropped, as are truncated ones.
    template <class EmitFn>
    void send(const std::uint8_t* data, std::size_t n, const EmitFn& emit) {
        for (std::size_t i = 0; i < n; ++i) feed(data[i], emit);
    }

    // One complete message, framed as a send() call of its own. A transport writes these between the chunks it streams
    // (a leaving cart node's note-offs), where send() would read them into the message still being collected: a
    // status cuts a split SysEx short, its bytes framed as messages of their own, and the SysEx's tail then read as
    // running-status data. The message being collected is left exactly as it was, bytes and all, so its own tail still
    // completes it.
    template <class EmitFn>
    void sendMessage(const std::uint8_t* data, std::size_t n, const EmitFn& emit) {
        const std::uint8_t                              status  = status_;
        const std::array<std::uint8_t, kMaxMessage + 2> msg     = msg_;
        const std::size_t                               msgLen  = msgLen_;
        const bool                                      tooLong = msgTooLong_;
        const bool                                      inSysex = inSysex_;
        status_     = 0;
        msgLen_     = 0;
        msgTooLong_ = false;
        inSysex_    = false;
        send(data, n, emit);
        status_     = status;
        msg_        = msg;
        msgLen_     = msgLen;
        msgTooLong_ = tooLong;
        inSysex_    = inSysex;
    }

    // The frame `seq` has gone out at nowNs.
    void onSent(int seq, std::int64_t nowNs) {
        for (std::size_t i = 0; i < windowSize_; ++i) {
            Frame& f = at(i);
            if (f.seq == seq && f.sentNs < 0) {
                f.sentNs = nowNs;
                if (f.firstSentNs < 0) f.firstSentNs = nowNs;
                return;
            }
        }
    }

    // Back-channel bytes from the cart: the ROM's `F5 <next>` acks (anything else is ignored).
    // Each ack is F5 <next> <last>: the seq the ROM expects next, and the seq of the last frame header it read.
    void onBackChannel(const std::uint8_t* data, std::size_t n, std::int64_t nowNs) {
        if (n) lastAckNs_ = nowNs;
        for (std::size_t i = 0; i < n; ++i) {
            const std::uint8_t b = data[i];
            if (b == FRAME) { ackState_ = 1; continue; }
            if (b & 0x80) { ackState_ = 0; continue; }
            if (ackState_ == 1) { ackNext_ = b; ackState_ = 2; continue; }
            if (ackState_ == 2) { onAck(ackNext_, b, nowNs); ackState_ = 0; }
        }
    }

    // Re-send whatever is due at nowNs. Returns the next time anything will be due (kNever when nothing is
    // outstanding), so a transport can skip calling until then.
    // `cartBusy`: the transport knows the cart still has unread bytes (the emulator can see its FIFO) - it cannot
    // have answered yet, so the timeout waits.
    template <class EmitFn>
    std::int64_t poll(std::int64_t nowNs, const EmitFn& emit, bool cartBusy = false) {
        if (windowSize_ == 0) { resendAt_ = kNever; return kNever; }
        const Frame& oldest = at(0);
        const bool timedOut = !cartBusy && oldest.sentNs >= 0 && nowNs - lastHeard(oldest) >= timing_.timeoutNs;
        if (nowNs >= resendAt_ || timedOut) {
            resendAt_ = kNever;
            ++resends_;
            if (pendingSyncLen_) { emit(pendingSync_.data(), pendingSyncLen_, -1); pendingSyncLen_ = 0; }
            for (std::size_t i = 0; i < windowSize_; ++i) {
                Frame& f = at(i);
                if (f.sentNs < 0) continue;   // still queued in the transport: it goes out on its own
                f.sentNs = -1;                // re-armed; onSent marks it again when it actually leaves
                ++framesResent_;
                emit(f.bytes, f.size, f.seq);
            }
        }
        return nextDue();
    }

    std::size_t outstanding() const { return windowSize_; }
    // Whether the window has stopped MOVING: the oldest outstanding frame first went out at least timeoutNs ago, and
    // nothing has acknowledged it since (an ack that advances the window makes a newer frame the oldest, with a newer
    // first send). This is not the re-send question above, which measures from the last thing the cart said: a
    // back-channel that chatters keeps that fresh for ever, and a transport that must look before it re-sends - a cart
    // at its menu reads a stale frame as one of its commands - needs the progress, not the silence.
    bool progressOverdue(std::int64_t nowNs) const {
        const std::int64_t first = oldestFirstSentNs();
        return first != kNever && nowNs - first >= timing_.timeoutNs;
    }
    // When the cart last said anything on the back-channel (an ack or not), and when the oldest outstanding frame first
    // went out (a re-send does not move it): with both far enough behind, a cart is not acknowledging anything, so a
    // transport may read a reply off the same channel. kNever while nothing is outstanding or that frame has not left;
    // lastHeardNs is the int64 minimum until the cart has said something.
    std::int64_t lastHeardNs() const { return lastAckNs_; }
    std::int64_t oldestFirstSentNs() const {
        return windowSize_ == 0 || at(0).firstSentNs < 0 ? kNever : at(0).firstSentNs;
    }
    // Round trips (frame out -> the ack that covered it): the fastest, and the latest. -1 until one is measured.
    std::int64_t rttMinNs() const { return rttMinNs_; }
    std::int64_t lastRttNs() const { return lastRttNs_; }
    std::uint64_t resends() const { return resends_; }
    std::uint64_t framesSent() const { return framesSent_; }       // every frame, first sends only
    std::uint64_t framesResent() const { return framesResent_; }   // frames sent again (each re-send of each frame)
    std::uint8_t  nextSeq() const { return nextSeq_; }

    static constexpr std::int64_t kNever = std::numeric_limits<std::int64_t>::max();

private:
    // A frame's bytes are inline: a window of 48 frames is a fixed ~3.4 KiB however many messages a block carries.
    struct Frame {
        std::uint8_t seq;
        std::int64_t sentNs;                    // -1 = not on its way yet
        std::int64_t firstSentNs;               // its first send, which a re-send keeps; -1 = not sent yet
        std::uint32_t size;
        std::uint8_t  bytes[3 + kMaxMessage];   // F5 seq message... sum
    };

    // Is `a` before `b` in the 7-bit sequence space (within half the space)?
    static bool before(std::uint8_t a, std::uint8_t b) {
        const std::uint8_t d = static_cast<std::uint8_t>((b - a) & 0x7F);
        return d != 0 && d < 64;
    }

    static std::size_t dataLen(std::uint8_t status) {
        const std::uint8_t k = status & 0xF0;
        return (k == 0xC0 || k == 0xD0) ? 1 : 2;
    }

    // The window, oldest first, as a ring: frames leave from the front (an ack) and join at the back (a new frame),
    // and the whole window goes at once (a reset, a full window's re-sync), so a frame stays where it was written
    // until it goes and nothing ever moves.
    Frame&       at(std::size_t i) { return window_[(windowHead_ + i) % kWindow]; }
    const Frame& at(std::size_t i) const { return window_[(windowHead_ + i) % kWindow]; }
    void         clearWindow() {
        windowHead_ = 0;
        windowSize_ = 0;
    }
    Frame& pushFrame(const Frame& f) {
        Frame& slot = window_[(windowHead_ + windowSize_) % kWindow];
        slot = f;
        ++windowSize_;
        return slot;
    }
    void popFrame() {
        windowHead_ = (windowHead_ + 1) % kWindow;
        --windowSize_;
    }

    template <class EmitFn>
    void feed(std::uint8_t b, const EmitFn& emit) {
        if (b >= 0xF8) { emit(&b, 1, -1); return; }   // real-time: unframed, anywhere (the ROM skips or acts on it)
        if (inSysex_) {
            if (b == 0xF7) {
                msgPush(b);
                if (!msgTooLong_ && msgLen_ <= kMaxMessage) frame(emit);   // longer: no frame (see kMaxMessage)
                msgLen_ = 0;
                msgTooLong_ = false;
                inSysex_ = false;
                return;
            }
            if (b < 0x80) { msgPush(b); return; }
            inSysex_ = false;   // a status cut the SysEx short: drop it (the ROM would not have applied it either)
            msgLen_ = 0;
            msgTooLong_ = false;
        }
        if (b == 0xF0) { status_ = 0; msg_[0] = b; msgLen_ = 1; msgTooLong_ = false; inSysex_ = true; return; }
        // System Common: the ROM ignores them.
        if (b >= 0xF0) { status_ = 0; msgLen_ = 0; msgTooLong_ = false; return; }
        if (b & 0x80) { status_ = b; msg_[0] = b; msgLen_ = 1; return; }
        if (status_ == 0) return;                       // data with no status: junk
        if (msgLen_ == 0) msg_[msgLen_++] = status_;    // running status: every frame carries its status
        msgPush(b);
        if (msgLen_ == 1 + dataLen(status_)) { frame(emit); }
    }

    // Append one byte of the message being collected. Past the longest message a frame may carry the bytes are dropped,
    // which is what a frame would do with them anyway (see kMaxMessage), and the message is marked too long.
    void msgPush(std::uint8_t b) {
        if (msgLen_ < sizeof msg_) msg_[msgLen_++] = b;
        else msgTooLong_ = true;
    }

    template <class EmitFn>
    void frame(const EmitFn& emit) {
        if (!synced_) {
            const std::uint8_t s[2] = { SYNC, nextSeq_ };
            emit(s, 2, -1);
            synced_ = true;
        }
        if (windowSize_ >= kWindow) {
            // The cart has stopped answering (unplugged, crashed, a ROM without the link): give the whole window up
            // and re-point the ROM past it rather than grow without bound.
            clearWindow();
            const std::uint8_t s[2] = { SYNC, nextSeq_ };
            emit(s, 2, -1);
        }
        Frame f{};
        f.seq = nextSeq_;
        f.sentNs = -1;
        f.firstSentNs = -1;
        f.size = 3 + static_cast<std::uint32_t>(msgLen_);
        f.bytes[0] = FRAME;
        f.bytes[1] = nextSeq_;
        std::memcpy(f.bytes + 2, msg_.data(), msgLen_);
        unsigned sum = nextSeq_;
        for (std::size_t i = 0; i < msgLen_; ++i) sum += msg_[i];
        f.bytes[2 + msgLen_] = static_cast<std::uint8_t>(sum & 0x7F);   // the ROM checks it before anything applies
        ++framesSent_;
        nextSeq_ = static_cast<std::uint8_t>((nextSeq_ + 1) & 0x7F);
        const Frame& added = pushFrame(f);
        emit(added.bytes, added.size, added.seq);
        msgLen_ = 0;
        msgTooLong_ = false;
    }

    void onAck(std::uint8_t next, std::uint8_t last, std::int64_t nowNs) {
        bool advanced = false;
        while (windowSize_ != 0 && before(at(0).seq, next)) {
            advanced = true;
            const std::int64_t sent = at(0).sentNs;
            if (sent >= 0) {
                const std::int64_t rtt = nowNs - sent;
                lastRttNs_ = rtt;
                if (rttMinNs_ < 0 || rtt < rttMinNs_) rttMinNs_ = rtt;
            }
            popFrame();
        }
        if (windowSize_ == 0) { resendAt_ = kNever; return; }
        if (advanced) resendAt_ = kNever;   // what a pending re-send was for has (partly) arrived: decide afresh
        const Frame& oldest = at(0);
        if (oldest.seq != next) {
            // The ROM expects something we no longer hold (it rebooted and latched another number, or we reset):
            // point it at our oldest frame and re-send from there.
            const std::uint8_t s[2] = { SYNC, oldest.seq };
            std::memcpy(pendingSync_.data(), s, 2);
            pendingSyncLen_ = 2;
            scheduleResend(nowNs);
            return;
        }
        // The ack names a frame we sent. If the ROM has READ that frame (<last> at or past <next>: its header, a later
        // frame's, or bytes it had to throw away - which it reports as the expected frame), it was lost: re-send now.
        // If not, it is still on its way - on a real cart it may be crossing USB, in the emulator it may land while the
        // ack is being written - and the ack that follows its arrival settles it; the timeout covers a cart that stops
        // answering. (Re-checking on a timer instead re-sent frames that had lost nothing, on a cart and in the
        // emulator alike: the confirming ack comes only once the ROM has processed the frame.)
        if (oldest.sentNs >= 0 && !before(last, next)) scheduleResend(nowNs);
    }

    std::int64_t lastHeard(const Frame& f) const { return f.sentNs > lastAckNs_ ? f.sentNs : lastAckNs_; }

    void scheduleResend(std::int64_t atNs) {
        const std::int64_t t = atNs + timing_.resendDelayNs;
        if (t < resendAt_) resendAt_ = t;
    }

    std::int64_t nextDue() const {
        std::int64_t due = resendAt_;
        if (windowSize_ != 0 && at(0).sentNs >= 0) {
            const std::int64_t t = lastHeard(at(0)) + timing_.timeoutNs;
            if (t < due) due = t;
        }
        return due;
    }

    Timing                    timing_;
    // Fixed storage, held inline: the sender runs inside a block (the NES role frames a released message as the
    // block's emulation reaches it) and on the Pico box, which has no heap, so neither the window nor the message being
    // collected may grow, and nothing here is allocated at all.
    std::array<Frame, kWindow> window_{};
    std::size_t               windowHead_ = 0;   // the oldest frame's slot
    std::size_t               windowSize_ = 0;   // frames outstanding
    std::uint8_t              nextSeq_    = 0;
    bool                      synced_     = false;
    std::uint8_t              status_     = 0;
    std::array<std::uint8_t, kMaxMessage + 2> msg_{};   // F0, up to kMaxMessage of payload, F7
    std::size_t               msgLen_     = 0;
    bool                      msgTooLong_ = false;      // a byte past what a frame could carry was dropped
    bool                      inSysex_    = false;
    std::uint8_t              ackState_   = 0;   // 0 idle, 1 after F5, 2 after <next>
    std::uint8_t              ackNext_    = 0;
    std::int64_t              resendAt_   = kNever;
    std::array<std::uint8_t, 2> pendingSync_{};  // a re-sync onAck decided on: poll() sends it ahead of the re-send
    std::size_t               pendingSyncLen_ = 0;
    std::uint64_t             resends_    = 0;
    std::uint64_t             framesSent_   = 0;
    std::uint64_t             framesResent_ = 0;
    std::int64_t              lastAckNs_  = std::numeric_limits<std::int64_t>::min();
    std::int64_t              rttMinNs_   = -1;
    std::int64_t              lastRttNs_  = -1;
};

}  // namespace rp
```

In `packages/n8-wire/native/CMakeLists.txt`:

Old (lines 9-12):

```cmake
# Header-only, so an INTERFACE target: a consumer gets this package's include root, where the two headers keep the paths
# they are included by ("transport/N8ReliableLink.hpp", "n8wire/ExpansionVolume.hpp"), and node-sdk's include root,
# for the link's frame store (util/FixedQueue.hpp, header-only: nothing here links reflect-cpp or any library). A build
# adds it too, between node-sdk and the packages that list it.
```

New:

```cmake
# Header-only, so an INTERFACE target: a consumer gets this package's include root, where the two headers keep the paths
# they are included by ("transport/N8ReliableLink.hpp", "n8wire/ExpansionVolume.hpp"). Both are the standard library's
# alone and heap-free (the link's window is a ring inside the sender), so the Pico box's firmware links the link under
# the device build's trap; node-sdk's headers, which this package declares as every native package does, come along
# with the link below and are not included by either. A build adds it too, between node-sdk and the packages that list
# it.
```

In `packages/n8-wire/package.json`, line 5:

Old:

```json
  "description": "The wire a real Everdrive N8 Pro cart and the emulated NES FIFO that stands in for it share, and neither owns: BlipToaster's reliable link, whose one sender the cart's desktop link and the emulator's FIFO role both drive, and the expansion-volume range the emulated chip's knob and the cart's master-volume register are set from. Two headers and no code; `device-ready` here says what is checked: the range is compiled by every device build, through rp.n8.nes, while the link is compiled by no device target and its std::function emit may allocate, so the link is device-ready by inspection, not by a check.",
```

New:

```json
  "description": "The wire a real Everdrive N8 Pro cart and the emulated NES FIFO that stands in for it share, and neither owns: BlipToaster's reliable link, whose one sender the cart's desktop link, the Pico box's cart link and the emulator's FIFO role all drive, and the expansion-volume range the emulated chip's knob and the cart's master-volume register are set from. Headers and no code, heap-free: the range is compiled by every device build, through rp.n8.nes, and the link by the Pico box's firmware under the device build's link trap.",
```

- [ ] **Step 5: run the case and see it pass**

```
timeout -k 10 1800 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh cmake --build /workspaces/RetroPlug-main/cuttlefish/build --target retroplug-device-mode-test -j$(nproc)
timeout -k 10 300 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh /workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-device-mode-test "[n8-wire]"
```

Expected: exit 0, `All tests passed` for the one case.

- [ ] **Step 6: the wider suites this task reaches**

The sender's own cases (`retroplug-n8-test`'s `N8ReliableLink.test.cpp`), its two transports (`N8Link` in the n8 binary
and the engine-graph binary's `N8Adapters`; `NesN8FifoRole` in `retroplug-mesen-test`), the whole device-mode binary,
the hardware test compiled (never run), and the byte-exact baselines the emulated link feeds (the parity suite and the
BlipToaster ROM cases):

```
timeout -k 10 1800 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh cmake --build /workspaces/RetroPlug-main/cuttlefish/build --target retroplug-n8-test retroplug-mesen-test retroplug-engine-graph-test retroplug-device-mode-test retroplug-host retroplug-n8-hwtest -j$(nproc)
timeout -k 10 300 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh /workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-n8-test
timeout -k 10 600 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh /workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-mesen-test
timeout -k 10 600 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh /workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-device-mode-test
timeout -k 10 1800 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh /workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-engine-graph-test
timeout -k 10 1800 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh pnpm -C /workspaces/RetroPlug-main/cuttlefish test:n8
timeout -k 10 1800 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh pnpm -C /workspaces/RetroPlug-main/cuttlefish test:native parity
timeout -k 10 1800 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh pnpm -C /workspaces/RetroPlug-main/cuttlefish test:native bliptoaster-rom
```

Expected: exit 0 for each; each Catch2 binary's summary shows no failure; the two `test:native` runs print their
`# ... native test file(s) passed` summaries. A parity or ROM case that moves is a real change to the bytes the link
writes (AGENTS.md: never noise): stop and compare the frames, do not re-record. Fixture-gated parity cases skip with
`RETROPLUG_RESOURCES_DIR` unset, as the skip baseline allows; the controller's kit runs them with the fixtures.

- [ ] **Step 7: commit**

```
git -C /workspaces/RetroPlug-main/cuttlefish add packages/n8-wire/native/src/transport/N8ReliableLink.hpp \
  packages/n8-wire/native/CMakeLists.txt packages/n8-wire/package.json \
  packages/native/test/device-mode/BoxHost.test.cpp
git -C /workspaces/RetroPlug-main/cuttlefish commit -F - <<'MSG'
n8-wire: the reliable link's sender allocates nothing

The Pico box links no heap, so the sender could not run there: its window was
a FixedQueue (a std::vector reserved when the sender was made) and its emitter
a std::function, which allocates for a lambda that captures more than two
references. The window is now a ring of 48 frames held inline, and send,
sendMessage and poll take the emitter as a template parameter; the callers'
lambdas are unchanged, and so is every byte the sender writes. A device-mode
case makes the sender, fills its window, has it re-send twice and re-sync, and
counts no allocation and no free.
MSG
```

---

## Task 2: the menu guard's decisions, shared

The guard's decisions live inside `N8Link::menuHold` and its helpers, beside the port I/O they drive, so the box
cannot run them. This task moves them into `rp::N8MenuGuard` (n8-wire, header-only), with the back-channel scan beside
them: each turn it is given the time, the sender (null on a raw link) and whether the stream is between messages, and
what the back-channel said is handed to it as it is read; it answers whether to hold, whether to write the probe now,
whether to probe the link again, and whether the menu state changed, and it answers `probeAwaited(now)`. Its
constants are `N8Link`'s, moved. `N8Link` becomes its driver (the ring, the tracker, the writes, the logs), in the
order `menuHold` did them, so it behaves exactly as before: its guard and job cases (`N8CartJobs.test.cpp`'s
`[menu-guard]`, `N8CartLink.test.cpp`, `N8Host.test.cpp`) are the proof, unedited. The guard's own rules get a case
each in a new test file.

**Files** (line numbers are the file at `7fa0d1e17`):

- Create: `packages/n8-wire/native/src/transport/N8MenuGuard.hpp`.
- Test (create): `packages/n8/native/test/N8MenuGuard.test.cpp`; modify `packages/n8/native/CMakeLists.txt:71-77`
  (`retroplug-n8-test`'s sources).
- Modify: `packages/n8/native/src/n8/desktop/N8Link.hpp:16-17` (the include), `:68-98` (`kMenuProbe`, `kMenuAnswer`,
  `BackChannelScan` become the guard's), `:252-259` (`menuHold`'s and its helpers' comments), `:341-357` (the guard's
  state becomes `guard_`).
- Modify: `packages/n8/native/src/n8/desktop/N8Link.cpp:16-28` (the guard's constants go), `:76-77` (`flushPort`),
  `:127-144` (the connect's seed), `:163-166` (`disconnect`), `:295` (`probeLink`'s scan state), `:350-357`
  (`readBackChannel`), `:376-453` (`menuHold` and `writeMenuProbe`'s head), `:469-483` (`menuProbeAwaited`,
  `reportMenuState`), `:506-513` (`enterMenu`'s resets), `:517-520` (`leaveMenu`'s), `:540` (`quietForRead`).
- Modify: `packages/n8/native/src/n8/desktop/N8Host.cpp:85-88` (a comment naming `N8Link.cpp`'s `kMenuAnswerNs`).
- Modify: `packages/n8-wire/native/CMakeLists.txt` and `packages/n8-wire/package.json` (the third header named).

**Interfaces:**

- Consumes: `rp::N8ReliableSender::FRAME`, `progressOverdue`, `outstanding` (task 1's header).
- Produces, in `transport/N8MenuGuard.hpp`, namespace `rp`:
  - `class N8MenuGuard` with `static constexpr std::uint8_t kProbe[4]` (`F0 2A 74 F7`), `kAnswer` (`0x6B`),
    `std::int64_t kProbeNs` (250 ms), `kAnswerNs` (100 ms), `kHoldNs` (1 s), `int kLeaveMisses` (3), `kStallMisses`
    (2);
  - `struct N8MenuGuard::Scan { enum class Heard { Nothing, Ack, MenuAnswer }; int state; Heard feed(std::uint8_t); }`;
  - `struct N8MenuGuard::Turn { bool hold, writeProbe, reprobeLink, enteredMenu, leftMenu; }`;
  - `void reset()`, `void seed(bool menu)`, `bool atMenu() const`, `Scan& scan()` / `const Scan& scan() const`,
    `void hear(const std::uint8_t* data, std::size_t n)`, `bool report(bool menu)` (true when the state changed),
    `Turn turn(std::int64_t now, const N8ReliableSender* sender, bool betweenMessages)`,
    `bool probeAwaited(std::int64_t now) const`.
  - Every member defaults to 0 or false, so a `N8MenuGuard` member of a static aggregate is constant-initialised to
    zero (task 3's `BoxCartLink::Storage`, in `.bss`).
- `N8Link` keeps `N8Link::kMenuProbe`, `N8Link::kMenuAnswer` and `N8Link::BackChannelScan` (now
  `rp::N8MenuGuard::Scan`), which N8Host.cpp's `askMenu` uses; its public API is otherwise unchanged.

### Steps

- [ ] **Step 1: write the guard's cases**

Create `packages/n8/native/test/N8MenuGuard.test.cpp`:

```cpp
// Guards the N8 menu guard's decisions (transport/N8MenuGuard.hpp, the n8-wire package's), a rule a case, driven by
// hand with a clock of the case's own: the raw link's cadence, only between two messages; a framed stall held and
// probed, and probed again each second; an ack's resume and its trusted second, which a drained window ends; two
// silent stalls asking for the link to be probed again; the menu's 'k' entering the menu and three misses in a row
// leaving it; a 'k' inside an ack that is not the menu's; and the state a probe from outside the guard reports. The
// drivers' own cases are N8CartJobs.test.cpp's [menu-guard] (N8Link over FakeN8) and the device-mode binary's
// [box-link] (the Pico box's cart link).
//
// Run via `pnpm test:n8` (retroplug-n8-test).

#include <cstddef>
#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include "transport/N8MenuGuard.hpp"
#include "transport/N8ReliableLink.hpp"

namespace {

using rp::N8MenuGuard;
using rp::N8ReliableSender;

constexpr std::int64_t kMs = 1'000'000;
// The guard's clock is never 0: 0 is its "no probe awaited", as a steady clock never reads.
constexpr std::int64_t T0 = 1'000 * kMs;

// A framed stream as the guard sees it: the reliable link's sender, its frames marked sent when the case says.
struct Stream {
    N8ReliableSender s;
    void sendAt(std::int64_t at, std::uint8_t key = 60) {
        const std::uint8_t on[3] = {0x90, key, 100};
        s.send(on, 3, [&](const std::uint8_t*, std::size_t, int seq) {
            if (seq >= 0) s.onSent(seq, at);
        });
    }
    // The ROM's ack: every frame before `next` arrived.
    void ack(std::uint8_t next, std::int64_t at) {
        const std::uint8_t a[3] = {0xF5, next, 0};
        s.onBackChannel(a, 3, at);
    }
};

}  // namespace

TEST_CASE("menu guard: a raw link is probed every 250 ms, and only between two messages", "[n8][menu-guard]") {
    N8MenuGuard g;
    N8MenuGuard::Turn t = g.turn(T0, nullptr, true);
    CHECK(t.writeProbe);  // the first at once
    CHECK_FALSE(t.hold);
    CHECK(g.probeAwaited(T0 + 99 * kMs));
    CHECK_FALSE(g.probeAwaited(T0 + 100 * kMs));
    CHECK_FALSE(g.turn(T0 + 100 * kMs, nullptr, true).writeProbe);   // unanswered: a game; the next is 250 ms on
    CHECK_FALSE(g.turn(T0 + 249 * kMs, nullptr, true).writeProbe);
    CHECK_FALSE(g.turn(T0 + 250 * kMs, nullptr, false).writeProbe);  // inside a message: it waits, however long
    CHECK_FALSE(g.turn(T0 + 900 * kMs, nullptr, false).writeProbe);
    t = g.turn(T0 + 901 * kMs, nullptr, true);
    CHECK(t.writeProbe);
    CHECK_FALSE(t.hold);
    CHECK_FALSE(g.atMenu());
}

TEST_CASE("menu guard: a framed window that stops moving is held and probed, and probed again each second",
          "[n8][menu-guard]") {
    N8MenuGuard g;
    Stream      f;
    f.sendAt(T0);
    N8MenuGuard::Turn t = g.turn(T0 + 99 * kMs, &f.s, true);
    CHECK_FALSE(t.hold);  // inside the link's own timeout: the window may still move
    CHECK_FALSE(t.writeProbe);
    t = g.turn(T0 + 100 * kMs, &f.s, true);
    CHECK(t.hold);
    CHECK(t.writeProbe);
    CHECK(g.probeAwaited(T0 + 150 * kMs));
    t = g.turn(T0 + 600 * kMs, &f.s, true);
    CHECK(t.hold);  // nothing heard: still held, and not asked again within the second
    CHECK_FALSE(t.writeProbe);
    t = g.turn(T0 + 1100 * kMs, &f.s, true);
    CHECK(t.hold);
    CHECK(t.writeProbe);
    CHECK_FALSE(t.reprobeLink);
}

TEST_CASE("menu guard: an ack heard after a stall's probe resumes the stream, and a stall is not asked about again "
          "for a second",
          "[n8][menu-guard]") {
    N8MenuGuard g;
    Stream      f;
    f.sendAt(T0);
    REQUIRE(g.turn(T0 + 100 * kMs, &f.s, true).writeProbe);
    const std::uint8_t probeAck[3] = {0xF5, 0x00, 0x00};  // a live BlipToaster's, for the probe's unframed bytes
    g.hear(probeAck, 3);
    N8MenuGuard::Turn t = g.turn(T0 + 110 * kMs, &f.s, true);
    CHECK_FALSE(t.hold);
    CHECK_FALSE(t.writeProbe);
    SECTION("the trusted second: a window still stopped is not asked about again until it ends") {
        t = g.turn(T0 + 1109 * kMs, &f.s, true);
        CHECK_FALSE(t.hold);
        CHECK_FALSE(t.writeProbe);
        t = g.turn(T0 + 1110 * kMs, &f.s, true);
        CHECK(t.hold);
        CHECK(t.writeProbe);
    }
    SECTION("a window that drains ends it: the next frame's stall is asked about at the link's own timeout") {
        f.ack(1, T0 + 120 * kMs);
        REQUIRE(f.s.outstanding() == 0);
        CHECK_FALSE(g.turn(T0 + 120 * kMs, &f.s, true).hold);
        f.sendAt(T0 + 200 * kMs, 62);
        CHECK_FALSE(g.turn(T0 + 299 * kMs, &f.s, true).hold);
        t = g.turn(T0 + 300 * kMs, &f.s, true);
        CHECK(t.hold);
        CHECK(t.writeProbe);
    }
}

TEST_CASE("menu guard: two stall probes that hear nothing at all ask for the link to be probed again",
          "[n8][menu-guard]") {
    N8MenuGuard g;
    Stream      f;
    f.sendAt(T0);
    REQUIRE(g.turn(T0 + 100 * kMs, &f.s, true).writeProbe);
    REQUIRE(g.turn(T0 + 1100 * kMs, &f.s, true).writeProbe);  // the first second heard nothing: asked again
    N8MenuGuard::Turn t = g.turn(T0 + 2099 * kMs, &f.s, true);
    CHECK(t.hold);
    CHECK_FALSE(t.reprobeLink);
    t = g.turn(T0 + 2100 * kMs, &f.s, true);
    CHECK(t.reprobeLink);
    CHECK_FALSE(t.hold);
    CHECK_FALSE(t.writeProbe);
    CHECK_FALSE(g.probeAwaited(T0 + 2100 * kMs));
    CHECK_FALSE(g.atMenu());
}

TEST_CASE("menu guard: the menu's 'k' enters the menu, and three unanswered probes in a row leave it",
          "[n8][menu-guard]") {
    N8MenuGuard g;
    REQUIRE(g.turn(T0, nullptr, true).writeProbe);
    const std::uint8_t k = N8MenuGuard::kAnswer;
    g.hear(&k, 1);
    N8MenuGuard::Turn t = g.turn(T0 + 20 * kMs, nullptr, true);
    CHECK(t.enteredMenu);
    CHECK(t.hold);
    CHECK_FALSE(t.writeProbe);  // the cadence runs from the probe the menu answered
    CHECK(g.atMenu());
    t = g.turn(T0 + 250 * kMs, nullptr, true);  // at the menu, every 250 ms whatever the link
    CHECK(t.hold);
    CHECK(t.writeProbe);
    CHECK_FALSE(t.enteredMenu);
    t = g.turn(T0 + 350 * kMs, nullptr, true);  // unanswered: one miss
    CHECK(t.hold);
    CHECK_FALSE(t.leftMenu);
    REQUIRE(g.turn(T0 + 500 * kMs, nullptr, true).writeProbe);
    SECTION("three in a row: a game") {
        CHECK_FALSE(g.turn(T0 + 600 * kMs, nullptr, true).leftMenu);  // two
        REQUIRE(g.turn(T0 + 750 * kMs, nullptr, true).writeProbe);
        t = g.turn(T0 + 850 * kMs, nullptr, true);  // three
        CHECK(t.leftMenu);
        CHECK_FALSE(t.hold);
        CHECK_FALSE(t.writeProbe);
        CHECK_FALSE(g.atMenu());
    }
    SECTION("an answer between the misses starts the count again") {
        g.hear(&k, 1);
        t = g.turn(T0 + 520 * kMs, nullptr, true);
        CHECK(t.hold);
        CHECK_FALSE(t.enteredMenu);  // already there
        REQUIRE(g.turn(T0 + 750 * kMs, nullptr, true).writeProbe);
        CHECK_FALSE(g.turn(T0 + 850 * kMs, nullptr, true).leftMenu);   // one
        REQUIRE(g.turn(T0 + 1000 * kMs, nullptr, true).writeProbe);
        CHECK_FALSE(g.turn(T0 + 1100 * kMs, nullptr, true).leftMenu);  // two
        CHECK(g.atMenu());
    }
}

TEST_CASE("menu guard: a 'k' inside an ack is not the menu's answer, even split across two reads", "[n8][menu-guard]") {
    N8MenuGuard        g;
    const std::uint8_t ack[3] = {0xF5, 0x6B, 0x6B};  // <next> and <last> both 0x6B
    g.hear(ack, 3);
    CHECK_FALSE(g.turn(T0, nullptr, true).enteredMenu);
    const std::uint8_t head[1] = {0xF5}, tail[2] = {0x6B, 0x6B};
    g.hear(head, 1);
    CHECK(g.scan().state == 1);
    g.hear(tail, 2);
    CHECK(g.scan().state == 0);
    CHECK_FALSE(g.turn(T0 + 1 * kMs, nullptr, true).enteredMenu);
    const std::uint8_t k = N8MenuGuard::kAnswer;
    g.hear(&k, 1);
    CHECK(g.turn(T0 + 2 * kMs, nullptr, true).enteredMenu);
}

TEST_CASE("menu guard: the state a probe from outside the guard reports, and the connect's seed", "[n8][menu-guard]") {
    N8MenuGuard g;
    g.seed(true);  // the connect's own check: the state, with nothing to enter
    CHECK(g.atMenu());
    CHECK_FALSE(g.report(true));  // already there: nothing changed
    CHECK(g.report(false));
    CHECK_FALSE(g.atMenu());
    CHECK(g.report(true));
    CHECK(g.atMenu());
    g.reset();
    CHECK_FALSE(g.atMenu());
    CHECK_FALSE(g.probeAwaited(T0));
}
```

In `packages/n8/native/CMakeLists.txt`:

Old (lines 73-74):

```cmake
             test/N8ReliableLink.test.cpp  # the reliable link's sender half, over LinkRomModel.hpp's ROM rules
             test/N8Host.test.cpp
```

New:

```cmake
             test/N8ReliableLink.test.cpp  # the reliable link's sender half, over LinkRomModel.hpp's ROM rules
             test/N8MenuGuard.test.cpp     # the menu guard's decisions, driven by hand
             test/N8Host.test.cpp
```

- [ ] **Step 2: run them and see them fail**

```
timeout -k 10 1800 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh cmake --build /workspaces/RetroPlug-main/cuttlefish/build --target retroplug-n8-test -j$(nproc)
```

Expected: the build fails compiling `N8MenuGuard.test.cpp`: `fatal error: transport/N8MenuGuard.hpp: No such file or
directory`.

- [ ] **Step 3: the guard**

Create `packages/n8-wire/native/src/transport/N8MenuGuard.hpp`:

```cpp
#pragma once

#include <cstddef>
#include <cstdint>

#include "transport/N8ReliableLink.hpp"

// The N8 menu guard's decisions (docs/n8-menu-guard-reset-design.md, docs/pico-box-framed-link-design.md section 2):
// whether the cart a stream goes to is at its menu, which reads the FIFO a game takes MIDI from as its own commands
// (0x2A 0x73 is '*s'), so a stream into it must stop. It asks with a probe, kProbe: '*t' inside a SysEx. The N8 menu
// skips every byte that is not a '*', so it reads the '*t' and answers 'k' (kAnswer) on the back-channel, after a
// console RESET too, which leaves the FPGA config block's map_idx at the game's mapper; a MIDI game ignores a SysEx it
// does not know, and BlipToaster, on the real cart, ignores this one. The answer is a 'k' outside an ack (Scan), which
// only the menu writes.
//
// The decisions alone: no I/O, no clock of its own and no allocation, so the desktop's N8Link (packages/n8) and the
// Pico box's cart link (pico/n8-host's BoxCartLink) run the one guard over their own transports. A driver hands it the
// back-channel as it reads it (hear(), after the reliable sender's onBackChannel on a framed link), then takes the
// turn() its stream is at, with the time (int64 nanoseconds of any monotonic clock, never 0: 0 is "no probe awaited")
// and its sender (null on a raw link), and does what the turn says, in this order: drop what it holds when the menu
// answered (enteredMenu); probe the link again when the menu was left (leftMenu) or a stalled window given up
// (reprobeLink, what the stream holds and the window dropped first); write kProbe when writeProbe says; and, unless
// the turn holds, the sender's re-sends at the same `now` and the stream's new messages. Not thread-safe: one thread
// drives it.
//
// When it asks:
// - raw, every 250 ms between two messages, as part of the stream: no read and no wait, the answer is heard on a later
//   turn. A raw link has no acks, so nothing else tells a menu the console's reset reached from a game.
// - framed, only once the window has STOPPED MOVING (the oldest frame unacknowledged past the link's own timeout, which
//   is the console reset), holding new frames and re-sends until a 'k' (the menu) or an ack (a game whose acks are
//   merely lost or damaged: its stream goes on, and a stall is not asked about again for a second, a trusted second
//   that a window which drains ends), and asking again each second that hears neither. A framed game never sees the
//   probe outside a stall. A live BlipToaster drops the probe's bytes as unframed and acknowledges F5 <next> <next> for
//   them (bliptoaster src/midi/parse_asm.s), so a game whose own acks were lost still answers the stall. Two stall
//   probes that hear nothing at all are neither the menu nor this link's game: a game without the link, started from
//   the cart's browser after a reset the guard never saw (a framed link asks only in a stall). The turn then asks for
//   the window and what the stream held for it to go, and the link to be probed again: a game with the link re-syncs
//   framed, any other is streamed raw.
// - at the menu, every 250 ms whatever the link: a game the user starts from the cart's own browser answers no probe,
//   and three left unanswered in a row leave the menu (the driver probes the link again); a single miss is a slow turn
//   of the menu's, and leaving on it would stream into the menu until the next probe.

namespace rp {

class N8MenuGuard {
public:
    static constexpr std::uint8_t kProbe[4] = {0xF0, 0x2A, 0x74, 0xF7};
    static constexpr std::uint8_t kAnswer   = 0x6B;
    // How often it asks (raw, and at the menu whatever the link); how long the menu's answer may take before the cart
    // is taken for a game, which ignores the probe; and how long a framed hold waits for an answer or an ack before it
    // asks again, which is also the trusted second. The lab's menu answers in about 20 ms (27 ms the worst of 8, at a
    // power-on menu), so the answer window has room to spare.
    static constexpr std::int64_t kProbeNs  = 250'000'000;
    static constexpr std::int64_t kAnswerNs = 100'000'000;
    static constexpr std::int64_t kHoldNs   = 1'000'000'000;
    // How many probes in a row the menu may leave unanswered before the cart is taken for a game (one slow turn of the
    // menu's loop must not stream into it: about 750 ms for a game booted by hand), and how many stall probes a framed
    // link may hear nothing at all to before it gives the window up.
    static constexpr int kLeaveMisses = 3;
    static constexpr int kStallMisses = 2;

    // The back-channel, a byte at a time: the menu's answer is a 'k' OUTSIDE an ack. An ack is F5 <next> <last>, both
    // 7-bit (N8ReliableSender::onBackChannel), so either can be a 0x6B, and the scan follows the same states to tell.
    // Its state runs on across reads, since an ack can be split between two: a driver that drops input (a flush)
    // starts it afresh, and one whose link probe stopped reading halfway through an ack sets where it stopped.
    struct Scan {
        enum class Heard { Nothing, Ack, MenuAnswer };
        int   state = 0;  // 0 between acks, 1 after F5, 2 after <next>
        Heard feed(std::uint8_t b) {
            if (b == N8ReliableSender::FRAME) {
                state = 1;
                return Heard::Nothing;
            }
            if (b & 0x80) {
                state = 0;
                return Heard::Nothing;
            }
            if (state == 1) {
                state = 2;
                return Heard::Nothing;
            }
            if (state == 2) {
                state = 0;
                return Heard::Ack;
            }
            return b == kAnswer ? Heard::MenuAnswer : Heard::Nothing;
        }
    };

    // What a turn tells its driver (see the top of this file for the order it acts in).
    struct Turn {
        bool hold        = false;  // nothing new, no re-send and no note release goes out on this turn
        bool writeProbe  = false;  // write kProbe now (on a raw link the turn was told it is between two messages)
        bool reprobeLink = false;  // drop what the stream holds and the window, and probe the link again
        bool enteredMenu = false;  // the menu answered: drop what the stream holds, the window and any pending release
        bool leftMenu    = false;  // the menu left three probes unanswered: a game, whose link is probed again
    };

    // Afresh, as at a connect: not at the menu, no probe awaited or due, nothing heard.
    void reset() { *this = N8MenuGuard{}; }
    // The state a connect's own check found (N8Host's askMenu, before the stream starts): set, with nothing to enter.
    void seed(bool menu) { atMenu_ = menu; }
    bool atMenu() const { return atMenu_; }
    Scan&       scan() { return scan_; }
    const Scan& scan() const { return scan_; }

    // What the cart has said on the back-channel: its whole acks counted, and the menu's 'k' remembered for the turn.
    void hear(const std::uint8_t* data, std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) {
            const Scan::Heard heard = scan_.feed(data[i]);
            if (heard == Scan::Heard::Ack) ++acksHeard_;
            else if (heard == Scan::Heard::MenuAnswer) menuAnswered_ = true;
        }
    }

    // Where the cart runs, as a probe outside the guard's own said it (N8Host's for a submission), or as a turn decides
    // it: true when that is a change, which the driver then acts on as a turn's enteredMenu or leftMenu. A probe of the
    // guard's still awaited when the menu is entered is the menu's to answer: whatever becomes of its 'k', it is not
    // "nothing answered", which at the menu would read as a game.
    bool report(bool menu) {
        if (menu == atMenu_) return false;
        atMenu_      = menu;
        probeAtNs_   = 0;
        stallMisses_ = 0;
        gameUntilNs_ = 0;
        if (menu) menuMisses_ = 0;
        return true;
    }

    // The turn at `now`: what the stream may write, and what the driver does first (see the top of this file).
    // `sender` is the reliable link's (null on a raw link); `betweenMessages` whether the stream's own bytes stand
    // between two messages, where the raw link's probe may go.
    Turn turn(std::int64_t now, const N8ReliableSender* sender, bool betweenMessages) {
        Turn t;
        if (menuAnswered_) {
            menuAnswered_ = false;
            probeAtNs_    = 0;
            menuMisses_   = 0;
            t.enteredMenu = report(true);
        }
        if (atMenu_) {
            if (probeAtNs_ != 0 && now - probeAtNs_ >= kAnswerNs) {
                probeAtNs_ = 0;
                if (++menuMisses_ >= kLeaveMisses) {
                    menuMisses_ = 0;
                    t.leftMenu  = report(false);
                    t.hold      = atMenu_;
                    return t;
                }
            }
            if (probeAtNs_ == 0 && now >= probeDueNs_) probe(now, t);
            t.hold = true;  // nothing new goes out while it says menu
            return t;
        }
        if (sender && sender->progressOverdue(now) && now >= gameUntilNs_) {
            if (probeAtNs_ != 0 && acksHeard_ != acksAtProbe_) {
                // An ack since the stall's probe: a game, whose own acks were lost or damaged. It streams on.
                probeAtNs_   = 0;
                stallMisses_ = 0;
                gameUntilNs_ = now + kHoldNs;
                return t;
            }
            if (probeAtNs_ != 0 && now - probeAtNs_ >= kHoldNs && ++stallMisses_ >= kStallMisses) {
                probeAtNs_     = 0;
                stallMisses_   = 0;
                t.reprobeLink  = true;
                return t;
            }
            if (probeAtNs_ == 0 || now - probeAtNs_ >= kHoldNs) probe(now, t);
            t.hold = true;  // hold the re-send until the cart has answered
            return t;
        }
        if (sender) {
            // The window moves (or a stall's ack was heard within the second): no probe of a stall is awaited any
            // more, and a miss left from an earlier stall does not count toward the next one's drop. A window that has
            // drained ends the trusted second too: it vouched for the game that was stalled, and the next frame's stall
            // (a RESET since) is asked about at the link's own timeout, which on a lossy link is otherwise a second
            // away.
            probeAtNs_   = 0;
            stallMisses_ = 0;
            if (sender->outstanding() == 0) gameUntilNs_ = 0;
            return t;
        }
        if (probeAtNs_ != 0 && now - probeAtNs_ >= kAnswerNs) probeAtNs_ = 0;  // unanswered: a game
        if (probeAtNs_ == 0 && now >= probeDueNs_ && betweenMessages) probe(now, t);
        return t;
    }

    // A probe's answer may still be coming: a read of a reply now could take the menu's 'k' for its first byte.
    bool probeAwaited(std::int64_t now) const { return probeAtNs_ != 0 && now - probeAtNs_ < kAnswerNs; }

private:
    // The probe goes out on this turn: booked here, so a driver whose write then fails has the state a probe written
    // leaves (the failure ends its stream either way).
    void probe(std::int64_t now, Turn& t) {
        probeAtNs_   = now;
        probeDueNs_  = now + kProbeNs;
        acksAtProbe_ = acksHeard_;
        t.writeProbe = true;
    }

    bool          atMenu_       = false;
    // When the last probe went out (0: none awaiting its answer), when the next is due, and the moment before which a
    // stopped window is not probed again, because the last probe's stall heard an ack (a game).
    std::int64_t  probeAtNs_    = 0;
    std::int64_t  probeDueNs_   = 0;
    std::int64_t  gameUntilNs_  = 0;
    // Probes in a row the menu left unanswered (it is left at kLeaveMisses), and stall probes in a row that heard
    // nothing at all (the link is given up at kStallMisses).
    int           menuMisses_   = 0;
    int           stallMisses_  = 0;
    // What the back-channel has said: the menu's 'k', and how many whole acks, against the count when the probe went.
    bool          menuAnswered_ = false;
    Scan          scan_;
    std::uint64_t acksHeard_    = 0;
    std::uint64_t acksAtProbe_  = 0;
};

}  // namespace rp
```

- [ ] **Step 4: run the guard's cases and see them pass**

```
timeout -k 10 1800 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh cmake --build /workspaces/RetroPlug-main/cuttlefish/build --target retroplug-n8-test -j$(nproc)
timeout -k 10 300 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh /workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-n8-test "menu guard:*"
```

Expected: exit 0, `All tests passed` over the seven `menu guard:` cases (the filter is their names' prefix; the
`[menu-guard]` tag also holds `N8CartJobs.test.cpp`'s cases, which step 6 runs with the rest).

- [ ] **Step 5: `N8Link` drives the guard**

In `packages/n8/native/src/n8/desktop/N8Link.hpp`:

Old (lines 16-17):

```cpp
#include "transport/N8ReliableLink.hpp"
#include "transport/SpscRing.hpp"
```

New:

```cpp
#include "transport/N8MenuGuard.hpp"
#include "transport/N8ReliableLink.hpp"
#include "transport/SpscRing.hpp"
```

Old (lines 68-98):

```cpp
    // The guard's question: '*t' inside a SysEx. The N8 menu skips every byte that is not a '*', so it reads the '*t'
    // and answers 'k' (kMenuAnswer) on the back-channel, after a console RESET too, which leaves the FPGA config
    // block's map_idx at the game's mapper (docs/n8-menu-guard-reset-design.md); a MIDI game ignores a SysEx it does
    // not know, and BlipToaster, on the real cart, ignores this one.
    static constexpr std::uint8_t kMenuProbe[4] = {0xF0, 0x2A, 0x74, 0xF7};
    static constexpr std::uint8_t kMenuAnswer   = 0x6B;
    // The back-channel, a byte at a time: the menu's answer is a 'k' OUTSIDE an ack. An ack is F5 <next> <last>, both
    // 7-bit (N8ReliableSender::onBackChannel), so either can be a 0x6B, and the scan follows the same states to tell.
    struct BackChannelScan {
        enum class Heard { Nothing, Ack, MenuAnswer };
        int state = 0;  // 0 between acks, 1 after F5, 2 after <next>
        Heard feed(std::uint8_t b) {
            if (b == rp::N8ReliableSender::FRAME) {
                state = 1;
                return Heard::Nothing;
            }
            if (b & 0x80) {
                state = 0;
                return Heard::Nothing;
            }
            if (state == 1) {
                state = 2;
                return Heard::Nothing;
            }
            if (state == 2) {
                state = 0;
                return Heard::Ack;
            }
            return b == kMenuAnswer ? Heard::MenuAnswer : Heard::Nothing;
        }
    };
```

New:

```cpp
    // The guard's question ('*t' inside a SysEx), the menu's answer ('k') and the back-channel scan that tells that
    // answer from an ack's bytes: the menu guard's own (transport/N8MenuGuard.hpp, the n8-wire package's, which the
    // Pico box's cart link runs too), named here for N8Host's probe, which writes the question and reads the answer on
    // the link's port itself.
    static constexpr std::uint8_t kMenuProbe[4] = {rp::N8MenuGuard::kProbe[0], rp::N8MenuGuard::kProbe[1],
                                                   rp::N8MenuGuard::kProbe[2], rp::N8MenuGuard::kProbe[3]};
    static constexpr std::uint8_t kMenuAnswer   = rp::N8MenuGuard::kAnswer;
    using BackChannelScan                       = rp::N8MenuGuard::Scan;
```

Old (lines 252-259):

```cpp
    // serial thread: the cart's at-menu state for this turn (see MenuCheck). True while the stream must put no new
    // chunk, re-send or release on the wire: the cart is at its menu, or the window has stopped moving and the guard's
    // probe has heard neither the menu's answer nor an ack yet.
    bool        menuHold(std::int64_t now);
    void        writeMenuProbe(std::int64_t now);              // serial thread: kMenuProbe out, between two messages
    bool        menuProbeAwaited(std::int64_t now) const;     // serial thread: a probe's answer may still be coming
    void        enterMenu();                                    // serial thread: discard what is held, reset the link
    void        leaveMenu();                                    // serial thread: probe the link again
```

New:

```cpp
    // serial thread: the menu guard's turn (guard_, see MenuCheck), carried out on the link's port. True while the
    // stream must put no new chunk, re-send or release on the wire: the cart is at its menu, or the window has stopped
    // moving and the guard's probe has heard neither the menu's answer nor an ack yet.
    bool        menuHold(std::int64_t now);
    void        writeMenuProbe(std::int64_t now);              // serial thread: kMenuProbe out (the guard booked it)
    bool        menuProbeAwaited(std::int64_t now) const;     // serial thread: a probe's answer may still be coming
    void        enterMenu();                                    // serial thread: discard what is held, reset the link
    void        leaveMenu();                                    // serial thread: probe the link again
```

Old (lines 341-357):

```cpp
    // The guard's probe (menuHold): when the last one went out (0: none awaiting its answer), when the next is due, and
    // the moment before which a stopped window is not probed again, because the last probe's stall heard an ack (a
    // game). Serial thread, reset at each connect.
    std::int64_t               menuProbeAtNs_   = 0;
    std::int64_t               menuProbeDueNs_  = 0;
    std::int64_t               menuGameUntilNs_ = 0;
    // Probes in a row the menu left unanswered (it leaves at kMenuLeaveMisses), and stall probes in a row that heard
    // nothing at all (the link is given up at kStallMisses). Serial thread, reset at each connect.
    int                        menuMisses_  = 0;
    int                        stallMisses_ = 0;
    // What the back-channel has said (readBackChannel): the menu's 'k', and how many whole acks against the count when
    // the probe went out. The scan's state runs on across reads, since an ack can be split between two. Serial thread,
    // reset at each connect.
    bool                       menuAnswered_ = false;
    BackChannelScan            backScan_;
    std::uint64_t              acksHeard_   = 0;
    std::uint64_t              acksAtProbe_ = 0;
```

New:

```cpp
    // The menu guard (transport/N8MenuGuard.hpp): its probe's timing, the misses, the trusted second, the back-channel
    // scan and what it heard. Serial thread, reset at each connect and disconnect; atMenu_ above is its state for
    // other threads.
    rp::N8MenuGuard            guard_;
```

In `packages/n8/native/src/n8/desktop/N8Link.cpp`:

Old (lines 16-28):

```cpp
// How often the guard asks where the cart runs (on a raw link, and at the menu whatever the link); how long the menu's
// answer may take before the cart is taken for a game, which ignores the probe (N8Host's seed waits as long, its
// kMenuAnswerMs); and how long a framed hold waits for an answer or an ack before it asks again. The lab's menu
// answers in about 20 ms (27 ms the worst of 8, at a power-on menu), so the answer window has room to spare.
constexpr std::int64_t kMenuProbeNs  = 250'000'000;
constexpr std::int64_t kMenuAnswerNs = 100'000'000;
constexpr std::int64_t kMenuHoldNs   = 1'000'000'000;
// How many probes in a row the menu may leave unanswered before the cart is taken for a game (one slow turn of the
// menu's loop must not stream into it: about 750 ms for a game booted by hand), and how many stall probes a framed link
// may hear nothing at all to before it gives the window up (see menuHold).
constexpr int kMenuLeaveMisses = 3;
constexpr int kStallMisses     = 2;
// RP_N8LINK_TRACE=1 logs every byte the link writes and every back-channel byte it reads (stderr) - read once.
```

New:

```cpp
// RP_N8LINK_TRACE=1 logs every byte the link writes and every back-channel byte it reads (stderr) - read once.
```

Old (lines 76-77):

```cpp
    backScan_ = {};
    serialPort_->flushInput();
```

New:

```cpp
    guard_.scan() = {};
    serialPort_->flushInput();
```

Old (lines 127-144):

```cpp
    atMenu_.store(false, std::memory_order_relaxed);
    menuProbeAtNs_   = 0;
    menuProbeDueNs_  = 0;
    menuGameUntilNs_ = 0;
    menuAnswered_    = false;
    menuMisses_      = 0;
    stallMisses_     = 0;
    backScan_        = {};
    acksHeard_       = 0;
    acksAtProbe_     = 0;
    if (menuCheck_) {
        try {
            // What the cart said after the handshake is not this check's answer: a flush leaves its reply on its own.
            flushPort();
            atMenu_.store(menuCheck_(*edio_) == 1, std::memory_order_relaxed);
        } catch (const std::exception&) {
        }
    }
```

New:

```cpp
    atMenu_.store(false, std::memory_order_relaxed);
    guard_.reset();
    if (menuCheck_) {
        try {
            // What the cart said after the handshake is not this check's answer: a flush leaves its reply on its own.
            flushPort();
            const bool menu = menuCheck_(*edio_) == 1;
            guard_.seed(menu);
            atMenu_.store(menu, std::memory_order_relaxed);
        } catch (const std::exception&) {
        }
    }
```

Old (lines 163-166):

```cpp
    atMenu_.store(false, std::memory_order_relaxed);
    menuProbeAtNs_   = 0;
    menuGameUntilNs_ = 0;
    rel_.reset();
```

New:

```cpp
    atMenu_.store(false, std::memory_order_relaxed);
    guard_.reset();
    rel_.reset();
```

Old (line 295):

```cpp
                backScan_.state = i + 1 < n ? 0 : 2;
```

New:

```cpp
                guard_.scan().state = i + 1 < n ? 0 : 2;
```

Old (lines 350-357):

```cpp
    if (n && rel_) rel_->onBackChannel(buf, n, now);
    if (!menuCheck_) return;
    for (std::size_t i = 0; i < n; ++i) {
        const BackChannelScan::Heard heard = backScan_.feed(buf[i]);
        if (heard == BackChannelScan::Heard::Ack) ++acksHeard_;
        else if (heard == BackChannelScan::Heard::MenuAnswer) menuAnswered_ = true;
    }
}
```

New:

```cpp
    if (n && rel_) rel_->onBackChannel(buf, n, now);
    if (menuCheck_) guard_.hear(buf, n);
}
```

Old (lines 376-453):

```cpp
// The cart's at-menu state for this turn, asked with the guard's probe (kMenuProbe) and answered by the menu's 'k' on
// the back-channel (readBackChannel), which trips the guard whenever it is heard: only the menu answers. When it asks:
// - raw, every 250 ms between two messages, as part of the stream: no read and no wait, the answer is heard on a later
//   turn. A raw link has no acks, so nothing else tells a menu the console's reset reached from a game.
// - framed, only once the window has STOPPED MOVING (the oldest frame unacknowledged past the link's own timeout, which
//   is the console reset), holding new frames and re-sends until a 'k' (the menu) or an ack (a game whose acks are
//   merely lost or damaged: its stream goes on, and a stall is not asked about again for a second), and asking again
//   each second that hears neither. A framed game never sees the probe outside a stall. A live BlipToaster drops the
//   probe's bytes as unframed and acknowledges F5 <next> <next> for them (bliptoaster src/midi/parse_asm.s), so a game
//   whose own acks were lost still answers the stall. Two stall probes that hear nothing at all are neither the menu
//   nor this link's game: a game without the link, started from the cart's browser after a reset the guard never saw
//   (a framed link asks only in a stall). The window and what the ring held for it go, counted, and the link is
//   probed again: a game with the link re-syncs framed, any other is streamed raw.
// - at the menu, every 250 ms whatever the link: a game the user starts from the cart's own browser answers no probe,
//   and three left unanswered in a row leave the menu (leaveMenu runs the link probe again); a single miss is a slow
//   turn of the menu's, and leaving on it would stream into the menu until the next probe.
bool N8Link::menuHold(std::int64_t now) {
    if (!menuCheck_) return false;
    if (menuAnswered_) {
        menuAnswered_  = false;
        menuProbeAtNs_ = 0;
        menuMisses_    = 0;
        reportMenuState(true);
    }
    if (atMenu_.load(std::memory_order_relaxed)) {
        if (menuProbeAtNs_ != 0 && now - menuProbeAtNs_ >= kMenuAnswerNs) {
            menuProbeAtNs_ = 0;
            if (++menuMisses_ >= kMenuLeaveMisses) {
                menuMisses_ = 0;
                reportMenuState(false);
                return atMenu_.load(std::memory_order_relaxed);
            }
        }
        if (menuProbeAtNs_ == 0 && now >= menuProbeDueNs_) writeMenuProbe(now);
        return true;  // nothing new goes out while it says menu
    }
    if (rel_ && rel_->progressOverdue(now) && now >= menuGameUntilNs_) {
        if (menuProbeAtNs_ != 0 && acksHeard_ != acksAtProbe_) {
            menuProbeAtNs_   = 0;
            stallMisses_     = 0;
            menuGameUntilNs_ = now + kMenuHoldNs;
            return false;
        }
        if (menuProbeAtNs_ != 0 && now - menuProbeAtNs_ >= kMenuHoldNs && ++stallMisses_ >= kStallMisses) {
            menuProbeAtNs_ = 0;
            stallMisses_   = 0;
            discardRing();
            rel_->reset();
            linkOutstanding_.store(0, std::memory_order_relaxed);
            probeLink();
            return false;
        }
        if (menuProbeAtNs_ == 0 || now - menuProbeAtNs_ >= kMenuHoldNs) writeMenuProbe(now);
        return true;  // hold the re-send until the cart has answered
    }
    if (rel_) {
        // The window moves (or a stall's ack was heard within the second): no probe of a stall is awaited any more,
        // and a miss left from an earlier stall does not count toward the next one's drop. A window that has drained
        // ends the trusted second too: it vouched for the game that was stalled, and the next frame's stall (a RESET
        // since) is asked about at the link's own timeout, which on a lossy link is otherwise a second away.
        menuProbeAtNs_ = 0;
        stallMisses_   = 0;
        if (rel_->outstanding() == 0) menuGameUntilNs_ = 0;
        return false;
    }
    if (menuProbeAtNs_ != 0 && now - menuProbeAtNs_ >= kMenuAnswerNs) menuProbeAtNs_ = 0;  // unanswered: a game
    if (menuProbeAtNs_ == 0 && now >= menuProbeDueNs_ && betweenMessages()) writeMenuProbe(now);
    return false;
}

// The guard's probe, out: unframed, at a point the caller has made sure is between two messages, and on a raw link
// through the tracker as any message is, since a SysEx ends running status on the cart's wire too (the stream's next
// chunk gets its status written back, see writeChunk). Not counted as forwarded: it is the link's question, not MIDI.
void N8Link::writeMenuProbe(std::int64_t now) {
    menuProbeAtNs_  = now;
    menuProbeDueNs_ = now + kMenuProbeNs;
    acksAtProbe_    = acksHeard_;
    if (!rel_) noteWireBytes(kMenuProbe, sizeof kMenuProbe, now, false);
```

New:

```cpp
// The cart's at-menu state for this turn: the menu guard's (transport/N8MenuGuard.hpp, which says when it asks and
// what it takes for the menu's answer), carried out here on the link's port in the order the guard gives: the menu's
// 'k' drops what the link holds (enterMenu); leaving the menu (leaveMenu) or a stalled window two probes heard nothing
// to (the window and what the ring held for it go, counted) probes the link again, so a game with the link re-syncs
// framed and any other is streamed raw; then the probe goes out when the guard asks for it. A link with no MenuCheck
// has no guard.
bool N8Link::menuHold(std::int64_t now) {
    if (!menuCheck_) return false;
    const rp::N8MenuGuard::Turn turn = guard_.turn(now, rel_.get(), betweenMessages());
    if (turn.enteredMenu) {
        atMenu_.store(true, std::memory_order_relaxed);
        enterMenu();
    }
    if (turn.leftMenu) {
        atMenu_.store(false, std::memory_order_relaxed);
        leaveMenu();
    }
    if (turn.reprobeLink) {
        discardRing();
        rel_->reset();
        linkOutstanding_.store(0, std::memory_order_relaxed);
        probeLink();
    }
    if (turn.writeProbe) writeMenuProbe(now);
    return turn.hold;
}

// The guard's probe, out: unframed, at a point the guard was told is between two messages, and on a raw link through
// the tracker as any message is, since a SysEx ends running status on the cart's wire too (the stream's next chunk
// gets its status written back, see writeChunk). Not counted as forwarded: it is the link's question, not MIDI. The
// guard has already booked it (its time, the next one's, the acks it is judged against).
void N8Link::writeMenuProbe(std::int64_t now) {
    if (!rel_) noteWireBytes(kMenuProbe, sizeof kMenuProbe, now, false);
```

(The rest of `writeMenuProbe`, its trace, write log and `fifoWR`, stays as it is.)

Old (lines 469-483):

```cpp
bool N8Link::menuProbeAwaited(std::int64_t now) const {
    return menuProbeAtNs_ != 0 && now - menuProbeAtNs_ < kMenuAnswerNs;
}

void N8Link::reportMenuState(bool menu) {
    // A link with no MenuCheck has no guard - a fake device's, or a case that turned it off on purpose - so there is
    // nothing for a probe's answer to feed.
    if (!menuCheck_) return;
    if (menu == atMenu_.load(std::memory_order_relaxed)) return;
    atMenu_.store(menu, std::memory_order_relaxed);
    if (menu)
        enterMenu();
    else
        leaveMenu();
}
```

New:

```cpp
bool N8Link::menuProbeAwaited(std::int64_t now) const { return guard_.probeAwaited(now); }

void N8Link::reportMenuState(bool menu) {
    // A link with no MenuCheck has no guard - a fake device's, or a case that turned it off on purpose - so there is
    // nothing for a probe's answer to feed.
    if (!menuCheck_) return;
    if (!guard_.report(menu)) return;
    atMenu_.store(menu, std::memory_order_relaxed);
    if (menu)
        enterMenu();
    else
        leaveMenu();
}
```

Old (lines 506-513):

```cpp
    }
    // A probe of the guard's still awaited is the menu's to answer: whatever becomes of its 'k', it is not "nothing
    // answered", which at the menu would read as a game.
    menuProbeAtNs_   = 0;
    menuMisses_      = 0;
    stallMisses_     = 0;
    menuGameUntilNs_ = 0;
    if (traceOn()) std::fprintf(stderr, "[n8link] the cart is at its menu: the stream is dropped\n");
```

New:

```cpp
    }
    if (traceOn()) std::fprintf(stderr, "[n8link] the cart is at its menu: the stream is dropped\n");
```

Old (lines 517-520):

```cpp
    if (traceOn()) std::fprintf(stderr, "[n8link] the cart runs a game: streaming, and probing the link\n");
    menuProbeAtNs_   = 0;
    stallMisses_     = 0;
    menuGameUntilNs_ = 0;
```

New:

```cpp
    if (traceOn()) std::fprintf(stderr, "[n8link] the cart runs a game: streaming, and probing the link\n");
```

Old (line 540):

```cpp
    if (menuProbeAwaited(now) || backScan_.state != 0) return false;
```

New:

```cpp
    if (menuProbeAwaited(now) || guard_.scan().state != 0) return false;
```

After these, this prints nothing (every one of the guard's old members and constants is gone from `N8Link`):

```
grep -n 'menuProbeAtNs_\|menuProbeDueNs_\|menuGameUntilNs_\|menuMisses_\|stallMisses_\|menuAnswered_\|backScan_\|acksHeard_\|acksAtProbe_\|kMenuProbeNs\|kMenuAnswerNs\|kMenuHoldNs\|kMenuLeaveMisses\|kStallMisses' /workspaces/RetroPlug-main/cuttlefish/packages/n8/native/src/n8/desktop/N8Link.hpp /workspaces/RetroPlug-main/cuttlefish/packages/n8/native/src/n8/desktop/N8Link.cpp
```

In `packages/n8/native/src/n8/desktop/N8Host.cpp`:

Old (lines 85-87):

```cpp
// How long the N8 menu's answer to a probe may take: one USB round trip and a turn of the menu's loop. Past it nothing
// answered, and the cart runs a game. The link waits as long for the answer to the probe it streams (N8Link.cpp's
// kMenuAnswerNs).
```

New:

```cpp
// How long the N8 menu's answer to a probe may take: one USB round trip and a turn of the menu's loop. Past it nothing
// answered, and the cart runs a game. The link waits as long for the answer to the probe it streams (the menu guard's
// kAnswerNs, transport/N8MenuGuard.hpp).
```

In `packages/n8-wire/native/CMakeLists.txt` (as task 1 left it), name the third header:

Old:

```cmake
# Header-only, so an INTERFACE target: a consumer gets this package's include root, where the two headers keep the paths
# they are included by ("transport/N8ReliableLink.hpp", "n8wire/ExpansionVolume.hpp"). Both are the standard library's
# alone and heap-free (the link's window is a ring inside the sender), so the Pico box's firmware links the link under
# the device build's trap; node-sdk's headers, which this package declares as every native package does, come along
# with the link below and are not included by either. A build adds it too, between node-sdk and the packages that list
# it.
```

New:

```cmake
# Header-only, so an INTERFACE target: a consumer gets this package's include root, where the headers keep the paths
# they are included by ("transport/N8ReliableLink.hpp", "transport/N8MenuGuard.hpp", "n8wire/ExpansionVolume.hpp"). All
# three are the standard library's alone and heap-free (the link's window is a ring inside the sender, and the menu
# guard's decisions do no I/O), so the Pico box's firmware links the link and the guard under the device build's
# trap; node-sdk's headers, which this package declares as every native package does, come along with the link below
# and are included by none of them. A build adds it too, between node-sdk and the packages that list it.
```

Also in that file, line 1-3's first sentence names "the one sender (rp::N8ReliableSender) each side drives": leave it.

In `packages/n8-wire/package.json` (as task 1 left it):

Old:

```json
  "description": "The wire a real Everdrive N8 Pro cart and the emulated NES FIFO that stands in for it share, and neither owns: BlipToaster's reliable link, whose one sender the cart's desktop link, the Pico box's cart link and the emulator's FIFO role all drive, and the expansion-volume range the emulated chip's knob and the cart's master-volume register are set from. Headers and no code, heap-free: the range is compiled by every device build, through rp.n8.nes, and the link by the Pico box's firmware under the device build's link trap.",
```

New:

```json
  "description": "The wire a real Everdrive N8 Pro cart and the emulated NES FIFO that stands in for it share, and neither owns: BlipToaster's reliable link, whose one sender the cart's desktop link, the Pico box's cart link and the emulator's FIFO role all drive; the N8 menu guard's decisions, which the desktop link and the box's cart link share; and the expansion-volume range the emulated chip's knob and the cart's master-volume register are set from. Headers and no code, heap-free: the range is compiled by every device build, through rp.n8.nes, and the link and the guard by the Pico box's firmware under the device build's link trap.",
```

- [ ] **Step 6: run the n8 suites and see them pass**

The guard's own cases, then `N8Link`'s guard and job cases unedited (`N8CartJobs.test.cpp`'s `[menu-guard]`,
`N8CartLink.test.cpp`, `N8Host.test.cpp`), the whole n8 binary three times (it has timing cases), the engine-graph
binary (its `N8Adapters` case drives `N8Link` behind a real Engine), the n8 runners, the TSan build of the n8 binary
(the guard's state is the serial thread's, read across threads only through `atMenu_` and `backChannelQuiet_`), and
the hardware test compiled, never run:

```
timeout -k 10 1800 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh cmake --build /workspaces/RetroPlug-main/cuttlefish/build --target retroplug-n8-test retroplug-engine-graph-test retroplug-device-mode-test retroplug-host retroplug-n8-hwtest -j$(nproc)
timeout -k 10 300 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh /workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-n8-test "[menu-guard]"
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

Expected: exit 0 for each; every Catch2 summary with no failure (the `[menu-guard]` run holds the seven new cases and
`N8CartJobs.test.cpp`'s, all passing unedited); each runner's passed summary with no failure (`test:n8`'s
and `test:native n8`'s); no ThreadSanitizer report. A guard or job case that fails is a behaviour the
move changed: compare `N8Link::menuHold`'s old body above with the guard's `turn` and its driver, do not edit the case.

- [ ] **Step 7: commit**

```
git -C /workspaces/RetroPlug-main/cuttlefish add packages/n8-wire/native/src/transport/N8MenuGuard.hpp \
  packages/n8-wire/native/CMakeLists.txt packages/n8-wire/package.json \
  packages/n8/native/src/n8/desktop/N8Link.hpp packages/n8/native/src/n8/desktop/N8Link.cpp \
  packages/n8/native/src/n8/desktop/N8Host.cpp packages/n8/native/CMakeLists.txt \
  packages/n8/native/test/N8MenuGuard.test.cpp
git -C /workspaces/RetroPlug-main/cuttlefish commit -F - <<'MSG'
n8-wire: the menu guard's decisions, shared by N8Link and the box

The guard's decisions lived in N8Link::menuHold, beside the port I/O they
drive, so the Pico box could not run them. They move into rp::N8MenuGuard, a
header of the n8-wire package with no I/O, no clock and no allocation, with
the back-channel scan beside them: each turn is given the time, the sender
(null on a raw link) and whether the stream is between messages, the
back-channel is handed to it as it is read, and it answers whether to hold,
whether to write the probe, whether to probe the link again and whether the
menu state changed, and whether a probe's answer may still be coming. Its
constants are N8Link's. N8Link drives it in the order menuHold acted, and keeps
kMenuProbe, kMenuAnswer and BackChannelScan for N8Host's probe; its guard and
job cases pass unedited. The guard's rules get a case each.
MSG
```

---

## Task 3: the box's cart link gets the framed link and the guard

`BoxCartLink` writes the ring to the cart FIFO raw. This task gives it the back-channel (`Writer::read`), the link's
64-bit time (`Clock::monotonicUs`), the sender (task 1) and the guard (task 2), all in its `.bss` `Storage`: a connect
writes the link's sync and listens for its ack across passes without blocking, each drain then reads the back-channel,
takes the guard's turn, puts a due probe at the front of its first write, and, unless the guard holds, writes the
re-sends and the ring framed (or raw), in writes of up to 192 bytes that never split a frame, framing no more than
the window less one at a time (the rest waits for the next drain's acks). It answers `atMenu()`
and `quietForRead()` and counts `resends`, `resent` and `unframed`. Without a `read` or a `monotonicUs` it is the raw
link it was, byte for byte, so every existing `[box]` case and the device-mode whole pass run unedited. It stays
portable, so the desktop's test builds it, and heap-free, as the device-mode objects (which compile it with no
exceptions and no RTTI) and the firmware need.

The cases are the design's section 5, item 4, against FakeN8 through a `Writer` over a `FakeN8Port` (the cart model
the desktop guard is tested on), a scripted cart where FakeN8 cannot say a thing, and the link's ROM model where bytes
are lost (FakeN8 models intact traffic only); plus a framed device-mode pass under the counting allocator.

**Files** (line numbers are the file at `7fa0d1e17`):

- Modify (rewrite whole): `pico/n8-host/BoxCartLink.hpp` (153 lines), `pico/n8-host/BoxCartLink.cpp` (151 lines).
- Modify: `packages/native/test/device-mode/DeviceModeEntries.hpp:77-89` (`BoxRecord`'s back-channel and clock,
  `BoxCounts`' new counts), `packages/native/test/device-mode/DeviceModeNodes.cpp:58-61` and `:82-98` (the
  device-mode link's read, its clock, its counts).
- Modify: `packages/native/cmake/tests.cmake:881-882` (`packages/n8/native/test` on `retroplug-device-mode-test`'s
  include path, for `FakeN8.hpp`).
- Test: `packages/native/test/device-mode/BoxHost.test.cpp` (two includes; ten cases appended after task 1's).

**Interfaces:**

- Consumes: `rp::N8ReliableSender` (task 1: `SYNC`, `FRAME`, `kWindow`, `kMaxMessage`, `Timing`, `sendMessage`,
  `poll`, `onSent`, `onBackChannel`, `outstanding`, `lastHeardNs`, `oldestFirstSentNs`, `resends`, `framesResent`,
  `framesSent`, `kNever`); `rp::N8MenuGuard` (task 2: `kProbe`, `Turn`, `turn`, `hear`, `scan`, `reset`, `atMenu`,
  `probeAwaited`, `kProbeNs`); in the cases, `retroplug::test::FakeN8` / `FakeN8Port` / `hex` (`FakeN8.hpp`: `menu`,
  `linkRom`, `linkDeaf`, `testReply`, `resetToMenu()`, `error40`, `syncs`, `applied`, `log`, `mu`, `countExact`),
  `rp::n8::Edio::fifoWR`, `rp::n8::test::RomModel`, `frame`, `cat` (`LinkRomModel.hpp`).
- Produces (task 4 uses all of it):
  - `BoxCartLink::Writer` gains a fourth member, `std::size_t (*read)(void* ctx, std::uint8_t* data, std::size_t size)`;
    `BoxCartLink::Clock` a fourth, `std::uint64_t (*monotonicUs)(void* ctx)`. Both null in every brace-initialiser
    that names three members, which keeps the link raw and unguarded.
  - `Counts` gains `resends`, `resent`, `unframed` (`std::uint32_t`).
  - `void setLinkAllowed(bool on)` (default on), `bool framed() const`, `bool atMenu() const`,
    `bool quietForRead() const`, `std::uint32_t outstanding() const`.
  - `connected()` reads false while the guard says the menu.
  - `BoxRecord` gains `backChannel`, `back[64]`, `backCount`, `monotonicUs`; `BoxCounts` gains `resends`, `resent`,
    `unframed`, `framed`.

### Steps

- [ ] **Step 1: write the failing cases**

In `packages/native/test/device-mode/BoxHost.test.cpp`, two includes more.

Old (lines 41-42):

```cpp
#include "BlockDriver.hpp"
#include "BoxCartLink.hpp"
```

New:

```cpp
#include "BlockDriver.hpp"
#include "BoxCartLink.hpp"
#include "FakeN8.hpp"
#include "LinkRomModel.hpp"
```

Old (task 1's line):

```cpp
#include "transport/N8ReliableLink.hpp"
```

New:

```cpp
#include "transport/N8MenuGuard.hpp"
#include "transport/N8ReliableLink.hpp"
```

Then append, after task 1's case at the end of the file:

```cpp

namespace {

// ---- the cart link's framed link and menu guard (docs/pico-box-framed-link-design.md section 5, item 4) ------------

using rp::n8::test::cat;
using rp::n8::test::frame;

const Bytes kProbeBytes(std::begin(rp::N8MenuGuard::kProbe), std::end(rp::N8MenuGuard::kProbe));

// A FIFO write as FakeN8's log records it, and its FIFO writes in order.
std::string fifoEntry(const Bytes& bytes) { return "fifo:" + retroplug::test::hex(bytes.data(), bytes.size()); }
std::vector<std::string> fifoWrites(retroplug::test::FakeN8& dev) {
    std::lock_guard<std::mutex> l(dev.mu);
    std::vector<std::string>    out;
    for (const std::string& e : dev.log)
        if (e.rfind("fifo:", 0) == 0) out.push_back(e);
    return out;
}

// The box's link over FakeN8 (packages/n8/native/test/FakeN8.hpp, the cart model the desktop's guard is tested on),
// through the Writer the box gives it: a FIFO write is Edio's fifoWR over a FakeN8Port, the back-channel is that
// port's input read without waiting, as the box's read(..., 0) is, and the link's clock is the case's own, so the
// guard's 100 ms, 250 ms and 1 s pass as a case moves it. FakeN8 answers at once (its delays stay 0: they would run on
// the wall clock), so no case sleeps. A game runs from the start (FakeN8 starts at its menu otherwise).
struct CartRig {
    retroplug::test::FakeN8                        dev;
    std::unique_ptr<retroplug::test::FakeN8Port>   port = std::make_unique<retroplug::test::FakeN8Port>(dev);
    rp::n8::Edio                                   edio{*port};
    std::unique_ptr<rp::box::BoxCartLink::Storage> ring = std::make_unique<rp::box::BoxCartLink::Storage>();
    rp::box::BoxCartLink                           link{*ring};
    std::uint64_t                                  us = 1'000'000;  // never 0, which the guard reads as "no probe"

    static std::uint32_t now(void* ctx) { return static_cast<std::uint32_t>(static_cast<CartRig*>(ctx)->us); }
    static std::uint64_t monotonic(void* ctx) { return static_cast<CartRig*>(ctx)->us; }
    static bool          fifo(void* ctx, const std::uint8_t* d, std::size_t n, std::uint32_t, std::uint32_t) {
        return static_cast<CartRig*>(ctx)->edio.fifoWR(d, n).ok();
    }
    static std::size_t read(void* ctx, std::uint8_t* d, std::size_t n) {
        return static_cast<CartRig*>(ctx)->port->read(d, n, 0);
    }

    CartRig() {
        dev.menu = false;
        link.setClock({this, &now, nullptr, &monotonic});
    }
    rp::box::BoxCartLink::Writer writer() { return {this, &fifo, nullptr, &read}; }
    bool push(const Bytes& m) { return link.push(0, m.data(), m.size(), false, 48000.0); }
    // One pass's drain, `ms` after the last.
    void drain(std::uint64_t ms = 1) {
        us += ms * 1000;
        link.drain(writer(), static_cast<std::uint32_t>(us / 1000));
    }
};

// A cart whose back-channel the case writes itself, for what FakeN8 never says on its own (an ack cut in two), and
// which takes every FIFO write.
struct ScriptedRig {
    std::vector<Bytes>                             writes;
    Bytes                                          back;  // what the cart has said and the link has not read
    std::unique_ptr<rp::box::BoxCartLink::Storage> ring = std::make_unique<rp::box::BoxCartLink::Storage>();
    rp::box::BoxCartLink                           link{*ring};
    std::uint64_t                                  us = 1'000'000;

    static std::uint32_t now(void* ctx) { return static_cast<std::uint32_t>(static_cast<ScriptedRig*>(ctx)->us); }
    static std::uint64_t monotonic(void* ctx) { return static_cast<ScriptedRig*>(ctx)->us; }
    static bool          fifo(void* ctx, const std::uint8_t* d, std::size_t n, std::uint32_t, std::uint32_t) {
        static_cast<ScriptedRig*>(ctx)->writes.emplace_back(d, d + n);
        return true;
    }
    static std::size_t read(void* ctx, std::uint8_t* d, std::size_t n) {
        auto&             self = *static_cast<ScriptedRig*>(ctx);
        const std::size_t k = n < self.back.size() ? n : self.back.size();
        for (std::size_t i = 0; i < k; ++i) d[i] = self.back[i];
        self.back.erase(self.back.begin(), self.back.begin() + static_cast<std::ptrdiff_t>(k));
        return k;
    }

    ScriptedRig() { link.setClock({this, &now, nullptr, &monotonic}); }
    rp::box::BoxCartLink::Writer writer() { return {this, &fifo, nullptr, &read}; }
    bool push(const Bytes& m) { return link.push(0, m.data(), m.size(), false, 48000.0); }
    void drain(std::uint64_t ms = 1) {
        us += ms * 1000;
        link.drain(writer(), static_cast<std::uint32_t>(us / 1000));
    }
    // Connected, and the probe's sync answered as BlipToaster answers it.
    void connectFramed() {
        link.setConnected(true);
        drain();
        back = {0xF5, 0x00, 0x7F};
        drain();
    }
};

// BlipToaster's end of the link as the link's ROM model plays it (LinkRomModel.hpp: the rules the sender's own soak is
// held to), behind a wire that loses bytes as an NTSC console's DMC fetch does, in bursts of 1 to 3, once `lossy`:
// FakeN8 models intact traffic only. Its FIFO runs empty after each write, so it acks then, when anything changed.
// The losses come from a fixed LCG (N8Link::setTestLossPermille's), not a <random> distribution, whose draws differ
// between standard libraries: the same bytes are lost on every platform, so a failure reproduces anywhere.
struct LossyRig {
    rp::n8::test::RomModel                         rom;
    std::uint32_t                                  rng = 0x2545F491u;
    bool                                           lossy = false;
    int                                            lost = 0;
    Bytes                                          back;
    std::unique_ptr<rp::box::BoxCartLink::Storage> ring = std::make_unique<rp::box::BoxCartLink::Storage>();
    rp::box::BoxCartLink                           link{*ring};
    std::uint64_t                                  us = 1'000'000;

    static std::uint32_t now(void* ctx) { return static_cast<std::uint32_t>(static_cast<LossyRig*>(ctx)->us); }
    static std::uint64_t monotonic(void* ctx) { return static_cast<LossyRig*>(ctx)->us; }
    std::uint32_t        draw() {
        rng = rng * 1664525u + 1013904223u;
        return rng >> 8;
    }
    static bool          fifo(void* ctx, const std::uint8_t* d, std::size_t n, std::uint32_t, std::uint32_t) {
        auto& self = *static_cast<LossyRig*>(ctx);
        for (std::size_t i = 0; i < n; ++i) {
            if (self.lossy && self.draw() % 1000 < 15) {
                const int k = 1 + static_cast<int>(self.draw() % 3);
                self.lost += k;
                i += static_cast<std::size_t>(k) - 1;
                continue;
            }
            self.rom.byte(d[i]);
        }
        Bytes ack;
        if (self.rom.ack(ack)) self.back.insert(self.back.end(), ack.begin(), ack.end());
        return true;
    }
    static std::size_t read(void* ctx, std::uint8_t* d, std::size_t n) {
        auto&             self = *static_cast<LossyRig*>(ctx);
        const std::size_t k = n < self.back.size() ? n : self.back.size();
        for (std::size_t i = 0; i < k; ++i) d[i] = self.back[i];
        self.back.erase(self.back.begin(), self.back.begin() + static_cast<std::ptrdiff_t>(k));
        return k;
    }

    LossyRig() { link.setClock({this, &now, nullptr, &monotonic}); }
    rp::box::BoxCartLink::Writer writer() { return {this, &fifo, nullptr, &read}; }
    bool push(const Bytes& m) { return link.push(0, m.data(), m.size(), false, 48000.0); }
    void drain(std::uint64_t ms = 1) {
        us += ms * 1000;
        link.drain(writer(), static_cast<std::uint32_t>(us / 1000));
    }
};

// The device-mode link's 64-bit clock: the box's fake clock, as main.cpp hands its link time_us_64.
std::uint64_t boxNow64(void* ctx) { return static_cast<Box*>(ctx)->clock.us; }
// What the cart says on the device-mode link's back-channel, for its next drain to read.
void say(BoxRecord& r, std::initializer_list<std::uint8_t> bytes) {
    for (const std::uint8_t b : bytes) r.back[r.backCount++] = b;
}

}  // namespace

TEST_CASE("the box's link probe at a connect: framed for BlipToaster, raw for a ROM without the link",
          "[device-mode][box][box-link]") {
    CartRig     r;
    const Bytes on = note(0x90, 60, 100);
    SECTION("BlipToaster acknowledges the sync: framed, and the message goes out as a frame") {
        r.dev.linkRom = true;
        r.link.setConnected(true);
        r.drain();
        CHECK(r.link.framed());
        REQUIRE(r.push(on));
        r.drain();
        CHECK(r.dev.applied == std::vector<Bytes>{on});
        CHECK(fifoWrites(r.dev) ==
              std::vector<std::string>{fifoEntry({0xF4, 0x00}), fifoEntry(cat({{0xF4, 0x00}, frame(0, on)}))});
        CHECK(r.link.counts().forwarded == 1);
    }
    SECTION("a ROM without the link says nothing: raw after 100 ms, the message waiting in the ring until then") {
        r.link.setConnected(true);
        r.drain();  // the sync
        REQUIRE(r.push(on));
        r.drain(98);
        CHECK(r.link.queued() == 1);
        CHECK_FALSE(r.link.framed());
        r.drain(1);  // 99 ms
        CHECK(r.link.queued() == 1);
        r.drain(1);  // 100 ms: raw, the guard's first probe ahead of the message
        CHECK(r.link.queued() == 0);
        CHECK_FALSE(r.link.framed());
        CHECK(fifoWrites(r.dev) ==
              std::vector<std::string>{fifoEntry({0xF4, 0x00}), fifoEntry(cat({kProbeBytes, on}))});
    }
    SECTION("a link the box may not probe (N8_BOX_LINK=0) streams raw at once, guarded") {
        r.dev.linkRom = true;
        r.link.setLinkAllowed(false);
        r.link.setConnected(true);
        REQUIRE(r.push(on));
        r.drain();
        CHECK_FALSE(r.link.framed());
        CHECK(fifoWrites(r.dev) == std::vector<std::string>{fifoEntry(cat({kProbeBytes, on}))});
        CHECK(r.dev.syncs == 0);
    }
}

TEST_CASE("the box's link adds no pass of delay: a message pushed in a pass goes out in that pass's drain",
          "[device-mode][box][box-link]") {
    CartRig r;
    SECTION("framed") {
        r.dev.linkRom = true;
        r.link.setConnected(true);
        r.drain();
        REQUIRE(r.link.framed());
        for (std::uint8_t i = 0; i < 10; ++i) {
            REQUIRE(r.push(note(0x90, static_cast<std::uint8_t>(60 + i), 100)));
            r.drain();
            CHECK(r.dev.applied.size() == i + 1u);
            CHECK(r.link.counts().forwarded == i + 1u);
        }
        CHECK(r.link.outstanding() <= 1);  // the last frame's ack is read at the next drain
    }
    SECTION("raw") {
        r.link.setConnected(true);
        r.drain();
        r.drain(100);  // raw, and the guard's first probe
        REQUIRE_FALSE(r.link.framed());
        for (std::uint8_t i = 0; i < 10; ++i) {
            const Bytes on = note(0x90, static_cast<std::uint8_t>(60 + i), 100);
            REQUIRE(r.push(on));
            r.drain();
            CHECK(fifoWrites(r.dev).back() == fifoEntry(on));
            CHECK(r.link.counts().forwarded == i + 1u);
        }
    }
}

TEST_CASE("the box's framed link frames no more than its window in one drain, and the rest goes out once acks come",
          "[device-mode][box][box-link]") {
    // A backlog in one pass (the ring after a guard hold, a block after the loop was held): framed to empty, its 49th
    // frame would make the sender give its window up and re-sync, and a frame lost before that would be gone for good.
    CartRig r;
    r.dev.linkRom = true;
    r.link.setConnected(true);
    r.drain();
    REQUIRE(r.link.framed());
    REQUIRE(r.dev.syncs == 1);  // the connect's probe
    std::vector<Bytes> sent;
    for (std::uint8_t i = 0; i < 100; ++i) {
        sent.push_back(note(0x90, i, 100));
        REQUIRE(r.push(sent.back()));
    }
    r.drain();
    CHECK(r.link.outstanding() == rp::N8ReliableSender::kWindow - 1);
    CHECK(r.link.queued() == 100 - (rp::N8ReliableSender::kWindow - 1));
    CHECK(r.dev.syncs == 2);  // and the sender's own, before its first frame: no give-up's re-sync
    for (int k = 0; k < 4 && r.link.queued() != 0; ++k) r.drain();  // each reads the acks, then frames more
    r.drain();                                                        // the last acks
    CHECK(r.link.queued() == 0);
    CHECK(r.link.outstanding() == 0);
    CHECK(r.dev.syncs == 2);
    CHECK(r.dev.applied == sent);
    CHECK(r.link.counts().forwarded == 100);
}

TEST_CASE("the box's framed link through a wire that loses bytes: every message arrives once, in order",
          "[device-mode][box][box-link]") {
    LossyRig r;
    r.link.setConnected(true);
    r.drain();
    REQUIRE(r.link.framed());
    r.lossy = true;
    // 2000 messages (notes, CCs, the odd SysEx), three a sequencer step, four 1 ms passes a step.
    std::vector<Bytes> sent;
    for (int i = 0; i < 2000; ++i) {
        Bytes m;
        switch (i % 7) {
            case 3: m = {0xB1, std::uint8_t(i & 0x7F), std::uint8_t((i * 3) & 0x7F)}; break;
            case 6: m = {0xF0, 0x7D, 0x42, 0x02, 0xF7}; break;
            default: m = note(std::uint8_t(0x90 | (i % 4)), std::uint8_t(36 + i % 40), 100); break;
        }
        sent.push_back(m);
        REQUIRE(r.push(m));
        if (i % 3 == 2)
            for (int k = 0; k < 4; ++k) r.drain();
    }
    for (int k = 0; k < 4000 && (r.link.queued() != 0 || r.link.outstanding() != 0); ++k) r.drain();
    CHECK(r.link.queued() == 0);
    CHECK(r.link.outstanding() == 0);
    CHECK(r.lost > 0);                         // the wire really did lose bytes
    CHECK(r.link.counts().resends > 0);        // and the link repaired them
    CHECK(r.link.framed());                    // without giving the link up
    REQUIRE(r.rom.applied.size() == sent.size());  // none lost, none applied twice
    CHECK(r.rom.applied == sent);                  // and in order
}

TEST_CASE("a console RESET on the box's framed link: nothing but the guard's probes after the stall, and the menu's "
          "answer drops the ring and the window",
          "[device-mode][box][box-link]") {
    CartRig r;
    r.dev.linkRom = true;
    r.link.setConnected(true);
    r.drain();
    REQUIRE(r.link.framed());
    const Bytes a = note(0x90, 60, 100), b = note(0x90, 62, 100), c = note(0x90, 64, 100);
    REQUIRE(r.push(a));
    r.drain();
    REQUIRE(r.dev.applied.size() == 1);
    // The RESET: the menu, which acknowledges nothing, and whose booting OS answers '*t' with nothing useful at first.
    r.dev.testReply = 0x00;
    r.dev.resetToMenu();
    REQUIRE(r.push(b));
    r.drain();  // b's frame, which the menu reads and skips (no 0x2A in it)
    const std::size_t atStall = fifoWrites(r.dev).size();
    r.drain(100);  // 100 ms: the window has stopped moving: held, and the menu asked
    CHECK(r.link.connected());
    REQUIRE(r.push(c));  // held in the ring, not written
    r.drain(500);
    r.dev.testReply = 'k';  // the OS is up
    r.drain(500);           // a second on: asked again
    r.drain();              // its 'k'
    CHECK(r.link.atMenu());
    CHECK_FALSE(r.link.connected());
    const std::vector<std::string> writes = fifoWrites(r.dev);
    CHECK(std::vector<std::string>(writes.begin() + static_cast<std::ptrdiff_t>(atStall), writes.end()) ==
          std::vector<std::string>{fifoEntry(kProbeBytes), fifoEntry(kProbeBytes)});
    CHECK(r.link.counts().notReady == 2);  // c from the ring, b from the window
    CHECK(r.link.queued() == 0);
    CHECK(r.link.outstanding() == 0);
    CHECK_FALSE(r.push(note(0x80, 60, 0)));  // at the menu: refused, counted
    CHECK(r.link.counts().notReady == 3);
    CHECK(r.dev.applied.size() == 1);
    CHECK_FALSE(r.dev.error40);
    CHECK(r.dev.menu);
}

TEST_CASE("a console RESET on the box's raw link is seen by the guard's probe, every 250 ms between writes",
          "[device-mode][box][box-link]") {
    CartRig r;
    r.link.setConnected(true);
    r.drain();
    r.drain(100);  // raw: the first probe, which the game ignores
    REQUIRE_FALSE(r.link.framed());
    std::uint64_t resetUs = 0;
    for (int i = 0; i < 20 && !r.link.atMenu(); ++i) {
        if (i == 2) {
            r.dev.resetToMenu();
            resetUs = r.us;
        }
        REQUIRE(r.push(note(0x90, static_cast<std::uint8_t>(60 + i), 100)));
        r.drain(50);
    }
    CHECK(r.link.atMenu());
    CHECK(r.us - resetUs <= static_cast<std::uint64_t>(rp::N8MenuGuard::kProbeNs / 1000) + 50'000);
    CHECK(r.dev.countExact(fifoEntry(kProbeBytes)) == 1);  // the first; the answered one rode with a message
    CHECK_FALSE(r.dev.error40);
    CHECK_FALSE(r.push(note(0x80, 60, 0)));
}

TEST_CASE("two stall probes the box's framed link hears nothing to are a game without the link: the link is probed "
          "again, and streams raw",
          "[device-mode][box][box-link]") {
    CartRig r;
    r.dev.linkRom = true;
    r.link.setConnected(true);
    r.drain();
    REQUIRE(r.link.framed());
    // The game stops answering, as one without the link does when a reset the guard never saw started it.
    r.dev.linkDeaf = true;
    REQUIRE(r.push(note(0x90, 60, 100)));
    r.drain();      // its frame, which nothing acknowledges
    r.drain(100);   // the stall: held, probed
    r.drain(1000);  // nothing heard: probed again
    REQUIRE(r.push(note(0x90, 62, 100)));  // held
    r.drain(1000);  // nothing again: the window and the ring go, and the link is probed again
    CHECK(r.link.counts().notReady == 2);
    r.drain();     // the sync, which nothing answers
    r.drain(100);  // raw, and the guard's first raw probe
    CHECK_FALSE(r.link.framed());
    const Bytes after = note(0x90, 64, 100);
    REQUIRE(r.push(after));
    r.drain();
    const std::vector<std::string> writes = fifoWrites(r.dev);
    REQUIRE(writes.size() >= 3);
    CHECK(std::vector<std::string>(writes.end() - 3, writes.end()) ==
          std::vector<std::string>{fifoEntry({0xF4, 0x00}), fifoEntry(kProbeBytes), fifoEntry(after)});
    CHECK(r.dev.countExact(fifoEntry({0xF4, 0x00})) == 2);  // the connect's probe and the second
    CHECK(r.dev.countExact(fifoEntry(kProbeBytes)) == 3);   // two stall probes and the raw link's first
    CHECK(r.link.counts().resends == 0);                    // a held stall re-sends nothing
}

TEST_CASE("the box's link is quiet for an Edio read only when nothing it is owed can land in the reply",
          "[device-mode][box][box-link]") {
    ScriptedRig r;
    SECTION("not while the connect's sync waits for its ack, nor while a guard probe awaits its answer") {
        r.link.setConnected(true);
        CHECK(r.link.quietForRead());  // nothing written yet
        r.drain();                     // the sync
        CHECK_FALSE(r.link.quietForRead());
        r.drain(100);                  // raw, and the guard's probe
        REQUIRE_FALSE(r.link.framed());
        REQUIRE(r.writes.back() == kProbeBytes);
        CHECK_FALSE(r.link.quietForRead());
        r.us += 99'000;
        CHECK_FALSE(r.link.quietForRead());
        r.us += 1'000;  // 100 ms: nothing answered, a game
        CHECK(r.link.quietForRead());
    }
    SECTION("not inside an ack") {
        r.connectFramed();
        REQUIRE(r.link.framed());
        CHECK(r.link.quietForRead());
        r.back = {0xF5, 0x00};
        r.drain();
        CHECK_FALSE(r.link.quietForRead());
        r.back = {0x7F};
        r.drain();
        CHECK(r.link.quietForRead());
    }
    SECTION("not with a fresh frame outstanding, until the cart has said nothing for 250 ms since it went") {
        r.connectFramed();
        REQUIRE(r.push(note(0x90, 60, 100)));
        r.drain();
        REQUIRE(r.link.outstanding() == 1);
        CHECK_FALSE(r.link.quietForRead());
        r.us += 249'000;
        CHECK_FALSE(r.link.quietForRead());
        r.us += 1'000;
        CHECK(r.link.quietForRead());
        r.back = {0xF5, 0x01, 0x00};
        r.drain();
        CHECK(r.link.outstanding() == 0);
        CHECK(r.link.quietForRead());
    }
}

TEST_CASE("a SysEx longer than a frame carries is dropped on the box's framed link, and counted",
          "[device-mode][box][box-link]") {
    CartRig r;
    r.dev.linkRom = true;
    r.link.setConnected(true);
    r.drain();
    REQUIRE(r.link.framed());
    Bytes big(45, 0x10);
    big.front() = 0xF0;
    big.back() = 0xF7;
    const Bytes on = note(0x90, 60, 100);
    REQUIRE(r.push(big));
    REQUIRE(r.push(on));
    r.drain();
    CHECK(r.link.counts().unframed == 1);
    CHECK(r.link.counts().forwarded == 1);
    CHECK(r.link.counts().sysex == 0);
    CHECK(r.dev.applied == std::vector<Bytes>{on});
}

TEST_CASE("the box's pass on the framed link: the connect's probe, a frame and its ack, allocating nothing",
          "[device-mode][box][box-link]") {
    auto box = std::make_unique<Box>();
    auto record = std::make_unique<BoxRecord>();
    record->clockCtx = box.get();
    record->now = &boxNow;
    record->arrival = &boxArrival;
    record->monotonicUs = &boxNow64;
    record->backChannel = true;
    void* link = rp_devtest_box_link_device_mode(record.get());
    box->load(link, &rp_devtest_box_acquired_device_mode, &rp_devtest_box_released_device_mode);
    auto drain = [&] { rp_devtest_box_drain_device_mode(static_cast<std::uint32_t>(box->clock.us / 1000)); };
    // A note for the game, on the UART's ring before the window (a ring that grows allocates), held back from the
    // passes until the link is framed.
    box->ringEnd = 0;
    box->arrive({0x90, 0x45, 0x7F});
    const std::uint32_t noteAt = static_cast<std::uint32_t>(box->clock.us);

    rp::test::beginCountingAllocations();
    rp_devtest_box_connect_device_mode(true);
    const bool ranOne = box->pass(1'000);
    drain();                             // the connect's probe: F4 00
    say(*record, {0xF5, 0x00, 0x7F});    // BlipToaster's ack for it
    const bool ranTwo = box->pass(500);
    drain();                             // framed, and the volume at the connect
    box->ringEnd = SIZE_MAX;
    const bool ranThree = box->pass(500);
    drain();                             // the note, framed behind the sender's own sync
    say(*record, {0xF5, 0x01, 0x00});    // its ack
    const bool ranFour = box->pass(500);
    drain();
    const auto counted = rp::test::endCountingAllocations();

    CHECK(ranOne);
    CHECK(ranTwo);
    CHECK(ranThree);
    CHECK(ranFour);
    CHECK(counted.allocations == 0);
    CHECK(counted.frees == 0);
    const BoxCounts c = boxCounts();
    CHECK(c.framed);
    CHECK(writeLines(*record) == "0: f4 0\n1: f4 0 f5 0 90 45 7f 54\n");
    CHECK(record->writes[1].firstUs == noteAt);
    CHECK(record->volumeCount == 1);
    CHECK(record->volumes[0] == 128);
    CHECK(c.forwarded == 1);
    CHECK(c.resends == 0);
    CHECK(c.unframed == 0);
    CHECK(record->backCount == 0);
    unloadPlan(box->plan.plan());
}
```

In `packages/native/test/device-mode/DeviceModeEntries.hpp`:

Old (lines 77-89):

```cpp
    // When what the plan pushes arrived: the clock for one the plan made, and the staging's arrival (MidiStaging) for
    // one it was handed, as the box gives its link.
    void*         clockCtx = nullptr;
    std::uint32_t (*now)(void* ctx) = nullptr;
    bool (*arrival)(void* ctx, std::uint32_t frame, const std::uint8_t* data, std::size_t size,
                    std::uint32_t* us) = nullptr;
};

struct BoxCounts {
    std::uint32_t received, made, notReady, full, released, forwarded, failed, sysex, writes, maxBatch;
    std::uint32_t fromPlanInput;
    int           volume, holders;
};
```

New:

```cpp
    // When what the plan pushes arrived: the clock for one the plan made, and the staging's arrival (MidiStaging) for
    // one it was handed, as the box gives its link.
    void*         clockCtx = nullptr;
    std::uint32_t (*now)(void* ctx) = nullptr;
    bool (*arrival)(void* ctx, std::uint32_t frame, const std::uint8_t* data, std::size_t size,
                    std::uint32_t* us) = nullptr;
    // The framed link's: the cart's back-channel (off unless a case turns it on, so the link is raw and unguarded, as
    // the box's was before it), what the cart has said on it and the link has not read yet, and the link's 64-bit
    // clock (main.cpp's time_us_64), read through clockCtx.
    bool          backChannel = false;
    std::uint8_t  back[64] = {};
    std::uint32_t backCount = 0;
    std::uint64_t (*monotonicUs)(void* ctx) = nullptr;
};

struct BoxCounts {
    std::uint32_t received, made, notReady, full, released, forwarded, failed, sysex, writes, maxBatch;
    std::uint32_t fromPlanInput;
    int           volume, holders;
    std::uint32_t resends, resent, unframed;
    bool          framed;
};
```

In `packages/native/test/device-mode/DeviceModeNodes.cpp`:

Old (lines 58-61):

```cpp
std::uint32_t boxNow(void*) { return gBoxRecord->now ? gBoxRecord->now(gBoxRecord->clockCtx) : 0; }
bool          boxArrival(void*, std::uint32_t frame, const std::uint8_t* data, std::size_t size, std::uint32_t* us) {
    return gBoxRecord->arrival && gBoxRecord->arrival(gBoxRecord->clockCtx, frame, data, size, us);
}
```

New:

```cpp
std::uint32_t boxNow(void*) { return gBoxRecord->now ? gBoxRecord->now(gBoxRecord->clockCtx) : 0; }
bool          boxArrival(void*, std::uint32_t frame, const std::uint8_t* data, std::size_t size, std::uint32_t* us) {
    return gBoxRecord->arrival && gBoxRecord->arrival(gBoxRecord->clockCtx, frame, data, size, us);
}
std::uint64_t boxMonotonic(void*) {
    return gBoxRecord->monotonicUs ? gBoxRecord->monotonicUs(gBoxRecord->clockCtx) : 0;
}
// The cart's back-channel, as the record holds it: what the case put there, handed over from the front.
std::size_t boxRead(void*, std::uint8_t* data, std::size_t size) {
    BoxRecord&          r = *gBoxRecord;
    const std::uint32_t n = r.backCount < size ? r.backCount : static_cast<std::uint32_t>(size);
    for (std::uint32_t i = 0; i < n; ++i) data[i] = r.back[i];
    for (std::uint32_t i = n; i < r.backCount; ++i) r.back[i - n] = r.back[i];
    r.backCount -= n;
    return n;
}
```

Old (lines 82-98):

```cpp
    gBox->setClock({nullptr, &boxNow, &boxArrival});
    return static_cast<rp::n8::CartLink*>(gBox);
}
extern "C" void rp_devtest_box_connect_device_mode(bool connected) {
    rp::graph::devtest::gBox->setConnected(connected);
}
extern "C" void rp_devtest_box_drain_device_mode(std::uint32_t nowMs) {
    using namespace rp::graph::devtest;
    gBox->drain({nullptr, &boxFifo, &boxVolume}, nowMs);
}
extern "C" void rp_devtest_box_counts_device_mode(BoxCounts* out) {
    const auto& c = rp::graph::devtest::gBox->counts();
    *out = BoxCounts{c.received,  c.made,   c.notReady, c.full,   c.released,
                     c.forwarded, c.failed, c.sysex,    c.writes, c.maxBatch,
                     c.fromPlanInput(), rp::graph::devtest::gBox->expansionVolume(),
                     rp::graph::devtest::gBox->holders()};
}
```

New:

```cpp
    gBox->setClock({nullptr, &boxNow, &boxArrival, &boxMonotonic});
    return static_cast<rp::n8::CartLink*>(gBox);
}
extern "C" void rp_devtest_box_connect_device_mode(bool connected) {
    rp::graph::devtest::gBox->setConnected(connected);
}
extern "C" void rp_devtest_box_drain_device_mode(std::uint32_t nowMs) {
    using namespace rp::graph::devtest;
    gBox->drain({nullptr, &boxFifo, &boxVolume, gBoxRecord->backChannel ? &boxRead : nullptr}, nowMs);
}
extern "C" void rp_devtest_box_counts_device_mode(BoxCounts* out) {
    const auto& c = rp::graph::devtest::gBox->counts();
    *out = BoxCounts{c.received,  c.made,   c.notReady, c.full,   c.released,
                     c.forwarded, c.failed, c.sysex,    c.writes, c.maxBatch,
                     c.fromPlanInput(), rp::graph::devtest::gBox->expansionVolume(),
                     rp::graph::devtest::gBox->holders(), c.resends, c.resent, c.unframed,
                     rp::graph::devtest::gBox->framed()};
}
```

In `packages/native/cmake/tests.cmake`:

Old (lines 881-882):

```cmake
             ${RP_BOX_HOST_INCLUDES}                   # the box host and midi.h
             ${_rp_box_dir}                            # BoxCartLink.hpp, UsbCdcTransport.hpp
```

New:

```cmake
             ${RP_BOX_HOST_INCLUDES}                   # the box host and midi.h
             ${_rp_box_dir}                            # BoxCartLink.hpp, UsbCdcTransport.hpp
             ${CMAKE_SOURCE_DIR}/packages/n8/native/test   # FakeN8.hpp: the cart the box's link is held against
```

(`FakeN8.hpp` includes `n8/desktop/N8Link.hpp` for its `factoryFor`, an inline function the box's cases never call,
so the binary links nothing of `retroplug-n8-desktop`; the header's includes are on the path through `retroplug-n8`,
which the binary links among the device entries' owners, as the engine-graph binary reaches the same header.)

- [ ] **Step 2: run them and see them fail**

```
timeout -k 10 1800 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh cmake --build /workspaces/RetroPlug-main/cuttlefish/build --target retroplug-device-mode-test -j$(nproc)
```

Expected: the build fails: `BoxHost.test.cpp` and `DeviceModeNodes.cpp` name what `BoxCartLink` does not have yet
(`setLinkAllowed`, `framed`, `atMenu`, `quietForRead`, `outstanding`, `Counts::resends`, `resent`, `unframed`, and a
fourth initialiser for `Writer` and `Clock`: "too many initializers").

- [ ] **Step 3: the link**

Write `pico/n8-host/BoxCartLink.hpp` whole:

```cpp
#pragma once

// The box's cart link (docs/modular-graph-phase-e3-design.md section 5, docs/pico-box-framed-link-design.md):
// rp::n8::CartLink (n8/CartLink.hpp) over the main loop's one Edio, with no thread of its own. rp.n8.nes pushes into a
// fixed ring during the block; after the block the main loop drains the ring to the cart's FIFO in writes of up to 192
// bytes (drain()), so a push never waits on the N8: block and drain run in turn on the main loop (an audio cadence
// would run blocks off it: design 10).
//
// - push() copies the message into the ring and answers false, counted, when no game is running (the box's forwarding
//   state, which its N8 part sets each pass through setConnected(): a game is running and the box forwards to it),
//   when the menu guard has heard the N8 menu answer (below), or when the ring is full.
// - drain() writes whole messages back to back, as many as fit in 192 bytes (a write costs the same ~5 ms whatever it
//   carries, so batching is what lets a burst through), and on a raw link splits only a message longer than that (a
//   SysEx). A message is counted forwarded, or failed, in the write that carries its last byte.
// - BlipToaster's reliable link and the N8 menu guard, the desktop's N8Link's, from the one implementation both run
//   (the n8-wire package's N8ReliableSender and N8MenuGuard), when the Writer reads the cart's back-channel and the
//   Clock has its 64-bit time; without either the link is raw and unguarded, as the box was before them. A connect
//   writes the link's sync (F4 00) at its next drain and listens for the ack across the passes that follow, for 100 ms,
//   while what is pushed waits in the ring: an ack makes the link framed (BlipToaster), silence leaves it raw
//   (setLinkAllowed(false) skips the probe). Each drain then reads the back-channel (the acks for the sender, the
//   menu's 'k' for the guard), takes the guard's turn, puts the guard's probe at the front of its first write when one
//   is due (a drain starts between two messages), and, unless the guard holds, writes the sender's re-sends and the
//   ring's messages, each framed on a framed link, in writes that never split a frame, and never with more than the
//   window less one outstanding (the rest waits for the next drain, which reads the acks first, so a backlog never
//   makes the sender give its window up). A SysEx longer than a frame carries is dropped there (`unframed`), as the
//   sender drops it. The menu's answer drops the ring and the window (counted not ready) and atMenu() says so until
//   the box disconnects the link to boot the ROM again; two stall probes that hear nothing drop them too, and probe
//   the link again, which a game without the link answers raw.
// - quietForRead() answers whether an Edio read now (the box's CMD_STATUS, its sniff, the volume's read-back) can meet
//   nothing the link is owed on the same USB input: N8Link's rule, and not while the connect's sync waits for its ack.
// - releaseNotes() queues a note-off for each note the same way, behind what was already pushed.
// - setExpansionVolume() records the value (clamped to the register's 0..255, or -1 to leave the register to the
//   cart), and drain() writes it to the cart's config register while connected: at every connect (once the link probe
//   has decided) and on a change, on a turn quietForRead() allows, as the desktop's N8Host does (and a failed write
//   again a second later). Its -1 (N8Host::EXP_VOL_AUTO) writes nothing here: the desktop's rule for it (unity for an
//   expansion mapper, else the N8 OS's value) is for a link no node holds. The bridge plan's rp.n8.nes never leaves it
//   at -1, as it gives its value at its first block.
// - acquire() and release() are the hooks the box installs the link with (rp.host.devices/2), counting the nodes that
//   hold it; the last release puts the volume back to -1, as N8CartLink does.
// - No device jobs: the box offers no rp.host.jobs/1, so rp.n8.nes refuses a job before it reaches the link, and the
//   link's own answer is CartLink's ("not connected").
//
// What it counts is the plan's edge into the cart, which the box's stats read without reaching into the plan: the
// pushes it was handed, and of them the ones rp.n8.nes made itself, the note-offs it owes for notes whose note-off a
// disconnect dropped and pays at the next connected block. The link keeps the node's books on its own answers to tell
// those apart (sounding: a note-on taken, no note-off taken since; owed: a sounding note whose note-off it refused),
// the rules N8Node.cpp's pushMidi keeps, so the box's `filtered` (staged minus what the link was handed from the
// plan's input) stays what the plan dropped. That holds for MIDI pushed through the node's `midi` input, the only input
// the box's plan wires. A message the link took and then dropped (the menu's answer, a re-probe, a disconnect) was
// taken, so the books, as the node's, count it delivered.
//
// Portable (no Pico SDK), so the desktop's tests build it as the box does, and heap-free, so the box links it under the
// device build's trap. One thread: the block and the drain both run on the box's main loop, which also sets the
// connected state.

#include <cstddef>
#include <cstdint>

#include "graph/rp_device_midi_in.h"
#include "n8/CartLink.hpp"
#include "transport/N8MenuGuard.hpp"
#include "transport/N8ReliableLink.hpp"

namespace rp::box {

class BoxCartLink final : public rp::n8::CartLink {
public:
    // Room for one block's MIDI (rp.device.midi-in's queue, which the plan's MIDI policy matches) twice over in bytes,
    // and 512 messages, so the note-offs the node owes after a reconnect (each before the block's own messages, as many
    // as fit) fit beside a full block: the ring is emptied after every block. The messages are not the block's 64
    // twice: a block can bring that many while a reconnect owes far more note-offs than the rest of 128 would take.
    static constexpr std::uint32_t kMessages = 512;
    static constexpr std::uint32_t kBytes = 2 * RP_DEVICE_MIDI_IN_BYTES;
    static_assert(kMessages >= 2 * RP_DEVICE_MIDI_IN_MESSAGES, "a full block twice over");
    // The most one FIFO write carries.
    static constexpr std::uint32_t kBatch = 192;
    // How long a failed volume write waits before it is tried again.
    static constexpr std::uint32_t kVolumeRetryMs = 1000;

    // Where the main loop's writes go, and what it reads: the cart's FIFO (`messages` of the ring's messages end in
    // `data`, 0 for a write that carries only the link's sync, the guard's probe or re-sends, and `firstUs` is when the
    // first of them arrived, or now for a write with none), the cart's expansion-volume register (`again` on a retry
    // of a write that failed), and the cart's back-channel. Each write answers whether it reached the cart: a volume
    // written whose read-back differs is written, not retried (the cart's register is its own business from there;
    // N8Host reads nothing back).
    struct Writer {
        void* ctx;
        bool (*fifo)(void* ctx, const std::uint8_t* data, std::size_t size, std::uint32_t messages,
                     std::uint32_t firstUs);
        bool (*volume)(void* ctx, int value, bool again);
        // The cart's back-channel, without waiting (the box's UsbCdcTransport::read with a 0 timeout: one USB pump, and
        // what it found): up to `size` bytes, 0 when nothing has come. Null: no back-channel, so no framed link and no
        // guard.
        std::size_t (*read)(void* ctx, std::uint8_t* data, std::size_t size);
    };
    // When what the plan pushes arrived: `arrival` answers it for a message the block handed the plan at `frame` with
    // these bytes (MidiStaging::arrival), `now` for one the plan made itself. Microseconds, the low 32 bits.
    // `monotonicUs` is the link's own time, for the sender's and the guard's timeouts: microseconds, all 64 bits,
    // never 0 (the box's time_us_64, read seconds after its boot). Null: no framed link and no guard.
    struct Clock {
        void* ctx;
        std::uint32_t (*now)(void* ctx);
        bool (*arrival)(void* ctx, std::uint32_t frame, const std::uint8_t* data, std::size_t size, std::uint32_t* us);
        std::uint64_t (*monotonicUs)(void* ctx);
    };

    struct Counts {
        std::uint32_t received = 0;  // pushes, taken or refused
        std::uint32_t made = 0;      // of them, the note-offs rp.n8.nes owed (see above)
        // Pushes refused: no game running, or the menu answered. And what a game that went took with it: the ring at
        // the menu's answer, a re-probe or a disconnect, and the framed window's unacknowledged messages (written, so
        // counted forwarded too).
        std::uint32_t notReady = 0;
        std::uint32_t full = 0;      // pushes refused: the ring full
        std::uint32_t released = 0;  // note-offs releaseNotes queued
        std::uint32_t forwarded = 0; // messages in writes the cart took
        std::uint32_t failed = 0;    // messages in writes that failed
        std::uint32_t sysex = 0;     // of the forwarded, SysEx
        std::uint32_t writes = 0;    // FIFO writes (the link's syncs, the guard's probes and re-sends among them)
        std::uint32_t maxBatch = 0;  // the most messages one write carried
        std::uint32_t resends = 0;   // the framed link's re-send rounds (N8ReliableSender::resends, across connects)
        std::uint32_t resent = 0;    // frames those rounds wrote again
        std::uint32_t unframed = 0;  // messages a framed link could not carry (a SysEx past 40 bytes), dropped

        // What the plan handed the link from its input: its pushes, less the node's own note-offs.
        std::uint32_t fromPlanInput() const { return received - made; }
    };

    // The ring, the books, the framed link's sender and the guard, kept apart from the link in a plain struct the box
    // holds in .bss: a polymorphic object is initialised data (its vtable pointer), and what it held inline would be
    // copied out of flash at boot with it. Every byte of it starts at zero: the sender is raw storage, made in place
    // when a connect's sync is acknowledged (a sender object here would be constant-initialised to its non-zero
    // defaults, about 3.5 KB of .data), and the guard's state is all zeros.
    struct Message {
        std::uint32_t at;  // arrival, microseconds
        std::uint16_t size;
        bool          sysex;
    };
    struct Storage {
        std::uint8_t      bytes[kBytes];
        Message           messages[kMessages];
        std::uint8_t      batch[kBatch];
        rp::n8::CartNotes sounding, owed;
        alignas(rp::N8ReliableSender) unsigned char sender[sizeof(rp::N8ReliableSender)];
        rp::N8MenuGuard   guard;
    };

    constexpr explicit BoxCartLink(Storage& storage) : s_(storage) {}
    void setClock(const Clock& clock) { clock_ = clock; }
    // Whether a connect probes for BlipToaster's reliable link (the default) or streams raw at once, the guard on
    // either way, as N8Link::setLinkAllowed: the box's N8_BOX_LINK=0 build, the raw side of the framed link's latency
    // comparison. Set before a connect.
    void setLinkAllowed(bool on) { linkAllowed_ = on; }

    // rp::n8::CartLink. Connected: a game runs and the guard has not heard the menu.
    bool push(std::uint32_t frame, const std::uint8_t* data, std::size_t size, bool flush,
              double sampleRate) override;
    bool          connected() const override { return connected_ && !s_.guard.atMenu(); }
    std::uint64_t dropped() const override { return std::uint64_t{counts_.notReady} + counts_.full; }
    void          setExpansionVolume(int value) override;
    void          releaseNotes(const rp::n8::CartNotes& notes) override;

    // The box's side. Whether a game is running and the box forwards to it: a connect has the volume written again and
    // the link probed again, and a disconnect drops what is queued and the framed window (counted not ready), as
    // nothing may reach a cart that went away.
    void setConnected(bool connected);
    // The back-channel, the guard's turn, the volume while connected, then the re-sends and the ring, in writes of up
    // to kBatch bytes, until the ring is empty (or the guard holds it, or a framed window is full).
    void drain(const Writer& writer, std::uint32_t nowMs);
    void acquire() { ++holders_; }
    void release();

    // The link is framed: this connect's sync was acknowledged.
    bool framed() const { return sender_ != nullptr; }
    // The guard heard the N8 menu answer its probe: nothing is taken or written, and the box boots the ROM again.
    bool atMenu() const { return s_.guard.atMenu(); }
    // Whether an Edio read now can meet nothing the link is owed (see above). Reads the link's clock.
    bool quietForRead() const;
    // Frames the framed link has written and the cart has not acknowledged (0 on a raw link).
    std::uint32_t outstanding() const { return sender_ ? static_cast<std::uint32_t>(sender_->outstanding()) : 0; }

    const Counts& counts() const { return counts_; }
    int           expansionVolume() const { return volume_; }
    std::uint32_t queued() const { return messageCount_; }
    int           holders() const { return holders_; }

private:
    // The connect's link probe: Pending until its sync is written (at the next drain with a back-channel), Waiting
    // for the ack, Done once framed or raw; Off while disconnected.
    enum class Probe : std::uint8_t { Off, Pending, Waiting, Done };
    // One framed write being filled: its length in s_.batch, the ring's messages it carries (and their SysEx), when the
    // first of them arrived, and the frames it carries, marked sent once it is written.
    struct Out {
        std::uint32_t length = 0, carried = 0, sysex = 0, firstUs = 0, seqCount = 0;
        std::uint8_t  seqs[rp::N8ReliableSender::kWindow] = {};
    };

    bool enqueue(const std::uint8_t* data, std::uint32_t size, std::uint32_t at);
    void book(const std::uint8_t* data, std::size_t size, bool taken);
    void discard();
    std::int64_t  nowNs() const;
    std::uint32_t nowUs() const;
    bool quietAt(std::int64_t now) const;
    bool write(const Writer& writer, const std::uint8_t* data, std::uint32_t size, std::uint32_t messages,
               std::uint32_t firstUs);
    void writeVolume(const Writer& writer, std::uint32_t nowMs);
    void startProbe(const Writer& writer, std::int64_t now);
    void readBackChannel(const Writer& writer, std::int64_t now);
    void drainRaw(const Writer& writer, bool probe);
    void drainFramed(const Writer& writer, std::int64_t now, bool probe);
    void emit(const Writer& writer, Out& out, const std::uint8_t* data, std::size_t size, int seq);
    void flush(const Writer& writer, Out& out);
    void takeFront(std::uint8_t* to);

    Storage& s_;
    Clock    clock_{};
    bool     connected_ = false;

    // The ring (s_.bytes and s_.messages): messages and their bytes, each in a circle of its own.
    std::uint32_t byteHead_ = 0, byteCount_ = 0;
    std::uint32_t messageHead_ = 0, messageCount_ = 0;
    std::uint32_t frontSent_ = 0;  // the bytes of the oldest message already written (a split one)

    int           volume_ = -1;
    bool          volumePending_ = false;
    bool          volumeWaiting_ = false;  // a failed write waits until retryAtMs_
    std::uint32_t retryAtMs_ = 0;
    int           holders_ = 0;

    // The framed link: the sender while framed (in s_.sender), the probe's state, whether its read has seen an ack's
    // F5, and when it gives up listening; whether a connect may probe at all; and whether the last drain had a
    // back-channel (quietForRead's: without one there is nothing a read can meet).
    rp::N8ReliableSender* sender_ = nullptr;
    Probe                 probe_ = Probe::Off;
    bool                  probeSawFrame_ = false;
    std::int64_t          probeUntilNs_ = 0;
    bool                  linkAllowed_ = true;
    bool                  backChannel_ = false;

    Counts counts_{};
};

}  // namespace rp::box
```

Write `pico/n8-host/BoxCartLink.cpp` whole:

```cpp
#include "BoxCartLink.hpp"

#include <new>
#include <type_traits>

namespace rp::box {

using rp::n8::NoteEvent;
using rp::n8::noteEvent;

// The framed link's sender is made in place in the storage at the connect whose sync was acknowledged, and forgotten,
// never ended, at the next connect, a re-probe or the menu: nothing in it holds anything to give back.
static_assert(std::is_trivially_destructible_v<rp::N8ReliableSender>, "the sender is forgotten, not destroyed");

namespace {

// How long a connect's sync listens for its ack before the link is taken for raw: N8Link::probeLink's 100 ms, which
// covers a busy ROM and the USB round trip with room to spare.
constexpr std::int64_t kLinkProbeNs = 100'000'000;
// How long a cart may say nothing while a frame is outstanding before it is taken for one acknowledging nothing, so a
// read may go ahead of the frame: N8Link's kAckWaitNs.
constexpr std::int64_t kAckWaitNs = 250'000'000;
// The most back-channel reads one drain makes, 64 bytes each: a cart that keeps talking does not hold the pass.
constexpr int kBackReads = 8;
// The link's sync, F4 <seq 0>: a BlipToaster with the link answers it with an ack the moment its FIFO runs empty; any
// other ROM ignores an undefined System Common status.
constexpr std::uint8_t kSync[2] = {rp::N8ReliableSender::SYNC, 0};

}  // namespace

bool BoxCartLink::push(std::uint32_t frame, const std::uint8_t* data, std::size_t size, bool flush,
                       double sampleRate) {
    // The flush flag is the bytes port's barrier, which a FIFO written in order keeps by itself, and the frame and the
    // rate place nothing on a link that writes after the block: the frame only finds when the message arrived.
    (void)flush;
    (void)sampleRate;
    if (size == 0) return true;  // nothing to lose
    ++counts_.received;
    const NoteEvent note = noteEvent(data, size);
    // At the menu the cart reads the FIFO as its commands, so a push there is refused as while no game runs.
    const bool open = connected();
    if (open && note.off && s_.owed.test(note.index)) ++counts_.made;
    bool taken = false;
    if (!open) {
        ++counts_.notReady;
    } else {
        std::uint32_t at = 0;
        if (!clock_.arrival || !clock_.arrival(clock_.ctx, frame, data, size, &at)) at = nowUs();
        taken = size <= kBytes && enqueue(data, static_cast<std::uint32_t>(size), at);
        if (!taken) ++counts_.full;
    }
    book(data, size, taken);
    return taken;
}

// The node's books on its notes, kept from this link's own answers (N8Node.cpp's pushMidi and payOwed).
void BoxCartLink::book(const std::uint8_t* data, std::size_t size, bool taken) {
    const NoteEvent note = noteEvent(data, size);
    if (note.on && taken) {
        s_.sounding.set(note.index);
        s_.owed.reset(note.index);
    } else if (note.off) {
        if (taken) {
            s_.sounding.reset(note.index);
            s_.owed.reset(note.index);
        } else if (s_.sounding.test(note.index)) {
            s_.owed.set(note.index);
        }
    }
}

bool BoxCartLink::enqueue(const std::uint8_t* data, std::uint32_t size, std::uint32_t at) {
    if (messageCount_ == kMessages || size > kBytes - byteCount_) return false;
    std::uint32_t write = (byteHead_ + byteCount_) % kBytes;
    for (std::uint32_t i = 0; i < size; ++i) {
        s_.bytes[write] = data[i];
        write = write + 1 == kBytes ? 0 : write + 1;
    }
    byteCount_ += size;
    s_.messages[(messageHead_ + messageCount_) % kMessages] =
        Message{at, static_cast<std::uint16_t>(size), data[0] == 0xF0};
    ++messageCount_;
    return true;
}

void BoxCartLink::releaseNotes(const rp::n8::CartNotes& notes) {
    const std::uint32_t at = nowUs();
    const bool          open = connected();
    for (std::size_t i = 0; i < notes.size(); ++i) {
        if (!notes.test(i)) continue;
        s_.sounding.reset(i);
        s_.owed.reset(i);
        const std::uint8_t off[3] = {static_cast<std::uint8_t>(0x80 | (i / 128)), static_cast<std::uint8_t>(i % 128),
                                     0};
        if (!open)
            ++counts_.notReady;
        else if (!enqueue(off, sizeof off, at))
            ++counts_.full;
        else
            ++counts_.released;
    }
}

void BoxCartLink::setExpansionVolume(int value) {
    const int clamped = value < 0 ? -1 : (value > 255 ? 255 : value);
    if (clamped == volume_) return;
    volume_ = clamped;
    volumePending_ = true;
    volumeWaiting_ = false;
}

void BoxCartLink::release() {
    if (holders_ > 0 && --holders_ == 0) {
        volume_ = -1;
        volumePending_ = false;
    }
}

void BoxCartLink::setConnected(bool connected) {
    if (connected && !connected_) {
        volumePending_ = true;  // every connect again: a fresh game's register is the N8 OS's
        volumeWaiting_ = false;
        // A fresh game, whose link is probed at the next drain that has a back-channel, under a guard that starts
        // afresh.
        s_.guard.reset();
        probe_ = linkAllowed_ ? Probe::Pending : Probe::Done;
    }
    if (!connected && connected_) {
        discard();
        s_.guard.reset();
        probe_ = Probe::Off;
    }
    connected_ = connected;
}

void BoxCartLink::discard() {
    counts_.notReady += messageCount_;
    // The framed window's messages were written and never acknowledged: the game they were for went with them.
    if (sender_) counts_.notReady += static_cast<std::uint32_t>(sender_->outstanding());
    byteHead_ = byteCount_ = messageHead_ = messageCount_ = frontSent_ = 0;
    sender_ = nullptr;
}

std::int64_t BoxCartLink::nowNs() const {
    return clock_.monotonicUs ? static_cast<std::int64_t>(clock_.monotonicUs(clock_.ctx)) * 1000 : 0;
}

std::uint32_t BoxCartLink::nowUs() const { return clock_.now ? clock_.now(clock_.ctx) : 0; }

bool BoxCartLink::quietForRead() const { return quietAt(nowNs()); }

// N8Link::quietForRead's rule: no guard probe awaiting its answer (the menu's 'k' read inside a reply would be taken
// for its first byte, and lost to the guard), the scan not halfway through an ack (whose tail would be the reply's
// first bytes), and a raw link between two messages, nothing outstanding, or a cart that has said nothing for the
// pause's ack bound since the oldest outstanding frame first went out (one at its menu, or a game that has stopped
// answering). And not while the connect's sync waits for its ack, which the desktop's probe waits out in a call of its
// own and the box's across passes.
bool BoxCartLink::quietAt(std::int64_t now) const {
    if (!backChannel_) return true;  // nothing the link reads, so nothing a read can meet
    if (probe_ == Probe::Waiting) return false;
    if (s_.guard.probeAwaited(now) || s_.guard.scan().state != 0) return false;
    if (!sender_) return frontSent_ == 0;
    if (sender_->outstanding() == 0) return true;
    const std::int64_t first = sender_->oldestFirstSentNs();
    const std::int64_t heard = sender_->lastHeardNs();
    const std::int64_t spoke = heard > first ? heard : first;
    return spoke != rp::N8ReliableSender::kNever && now - spoke >= kAckWaitNs;
}

bool BoxCartLink::write(const Writer& writer, const std::uint8_t* data, std::uint32_t size, std::uint32_t messages,
                        std::uint32_t firstUs) {
    ++counts_.writes;
    return writer.fifo && writer.fifo(writer.ctx, data, size, messages, firstUs);
}

void BoxCartLink::writeVolume(const Writer& writer, std::uint32_t nowMs) {
    if (!connected_ || !volumePending_ || (volumeWaiting_ && static_cast<std::int32_t>(nowMs - retryAtMs_) < 0))
        return;
    if (volume_ < 0 || (writer.volume && writer.volume(writer.ctx, volume_, volumeWaiting_))) {
        volumePending_ = volumeWaiting_ = false;
    } else {
        volumeWaiting_ = true;
        retryAtMs_ = nowMs + kVolumeRetryMs;
    }
}

void BoxCartLink::drain(const Writer& writer, std::uint32_t nowMs) {
    backChannel_ = connected_ && writer.read != nullptr && clock_.monotonicUs != nullptr;
    if (!backChannel_) {
        // Nothing can be heard, so nothing is framed or guarded: the volume, then the ring, raw.
        writeVolume(writer, nowMs);
        drainRaw(writer, false);
        return;
    }
    const std::int64_t now = nowNs();
    if (probe_ == Probe::Pending) startProbe(writer, now);
    // What the cart has said: the probe's ack, the sender's acks, the menu's 'k'.
    readBackChannel(writer, now);
    if (probe_ == Probe::Waiting && now >= probeUntilNs_) probe_ = Probe::Done;  // nothing answered: raw
    // The ring waits for the probe's verdict (a sync whose write failed goes out again at the next drain).
    if (probe_ != Probe::Done) return;
    const rp::N8MenuGuard::Turn turn = s_.guard.turn(now, sender_, frontSent_ == 0);
    if (turn.enteredMenu) discard();  // the menu: what the ring held and the window go, counted
    if (turn.reprobeLink || (turn.leftMenu && linkAllowed_)) {
        // A stalled window two probes heard nothing to (a game without the link), or a game the user started from the
        // menu: the link is probed again at the next drain, and a game with it re-syncs framed.
        if (turn.reprobeLink) discard();
        sender_ = nullptr;
        probe_ = Probe::Pending;
        return;
    }
    // The volume, as N8Link's OnControl runs it: after the guard's turn, whose probe just booked makes the turn not
    // quiet, and only on a quiet one, since its read-back would take an ack or the menu's answer for the register.
    if (quietAt(now)) writeVolume(writer, nowMs);
    if (turn.hold) {
        // Nothing new and no re-send: the window has stopped moving and the guard is asking, or the cart is at its
        // menu. Only the probe, alone.
        if (turn.writeProbe)
            write(writer, rp::N8MenuGuard::kProbe, static_cast<std::uint32_t>(sizeof rp::N8MenuGuard::kProbe), 0,
                  nowUs());
        return;
    }
    if (sender_)
        drainFramed(writer, now, turn.writeProbe);
    else
        drainRaw(writer, turn.writeProbe);
}

void BoxCartLink::startProbe(const Writer& writer, std::int64_t now) {
    // Whatever the back-channel already holds goes before the sync, as N8Link::probeLink flushes the port first: bytes
    // that spell an ack's F5 <next> would turn the framed link on for a ROM that reads no frames. Read and dropped
    // without waiting, rather than flushed (the box's flush waits for 20 ms of quiet, which would hold the loop), and
    // the guard's scan starts afresh with the input.
    std::uint8_t stale[64];
    for (int i = 0; i < kBackReads; ++i)
        if (writer.read(writer.ctx, stale, sizeof stale) < sizeof stale) break;
    s_.guard.scan() = {};
    sender_ = nullptr;
    if (!write(writer, kSync, static_cast<std::uint32_t>(sizeof kSync), 0, nowUs())) return;
    probe_ = Probe::Waiting;
    probeSawFrame_ = false;
    probeUntilNs_ = now + kLinkProbeNs;
}

void BoxCartLink::readBackChannel(const Writer& writer, std::int64_t now) {
    std::uint8_t buf[64];
    for (int r = 0; r < kBackReads; ++r) {
        const std::size_t n = writer.read(writer.ctx, buf, sizeof buf);
        if (n == 0) return;
        if (probe_ == Probe::Waiting) {
            // The probe's answer, F5 <next> (N8Link::probeLink's test): the link is framed from here, with a sender of
            // its own. The rest of this read goes with the ack, as the desktop's probe leaves it unread: the ack's
            // <last> was in it, or is still to come, and then the guard's scan must read it as the ack's (a 0x6B there
            // is not the menu's answer).
            for (std::size_t i = 0; i < n; ++i) {
                if (probeSawFrame_ && buf[i] < 0x80) {
                    rp::N8ReliableSender::Timing t;
                    t.resendDelayNs = 0;  // the real round trip already applies
                    sender_ = new (s_.sender) rp::N8ReliableSender(t);
                    probe_ = Probe::Done;
                    s_.guard.scan().state = i + 1 < n ? 0 : 2;
                    return;
                }
                probeSawFrame_ = buf[i] == rp::N8ReliableSender::FRAME;
            }
        } else {
            if (sender_) sender_->onBackChannel(buf, n, now);
            s_.guard.hear(buf, n);
        }
        if (n < sizeof buf) return;
    }
}

void BoxCartLink::drainRaw(const Writer& writer, bool probe) {
    if (probe && messageCount_ == 0) {
        write(writer, rp::N8MenuGuard::kProbe, static_cast<std::uint32_t>(sizeof rp::N8MenuGuard::kProbe), 0, nowUs());
        return;
    }
    while (messageCount_ != 0) {
        // One write: the guard's probe first when one is due (a drain starts between two messages, and every message
        // the box writes carries its own status, so none after the probe runs on a status the probe ended), then the
        // messages that fit whole, from the oldest; one longer than a write is split across writes.
        std::uint32_t length = 0, carried = 0, sysex = 0;
        if (probe) {
            for (const std::uint8_t b : rp::N8MenuGuard::kProbe) s_.batch[length++] = b;
            probe = false;
        }
        const std::uint32_t firstUs = s_.messages[messageHead_].at;
        while (messageCount_ != 0 && length < kBatch) {
            const Message&      m = s_.messages[messageHead_];
            const std::uint32_t left = m.size - frontSent_;
            if (frontSent_ == 0 && m.size <= kBatch && length + m.size > kBatch) break;  // whole, in the next write
            const std::uint32_t take = left < kBatch - length ? left : kBatch - length;
            for (std::uint32_t i = 0; i < take; ++i) {
                s_.batch[length++] = s_.bytes[byteHead_];
                byteHead_ = byteHead_ + 1 == kBytes ? 0 : byteHead_ + 1;
            }
            byteCount_ -= take;
            frontSent_ += take;
            if (frontSent_ < m.size) break;  // the rest of it goes in the next write
            ++carried;
            if (m.sysex) ++sysex;
            frontSent_ = 0;
            messageHead_ = messageHead_ + 1 == kMessages ? 0 : messageHead_ + 1;
            --messageCount_;
        }
        const bool ok = write(writer, s_.batch, length, carried, firstUs);
        if (ok) {
            counts_.forwarded += carried;
            counts_.sysex += sysex;
        } else {
            counts_.failed += carried;
        }
        if (carried > counts_.maxBatch) counts_.maxBatch = carried;
    }
}

void BoxCartLink::drainFramed(const Writer& writer, std::int64_t now, bool probe) {
    Out        out;
    const auto emitTo = [&](const std::uint8_t* data, std::size_t size, int seq) {
        emit(writer, out, data, size, seq);
    };
    if (probe) emitTo(rp::N8MenuGuard::kProbe, sizeof rp::N8MenuGuard::kProbe, -1);
    // The re-sends due at `now`, the guard's own turn time, so none can slip past a hold the guard decided by a later
    // reading of the clock (N8Link::serviceLinkResends).
    const std::uint64_t resends = sender_->resends(), resent = sender_->framesResent();
    sender_->poll(now, emitTo);
    counts_.resends += static_cast<std::uint32_t>(sender_->resends() - resends);
    counts_.resent += static_cast<std::uint32_t>(sender_->framesResent() - resent);
    // Then the ring, each message framed as one of its own: they are whole here (only the raw path splits one, and it
    // finishes it in the drain that began it). No more than the window less one is ever outstanding: acks are read
    // only at a drain's start, so framing a backlog (the ring after a guard hold, a block after the loop was held)
    // to empty would make the sender give its window up at the 49th frame and re-sync, and a frame lost before it
    // would then be gone for good. What does not fit waits in the ring for the next drain, which reads the acks first.
    std::uint8_t message[rp::N8ReliableSender::kMaxMessage];
    while (messageCount_ != 0 && sender_->outstanding() < rp::N8ReliableSender::kWindow - 1) {
        const Message m = s_.messages[messageHead_];
        if (m.size > rp::N8ReliableSender::kMaxMessage) {
            // Longer than a frame carries (BlipToaster's longest SysEx is 37 bytes): dropped, as the sender drops it.
            takeFront(nullptr);
            ++counts_.unframed;
            continue;
        }
        takeFront(message);
        const std::uint64_t sentBefore = sender_->framesSent();
        sender_->sendMessage(message, m.size, emitTo);
        // Framed, or a real-time byte, which goes unframed (the ROM takes it anywhere). Anything else the sender
        // dropped without writing a byte of it (a System Common message, which the ROM ignores).
        if (sender_->framesSent() == sentBefore && !(m.size == 1 && message[0] >= 0xF8)) {
            ++counts_.unframed;
            continue;
        }
        if (out.carried == 0) out.firstUs = m.at;
        ++out.carried;
        if (m.sysex) ++out.sysex;
    }
    flush(writer, out);
}

void BoxCartLink::emit(const Writer& writer, Out& out, const std::uint8_t* data, std::size_t size, int seq) {
    // A frame is never split across writes (a sync and a frame are at most 2 + 43 bytes, far under a write): what does
    // not fit goes out first.
    if (out.length + size > kBatch) flush(writer, out);
    for (std::size_t i = 0; i < size; ++i) s_.batch[out.length++] = data[i];
    if (seq >= 0 && out.seqCount < rp::N8ReliableSender::kWindow)
        out.seqs[out.seqCount++] = static_cast<std::uint8_t>(seq);
}

void BoxCartLink::flush(const Writer& writer, Out& out) {
    if (out.length == 0) return;
    const std::int64_t sentNs = nowNs();
    const bool         ok = write(writer, s_.batch, out.length, out.carried, out.carried ? out.firstUs : nowUs());
    // A frame has left once its write was made, taken or not (N8Link::writeFramed marks them before its write): a
    // failed write's frames are re-sent on the timeout rather than left waiting to be sent.
    for (std::uint32_t i = 0; i < out.seqCount; ++i) sender_->onSent(out.seqs[i], sentNs);
    if (ok) {
        counts_.forwarded += out.carried;
        counts_.sysex += out.sysex;
    } else {
        counts_.failed += out.carried;
    }
    if (out.carried > counts_.maxBatch) counts_.maxBatch = out.carried;
    out = Out{};
}

// The oldest message off the ring, whole, its bytes copied to `to` (null: dropped).
void BoxCartLink::takeFront(std::uint8_t* to) {
    const std::uint32_t size = s_.messages[messageHead_].size;
    for (std::uint32_t i = 0; i < size; ++i) {
        if (to) to[i] = s_.bytes[byteHead_];
        byteHead_ = byteHead_ + 1 == kBytes ? 0 : byteHead_ + 1;
    }
    byteCount_ -= size;
    messageHead_ = messageHead_ + 1 == kMessages ? 0 : messageHead_ + 1;
    --messageCount_;
}

}  // namespace rp::box
```

- [ ] **Step 4: run them and see them pass**

```
timeout -k 10 1800 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh cmake --build /workspaces/RetroPlug-main/cuttlefish/build --target retroplug-device-mode-test -j$(nproc)
timeout -k 10 300 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh /workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-device-mode-test "[box-link]"
timeout -k 10 300 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh /workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-device-mode-test "[box]"
```

Expected: exit 0 for both runs: the ten `[box-link]` cases pass, then every `[box]` case (the existing ones unedited,
task 1's and these) passes.

- [ ] **Step 5: the wider suites this task reaches**

The whole device-mode binary (its device-mode objects compile the new link with no exceptions and no RTTI, and the
heap-header check reads what they include), and the n8 and mesen binaries, which include the n8-wire headers the link
now includes:

```
timeout -k 10 1800 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh cmake --build /workspaces/RetroPlug-main/cuttlefish/build --target retroplug-device-mode-test retroplug-n8-test retroplug-mesen-test -j$(nproc)
timeout -k 10 600 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh /workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-device-mode-test
timeout -k 10 300 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh /workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-n8-test
timeout -k 10 600 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh /workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-mesen-test
```

Expected: exit 0 for each, every summary with no failure.

- [ ] **Step 6: commit**

```
git -C /workspaces/RetroPlug-main/cuttlefish add pico/n8-host/BoxCartLink.hpp pico/n8-host/BoxCartLink.cpp \
  packages/native/test/device-mode/BoxHost.test.cpp packages/native/test/device-mode/DeviceModeEntries.hpp \
  packages/native/test/device-mode/DeviceModeNodes.cpp packages/native/cmake/tests.cmake
git -C /workspaces/RetroPlug-main/cuttlefish commit -F - <<'MSG'
box: the cart link speaks BlipToaster's reliable link, under the menu guard

The box wrote MIDI to the cart FIFO raw. Its cart link now runs the desktop's
reliable link and menu guard, from the one implementation in n8-wire, when its
Writer reads the cart's back-channel and its Clock has a 64-bit time: a connect
writes the link's sync and listens for the ack across passes without holding
the loop, an ack makes the link framed and silence leaves it raw, and each
drain reads the back-channel, takes the guard's turn, puts a due probe at the
front of its first write and, unless the guard holds, writes the re-sends and
the ring, framed into writes that never split a frame and never past the window
less one outstanding, so a backlog waits for the next drain's acks rather than
making the sender give its window up. The menu's answer drops the ring and the
window, counted not ready, and atMenu() says so;
quietForRead() says when an Edio read cannot meet an ack or the menu's 'k'.
The sender is made in place in the link's .bss storage and the guard lives
there too, all zeros at boot. Without a back-channel the link is the raw one
it was, so every earlier box case is unedited.

The cases run the link against FakeN8 through Edio (the probe choosing framed
or raw, no pass of delay, a 100-note backlog held to the window, a framed RESET
held to its probes and then dropped, a raw RESET seen at the 250 ms cadence,
two silent stalls falling back to raw, a long SysEx dropped), a scripted cart
(quietForRead with a probe awaited, inside an ack, with a fresh frame out),
the link's ROM model behind a wire losing 1 to 3 byte bursts (every message
once, in order), and a framed device-mode pass that allocates nothing.
MSG
```

---

## Task 4: the firmware runs the link

The firmware hands its cart link the back-channel and the 64-bit clock, so the link probes, frames and guards; turns
the guard's `atMenu()` into the boot (forwarding stops, the link disconnects, and the boot path runs again from the
`*t` handshake, skipping "`CMD_STATUS` non-zero, a game is running"); reads over Edio (`health_check`'s `CMD_STATUS`,
`sniff_report`'s `memRD`) only on a pass `cart.quietForRead()` allows (the volume's read-back is the link's own, task
3); connects the link only while the plan runs (a refused plan or a faulted node forwards nothing, design section
4); gains the `N8_BOX_LINK` knob (default 1; 0 skips the link probe, so the box streams raw, guarded: the latency
comparison's raw side); and adds `link=framed|raw`, `resends`, `resent` and `unframed` to `[stats]`. The latency trace
gains the back-channel read's time. `main.cpp` is not desktop-built, so the proof is `pnpm device:box`: both USB
variants link under the trap, and the image's sizes before (task 1, step 1) and after go in the commit.

**Files** (line numbers are the file at `7fa0d1e17`):

- Modify: `pico/n8-host/main.cpp:92-93` (the knob), `:113` (`menu_answered`), `:169-189` (`bridge_stats`),
  `:233-247` (`fifo_write`), `:263-266` (`cart_read`, `cart_writer`), `:600-616` (`stats_report`), `:731`
  (`setClock`, `setLinkAllowed`), `:787-798` (the boot on the menu's answer), `:814` (no connect without a plan),
  `:816` (the reads gated), `:858-864` (`tuh_cdc_mount_cb`).
- Modify: `pico/n8-host/CMakeLists.txt:258-262` (the knob list).
- Modify: `pico/n8-host/LatTrace.hpp:10-12` (its summary's description), `:129-131` (`backReads`), `:213-216`
  (`backRead`), `:362-383` (the passes line and its reset).

**Interfaces:**

- Consumes: task 3's `BoxCartLink::Writer::read`, `Clock::monotonicUs`, `setLinkAllowed`, `framed`, `atMenu`,
  `quietForRead`, `Counts::resends` / `resent` / `unframed`; `UsbCdcTransport::read(buf, size, 0)` (one USB pump and
  what it found); main.cpp's own `clock_us` (`time_us_64`).
- Produces: the `N8_BOX_LINK` knob (`-DN8_BOX_LINK=0`), the four `[stats]` fields, the `[lat] passes` line's
  `bcread a/m`; task 6 documents them.

### Steps

- [ ] **Step 1: the knob, the read and the clock**

In `pico/n8-host/main.cpp`:

Old (lines 92-93):

```cpp
#define LAT(call) ((void)0)
#endif
```

New:

```cpp
#define LAT(call) ((void)0)
#endif

// 0 = the cart link never probes for BlipToaster's reliable link and streams raw, its menu guard still on, as the
// desktop's N8Link::setLinkAllowed(false) does: the raw build the framed link's latency is measured against
// (docs/pico-box-framed-link-design.md section 6). Measurement only.
#ifndef N8_BOX_LINK
#define N8_BOX_LINK 1
#endif
```

Old (line 113):

```cpp
static uint32_t      menu_stuck_until = 0; // !0: the menu stopped answering - no forwarding; retry the boot then
```

New:

```cpp
static uint32_t      menu_stuck_until = 0; // !0: the menu stopped answering - no forwarding; retry the boot then
static bool          menu_answered = false; // the cart link's guard heard the menu: the next boot skips CMD_STATUS
```

Old (lines 263-266):

```cpp
static bool volume_write(void *, int value, bool again) {
    return rp::box::writeExpansionVolume(edio, value, again, print_line);
}
static const rp::box::BoxCartLink::Writer cart_writer = {nullptr, fifo_write, volume_write};
```

New:

```cpp
static bool volume_write(void *, int value, bool again) {
    return rp::box::writeExpansionVolume(edio, value, again, print_line);
}
// The cart link's back-channel: what the N8 has said (the reliable link's acks, the menu's answer to the guard's
// probe), read without waiting: one USB pump, and what it found.
static size_t cart_read(void *, uint8_t *data, size_t size) {
#if N8_LAT_TRACE
    const uint32_t from = time_us_32();
    const size_t   n = usb.read(data, size, 0);
    lat::backRead(time_us_32() - from);
    return n;
#else
    return usb.read(data, size, 0);
#endif
}
static const rp::box::BoxCartLink::Writer cart_writer = {nullptr, fifo_write, volume_write, cart_read};
```

Old (line 731):

```cpp
    cart.setClock({nullptr, now_us, arrival_of});
```

New:

```cpp
    cart.setClock({nullptr, now_us, arrival_of, clock_us});
    cart.setLinkAllowed(N8_BOX_LINK != 0);
```

- [ ] **Step 2: time and blink only the writes that carry MIDI**

Old (lines 233-247):

```cpp
// The cart link's FIFO writes: one fifoWR a batch (up to 192 bytes of whole messages; a write costs the same ~5 ms
// whatever it carries, so batching is what lets a burst through), each timed from the arrival of the first message it
// carries to the N8's ACK.
static bool fifo_write(void *, const uint8_t *batch, size_t len, uint32_t msgs, uint32_t first_us) {
    LAT(writeBegin(first_us, !ack_pending));
    if (!ack_pending) { ack_from_us = first_us; ack_pending = true; }   // armed first: the
    // write pumps tuh_task, so its completion can arrive before Edio::fifoWR even returns
    uint64_t t0 = time_us_64();
    bool ok = edio.fifoWR(batch, len).ok();
    LAT(writeEnd(ok));
    uint32_t us = (uint32_t)(time_us_64() - t0);
    if (!ok) ack_pending = false;
    write_fail_streak = ok ? 0 : write_fail_streak + 1;
    if (ok) led_activity();
```

New:

```cpp
// The cart link's FIFO writes: one fifoWR a batch (up to 192 bytes of whole messages, framed on a framed link; a write
// costs the same ~5 ms whatever it carries, so batching is what lets a burst through), each timed from the arrival of
// the first message it carries to the N8's ACK. A write that carries none of the ring's messages (the link's sync, the
// guard's probe, re-sends alone: `msgs` 0) is no message's latency and no MIDI activity, so it is neither timed nor
// blinked: a raw link's idle probe every 250 ms would otherwise blink the LED at 4 Hz and pull `lat=` down.
static bool fifo_write(void *, const uint8_t *batch, size_t len, uint32_t msgs, uint32_t first_us) {
    const bool timed = msgs != 0 && !ack_pending;
    LAT(writeBegin(first_us, timed));
    if (timed) { ack_from_us = first_us; ack_pending = true; }   // armed first: the
    // write pumps tuh_task, so its completion can arrive before Edio::fifoWR even returns
    uint64_t t0 = time_us_64();
    bool ok = edio.fifoWR(batch, len).ok();
    LAT(writeEnd(ok));
    uint32_t us = (uint32_t)(time_us_64() - t0);
    if (!ok && msgs != 0) ack_pending = false;
    write_fail_streak = ok ? 0 : write_fail_streak + 1;
    if (ok && msgs != 0) led_activity();
```

(`if (!ok) ack_pending = false;` becomes `if (!ok && msgs != 0)`: a failed write carrying MIDI disarms the timing as at
HEAD, and a failed sync or probe, which never armed it, leaves an earlier write's timing alone. The `N8_BRIDGE_VERBOSE`
and `N8_APU_LAT` blocks below stay as they are; the APU probe keys on `batch[0] == 0x90`, so on a framed link (whose
writes start `F4` or `F5`) it never fires, which is fine for a measurement build of the raw link.)

- [ ] **Step 3: `[stats]`**

Old (lines 186-189):

```cpp
    uint32_t link_full;    // messages lost to a full cart-link ring (one block's MIDI twice over: none expected)
    uint32_t skipped;      // frames the block driver dropped past its carry's bound (an N8 boot's wait, a long hold;
                           //   BlockDriver.hpp), saturating
} bridge_stats;
```

New:

```cpp
    uint32_t link_full;    // messages lost to a full cart-link ring (one block's MIDI twice over: none expected)
    uint32_t skipped;      // frames the block driver dropped past its carry's bound (an N8 boot's wait, a long hold;
                           //   BlockDriver.hpp), saturating
    uint32_t framed;       // 1: the cart link is framed (BlipToaster answered the connect's sync), 0: raw
    uint32_t resends;      // the framed link's re-send rounds, since boot
    uint32_t resent;       // frames those rounds wrote again
    uint32_t unframed;     // messages a framed link could not carry (a SysEx past 40 bytes), dropped
} bridge_stats;
```

Old (lines 600-616):

```cpp
    stats.skipped = driver.skipped() > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)driver.skipped();
    stats.filtered = rp::box::filtered(in, out);
    stats.lat_avg_us = lat_n ? lat_sum / lat_n : 0;
    stats.lat_max_us = lat_win_max;
    lat_sum = lat_n = lat_win_max = 0;
    if (memcmp(&last, &stats, sizeof stats) == 0) return;
    last = stats;
    printf("[stats] rx=%lu msgs=%lu sysex=%lu fwd=%lu fail=%lu writes=%lu maxbatch=%lu filtered=%lu "
           "notready=%lu oe=%lu fe=%lu ringfull=%lu maxfwd=%luus lat=%lu/%luus linkfull=%lu skipped=%lu\n",
           (unsigned long)stats.rx_bytes, (unsigned long)stats.msgs, (unsigned long)stats.sysex,
           (unsigned long)stats.fwd,
           (unsigned long)stats.fwd_fail, (unsigned long)stats.writes,
           (unsigned long)stats.max_batch, (unsigned long)stats.filtered,
           (unsigned long)stats.not_ready, (unsigned long)stats.uart_oe,
           (unsigned long)stats.uart_fe, (unsigned long)stats.ring_full,
           (unsigned long)stats.max_fwd_us, (unsigned long)stats.lat_avg_us,
           (unsigned long)stats.lat_max_us, (unsigned long)stats.link_full, (unsigned long)stats.skipped);
}
```

New:

```cpp
    stats.skipped = driver.skipped() > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)driver.skipped();
    stats.filtered = rp::box::filtered(in, out);
    stats.lat_avg_us = lat_n ? lat_sum / lat_n : 0;
    stats.lat_max_us = lat_win_max;
    stats.framed = cart.framed() ? 1u : 0u;
    stats.resends = out.resends;
    stats.resent = out.resent;
    stats.unframed = out.unframed;
    lat_sum = lat_n = lat_win_max = 0;
    if (memcmp(&last, &stats, sizeof stats) == 0) return;
    last = stats;
    printf("[stats] rx=%lu msgs=%lu sysex=%lu fwd=%lu fail=%lu writes=%lu maxbatch=%lu filtered=%lu "
           "notready=%lu oe=%lu fe=%lu ringfull=%lu maxfwd=%luus lat=%lu/%luus linkfull=%lu skipped=%lu "
           "link=%s resends=%lu resent=%lu unframed=%lu\n",
           (unsigned long)stats.rx_bytes, (unsigned long)stats.msgs, (unsigned long)stats.sysex,
           (unsigned long)stats.fwd,
           (unsigned long)stats.fwd_fail, (unsigned long)stats.writes,
           (unsigned long)stats.max_batch, (unsigned long)stats.filtered,
           (unsigned long)stats.not_ready, (unsigned long)stats.uart_oe,
           (unsigned long)stats.uart_fe, (unsigned long)stats.ring_full,
           (unsigned long)stats.max_fwd_us, (unsigned long)stats.lat_avg_us,
           (unsigned long)stats.lat_max_us, (unsigned long)stats.link_full, (unsigned long)stats.skipped,
           stats.framed ? "framed" : "raw", (unsigned long)stats.resends, (unsigned long)stats.resent,
           (unsigned long)stats.unframed);
}
```

- [ ] **Step 4: the menu's answer boots the ROM again, and the reads wait for a quiet pass**

Old (lines 787-798):

```cpp
        // What the block pushed, to the N8 (the cart link refused it all while no game runs).
        cart.drain(cart_writer, to_ms_since_boot(get_absolute_time()));
        LAT(mark(lat::N8));

        if (n8_ready && !probed) {
            led_set_mode(LED_BUSY);   // the boot blocks for seconds: show it now, not after
            probed = true;
            usb.bind(n8_cdc_idx);     // here, at the top of the loop: never inside an Edio call's wait
            int status = n8_probe();
            game_seen = false;
            boot_rom(status);
        }
```

New:

```cpp
        // What the block pushed, to the N8 (the cart link refused it all while no game runs).
        cart.drain(cart_writer, to_ms_since_boot(get_absolute_time()));
        LAT(mark(lat::N8));

        // The cart link's guard heard the N8 menu answer its probe: a console RESET took the game away. Forwarding
        // stops, the link lets go of what it held (it dropped it at the answer), and the boot runs again knowing the
        // menu is up: straight to the '*t' handshake, then the chosen ROM (or a pending MIDI pick). The reconnect after
        // it starts from a clean stream and probes the link afresh. CMD_STATUS turning 0 (health_check) stays the way
        // back for a menu reached while nothing streams, which the guard does not ask about.
        if (forward_ok && cart.atMenu()) {
            forward_ok = false;
            was_forwarding = false;
            cart.setConnected(false);
            game_seen = false;
            if (boot_error) {
                // The boot that left the box forwarding failed (its line said why), so this menu is the one it could
                // not leave: nothing is forwarded into it, and the boot is tried again in 20 s, as for a menu that
                // stopped answering, rather than at once and over again.
                printf("[box] the N8 menu answered the cart link's probe after a failed boot - retrying in 20 s\n");
                menu_stuck_until = to_ms_since_boot(get_absolute_time()) + 20000;
                menu_answered = true;  // the retry goes straight to '*t': CMD_STATUS at this menu is unknown
            } else {
                printf("[box] the N8 menu answered the cart link's probe - booting the chosen ROM\n");
                menu_answered = true;
                probed = false;
            }
        }

        if (n8_ready && !probed) {
            led_set_mode(LED_BUSY);   // the boot blocks for seconds: show it now, not after
            probed = true;
            usb.bind(n8_cdc_idx);     // here, at the top of the loop: never inside an Edio call's wait
            // After the menu answered the guard, CMD_STATUS is not asked: whether it reads 0 after a RESET was never
            // seen, and a non-zero one would take the menu for a game. The boot goes straight to the handshake.
            int status = menu_answered ? 0 : n8_probe();
            menu_answered = false;
            game_seen = false;
            boot_rom(status);
        }
```

Old (line 814):

```cpp
        cart.setConnected(forward_ok);
```

New:

```cpp
        // A plan the loader refused, or one whose node faulted, forwards nothing, so the link does not connect either
        // (docs/pico-box-framed-link-design.md section 4): no link probe and no guard probe go to the cart for it.
        cart.setConnected(forward_ok && plan.plan() && !plan.faulted());
```

Old (line 816):

```cpp
        if (forward_ok) { sniff_report(); health_check(); }
```

New:

```cpp
        // Each reads a reply off the USB input the link's acks and the menu's answer come in on, so each waits for a
        // pass the cart link says is quiet (an ack or a 'k' read as a reply's first byte would misread both); a pass
        // that is not is skipped, and the next quiet one runs it (their own cadences stand).
        if (forward_ok && cart.quietForRead()) sniff_report();
        if (forward_ok && cart.quietForRead()) health_check();
```

Old (lines 858-864):

```cpp
    if (vid == N8_VID && pid == N8_PID) {
        n8_cdc_idx = idx;
        menu_stuck_until = 0;
        n8_gen = n8_gen + 1;
        boot_error = false;   // a fresh mount gets a fresh boot
        n8_ready = true;
        probed = false;
```

New:

```cpp
    if (vid == N8_VID && pid == N8_PID) {
        n8_cdc_idx = idx;
        menu_stuck_until = 0;
        n8_gen = n8_gen + 1;
        boot_error = false;   // a fresh mount gets a fresh boot
        menu_answered = false;   // ... from its probe: what the last N8's menu answered says nothing of this one
        n8_ready = true;
        probed = false;
```

In `pico/n8-host/CMakeLists.txt`:

Old (lines 258-262):

```cmake
# Experiment knobs, each a plain #define in main.cpp with a default: N8_MENU_TRIES (how many
# '*t' attempts before giving up on the menu), N8_LAT_TRACE (the latency trace, LatTrace.hpp: a message's way through
# the box and what each part of a pass costs, printed after a second of quiet). The expansion volume is no knob: it is
# rp.n8.nes's `expansionVolume` param, unity unless plan.json sets it, which the cart link writes.
foreach(knob N8_MENU_TRIES N8_APU_LAT N8_TEST_HOOKS N8_LAT_TRACE)
```

New:

```cmake
# Experiment knobs, each a plain #define in main.cpp with a default: N8_MENU_TRIES (how many
# '*t' attempts before giving up on the menu), N8_LAT_TRACE (the latency trace, LatTrace.hpp: a message's way through
# the box and what each part of a pass costs, printed after a second of quiet), N8_BOX_LINK (0: the cart link never
# probes for BlipToaster's reliable link and streams raw, its menu guard still on: the raw build the framed link's
# latency is measured against). The expansion volume is no knob: it is rp.n8.nes's `expansionVolume` param, unity
# unless plan.json sets it, which the cart link writes.
foreach(knob N8_MENU_TRIES N8_APU_LAT N8_TEST_HOOKS N8_LAT_TRACE N8_BOX_LINK)
```

- [ ] **Step 5: the latency trace's back-channel read**

In `pico/n8-host/LatTrace.hpp`:

Old (lines 10-12):

```cpp
//   [lat] passes idle n=<N> <us> us avg, <us> max, <misses> miss avg, <misses> max | block in <p>% of passes,
//         <us> us avg, <us> max | busy <N> | seg max us usb .. stats .. | worst msg <us>: rx in <seg>, ack in <seg>
//
```

New:

```cpp
//   [lat] passes idle n=<N> <us> us avg, <us> max, <misses> miss avg, <misses> max | block in <p>% of passes,
//         <us> us avg, <us> max | busy <N> | seg max us usb .. stats .. | bcread <us> avg/max | worst msg <us>:
//         rx in <seg>, ack in <seg>
//
// `bcread` is the cart link's back-channel reads (main.cpp's cart_read: a USB pump and what it found), each timed,
// the one part of the link's drain a message's own points do not split out: the guard's turn and the framing are in
// push>write, the FIFO write in write.
//
```

Old (lines 129-131):

```cpp
    Acc rxDrain, drainBlock, blockPush, pushWrite, write, writeAck, total, path, pathMisses, msgBlock, msgBlockMisses,
        writeMisses, idleUs10, idleMiss, blockUs10;
    uint32_t over1500 = 0, passes = 0, blockPasses = 0, busy = 0;
```

New:

```cpp
    Acc rxDrain, drainBlock, blockPush, pushWrite, write, writeAck, total, path, pathMisses, msgBlock, msgBlockMisses,
        writeMisses, idleUs10, idleMiss, blockUs10;
    Acc backReads;  // the cart link's back-channel reads (bcread), each
    uint32_t over1500 = 0, passes = 0, blockPasses = 0, busy = 0;
```

Old (lines 213-216):

```cpp
// The link asked when a message it was pushed arrived, and was told `stamp`.
inline void pushed(uint32_t stamp) {
    if (g.pushes < State::kPushes) g.push[g.pushes++] = State::Push{stamp, time_us_32()};
}
```

New:

```cpp
// The link asked when a message it was pushed arrived, and was told `stamp`.
inline void pushed(uint32_t stamp) {
    if (g.pushes < State::kPushes) g.push[g.pushes++] = State::Push{stamp, time_us_32()};
}

// One read of the cart link's back-channel, `us` long (main.cpp's cart_read).
inline void backRead(uint32_t us) { g.backReads.add(us); }
```

Old (lines 362-383):

```cpp
    printf("[lat] passes idle n=%lu %lu.%lu us avg, %lu.%lu max, %lu miss avg, %lu max | block in %lu%% of passes, "
           "%lu.%lu us avg, %lu.%lu max | busy %lu | seg max us usb %lu uart %lu block %lu link %lu n8 %lu diag %lu "
           "watch %lu stats %lu | worst msg %lu: rx in %s, ack in %s\n",
           static_cast<unsigned long>(g.idleUs10.n), static_cast<unsigned long>(idle / 10),
           static_cast<unsigned long>(idle % 10), static_cast<unsigned long>(idleMax / 10),
           static_cast<unsigned long>(idleMax % 10), static_cast<unsigned long>(g.idleMiss.avg()),
           static_cast<unsigned long>(g.idleMiss.max),
           static_cast<unsigned long>(g.passes ? static_cast<uint64_t>(g.blockPasses) * 100u / g.passes : 0),
           static_cast<unsigned long>(blk / 10), static_cast<unsigned long>(blk % 10),
           static_cast<unsigned long>(blkMax / 10), static_cast<unsigned long>(blkMax % 10),
           static_cast<unsigned long>(g.busy), static_cast<unsigned long>(g.segMax[Usb]),
           static_cast<unsigned long>(g.segMax[Uart]), static_cast<unsigned long>(g.segMax[Block]),
           static_cast<unsigned long>(g.segMax[Link]), static_cast<unsigned long>(g.segMax[N8]),
           static_cast<unsigned long>(g.segMax[Diag]), static_cast<unsigned long>(g.segMax[Watch]),
           static_cast<unsigned long>(g.segMax[Stats]), static_cast<unsigned long>(g.worstTotal),
           kSegNames[g.worstRxSeg], kSegNames[g.worstAckSeg]);
    g.reportTo = time_us_32();
    // A fresh window: the counters only (the clock set-up, the XIP totals and the timelines carry on).
    for (Acc* a : {&g.rxDrain, &g.drainBlock, &g.blockPush, &g.pushWrite, &g.write, &g.writeAck, &g.total, &g.path,
                   &g.pathMisses, &g.msgBlock, &g.msgBlockMisses, &g.writeMisses, &g.idleUs10, &g.idleMiss,
                   &g.blockUs10})
        a->reset();
```

New:

```cpp
    char bcread[24];
    am(g.backReads, bcread, sizeof bcread);
    printf("[lat] passes idle n=%lu %lu.%lu us avg, %lu.%lu max, %lu miss avg, %lu max | block in %lu%% of passes, "
           "%lu.%lu us avg, %lu.%lu max | busy %lu | seg max us usb %lu uart %lu block %lu link %lu n8 %lu diag %lu "
           "watch %lu stats %lu | bcread %s | worst msg %lu: rx in %s, ack in %s\n",
           static_cast<unsigned long>(g.idleUs10.n), static_cast<unsigned long>(idle / 10),
           static_cast<unsigned long>(idle % 10), static_cast<unsigned long>(idleMax / 10),
           static_cast<unsigned long>(idleMax % 10), static_cast<unsigned long>(g.idleMiss.avg()),
           static_cast<unsigned long>(g.idleMiss.max),
           static_cast<unsigned long>(g.passes ? static_cast<uint64_t>(g.blockPasses) * 100u / g.passes : 0),
           static_cast<unsigned long>(blk / 10), static_cast<unsigned long>(blk % 10),
           static_cast<unsigned long>(blkMax / 10), static_cast<unsigned long>(blkMax % 10),
           static_cast<unsigned long>(g.busy), static_cast<unsigned long>(g.segMax[Usb]),
           static_cast<unsigned long>(g.segMax[Uart]), static_cast<unsigned long>(g.segMax[Block]),
           static_cast<unsigned long>(g.segMax[Link]), static_cast<unsigned long>(g.segMax[N8]),
           static_cast<unsigned long>(g.segMax[Diag]), static_cast<unsigned long>(g.segMax[Watch]),
           static_cast<unsigned long>(g.segMax[Stats]), bcread, static_cast<unsigned long>(g.worstTotal),
           kSegNames[g.worstRxSeg], kSegNames[g.worstAckSeg]);
    g.reportTo = time_us_32();
    // A fresh window: the counters only (the clock set-up, the XIP totals and the timelines carry on).
    for (Acc* a : {&g.rxDrain, &g.drainBlock, &g.blockPush, &g.pushWrite, &g.write, &g.writeAck, &g.total, &g.path,
                   &g.pathMisses, &g.msgBlock, &g.msgBlockMisses, &g.writeMisses, &g.idleUs10, &g.idleMiss,
                   &g.blockUs10, &g.backReads})
        a->reset();
```

- [ ] **Step 6: build the firmware, its trap and its trace build, and record the sizes**

`pnpm device:box` builds both USB variants (pico2, PIO-USB and `RP_NATIVE_USB`) under the link trap, each with its
trap probe, and prints their sizes; the `N8_LAT_TRACE=1` and `N8_BOX_LINK=0` builds are configured by hand into
build dirs of this plan's own under `.superpowers/pbplan/` (never `pico/n8-host/build*`, which are `loop.sh`'s), from
the same SDK and Pico-PIO-USB `tools/build-box.sh` uses:

```
timeout -k 10 1800 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh pnpm -C /workspaces/RetroPlug-main/cuttlefish device:box > /workspaces/RetroPlug-main/cuttlefish/.superpowers/pbplan/box-size-after.log 2>&1; echo "exit $?"
grep '^== \(pio\|native\):' /workspaces/RetroPlug-main/cuttlefish/.superpowers/pbplan/box-size-before.log /workspaces/RetroPlug-main/cuttlefish/.superpowers/pbplan/box-size-after.log
timeout -k 10 1800 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh cmake -S /workspaces/RetroPlug-main/cuttlefish/pico/n8-host -B /workspaces/RetroPlug-main/cuttlefish/.superpowers/pbplan/build-lat -DPICO_SDK_PATH=/workspaces/pico-sdk -DPICO_PIO_USB_PATH=/workspaces/Pico-PIO-USB -DCMAKE_BUILD_TYPE=Release -DPICO_NO_PICOTOOL=1 -DRP_EXPORT_PLAN_TOOL=/workspaces/RetroPlug-main/cuttlefish/build/export-plan/retroplug-export-plan -DRP_REFLECTCPP_SOURCE_DIR="$(sed -n 's/^reflectcpp_SOURCE_DIR:STATIC=//p' /workspaces/RetroPlug-main/cuttlefish/build/CMakeCache.txt)" -DN8_LAT_TRACE=1
timeout -k 10 1800 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh cmake --build /workspaces/RetroPlug-main/cuttlefish/.superpowers/pbplan/build-lat -j$(nproc)
timeout -k 10 1800 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh cmake -S /workspaces/RetroPlug-main/cuttlefish/pico/n8-host -B /workspaces/RetroPlug-main/cuttlefish/.superpowers/pbplan/build-raw -DPICO_SDK_PATH=/workspaces/pico-sdk -DPICO_PIO_USB_PATH=/workspaces/Pico-PIO-USB -DCMAKE_BUILD_TYPE=Release -DPICO_NO_PICOTOOL=1 -DRP_EXPORT_PLAN_TOOL=/workspaces/RetroPlug-main/cuttlefish/build/export-plan/retroplug-export-plan -DRP_REFLECTCPP_SOURCE_DIR="$(sed -n 's/^reflectcpp_SOURCE_DIR:STATIC=//p' /workspaces/RetroPlug-main/cuttlefish/build/CMakeCache.txt)" -DN8_BOX_LINK=0
timeout -k 10 1800 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh cmake --build /workspaces/RetroPlug-main/cuttlefish/.superpowers/pbplan/build-raw -j$(nproc)
```

Expected: `exit 0`; the grep prints the two `before` lines (task 1) and the two `after` lines, `== pio: flash <n>,
data <n>, bss <n> bytes; ...` and `== native: ...`; both hand builds end with `n8_host.elf` linked and exit 0. `data`
grows by a few hundred bytes (a `copy_to_ram` image keeps its constants there: the new strings and the longer
`[stats]` format, `kSync`, `kProbe`); the sender is raw storage and the guard all zeros, both in `.bss`, so a `data`
that grows by about 3.5 KB means the sender or the guard was constant-initialised to something non-zero, which is a
finding, not a size to record. If `/workspaces/pico-sdk` or `/workspaces/Pico-PIO-USB` is not where
`tools/build-box.sh` found its SDK, use the paths its log printed (`== the firmware, pio (... Pico SDK <path>,
...)`). A link error naming `__wrap_<symbol>` is the trap: the object it names reached the heap or a throw.

The RP2040 image, only if task 1, step 1 found it links at HEAD (`.superpowers/pbplan/box-rp2040.txt` holds a size
line, not "does not link"): build it again now, in a build dir of its own, and append its size line after the HEAD one.
If it did not link at HEAD, skip this: the HEAD log is the record.

```
timeout -k 10 1800 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh cmake -S /workspaces/RetroPlug-main/cuttlefish/pico/n8-host -B /workspaces/RetroPlug-main/cuttlefish/.superpowers/pbplan/build-rp2040-after -DPICO_BOARD=pico -DPICO_SDK_PATH=/workspaces/pico-sdk -DPICO_PIO_USB_PATH=/workspaces/Pico-PIO-USB -DCMAKE_BUILD_TYPE=Release -DPICO_NO_PICOTOOL=1 -DRP_EXPORT_PLAN_TOOL=/workspaces/RetroPlug-main/cuttlefish/build/export-plan/retroplug-export-plan -DRP_REFLECTCPP_SOURCE_DIR="$(sed -n 's/^reflectcpp_SOURCE_DIR:STATIC=//p' /workspaces/RetroPlug-main/cuttlefish/build/CMakeCache.txt)"
timeout -k 10 1800 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh cmake --build /workspaces/RetroPlug-main/cuttlefish/.superpowers/pbplan/build-rp2040-after -j$(nproc)
arm-none-eabi-size -A /workspaces/RetroPlug-main/cuttlefish/.superpowers/pbplan/build-rp2040-after/n8_host.elf | grep -E '^\.(text|data|bss) '
```

Expected (only when run): exit 0 for both, and the three rows, which go into `box-rp2040.txt` after the HEAD ones.

- [ ] **Step 7: commit**

The body's last paragraph is the four `==` lines the grep printed, before then after, copied as they are.

```
git -C /workspaces/RetroPlug-main/cuttlefish add pico/n8-host/main.cpp pico/n8-host/CMakeLists.txt \
  pico/n8-host/LatTrace.hpp
git -C /workspaces/RetroPlug-main/cuttlefish commit -F - <<'MSG'
box: the firmware streams framed, and a RESET's menu boots the ROM again

The firmware hands the cart link the N8's back-channel (one USB pump a read)
and its 64-bit clock, so the link probes for BlipToaster's reliable link at
each connect and runs the menu guard. When the guard hears the menu answer
its probe, forwarding stops, the link lets go, and the boot runs again
straight from the '*t' handshake, without asking CMD_STATUS, which need not
read 0 after a RESET; health_check's CMD_STATUS-0 return stays for a menu
reached while nothing streams. The sniff and health_check read over Edio only
on a pass the link says is quiet, and the link connects only while the plan
runs. N8_BOX_LINK=0 builds the box raw, guarded,
for the latency comparison. [stats] gains link=framed|raw, resends, resent
and unframed; a write that carries no MIDI (the sync, a probe, re-sends
alone) is neither timed nor blinked; the latency trace times the back-channel
read (bcread).

pnpm device:box, before and after (flash, data, bss):
<the two "== pio:" / "== native:" lines from box-size-before.log>
<the two "== pio:" / "== native:" lines from box-size-after.log>
MSG
```

Replace the two `<...>` lines with the four lines themselves before committing (each `==` line as printed, before
first). Then add the line `.superpowers/pbplan/box-rp2040.txt` holds (the RP2040 image: "does not link at HEAD, RAM
overflow", or its sizes before and after), prefixed `PICO_BOARD=pico: `.

---

## Task 5: `retroplug-n8-hwtest timing --midi`

The box is a performance path, so the framed link's cost in latency is measured (design section 6), and the tool that
measures the desktop's link from its USB write to the sound gains a mode that measures the box from its MIDI write:
`--midi <port>` writes the same arrangement through ALSA rawmidi to the CH345 cable plugged into the box's MIDI jack,
instead of through `N8Link`, each write stamped with `CLOCK_MONOTONIC` (the clock the L6 capture's timestamps are
taken in), so every number the tool prints keeps its meaning. The port is chosen by the CH345's whole name and refused
otherwise, every output is listed before anything is opened, and the existing modes are untouched. It cannot run here
(it opens the L6, and the CH345): this task builds it, and its parsing is read, not run.

**Files** (line numbers are the file at `7fa0d1e17`):

- Modify: `packages/n8/native/test/n8-timing.cpp:1-24` (the usage and the `--midi` paragraph), `:176-178` (the MIDI
  port's listing and opening, after `flag()`), `:180-268` (`runTiming`'s options, the link or the port, the write
  loop, the close), `:326-331` (the header line).
- Modify: `packages/n8/native/test/n8-sd-hwtest.cpp:27` (the usage line).

**Interfaces:**

- Consumes: ALSA's `snd_card_next`, `snd_ctl_open`, `snd_ctl_rawmidi_next_device`, `snd_rawmidi_info_*`,
  `snd_ctl_rawmidi_info`, `snd_ctl_close`, `snd_rawmidi_open`, `snd_rawmidi_write`, `snd_rawmidi_drain`,
  `snd_rawmidi_close` (`<alsa/asoundlib.h>`, already included; the binary links `ALSA::ALSA` where `timing` is built).
- Produces: `retroplug-n8-hwtest timing <idle|busy> [steps] ... --midi "<CH345 port name>"`. With `--midi`, `--raw`
  means the box is the raw build (`-DN8_BOX_LINK=0`) and only adds the eight unframed messages that drop a ROM left in
  link mode back to plain MIDI, as it does for the desktop's raw run; `--guard` is refused; `--port` is unused.

### Steps

- [ ] **Step 1: the usage and the mode's description**

In `packages/n8/native/test/n8-timing.cpp`:

Old (lines 1-6):

```cpp
// retroplug-n8-hwtest timing - BlipToaster's MIDI latency, jitter and latency-under-load on a REAL console, in ms.
//
//   retroplug-n8-hwtest timing <idle|busy> [steps] [--raw] [--no-kick] [--pcm hw:L6,0] [--tick-ch 3] [--kick-ch 4] [--db -40]
//                              [--wav out.wav] [--port /dev/ttyACM0] [--guard]
//
// Plays a 170 bpm arrangement through N8Link - the code the plugin streams to a cart with, over the reliable link
```

New:

```cpp
// retroplug-n8-hwtest timing - BlipToaster's MIDI latency, jitter and latency-under-load on a REAL console, in ms.
//
//   retroplug-n8-hwtest timing <idle|busy> [steps] [--raw] [--no-kick] [--pcm hw:L6,0] [--tick-ch 3] [--kick-ch 4] [--db -40]
//                              [--wav out.wav] [--port /dev/ttyACM0] [--guard] [--midi "<CH345 port name>"]
//
// --midi times the Pico box instead (docs/pico-box-framed-link-design.md section 6): the same arrangement goes out
// of the CH345 USB MIDI cable plugged into the box's MIDI jack, through ALSA rawmidi, each write stamped with the
// clock the capture is (CLOCK_MONOTONIC), so a latency runs from the MIDI write to the sound as it otherwise runs from
// the USB write. The port is named by its whole name as `amidi -l` prints it, and it must be the CH345's: every MIDI
// output is listed first (through the cards' control interfaces, which opens no port), and a name no output has
// exactly, one two outputs share, or one without "CH345" is refused before anything is opened. The box frames, guards
// and re-sends on its own, so its re-sends are its [stats] line's; --raw then means the box is its raw build
// (N8_BOX_LINK=0) and only adds the unframed messages that take a ROM out of link mode; --guard is the desktop link's,
// and refused.
//
// Plays a 170 bpm arrangement through N8Link - the code the plugin streams to a cart with, over the reliable link
```

- [ ] **Step 2: the MIDI port, listed and matched before it is opened**

In `packages/n8/native/test/n8-timing.cpp`:

Old (lines 173-178):

```cpp
bool flag(int argc, char** argv, const char* name) {
    for (int i = 1; i < argc; ++i) if (std::strcmp(argv[i], name) == 0) return true;
    return false;
}

}  // namespace
```

New:

```cpp
bool flag(int argc, char** argv, const char* name) {
    for (int i = 1; i < argc; ++i) if (std::strcmp(argv[i], name) == 0) return true;
    return false;
}

// A rawmidi output as `amidi -l` lists it: hw:<card>,<device>,<subdevice> and its name (the subdevice's, or the
// device's where the subdevice has none).
struct MidiPort { std::string hw, name; };

// Every rawmidi output ALSA has, read through each card's control interface: no MIDI port is opened to list them.
std::vector<MidiPort> listMidiOutputs() {
    std::vector<MidiPort> out;
    snd_rawmidi_info_t*   info;
    snd_rawmidi_info_alloca(&info);
    int card = -1;
    while (snd_card_next(&card) == 0 && card >= 0) {
        char ctlName[32];
        std::snprintf(ctlName, sizeof ctlName, "hw:%d", card);
        snd_ctl_t* ctl = nullptr;
        if (snd_ctl_open(&ctl, ctlName, 0) < 0) continue;
        int device = -1;
        while (snd_ctl_rawmidi_next_device(ctl, &device) == 0 && device >= 0) {
            snd_rawmidi_info_set_device(info, static_cast<unsigned>(device));
            snd_rawmidi_info_set_stream(info, SND_RAWMIDI_STREAM_OUTPUT);
            snd_rawmidi_info_set_subdevice(info, 0);
            if (snd_ctl_rawmidi_info(ctl, info) < 0) continue;   // no output on this device
            const unsigned subs = snd_rawmidi_info_get_subdevices_count(info);
            for (unsigned sub = 0; sub < subs; ++sub) {
                snd_rawmidi_info_set_subdevice(info, sub);
                if (snd_ctl_rawmidi_info(ctl, info) < 0) continue;
                const char* subName = snd_rawmidi_info_get_subdevice_name(info);
                const char* devName = snd_rawmidi_info_get_name(info);
                char hw[48];
                std::snprintf(hw, sizeof hw, "hw:%d,%d,%u", card, device, sub);
                out.push_back({hw, subName && *subName ? subName : (devName ? devName : "")});
            }
        }
        snd_ctl_close(ctl);
    }
    return out;
}

// The box's MIDI cable (timing --midi), opened for output: the one output named `name` exactly, which must be the
// CH345's. Every output is printed first, so a refusal shows what there is; a name without "CH345", one no output has,
// or one two outputs share opens nothing. The L6's own MIDI ports are its mixer's control ports, which a stray write
// can reconfigure, so a name is matched whole, never as a prefix or a part. Null on a refusal, said on stderr.
snd_rawmidi_t* openBoxMidi(const std::string& name, std::string& hw) {
    const std::vector<MidiPort> ports = listMidiOutputs();
    std::fprintf(stderr, "timing: MIDI outputs:\n");
    for (const MidiPort& p : ports) std::fprintf(stderr, "  %-14s %s\n", p.hw.c_str(), p.name.c_str());
    if (name.find("CH345") == std::string::npos) {
        std::fprintf(stderr, "timing: --midi \"%s\" is not the CH345 cable's port; refused\n", name.c_str());
        return nullptr;
    }
    const MidiPort* match = nullptr;
    for (const MidiPort& p : ports) {
        if (p.name != name) continue;
        if (match) {
            std::fprintf(stderr, "timing: two MIDI outputs are named \"%s\" (%s, %s); refused\n", name.c_str(),
                         match->hw.c_str(), p.hw.c_str());
            return nullptr;
        }
        match = &p;
    }
    if (!match) {
        std::fprintf(stderr, "timing: no MIDI output is named \"%s\" exactly; refused\n", name.c_str());
        return nullptr;
    }
    snd_rawmidi_t* out = nullptr;
    if (snd_rawmidi_open(nullptr, &out, match->hw.c_str(), 0) < 0) {
        std::fprintf(stderr, "timing: snd_rawmidi_open %s failed\n", match->hw.c_str());
        return nullptr;
    }
    hw = match->hw;
    return out;
}

}  // namespace
```

- [ ] **Step 3: `runTiming` writes through the box when asked**

In `packages/n8/native/test/n8-timing.cpp`:

Old (lines 180-196):

```cpp
int runTiming(int argc, char** argv, const std::string& defaultPort) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s timing <idle|busy> [steps] [--raw] [--pcm hw:L6,0] [--tick-ch 3] [--kick-ch 4] "
                             "[--db -40] [--wav out.wav] [--port dev] [--guard]\n", argv[0]);
        return 2;
    }
    const std::string mode  = argv[2];
    const bool        busy  = (mode == "busy");
    const int         steps = (argc > 3 && argv[3][0] != '-') ? std::atoi(argv[3]) : 128;
    const bool        raw   = flag(argc, argv, "--raw");
    const bool        noKick = flag(argc, argv, "--no-kick");   // no DMC: tells DMC-DMA effects from the rest
    const bool        guard  = flag(argc, argv, "--guard");     // the menu guard on: what its probes cost the game
    const std::string pcmDev = arg(argc, argv, "--pcm", "hw:L6,0");
    const std::string port  = arg(argc, argv, "--port", defaultPort.c_str());
    const float       thr   = static_cast<float>(std::pow(10.0, std::atof(arg(argc, argv, "--db", "-40")) / 20.0));
    const char*       wav   = arg(argc, argv, "--wav", nullptr);
    if (port.empty()) { std::fprintf(stderr, "no Everdrive N8 found; pass --port\n"); return 2; }
```

New:

```cpp
int runTiming(int argc, char** argv, const std::string& defaultPort) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s timing <idle|busy> [steps] [--raw] [--pcm hw:L6,0] [--tick-ch 3] [--kick-ch 4] "
                             "[--db -40] [--wav out.wav] [--port dev] [--guard] [--midi \"<CH345 port name>\"]\n",
                     argv[0]);
        return 2;
    }
    const std::string mode  = argv[2];
    const bool        busy  = (mode == "busy");
    const int         steps = (argc > 3 && argv[3][0] != '-') ? std::atoi(argv[3]) : 128;
    const bool        raw   = flag(argc, argv, "--raw");
    const bool        noKick = flag(argc, argv, "--no-kick");   // no DMC: tells DMC-DMA effects from the rest
    const bool        guard  = flag(argc, argv, "--guard");     // the menu guard on: what its probes cost the game
    const std::string pcmDev = arg(argc, argv, "--pcm", "hw:L6,0");
    const std::string port  = arg(argc, argv, "--port", defaultPort.c_str());
    const float       thr   = static_cast<float>(std::pow(10.0, std::atof(arg(argc, argv, "--db", "-40")) / 20.0));
    const char*       wav   = arg(argc, argv, "--wav", nullptr);
    const char*       midiName = arg(argc, argv, "--midi", nullptr);   // through the box: the CH345's whole name
    if (midiName && guard) {
        std::fprintf(stderr, "timing: --guard is the desktop link's; the box runs its own guard\n");
        return 2;
    }
    if (!midiName && port.empty()) { std::fprintf(stderr, "no Everdrive N8 found; pass --port\n"); return 2; }
```

Old (lines 235-257):

```cpp
    // The link, with its write log.
    std::mutex wm;
    std::vector<std::pair<std::int64_t, std::vector<std::uint8_t>>> writes;
    N8Link link(
        [](const std::string& p) -> std::unique_ptr<PortTransport> { return std::make_unique<SerialTransport>(p); });
    link.setLinkAllowed(!raw);
    // The menu guard on, as the plugin runs it, so its probes are among the writes (a raw stream carries one every
    // 250 ms; a framed one only in a stall). The connect seed is taken as a game, which is where this leg runs.
    if (guard) link.setMenuCheck([](rp::n8::Edio&) { return 0; });
    link.setWriteLog([&](const std::uint8_t* d, std::size_t n, std::int64_t ns, bool resend) {
        if (resend) return;   // a hit's latency runs from its FIRST write: a re-send is part of it
        std::lock_guard<std::mutex> l(wm);
        writes.emplace_back(ns, std::vector<std::uint8_t>(d, d + n));
    });
    if (!link.connect(port)) { std::fprintf(stderr, "timing: connect failed: %s\n", link.lastError().c_str()); return 1; }
    link.setLookaheadMs(0);

    Capture cap;
    cap.tickCh = static_cast<unsigned>(std::atoi(arg(argc, argv, "--tick-ch", "3")) - 1);
    cap.kickCh = static_cast<unsigned>(std::atoi(arg(argc, argv, "--kick-ch", "4")) - 1);
    if (!cap.open(pcmDev, wav != nullptr)) { std::fprintf(stderr, "timing: capture: %s\n", cap.err.c_str()); return 1; }
    std::this_thread::sleep_for(std::chrono::milliseconds(400));   // the capture settles; the link probe is done
    const bool reliable = link.reliable();
```

New:

```cpp
    // The link, with its write log, or (--midi) the box's MIDI cable, whose writes are logged the same way.
    std::mutex wm;
    std::vector<std::pair<std::int64_t, std::vector<std::uint8_t>>> writes;
    N8Link link(
        [](const std::string& p) -> std::unique_ptr<PortTransport> { return std::make_unique<SerialTransport>(p); });
    snd_rawmidi_t* midiOut = nullptr;
    std::string    midiHw;
    if (midiName) {
        midiOut = openBoxMidi(midiName, midiHw);
        if (!midiOut) return 2;
    } else {
        link.setLinkAllowed(!raw);
        // The menu guard on, as the plugin runs it, so its probes are among the writes (a raw stream carries one every
        // 250 ms; a framed one only in a stall). The connect seed is taken as a game, which is where this leg runs.
        if (guard) link.setMenuCheck([](rp::n8::Edio&) { return 0; });
        link.setWriteLog([&](const std::uint8_t* d, std::size_t n, std::int64_t ns, bool resend) {
            if (resend) return;   // a hit's latency runs from its FIRST write: a re-send is part of it
            std::lock_guard<std::mutex> l(wm);
            writes.emplace_back(ns, std::vector<std::uint8_t>(d, d + n));
        });
        if (!link.connect(port)) {
            std::fprintf(stderr, "timing: connect failed: %s\n", link.lastError().c_str());
            return 1;
        }
        link.setLookaheadMs(0);
    }

    Capture cap;
    cap.tickCh = static_cast<unsigned>(std::atoi(arg(argc, argv, "--tick-ch", "3")) - 1);
    cap.kickCh = static_cast<unsigned>(std::atoi(arg(argc, argv, "--kick-ch", "4")) - 1);
    if (!cap.open(pcmDev, wav != nullptr)) {
        std::fprintf(stderr, "timing: capture: %s\n", cap.err.c_str());
        if (midiOut) snd_rawmidi_close(midiOut);
        return 1;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(400));   // the capture settles; the link probe is done
    const bool reliable = link.reliable();
```

Old (lines 259-266):

```cpp
    const auto t0 = Clock::now();
    for (const Ev& e : evs) {
        std::this_thread::sleep_until(t0 + std::chrono::microseconds(static_cast<std::int64_t>(e.ms * 1000)));
        link.push(0, e.bytes.data(), e.bytes.size(), 48000.0);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    const std::uint64_t resends = link.linkResends();
    link.disconnect();
```

New:

```cpp
    const auto t0 = Clock::now();
    bool       midiFailed = false;
    for (const Ev& e : evs) {
        std::this_thread::sleep_until(t0 + std::chrono::microseconds(static_cast<std::int64_t>(e.ms * 1000)));
        if (!midiOut) {
            link.push(0, e.bytes.data(), e.bytes.size(), 48000.0);
            continue;
        }
        // Stamped just before the write, as N8Link's write log stamps its USB writes, with the same clock.
        const std::int64_t ns = monoNs();
        if (snd_rawmidi_write(midiOut, e.bytes.data(), e.bytes.size()) < 0) {
            midiFailed = true;
            break;
        }
        std::lock_guard<std::mutex> l(wm);
        writes.emplace_back(ns, e.bytes);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    const std::uint64_t resends = link.linkResends();
    if (midiOut) {
        snd_rawmidi_drain(midiOut);
        snd_rawmidi_close(midiOut);
    } else {
        link.disconnect();
    }
    if (midiFailed) {
        cap.stop();
        std::fprintf(stderr, "timing: a write to %s failed\n", midiHw.c_str());
        return 1;
    }
```

Old (lines 326-328):

```cpp
    std::printf("timing: %s, %d steps @ 170 bpm, %s, %llu re-sends\n", mode.c_str(), steps,
                reliable ? "reliable link" : (raw ? "raw (--raw)" : "raw (the ROM did not answer the probe)"),
                static_cast<unsigned long long>(resends));
```

New:

```cpp
    if (midiName)
        std::printf("timing: %s, %d steps @ 170 bpm, through the box (%s \"%s\"%s), its re-sends on its [stats] line\n",
                    mode.c_str(), steps, midiHw.c_str(), midiName, raw ? ", the raw build (--raw)" : "");
    else
        std::printf("timing: %s, %d steps @ 170 bpm, %s, %llu re-sends\n", mode.c_str(), steps,
                    reliable ? "reliable link" : (raw ? "raw (--raw)" : "raw (the ROM did not answer the probe)"),
                    static_cast<unsigned long long>(resends));
```

Old (line 331):

```cpp
    std::printf("  voice   n    mean    p50    p95    max  jitter(sd)  missed   [ms, USB write -> sound]\n");
```

New:

```cpp
    std::printf("  voice   n    mean    p50    p95    max  jitter(sd)  missed   [ms, %s write -> sound]\n",
                midiName ? "MIDI" : "USB");
```

(The capture's own `cap.stop()` and the rest stay as they are.)

In `packages/n8/native/test/n8-sd-hwtest.cpp`:

Old (line 27):

```cpp
//   retroplug-n8-hwtest timing   <idle|busy> [steps] [--raw]   # latency / jitter / load on the real console
```

New:

```cpp
//   retroplug-n8-hwtest timing   <idle|busy> [steps] [--raw] [--midi <port>]  # latency / jitter / load, real console
```

- [ ] **Step 4: build it (never run it)**

```
timeout -k 10 1800 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh cmake --build /workspaces/RetroPlug-main/cuttlefish/build --target retroplug-n8-hwtest -j$(nproc)
nm -C /workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-n8-hwtest | grep -c "runTiming"
```

Expected: the build exits 0 with no warning from `n8-timing.cpp`; the count is above 0 (`runTiming` is in the
binary: this build found ALSA, so `timing`, and its `--midi`, are compiled in, not skipped). The binary opens the
real cart, the L6 and the CH345: it is never run here, and its `--midi` parsing (the refusals above) is checked by
reading the code against step 2.

- [ ] **Step 5: commit**

```
git -C /workspaces/RetroPlug-main/cuttlefish add packages/n8/native/test/n8-timing.cpp \
  packages/n8/native/test/n8-sd-hwtest.cpp
git -C /workspaces/RetroPlug-main/cuttlefish commit -F - <<'MSG'
n8: timing --midi measures the box from its MIDI write to the sound

The box's framed link is measured against its raw build and against the
desktop's link, so the timing leg gains a mode that writes the arrangement
through ALSA rawmidi to the CH345 cable in the box's MIDI jack instead of
through N8Link, each write stamped with CLOCK_MONOTONIC as the L6 capture's
timestamps are. The port is the CH345's whole name as amidi -l prints it:
every output is listed through the cards' control interfaces first, and a
name without CH345, one no output has exactly, or one two outputs share is
refused before anything is opened. --guard is refused with --midi, and the
existing modes are unchanged. Built here, never run: it needs the rig.
MSG
```

---

## Task 6: the documents

The specs and the box's README say what the box does now. spec/07's entry shrinks to what this work leaves open (the
rig's acceptance, design section 7, and the latency runs, section 6), retitled, and its checklist line with it;
spec/13's "Where the guard runs" adds the box; spec/00's `n8-wire` row names the guard; and the README's RESET
section, its menu-return sentence, its `[stats]` paragraph, its knob list, its layout paragraph and its `device:box`
sizes say what is built. The design's as-built and execution sections are the controller's. Documents only: no
failing test, and `pnpm check:docs` is the check.

**Files** (line numbers are the file at `7fa0d1e17`):

- Modify: `spec/07-remaining-work.md:71` (the checklist line), `:569-581` (the entry), `:592-593` (the CLI entry's
  pointer to the box's).
- Modify: `spec/13-subsystems.md:181-185` ("Where the guard runs"), `:199` ("Prove a change").
- Modify: `spec/00-overview.md:104` (the `n8-wire` row).
- Modify: `pico/n8-host/README.md:109` (the knob), `:159-161` (`[stats]`), `:344-345` (the menu coming back),
  `:370-379` (RESET, and what is not verified), `:388-389` (the layout), `:446-448` (`device:box`'s sizes).

**Interfaces:** none (documents). The README's sizes are task 4's `box-size-after.log` figures.

### Steps

- [ ] **Step 1: spec/07**

In `spec/07-remaining-work.md`:

Old (line 71):

```markdown
- [ ] [The box's cart stream has no framing and no menu guard](#the-boxs-cart-stream-has-no-framing-and-no-menu-guard)
```

New:

```markdown
- [ ] [The box's framed link and menu guard are unproven on the rig](#the-boxs-framed-link-and-menu-guard-are-unproven-on-the-rig)
```

Old (lines 569-581):

```markdown
### The box's cart stream has no framing and no menu guard

The box writes MIDI to the cart FIFO raw ([BoxCartLink.hpp](../pico/n8-host/BoxCartLink.hpp)): it predates the framed
link to BlipToaster (the box began 2026-08-14, the link 2026-10-22) and never moved onto it, and no design decided
against it. So on an NTSC console the DMC sample DMA's repeated FIFO read drops bytes (a lost hit or a stuck note),
which the lab's PAL console never shows, and a console RESET mid-song is not guarded, so a note stream across the
booting OS's window leaves it at ERROR:40 (a second RESET recovers). The host half of the link (`N8ReliableSender`, in
the `n8-wire` package) is transport-agnostic; whether it and the menu guard fit the box's RAM and code budget beside the
engine is unmeasured. Measured on the lab's NTSC Famicom (2026-11-19, `retroplug-n8-hwtest timing idle 512 --tick-ch 3
--kick-ch 6 --db -20`, through the desktop link, DMC kicks playing): framed, every run sounded 512 of 512 ticks on the
grid (p50 0.13 ms), repairing 3 to 7 lost bytes a 45 s run; raw, 1 tick in 3 of 7 runs never sounded, 5 of 7 runs had
notes off the grid (p50 5.9 to 48.6 ms), and a DMC note was left stuck. So the box needs the link on NTSC; what remains
is whether it fits. ([the menu guard's design](../docs/n8-menu-guard-reset-design.md) section 7.)
```

New:

```markdown
### The box's framed link and menu guard are unproven on the rig

The box streams to the cart through BlipToaster's reliable link and the N8 menu guard, the desktop's, from the one
implementation both run (`N8ReliableSender` and `N8MenuGuard`, in the `n8-wire` package;
[BoxCartLink.hpp](../pico/n8-host/BoxCartLink.hpp), [the box's design](../docs/pico-box-framed-link-design.md)), and a
RESET's menu boots the staged ROM again. That is held headlessly (`retroplug-device-mode-test "[box-link]"`: the link
over FakeN8, a scripted cart and the link's ROM model behind a lossy wire) and the image links under the trap (`pnpm
device:box`); the rig has not run it (design section 7). Once the rig is rewired for the box (the N8 in the Pico's
socket, the debug probe, the CH345 in its jack, the NTSC Famicom): `stress.sh` passing with nothing lost and `[stats]`
showing `link=framed` and some re-sends; a soak with the DMC kicks keeping the sniffer in sync; three RESETs mid-song
each coming back playing (an audio capture after each), no video frame showing ERROR:40, and the press-to-sound time
measured. And the latency runs (section 6), `retroplug-n8-hwtest timing ... --midi "<CH345 port>"` in `idle` and
`busy`: the box framed against its raw build (`-DN8_BOX_LINK=0`) with `--no-kick` and with the DMC kicks, the box
framed against the desktop's framed link to the same ROM, and the `N8_LAT_TRACE` build's parts of a pass (`bcread`,
`push>write`, `write`) against the raw build's, with the `[stats]` `lat` under `stress.sh`'s soak rates. The bar, fixed
before the runs: without DMC, framed within 1 ms of raw at p50 and 2 ms at p95, and no worse grid error; with DMC, no
missed hit and no stuck note in 5 runs, the tail (p95, max) reported beside the box's re-sends. A miss is a finding to
investigate before acceptance. The NTSC loss that makes the link the box's need is in the design's section 1.
```

Old (lines 592-593):

```markdown
`n8-bridge` is a performance path, so on an NTSC console it loses bytes to the DMC DMA (missed, off-grid and stuck notes
on the lab's Famicom, see the box's entry) and a console RESET mid-song can leave the N8 OS at ERROR:40. Move the bridge
```

New:

```markdown
`n8-bridge` is a performance path, so on an NTSC console it loses bytes to the DMC DMA (missed, off-grid and stuck notes
on the lab's Famicom, [the box's design](../docs/pico-box-framed-link-design.md) section 1) and a console RESET
mid-song can leave the N8 OS at ERROR:40. Move the bridge
```

- [ ] **Step 2: spec/13**

In `spec/13-subsystems.md`:

Old (lines 181-185):

```markdown
  - **Where the guard runs.** Only on the app's path to a cart: `N8Link` under `N8Host`, which the cart node streams
    through in the plugin and the standalones. The CLI's `n8-bridge` and `n8-play`, risa's `n8-sync` and the Pico box
    write the cart FIFO without it, raw and unguarded
    ([spec/07](07-remaining-work.md#the-clis-n8-tools-stream-unframed-and-unguarded),
    [box](07-remaining-work.md#the-boxs-cart-stream-has-no-framing-and-no-menu-guard)).
```

New:

```markdown
  - **Where the guard runs.** On the app's path to a cart, `N8Link` under `N8Host`, which the cart node streams through
    in the plugin and the standalones, and on the Pico box, whose cart link (`BoxCartLink`) drives the same
    `N8MenuGuard` and `N8ReliableSender` (the `n8-wire` package's) at each main-loop drain
    ([the box's design](../docs/pico-box-framed-link-design.md)): it probes the link at a connect without holding the
    loop, its probe goes at the front of a write (a drain starts between two messages), the menu's `k` sends the box
    back through its boot (straight to `*t`, then the staged ROM, about 6 s from the press to playing, most of it the
    boot's wait for the game), and the box's own Edio reads (`CMD_STATUS`, the sniff, the volume's read-back) wait for a
    quiet pass. The rig has not run the box's
    ([spec/07](07-remaining-work.md#the-boxs-framed-link-and-menu-guard-are-unproven-on-the-rig)). The CLI's
    `n8-bridge` and `n8-play` and risa's `n8-sync` write the cart FIFO without it, raw and unguarded
    ([spec/07](07-remaining-work.md#the-clis-n8-tools-stream-unframed-and-unguarded)).
```

Old (line 199):

```markdown
  over the fake N8); for the box, `pnpm device:box`, then the rig's `loop.sh`, `stress.sh` and `faults.sh` by hand, each
```

New:

```markdown
  over the fake N8); for the box, `retroplug-device-mode-test "[box]"` (its cart link's framed link and guard are
  `[box-link]`), `pnpm device:box`, then the rig's `loop.sh`, `stress.sh` and `faults.sh` by hand, each
```

- [ ] **Step 3: spec/00**

In `spec/00-overview.md`:

Old (line 104):

```markdown
| [`packages/n8-wire/`](../packages/n8-wire) | The header-only wire between a console and the N8: `N8ReliableLink.hpp` and the expansion-volume range, device-ready, so the emulated NES FIFO role and the cart's link share one definition instead of `mesen` depending on `n8`. | `n8` and `mesen`. |
```

New:

```markdown
| [`packages/n8-wire/`](../packages/n8-wire) | The header-only wire between a console and the N8: `N8ReliableLink.hpp` (the reliable link's sender), `N8MenuGuard.hpp` (the N8 menu guard's decisions) and the expansion-volume range, heap-free and device-ready, so the emulated NES FIFO role, the cart's desktop link and the Pico box's cart link share one definition instead of `mesen` depending on `n8`. | `n8` and `mesen`; the Pico box's cart link through `n8`. |
```

- [ ] **Step 4: the box's README**

In `pico/n8-host/README.md`:

Old (line 109):

```
./loop.sh -DN8_MENU_TRIES=3                   # any CMake define; each set gets its own build dir
```

New:

```
./loop.sh -DN8_MENU_TRIES=3                   # any CMake define; each set gets its own build dir
./loop.sh -DN8_BOX_LINK=0                     # the raw build: no reliable-link probe (the guard stays on)
```

Old (lines 159-161):

```markdown
released, not a stuck one - plus a `[stats]` line once a second while anything moves (bytes in,
messages parsed / forwarded, fifoWRs and their largest batch, and every way a byte can be lost:
UART overrun, framing error, full receive ring; and `lat=avg/worst` in µs for the window).
```

New:

```markdown
released, not a stuck one - plus a `[stats]` line once a second while anything moves (bytes in,
messages parsed / forwarded, fifoWRs and their largest batch, and every way a byte can be lost:
UART overrun, framing error, full receive ring; and `lat=avg/worst` in µs for the window, timed for writes that carry
MIDI). `link=framed` or `link=raw` is the cart link's state (framed once BlipToaster answered the connect's sync),
`resends` and `resent` its re-send rounds and the frames they wrote again, and `unframed` the messages a framed link
could not carry (a SysEx past 40 bytes, which BlipToaster acts on none of); `writes` counts the link's syncs, the
guard's probes and re-sends too.
```

Old (lines 344-345):

```markdown
The menu coming back is noticed by `CMD_STATUS` turning 0 after a game was seen, and the chosen ROM is
booted again.
```

New:

```markdown
The menu coming back is noticed by the cart link's menu guard while the box forwards (the menu's `k` to its probe, see
RESET below), and by `CMD_STATUS` turning 0 after a game was seen while nothing streams; either boots the chosen ROM
again.
```

Old (lines 370-379):

```markdown
**RESET.** On this rig the NES's RESET button restarts the running ROM in place - the N8 does not show
its menu (a 10 fps brightness trace of the press: ~2 s blank, ~1.4 s of lost signal, then BlipToaster
again, never the menu's brighter frames) - so the box has nothing to do and plays on (440.30 Hz after
two presses). Whether RESET goes to the menu is the N8's own setting; the box's menu-return path
(`CMD_STATUS` 0 after a game) is for when it does, and that path is the one not seen here.

**Not yet verified on hardware:** the "never enumerated" recovery firing for a real non-enumerating
device (the only trigger seen was the false positive above, now fixed); the menu-return path just
described (this N8 never returned to its menu on RESET); and the error LED's double blink (no boot
failed or stuck while it was being sampled).
```

New:

```markdown
**RESET.** The cart link runs BlipToaster's reliable link and the N8 menu guard, the desktop's, from the same code
(`BoxCartLink`, [the box's design](../../docs/pico-box-framed-link-design.md)). When a RESET takes the N8 to its menu
mid-song (the lab's rig does now; on the 2026-10-30 rig it restarted the ROM in place, a 10 fps brightness trace
showing ~2 s blank, ~1.4 s of lost signal, then BlipToaster again), the guard notices: on a framed link the acks stop,
and 100 ms later the stalled window is held and the menu asked with `F0 2A 74 F7` (`*t` inside a SysEx), again each
second while the booting OS answers nothing; on a raw link the same probe goes out every 250 ms. The menu's `k` stops
forwarding, drops what the link held, and runs the boot again straight from the `*t` handshake: the staged ROM
installed with no upload (its saved CRC) and started, the boot's 4 s wait, and a fresh link probe, about 6 s from the
press to playing. A framed stream stops writing at the stall; a raw one can still leave the booting OS at ERROR:40
(notes landing about 330 to 400 ms after the press). `CMD_STATUS` turning 0 after a game stays the way back for a menu
reached while nothing streams, which the guard does not ask about.

**Not yet verified on hardware:** the "never enumerated" recovery firing for a real non-enumerating
device (the only trigger seen was the false positive above, now fixed); the guard's RESET path just described, and the
framed link on the box (`stress.sh` and the latency runs:
[spec/07](../../spec/07-remaining-work.md#the-boxs-framed-link-and-menu-guard-are-unproven-on-the-rig)); and the
error LED's double blink (no boot failed or stuck while it was being sampled).
```

Old (lines 388-389):

```markdown
(USB host, the plan's blocks, the probe, the boot, the sniff), `BoxCartLink.{hpp,cpp}` the `rp::n8::CartLink` over a
fixed 512-message ring (it drops and counts while no game runs and drains in FIFO writes of up to 192 bytes),
```

New:

```markdown
(USB host, the plan's blocks, the probe, the boot, the sniff), `BoxCartLink.{hpp,cpp}` the `rp::n8::CartLink` over a
fixed 512-message ring (it drops and counts while no game runs, drains in FIFO writes of up to 192 bytes, framed for
BlipToaster's reliable link once a connect's sync is acknowledged, and runs the menu guard: both `n8-wire`'s, the
desktop's own, held in the ring's `.bss` storage),
```

The sizes in the `pnpm device:box` paragraph become task 4's. Old (lines 446-448):

```markdown
`__wrap__Znwj` and `__wrap_malloc`), and prints the sizes (flash / data / bss: PIO-USB 199536 / 46120 / 120600,
`RP_NATIVE_USB` 199920 / 32084 / 114368, with 191960 and 192344 bytes of the flash running from RAM and 358704 and
338824 bytes of SRAM in all; E3's were 101200 / 23832 / 188020 and 101576 / 9836 / 181788 (312484 and 292636 bytes),
```

New (each `<...>` is the figure from `.superpowers/pbplan/box-size-after.log`'s `== pio:` line, then its `== native:`
line: `flash <F>, data <D>, bss <B> bytes; <R> of the flash runs from RAM; ... RAM <T> bytes in all`; write the
numbers, not the brackets):

```markdown
`__wrap__Znwj` and `__wrap_malloc`), and prints the sizes (flash / data / bss, with the reliable link and the menu
guard: PIO-USB <F> / <D> / <B>, `RP_NATIVE_USB` <F> / <D> / <B>, with <R> and <R> bytes of the flash running from RAM
and <T> and <T> bytes of SRAM in all; E4's were 199536 / 46120 / 120600 and 199920 / 32084 / 114368 (358704 and
338824 bytes); E3's were 101200 / 23832 / 188020 and 101576 / 9836 / 181788 (312484 and 292636 bytes),
```

- [ ] **Step 5: check the documents**

```
timeout -k 10 300 /workspaces/RetroPlug-main/cuttlefish/.superpowers/mgplan/guarded.sh pnpm -C /workspaces/RetroPlug-main/cuttlefish check:docs
grep -rn 'the-boxs-cart-stream-has-no-framing' /workspaces/RetroPlug-main/cuttlefish/spec /workspaces/RetroPlug-main/cuttlefish/pico /workspaces/RetroPlug-main/cuttlefish/README.md
grep -n '<F>\|<D>\|<B>\|<R>\|<T>' /workspaces/RetroPlug-main/cuttlefish/pico/n8-host/README.md
grep -rnP '\x{2014}' /workspaces/RetroPlug-main/cuttlefish/spec/07-remaining-work.md /workspaces/RetroPlug-main/cuttlefish/spec/13-subsystems.md /workspaces/RetroPlug-main/cuttlefish/spec/00-overview.md /workspaces/RetroPlug-main/cuttlefish/pico/n8-host/README.md
```

Expected: `check:docs` exits 0; the three greps print nothing (no link to the old anchor is left, every size is a
number, and no em dash was written). The design doc's own references to the entry's old title (its opening paragraph)
are the controller's to update with its as-built section.

- [ ] **Step 6: commit**

```
git -C /workspaces/RetroPlug-main/cuttlefish add spec/07-remaining-work.md spec/13-subsystems.md spec/00-overview.md \
  pico/n8-host/README.md
git -C /workspaces/RetroPlug-main/cuttlefish commit -F - <<'MSG'
docs: the box runs the framed link and the menu guard

spec/07's box entry shrinks to what is left: the rig's acceptance (stress.sh
framed with re-sends, a DMC soak in sync, three RESETs coming back playing with
no ERROR:40) and the latency runs against their bar, retitled. spec/13's
"Where the guard runs" adds the box, and its check; spec/00's n8-wire row
names the guard. The box's README says how a RESET comes back now, what the new
[stats] fields and the N8_BOX_LINK knob are, and the image's sizes with the
link.
MSG
```

---

## The design, section by section

| Design | Where |
|---|---|
| 1. What is known (NTSC loss, the menu's answer after a RESET, the box's unproven menu return, no heap, the shared CDC input, RAM) | Task 1 (no heap), task 3 (the shared input: `quietForRead`), task 4 (the menu return), task 6 (spec/07 keeps the measurements' pointer) |
| 2.1 `N8ReliableSender` allocates nothing, protocol untouched | Task 1 |
| 2.2 `N8MenuGuard`, `N8Link` its driver | Task 2 |
| 2.3 `BoxCartLink` gains the link and the guard, `Writer::read`, `.bss` storage, non-blocking probe, `atMenu`, `quietForRead`, portable | Task 3 |
| 2.4 `main.cpp`: `atMenu()` into the boot, the reads gated | Task 4 |
| 3. The flow: connect probe; each drain's order; the framed and the raw RESET; two silent stalls; long SysEx `unframed`; `[stats]` | Task 3 (the link and its cases), task 4 (the boot and `[stats]`) |
| 4. Edio reads and failures (`quietForRead`'s rule, `health_check`'s cadence and fallback, unplug, failed writes re-sent, the window give-up, a menu that stops answering, a refused plan; the note books) | Task 3 (`quietAt`, `flush`, `discard`), task 4 (the gating, `boot_rom`'s retries unchanged) |
| 5.1 the sender's no-allocation case, existing cases and goldens unchanged | Task 1, in the device-mode binary (see the decisions above) |
| 5.2 `N8MenuGuard`'s cases, a rule a case | Task 2, `N8MenuGuard.test.cpp` |
| 5.3 `N8Link`'s guard and job cases unedited | Task 2, step 6 |
| 5.4 the box's cases against FakeN8 | Task 3 (loss against the ROM model, see the decisions above) |
| 5.5 the firmware: images, trap, sizes before and after | Task 1 step 1, task 4 step 6 |
| 6. Latency: `timing --midi`, the comparisons' builds (`N8_BOX_LINK=0`, `N8_LAT_TRACE`) | Task 5 (the tool), task 4 (the knob, `bcread`); the runs are the controller's |
| 7. Plan, execution and acceptance; the documents | Task 6 (spec/07, spec/13, the README); the design's own as-built and execution sections are the controller's |
