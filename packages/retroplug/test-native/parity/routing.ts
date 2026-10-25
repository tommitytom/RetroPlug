// Many systems fanned across the host's outputs (Stereo / TwoPerInstance / OnePerInstance). The order
// systems sum in, and OnePerInstance's L-then-R mono association, are what B1's wiring has to reproduce.
// One file per mode (routing-<mode>.test.ts).
import type { ParityCase } from "./group";
import { even, type Rig } from "./rig";
import { frames, mgb, mgbFile, BOOT_MS, phrase, ROMS } from "./scenes";
import type { AudioRouting } from "../../src/settingsEnums";

const SR = 48000;
const blocks = (n: number) => even(n, 512);

// Mixed cores by slot, so every count crosses the SameBoy / Mesen boundary.
const BY_SLOT: ((rig: Rig) => unknown)[] = [
  (rig) => mgb(rig),
  (rig) => rig.add(ROMS.bliptoaster),
  (rig) => mgbFile(rig),
  (rig) => rig.add(ROMS.vrc6),
  (rig) => mgbFile(rig),
];

/** The lanes the multi-out wiring feeds for `count` systems (buildGraph's placement, graph/buildGraph.ts). */
export function routedLanes(mode: AudioRouting, outputs: number, count: number): number[] {
  const lanes = new Set<number>();
  for (let i = 0; i < count; i++) {
    if (mode === "twoPerInstance") [(2 * i) % outputs, (2 * i + 1) % outputs].forEach((l) => lanes.add(l));
    else if (mode === "onePerInstance") lanes.add(i % outputs);
    else [0, 1].forEach((l) => lanes.add(l));
  }
  return [...lanes].sort((a, b) => a - b);
}

export function routingCases(mode: AudioRouting): ParityCase[] {
  const out: ParityCase[] = [];
  for (const outputs of [2, 4, 8] as const)
    for (const count of [1, 2, 3, 5])
      out.push({
        name: `${mode}-o${outputs}-n${count}`,
        sampleRate: SR,
        outputs,
        audible: routedLanes(mode, outputs, count),
        setup: (rig) => {
          for (let i = 0; i < count; i++) BY_SLOT[i](rig);
          // Slots 0, 2 and 4 are all mGB and render bit-identical audio, so a misroute or a reordered sum
          // among them would be invisible. A distinct gain per slot tells every slot apart.
          for (let i = 0; i < count; i++) rig.project.systems.setGain(rig.id(i), -i);
          rig.project.setAudioRouting(mode);
        },
        // sendToAll: every system hears the phrase, mGB on its voices 0..3 and the NES on its channels 0..3.
        segments: [{ blocks: blocks(frames(SR, BOOT_MS)) }, { blocks: blocks(frames(SR, 600)), midi: phrase(SR, [0, 1, 2, 3]) }],
      });
  return out;
}
