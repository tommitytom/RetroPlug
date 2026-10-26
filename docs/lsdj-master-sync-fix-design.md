# LSDj master sync: telling a stopped LSDj by time, not by block

**Date:** 2026-10-26 · **Branch:** `feature/modularize` · **Status:** designed; not yet built.

The eighth defect B0 pinned (design section 2.5 of [modular-graph-phase-b0-design.md](modular-graph-phase-b0-design.md)):
in small host blocks, LSDj master sync decodes a stopped LSDj as clocks. By the user's decision it is fixed after B3a
([modular-graph-phase-b3a-design.md](modular-graph-phase-b3a-design.md)) and before B3b ports the decoder into a
protocol node, as its own change, re-recording exactly the parity cases it moves.

## 1. The defect

Master sync (lsdj-sync mode `masterSync`, the Arduinoboy firmware's Mode 2, SYNC=LSDj) turns what LSDj sends out of
its link port into host MIDI: one 0xF8 per byte while LSDj plays, a song-row NoteOn and 0xFA before the first, and
0xFC when it stops. A stopped LSDj does not go quiet: it floods its port. The decoder (`arduinoboyMasterSyncBlock`,
[lsdjArduinoboy.ts](../packages/retroplug/src/lsdjArduinoboy.ts)) tells the two apart by counting: a block holding
more than 16 bytes (`MASTER_SYNC_IDLE_FLOOD`) is the flood. How many bytes a block holds depends on how long the block
is, not on LSDj, so in 1-frame blocks every flood byte is a clock (10497 MIDI messages on `lsdj-modes`' master-sync
scene at 44.1 kHz, against 101 in 1024-frame blocks), and FL-style splits fall on both sides of the threshold, opening
and closing a run every few blocks.

## 2. What the bytes look like

Measured on that scene in 1-frame blocks, where a decoded clock's block index is its byte's frame:

| | Gap between bytes | Rate |
|---|---|---|
| A stopped LSDj's flood | 8 to 16 frames (at most 0.33 ms at 48 kHz) | about 5000 a second, steady |
| A playing LSDj | about 860 frames at 44.1 kHz (19.5 ms) | 51 a second at tempo 128 (24 PPQN) |
| A playing LSDj at its fastest tempo (295 BPM) | about 8.5 ms | 118 a second |

A gap of 2 ms separates them with room on both sides: six times the flood's widest gap, a quarter of the fastest
clock's. Two more facts from the same run:

- At 44.1 kHz a booted LSDj is quiet on the song screen (1.2 s to 6.0 s). It floods during boot, from 0.3 s to 1.2 s
  after two lone bytes, and after a stop. At 48 kHz it floods throughout and never plays: B0's sixth defect, a
  separate one, which stays pinned.
- The byte timeline barely depends on the block split. Mapped back to frames, `fl256`'s 81 clocks fall within about
  30 frames of the 1-frame run's 81: the core runs past its target by an instruction at a time, so a split moves its
  timing by tens of frames, never by a gap's worth.

The core already stamps every captured byte with its frame (`SameBoySystem::serialOutLog_`); the Game Boy node drops
the stamp and pushes every byte at frame 0.

## 3. Decisions taken

| Question | Decision |
|---|---|
| Where does the decoder get time from? | **The bytes' own frames**, carried from the core to the decoder. A TS-only density window over block lengths was rejected: a long block gives its bytes no positions, and the block where play stops mixes clocks and flood, which is exactly where the decision matters. |
| What is a clock? | **A byte with no other byte within 2 ms on either side.** Every other byte is flood. |
| When is a byte decided? | **Once its forward window is in the past**, so the newest byte waits up to 2 ms. Messages still go out at frame 0 of the block that decides them. |
| The firmware's own stop rule (every byte a clock, 0xFC after a silence)? | **Not used.** The emulated stopped LSDj floods rather than going quiet, so every flood byte would be a clock. A silence timeout can be added later if a quiet stop ever shows up. |
| 0xFC when the user switches mode mid-run? | **Not added.** Today does not send one either. |

## 4. The data path

**The Game Boy node** (`GameBoy::writeQueues`, [SystemNodes.cpp](../packages/native/src/host/graph/SystemNodes.cpp))
pushes each byte at the frame its core completed it, `serialOutLog_`'s `entry.first`, instead of at frame 0. A byte
the core completed past the block's end (it runs an instruction at a time, so it can overshoot) is stamped at the
block's last frame, so a block's messages always lie inside it. `buildGraph` wires `serialOut` only into the kernel
node and the MI.OUT decoder ignores frames, so nothing else the engine produces changes.

**The kernel** carries the frames to the role:

- `SerialOutEvent` ([dspKernel.ts](../packages/retroplug/src/dspKernel.ts)) gains `frame`: where the byte fell in the
  block that produced it, which is the previous block, since `serialOut` reaches the kernel over a feedback wire.
- `blockInput` ([kernelNode.ts](../packages/retroplug/src/script/kernelNode.ts)) copies each message's frame.
- `SystemCtx` gains `serialOutFrames: number[]`, parallel to `serialOut`, so the MI.OUT decoder keeps its plain
  `number[]` input and the kernel allocates nothing more per block (the fan-out pushes one more number per byte).

## 5. The decoder

`arduinoboyMasterSyncBlock(bytes, frames, block, state, emit)`, where `block` is the current `{ frames, sampleRate }`;
the role passes `c.serialOut`, `c.serialOutFrames` and `c.block`.

**The timeline.** Positions are frames from the current block's start. This call's bytes came from the previous
block, so a byte sits at `frame - lastFrames`, a negative position; `lastFrames` is the previous call's block length,
taken to equal the current one on the first call. At the end of a call every stored position shifts back by
`block.frames` and `lastFrames` becomes `block.frames`. Nothing in the decoder depends on how the frames were cut into
blocks.

**The rule.** `T` is `MASTER_SYNC_GAP_MS` (2 ms) at the block's sample rate, 88 frames at 44.1 kHz, with section 2's
figures beside the constant. Decisions are made in byte order:

- A byte with no other byte within `T` on either side is a **clock**. If no run is open it opens one first: the
  song-row NoteOn (the byte's value, on the configured channel, default 0) and 0xFA. Then its 0xF8.
- Any other byte is **flood** and is dropped. If a run is open, the first flood byte sends one 0xFC and closes it.

**The hold.** Every byte up to the current block's start is known, so the newest byte is decided once it lies more
than `T` before that start; until then it waits, at most one byte at a time, held as plain fields on the state (no
per-block allocation). In 1-frame blocks a clock goes out about 2 ms after its byte; in 1024-frame blocks only a byte
in the last `T` frames of its block goes out a block later than it does today.

**State across modes.** The kernel keeps a stage's state across config changes, so master sync's fields (the run, the
pending byte, the last byte's position, `lastFrames`) move into their own sub-object of the lsdj-sync state, and the
dispatcher drops it whenever the mode is not master sync. Coming back to master sync then starts clean rather than
deciding new bytes against a stale pending one. It also stops a run left open by a mode switch from swallowing the
next run's NoteOn and 0xFA, which happens today.

## 6. What moves

Only master sync's MIDI out. Its MIDI out feeds nothing back into a core, so every audio, SRAM and RAM digest stays as
recorded. Expected at 44.1 kHz, at every split: LSDj's two lone boot bytes open a short run that the boot flood closes
(NoteOn, 0xFA, two clocks, 0xFC); START opens the real run with its NoteOn and 0xFA and about 80 clocks; the stop sends
one 0xFC and nothing follows. About 90 messages in each split, against 10497 (`1`), 316 (`fl`), 894 (`fl256`) and 101
(1024-frame) today. At 48 kHz the flood never stops, so those cases drop from 42861 / 1000 / 3373 messages to the boot's
short run alone.

The re-record covers `blocks-tiny-lsdj-master` (six cases), `lsdj-modes`' `masterSync-44100` and `masterSync-48000`,
and the KernelNode C++ test's master-sync digest. It is accepted only if the baseline diff touches nothing but those
eight cases' `midiOut` digests, which a script checks, and the full parity suite then passes with the fixtures present
and `RP_FAIL_ON_SKIP=1`.

## 7. Tests

- **Unit** (`pnpm test`, [lsdj-arduinoboy.test.ts](../packages/retroplug/test/dsp/lsdj-arduinoboy.test.ts)). The
  master-sync cases are rewritten around a helper that takes absolute byte times and a block partition and feeds the
  decoder one block late, as the wire does: clocks 860 frames apart; a flood after a run (one 0xFC, no clocks); a flood
  before any run (nothing); a restart after a flood (a fresh NoteOn and 0xFA); the boot shape (lone bytes, then a
  flood: a short run the flood closes); the hold (a byte near a block's end decided in a later block); leaving master
  sync and coming back (a clean start). And the property the fix exists for: one timeline cut into 1-frame,
  `FL_RAGGED`, `FL_BUFFER`, 1024- and 8192-frame blocks decodes to the identical message sequence.
- **Kernel.** A kernel-level case shows `SerialOutEvent` frames reaching `ctx.serialOutFrames`;
  [kernel-node.test.ts](../packages/retroplug/test/script/kernel-node.test.ts) shows `blockInput` copying the frames.
- **Native.** The HostIo case "each byte at frame 0" becomes "each byte at the frame its core completed it"; the
  sync-trace case's expected lines group bytes by frame; the KernelNode rig's stand-in Game Boy gives its bytes frames.
- **End to end** ([lsdj-mastersync.test.ts](../packages/retroplug/test-native/lsdj-mastersync.test.ts)). A new case
  runs a real LSDj through `renderBlocks` in 1-frame, `fl` and 1024-frame blocks, through START, play, stop and START
  again, and holds every split to the same transport story: one NoteOn and 0xFA per start, one 0xFC per stop, no clock
  while stopped, clock counts within one of each other (a byte at a segment edge can land on either side, section 2).
  The parity cases never restart after a stop, so this is the only check that a restart after a flood works.

## 8. Docs

B0 design section 2.5 marks the eighth defect fixed with the new figures, and its sixth-defect paragraph drops the
sentence about 512-frame blocks opening a run that never closes (the boot flood now closes it; the cases keep their
1024-frame blocks). AGENTS.md counts four pinned defects and drops "the next due". `spec/06` drops this one from its
defect list, `spec/07` drops its item (B3b is next), the B3a design's section 10 and the main design's B3 paragraph
say it is done, and `spec/04` describes the gap rule. Comments follow in `dspRoles.ts`, `lsdjArduinoboy.ts`,
`SystemNodes.cpp` and the three LSDj test files.

## 9. Landing

1. This design, and its plan beside it.
2. The frames on the wire and through the kernel: no behaviour change, parity bit for bit.
3. The decoder, its tests, the re-record and the docs, in one commit, as an intentional change must be (AGENTS.md).

Each goes through review.
