// Edits made while audio runs: each lands between two segments, as a store call, which is where it lands
// in the plugin too (drained at the start of the next block). Live settings (gain, which the core smooths
// per frame, and role knobs) are edits-live.test.ts; every structural edit the UI makes mid-performance is
// edits-structure.test.ts.
import type { ParityCase } from "./group";
import { even, type Rig } from "./rig";
import { frames, mgb, mgbFile, BOOT_MS, phrase, ROMS } from "./scenes";

const SR = 48000;
const blocks = (n: number) => even(n, 512);
const play = (ms = 600) => ({ blocks: blocks(frames(SR, ms)), midi: phrase(SR, [0, 1, 2, 3]) });
const quiet = (ms = 600) => ({ blocks: blocks(frames(SR, ms)) });
const boot = () => ({ blocks: blocks(frames(SR, BOOT_MS)) });

const stereo = (name: string, setup: (rig: Rig) => unknown, segments: ParityCase["segments"]): ParityCase => ({
  name,
  sampleRate: SR,
  outputs: 2,
  audible: [0, 1],
  setup: (rig) => void setup(rig),
  segments,
});

export const liveEdits: ParityCase[] = [
  // A held note through two gain changes: the smoother ramps each one per frame.
  stereo("gain-mid-note", (rig) => mgb(rig), [
    boot(),
    { ...play(), after: (rig) => void rig.project.systems.setGain(rig.id(0), -12) },
    { ...quiet(), after: (rig) => void rig.project.systems.setGain(rig.id(0), 0) },
    quiet(),
  ]),
  stereo("sameboy-highpass-live", (rig) => mgb(rig), [
    boot(),
    { ...play(), after: (rig) => void rig.project.systems.setRoleConfig(rig.id(0), "sameboy", { highpass: "removeDcOffset" }) },
    play(),
  ]),
  stereo("nes-apu-latency-live", (rig) => rig.add(ROMS.bliptoaster), [
    boot(),
    { ...play(), after: (rig) => void rig.project.systems.setRoleConfig(rig.id(0), "mesen", { apuLatencyMs: 3 }) },
    play(),
  ]),
  stereo("vrc6-expansion-volume-live", (rig) => rig.add(ROMS.vrc6), [
    boot(),
    { blocks: blocks(frames(SR, 1100)), midi: phrase(SR, [0, 1, 2, 3, 4, 5, 6, 7]), after: (rig) => void rig.project.systems.setRoleConfig(rig.id(0), "mesen", { expansionVolume: 40 }) },
    { blocks: blocks(frames(SR, 1100)), midi: phrase(SR, [0, 1, 2, 3, 4, 5, 6, 7]) },
  ]),
];

export const structuralEdits: ParityCase[] = [
  // A reset cold-boots the core, so it boots again before the next phrase.
  stereo("reset-mid-run", (rig) => mgb(rig), [boot(), { ...play(), after: (rig) => void rig.project.systems.reset(rig.id(0)) }, boot(), play()]),
  // The clone boots from the source's savestate and plays on alongside it.
  {
    name: "duplicate-mid-run",
    sampleRate: SR,
    outputs: 4,
    audible: [0, 1, 2, 3],
    setup: (rig) => {
      mgb(rig);
      rig.project.setAudioRouting("twoPerInstance");
    },
    segments: [boot(), { ...play(), after: (rig) => void rig.project.systems.duplicateSystem(rig.id(0)) }, play()],
  },
  {
    name: "add-mid-run",
    sampleRate: SR,
    outputs: 4,
    audible: [0, 1, 2, 3],
    setup: (rig) => {
      mgb(rig);
      rig.project.setAudioRouting("twoPerInstance");
    },
    // The added system boots while the first plays on.
    segments: [boot(), { ...play(), after: (rig) => void mgbFile(rig) }, boot(), play()],
  },
  {
    name: "remove-mid-run",
    sampleRate: SR,
    outputs: 4,
    audible: [0, 1, 2, 3],
    setup: (rig) => {
      mgb(rig);
      mgbFile(rig);
      rig.project.setAudioRouting("twoPerInstance");
    },
    segments: [boot(), { ...play(), after: (rig) => void rig.project.systems.removeSystem(rig.id(0)) }, play()],
  },
  stereo("replace-mid-run", (rig) => mgb(rig), [
    boot(),
    { ...play(), after: (rig) => void rig.project.systems.replaceSystem(rig.id(0), rig.rom(ROMS.bliptoaster)) },
    boot(),
    play(),
  ]),
  // A split is single-system: a second system drops the project to Stereo, removing it splits again.
  {
    name: "split-then-second-system",
    sampleRate: SR,
    outputs: 8,
    audible: [0, 1, 2, 3, 4, 5, 6, 7],
    setup: (rig) => {
      mgb(rig);
      rig.project.setAudioRouting("channelSplit");
    },
    segments: [
      boot(),
      { ...play(), after: (rig) => void mgbFile(rig) },
      { ...play(), after: (rig) => void rig.project.systems.removeSystem(rig.id(1)) },
      play(),
    ],
  },
  // Leaving a pin split puts back the channel-export mode the split armed.
  {
    name: "pinSplit-then-stereo",
    sampleRate: SR,
    outputs: 4,
    audible: [0, 1],
    setup: (rig) => {
      rig.add(ROMS.bliptoaster);
      rig.project.setAudioRouting("pinSplit");
    },
    segments: [boot(), { ...play(), after: (rig) => void rig.project.setAudioRouting("stereo") }, play()],
  },
];
