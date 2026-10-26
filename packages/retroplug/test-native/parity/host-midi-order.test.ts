// The one behaviour B3a changes on purpose (docs/modular-graph-phase-b3a-design.md sections 3.4 and 7.2): host MIDI
// staged out of frame order, as the plugin stages a block (the DAW's notes, then each moved automation slot's CC at
// frame 0), reaches every system in frame order, because rp.host.midi-in stable-sorts what is staged before the
// kernel reads it.
//
// - A Game Boy now takes the CC at the block's start, before the notes. Before B3a its serial queue, which releases
//   only the byte at its front, took the CC's bytes after the last note's, as if the CC were at that note's frame.
// - A NES takes the CC at frame 0, as before: its FIFO stable-sorted the same staging by frame.
//
// So for each, the plugin-style staging renders exactly what the same messages staged in frame order render, and
// something other than the CC staged at the last note's frame (the Game Boy's old behaviour), which shows the CC's
// place is audible. CC 1 on channel 1 is mGB's pulse width and BlipToaster's duty, so it changes both notes that
// follow it. Parity cannot hold this: renderBlocks refuses unsorted MIDI, so the staging goes through stageMidiIn.
//
// Among the parity files because the renders compared must start from cores in the same state: the runner powers
// cores on in a fixed one for parity files only (RETROPLUG_DETERMINISTIC).
import { test, expect } from "../../testing/harness";
import { Rig, even } from "./rig";
import { BOOT_MS, ROMS, frames, mgb } from "./scenes";

const SR = 48000;
const rig = new Rig();

type Staged = [frame: number, bytes: number[]][];
const NOTE_A: number[] = [0x90, 60, 100];
const NOTE_B: number[] = [0x90, 67, 100];
const DUTY: number[] = [0xb0, 1, 0];

/** The plugin's order: the block's notes, then the automation CC at frame 0. */
const PLUGIN_STYLE: Staged = [[100, NOTE_A], [300, NOTE_B], [0, DUTY]];
/** The same messages staged in frame order. */
const IN_ORDER: Staged = [[0, DUTY], [100, NOTE_A], [300, NOTE_B]];
/** The CC after the last note, at its frame: what a Game Boy took before. */
const CC_LAST: Staged = [[100, NOTE_A], [300, NOTE_B], [300, DUTY]];

/** Boot `place`'s system, stage `staged` for the next block through stageMidiIn, and render 400 ms from that block
 *  in 1024-frame ones; the left lane. */
function render(place: () => void, staged: Staged): Float32Array {
  rig.reset(SR);
  place();
  rig.render(2, { blocks: even(frames(SR, BOOT_MS), 1024) });
  for (const [frame, bytes] of staged) expect(rig.audio.stageMidiIn(bytes, frame), `staged at ${frame}`).toBe(true);
  return rig.render(2, { blocks: even(frames(SR, 400), 1024) }).lanes[0];
}

/** The first frame two renders differ at, or -1. */
function firstDifference(a: Float32Array, b: Float32Array): number {
  for (let i = 0; i < a.length; i++) if (!Object.is(a[i], b[i])) return i;
  return a.length === b.length ? -1 : a.length;
}

function rms(a: Float32Array): number {
  let s = 0;
  for (let i = 0; i < a.length; i++) s += a[i] * a[i];
  return Math.sqrt(s / Math.max(1, a.length));
}

for (const [name, place] of [
  ["a Game Boy now takes the frame-0 CC before the notes", () => void mgb(rig)],
  ["a NES takes the frame-0 CC at frame 0, as before", () => void rig.add(ROMS.bliptoaster)],
] as const) {
  test(`plugin-style host MIDI reaches each system in frame order: ${name}`, () => {
    const plugin = render(place, PLUGIN_STYLE);
    const inOrder = render(place, IN_ORDER);
    const ccLast = render(place, CC_LAST);
    expect(rms(plugin), "the notes sound").toBeGreaterThan(0.001);
    expect(firstDifference(plugin, inOrder), "the first frame the plugin-style staging renders otherwise").toBe(-1);
    // From the first note on: before it, the CC has nothing to change.
    expect(firstDifference(plugin, ccLast)).toBeGreaterThanOrEqual(100);
  });
}
