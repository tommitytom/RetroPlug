// Block partitioning: the same scene in every host block size, at both common sample rates. Output
// depends on the partition (SameBoy drops what a block overproduces, Mesen carries its resampler
// leftovers), so each size is its own baseline, and MIDI lands inside blocks rather than on their edges.
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

function blockCase(system: BlockSystem, sr: number, split: string, blocks: Blocks): ParityCase {
  const base = { name: `${system}-${sr}-${split}`, sampleRate: sr, outputs: 2 as const, audible: [0, 1] };
  switch (system) {
    case "mgb":
      return {
        ...base,
        setup: (rig) => void mgb(rig),
        segments: [{ blocks: blocks(frames(sr, BOOT_MS)) }, { blocks: blocks(frames(sr, 600)), midi: phrase(sr, MGB_CHANNELS) }],
      };
    case "nes":
      return {
        ...base,
        setup: (rig) => void rig.add(ROMS.bliptoaster),
        segments: [{ blocks: blocks(frames(sr, BOOT_MS)) }, { blocks: blocks(frames(sr, 600)), midi: phrase(sr, NES_CHANNELS) }],
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
