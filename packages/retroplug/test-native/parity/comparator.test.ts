// The parity comparator on its own: the smallest possible change to audio, MIDI out or SRAM must fail,
// and the report must name where. If this passes, a matching hash means matching output.
import { test, expect } from "../../testing/harness";
import { bytesDigest, compareDigests, laneDigest, midiDigest, stableJson, type CaseDigest, type MidiOutEntry } from "./digest";

function lanes(seed: number): Float32Array[] {
  return [0, 1, 2].map((l) => Float32Array.from({ length: 256 }, (_, i) => Math.sin((i + 1) * (seed + l + 1) * 0.01)));
}

function digestOf(segs: Float32Array[][], midi: MidiOutEntry[], sram: Uint8Array): CaseDigest {
  return {
    segments: segs.map((ls) => ({ frames: 256, lanes: ls.map((l) => laneDigest(l, 0, 256)) })),
    midiOut: midiDigest(midi),
    sram: { "0": bytesDigest(sram) },
    ram: { "0": bytesDigest(RAM) },
  };
}

const MIDI: MidiOutEntry[] = [{ block: 3, slot: 0, frame: 17, data: Uint8Array.of(0x90, 60, 100) }];
const SRAM = Uint8Array.from({ length: 64 }, (_, i) => i);
const RAM = Uint8Array.from({ length: 32 }, (_, i) => 255 - i);

test("identical output compares equal", () => {
  const a = digestOf([lanes(1), lanes(2)], MIDI, SRAM);
  const b = digestOf([lanes(1), lanes(2)], MIDI, SRAM);
  expect(compareDigests(a, b, { audio: true })).toEqual([]);
});

test("one flipped low bit in one float names its segment and lane", () => {
  const segs = [lanes(1), lanes(2)];
  const a = digestOf(segs, MIDI, SRAM);
  const bits = new Uint32Array(segs[1][2].buffer);
  bits[100] ^= 1;
  const p = compareDigests(a, digestOf(segs, MIDI, SRAM), { audio: true });
  expect(p.length).toBe(1);
  expect(p[0].startsWith("segment 1 (frames 256..512), lane 2: expected ")).toBeTruthy();
});

test("audio is not compared when the target cannot be held to it", () => {
  const segs = [lanes(1)];
  const a = digestOf(segs, MIDI, SRAM);
  new Uint32Array(segs[0][0].buffer)[0] ^= 1;
  expect(compareDigests(a, digestOf(segs, MIDI, SRAM), { audio: false })).toEqual([]);
});

test("one changed MIDI-out byte, or a moved frame, fails", () => {
  const a = digestOf([lanes(1)], MIDI, SRAM);
  const byte = [{ ...MIDI[0], data: Uint8Array.of(0x90, 60, 101) }];
  const frame = [{ ...MIDI[0], frame: 18 }];
  expect(compareDigests(a, digestOf([lanes(1)], byte, SRAM), { audio: true })[0].startsWith("MIDI out: expected 1 message(s)")).toBeTruthy();
  expect(compareDigests(a, digestOf([lanes(1)], frame, SRAM), { audio: true }).length).toBe(1);
});

test("one changed SRAM byte names the slot", () => {
  const a = digestOf([lanes(1)], MIDI, SRAM);
  const sram = SRAM.slice();
  sram[63] ^= 0x80;
  expect(compareDigests(a, digestOf([lanes(1)], MIDI, sram), { audio: true })).toEqual([
    `SRAM slot 0: expected 64 bytes hashing ${a.sram["0"].fnv}, got 64 hashing ${bytesDigest(sram).fnv}`,
  ]);
});

test("one changed work-RAM byte names the slot", () => {
  const a = digestOf([lanes(1)], MIDI, SRAM);
  const b = digestOf([lanes(1)], MIDI, SRAM);
  b.ram["0"] = bytesDigest(Uint8Array.of(1, 2, 3));
  expect(compareDigests(a, b, { audio: false }).map((p) => p.split(":")[0])).toEqual(["work RAM slot 0"]);
});

test("a baseline is written with sorted keys, one value per line", () => {
  expect(stableJson({ b: 1, a: { d: [1], c: "x" } })).toBe('{\n "a": {\n  "c": "x",\n  "d": [\n   1\n  ]\n },\n "b": 1\n}\n');
});
