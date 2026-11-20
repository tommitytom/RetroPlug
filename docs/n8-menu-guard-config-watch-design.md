# The N8 menu guard at the menu: watch the config block, write nothing

The menu guard ([the reset design](n8-menu-guard-reset-design.md), [spec/13](../spec/13-subsystems.md#the-everdrive-n8-bridge))
asks the N8 OS menu `'*t'` and takes its `'k'` for "at the menu". Once there, it kept asking every 250 ms, and left
the menu after three probes went unanswered (a game started from the cart's own browser). On the lab's cart that
probing breaks the very start it waits for. This design stops all writes at the menu and leaves it on what the FPGA
config block shows when the OS starts a game.

## 1. What the hardware showed (2026-11-20)

NTSC Famicom, N8 Pro on the PC, `retroplug-n8-hwtest menuwatch`, `cfgwatch` and `pausereset` (the session's logs are in
the worktree's `.superpowers/menucheck/`).

- **An idle menu is quiet and quick.** With nothing written, 30 s of back-channel held no byte. Probed every 250 ms
  for 60 s, it answered 240 of 240, p50 7.4 ms, worst 12.6 ms, and never said `'k'` unasked. Browsing with the pad for a
  minute, and opening a folder and leaving it five times, gave one answer at 101 ms and nothing else.
- **A game started from the browser under the probes ends at ERROR:40**, "unexpected io status", 2 of 2 (Akumajou
  Densetsu, VRC6), each time with exactly one probe swallowed at Start Game. The same start with nothing written
  started the game (1 of 1).
- **Why** (edn8-pro-pub `edio/everdrive.c`, `ed_check_status`): for each of its operations the OS sends `CMD_STATUS` to
  the cart's MCU and reads the two-byte reply from the FIFO the host writes into, expecting `0xA5 xx`. A probe's bytes
  waiting there are read as the status, and the OS stops with `ERR_UNXP_STAT` (0x40). Any byte the host writes while
  the OS is mid-operation can do it; a game start is a long run of such operations, a folder open a short one. This is
  also the likely cause of the raw-link limit (a raw note stream across a RESET leaving ERROR:40, or ERROR:48 on the
  box), which spec/13 put down to a `'*'` followed by a byte the menu takes for no command.
- **Reading the config block does not disturb the OS.** `memRD 0x1800020` (16 bytes) is the MCU's to answer and never
  reaches the console's FIFO: read every 50 ms through two starts, both games started clean.
- **Every start from the browser clears the block first.** A power-on menu reads `FF 00 00 00 00 00 00 80`; Start Game
  read all zeros, then about 100 ms later the game's config (`18 65 04 8B 00 00 00 9E 14`, mapper 24). A RESET left the
  block alone (as [the reset design](n8-menu-guard-reset-design.md) section 1 found), and starting the SAME game again
  went through all zeros to the identical config. So a start is visible even when it ends where it began.
- **A RESET between framed notes stays clean**, 6 of 6 (`pausereset`, BlipToaster): gaps of 1000 ms (2), 400 ms (3,
  the first unacknowledged frame within ~370 ms of the RESET and the stall probe 100 ms after it) and 100 ms with 10%
  test loss (1). Two or three frames and the stall's probes reaching the booting OS did not break it, and the next
  `n8-load` worked each time. The framed side of the guard stays as it is.

## 2. The rule at the menu

`N8MenuGuard` (n8-wire, I/O-free, shared by `N8Link` and the Pico box) gains a read request:

- **`Turn::readConfig`**: at the menu, every `kConfigNs` (20 ms), the turn asks for the config block instead of writing
  the probe. The driver reads the 16 bytes and hands them back with `config(now, block)`. A read that failed is not
  handed back: the guard asks again on its cadence, and nothing else changes.
- **The first block after entering the menu is the reference.** The entry is the `'k'` (or `report(true)`, a
  submission's probe), unchanged.
- **Leaving:** the guard waits for the block to read the same non-zero value for `kSettleNs` (500 ms), an all-zero
  block restarting the wait, and answers `leftMenu` once that settled block differs from the reference, or once an
  all-zero block was read since entering (what makes the same game's restart visible). One garbled read (a stray byte
  in the reply) followed by the unchanged block is therefore no start. The first read waits out the answer window
  (`kAnswerNs`) after the entering turn, as a late `'k'` from an earlier probe could still land in its reply. The wait keeps the driver's next write, the link
  probe's sync that `leftMenu` sets off, clear of the OS's last operations before it jumps to the game.
- **No write at the menu, ever**: `writeProbe` is never set there, and `kLeaveMisses` and the menu's miss count go. A
  start the 20 ms cadence misses (a clear shorter than a poll, never seen) leaves the guard parked at the menu: pushes
  drop and count, Settings > N8 says "At the N8 menu (not streaming)", and a reconnect probes afresh. A miss costs a
  click, never an ERROR:40.

Unchanged: the raw link's probe every 250 ms while it streams to a game (a game ignores it), the framed stall probe
and its trusted second, the connect's seed probe and a submission's probe (one write each, needed to tell a menu from a
game after a RESET, where the block cannot), and the scan that tells the menu's `'k'` from an ack.

## 3. The drivers

- **`N8Link`** (desktop): when a turn asks `readConfig`, the serial thread reads the block on a turn that is quiet for a
  read (`quietForRead`: no probe of the guard's awaiting its answer, so a late `'k'` from the probe that entered the
  menu cannot land inside the reply) and hands it to the guard. A failed read is logged at most once a second. Jobs
  still run at the menu, pausing the stream as today.
- **The Pico box** (`BoxCartLink`): unchanged in behaviour. Its main loop disconnects the cart link in the same pass the
  guard enters the menu, and boots the staged ROM, so its guard never turns at the menu and never sets `readConfig`.
  The box's `leftMenu` branch, now unreachable (a guard that never turns at the menu never leaves it), is removed.

## 4. Tests

- **`N8MenuGuard.test.cpp`**: no `writeProbe` at the menu, `readConfig` every 20 ms; a different game's block leaves
  after 500 ms steady; the same game through all zeros to the identical block leaves; a block that never changes stays;
  a block still changing, or back to zeros, does not leave early; a read never handed back changes nothing; the
  three-miss cases go.
- **`FakeN8` learns a browser start**: the block reads zero for a while, then the game's; a FIFO byte that arrives during
  the start puts the fake at ERROR:40.
- **`N8Link` on that fake**: at the menu, a browser start ends clean (no byte written during it) and the link comes back,
  framed for a game with the link. The case fails on the probing guard.

## 5. Hardware acceptance

- **The gate, first**: `cfgwatch` at 10 ms through browser starts of three ROMs of different sizes. The all-zero block
  must last well past 20 ms each time; if it does not, the cadence changes before anything relies on it.
- **The link**: `retroplug-n8-hwtest menuguard` gains a mode that streams on through the menu and logs every change of
  `atMenu`, `reliable` and `connected`. With it running, five starts each from the browser: BlipToaster must come back
  framed and play, Akumajou Densetsu must start clean and go raw; no ERROR.

## 6. Docs

spec/13's guard section loses its at-menu probing and three-miss leave for this rule, and its raw-link limit takes the
corrected cause. spec/07 loses what section 1 settled: the idle menu's silence and browsing, the busy menu's false leave
and the bytes it lets in (no leave by misses remains), the paused framed RESET and the trusted second, and raw streams
to other ROMs (the cause is the OS's, whatever the ROM). It keeps a job submitted right after a power-cycle or a RESET,
the raw probe's cost to a running game (`timing busy --raw --guard`), and this design's hardware acceptance until it
passes.
