// The GBA core at all: nanoloop booting through the stores, under Stereo and TwoPerInstance. nanoloop is
// silent until it is played, so these pin that the core renders and what its work RAM holds after boot,
// not a sound. Needs the sibling nanoloop ROM, so these skip in CI.
import { runGroup, type ParityCase } from "./group";
import { even } from "./rig";
import { frames, ROMS } from "./scenes";

const SR = 48000;
const blocks = (n: number) => even(n, 512);

const cases: ParityCase[] = (["stereo", "twoPerInstance"] as const).map((mode) => ({
  name: `boot-${mode}`,
  needs: [ROMS.nanoloop],
  sampleRate: SR,
  outputs: 4 as const,
  audible: [], // silent until played (see above)
  setup: (rig) => {
    rig.add(ROMS.nanoloop);
    rig.project.setAudioRouting(mode);
  },
  segments: [{ blocks: blocks(frames(SR, 2000)) }, { blocks: blocks(frames(SR, 1000)) }],
}));

runGroup("gba", cases);
