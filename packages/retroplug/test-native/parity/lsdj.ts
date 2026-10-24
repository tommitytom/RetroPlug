// LSDj under each lsdj-sync mode, with the stimulus that mode answers to. Needs the sibling LSDj ROMs
// (ROMS.lsdj, and ROMS.arduinoboy for the Arduinoboy firmware's modes), so these skip in CI. The cases
// are lsdj-sync.test.ts, lsdj-modes.test.ts and lsdj-link.test.ts.
import type { ParityCase } from "./group";
import { even, type Rig, type Segment } from "./rig";
import { frames, type Blocks } from "./scenes";
import { savFrom, type SavInput, type SongSettings } from "../../src/lsdjSav";

export const SR = 48000;
export const blocks = (n: number) => even(n, 512);
const START = 7;
/** A valid battery skips LSDj's self-test, and it still takes a few seconds to reach the song screen. */
export const LSDJ_BOOT_MS = 6000;

/** One pulse note per phrase: sparse, so a sync mode's effect on WHEN it plays shows. */
export function songSav(syncMode: SongSettings["syncMode"]): Uint8Array {
  return savFrom({
    workingSong: {
      formatVersion: 22,
      settings: { syncMode },
      rows: [{ chains: [0] }],
      chains: [{ phrases: [0] }],
      phrases: [{ notes: [1], instruments: [0] }],
      instruments: [{ type: "pulse", panning: "LeftRight", adsr: { initialLevel: 8, attackSpeed: 8 } }],
    },
  } as SavInput);
}

/** Eight notes on the first eight steps: dense enough that the step timing a clock produces shows within a
 *  couple of seconds (one note per phrase plays identically at every tempo divisor until the phrase wraps). */
export function denseSongSav(syncMode: SongSettings["syncMode"]): Uint8Array {
  return savFrom({
    workingSong: {
      formatVersion: 22,
      settings: { syncMode },
      rows: [{ chains: [0] }],
      chains: [{ phrases: [0] }],
      phrases: [{ notes: [1, 13, 25, 1, 13, 25, 1, 13], instruments: [0, 0, 0, 0, 0, 0, 0, 0] }],
      instruments: [{ type: "pulse", panning: "LeftRight", adsr: { initialLevel: 8, attackSpeed: 8 } }],
    },
  } as SavInput);
}

/** For MI.OUT: N commands that the Arduinoboy firmware turns into MIDI notes. */
export function midiOutSav(): Uint8Array {
  return savFrom({
    workingSong: {
      formatVersion: 22,
      settings: { syncMode: "MidiOut" },
      rows: [{ chains: [0] }],
      chains: [{ phrases: [0] }],
      phrases: [
        {
          notes: [1, 13, 25, 1, 13, 25, 1, 13],
          instruments: [0, 0, 0, 0, 0, 0, 0, 0],
          commands: ["N", "N", "N", "N", "N", "N", "N", "N"],
          commandValues: [48, 52, 55, 60, 64, 67, 72, 36],
        },
      ],
      instruments: [{ type: "pulse", panning: "LeftRight", adsr: { initialLevel: 8, attackSpeed: 8 } }],
    },
  } as SavInput);
}

/** Add an LSDj build with `sav` as its battery and lsdj-sync set to `config`. Each system gets its own copy
 *  of the ROM, so its battery is the one written here rather than a fresh "-2.sav" slot. */
export function lsdj(rig: Rig, rom: string, sav: Uint8Array, config: Record<string, unknown>): number {
  const copy = rig.rom(rom, `s${rig.project.systems.view().length}-`);
  rig.writeFile(copy.replace(/\.[^./]+$/, ".sav"), sav);
  const id = rig.project.systems.addSystem(copy);
  if (id == null) throw new Error(`${rom} did not build`);
  rig.project.systems.setRoleConfig(id, "lsdj-sync", config);
  return id;
}

/** The boot segment, in blocks from `split` (512 frames by default). */
export const boot = (rate = SR, split: Blocks = blocks): Segment => ({ blocks: split(frames(rate, LSDJ_BOOT_MS)) });

/** Press START on `slot` after this segment, and release it after the next (an 80 ms tap). */
export function tap(slot = 0, rate = SR, split: Blocks = blocks): Segment[] {
  return [
    { blocks: split(frames(rate, 10)), after: (rig) => rig.press(slot, START, true) },
    { blocks: split(frames(rate, 80)), after: (rig) => rig.press(slot, START, false) },
  ];
}

export function lsdjCase(
  name: string,
  rom: string,
  setup: (rig: Rig) => unknown,
  segments: Segment[],
  outputs: 2 | 4 = 2,
  audible: number[] = outputs === 2 ? [0, 1] : [0, 1, 2, 3],
  rate = SR,
  split: Blocks = blocks,
): ParityCase {
  return {
    name,
    needs: [rom],
    sampleRate: rate,
    outputs,
    audible,
    setup: (rig) => void setup(rig),
    segments: [boot(rate, split), ...segments],
  };
}
