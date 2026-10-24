// Block partitioning: the same scene in every host block size, at both common sample rates. SameBoy's
// output depends on the partition (it drops what a block overproduces), so each size is its own baseline.
// Mesen's audio proved partition-invariant (blocks-{nes,sms,gg} audio is bit-identical across the six
// patterns at each rate; only end-of-case work RAM differs), and is pinned as invariant. The phrase's MIDI
// lands inside blocks; the mgb and nes cases add one note that starts on a block's first frame and ends on
// a later block's last frame.
// One file per system (blocks-<system>.test.ts), so no one file outweighs the rest of test:native.
import type { ParityCase } from "./group";
import { cycle, even } from "./rig";
import { frames, mgb, MGB_CHANNELS, BOOT_MS, NES_CHANNELS, phrase, ROMS, smsggdj, smsStart, type Blocks } from "./scenes";

const RATES = [44100, 48000];
const SPLITS: [string, Blocks][] = [
  ["64", (n) => even(n, 64)],
  ["199", (n) => even(n, 199)],
  ["512", (n) => even(n, 512)],
  ["1024", (n) => even(n, 1024)],
  ["8192", (n) => even(n, 8192)],
  ["cycle", (n) => cycle(n, [64, 199, 512, 1024, 8192, 333])],
];

export type BlockSystem = "mgb" | "nes" | "sms" | "gg";

/** A note-on at the first frame of a block well inside the segment and its note-off at the LAST frame of a
 *  later block, both from this case's own block list (channel 1, key 58: one the phrase never plays). */
function edgeNote(blocks: number[]): { at: number; bytes: number[] }[] {
  const start = (i: number) => blocks.slice(0, i).reduce((a, b) => a + b, 0);
  const on = Math.floor(blocks.length / 3);
  const offBlock = Math.floor((2 * blocks.length) / 3);
  return [
    { at: start(on), bytes: [0x91, 58, 64] },
    { at: start(offBlock) + blocks[offBlock] - 1, bytes: [0x81, 58, 64] },
  ];
}

const withEdges = (midi: { at: number; bytes: number[] }[], blocks: number[]) => [...midi, ...edgeNote(blocks)].sort((a, b) => a.at - b.at);

function blockCase(system: BlockSystem, sr: number, split: string, blocks: Blocks): ParityCase {
  const base = { name: `${system}-${sr}-${split}`, sampleRate: sr, outputs: 2 as const, audible: [0, 1] };
  switch (system) {
    case "mgb": {
      const play = blocks(frames(sr, 600));
      return {
        ...base,
        setup: (rig) => void mgb(rig),
        segments: [{ blocks: blocks(frames(sr, BOOT_MS)) }, { blocks: play, midi: withEdges(phrase(sr, MGB_CHANNELS), play) }],
      };
    }
    case "nes": {
      const play = blocks(frames(sr, 600));
      return {
        ...base,
        setup: (rig) => void rig.add(ROMS.bliptoaster),
        segments: [{ blocks: blocks(frames(sr, BOOT_MS)) }, { blocks: play, midi: withEdges(phrase(sr, NES_CHANNELS), play) }],
      };
    }
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
