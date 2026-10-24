// risa (NES / MMC5) following the host transport: the risa-sync role turns it into risa's arm / start /
// 24-PPQN clock / stop byte stream on the N8 FIFO. The same transport script as sync-sms, and risa
// under PinSplit. Needs the sibling risa fixtures, so these skip in CI.
import { runGroup, type ParityCase } from "./group";
import { even, type Rig } from "./rig";
import { frames, ROMS } from "./scenes";

const SR = 48000;
const blocks = (n: number) => even(n, 512);
/** risa materializes the song into working RAM while it boots, transport off. */
const RISA_BOOT_MS = 2500;

function risa(rig: Rig): number {
  const copy = rig.rom(ROMS.risa);
  const song = rig.session.backend.readFile(ROMS.risaSong);
  if (!song) throw new Error(`cannot read ${ROMS.risaSong}`);
  rig.writeFile(copy.replace(/\.[^./]+$/, ".srm"), song);
  const id = rig.project.systems.addSystem(copy);
  if (id == null) throw new Error("risa did not build");
  return id;
}

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
    // let_go sounds on the TND pin only: its pulse and MMC5 voices are silent over this stretch.
    audible: [1],
    setup: (rig) => {
      risa(rig);
      rig.project.setAudioRouting("pinSplit");
    },
    segments: transport,
  },
];

runGroup("risa", cases);
