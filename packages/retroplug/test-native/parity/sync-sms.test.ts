// Transport-driven sync: smsggdj set to SYNC IN24 waits for the host clock, which the sms-sync role
// turns into a 2-bit counter on the cart's sync lines at 24 PPQN (port 2 on SMS, the EXT port on Game
// Gear). A transport script exercises start, a bpm change, stop, a locate, and the short block a DAW
// hands over at a loop wrap.
import { runGroup, type ParityCase } from "./group";
import { even } from "./rig";
import { frames, smsggdj, smsStart } from "./scenes";

const WRAP_BPM = 140;
// The loop end is OFF the 24-PPQN tick grid (a tick every 1/24 quarter): smsggdj sees only counter deltas, so
// with the end on the grid a tick lands at the next block's frame 0 whether ppq is relocated or not, and the
// locate would be invisible. Off the grid, a wrap shows through the tick phase.
const LOOP_END_PPQ = 4 + 1 / 48;
const WRAP_BLOCK = 300;

function transportCase(machine: "sms" | "gg", sr: number, size: number | null): ParityCase {
  const blocks = (n: number) => even(n, size ?? 512);
  // The block before the wrap ENDS exactly at the loop end: its start ppq is the loop end less what it spans.
  const wrapStart = LOOP_END_PPQ - (WRAP_BLOCK * WRAP_BPM) / 60 / sr;
  return {
    name: size === null ? `${machine}-transport` : `${machine}-transport-${sr}-${size}`,
    sampleRate: sr,
    outputs: 2,
    audible: [0, 1],
    setup: (rig) => void smsggdj(rig, machine, true),
    segments: [
      ...smsStart(sr, blocks),
      { blocks: blocks(frames(sr, 1500)), transport: [{ at: 0, bpm: 120, ppq: 0, playing: true }, { at: frames(sr, 900), bpm: WRAP_BPM }] },
      { blocks: blocks(frames(sr, 500)), transport: [{ at: 0, playing: false }] },
      // A loop wrap, as a DAW does it while playing: a short block that ends at the loop end, then a locate
      // back to the loop start at the next block's start, still playing. The short block also starts the
      // transport after the stop segment above.
      {
        blocks: [WRAP_BLOCK, ...blocks(frames(sr, 1500))],
        transport: [{ at: 0, ppq: wrapStart, bpm: WRAP_BPM, playing: true }, { at: WRAP_BLOCK, ppq: 0 }],
      },
    ],
  };
}

// 48 kHz in 512-frame blocks, then 44.1 kHz in 8192-frame blocks (a transport change only lands at a block start).
const cases: ParityCase[] = (["sms", "gg"] as const).flatMap((machine) => [
  transportCase(machine, 48000, null),
  transportCase(machine, 44100, 8192),
]);

runGroup("sync-sms", cases);
