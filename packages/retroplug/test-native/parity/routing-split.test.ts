// The split routings on the base builds, at every host width; split.ts has the why.
import { runGroup, type ParityCase } from "./group";
import { range, single, splitSegments, SR } from "./split";
import { mgb, ROMS } from "./scenes";

const cases: ParityCase[] = [];
for (const outputs of [2, 4, 8] as const) {
  // A Game Boy splits into 4 stereo pairs: 8 lanes, or Stereo (bit-identical to it) when the host is narrower.
  cases.push(single(`gb-channelSplit-o${outputs}`, "channelSplit", outputs, outputs === 8 ? range(8) : [0, 1], (rig) => mgb(rig), [0, 1, 2, 3]));
  // A NES splits into its 5 core channels, one lane each. The phrase sounds the two pulses and the
  // triangle, and an added in-range note (split.ts) sounds noise on lane 3; DMC stays silent because
  // nothing plays a sample. On a host narrower than 8 (o2, o4) this is the KNOWN DEFECT recorded as it is
  // (split.ts has the cause): no Stereo fallback, and the mix path drifts. Those cases pin the drift.
  cases.push(single(`nes-channelSplit-o${outputs}`, "channelSplit", outputs, outputs === 8 ? range(4) : [0, 1], (rig) => rig.add(ROMS.bliptoaster), [0, 1, 2, 3]));
  // Pins: Pulse | TND | Expansion (silent on the 2A03-only build), mono on 3 lanes or a pair each on 6.
  // Narrow hosts (pinSplit o2, stereoPinSplit o2/o4) are the same KNOWN DEFECT as above.
  cases.push(single(`nes-pinSplit-o${outputs}`, "pinSplit", outputs, [0, 1], (rig) => rig.add(ROMS.bliptoaster), [0, 1, 2, 3]));
  cases.push(single(`nes-stereoPinSplit-o${outputs}`, "stereoPinSplit", outputs, outputs === 8 ? range(4) : [0, 1], (rig) => rig.add(ROMS.bliptoaster), [0, 1, 2, 3]));
}
// A Game Boy under PinSplit has no pins (buildGraph, in graph/buildGraph.ts, splits pins only on a NES):
// Stereo, bit-identical.
cases.push(single("gb-pinSplit-o8", "pinSplit", 8, [0, 1], (rig) => mgb(rig), [0, 1, 2, 3]));
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
