# LSDj master sync: listening to LSDj instead of clocking it

**Date:** 2026-10-26 · **Branch:** `feature/modularize` · **Status:** designed (revised after the first prototype,
section 2); not yet built.

LSDj master sync (lsdj-sync mode `masterSync`, the Arduinoboy firmware's Mode 2, SYNC=LSDj) turns what LSDj sends
out of its link port into host MIDI: one 0xF8 per byte while LSDj plays, a song-row NoteOn and 0xFA before the
first, and 0xFC when it stops. B0 pinned two defects in it (design section 2.5 of
[modular-graph-phase-b0-design.md](modular-graph-phase-b0-design.md)): the **sixth**, master sync silent at 48 kHz and
above while serial-out capture is armed, and the **eighth**, a stopped LSDj decoded as clocks in small host blocks.
By the user's decision the eighth is fixed after B3a ([modular-graph-phase-b3a-design.md](modular-graph-phase-b3a-design.md))
and before B3b ports the decoder into a protocol node, as its own change. Building it found that the two share a
cause, which also stops a stopped LSDj from ever playing again, so this change fixes all three.

## 1. What was pinned

The decoder (`arduinoboyMasterSyncBlock`, [lsdjArduinoboy.ts](../packages/retroplug/src/lsdjArduinoboy.ts)) took a
block holding more than 16 bytes (`MASTER_SYNC_IDLE_FLOOD`) to be a stopped LSDj's "idle flood", dropped it, and
sent one 0xFC. How many bytes a block holds depends on how long the block is, not on LSDj: in 1-frame blocks every
flood byte decoded as a clock (10497 MIDI messages on `lsdj-modes`' master-sync scene at 44.1 kHz, against 101 in
1024-frame blocks). And at 48 kHz, master sync was silent: LSDj never played, and its MIDI out was the flood alone.

## 2. What building found

The first design (c8135ed4) kept the flood as LSDj's behaviour and told it from clocks by the time between bytes. Its
prototype decoded every split identically, but an end-to-end check that restarts LSDj after a stop found that a
stopped LSDj in master sync never plays again: silent through two more START taps over six seconds, where the same
taps restart it with the mode `off`. The cause is the emulator, not LSDj:

- With capture armed, `SameBoySystem::writeAudioSample` clocks any transfer waiting on an EXTERNAL clock (SC=0x80),
  one bit per audio frame, and captures each bit. That is MI.OUT's path: there LSDj (the Arduinoboy build) is the
  serial slave and the Arduinoboy clocks it. Master sync needs none of it: the Arduinoboy only listens to the bytes
  LSDj clocks itself (SC=0x81), which `serialStart` already captures.
- A stopped stock LSDj in SYNC=LSDj waits on an external clock, for another master. We clock it, so every bit we
  clock is captured (one bit a frame is 5512 bytes a second at 44.1 kHz: the "flood", 8 to 16 frames a byte), and
  LSDj takes our idle 0xFF bytes for a master's clock and behaves as a slave, which ignores START: no restart.
- At 48 kHz it falls into that state at boot and never leaves it: the sixth defect. At 44.1 kHz it leaves it during
  boot (1.2 s in) and falls back into it at the first stop.

With that clocking switched off for master sync (a scratch run): no flood and no boot noise at either rate, LSDj
plays at 44.1 and 48 kHz alike, START restarts it after a stop at both, and a stopped LSDj goes quiet, as it does on
the hardware. Nothing then sends 0xFC, so the decoder takes the firmware's own stop rule: a silence.

A lead, not fixed here: MI.OUT's path clocks one bit per audio frame, so MI.OUT's bit rate is the host's sample rate,
which is a likely cause of B0's seventh defect (MI.OUT's decoded notes depend on the sample rate).

## 3. Decisions taken

| Question | Decision |
|---|---|
| Fix the decoder, or the cause? | **The cause.** Master sync captures only what LSDj clocks itself and never clocks its port. The first design's gap rule was a detector for our own artefact; it is dropped. |
| How does the core know which? | **Capture becomes a mode.** `listen`: capture the bits the Game Boy clocks itself. `drive`: that, and clock a transfer waiting on an external clock, capturing its bits too (today's behaviour, MI.OUT's). Absent is off. The store picks `drive` for `midiOut` and `listen` for `masterSync`. |
| The stop rule | **The firmware's: a silence.** Every byte is a clock, sent in the block it reaches; 0xFC goes out once no byte has come for 200 ms (`MASTER_SYNC_STOP_MS`), about three times the gap between clocks at LSDj's slowest tempo (40 BPM at 24 PPQN: 62.5 ms). |
| Measured how? | **On the bytes' own frames** (built first, section 9): the Game Boy's `serialOut` stamps each byte at the frame its core completed it, and the kernel hands the frames to the role beside the bytes. So the silence is exact and the 0xFC lands in the same place at any block split. |
| State across modes | **Dropped when the mode is not master sync**, so coming back starts clean rather than from a stale run or a stale last byte. |
| 0xFC when the user switches mode mid-run? | **Not added.** Today does not send one either. |

## 4. The core: capture modes

`SameBoySystem` replaces its `serialOutEnabled_` flag with `SerialOutCapture serialOutCapture_` (`Off`, `Listen`,
`Drive`), set by `setSerialOutCapture(SerialOutCapture)`:

- `serialStart`, the internal-clock path, captures under `Listen` and `Drive` (it captured whenever the flag was on).
- `writeAudioSample`'s external-clock path runs under `Drive` only.

On the wire the mode is a string, `"listen"` or `"drive"`, absent for off, parsed in one place on the native side:

- The build spec's `serialOutCapture` (`ConstructSpec` in [backend.ts](../packages/retroplug/src/backend.ts),
  `BackendConstructSpec` in [BackendTypes.hpp](../packages/native/src/host/rpc/BackendTypes.hpp)) goes from a
  boolean to the mode.
- `setSerialOutCapture(id, mode)` takes the mode or null ([EngineRpcService](../packages/native/src/host/rpc/EngineRpcService.cpp));
  an unknown string is refused. It rides `ConfigField::SerialOutCapture` as 0 (off), 1 (`listen`) or 2 (`drive`).
- `serialOutCaptureFor` ([systemsStore.ts](../packages/retroplug/src/systemsStore.ts)) returns `"drive"` for
  `midiOut`, `"listen"` for `masterSync`, and null otherwise; the build hook and the live re-arm both send it.

## 5. The decoder

`arduinoboyMasterSyncBlock(bytes, frames, block, state, emit)`, where `block` is the current `{ frames, sampleRate }`;
the role passes `c.serialOut`, `c.serialOutFrames` and `c.block`.

- **Every byte is a clock**, sent at once: if no run is open it first sends the song-row NoteOn (the byte's value, on
  the configured channel, default 0) and 0xFA.
- **The stop.** Positions are frames from the current block's start. This call's bytes came from the previous block,
  so a byte sits at `frame - lastFrames`; `lastFrames` is the previous call's block length, taken to equal the current
  one on the first call. Every byte before the current block's start is then known, so once a run is open and its
  newest byte lies more than `MASTER_SYNC_STOP_MS` before that start, the decoder sends 0xFC and closes the run. At
  the end of a call the stored position shifts back by `block.frames`.
- **State** is the run, the newest byte's position and `lastFrames`, plain fields (no per-block allocation), in a
  `masterSync` sub-object of the lsdj-sync stage's state that the dispatcher drops whenever the mode is not master
  sync.

## 6. What moves

Only the master-sync cases; MI.OUT keeps `drive`, so its cases (`lsdj-modes`' `midiOut`, `blocks-tiny-lsdj`'s) stay
bit for bit. The eight master-sync cases move in audio, RAM and MIDI out: `blocks-tiny-lsdj-master`'s six and
`lsdj-modes`' `masterSync-44100` and `masterSync-48000`. At 44.1 kHz LSDj is no longer clocked during boot, so its
audio moves and the boot sends nothing; the run sends its NoteOn, 0xFA and clocks, and the stop one 0xFC 200 ms after
LSDj's last byte. The 48 kHz cases become audible, their `audible` lanes `[0, 1]` instead of `[]`, with the same story.
The C++ KernelNode test's master-sync input, which modelled the flood, becomes a run, a silence and a run, and its
digest is re-recorded.

The re-record is accepted only if a script finds no other case's digest moved, and the full parity suite then passes
with the fixtures present and `RP_FAIL_ON_SKIP=1`.

## 7. Tests

- **Unit** (`pnpm test`, [lsdj-arduinoboy.test.ts](../packages/retroplug/test/dsp/lsdj-arduinoboy.test.ts)). Around a
  helper that takes absolute byte times and a block partition and feeds the decoder one block late, as the wire does:
  every byte a clock, the first opening the run; a silence of 200 ms closing it with one 0xFC, and a silence just
  short of it not; a byte after the stop opening a fresh run; the 0xFC landing in the first block that starts more
  than 200 ms after the last byte; the silence in time, not frames (one length is a stop at one rate and not at
  another); leaving master sync and coming back starting clean. And one timeline cut into 1-frame, `FL_RAGGED`,
  `FL_BUFFER`, 1024- and 8192-frame blocks decoding to the identical message sequence.
- **Kernel.** A kernel-level case shows `SerialOutEvent` frames reaching `ctx.serialOutFrames`;
  [kernel-node.test.ts](../packages/retroplug/test/script/kernel-node.test.ts) shows `blockInput` handing the frames
  to the decoder.
- **Capture modes.** The build-spec wire tests (TS and the C++ EngineGraph case) carry each mode under its key;
  `listen` captures a Game Boy's own clocked bytes and never clocks its port (mGB, which holds its port in
  external-clock receive, captures nothing under `listen` and a byte every eight frames under `drive`).
- **End to end** ([lsdj-mastersync.test.ts](../packages/retroplug/test-native/lsdj-mastersync.test.ts)). A real LSDj
  through `renderBlocks` in 1-frame, `fl` and 1024-frame blocks, at 44.1 and 48 kHz, from power-on through START,
  play, stop, START again and play: every run tells the same story, nothing during boot, one NoteOn and 0xFA per
  start, one 0xFC per stop, no clock while stopped, clock counts within one of each other across the splits (the
  core's own timing moves by tens of frames with the split, so a byte at a render's edge can land on either side).

## 8. Docs

B0 design section 2.5 marks the sixth and eighth defects fixed, with the cause and the new figures, and notes the
lead on the seventh. AGENTS.md counts three pinned defects and drops "the next due". `spec/06` drops both from its
defect list, `spec/07` drops its item (B3b is next), the B3a design's section 10 and the main design's B3 paragraph
say it is done, and `spec/02` / `spec/04` describe the capture modes and the stop rule. Comments follow in
`dspRoles.ts`, `lsdjArduinoboy.ts`, `SameBoySystem`, `SystemNodes.cpp` and the LSDj test files.

## 9. Landing

1. This design, and its plan beside it.
2. The frames on the wire and through the kernel: no behaviour change, parity bit for bit.
3. The capture modes, the decoder, their tests, the re-record and the docs, in one commit, as an intentional change
   must be (AGENTS.md).

Each goes through review.
