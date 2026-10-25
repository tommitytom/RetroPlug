// LSDj as the clock master in 1-frame blocks and FL-style ragged splits (blocks-tiny.ts has the lists):
// lsdj-modes' masterSync scene, LSDj's own clock decoded into host MIDI out, with a known defect pinned
// (masterSync below). Split from blocks-tiny-lsdj.test.ts so the two run in parallel: together they were the
// slowest file in the suite. Needs the sibling LSDj ROMs, so these skip in CI.
import { runGroup, type ParityCase } from "./group";
import { TINY_SPLITS } from "./blocks-tiny";
import { lsdj, lsdjCase, songSav, tap } from "./lsdj";
import { frames, ROMS, type Blocks } from "./scenes";

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

const cases: ParityCase[] = [44100, 48000].flatMap((sr) => TINY_SPLITS.map(([split, blocks]) => masterSync(sr, split, blocks)));

runGroup("blocks-tiny-lsdj-master", cases);
