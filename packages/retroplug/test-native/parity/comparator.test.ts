// The parity comparator on its own: the smallest possible change to audio, MIDI out or SRAM must fail,
// and the report must name where. If this passes, a matching hash means matching output.
import { test, expect } from "../../testing/harness";
import {
  bytesDigest,
  compareDigests,
  fnv1a,
  hex8,
  laneDigest,
  laneSounds,
  midiDigest,
  rms4,
  stableJson,
  stableJsonLine,
  type CaseDigest,
  type MidiOutEntry,
} from "./digest";

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
  expect(p[0].startsWith("segment 1 (frames 256..512), lane 2: expected "), p[0]).toBeTruthy();
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
  const msg = compareDigests(a, digestOf([lanes(1)], byte, SRAM), { audio: true })[0];
  expect(msg.startsWith("MIDI out: expected 1 message(s)"), msg).toBeTruthy();
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
  const ram = RAM.slice();
  ram[0] ^= 1;
  b.ram["0"] = bytesDigest(ram);
  expect(compareDigests(a, b, { audio: false })).toEqual([
    `work RAM slot 0: expected 32 bytes hashing ${a.ram["0"].fnv}, got 32 hashing ${bytesDigest(ram).fnv}`,
  ]);
});

test("a baseline is written with sorted keys, one value per line", () => {
  expect(stableJson({ b: 1, a: { d: [1], c: "x" } })).toBe('{\n "a": {\n  "c": "x",\n  "d": [\n   1\n  ]\n },\n "b": 1\n}\n');
});

test("a failure's actual digest is the same sorted JSON on one line", () => {
  const v = { b: 1, a: { d: [1], c: "x" } };
  expect(stableJsonLine(v)).toBe('{"a":{"c":"x","d":[1]},"b":1}');
  expect(JSON.stringify(JSON.parse(stableJson(v)))).toBe(stableJsonLine(v));
});

const utf8 = (t: string): Uint8Array => Uint8Array.from(t, (c) => c.charCodeAt(0));

test("the stored format is pinned by literals", () => {
  expect(hex8(fnv1a(utf8("")))).toBe("811c9dc5");
  expect(hex8(fnv1a(utf8("a")))).toBe("e40c292c");
  expect(hex8(fnv1a(utf8("foobar")))).toBe("bf9cf968");
  expect(rms4(new Float32Array(4), 0, 4)).toBe("0");
  expect(rms4(Float32Array.of(1e-7, 1e-7), 0, 2)).toBe("1.000e-7");
  expect(laneDigest(Float32Array.of(0.5, -0.25), 0, 2)).toBe("4fed8f02 0.3953");
  const m = midiDigest(MIDI);
  expect(m.count).toBe(1);
  expect(m.fnv).toBe("b8002bec");
});

function seg(...lanes: string[]) {
  return { frames: 256, lanes };
}
function withSegs(segments: ReturnType<typeof seg>[]): CaseDigest {
  return { segments, midiOut: midiDigest([]), sram: {}, ram: {} };
}

test("laneSounds ignores the boot segment and never sounds on a boot-only case", () => {
  const loud = "00000001 0.5";
  const quiet = "00000002 0";
  expect(laneSounds(withSegs([seg(loud), seg(quiet), seg(quiet)]), 0)).toBe(false);
  expect(laneSounds(withSegs([seg(quiet), seg(quiet), seg(loud)]), 0)).toBe(true);
  expect(laneSounds(withSegs([seg(loud)]), 0)).toBe(false);
});

test("segment, frame and lane-count mismatches are reported", () => {
  const a = withSegs([seg("1 1", "2 1"), seg("3 1", "4 1")]);
  expect(compareDigests(a, withSegs([seg("1 1", "2 1")]), { audio: true })).toEqual(["expected 2 segment(s), got 1"]);
  const frames = withSegs([seg("1 1", "2 1"), { frames: 128, lanes: ["3 1", "4 1"] }]);
  expect(compareDigests(a, frames, { audio: true })).toEqual(["segment 1: expected 256 frames x 2 lanes, got 128 x 2"]);
  const fewer = withSegs([seg("1 1", "2 1"), seg("3 1")]);
  expect(compareDigests(a, fewer, { audio: true })).toEqual(["segment 1: expected 256 frames x 2 lanes, got 256 x 1"]);
});

test("an SRAM slot present on one side only is reported", () => {
  const a = digestOf([lanes(1)], MIDI, SRAM);
  const b = digestOf([lanes(1)], MIDI, SRAM);
  b.sram["1"] = bytesDigest(SRAM);
  expect(compareDigests(a, b, { audio: true })).toEqual(["SRAM slot 1: unexpected"]);
  expect(compareDigests(b, a, { audio: true })).toEqual(["SRAM slot 1: missing"]);
});

test("slots report in numeric order", () => {
  const a = digestOf([lanes(1)], MIDI, SRAM);
  const b = digestOf([lanes(1)], MIDI, SRAM);
  for (const k of ["2", "10"]) b.sram[k] = bytesDigest(SRAM);
  expect(compareDigests(a, b, { audio: true })).toEqual(["SRAM slot 2: unexpected", "SRAM slot 10: unexpected"]);
});

test("two silent lanes that differ say so", () => {
  const pos = Float32Array.of(0, 0);
  const neg = Float32Array.of(-0, 0);
  const a = withSegs([{ frames: 2, lanes: [laneDigest(pos, 0, 2)] }]);
  const b = withSegs([{ frames: 2, lanes: [laneDigest(neg, 0, 2)] }]);
  const p = compareDigests(a, b, { audio: true });
  expect(p.length).toBe(1);
  expect(p[0].endsWith(" (both silent: only the sign of a zero differs)"), p[0]).toBeTruthy();
});
