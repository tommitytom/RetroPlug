# The Pico box's framed link and menu guard

The box ([pico/n8-host](../pico/n8-host/README.md)) writes MIDI to the cart FIFO raw: it predates BlipToaster's
reliable link and never moved onto it. On an NTSC console the DMC sample DMA's repeated FIFO read drops bytes, and a
console RESET mid-song streams into the booting N8 OS until it stops at ERROR:40. This design puts the box on the same
framed link and menu guard the desktop's `N8Link` runs, from one shared implementation, and makes a RESET come back
playing the staged ROM. spec/07's "The box's cart stream has no framing and no menu guard" is the entry it closes.

## 1. What is known

- **NTSC loss, measured** (the lab's Famicom, 2026-11-19, `retroplug-n8-hwtest timing idle 512 --tick-ch 3 --kick-ch 6
  --db -20`, through the desktop link, DMC kicks playing): framed, 512 of 512 ticks on the grid (p50 0.13 ms), 3 to 7
  lost bytes repaired per 45 s run; raw, a tick never sounded in 3 of 7 runs, 5 of 7 runs had notes off the grid (p50
  5.9 to 48.6 ms), and a DMC note stuck. The box's own `stress.sh` lost nothing, on the PAL console.
- **A RESET reaches the menu and keeps USB up** ([the menu guard's design](n8-menu-guard-reset-design.md) sections 1
  and 7): the menu answers `F0 2A 74 F7` with `'k'` from about 1 s after the press, on the PAL NES and the NTSC Famicom
  alike; raw notes landing about 330 to 400 ms after it leave ERROR:40.
- **The box's own menu-return path is unproven.** `health_check` (`main.cpp`) reboots the staged ROM when `CMD_STATUS`
  reads 0 after a game, but it runs only after 100 ms of MIDI quiet (a playing stream goes on into the OS), and whether
  `CMD_STATUS` turns 0 after a RESET was never seen: the README's 2026-10-30 RESET test found the ROM restarting in
  place, where this week's rig goes to the menu.
- **The firmware links no heap, no exceptions and no RTTI** (`pico/n8-host/CMakeLists.txt`, the link trap).
  `N8ReliableSender` holds its window in a `FixedQueue` (a `std::vector`) and takes a `std::function`, so it cannot
  link there as it is.
- **The back-channel and Edio replies share the CDC input.** `fifoWR` reads no reply, but `CMD_STATUS`
  (`health_check`), `memRD` (`sniff_report`) and the volume write's read-back do: an ack landing in front of a reply is
  read as its first bytes.
- **RAM is not the limit.** The pico2 image is 245 KB of text and 123 KB of `.bss` of 520 KB; the sender is about
  3.5 KB.

## 2. The parts

1. **`N8ReliableSender` (n8-wire) allocates nothing.** The window becomes a fixed ring held inline
   (`std::array<Frame, kWindow>`, about 3.4 KB). `send`, `sendMessage` and `poll` take the emitter as a template
   parameter in place of `Emit`'s `std::function`; the callers' lambdas are unchanged. The protocol is untouched: the
   emulator's `NesN8FifoRole`, `N8Link` and the parity goldens are byte-identical.
2. **`N8MenuGuard` (new, n8-wire, no I/O)**: the guard's decisions, moved out of `N8Link::menuHold`, with
   `BackChannelScan` beside them. Each turn it is given the time, the sender (null on a raw link), whether the stream is
   between messages, and what the scan heard (an ack, the menu's `'k'`); it answers whether to hold new frames and
   re-sends, whether to write the probe now, whether to probe the link again, and whether the menu state changed. It
   also answers `probeAwaited(now)`, which a quiet-for-read test needs. Its constants stay `N8Link`'s: 250 ms raw
   cadence, 100 ms for an answer, 1 s hold and trusted second, 3 misses to leave the menu, 2 silent stalls to re-probe.
   `N8Link` becomes its driver (the ring, the tracker, the writes, the logs) and behaves exactly as before.
3. **`BoxCartLink` gains the link and the guard.** `Writer` gets a `read` callback, the back-channel without waiting
   (`usb.read(..., 0)`: one USB pump). The sender and the guard live in the link's `.bss` `Storage`. A connect probes the
   link without blocking the loop, and the drain then writes framed or raw. It answers `atMenu()` and `quietForRead()`.
   It stays portable, so the desktop's tests build it.
4. **`main.cpp`** turns `cart.atMenu()` into the boot: forwarding stops, and the boot path runs again knowing the menu
   has answered, so it skips "`CMD_STATUS` non-zero, a game is running" and goes to the `'*t'` handshake, then the
   staged ROM (or a pending MIDI pick). `health_check`, `sniff_report` and the volume write read over Edio only when
   `cart.quietForRead()` allows.

## 3. The flow

**At connect** (`setConnected(true)`: after a boot, or at a mount with a game already running), the link writes
`F4 00` and listens for up to 100 ms, across main-loop passes, for an `F5 xx` ack; what is pushed meanwhile waits in
the ring. An ack makes the link framed; silence leaves it raw. The volume is then written as today.

**Each `drain()`**, in the desktop's order:

1. The back-channel is read: its bytes go to the sender, and the scan's acks and `'k'` to the guard.
2. The guard's turn. A re-probe drops the ring and the window, counted, and runs the connect's probe again.
3. A probe that is due goes into the write. Every write boundary is a message boundary on the box, except inside a
   raw SysEx longer than one write (192 bytes), where the probe waits for the next boundary.
4. Unless the guard holds: the sender's re-sends, then the ring's messages framed into writes of up to 192 bytes.
   Frames are never split across writes (a frame is its message and 3 bytes). `onSent` marks a write's frames once it
   is written, and also when the write failed, so the 100 ms timeout re-sends them rather than leaving them unsent.
5. While the guard holds, messages stay in the ring: they go out once an ack says it was the game, and are dropped if
   the menu answers. A hold that fills the 512-message ring refuses pushes, counted `full`.

**A RESET on a framed link (BlipToaster):** the acks stop; 100 ms later the oldest frame is overdue, so the guard holds
and probes, and probes again each second while the booting OS answers nothing (about 1 s). The menu's `'k'` sets
`atMenu()`: the ring and the window go, counted `notReady`, and the main loop runs the boot: `'*t'`, install with no
upload (the saved CRC), `'*s'`, the boot's 4 s wait, and a framed connect. That is about 6 s from the press to playing,
most of it the boot's existing wait for the game; shortening that wait is outside this design.

**A RESET on a raw link (a ROM without the link):** the probe goes out every 250 ms between writes, and its `'k'` runs
the same boot. The raw limit stands: notes landing about 330 to 400 ms after the RESET can still leave ERROR:40.

**Two stall probes that hear nothing** are a game without the link (started after a reset the guard never saw): the
window goes and the link is probed again, which drops it to raw.

**A SysEx longer than a frame carries** (40 bytes; BlipToaster's longest is 37) is dropped on a framed link, as the
sender does, and counted `unframed`. `[stats]` gains `link=framed|raw`, `resends`, `resent` and `unframed`.

## 4. Edio reads and failures

**`quietForRead()`** is the desktop's rule: no guard probe awaiting its answer, the scan not inside an ack, and either
a raw link between messages, nothing outstanding, or a cart silent for 250 ms since the oldest frame first went out.
`health_check`, `sniff_report` and the volume write wait for it, skipping a pass that is not quiet. `health_check`'s
1 s cadence and its link-dead recovery (3 failed reads) stay, and so does its `CMD_STATUS`-0 menu return, as the
fallback for a menu reached while nothing streams (the guard asks only while forwarding).

**Failures:**
- An unplug or power cut takes the existing unmount path; the disconnect resets the sender and the guard, and a read
  on a down link returns nothing.
- A failed write's frames are re-sent on the timeout (section 3, step 4); 5 failed writes in a row still reboot the box
  (`write_fail_streak`).
- A window that fills to 48 unanswered frames gives up and re-syncs, as the sender does today.
- A menu that answered `'k'` and then stops answering meets the boot's 20 `'*t'` tries, then the stuck-menu error LED
  and its 20 s retry.
- A refused plan, or a node that faulted, forwards nothing, so the link never connects.

**Unchanged:** the box's `F0 7D 50` commands, the ROM pick, the watchdog, the 192-byte batches, and the note books:
a held message is taken, and one the menu's `'k'` drops is booked as a refused note-off, as a disconnect's are.

## 5. Tests

Red before green, every run through `.superpowers/mgplan/guarded.sh`.

1. **The sender** (`N8ReliableLink.test.cpp`): every existing case unedited; a new case shows a full window and its
   re-sends allocate nothing (the counting allocator `BoxHost.test.cpp` uses). The emulator's parity and kit goldens
   are unchanged.
2. **`N8MenuGuard`** (a new test file), a case a rule: the raw cadence only between messages; a stall's hold and probe;
   an ack's trusted second, and a drained window ending it; two silent stalls asking for a re-probe; `'k'` entering the
   menu and three misses leaving it; `probeAwaited`.
3. **`N8Link`**: its FakeN8 guard and job cases (`N8CartLink`, `N8Host`, `N8CartJobs`) pass unedited, the proof the
   move changed nothing.
4. **The box** (`BoxHost.test.cpp`): `BoxCartLink` against FakeN8, the cart model the desktop guard is tested on,
   through a `Writer` over its port. The connect probe choosing framed for BlipToaster and raw for a ROM without the
   link; FakeN8 dropping bytes, every message arriving in order; a framed RESET (nothing but probes written after the
   stall, `atMenu()` on the `'k'`, the ring and the window counted); a raw RESET by the 250 ms probes; two silent stalls
   falling back to raw; `quietForRead()` false with a probe awaited, inside an ack, and with fresh frames outstanding;
   a message pushed in a pass written in that pass's drain, framed or raw (the link adds no pass of delay); and "the
   box's pass allocates nothing" extended to the framed path.
5. **The firmware**: the pico2 image and `PICO_BOARD=pico` build, the link trap holds, and `.text` / `.bss` before and
   after are recorded.

## 6. Latency

The box is a performance path, so the link's cost in latency is measured, not assumed. What can add it: three more
bytes a message (a USB write costs about the same whatever it carries), the back-channel read each pass, the guard's
hold, and a lost frame's re-send.

**The tool.** `retroplug-n8-hwtest timing` gains `--midi <port>`: the same arrangement, written through ALSA rawmidi to
the CH345 cable into the box's MIDI jack instead of through `N8Link`, each write stamped with `CLOCK_MONOTONIC` as the
L6 capture is, so the latency runs from the MIDI write to the sound, as it does today from the USB write. The port is
the CH345's whole name, refused otherwise, and listed before anything is opened. No Python.

**The comparisons**, each in `idle` and `busy`, on the Famicom:
- the box framed against the box raw (`-DN8_BOX_LINK=0`, a build that skips the link probe as
  `N8Link::setLinkAllowed(false)` does), `--no-kick`: the framing's own cost, with no DMC loss to repair;
- the same with the DMC kicks: what the repair costs and saves (missed hits, grid error, stuck notes);
- the box framed against the desktop's framed link to the same ROM (the N8 moved back to the container between the
  two): what the box's path adds;
- the box's `N8_LAT_TRACE` build: each part of a pass (the back-channel read, the guard, the framing, the write)
  against the raw build's, and the `[stats]` `lat` (a message off the UART to the N8's USB acknowledgement) under
  `stress.sh`'s soak rates.

**The bar**, fixed before the runs: without DMC, framed within 1 ms of raw at p50 and 2 ms at p95, and no worse grid
error; with DMC, no missed hit and no stuck note in 5 runs, and the tail (p95, max) reported beside the box's re-send
count. A miss is a finding to investigate before acceptance, not a number to note.

## 7. Plan, execution and acceptance

The plan is written from this design and validated on the tree; Opus reviews each task and Sonnet implements, then
the follow-ups, then the serial and parallel tip-verify kits.

**Hardware acceptance** runs once the rig is rewired for the box (the N8 in the Pico's socket, the debug probe, the
CH345 in its jack, the Famicom): `stress.sh` passes with nothing lost and `[stats]` showing `link=framed` and some
re-sends; a soak with the DMC kicks keeps the sniffer in sync; three RESETs mid-song each come back playing (an audio
capture after each) and no video frame shows ERROR:40, with the press-to-sound time measured; and section 6's latency
runs meet their bar. Until it runs, spec/07's box entry keeps only the hardware acceptance; it goes once that passes.

**Docs:** spec/13's "Where the guard runs" adds the box; the box README's RESET section and stats line; spec/07 as
above; and this design gains an as-built and an execution section.

## 8. As built

The six tasks landed as planned; follow-up passes took the reviews' findings. What differs from sections 2 to 6:

- **Section 2: the sender's storage is raw `.bss` made by placement new**, so its window is not 3.5 KB of `.data`.
  The guard's decisions are `N8MenuGuard` (with its `Scan` and `Turn`), and `N8Link` keeps `kMenuProbe`,
  `kMenuAnswer` and `BackChannelScan` as aliases; `N8Link::serialLoop` now resets the guard beside `atMenu_`, so the
  two copies of the menu state agree (nothing could see the difference before). The box frames and guards only when
  its `Writer` has `read` and its `Clock` the 64-bit `monotonicUs`, which kept every existing box case unedited.
- **Section 3: the framed drain stops framing one frame short of the window** (`outstanding() >= kWindow - 1`), and
  the rest waits in the ring for the next drain. Reading acks only at a drain's start, a burst of 49 or more messages
  (the ring after a 1 s hold) would otherwise have made the sender give its window up and re-sync, and a DMC loss in it
  would have lost every frame up to the re-sync for good, note-offs too. So the sender's 48-frame give-up cannot
  happen on the box.
- **Section 3: a failed write's frames are re-sent on the guard's stall, not the 100 ms timeout.** On a silent cart
  the guard's stall probe comes first, and BlipToaster's ack to it (`F5 <next> <next>`) names the missing frame, which
  sets off the re-send; the desktop link does the same.
- **Section 3: a raw link that hears an ack probes the link again**, once per connect, and never with a split SysEx
  half written. A BlipToaster that reads the connect's `F4 00` after the 100 ms window is in its sticky link mode and
  drops unframed messages; the late ack is how the box finds out. The desktop's `N8Link` has the same gap and does not
  re-probe: spec/07 lists it.
- **Section 3: no probe is written in the drain that hears the menu's `'k'`**: the box leaves the link at once, and a
  fresh probe's answer could land after the boot's 20 ms flush and shift its replies.
- **Section 4: no boot runs with the link connected.** `health_check`'s menu return and the boot block both disconnect
  first, so a framed link is never left synced to the game that went. Once the menu has answered, a boot whose 20
  `'*t'` fail takes the stuck branch rather than trusting `CMD_STATUS`, and the retry goes straight to `'*t'`. A
  failed boot retries every 20 s, at most three times in a row, then stops with the error LED until a power-cycle or a
  fresh mount (a persistent upload failure would otherwise rewrite the SD card every 20 s for ever).
- **Section 4: the quiet test** also counts a pending or waiting link probe as not quiet, and treats a scan left inside
  an ack as between acks after 250 ms of back-channel silence (a cart that stopped mid-ack held it false for good).
  `n8_probe` flushes the input before its `CMD_STATUS`. A failed raw write stops that drain.
- **Section 4: `[stats]`** prints `link=off|raw|framed`, `resends`, `resent` and `unframed`, and `writes` less the
  probes written alone (`probeOnly`), so an idle raw link's 250 ms probe does not print a line every second.
- **Section 5: the byte-loss case runs on `LinkRomModel`, not FakeN8**, which models only intact traffic; its loss is
  a local LCG. The box's cases are 20 `[box-link]` ones.
- **Section 5's `PICO_BOARD=pico` build does not link, before or after** ("RAM overflowed by 106472 bytes" at
  7fa0d1e17: the copy-to-RAM image needs about 350 KB of SRAM), so only the two pico2 variants are sized. PIO-USB went
  from flash 199752 / data 46120 / bss 120600 to 206608 / 46424 / 124304 bytes; native from 200136 / 32148 / 114368 to
  206912 / 32452 / 118072.
- **Section 6: `timing --midi`** stamps each write just before `snd_rawmidi_write`, as `N8Link` stamps its USB
  writes, and its figure includes the CH345's USB-to-serial delay and the 31250-baud wire (about 0.96 ms a message):
  on 4 of 16 steps the tick waits about 1 ms behind a kick, so a box against desktop comparison runs `--no-kick`. It
  lists outputs before opening one and refuses a name without "CH345", one no output has exactly, or one two share.
- **Left as it is:** a game started by hand during a failed boot's 20 s wait gets no MIDI until the retry finds it
  running; an ack split across the 100 ms probe window's end is missed, and the next ack heals it.

## 9. Execution

- Design 7fa0d1e17; plan f85879ac9, written by an Opus agent without a compiler (its 65 edit blocks replayed against
  the tree), then an Opus review (one Important, the window give-up, and eleven Minor) and its fixes.
- Tasks, each by a Sonnet implementer, test first, each Opus-reviewed: 47fd153b8, 221630027, 81537b79a (+0ebeaa671:
  the failed-write case the review asked for), b7b0ec0a4 (+f5031f6d5: the review's two Important, a boot with the link
  connected and the stuck menu after its answer), d6d1b94c3 (+297fb15b1: what the latency includes), 35436e675.
- Follow-ups, Sonnet, Opus-reviewed: A 3137baf27, b2f3dc7c4 (+65fa13efa: no late-ack re-probe inside a split SysEx);
  B 0e01d2f25, 29003aada (the final review's items). The Opus final review was ready.
- Acceptance, headless: the serial kit at 29003aada, 45 of 45 legs exit 0; the parallel kit at 65fa13efa, every leg but
  `tsandevmode`, whose script-pack CPU-time deadline case failed under the parallel lanes' load (75 ms against 62.5 ms)
  and passed alone and in the serial run. `pnpm device:box` builds both variants under the link trap, and the
  `N8_LAT_TRACE=1` and `N8_BOX_LINK=0` builds link.
- **Not yet run: the hardware acceptance** (section 7) and the latency runs (section 6), which need the rig rewired for
  the box. Beside section 7's list, the final review adds: whether `CMD_STATUS` reads 0 after an idle RESET, a RESET on
  the `N8_BOX_LINK=0` build, `faults.sh` (the boot, retry cap and error LED have no headless test), whether the
  first note after a reboot is lost to BlipToaster's first-message drop, and the expansion volume written again after
  each reboot. spec/07 keeps the entry until they pass. Nothing is pushed.
