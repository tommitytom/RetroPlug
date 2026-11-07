// Unit tests for the Arduinoboy MIDIOUT (MI.OUT) decoder (lsdjArduinoboy.ts): the TS port of the
// native ArduinoboyMaster role. Two layers:
//   - arduinoboyDecodeByte: the byte protocol. These 11 cases are a direct port of the native
//     packages/native/test/ArduinoboyMasterTests.cpp (same bytes in, same MIDI out).
//   - arduinoboyDecodeSerialOut: the flag-gated framing in front of it: the raw captured bytes LSDj
//     actually emits (each wire frame = 1 flag bit + 7 payload bits, MSB-first), decoded to the same MIDI.
// Then master sync's decoder, arduinoboyMasterSyncBlock. Each decoder hands every message to its emit with its byte's
// frame, and adds no dependence on the host's block split of its own: one byte stream cut into any blocks sends the
// same messages at the same absolute frames. The rp.lsdj.serial-out node that runs them off its Game Boy's serial out
// (modes 7 and 8) is lsdjSerialOut.test.ts's.
import { test, expect } from "@retroplug/testing";
import {
  arduinoboyDecodeByte,
  arduinoboyDecodeSerialOut,
  arduinoboyMasterSyncBlock,
  arduinoboyReset,
  type ArduinoboyState,
  type MasterSyncState,
  type MidiEmit,
} from "../lsdjArduinoboy";

/** An emit recording each message's bytes into `out`, and its frame into `frames` when given. */
const into = (out: number[][], frames?: number[]): MidiEmit => (frame, len, b0, b1, b2) => {
  out.push([b0, b1, b2].slice(0, len));
  frames?.push(frame);
};

// Feed a run of protocol bytes through a fresh decoder, returning the emitted MIDI messages.
function decode(bytes: number[]): number[][] {
  const out: number[][] = [];
  const state: ArduinoboyState = {};
  bytes.forEach((b, i) => arduinoboyDecodeByte(b, i, state, into(out)));
  return out;
}

// --- arduinoboyDecodeByte: the 11 ArduinoboyMasterTests.cpp cases -------------------------------

test("ArduinoboyMaster: 0x7F → MIDI clock tick (0xF8)", () => {
  const evs = decode([0x7f, 0x7f, 0x7f]);
  expect(evs.length).toBe(3);
  for (const e of evs) expect(e).toEqual([0xf8]);
});

test("ArduinoboyMaster: 0x7D → start (0xFA), 0x7E → stop (0xFC)", () => {
  const evs = decode([0x7d, 0x7f, 0x7f, 0x7e]);
  expect(evs.map((e) => e[0])).toEqual([0xfa, 0xf8, 0xf8, 0xfc]);
});

test("ArduinoboyMaster: 0x70..0x73 + value → NoteOn ch 0..3", () => {
  expect(decode([0x70, 60])).toEqual([[0x90, 60, 0x7f]]); // ch 0, middle C
  expect(decode([0x73, 72])).toEqual([[0x93, 72, 0x7f]]); // ch 3
});

test("ArduinoboyMaster: 0x7X note command with value 0 → NoteOff", () => {
  expect(decode([0x71, 0])).toEqual([[0x81, 0, 0]]); // NoteOff ch 1
});

test("ArduinoboyMaster: 0x74..0x77 + value → Control Change", () => {
  expect(decode([0x74, 64])).toEqual([[0xb0, 4, 64]]); // CC ch 0, CC# = m (simplified), value 64
});

test("ArduinoboyMaster: 0x78..0x7B + value → Program Change", () => {
  expect(decode([0x7a, 7])).toEqual([[0xc2, 7]]); // PC ch 2, patch 7
});

test("ArduinoboyMaster: realtime bytes interleave with command/value pairs", () => {
  const evs = decode([0x7d, 0x7f, 0x71, 60, 0x7f, 0x7e]);
  expect(evs).toEqual([[0xfa], [0xf8], [0x91, 60, 0x7f], [0xf8], [0xfc]]);
});

test("ArduinoboyMaster: value bytes without a pending command are dropped", () => {
  expect(decode([0x40, 0x7f])).toEqual([[0xf8]]); // 0x40 dropped, only the clock survives
});

test("ArduinoboyMaster: high-bit bytes (>=0x80) are ignored", () => {
  expect(decode([0xff, 0xaa, 0x55, 0xd5, 0x7f])).toEqual([[0xf8]]);
});

test("ArduinoboyMaster: reset clears pending state mid-command", () => {
  const out: number[][] = [];
  const state: ArduinoboyState = {};
  arduinoboyDecodeByte(0x71, 0, state, into(out)); // pending NoteOn ch 1
  expect(out.length).toBe(0);
  arduinoboyReset(state);
  arduinoboyDecodeByte(60, 0, state, into(out)); // would complete the note; now dropped
  expect(out.length).toBe(0);
  arduinoboyDecodeByte(0x7f, 0, state, into(out)); // realtime still works
  expect(out).toEqual([[0xf8]]);
});

test("ArduinoboyMaster: command 0x7C consumes its value but emits nothing (m=0xC undefined)", () => {
  expect(decode([0x7c, 42, 0x7f])).toEqual([[0xf8]]); // only the clock survives
});

// --- arduinoboyDecodeSerialOut: the flag-gated framing --------------------------------------------

// Flag-frame a protocol byte the way LSDj's wire does: a 1 flag bit then the 7 payload bits (MSB-first),
// packed into a single captured 8-bit byte (so a data byte reads as 0x80 | payload).
const framed = (cmd: number): number => 0x80 | (cmd & 0x7f);

// A byte every eight frames, as SameBoy clocks MI.OUT's port (one bit an audio frame).
function decodeSerial(bytes: number[]): number[][] {
  const out: number[][] = [];
  const state: ArduinoboyState = {};
  arduinoboyDecodeSerialOut(bytes, bytes.map((_, i) => 8 * i + 7), state, into(out));
  return out;
}

test("framing: a stream of framed protocol bytes decodes to the same MIDI as the raw bytes", () => {
  // START, clock, NoteOn ch0 note 0x69, clock — each as one flag-framed captured byte.
  const evs = decodeSerial([framed(0x7d), framed(0x7f), framed(0x70), framed(0x69), framed(0x7f)]);
  expect(evs).toEqual([[0xfa], [0xf8], [0x90, 0x69, 0x7f], [0xf8]]);
});

test("framing: idle (flag=0) bits between wire frames are skipped", () => {
  // 0x00 = eight idle bits (flag 0 each); they carry no command and must not corrupt the stream.
  const evs = decodeSerial([0x00, framed(0x7f), 0x00, framed(0x7d)]);
  expect(evs).toEqual([[0xf8], [0xfa]]);
});

test("framing: a wire frame straddling a captured-byte boundary buffers, then decodes once", () => {
  // Bit stream (MSB-first): one idle bit (flag 0), then a framed clock (flag 1 + 7 payload = 0x7F):
  // 9 bits total, so the wire frame's final payload bit lands in the SECOND captured byte. First byte
  // 0x7F = `0 1111111` (idle + flag + 6 payload bits); second byte 0x80 = `1 0000000` (last payload
  // bit + idle padding). The decoder must buffer the partial wire frame across the two calls and emit once.
  const out: number[][] = [];
  const state: ArduinoboyState = {};
  arduinoboyDecodeSerialOut([0x7f], [0], state, into(out));
  expect(out.length).toBe(0); // partial wire frame buffered, nothing decoded yet
  arduinoboyDecodeSerialOut([0x80], [0], state, into(out));
  expect(out).toEqual([[0xf8]]); // the straddling clock's wire frame completes exactly once
});

test("framing: each message goes at the frame of the captured byte whose bits complete it", () => {
  // START, clock, then a NoteOn whose command byte and value byte are two captured bytes: it completes at the value's.
  const out: number[][] = [];
  const frames: number[] = [];
  const bytes = [framed(0x7d), framed(0x7f), framed(0x70), framed(0x69)];
  arduinoboyDecodeSerialOut(bytes, [7, 15, 23, 31], {}, into(out, frames));
  expect(out).toEqual([[0xfa], [0xf8], [0x90, 0x69, 0x7f]]);
  expect(frames).toEqual([7, 15, 31]);
});

test("framing: a wire frame straddling two calls goes at its completing byte's frame in the second", () => {
  // The straddling clock above, its first byte at frame 1000 of one block and its second at frame 3 of the next; and a
  // NoteOn whose command ends one call and whose value starts the next, at the value's frame there.
  const out: number[][] = [];
  const frames: number[] = [];
  const state: ArduinoboyState = {};
  arduinoboyDecodeSerialOut([0x7f], [1000], state, into(out, frames));
  arduinoboyDecodeSerialOut([0x80, framed(0x70)], [3, 11], state, into(out, frames));
  arduinoboyDecodeSerialOut([framed(60)], [5], state, into(out, frames));
  expect(out).toEqual([[0xf8], [0x90, 60, 0x7f]]);
  expect(frames).toEqual([3, 5]);
});

// --- Master Sync (mode 8, SYNC=LSDJ): each byte a clock, a silence the stop ------------------------------------

const RATE = 44100;
/** MASTER_SYNC_STOP_MS (200 ms) at RATE. */
const STOP_FRAMES = 8820;
const START = [0xfa];
const STOP = [0xfc];
const CLOCK = [0xf8];
const row = (r: number): number[] => [0x90, r, 0x7f];
const clocksOf = (n: number): number[][] => Array.from({ length: n }, () => CLOCK);

/** A byte timeline: [absolute frame, byte], ascending. */
type Timeline = [number, number][];
/** A playing LSDj's clock bytes at tempo 128, 860 frames apart at 44.1 kHz: `n` of `byte`, the first at `at`. */
const clocks = (at: number, n: number, byte = 9): Timeline => Array.from({ length: n }, (_, i): [number, number] => [at + i * 860, byte]);

/** `total` frames in blocks of `size` (the last one short). */
const even = (size: number) => (total: number): number[] => {
  const out: number[] = [];
  for (let at = 0; at < total; at += size) out.push(Math.min(size, total - at));
  return out;
};
/** `total` frames in blocks cycling through `sizes` (the last one short). */
const cycle = (sizes: number[]) => (total: number): number[] => {
  const out: number[] = [];
  for (let at = 0, i = 0; at < total; i++) {
    out.push(Math.min(sizes[i % sizes.length], total - at));
    at += out[out.length - 1];
  }
  return out;
};
// The FL-style splits the parity matrix runs (test-native/parity/blocks-tiny.ts): ragged, and a 256-frame buffer
// split at automation points.
const FL_RAGGED = [33, 1, 222, 1, 7, 4096, 2];
const FL_BUFFER = [256, 33, 1, 222, 255, 1, 1, 255, 7, 249];

/** A message as a split decoder sent it: the block it went out in, its frame there, that frame counted from the
 *  timeline's start, and whether the frame lies inside its block. */
interface Sent {
  block: number;
  frame: number;
  at: number;
  inside: boolean;
  data: number[];
}

/** Decode `timeline` in the blocks `split` cuts `total` frames into, each block's bytes handed to the decoder in that
 *  block at their frames in it, as rp.lsdj.serial-out reads its Game Boy's serial out after the system. `rate` is every
 *  block's, or each block's by its index. */
function masterSync(
  timeline: Timeline,
  split: (total: number) => number[],
  total: number,
  rate: number | ((block: number) => number) = RATE,
): Sent[] {
  const out: Sent[] = [];
  const state: MasterSyncState = {};
  let start = 0;
  let next = 0;
  split(total).forEach((n, block) => {
    const bytes: number[] = [];
    const frames: number[] = [];
    for (; next < timeline.length && timeline[next][0] < start + n; next++) {
      frames.push(timeline[next][0] - start);
      bytes.push(timeline[next][1]);
    }
    const sampleRate = typeof rate === "number" ? rate : rate(block);
    arduinoboyMasterSyncBlock(bytes, frames, { frames: n, sampleRate }, state, (frame, len, b0, b1, b2) =>
      out.push({ block, frame, at: start + frame, inside: frame >= 0 && frame < n, data: [b0, b1, b2].slice(0, len) }),
    );
    start += n;
  });
  return out;
}
const sent = (out: { data: number[] }[]): number[][] => out.map((m) => m.data);
/** Each message at its absolute frame. */
const timed = (out: Sent[]): [number, number[]][] => out.map((m) => [m.at, m.data]);

test("MasterSync: every byte is a clock, and the first opens the run with the song-row NoteOn and 0xFA", () => {
  expect(sent(masterSync(clocks(100, 3, 5), even(1024), 4096))).toEqual([row(5), START, CLOCK, CLOCK, CLOCK]);
});

test("MasterSync: a silence longer than 200 ms closes the run with one 0xFC, a shorter one does not", () => {
  // 8000 frames between the first two bytes, then 21900 before a third, then the end.
  const out = sent(masterSync([[100, 9], [8100, 9], [30000, 7]], even(1024), 50000));
  expect(out).toEqual([row(9), START, CLOCK, CLOCK, STOP, row(7), START, CLOCK, STOP]);
});

test("MasterSync: before any byte, and while stopped, nothing is sent", () => {
  expect(sent(masterSync([], even(1024), 20000))).toEqual([]);
  expect(sent(masterSync([[100, 9]], even(1024), 40000))).toEqual([row(9), START, CLOCK, STOP]);
});

test("MasterSync: the 0xFC goes at the run's mark, the first frame its silence passes 200 ms, in the block that holds it", () => {
  // A byte at frame 100: the silence passes STOP_FRAMES after frame 100 + STOP_FRAMES = 8920, so the mark is 8921.
  const stopOf = (out: Sent[]) => out.find((m) => m.data[0] === 0xfc);
  expect(stopOf(masterSync([[100, 9]], even(1024), 20000))).toEqual({ block: 8, frame: 729, at: 8921, inside: true, data: STOP });
  expect(stopOf(masterSync([[100, 9]], even(1), 10000))).toEqual({ block: 8921, frame: 0, at: 8921, inside: true, data: STOP });
});

test("MasterSync: the silence is time, so 10000 frames is a stop at 44.1 kHz and not at 96 kHz", () => {
  const timeline: Timeline = [[100, 1], [10100, 1]];
  expect(sent(masterSync(timeline, even(1024), 12000, 44100))).toEqual([row(1), START, CLOCK, STOP, row(1), START, CLOCK]);
  expect(sent(masterSync(timeline, even(1024), 12000, 96000))).toEqual([row(1), START, CLOCK, CLOCK]);
});

test("MasterSync: one timeline decodes to the same messages however the host cuts its blocks", () => {
  // A run of 80 clocks, LSDj stopped for about a second, a run of 40, and LSDj stopped again.
  const timeline: Timeline = [...clocks(5000, 80), ...clocks(120000, 40, 3)];
  const expected = [row(9), START, ...clocksOf(80), STOP, row(3), START, ...clocksOf(40), STOP];
  for (const split of [even(1), cycle(FL_RAGGED), cycle(FL_BUFFER), even(1024), even(8192)])
    expect(sent(masterSync(timeline, split, 170000))).toEqual(expected);
});

test("MasterSync: a silence that ends inside one call still stops the run, however the host cuts its blocks", () => {
  // The silence before frame 18100 passes 200 ms at frame 16921, and no 4096- or 8192-frame block ends between that
  // and the byte that ends it, so the byte reaches the decoder in the same call as the silence's mark.
  const timeline: Timeline = [[100, 1], [8100, 1], [18100, 2], [40000, 3]];
  const expected = [row(1), START, CLOCK, CLOCK, STOP, row(2), START, CLOCK, STOP, row(3), START, CLOCK, STOP];
  for (const split of [even(1), cycle(FL_RAGGED), cycle(FL_BUFFER), even(1024), even(4096), even(8192)])
    expect(sent(masterSync(timeline, split, 60000))).toEqual(expected);
});

test("MasterSync: the checks before a byte and at a block's end share the stop's boundary: exactly 200 ms is no stop, a frame more is", () => {
  // The second byte comes exactly STOP_FRAMES after the first, before its mark; the third STOP_FRAMES + 1 after the
  // second, on its mark. So the check before the third sends the stop, at the third's frame, wherever the blocks are
  // cut: a block that ends on the mark carries it to the next, whose first frame the third byte is.
  const timeline: Timeline = [[100, 1], [100 + STOP_FRAMES, 1], [100 + 2 * STOP_FRAMES + 1, 2]];
  const expected = [row(1), START, CLOCK, CLOCK, STOP, row(2), START, CLOCK, STOP];
  for (const split of [even(1), cycle(FL_RAGGED), cycle(FL_BUFFER), even(1024), even(4096), even(8192)]) {
    const out = masterSync(timeline, split, 40000);
    expect(sent(out)).toEqual(expected);
    expect(out.filter((m) => m.data[0] === 0xfc).map((m) => m.at)).toEqual([100 + 2 * STOP_FRAMES + 1, 100 + 3 * STOP_FRAMES + 2]);
  }
});

test("MasterSync: each byte's messages go at its frame, in its own block, the run's NoteOn and 0xFA at its first byte's", () => {
  // Bytes at frames 100, 960 and 1820 of 1024-frame blocks: each reaches the decoder in the block that captured it.
  const out = masterSync([[100, 5], [960, 5], [1820, 5]], even(1024), 4096);
  expect(out.slice(0, 5).map(({ block, frame, data }) => ({ block, frame, data }))).toEqual([
    { block: 0, frame: 100, data: row(5) },
    { block: 0, frame: 100, data: START },
    { block: 0, frame: 100, data: CLOCK },
    { block: 0, frame: 960, data: CLOCK },
    { block: 1, frame: 796, data: CLOCK },
  ]);
});

test("MasterSync: a run stops at its mark mid-block", () => {
  // One byte at frame 100 of a 16384-frame block: its mark, 8921, is inside the same block.
  expect(masterSync([[100, 1]], even(16384), 16384)).toEqual([
    { block: 0, frame: 100, at: 100, inside: true, data: row(1) },
    { block: 0, frame: 100, at: 100, inside: true, data: START },
    { block: 0, frame: 100, at: 100, inside: true, data: CLOCK },
    { block: 0, frame: 8921, at: 8921, inside: true, data: STOP },
  ]);
});

test("MasterSync: a mark at a block's length goes at the next block's first frame", () => {
  // A byte at frame 395: its mark is 9216, block 8's length past its start, so block 8 carries it and block 9 sends it
  // at its frame 0, with no byte there to send it before.
  const out = masterSync([[395, 1]], even(1024), 12288);
  expect(out[out.length - 1]).toEqual({ block: 9, frame: 0, at: 9216, inside: true, data: STOP });
  expect(out.length).toBe(4);
});

test("MasterSync: a run's mark is carried across a block with no bytes", () => {
  // A byte at frame 100 of a 4096-frame block: block 1 holds no byte and not the mark, which it carries; block 2
  // sends it at 8921 - 8192.
  const out = masterSync([[100, 1]], even(4096), 16384);
  expect(out.map(({ block, frame, data }) => [block, frame, data])).toEqual([
    [0, 100, row(1)],
    [0, 100, START],
    [0, 100, CLOCK],
    [2, 729, STOP],
  ]);
});

test("MasterSync: a restart in the block of its stop sends the 0xFC at the mark, then the NoteOn, 0xFA and 0xF8 at the byte", () => {
  // The silence after frame 100 passes 200 ms at 8921, inside the 16384-frame block whose byte at 16000 ends it: the
  // check before that byte sends the stop at the mark, and the byte opens a fresh run at its own frame. That run's
  // mark, 24821, is past the block, so the next block sends it, at 24821 - 16384.
  expect(masterSync([[100, 1], [16000, 2]], even(16384), 49152).map(({ block, frame, data }) => [block, frame, data])).toEqual([
    [0, 100, row(1)],
    [0, 100, START],
    [0, 100, CLOCK],
    [0, 8921, STOP],
    [0, 16000, row(2)],
    [0, 16000, START],
    [0, 16000, CLOCK],
    [1, 8437, STOP],
  ]);
});

// A host can change its rate between two blocks (a DAW's project rate, the standalone's settings), and the node, and so
// a run, lives through it. The silence a run carries is time, so 200 ms stays 200 ms at the new rate.

test("MasterSync: a run carried over a drop in the host's rate stops 200 ms after its byte, inside the new rate's block", () => {
  // A byte at frame 100 of 1024-frame blocks at 48 kHz, and from block 9 at 44.1 kHz. At block 9's start the silence
  // is 9116 frames at 48 kHz (189.9 ms), 8375.3 at 44.1 kHz, so it passes 200 ms, 8820 frames, at block 9's frame 445.
  const out = masterSync([[100, 1]], even(1024), 16384, (block) => (block < 9 ? 48000 : 44100));
  expect(out.map(({ block, frame, inside, data }) => [block, frame, inside, data])).toEqual([
    [0, 100, true, row(1)],
    [0, 100, true, START],
    [0, 100, true, CLOCK],
    [9, 445, true, STOP],
  ]);
});

test("MasterSync: a run carried over a rise in the host's rate stops 200 ms after its byte, not later", () => {
  // A byte at frame 100 of 1024-frame blocks at 44.1 kHz, and from block 8 at 48 kHz. At block 8's start the silence
  // is 8092 frames at 44.1 kHz (183.5 ms), 8807.6 at 48 kHz, so it passes 200 ms, 9600 frames, at block 8's frame 793.
  const out = masterSync([[100, 1]], even(1024), 16384, (block) => (block < 8 ? 44100 : 48000));
  expect(out.map(({ block, frame, inside, data }) => [block, frame, inside, data])).toEqual([
    [0, 100, true, row(1)],
    [0, 100, true, START],
    [0, 100, true, CLOCK],
    [8, 793, true, STOP],
  ]);
});

test("MasterSync: two rate changes inside one silence leave the stop at the first frame past 200 ms", () => {
  // A byte at frame 100 of 1024-frame blocks at 48 kHz, with block 3 alone at 44.1 kHz. Block 3 is 1024 x 48000 /
  // 44100, about 1114.558 frames at 48 kHz, so block 9 starts about 9206.558 frames after the byte and the silence
  // passes 9600 frames, 200 ms, at about its frame 393.442: the first frame past that is 394. Rescaling a floored
  // position at each change moves the mark to 393, a frame early.
  const out = masterSync([[100, 1]], even(1024), 16384, (block) => (block === 3 ? 44100 : 48000));
  expect(out.filter((m) => m.data[0] === 0xfc).map(({ block, frame }) => [block, frame])).toEqual([[9, 394]]);
});

test("MasterSync: a stop a rise in the rate leaves overdue at a block's start goes at its first frame", () => {
  // A byte at frame 395 of 1024-frame blocks at 44.1 kHz marks block 9's frame 0, as above, and block 9 is at 96 kHz.
  // Its silence passed 200 ms one 44.1 kHz frame before the block's start, 2.2 frames at 96 kHz, so the finer grid
  // marks frame -2: the 0xFC goes at the block's first frame instead, where the old rate had it, and before a byte
  // there.
  const rate = (block: number) => (block < 9 ? 44100 : 96000);
  const stops = (timeline: Timeline) =>
    masterSync(timeline, even(1024), 12288, rate)
      .filter((m) => m.data[0] === 0xfc)
      .map(({ block, frame, inside }) => [block, frame, inside]);
  expect(stops([[395, 1]])).toEqual([[9, 0, true]]);
  const restart = masterSync([[395, 1], [9216, 2]], even(1024), 12288, rate).slice(3, 7);
  expect(restart.map(({ block, frame, data }) => [block, frame, data])).toEqual([
    [9, 0, STOP],
    [9, 0, row(2)],
    [9, 0, START],
    [9, 0, CLOCK],
  ]);
});

// --- the decoders add no split dependence of their own ----------------------------------------------------------
//
// A core's byte timing moves with the host's block split (test-native/lsdj-mastersync.test.ts says so), and that is the
// core's. What the decoders are held to here is that they add none: one byte stream at absolute frames, cut into
// 1-frame blocks, FL-style ragged ones (some longer than a run's gaps, some shorter), a 256-frame buffer split at
// automation points and 1024-frame blocks, sends the same messages at the same absolute frames, each inside the block
// it goes out in.

/** Block lists the property cases cut a stream into: ragged lengths below and above a clock's 860-frame gap and the
 *  stop's 8820, among them. */
const SPLITS: [string, (total: number) => number[]][] = [
  ["1", even(1)],
  ["fl", cycle(FL_RAGGED)],
  ["fl256", cycle(FL_BUFFER)],
  ["ragged", cycle([5000, 13, 777, 9000, 1, 861, 12000, 3])],
  ["1024", even(1024)],
];

test("MasterSync: one byte stream sends the same messages at the same absolute frames however the host cuts its blocks", () => {
  // Runs, silences past 200 ms and restarts: 80 clocks; a second's silence; 40; a lone byte; a restart on its mark
  // exactly; a gap of exactly 200 ms inside a run; and a run left open at the end, its mark past the stream.
  const last1 = 5000 + 79 * 860;
  const lone = 170000;
  const onMark = lone + STOP_FRAMES + 1;
  const timeline: Timeline = [
    ...clocks(5000, 80),
    ...clocks(120000, 40, 3),
    [lone, 4],
    ...clocks(onMark, 5, 6),
    [onMark + 4 * 860 + STOP_FRAMES, 6],
    ...clocks(215000, 3, 7),
  ];
  const total = 220000;
  const runs = SPLITS.map(([name, split]) => {
    const out = masterSync(timeline, split, total);
    expect(out.every((m) => m.inside), `${name}: every frame inside its block`).toBe(true);
    return { name, out: timed(out) };
  });
  for (const { name, out } of runs) expect(out, name).toEqual(runs[0].out);
  // And that one list is the rule's: each byte's messages at its frame, each stop at its run's mark.
  const at = (frame: number, data: number[]): [number, number[]] => [frame, data];
  const run = (t: Timeline): [number, number[]][] => t.flatMap(([f, b], i) => (i === 0 ? [at(f, row(b)), at(f, START), at(f, CLOCK)] : [at(f, CLOCK)]));
  const stopAfter = (frame: number) => at(frame + STOP_FRAMES + 1, STOP);
  const second = clocks(120000, 40, 3);
  const fourth: Timeline = [...clocks(onMark, 5, 6), [onMark + 4 * 860 + STOP_FRAMES, 6]];
  expect(runs[0].out).toEqual([
    ...run(clocks(5000, 80)),
    stopAfter(last1),
    ...run(second),
    stopAfter(second[second.length - 1][0]),
    ...run([[lone, 4]]),
    stopAfter(lone),
    ...run(fourth),
    stopAfter(fourth[fourth.length - 1][0]),
    ...run(clocks(215000, 3, 7)),
  ]);
});

/** An MI.OUT bit stream: each command a flag bit and its 7 payload bits, `idle` zero bits before it, MSB-first. */
function miOutBits(commands: [number, number][]): number[] {
  const bits: number[] = [];
  for (const [idle, cmd] of commands) {
    for (let k = 0; k < idle; k++) bits.push(0);
    bits.push(1);
    for (let k = 6; k >= 0; k--) bits.push((cmd >> k) & 1);
  }
  while (bits.length % 8 !== 0) bits.push(0);
  return bits;
}

test("MI.OUT: one captured stream sends the same messages at the same absolute frames however the host cuts its blocks", () => {
  // A byte every 8 frames from frame 3 (MI.OUT's port, a bit a frame), the commands off the byte grid by the idle bits
  // before them, so most straddle two captured bytes and, cut into short blocks, two blocks: clocks, START, a NoteOn
  // and its NoteOff, a CC, a program change, STOP. Seventy times over, each time a bit later against the byte grid (it
  // is 145 bits long), so the stream spans 10155 frames and every split cuts it into four blocks or more, each at
  // places of its own.
  const once: [number, number][] = [
    [0, 0x7d], [3, 0x7f], [0, 0x70], [1, 60], [5, 0x7f], [2, 0x74], [0, 64], [7, 0x70], [1, 0],
    [0, 0x78], [4, 9], [1, 0x7f], [0, 0x7e], [9, 0x7f],
  ];
  const REPEATS = 70;
  const commands = Array.from({ length: REPEATS }, () => once).flat();
  const bits = miOutBits(commands);
  const bytes: number[] = [];
  for (let i = 0; i < bits.length; i += 8) bytes.push(bits.slice(i, i + 8).reduce((v, b) => (v << 1) | b, 0));
  const frameOf = (i: number) => 3 + 8 * i;
  expect(frameOf(bytes.length)).toBe(10155);
  const total = frameOf(bytes.length) + 100;
  const runs = SPLITS.map(([name, split]) => {
    const out: [number, number[]][] = [];
    const state: ArduinoboyState = {};
    let start = 0;
    let next = 0;
    let inside = true;
    let spanned = 0; // blocks holding a byte of the stream
    for (const n of split(total)) {
      const blockBytes: number[] = [];
      const frames: number[] = [];
      for (; next < bytes.length && frameOf(next) < start + n; next++) {
        blockBytes.push(bytes[next]);
        frames.push(frameOf(next) - start);
      }
      if (blockBytes.length > 0) spanned++;
      arduinoboyDecodeSerialOut(blockBytes, frames, state, (frame, len, b0, b1, b2) => {
        inside = inside && frame >= 0 && frame < n;
        out.push([start + frame, [b0, b1, b2].slice(0, len)]);
      });
      start += n;
    }
    expect(inside, `${name}: every frame inside its block`).toBe(true);
    expect(spanned, `${name}: blocks the stream spans`).toBeGreaterThan(3);
    return { name, out };
  });
  for (const { name, out } of runs) expect(out, name).toEqual(runs[0].out);
  // Each message at the frame of the captured byte holding its last bit.
  const ends: number[] = [];
  let bit = 0;
  for (const [idle] of commands) ends.push((bit += idle + 8) - 1);
  const atBit = (k: number) => frameOf(Math.floor(ends[k] / 8));
  const pass = (r: number): [number, number[]][] => {
    const k = r * once.length;
    return [
      [atBit(k), [0xfa]],
      [atBit(k + 1), [0xf8]],
      [atBit(k + 3), [0x90, 60, 0x7f]],
      [atBit(k + 4), [0xf8]],
      [atBit(k + 6), [0xb0, 4, 64]],
      [atBit(k + 8), [0x80, 0, 0]],
      [atBit(k + 10), [0xc0, 9]],
      [atBit(k + 11), [0xf8]],
      [atBit(k + 12), [0xfc]],
      [atBit(k + 13), [0xf8]],
    ];
  };
  expect(runs[0].out).toEqual(Array.from({ length: REPEATS }, (_, r) => pass(r)).flat());
});

// --- the emit ---------------------------------------------------------------------------------------------------

test("the emit: each message's frame, its length and its bytes, 0 past its length, from either decoder", () => {
  // Every length each decoder emits: MI.OUT's realtime (1), program change (2), NoteOn, NoteOff and CC (3); master
  // sync's song-row NoteOn (3), and its 0xFA, clock and 0xFC (1).
  const args: number[][] = [];
  const record: MidiEmit = (...a) => args.push(a);
  const miOut = [0x7d, 0x78, 9, 0x71, 60, 0x71, 0, 0x75, 64, 0x7f, 0x7e].map(framed);
  arduinoboyDecodeSerialOut(miOut, miOut.map((_, i) => 8 * i + 7), {}, record);
  expect(args).toEqual([
    [7, 1, 0xfa, 0, 0],
    [23, 2, 0xc0, 9, 0],
    [39, 3, 0x91, 60, 0x7f],
    [55, 3, 0x81, 0, 0],
    [71, 3, 0xb1, 5, 64],
    [79, 1, 0xf8, 0, 0],
    [87, 1, 0xfc, 0, 0],
  ]);
  args.length = 0;
  arduinoboyMasterSyncBlock([5], [100], { frames: 16384, sampleRate: RATE }, {}, record);
  expect(args).toEqual([
    [100, 3, 0x90, 5, 0x7f],
    [100, 1, 0xfa, 0, 0],
    [100, 1, 0xf8, 0, 0],
    [8921, 1, 0xfc, 0, 0],
  ]);
});
