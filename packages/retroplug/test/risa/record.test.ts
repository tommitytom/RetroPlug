// risa song-payload record codec — decode/encode golden + round-trip tests. Goldens are risa's own
// record_codec.js parseSongRecord output (see scripts/gen-risa-record-fixtures.mjs); the byte-identity
// round-trip mirrors what risa's makeRecord(parseSongRecord(rec)) === rec guarantees on the oracle side.
// Vectors: a real record-v7 song (RAVER.risong) and the v5 record inside the v2_blumarbl battery.
import { test, expect } from "../../testing/harness";
import { deepEqual } from "./_assert";
import { recordBytes } from "./recordFixtures";
import { savBytes } from "./fixtures";
import {
  decodeRecord,
  encodeRecord,
  recordBytesAt,
  normalizeSaveContainer,
  CURRENT_LAYOUT,
  type RisaRecord,
} from "../../src/risaSav";
import goldRaver from "./golden/record_raver_v7.json";
import goldBlum from "./golden/record_blumarbl_v5.json";

// Kept in lockstep with scripts/gen-risa-record-fixtures.mjs `summarize` — a compact, JSON-comparable
// view of a decoded record (name/version/tempo, present-object counts, and two spot-checked objects).
function summarize(rec: RisaRecord, byteLength: number) {
  const countNonNull = (arr: (Uint8Array | null)[]) => arr.reduce((n, x) => n + (x ? 1 : 0), 0);
  const songCells = rec.song.reduce(
    (n, track) => n + Array.from(track).reduce((m, c) => m + (c !== 0xff ? 1 : 0), 0),
    0,
  );
  const firstPhrase = rec.phrases.findIndex((p) => p);
  const firstInst = rec.instruments.findIndex((i) => i);
  return {
    name: rec.name,
    version: rec.recordVersion,
    tempoBpm: (rec.settings[0] << 8) | rec.settings[1],
    present: {
      songCells,
      chains: countNonNull(rec.chains),
      phrases: countNonNull(rec.phrases),
      aux: countNonNull(rec.auxPhrases[0]),
      instruments: countNonNull(rec.instruments),
      tables: countNonNull(rec.tables),
      grooves: countNonNull(rec.grooves),
    },
    spot: {
      phrase: firstPhrase < 0 ? null : { idx: firstPhrase, row0: Array.from(rec.phrases[firstPhrase]!.slice(0, 4)) },
      instrument: firstInst < 0 ? null : { idx: firstInst, bytes: Array.from(rec.instruments[firstInst]!) },
    },
    length: byteLength,
  };
}

function sameBytes(a: Uint8Array, b: Uint8Array): boolean {
  if (a.length !== b.length) return false;
  for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
  return true;
}

/** The v5 blumarbl record lives inside the v2_blumarbl battery (catalog record 0). */
function blumarblRecord(): Uint8Array {
  const save = normalizeSaveContainer(savBytes("v2_blumarbl")).save;
  return recordBytesAt(save, CURRENT_LAYOUT, 0)!;
}

test("decodeRecord matches the risa parseSongRecord oracle for the v7 RAVER song", () => {
  const bytes = recordBytes("raver_v7");
  const rec = decodeRecord(bytes);
  deepEqual(summarize(rec, bytes.length), goldRaver, "raver_v7");
  expect(rec.name).toBe("RAVER");
  expect(rec.recordVersion).toBe(7);
});

test("encodeRecord(decodeRecord(x)) is byte-identical for the canonical v7 RAVER record", () => {
  const bytes = recordBytes("raver_v7");
  expect(sameBytes(encodeRecord(decodeRecord(bytes)), bytes)).toBe(true);
});

test("decodeRecord reads the v5 blumarbl record (pre-v6 phrase bitset + single aux lane)", () => {
  const bytes = blumarblRecord();
  const rec = decodeRecord(bytes);
  deepEqual(summarize(rec, bytes.length), goldBlum, "blumarbl_v5");
  expect(rec.recordVersion).toBe(5);
});

test("encodeRecord(decodeRecord(x)) is byte-identical for the v5 blumarbl record", () => {
  const bytes = blumarblRecord();
  expect(sameBytes(encodeRecord(decodeRecord(bytes)), bytes)).toBe(true);
});

test("decodeRecord rejects a length-header / buffer-size mismatch", () => {
  // The header says 1649 bytes; hand it a 100-byte slice.
  expect(() => decodeRecord(recordBytes("raver_v7").slice(0, 100))).toThrow();
});

test("decodeRecord rejects a record whose payload does not consume its declared length", () => {
  const bytes = recordBytes("raver_v7");
  const grown = new Uint8Array(bytes.length + 1);
  grown.set(bytes);
  grown[0] = (bytes.length + 1) & 0xff; // bump the length header to match the buffer...
  grown[1] = ((bytes.length + 1) >> 8) & 0xff; // ...so the walk stops one byte short of the end
  expect(() => decodeRecord(grown)).toThrow();
});
