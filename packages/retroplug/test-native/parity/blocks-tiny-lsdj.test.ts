// LSDj's host-sync protocols in 1-frame blocks and FL-style ragged splits (blocks-tiny.ts has the lists, lsdj.ts
// the scenes): SYNC=MIDI following the host clock, each tick at its own frame; the Arduinoboy protocol, its
// clocks at a block's frame 0, its play / stop notes, 0xFA and 0xFC on the transport's edges; MI.OUT, LSDj's
// serial out decoded into host MIDI out a block later, so the recorded block indices pin that one-block latency
// at every block size here; and master sync, LSDj's own clock decoded the same way, with a known defect pinned
// (masterSync below). Tempo and transport points after a segment's start sit on 1-frame blocks,
// where FL puts them (it splits a buffer at each one), and the sync notes on block edges. Needs the sibling LSDj
// ROMs, so these skip in CI.
import { runGroup, type ParityCase } from "./group";
import { ANY, BUFFER, Edges, TINY_SPLITS } from "./blocks-tiny";
import { denseSongSav, lsdj, lsdjCase, midiOutSav, songSav, SR, tap } from "./lsdj";
import { frames, ROMS, type Blocks } from "./scenes";

/** lsdj-sync's midiSync-div1 (START, then the transport), with a tempo change a second in and the stop 50 ms
 *  into the last segment. */
function midiSync(split: string, blocks: Blocks): ParityCase {
  const play = new Edges(blocks(frames(SR, 2000)));
  const stop = new Edges(blocks(frames(SR, 500)));
  return lsdjCase(
    `midiSync-${split}`,
    ROMS.lsdj,
    (rig) => lsdj(rig, ROMS.lsdj, denseSongSav("Midi"), { mode: "midiSync", tempoDivisor: 1 }),
    [
      ...tap(0, SR, blocks),
      { blocks: play.blocks, transport: [{ at: 0, bpm: 120, ppq: 0, playing: true }, { at: play.oneFrameAt(frames(SR, 1000)), bpm: 140 }] },
      { blocks: stop.blocks, transport: [{ at: stop.oneFrameAt(frames(SR, 50)), playing: false }] },
    ],
    2,
    [0, 1],
    SR,
    blocks,
  );
}

/** lsdj-sync's Arduinoboy scene: the transport starts, note 24 (play) arrives in a 1-frame block, then note 25
 *  (stop) on a buffer-sized block's last frame, which ends the clocks, and 200 ms later the transport stop on
 *  a 1-frame block.
 *
 *  KNOWN DEFECT, recorded as it is (lsdj-sync.test.ts, design section 2.5 of modular-graph-phase-b0-design.md):
 *  Arduinoboy MIDI sync plays only under some partitions. In 1-frame blocks, and in `fl256` (no block over 256
 *  frames), it never starts, as at 128 to 768 frames, so those cases pin the silence and the work RAM the
 *  protocol's bytes leave. `fl` plays: at 120 bpm a tick is 1000 frames, so its 4096-frame block carries about
 *  four and its six other blocks almost none, and "every block carries a tick" is not the condition. */
function arduinoboy(split: string, blocks: Blocks): ParityCase {
  const run = new Edges(blocks(frames(SR, 2000)));
  const end = new Edges(blocks(frames(SR, 500)));
  const stopNote = end.last(end.next(frames(SR, 100), BUFFER, ANY));
  const stop = end.oneFrameAt(stopNote + frames(SR, 200));
  return lsdjCase(
    `midiSyncArduinoboy-${split}`,
    ROMS.arduinoboy,
    (rig) => lsdj(rig, ROMS.arduinoboy, denseSongSav("Lsdj"), { mode: "midiSyncArduinoboy", tempoDivisor: 1 }),
    [
      { blocks: blocks(frames(SR, 1000)), transport: [{ at: 0, bpm: 120, ppq: 0, playing: true }] },
      { blocks: run.blocks, midi: [{ at: run.oneFrameAt(0), bytes: [0x90, 24, 100] }] },
      {
        blocks: end.blocks,
        midi: [{ at: stopNote, bytes: [0x90, 25, 100] }],
        transport: [{ at: stop, playing: false }],
      },
    ],
    2,
    split === "fl" ? [0, 1] : [], // `1` and `fl256`: silent, the defect above
    SR,
    blocks,
  );
}

/** lsdj-modes' midiOut scene (play, stop, play, each by a START tap), the last run shortened to a second.
 *  KNOWN DEFECT, recorded as it is (lsdj-modes.test.ts): what MI.OUT decodes depends on the host sample rate and
 *  matches none of the song's N values, so this pins what is captured today, at 48 kHz. */
function midiOut(split: string, blocks: Blocks): ParityCase {
  return lsdjCase(
    `midiOut-${split}`,
    ROMS.arduinoboy,
    (rig) => lsdj(rig, ROMS.arduinoboy, midiOutSav(), { mode: "midiOut" }),
    [
      ...tap(0, SR, blocks),
      { blocks: blocks(frames(SR, 2000)) },
      ...tap(0, SR, blocks),
      { blocks: blocks(frames(SR, 400)) },
      ...tap(0, SR, blocks),
      { blocks: blocks(frames(SR, 1000)) },
    ],
    2,
    [0, 1],
    SR,
    blocks,
  );
}

/** lsdj-modes' masterSync scene at both of its rates: LSDj as the clock master (SYNC=LSDj, lsdj-sync mode
 *  masterSync), its serial out decoded into host MIDI out. START, 1.5 s playing, START again, then 1.2 s
 *  stopped, so the stopped LSDj's idle flood is in the case after the stop as well as before the first START.
 *
 *  KNOWN DEFECT, pinned as it is (design section 2.5 of modular-graph-phase-b0-design.md): master sync breaks
 *  in small blocks. The decoder (arduinoboyMasterSyncBlock, lsdjArduinoboy.ts) tells a stopped LSDj from a
 *  playing one by how many serial-out bytes one block carries: more than 16 is the stopped LSDj's idle flood,
 *  dropped, with one 0xFC on the play-to-stop edge. A playing LSDj sends a byte a tick and a stopped one about
 *  5000 a second, so whether a block passes 16 depends on the block's length, not on LSDj. In 1-frame blocks
 *  none does: every byte of the flood is decoded as a clock, before the first START and after the stop, no
 *  0xFC is ever sent, and the run the boot's bytes open never closes, so START gets no NoteOn / 0xFA. In `fl`
 *  and `fl256` the flood's blocks fall on both sides of the threshold, so a stopped LSDj opens and closes a run
 *  (NoteOn, 0xFA, clocks, 0xFC) every few blocks. At 44.1 kHz that is 10497 MIDI messages in `1`, 316 in `fl`
 *  and 894 in `fl256`, against 101 in 1024-frame blocks (lsdj-modes' masterSync-44100). At 48 kHz LSDj never
 *  plays (lsdj-modes' masterSync-48000, the rate defect), so those cases are silent and their MIDI out is the
 *  decoded flood alone. Fixing the decoder is its own change, with a deliberate re-record of these cases. */
function masterSync(sr: number, split: string, blocks: Blocks): ParityCase {
  return lsdjCase(
    `masterSync-${sr}-${split}`,
    ROMS.lsdj,
    (rig) => lsdj(rig, ROMS.lsdj, songSav("Lsdj"), { mode: "masterSync" }),
    [...tap(0, sr, blocks), { blocks: blocks(frames(sr, 1500)) }, ...tap(0, sr, blocks), { blocks: blocks(frames(sr, 1200)) }],
    2,
    sr === 44100 ? [0, 1] : [], // 48 kHz: silent, the rate defect
    sr,
    blocks,
  );
}

const cases: ParityCase[] = [
  ...[midiSync, arduinoboy, midiOut].flatMap((scene) => TINY_SPLITS.map(([split, blocks]) => scene(split, blocks))),
  ...[44100, 48000].flatMap((sr) => TINY_SPLITS.map(([split, blocks]) => masterSync(sr, split, blocks))),
];

runGroup("blocks-tiny-lsdj", cases);
