// Host blocks the rest of the matrix never runs: 1-frame blocks, and the ragged splits FL Studio hands a
// plugin. FL splits a host buffer at every automation and tempo point, so one buffer can arrive as blocks of
// 256, 33 or 1 frame, and an event's frame is an offset inside the block it lands in. Recorded on the engine
// before B3a moves the kernel's I/O onto wires (docs/modular-graph-phase-b3a-design.md sections 6 and 7.1),
// so B3a is held to it bit for bit. A case renders all of itself, boot included, in one TINY_SPLITS entry,
// and its edge segment places MIDI, automation CCs and (in the sync groups) transport points on block edges: a
// block's first frame, its last, and a 1-frame block's only frame, which is both. The phrase segment's MIDI is
// the shared one and lands inside blocks.
//
// The mgb and nes cases are here (blocks-tiny-mgb.test.ts, blocks-tiny-nes.test.ts); the scenes whose timing
// is a sync protocol, which need the sibling fixtures, are blocks-tiny-lsdj.test.ts and blocks-tiny-risa.test.ts.
import type { ParityCase } from "./group";
import { cycle, even, type Segment } from "./rig";
import { BOOT_MS, frames, mgb, MGB_CHANNELS, NES_CHANNELS, phrase, ROMS, type Blocks } from "./scenes";

/** The design's ragged list (section 7.1). No buffer grid at all: 1-frame blocks between odd sizes, and a
 *  4096-frame block next to a 2-frame one, the largest swing in size from one block to the next, which is
 *  what a core's per-block run-ahead and MI.OUT's one-block latency move with. It sums to 4362 frames, a
 *  multiple of neither a 24-PPQN tick nor a console frame, so its edges drift against both from one cycle to
 *  the next rather than meeting them at one phase. */
export const FL_RAGGED = [33, 1, 222, 1, 7, 4096, 2];

/** FL's own shape: a fixed 256-frame buffer, split inside at automation points and never across one, as five
 *  buffers [256] [33 1 222] [255 1] [1 255] [7 249]. So the edges keep a 256-frame grid, and a buffer's last
 *  frame and the next buffer's first frame are each a 1-frame block, back to back. */
export const FL_BUFFER = [256, 33, 1, 222, 255, 1, 1, 255, 7, 249];

/** Every tiny-block partition, by name (the case-name suffix). Each segment's list starts its cycle afresh,
 *  so a segment's frame 0 is the first frame of a 33-frame block (`fl`) or of a 256-frame one (`fl256`). */
export const TINY_SPLITS: [string, Blocks][] = [
  ["1", (n) => even(n, 1)],
  ["fl", (n) => cycle(n, FL_RAGGED)],
  ["fl256", (n) => cycle(n, FL_BUFFER)],
];

/** Block kinds `Edges.next` looks for, by size. */
export const ONE = (n: number): boolean => n === 1;
export const SMALL = (n: number): boolean => n >= 2 && n <= 7;
export const BUFFER = (n: number): boolean => n >= 200 && n < 1024;
export const BIG = (n: number): boolean => n >= 1024;
export const ANY = (): boolean => true;

/** One segment's block list, and where its blocks' edges are. */
export class Edges {
  private readonly starts: number[] = [];

  constructor(readonly blocks: number[]) {
    let at = 0;
    for (const n of blocks) {
      this.starts.push(at);
      at += n;
    }
  }

  /** The index of the first block starting at or after frame `from` whose size passes `kinds[0]`; if no such
   *  block exists, the first passing `kinds[1]`, and so on. So `next(f, BIG, BUFFER, ANY)` is a 4096 block in
   *  `fl`, a buffer-sized one in `fl256` (it has no big block) and just the next block in `1`. */
  next(from: number, ...kinds: ((size: number) => boolean)[]): number {
    for (const kind of kinds) {
      const i = this.starts.findIndex((s, b) => s >= from && kind(this.blocks[b]));
      if (i >= 0) return i;
    }
    throw new Error(`no block of the kinds asked for starts at or after frame ${from}`);
  }

  /** Block `i`'s first frame, in segment frames. */
  first(i: number): number {
    if (i >= this.blocks.length) throw new Error(`block ${i} is past the segment's ${this.blocks.length} blocks`);
    return this.starts[i];
  }

  /** Block `i`'s last frame (its first, for a 1-frame block). */
  last(i: number): number {
    return this.first(i) + this.blocks[i] - 1;
  }

  /** The first frame of the next 1-frame block at or after frame `from`: `from` is a floor, not the point (in
   *  `fl` the first 1-frame block at or after frame 300 starts at frame 4395, 92 ms in). */
  nextOneFrame(from: number): number {
    return this.first(this.next(from, ONE));
  }
}

export type TinySystem = "mgb" | "nes";

/** The automation CC each system's edge segment moves, on its two pulse voices (channels 0 and 1), with the
 *  three values it sets: mGB's pan (CC10: 0 left, 127 right, 64 both), BlipToaster's volume (CC7). Both
 *  also take CC1 (duty) on those voices, which the ramp moves. */
const AUTOMATION: Record<TinySystem, { cc: number; values: [number, number, number] }> = {
  mgb: { cc: 10, values: [0, 127, 64] },
  nes: { cc: 7, values: [127, 40, 80] },
};

/** CC1 values for the ramp: up through the four duties and back, so neighbours always differ. */
const DUTY_RAMP = [0, 40, 80, 120, 80, 40, 0, 120];

/** 700 ms of notes and automation CCs on block edges, each event at the next block of a kind after a point in
 *  the segment (in the 1-frame split every block is of every kind, so each lands on the next frame):
 *  - a note and an automation CC on one frame of a 1-frame block, the note first, as the plugin stages them (the
 *    host's events, then each moved parameter's CC at frame 0);
 *  - automation at a big block's frame 0, and a note-on on its last frame (a buffer-sized block in `fl256`;
 *    in `1` the same frame, so the CC comes first there. Both orders of a CC and a note on one frame are
 *    deliberate, and the sort is stable, so each is kept as listed);
 *  - an automation ramp, a CC at frame 0 of each of up to 32 consecutive blocks, which is what a host that
 *    splits at every automation point delivers (in `1`, a CC on each of 32 frames in a row);
 *  - a CC in a 1-frame block, and a note-off on the next block's first frame;
 *  - automation at a small block's first frame (2 to 7 frames), a note-off on its last;
 *  - a note-on in a 1-frame block, its note-off on the last frame of the next buffer-sized block;
 *  - a roll on the noise voice (channel 3): a note at the first frame of the next block every 1.5 ms, the keys
 *    alternating by the index of the 1.5 ms grid step, not of the note emitted, so where steps share a block the
 *    sequence repeats a key (in `fl` and `fl256`).
 *  Collisions, kept on purpose: in the 44.1 kHz `fl` cases the ramp is only 3 CCs and its third shares the big
 *  block's frame 0 with that block's automation CC; and the roll's last note-on lands on the frame of the note-offs
 *  (a zero-length note).
 *  Each of them is heard: removing any one changes the case's digest. Moving one by a frame mostly is not:
 *  mGB takes a lone message at its own pace, and a lone note-on moved by a frame changes BlipToaster's output
 *  only once every ~67 frames at 48 kHz. In the 1-frame split a frame is what one block of latency moves an
 *  event by, so the ramp and the roll are dense on purpose, and mGB hears the segment moved: every event of it
 *  moved by one frame, either way, changes its digest. BlipToaster does not, so on its path a block of latency
 *  is pinned by the ragged splits, where most frames sit in blocks far longer than one frame. */
function edgeSegment(sr: number, split: Blocks, system: TinySystem): Segment {
  const e = new Edges(split(frames(sr, 700)));
  const ms = (t: number) => frames(sr, t);
  const { cc, values } = AUTOMATION[system];
  const midi: { at: number; bytes: number[] }[] = [];
  const at = (frame: number, ...messages: number[][]) => messages.forEach((bytes) => midi.push({ at: frame, bytes }));

  const one = e.next(ms(20), ONE);
  at(e.first(one), [0x90, 64, 100], [0xb0, cc, values[0]]);

  const big = e.next(ms(120), BIG, BUFFER, ANY);
  at(e.first(big), [0xb0, cc, values[1]]);
  at(e.last(big), [0x91, 71, 100]);

  const ramp = e.next(ms(200), ANY);
  for (let i = 0; i < 32 && e.first(ramp + i) < ms(280); i++) at(e.first(ramp + i), [0xb0, 1, DUTY_RAMP[i % DUTY_RAMP.length]]);

  const lone = e.next(ms(300), ONE);
  at(e.first(lone), [0xb1, 1, 96]);
  at(e.first(lone + 1), [0x80, 64, 0]);

  const small = e.next(ms(380), SMALL, ANY);
  at(e.first(small), [0xb1, cc, values[2]]);
  at(e.last(small), [0x81, 71, 0]);

  const last = e.next(ms(450), ONE);
  at(e.first(last), [0x90, 69, 90]);
  at(e.last(e.next(e.first(last) + 1, BUFFER, ANY)), [0x80, 69, 0]);

  let prev = -1;
  for (let t = 520, k = 0; t < 620; t += 1.5, k++) {
    const b = e.next(ms(t), ANY);
    if (b === prev) continue;
    prev = b;
    at(e.first(b), [0x93, k % 2 ? 52 : 40, 100]);
  }
  at(e.first(e.next(ms(640), ANY)), [0x83, 40, 0], [0x83, 52, 0]);

  return { blocks: e.blocks, midi };
}

function tinyCase(system: TinySystem, sr: number, split: string, blocks: Blocks): ParityCase {
  return {
    name: `${system}-${sr}-${split}`,
    sampleRate: sr,
    outputs: 2,
    audible: [0, 1],
    setup: system === "mgb" ? (rig) => void mgb(rig) : (rig) => void rig.add(ROMS.bliptoaster),
    segments: [
      { blocks: blocks(frames(sr, BOOT_MS)) },
      { blocks: blocks(frames(sr, 600)), midi: phrase(sr, system === "mgb" ? MGB_CHANNELS : NES_CHANNELS) },
      edgeSegment(sr, blocks, system),
    ],
  };
}

export function tinyCases(system: TinySystem): ParityCase[] {
  return [44100, 48000].flatMap((sr) => TINY_SPLITS.map(([split, blocks]) => tinyCase(system, sr, split, blocks)));
}
