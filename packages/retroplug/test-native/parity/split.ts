// One system's channels fanned across the host's outputs: ChannelSplit (a Game Boy's 4 stereo channels,
// a NES's 5 mono core channels), PinSplit and StereoPinSplit (a NES's 3 output pins, mono or a pair
// each). A split needs its lanes. A Game Boy on a host too narrow for it, and any two-system project,
// fall back to Stereo (bit-identical to it). A single NES on a narrow host falls back to its mix the same
// way: the engine still arms its capture tap whatever the lane budget (tapArmOf in graph/buildGraph.ts
// asks for it on any host) while buildGraph, finding no room, wires the mix, so the tap's capture is
// wasted and the block drains the mix. The core gates on the ring the block drains, so a host too narrow
// for a NES split hears exactly the unarmed mix, at one block's latency and with no drift
// (docs/modular-graph-phase-b0-design.md section 2.5 records the defect that was, and
// docs/modular-graph-phase-c4-design.md section 10 the fix). The shape a NES split does not fit is
// reached at any width, too, by a construct-time export mode under a mix wiring.
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
  { blocks: blocks(frames(SR, channels.length > 4 ? 1100 : 600)), midi: withNoise(phrase(SR, channels)) },
];

/** phrase() gives channel index 3 keys 69/81/74, and BlipToaster's noise voice answers keys 36..67, so the
 *  NES noise channel would never sound. One in-range noise note at irregular frames puts it on ChannelSplit's
 *  lane 3 and in the TND pin. */
function withNoise(midi: { at: number; bytes: number[] }[]): { at: number; bytes: number[] }[] {
  return [...midi, { at: frames(SR, 17) + 5, bytes: [0x93, 48, 100] }, { at: frames(SR, 317) + 3, bytes: [0x83, 48, 0] }].sort((a, b) => a.at - b.at);
}

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
