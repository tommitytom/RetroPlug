// risa working-RAM expansion — the record <-> working-song image (WRAM banks 0..3) codec. The primary
// proof is a byte-identity round-trip: encodeRecord(readWorking(writeWorking(decodeRecord(raver)))) ===
// raver, an invariant the firmware itself guarantees (its save_verify_record self-check). Plus structural
// asserts that objects land at their exact WRAM strides, and coverage of the pre-v7 instrument migration
// writeWorking applies (mirroring seq_data_save_load). All pure-TS, no emulator.
import { test, expect } from "../../testing/harness";
import { recordBytes } from "./recordFixtures";
import { savBytes } from "./fixtures";
import {
  decodeRecord,
  encodeRecord,
  writeWorking,
  readWorking,
  expandRecordToWorking,
  initWorkingDefaults,
  recordBytesAt,
  normalizeSaveContainer,
  CURRENT_LAYOUT,
} from "../../src/risaSav";

// Working-image absolute offsets (bank * 0x2000 + within-bank offset), for the structural asserts.
const B1 = 0x2000; // bank 1 (data)
const MAGIC = B1 + 0x1e80;
const SETTINGS = B1 + 0x1e84;
const SONG = B1 + 0x1000; // track 0
const INST0 = B1 + 0x1280;

function sameBytes(a: Uint8Array, b: Uint8Array): boolean {
  if (a.length !== b.length) return false;
  for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
  return true;
}

function blumarblRecord(): Uint8Array {
  const save = normalizeSaveContainer(savBytes("v2_blumarbl")).save;
  return recordBytesAt(save, CURRENT_LAYOUT, 0)!;
}

test("readWorking(writeWorking(rec)) round-trips the v7 RAVER record byte-for-byte", () => {
  const bytes = recordBytes("raver_v7");
  const back = encodeRecord(readWorking(writeWorking(decodeRecord(bytes))));
  expect(sameBytes(back, bytes)).toBe(true);
});

test("initWorkingDefaults is a 32 KB image with sentinel defaults (empty phrase, groove 02 06 06)", () => {
  const w = initWorkingDefaults();
  expect(w.length).toBe(0x8000);
  // phrase 0 (bank 0, offset 0) default row.
  expect(Array.from(w.slice(0, 4))).toEqual([0xff, 0xff, 0x00, 0x00]);
  // groove 0 (bank 1, 0x1580) default = len 2, steps 6 6.
  expect(Array.from(w.slice(B1 + 0x1580, B1 + 0x1580 + 3))).toEqual([2, 6, 6]);
  // song cell (track 0 row 0) = CHAIN_EMPTY.
  expect(w[SONG]).toBe(0xff);
});

test("expandRecordToWorking places magic, settings, song, and a phrase row at their WRAM strides", () => {
  const bytes = recordBytes("raver_v7");
  const rec = decodeRecord(bytes);
  const w = expandRecordToWorking(bytes);
  expect(w.length).toBe(0x8000);

  // 'N8T' + version 0x0C at bank-1 0x1E80.
  expect(Array.from(w.slice(MAGIC, MAGIC + 4))).toEqual([0x4e, 0x38, 0x54, 0x0c]);
  // Project settings copied verbatim.
  expect(Array.from(w.slice(SETTINGS, SETTINGS + 8))).toEqual(Array.from(rec.settings));
  // Song track 0 (bank 1, 0x1000) matches the decoded grid.
  expect(Array.from(w.slice(SONG, SONG + 128))).toEqual(Array.from(rec.song[0]));

  // The first present phrase's row 0 lands at its bank-0/3 stride.
  const idx = rec.phrases.findIndex((p) => p);
  const base = (idx & 0x80 ? 3 * 0x2000 : 0) + ((idx & 0x7f) << 6);
  expect(Array.from(w.slice(base, base + 4))).toEqual(Array.from(rec.phrases[idx]!.slice(0, 4)));
});

test("writeWorking upgrades a pre-v7 pulse/noise instrument envelope in place (v5 blumarbl)", () => {
  const rec = decodeRecord(blumarblRecord());
  const w = writeWorking(rec);

  // Instrument 0 is a pulse in blumarbl; its v5 [vol,env] pack is re-encoded to the v7 A/D/R form.
  const src = rec.instruments[0]!;
  const vol = src[1] & 0x0f;
  const env = src[2] & 0x0f;
  const expected = Array.from(src);
  if (env === 0 || env === 8) {
    expected[1] = vol << 4;
    expected[2] = vol << 4;
    expected[10] = vol << 4;
  } else if (env < 8) {
    expected[1] = (vol << 4) | (env << 1);
    expected[2] = 0;
    expected[10] = 0;
  } else {
    expected[1] = (vol << 4) | ((16 - env) << 1);
    expected[2] = 0xf0;
    expected[10] = 0xf0;
  }
  expect(Array.from(w.slice(INST0, INST0 + 12))).toEqual(expected);

  // The read-back is a current (v7) record, and the musical structure survived the upgrade. (Aux count is
  // deliberately NOT compared: v5 stores an aux lane per present phrase — 118, mostly empty — while the v7
  // model keeps aux only for phrases with real notes, so empty lanes drop out. That's correct canonicalization.)
  const back = readWorking(w);
  expect(back.recordVersion).toBe(7);
  const count = (a: (Uint8Array | null)[]) => a.reduce((n, x) => n + (x ? 1 : 0), 0);
  expect(count(back.chains)).toBe(count(rec.chains));
  expect(count(back.phrases)).toBe(count(rec.phrases));
  expect(count(back.instruments)).toBe(count(rec.instruments));
});

test("writeWorking/readWorking round-trip real aux notes into the shared WRAM lane (v7)", () => {
  // Neither fixture uses the aux/echo note lane, so inject one and prove it survives the round-trip.
  const rec = decodeRecord(recordBytes("raver_v7"));
  const aux = new Uint8Array(16).fill(0xff);
  aux[0] = 0x24;
  aux[3] = 0x30;
  rec.auxPhrases[0][5] = aux;
  const canonical = encodeRecord(rec); // a valid v7 record carrying the aux notes

  const back = encodeRecord(readWorking(writeWorking(decodeRecord(canonical))));
  expect(sameBytes(back, canonical)).toBe(true);

  // The notes landed in bank 2 at AUX_SHARED (0x1000) + phrase 5 * 16.
  const auxBase = 0x4000 + 0x1000 + 5 * 16;
  const w = writeWorking(rec);
  expect(w[auxBase]).toBe(0x24);
  expect(w[auxBase + 3]).toBe(0x30);
});
