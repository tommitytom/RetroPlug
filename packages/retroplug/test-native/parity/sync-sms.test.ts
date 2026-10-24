// Transport-driven sync: smsggdj set to SYNC IN24 waits for the host clock, which the sms-sync role
// turns into a 2-bit counter on the cart's sync lines at 24 PPQN (port 2 on SMS, the EXT port on Game
// Gear). A transport script exercises start, a bpm change, stop, a locate, and the short block a DAW
// hands over at a loop wrap.
import { runGroup, type ParityCase } from "./group";
import { even } from "./rig";
import { frames, smsggdj, smsStart } from "./scenes";

const SR = 48000;
const blocks = (n: number) => even(n, 512);

const cases: ParityCase[] = (["sms", "gg"] as const).map((machine) => ({
  name: `${machine}-transport`,
  sampleRate: SR,
  outputs: 2,
  audible: [0, 1],
  setup: (rig) => void smsggdj(rig, machine, true),
  segments: [
    ...smsStart(SR, blocks),
    { blocks: blocks(frames(SR, 1500)), transport: [{ at: 0, bpm: 120, ppq: 0, playing: true }, { at: frames(SR, 900), bpm: 140 }] },
    { blocks: blocks(frames(SR, 500)), transport: [{ at: 0, playing: false }] },
    // A loop wrap: a short block, then a locate back to the loop start while playing.
    { blocks: [300, ...blocks(frames(SR, 1500))], transport: [{ at: 0, ppq: 4, playing: true }, { at: 300, ppq: 0 }] },
  ],
}));

runGroup("sync-sms", cases);
