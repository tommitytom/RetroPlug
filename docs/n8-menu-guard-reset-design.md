# The N8 menu guard after a console reset

Known-bugs batch 4's menu guard ([its design](known-bugs-batch-4-design.md) section 5) tells the N8 OS menu from a game
by the FPGA config block's map_idx, 255 at the menu. On the real cart that holds only for a menu reached by power-on: a
console RESET drops a game to the menu and leaves the block holding the game's mapper. This design replaces the signal
with the menu's own answer. spec/07's "The menu guard misses a console reset" is the entry it closes.

## 1. What the hardware showed (2026-11-18)

Measured on the lab's N8 Pro (PAL NES, BlipToaster, `retroplug-n8-hwtest menuguard`, `peek` and `menuprobe`):

- **A reset leaves map_idx alone, for good.** BlipToaster running: `45 65 02 81 01 00 00 8E 14` (map_idx 69). After
  RESET, with nothing streamed and the file browser on screen, the block polled every ~50 ms for 7 minutes never
  changed. A power-on menu reads `FF 00 00 00 00 00 00 80` (255). The FPGA explains it (edn8-pro-pub `everdrive.sv:66`,
  `var.sv`): a reset raises `map_rst_req`, which routes the OS mapper until the OS writes 255 to map_idx, and the OS
  does not; `map_rst_req` is not readable over USB.
- **So the guard never trips on a reset.** Streaming framed to BlipToaster, RESET at 44.1 s: acks stopped at once, the
  guard read "a game" every time, and about 35 KB of frames and 202 re-sends reached the menu in 21 s, which stopped on
  "ERROR:40 unexpected io status".
- **The menu answers `'*t'` with `'k'` after a reset** (map_idx still 69).
- **The menu skips any byte that is not `'*'`.** `F0 2A 74 F7` (the `'*t'` wrapped as a SysEx) is answered `'k'`, and
  a plain `'*t'` after it too; a lone `90 3C 40` leaves the menu answering `'k'`. ERROR:40 is a `'*'` (0x2A) followed by
  a byte that is not a command, which a framed stream carries sooner or later.
- **A running BlipToaster ignores `F0 2A 74 F7`**: three of them, then a control A4 that played and showed as usual.
- **Acks are `F5 <next> <last>`**, both 0-0x7F (`N8ReliableLink.hpp`), so a 0x6B can sit inside an ack; a 0x6B outside
  an `F5 x y` is not one.

## 2. The signal: the menu's `'k'`

The guard asks the menu, and only the menu answers:

- **The probe** is `F0 2A 74 F7`: a SysEx a MIDI game ignores, whose `'*t'` the menu reads. It is written between whole
  messages (the message-boundary tracker task 4 built), never inside one, and never inside a frame.
- **The answer** is a 0x6B on the back-channel outside an ack: the back-channel scan keeps the ack parser's state, and
  a `'k'` seen in state 0 (not inside `F5 x y`) is the menu's. A `'k'` trips the guard as the config read's "menu" did
  (`enterMenu`: drop, discard, `connected` 0).
- **Leaving the menu** is unchanged in shape: a game booted from the menu answers no probe, so after a trip the guard
  keeps probing on its cadence and leaves the menu when a probe goes unanswered for its window and the link probe (sync
  and ack) succeeds again, as today's leave-menu re-probe does.

**When it probes:**
- **Raw links:** every 250 ms (today's read cadence), between messages. 16 bytes a second, no read, no wait: the serial
  thread writes the probe as part of the stream and goes on.
- **Framed links:** only when the window stops moving (`progressOverdue`, today's hold trigger): a reset is an ack
  stall, so a probe goes out once the stall is seen, unframed, and the hold keeps new frames and re-sends back until
  either a `'k'` (the menu: trip) or an ack (a slow game: resume). The one-second bound stays: a stall that sees neither
  for a second probes again, and a framed game never sees the probe outside a stall.
- **At connect:** the seed is a probe answered within 100 ms, a plain `'*t'` wait (nothing is streaming yet).

**The config-block read goes**: the guard's cadence read, its forced read, its flush rules for that read and the
"trusted game second" all served map_idx, and map_idx tells nothing after a reset. spec/07's "what the periodic config
reads cost a running game" check goes with them.

## 3. The job check

`N8Host::probeMenu` (a submission's refusal: a job runs only at the menu) reads map_idx too, so a job submitted at a
menu reached by RESET is refused as "a game is running". It asks the menu instead: a plain `'*t'` on a quiet turn (a job
may wait), `'k'` meaning the menu. Its trip-only report to the guard (`reportMenuState(true)`) stays.

## 4. What is left exposed

Bytes written before the menu's `'k'` arrives still reach the menu: raw, up to one probe cadence (250 ms) of MIDI;
framed, the frames written before the stall is seen (the link's 100 ms timeout). Only a `'*'` followed by a byte the
menu takes as a command or rejects (ERROR:40) does harm; a stream without 0x2A is harmless. Closing that would need a
probe before every chunk. spec/07 keeps one hardware line for it.

## 5. Tests

- **FakeN8 models a reset menu**: `resetToMenu()` puts the fake at its menu with the config block left at the game's
  mapper, so every existing menu case can run in both kinds; a `'*'` followed by a non-command byte sets an `error40`
  flag; the menu answers `'*t'` through a wrapping `F0 .. F7`.
- **Red first** (`retroplug-n8-test`), each failing on today's guard: a framed stream to a reset menu trips and writes
  nothing after; a raw stream to a reset menu trips within one cadence; the fake never sees `error40` from a framed
  stream with 0x2A-bearing frames after the trip; a job submitted at a reset menu is accepted.
- **Green and unchanged**: a game never trips (a framed game's acks, a raw game's silence), a `'k'` inside an ack does
  not trip, and the cold-menu cases.
- **On the rig, by hand**: `retroplug-n8-hwtest menuguard` with a RESET press (framed BlipToaster, then BlipToaster held
  raw), expecting a trip and 0 bytes after; `menuprobe` for the menu's answers.

## 6. Plan, execution and acceptance

Tasks, test-first: 1 FakeN8's reset menu and the red cases; 2 the guard's probe (raw cadence, framed stall, connect
seed, the back-channel scan), removing the config read; 3 the job check; 4 docs (spec/13's guard paragraph, spec/07's
entry replaced by the residual line, batch 4's design pointing here). Process as batch 4: a validated plan, Flash-Next
implementers, Opus reviews (all three code tasks touch the serial thread), an Opus final review. Acceptance: the serial
kit, then the rig check above with a RESET press.

## 7. As built

The four tasks landed as planned, each tree identical to the one the plan's author validated; follow-up passes took the
reviews' findings and then the rig's. What differs from sections 2 to 6:

- **Section 2: the connect seed and the job check send the wrapped probe too**, not a plain `'*t'`: a reconnect can
  reach a game left in running status, and the job check runs mid-stream, where a bare `2A 74` plays as a stuck note.
  The guard leaves the menu after three unanswered probes in a row (the real menu answers in about 20 ms, 27 ms at
  worst, timed at a power-on menu); a framed link whose stall hears neither a `'k'` nor an ack for two probes drops its
  window and re-runs the link probe, so a ROM without the link booted after a quiet reset is not held for good. A
  framed stall resumes on an ack because BlipToaster acknowledges unframed bytes (`parse_asm.s`), which FakeN8 now
  models. The trusted second after a stall's ack ends once the window drains.
- **Section 3: the job's first step reads past late answers**: a menu that answered a guard probe late left a `'k'`
  for the job to take as its own status, which failed a dump and left an upload's file open on the card. The job also
  books its probe on the raw stream's running status, so a running-status note after it is not misread.
- **Found on the way and fixed:** a serial read on Windows reconfigured the port at every call, which raw links' every-
  turn back-channel read made hot; it now does so only when its timeout changes. A config read that timed out leaves
  no tail for the guard's scan.
- **Section 4 is narrower than designed for raw links.** On the cart a framed link streaming when the RESET comes is
  protected (a reset in a pause or inside the trusted second is not covered by that run, spec/07): a RESET mid-stream
  tripped the guard 1.1 s after the acks stopped (the N8 OS answers no probe for about the first second after a reset),
  with 0 stream bytes after the trip and a clean menu. An NTSC Famicom with the Famicom N8 Pro gave the same
  (2026-11-19: tripped 1107 ms after the acks stopped, 0 bytes after, clean menu, map_idx still the game's). A raw link
  is not: the guard tripped and wrote nothing after, yet the menu stood at ERROR:40, and it does so with no guard at
  all. `retroplug-n8-hwtest bootwindow` timed it: note bytes landing about 330 to 400 ms after a RESET (estimates, +-30
  ms) send the booting OS to ERROR:40; single notes at 200, 300 or 1000 ms, notes over 400 to 600 ms, 100 notes at an
  idle menu, and MIDI clock (`F8` every 20 ms, risa's sync) through the window are all harmless. A menu at ERROR:40
  still answers `'k'`, and a second RESET recovers it. Nothing on the host sees a reset on a raw link before the menu
  answers, so spec/13 states the limit and spec/07 keeps what is open.
- **Section 5's FakeN8** answers `'*t'` at ERROR:40 as the cart does, and can model the boot window and the OS's
  silence after a reset (off by default, so no case moved).
- **Left as it is:** a raw stream left inside a message holds off the raw probe (the plan review's optional D); the
  connect seed's probe is not in the write log, as it runs before the serial thread.

## 8. Execution

- Design 6a4b3fb72; plan c5cff9d9c, written and validated on the tree by an Opus agent (a DeepSeek run was stopped
  when the user moved the work to Claude models), replayed from its text to the identical tree; an Opus review was
  ready with fixes (five Important: the 3-miss leave, the unframed ack, the silent stall, the exposure text, the job
  probe's running status), the revision's re-check ready with three one-line fixes the controller applied.
- Tasks, each by a Sonnet implementer and checked byte-identical to the validated diff by a script: 6014cfe1f,
  38209e536, 7134d6d11, 0996992da. Opus code reviews: tasks 1 and 2 approved, task 3 asked for changes (the late `'k'`).
- Follow-ups, Sonnet, each Opus-reviewed: A 1dea305ad..49545b05a; B b4c7f673c, 63c5210ac (the final review's five
  fixes); c6e399fcb; C d995ed575, d77e61b0f, 70d85254b (the rig's findings). The Opus final review was ready.
- Acceptance: the serial kit at 49545b05a, 45 of 45 legs; the parallel kit at 63c5210ac, 47 legs exit 0 (BlipToaster
  57/57, NESVJ 22/22, SDL PipeWire 27/0/0) and `sdl:controller` on its own; on the cart, with a person pressing RESET:
  `menuguard` framed and raw, `bootwindow` at seven settings, and the second-RESET recovery. Nothing is pushed.
