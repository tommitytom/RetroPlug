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

// --- legacy WAVE -> Z-Saw normalization ----------------------------------------------------------------
// Instrument type 4 was WAVE up to 2.2.x and is Z-Saw from 2.3.0, in records of the SAME version (v7), so
// the marker byte at 11 is the only discriminator. risa normalizes on its own load paths; Songs > Load
// writes working RAM directly under a running core, so writeWorking has to do the same or a pre-2.3.0
// song's WAVE instruments read as Z-Saw garbage until the next reset.

// A record whose instrument 0 is type 4 with `byte11` in the legacy WAVE sound-selector slot.
function recordWithType4(byte11: number): ReturnType<typeof decodeRecord> {
  const rec = decodeRecord(recordBytes("raver_v7"));
  const inst = new Uint8Array(12);
  inst[6] = 4; // INST_TYPE_ZSAW / legacy WAVE
  inst[11] = byte11;
  rec.instruments[0] = inst;
  return rec;
}

test("writeWorking converts a legacy WAVE instrument to Z-Saw (marker, timbre, default envelope)", () => {
  for (const [byte11, timbre] of [[0, 0], [1, 1], [3, 1], [2, 0]] as const) {
    const w = writeWorking(recordWithType4(byte11));
    const inst = Array.from(w.slice(INST0, INST0 + 12));
    expect(inst[6]).toBe(4); // still type 4
    expect(inst[11]).toBe(0xa5); // claimed by the Z-Saw marker
    expect(inst[0]).toBe(timbre); // sound selector's low bit picks the saw timbre
    expect([inst[1], inst[2], inst[10]]).toEqual([0x88, 0x00, 0x00]); // Z-Saw's fixed default envelope
  }
});

test("writeWorking leaves an already-marked Z-Saw instrument alone (idempotent)", () => {
  const rec = recordWithType4(0xa5);
  const inst = rec.instruments[0]!;
  inst[0] = 4; // triangle timbre
  inst[1] = 0x42; // a hand-set envelope the normalizer must not stomp
  const w = writeWorking(rec);
  expect(Array.from(w.slice(INST0, INST0 + 12))).toEqual(Array.from(inst));

  // And running it again over the already-converted image changes nothing further.
  const once = writeWorking(recordWithType4(1));
  const twice = writeWorking(readWorking(once));
  expect(Array.from(twice.slice(INST0, INST0 + 12))).toEqual(Array.from(once.slice(INST0, INST0 + 12)));
});

test("normalization is scoped to type 4 - other instrument types are untouched", () => {
  const rec = decodeRecord(recordBytes("raver_v7"));
  const inst = new Uint8Array(12);
  inst[6] = 3; // DMC
  inst[11] = 0x07; // would be the WAVE sound selector, but this isn't type 4
  rec.instruments[0] = inst;
  expect(Array.from(writeWorking(rec).slice(INST0, INST0 + 12))).toEqual(Array.from(inst));
});
