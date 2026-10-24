// The Launchpad controller role: pad presses arrive on the controller stream (not as musical MIDI), and
// the lsdj-midimap app turns them into row launches on an Arduinoboy LSDj in SYNC=MIDIMAP. Needs the
// sibling Arduinoboy ROM, so these skip in CI.
import { runGroup, type ParityCase } from "./group";
import { blocks, boot, lsdj, SR } from "./lsdj";
import { frames, ROMS } from "./scenes";
import { savFrom, type SavInput } from "../../src/lsdjSav";
import { padIndex } from "../../src/launchpad";

// 16 contiguous rows, each one single-phrase chain, so every row a pad shows is playable.
const SONG: SavInput = {
  workingSong: {
    formatVersion: 22,
    settings: { syncMode: "MidiMap", tempo: 128 },
    rows: Array.from({ length: 16 }, () => ({ chains: [0, 0, 0, 0] })),
    chains: [{ phrases: [0] }],
    phrases: [{ notes: Array.from({ length: 16 }, () => 1), instruments: Array.from({ length: 16 }, () => 0) }],
    instruments: [{ type: "pulse", panning: "LeftRight", adsr: { initialLevel: 8, attackSpeed: 8 } }],
  },
} as SavInput;

/** The pad showing (pu1, row) on page 0: rows 0-7 are the left pane, 8-15 the right. */
const press = (row: number): number[] => [0x90, padIndex({ x: row < 8 ? 0 : 4, y: row % 8 }), 100];
const release = (row: number): number[] => [0x80, padIndex({ x: row < 8 ? 0 : 4, y: row % 8 }), 0];

const cases: ParityCase[] = [
  {
    name: "pad-launches",
    needs: [ROMS.arduinoboy],
    sampleRate: SR,
    outputs: 2,
    audible: [0, 1],
    setup: (rig) => {
      lsdj(rig, ROMS.arduinoboy, savFrom(SONG), { mode: "midiMap" });
      rig.project.setController({ enabled: true });
    },
    segments: [
      boot(),
      {
        blocks: blocks(frames(SR, 2000)),
        controller: [
          { at: 0, bytes: press(6) },
          { at: frames(SR, 100), bytes: release(6) },
          { at: frames(SR, 1000), bytes: press(2) },
          { at: frames(SR, 1100), bytes: release(2) },
        ],
      },
    ],
  },
];

runGroup("controller", cases);
