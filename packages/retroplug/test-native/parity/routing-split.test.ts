// The split routings on the base builds, at every host width; split.ts has the why.
import { runGroup, type ParityCase } from "./group";
import { range, single, splitSegments, SR } from "./split";
import { mgb, ROMS } from "./scenes";

const cases: ParityCase[] = [];
for (const outputs of [2, 4, 8] as const) {
  // A Game Boy splits into 4 stereo pairs: 8 lanes, or Stereo when the host is narrower.
  cases.push(single(`gb-channelSplit-o${outputs}`, "channelSplit", outputs, outputs === 8 ? range(8) : [0, 1], (rig) => mgb(rig), [0, 1, 2, 3]));
  // A NES splits into its 5 core channels, one lane each. This phrase sounds the two pulses and the
  // triangle; noise and DMC stay silent, and are recorded as such.
  cases.push(single(`nes-channelSplit-o${outputs}`, "channelSplit", outputs, outputs === 8 ? range(3) : [0, 1], (rig) => rig.add(ROMS.bliptoaster), [0, 1, 2, 3]));
  // Pins: Pulse | TND | Expansion (silent on the 2A03-only build), mono on 3 lanes or a pair each on 6.
  cases.push(single(`nes-pinSplit-o${outputs}`, "pinSplit", outputs, [0, 1], (rig) => rig.add(ROMS.bliptoaster), [0, 1, 2, 3]));
  cases.push(single(`nes-stereoPinSplit-o${outputs}`, "stereoPinSplit", outputs, outputs === 8 ? range(4) : [0, 1], (rig) => rig.add(ROMS.bliptoaster), [0, 1, 2, 3]));
}
// Splits are single-system: with two, the project renders as Stereo.
cases.push({
  name: "gb-channelSplit-two-systems",
  sampleRate: SR,
  outputs: 8,
  audible: [0, 1],
  setup: (rig) => {
    mgb(rig);
    rig.add(ROMS.bliptoaster);
    rig.project.setAudioRouting("channelSplit");
  },
  segments: splitSegments([0, 1, 2, 3]),
});

runGroup("routing-split", cases);
