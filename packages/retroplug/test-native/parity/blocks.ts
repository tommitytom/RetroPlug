// Block partitioning: the same scene in every host block size, at both common sample rates. SameBoy's
// output depends on the partition (it drops what a block overproduces), so each size is its own baseline.
// Mesen's audio proved partition-invariant (blocks-{nes,sms,gg} audio is bit-identical across the six
// patterns at each rate; only end-of-case work RAM differs), and is pinned as invariant. The phrase's MIDI
// lands inside blocks; the mgb and nes cases end with one more segment, shared by every partition, whose
// note starts on a block's first frame and ends on a later block's last frame.
// One file per system (blocks-<system>.test.ts), so no one file outweighs the rest of test:native.
import type { ParityCase } from "./group";
import { cycle, even, type Segment } from "./rig";
import { frames, mgb, MGB_CHANNELS, BOOT_MS, NES_CHANNELS, phrase, ROMS, smsggdj, smsStart, type Blocks } from "./scenes";

const RATES = [44100, 48000];
/** Every host block size the matrix renders, by name; lsdj-link reuses them. */
export const SPLITS: [string, Blocks][] = [
  ["64", (n) => even(n, 64)],
  ["199", (n) => even(n, 199)],
  ["512", (n) => even(n, 512)],
  ["1024", (n) => even(n, 1024)],
  ["8192", (n) => even(n, 8192)],
  ["cycle", (n) => cycle(n, [64, 199, 512, 1024, 8192, 333])],
];

export type BlockSystem = "mgb" | "nes" | "sms" | "gg";

/** The trailing segment every mgb and nes partition shares: ONE irregular block list (the same at a rate for
 *  all six partitions), carrying a note-on on the first frame of one block and its note-off on the LAST frame
 *  of a later one (channel 1, key 58: one the phrase never plays). This pins that events on block edges are
 *  delivered, not dropped or moved by a large amount; the cores are not one-frame sensitive at these
 *  positions, so it does not pin an off-by-one. */
function edgeSegment(sr: number): Segment {
  const blocks = cycle(frames(sr, 600), [64, 199, 333, 512, 1024, 7, 2048]);
  const start = (i: number) => blocks.slice(0, i).reduce((a, b) => a + b, 0);
  const offBlock = 30;
  return {
    blocks,
    midi: [
      { at: start(10), bytes: [0x91, 58, 64] },
      { at: start(offBlock) + blocks[offBlock] - 1, bytes: [0x81, 58, 64] },
    ],
  };
}

function blockCase(system: BlockSystem, sr: number, split: string, blocks: Blocks): ParityCase {
  const base = { name: `${system}-${sr}-${split}`, sampleRate: sr, outputs: 2 as const, audible: [0, 1] };
  switch (system) {
    case "mgb":
      return {
        ...base,
        setup: (rig) => void mgb(rig),
        segments: [
          { blocks: blocks(frames(sr, BOOT_MS)) },
          { blocks: blocks(frames(sr, 600)), midi: phrase(sr, MGB_CHANNELS) },
          edgeSegment(sr),
        ],
      };
    case "nes":
      return {
        ...base,
        setup: (rig) => void rig.add(ROMS.bliptoaster),
        segments: [
          { blocks: blocks(frames(sr, BOOT_MS)) },
          { blocks: blocks(frames(sr, 600)), midi: phrase(sr, NES_CHANNELS) },
          edgeSegment(sr),
        ],
      };
    default:
      return {
        ...base,
        setup: (rig) => void smsggdj(rig, system, false),
        segments: [...smsStart(sr, blocks), { blocks: blocks(frames(sr, 600)) }],
      };
  }
}

export function blockCases(system: BlockSystem): ParityCase[] {
  return RATES.flatMap((sr) => SPLITS.map(([split, blocks]) => blockCase(system, sr, split, blocks)));
}
