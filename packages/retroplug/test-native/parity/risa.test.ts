// risa (NES / MMC5) following the host transport: the risa-sync role turns it into risa's arm / start /
// 24-PPQN clock / stop byte stream on the N8 FIFO. A transport script like sync-sms's (start, bpm change,
// stop, a loop wrap), and risa under PinSplit. Unlike sync-sms, the wrap here sits ON the tick grid: risa's
// arm carries the position, so the locate is still observed. Needs the sibling risa fixtures, so these skip in CI.
import { runGroup, type ParityCase } from "./group";
import { even } from "./rig";
import { frames, risa, RISA_BOOT_MS, ROMS } from "./scenes";

const SR = 48000;
const blocks = (n: number) => even(n, 512);

const transport: ParityCase["segments"] = [
  { blocks: blocks(frames(SR, RISA_BOOT_MS)) },
  { blocks: blocks(frames(SR, 1500)), transport: [{ at: 0, bpm: 120, ppq: 0, playing: true }, { at: frames(SR, 900), bpm: 140 }] },
  { blocks: blocks(frames(SR, 500)), transport: [{ at: 0, playing: false }] },
  // A loop wrap: a short block, then a locate back to the loop start while playing.
  { blocks: [300, ...blocks(frames(SR, 1500))], transport: [{ at: 0, ppq: 4, playing: true }, { at: 300, ppq: 0 }] },
];

const cases: ParityCase[] = [
  { name: "transport", needs: [ROMS.risa, ROMS.risaSong], sampleRate: SR, outputs: 2, audible: [0, 1], setup: (rig) => void risa(rig), segments: transport },
  {
    name: "transport-pinSplit",
    needs: [ROMS.risa, ROMS.risaSong],
    sampleRate: SR,
    outputs: 4,
    // 3 mono pins in 4 lanes (Pulse, TND, Expansion), so lane 3 is unused. let_go sounds on all three pins
    // over this stretch: the APU pulses, the TND pin and the MMC5 expansion.
    audible: [0, 1, 2],
    setup: (rig) => {
      risa(rig);
      rig.project.setAudioRouting("pinSplit");
    },
    segments: transport,
  },
];

runGroup("risa", cases);
