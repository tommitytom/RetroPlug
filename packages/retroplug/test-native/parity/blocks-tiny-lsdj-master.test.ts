// LSDj as the clock master in 1-frame blocks and FL-style ragged splits (blocks-tiny.ts has the lists):
// lsdj-modes' masterSync scene, LSDj's own clock decoded into host MIDI out. Split from blocks-tiny-lsdj.test.ts so
// the two run in parallel: together they were the slowest file in the suite. Needs the sibling LSDj ROMs, so these
// skip in CI.
import { runGroup, type ParityCase } from "./group";
import { TINY_SPLITS } from "./blocks-tiny";
import { lsdj, lsdjCase, songSav, tap } from "./lsdj";
import { frames, ROMS, type Blocks } from "./scenes";

/** lsdj-modes' masterSync scene at both of its rates: LSDj as the clock master (SYNC=LSDj, lsdj-sync mode
 *  masterSync), its serial out decoded into host MIDI out. START, 1.5 s playing, START again, then 1.2 s
 *  stopped. The core only listens to the bytes LSDj clocks out itself, and the decoder sends a clock per byte and
 *  0xFC after a 200 ms silence, timed on the bytes' frames, so every split here sends what lsdj-modes'
 *  1024-frame masterSync-44100 sends: the run's NoteOn, 0xFA and clocks, and one 0xFC
 *  (docs/lsdj-master-sync-fix-design.md). Until that fix these cases pinned two defects: a decoder that counted
 *  bytes per block, and a core that clocked a stopped LSDj into a slave, flooding its port and, at 48 kHz, keeping
 *  it from ever playing. */
function masterSync(sr: number, split: string, blocks: Blocks): ParityCase {
  return lsdjCase(
    `masterSync-${sr}-${split}`,
    ROMS.lsdj,
    (rig) => lsdj(rig, ROMS.lsdj, songSav("Lsdj"), { mode: "masterSync" }),
    [...tap(0, sr, blocks), { blocks: blocks(frames(sr, 1500)) }, ...tap(0, sr, blocks), { blocks: blocks(frames(sr, 1200)) }],
    2,
    [0, 1],
    sr,
    blocks,
  );
}

const cases: ParityCase[] = [44100, 48000].flatMap((sr) => TINY_SPLITS.map(([split, blocks]) => masterSync(sr, split, blocks)));

runGroup("blocks-tiny-lsdj-master", cases);
