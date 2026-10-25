// risa host sync in 1-frame blocks and FL-style ragged splits (blocks-tiny.ts has the lists): risa.test.ts's
// transport script (start, a tempo change, stop, a loop wrap's locate) with every point after a segment's start
// on a 1-frame block, where FL puts it: it splits a buffer at each tempo point and at the loop end, so the block
// after a wrap starts with the locate. The risa-sync role clocks risa at each tick's own frame, so its clocks
// land on block edges throughout, and the arm it sends on a locate flushes the FIFO. Mesen's audio is
// partition-invariant (blocks.ts), so these lanes pin the bytes the role sent and when, not the partition.
// Needs the sibling risa fixtures, so these skip in CI.
import { runGroup, type ParityCase } from "./group";
import { Edges, TINY_SPLITS } from "./blocks-tiny";
import { frames, risa, RISA_BOOT_MS, ROMS, type Blocks } from "./scenes";

const SR = 48000;

function transport(split: string, blocks: Blocks): ParityCase {
  const play = new Edges(blocks(frames(SR, 1500)));
  const stop = new Edges(blocks(frames(SR, 500)));
  const wrap = new Edges(blocks(frames(SR, 1500)));
  return {
    name: `transport-${split}`,
    needs: [ROMS.risa, ROMS.risaSong],
    sampleRate: SR,
    outputs: 2,
    audible: [0, 1],
    setup: (rig) => void risa(rig),
    segments: [
      { blocks: blocks(frames(SR, RISA_BOOT_MS)) },
      { blocks: play.blocks, transport: [{ at: 0, bpm: 120, ppq: 0, playing: true }, { at: play.oneFrameAt(frames(SR, 900)), bpm: 140 }] },
      { blocks: stop.blocks, transport: [{ at: stop.oneFrameAt(0), playing: false }] },
      // Playing again from ppq 4, then the wrap: a locate back to the loop start on a 1-frame block.
      { blocks: wrap.blocks, transport: [{ at: 0, ppq: 4, playing: true }, { at: wrap.oneFrameAt(300), ppq: 0 }] },
    ],
  };
}

runGroup(
  "blocks-tiny-risa",
  TINY_SPLITS.map(([split, blocks]) => transport(split, blocks)),
);
