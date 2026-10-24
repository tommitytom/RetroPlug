// One system's channels fanned across the host's outputs: ChannelSplit (a Game Boy's 4 stereo channels,
// a NES's 5 mono core channels), PinSplit and StereoPinSplit (a NES's 3 output pins, mono or a pair
// each). A split needs its lanes: a host too narrow for it, or a second system, falls back to Stereo.
// The cases are routing-split.test.ts and routing-split-expansion.test.ts.
import type { ParityCase } from "./group";
import { even, type Rig } from "./rig";
import { BOOT_MS, frames, phrase } from "./scenes";
import type { AudioRouting } from "../../src/settingsEnums";

export const SR = 48000;
export const range = (n: number): number[] => Array.from({ length: n }, (_, i) => i);
const blocks = (n: number) => even(n, 512);

/** Boot, then a phrase on `channels` (16 channels' worth of phrase runs just over a second). */
export const splitSegments = (channels: number[]): ParityCase["segments"] => [
  { blocks: blocks(frames(SR, BOOT_MS)) },
  { blocks: blocks(frames(SR, channels.length > 4 ? 1100 : 600)), midi: phrase(SR, channels) },
];

/** One system under `mode`. */
export function single(
  name: string,
  mode: AudioRouting,
  outputs: 2 | 4 | 8,
  audible: number[],
  add: (rig: Rig) => unknown,
  channels: number[],
): ParityCase {
  return {
    name,
    sampleRate: SR,
    outputs,
    audible,
    setup: (rig) => {
      add(rig);
      rig.project.setAudioRouting(mode);
    },
    segments: splitSegments(channels),
  };
}
