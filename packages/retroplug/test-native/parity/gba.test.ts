// The GBA core at all: nanoloop booting through the stores, under Stereo and TwoPerInstance. nanoloop is
// silent until it is played, so these pin that the core renders and what its work RAM holds after boot.
// The START press in the second case makes a small click on lane 1 (the core's own output; `audible: []`
// stays right). That case's value is the GBA input path: the TwoPerInstance half is not observable with one
// system. Needs the sibling nanoloop ROM, so these skip in CI.
import { runGroup, type ParityCase } from "./group";
import { even } from "./rig";
import { frames, ROMS } from "./scenes";

const SR = 48000;
const blocks = (n: number) => even(n, 512);

const START = 7;

const cases: ParityCase[] = (["stereo", "twoPerInstance"] as const).map((mode) => ({
  name: mode === "stereo" ? "boot-stereo" : "boot-twoPerInstance-start",
  needs: [ROMS.nanoloop],
  sampleRate: SR,
  outputs: 4 as const,
  audible: [], // silent until played (see above)
  setup: (rig) => {
    rig.add(ROMS.nanoloop);
    rig.project.setAudioRouting(mode);
  },
  // The second case presses START after boot (and releases it a segment later), so GBA input is covered.
  segments:
    mode === "stereo"
      ? [{ blocks: blocks(frames(SR, 2000)) }, { blocks: blocks(frames(SR, 1000)) }]
      : [
          { blocks: blocks(frames(SR, 2000)), after: (rig) => rig.press(0, START, true) },
          { blocks: blocks(frames(SR, 1000)), after: (rig) => rig.press(0, START, false) },
          { blocks: blocks(frames(SR, 500)) },
        ],
}));

runGroup("gba", cases);
