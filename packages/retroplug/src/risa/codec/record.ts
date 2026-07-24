// Codec for a risa (NES/MMC5 tracker) catalog song RECORD — the payload after the 16-byte header.
//
// The payload is NOT compressed. Each collection is a sparse "presence bitset + per-object row-mask
// delta-vs-default" encoding: a bitset says which objects are non-empty, then for each present object a
// row bitmask says which rows differ from that object's default, then only those rows' raw bytes follow.
// Absent objects and default rows cost zero bytes. This is a faithful port of risa's own
// tools/rom_patcher/src/save_manager/record_codec.js (parseSongRecord / makeRecord) — the decoded shape
// mirrors that oracle exactly so goldens cross-check byte-for-byte. Version-gated (record versions 2..7):
// the phrase bitset widens 16->32 bytes at v6 (count 128->255) and the table bitset 2->4 bytes at v2; the
// aux note lane appears at v3 (two lanes v3/v4, one from v5, its own bitset from v6).
//
// The higher-level expansion to the firmware's working-RAM image (banks 0..3) lives in ./working.ts.

import {
  SEQ_TRACK_COUNT,
  SONG_ROWS,
  CHAIN_ROWS,
  CHAIN_COUNT,
  PHRASE_ROWS,
  PHRASE_COUNT,
  INST_SIZE,
  INST_COUNT,
  TABLE_ROWS,
  TABLE_COUNT,
  GROOVE_COUNT,
  CHAIN_EMPTY,
  PHRASE_EMPTY,
  NOTE_EMPTY,
  INST_EMPTY,
  TABLE_VOL_INHERIT,
  SAVE_REC_HEADER,
  SAVE_RECORD_VERSION,
  SAVE_RECORD_VERSION_V6,
  SAVE_RECORD_VERSION_V5,
  SAVE_RECORD_VERSION_V3,
  SAVE_RECORD_VERSION_V2,
  SONG_NAME_LEN,
  UNTITLED,
} from "./constants";

/** A decoded risa song record — flat typed-array collections mirroring record_codec.js parseSongRecord.
 *  Each collection element is either the object's raw row bytes or null (absent). */
export interface RisaRecord {
  recordVersion: number;
  name: string;
  settings: Uint8Array; // 8 bytes (tempo hi/lo, transpose, theme, key-repeat, note-preview, dirty, font)
  song: Uint8Array[]; // [SEQ_TRACK_COUNT] each SONG_ROWS chain-ids (CHAIN_EMPTY where absent)
  chains: (Uint8Array | null)[]; // [CHAIN_COUNT] each CHAIN_ROWS*2 (phrase, transpose)
  phrases: (Uint8Array | null)[]; // [PHRASE_COUNT] each PHRASE_ROWS*4 (note, inst, fxType, fxVal)
  auxPhrases: [(Uint8Array | null)[], (Uint8Array | null)[]]; // 2 lanes, each [PHRASE_COUNT] of PHRASE_ROWS notes
  instruments: (Uint8Array | null)[]; // [INST_COUNT] each INST_SIZE
  tables: (Uint8Array | null)[]; // [TABLE_COUNT] each TABLE_ROWS*6 (vol, transpose, fx1t, fx1v, fx2t, fx2v)
  grooves: (Uint8Array | null)[]; // [GROOVE_COUNT] each [len, ...len steps]
}

const bitIsSet = (bytes: Uint8Array, idx: number): boolean => (bytes[idx >> 3] & (1 << (idx & 7))) !== 0;

function readU16(bytes: Uint8Array, off: number): number {
  return bytes[off] | (bytes[off + 1] << 8);
}
function writeU16(bytes: Uint8Array, off: number, value: number): void {
  bytes[off] = value & 0xff;
  bytes[off + 1] = (value >> 8) & 0xff;
}

function ensureWithin(bytes: Uint8Array, end: number): void {
  if (end > bytes.length) throw new Error("Song record ended early");
}
function readByte(bytes: Uint8Array, pos: number): number {
  ensureWithin(bytes, pos + 1);
  return bytes[pos];
}
function readBytes(bytes: Uint8Array, pos: number, len: number): Uint8Array {
  ensureWithin(bytes, pos + len);
  return bytes.slice(pos, pos + len);
}

function decodeName(bytes: Uint8Array): string {
  let out = "";
  for (const byte of bytes) {
    if (byte === 0) break;
    out += String.fromCharCode(byte);
  }
  return out.replace(/\s+$/, "") || UNTITLED;
}
function encodeName(name: string): Uint8Array {
  const out = new Uint8Array(SONG_NAME_LEN).fill(0x20);
  const s = String(name || UNTITLED).slice(0, SONG_NAME_LEN);
  for (let i = 0; i < s.length; i++) out[i] = s.charCodeAt(i) & 0xff;
  return out;
}

/** Decode a whole record (16-byte header + payload). Strict: the length header must equal the buffer
 *  length and the payload must consume exactly to the end (a faithful port of parseSongRecord). */
export function decodeRecord(recordBytes: Uint8Array): RisaRecord {
  const bytes = recordBytes instanceof Uint8Array ? recordBytes : new Uint8Array(recordBytes);
  const length = readU16(bytes, 0);
  if (length !== bytes.length) throw new Error("Song record length header does not match archive data");
  if (length < SAVE_REC_HEADER) throw new Error("Song record is too short");

  const recordVersion = bytes[10];
  if (recordVersion > SAVE_RECORD_VERSION) throw new Error(`Unsupported song record version ${recordVersion}`);

  const parsed: RisaRecord = {
    recordVersion,
    name: decodeName(bytes.subarray(2, 2 + SONG_NAME_LEN)),
    settings: new Uint8Array(8),
    song: Array.from({ length: SEQ_TRACK_COUNT }, () => new Uint8Array(SONG_ROWS).fill(CHAIN_EMPTY)),
    chains: Array.from({ length: CHAIN_COUNT }, () => null),
    phrases: Array.from({ length: PHRASE_COUNT }, () => null),
    auxPhrases: [
      Array.from({ length: PHRASE_COUNT }, () => null),
      Array.from({ length: PHRASE_COUNT }, () => null),
    ],
    instruments: Array.from({ length: INST_COUNT }, () => null),
    tables: Array.from({ length: TABLE_COUNT }, () => null),
    grooves: Array.from({ length: GROOVE_COUNT }, () => null),
  };

  let pos = SAVE_REC_HEADER;
  parsed.settings = readBytes(bytes, pos, 8);
  pos += 8;

  for (let track = 0; track < SEQ_TRACK_COUNT; track++) {
    const mask = readBytes(bytes, pos, 16);
    pos += 16;
    for (let row = 0; row < SONG_ROWS; row++) {
      if (bitIsSet(mask, row)) parsed.song[track][row] = readByte(bytes, pos++);
    }
  }

  const chainBits = readBytes(bytes, pos, 16);
  pos += 16;
  for (let idx = 0; idx < CHAIN_COUNT; idx++) {
    if (!bitIsSet(chainBits, idx)) continue;
    const rows = new Uint8Array(CHAIN_ROWS * 2);
    for (let row = 0; row < CHAIN_ROWS; row++) {
      rows[row * 2] = PHRASE_EMPTY;
      rows[row * 2 + 1] = 0;
    }
    const mask = readBytes(bytes, pos, 2);
    pos += 2;
    for (let row = 0; row < CHAIN_ROWS; row++) {
      if (!bitIsSet(mask, row)) continue;
      rows[row * 2] = readByte(bytes, pos++);
      rows[row * 2 + 1] = readByte(bytes, pos++);
    }
    parsed.chains[idx] = rows;
  }

  const phraseBitLen = recordVersion >= SAVE_RECORD_VERSION_V6 ? 32 : 16;
  const phraseLimit = recordVersion >= SAVE_RECORD_VERSION_V6 ? PHRASE_COUNT : 128;
  const phraseBits = new Uint8Array(32);
  phraseBits.set(readBytes(bytes, pos, phraseBitLen));
  pos += phraseBitLen;
  for (let idx = 0; idx < phraseLimit; idx++) {
    if (!bitIsSet(phraseBits, idx)) continue;
    const rows = new Uint8Array(PHRASE_ROWS * 4);
    for (let row = 0; row < PHRASE_ROWS; row++) {
      rows[row * 4] = NOTE_EMPTY;
      rows[row * 4 + 1] = INST_EMPTY;
      rows[row * 4 + 2] = 0;
      rows[row * 4 + 3] = 0;
    }
    const mask = readBytes(bytes, pos, 2);
    pos += 2;
    for (let row = 0; row < PHRASE_ROWS; row++) {
      if (!bitIsSet(mask, row)) continue;
      rows.set(readBytes(bytes, pos, 4), row * 4);
      pos += 4;
    }
    parsed.phrases[idx] = rows;
  }

  if (recordVersion >= SAVE_RECORD_VERSION_V3) {
    if (recordVersion >= SAVE_RECORD_VERSION_V6) {
      const auxPhraseBits = readBytes(bytes, pos, 32);
      pos += 32;
      for (let idx = 0; idx < PHRASE_COUNT; idx++) {
        if (!bitIsSet(auxPhraseBits, idx)) continue;
        const rows = new Uint8Array(PHRASE_ROWS).fill(NOTE_EMPTY);
        const mask = readBytes(bytes, pos, 2);
        pos += 2;
        for (let row = 0; row < PHRASE_ROWS; row++) {
          if (bitIsSet(mask, row)) rows[row] = readByte(bytes, pos++);
        }
        parsed.auxPhrases[0][idx] = rows;
      }
    } else {
      for (let idx = 0; idx < phraseLimit; idx++) {
        if (!bitIsSet(phraseBits, idx)) continue;
        const auxLaneCount = recordVersion >= SAVE_RECORD_VERSION_V5 ? 1 : 2;
        for (let lane = 0; lane < auxLaneCount; lane++) {
          const rows = new Uint8Array(PHRASE_ROWS).fill(NOTE_EMPTY);
          const mask = readBytes(bytes, pos, 2);
          pos += 2;
          for (let row = 0; row < PHRASE_ROWS; row++) {
            if (bitIsSet(mask, row)) rows[row] = readByte(bytes, pos++);
          }
          if (lane === 0) {
            parsed.auxPhrases[0][idx] = rows;
          } else {
            const sharedRows = parsed.auxPhrases[0][idx] ?? new Uint8Array(PHRASE_ROWS).fill(NOTE_EMPTY);
            for (let row = 0; row < PHRASE_ROWS; row++) {
              if (sharedRows[row] === NOTE_EMPTY) sharedRows[row] = rows[row];
            }
            parsed.auxPhrases[0][idx] = sharedRows;
          }
        }
      }
    }
  }

  const instBits = readBytes(bytes, pos, 8);
  pos += 8;
  for (let idx = 0; idx < INST_COUNT; idx++) {
    if (!bitIsSet(instBits, idx)) continue;
    parsed.instruments[idx] = readBytes(bytes, pos, INST_SIZE);
    pos += INST_SIZE;
  }

  const tableBitBytes = recordVersion >= SAVE_RECORD_VERSION_V2 ? 4 : 2;
  const tableBits = readBytes(bytes, pos, tableBitBytes);
  pos += tableBitBytes;
  for (let idx = 0; idx < TABLE_COUNT; idx++) {
    if (!bitIsSet(tableBits, idx)) continue;
    const rows = new Uint8Array(TABLE_ROWS * 6);
    const mask = readBytes(bytes, pos, 2);
    pos += 2;
    for (let row = 0; row < TABLE_ROWS; row++) {
      rows[row * 6] = TABLE_VOL_INHERIT;
      if (bitIsSet(mask, row)) {
        rows.set(readBytes(bytes, pos, 6), row * 6);
        pos += 6;
      }
      ensureWithin(bytes, pos);
    }
    parsed.tables[idx] = rows;
  }

  const grooveBits = readBytes(bytes, pos, 2);
  pos += 2;
  for (let idx = 0; idx < GROOVE_COUNT; idx++) {
    if (!bitIsSet(grooveBits, idx)) continue;
    const len = readByte(bytes, pos++);
    if (len < 1 || len > 16) throw new Error(`Groove ${idx} length is invalid`);
    const groove = new Uint8Array(1 + len);
    groove[0] = len;
    groove.set(readBytes(bytes, pos, len), 1);
    parsed.grooves[idx] = groove;
    pos += len;
    ensureWithin(bytes, pos);
  }

  if (pos !== bytes.length) throw new Error("Song record has trailing or missing payload bytes");
  return parsed;
}

function makeBits(items: (Uint8Array | null)[], count: number): Uint8Array {
  const bits = new Uint8Array(Math.ceil(count / 8));
  for (let i = 0; i < count; i++) if (items[i]) bits[i >> 3] |= 1 << (i & 7);
  return bits;
}
function makeAuxPhraseBits(auxPhrases: [(Uint8Array | null)[], (Uint8Array | null)[]]): Uint8Array {
  const bits = new Uint8Array(Math.ceil(PHRASE_COUNT / 8));
  for (let i = 0; i < PHRASE_COUNT; i++) if (auxPhrases[0][i] || auxPhrases[1][i]) bits[i >> 3] |= 1 << (i & 7);
  return bits;
}
function makeLegacyPhraseBits(
  phrases: (Uint8Array | null)[],
  auxPhrases: [(Uint8Array | null)[], (Uint8Array | null)[]],
): Uint8Array {
  const bits = new Uint8Array(16);
  for (let i = 0; i < 128; i++) if (phrases[i] || auxPhrases[0][i] || auxPhrases[1][i]) bits[i >> 3] |= 1 << (i & 7);
  return bits;
}
function mergeAuxPhraseRows(primary: Uint8Array | null, legacy: Uint8Array | null): Uint8Array | null {
  if (!legacy) return primary;
  if (!primary) return legacy;
  const merged = new Uint8Array(PHRASE_ROWS);
  for (let row = 0; row < PHRASE_ROWS; row++) merged[row] = primary[row] === NOTE_EMPTY ? legacy[row] : primary[row];
  return merged;
}

/** Re-encode a record to bytes (16-byte header + payload). A faithful port of makeRecord: it re-derives
 *  every presence bitset + row mask from the object contents, so decodeRecord -> encodeRecord is a byte
 *  round-trip for a canonical record (the firmware/tool never emits a present-but-all-default object). */
export function encodeRecord(rec: RisaRecord): Uint8Array {
  const recordVersion = rec.recordVersion;
  const chunks: Uint8Array[] = [];

  const settingsChunk = new Uint8Array(8);
  settingsChunk.set(rec.settings.slice(0, 8));
  chunks.push(settingsChunk);

  for (let track = 0; track < SEQ_TRACK_COUNT; track++) {
    const rowData = rec.song[track];
    const mask = new Uint8Array(16);
    const vals: number[] = [];
    for (let row = 0; row < SONG_ROWS; row++) {
      if (rowData[row] !== CHAIN_EMPTY) {
        mask[row >> 3] |= 1 << (row & 7);
        vals.push(rowData[row]);
      }
    }
    chunks.push(mask, Uint8Array.from(vals));
  }

  chunks.push(makeBits(rec.chains, CHAIN_COUNT));
  for (let idx = 0; idx < CHAIN_COUNT; idx++) {
    const rows = rec.chains[idx];
    if (!rows) continue;
    const mask = new Uint8Array(2);
    const vals: number[] = [];
    for (let row = 0; row < CHAIN_ROWS; row++) {
      if (rows[row * 2] !== PHRASE_EMPTY) {
        mask[row >> 3] |= 1 << (row & 7);
        vals.push(rows[row * 2], rows[row * 2 + 1]);
      }
    }
    chunks.push(mask, Uint8Array.from(vals));
  }

  const phraseLimit = recordVersion >= SAVE_RECORD_VERSION_V6 ? PHRASE_COUNT : 128;
  const phraseBits =
    recordVersion >= SAVE_RECORD_VERSION_V6
      ? makeBits(rec.phrases, phraseLimit)
      : makeLegacyPhraseBits(rec.phrases, rec.auxPhrases);
  chunks.push(phraseBits);
  for (let idx = 0; idx < phraseLimit; idx++) {
    if (!bitIsSet(phraseBits, idx)) continue;
    const rows = rec.phrases[idx];
    const mask = new Uint8Array(2);
    const vals: number[] = [];
    for (let row = 0; row < PHRASE_ROWS; row++) {
      const base = row * 4;
      const note = rows ? rows[base] : NOTE_EMPTY;
      const inst = rows ? rows[base + 1] : INST_EMPTY;
      const fxt = rows ? rows[base + 2] : 0;
      const fxv = rows ? rows[base + 3] : 0;
      if (!(note === NOTE_EMPTY && inst === INST_EMPTY && fxt === 0 && fxv === 0)) {
        mask[row >> 3] |= 1 << (row & 7);
        vals.push(note, inst, fxt, fxv);
      }
    }
    chunks.push(mask, Uint8Array.from(vals));
  }

  if (recordVersion >= SAVE_RECORD_VERSION_V3) {
    if (recordVersion >= SAVE_RECORD_VERSION_V6) {
      const auxPhraseBits = makeAuxPhraseBits(rec.auxPhrases);
      chunks.push(auxPhraseBits);
      for (let idx = 0; idx < PHRASE_COUNT; idx++) {
        if (!bitIsSet(auxPhraseBits, idx)) continue;
        const rows = mergeAuxPhraseRows(rec.auxPhrases[0][idx], rec.auxPhrases[1][idx]);
        const mask = new Uint8Array(2);
        const vals: number[] = [];
        for (let row = 0; row < PHRASE_ROWS; row++) {
          const note = rows ? rows[row] : NOTE_EMPTY;
          if (note !== NOTE_EMPTY) {
            mask[row >> 3] |= 1 << (row & 7);
            vals.push(note);
          }
        }
        chunks.push(mask, Uint8Array.from(vals));
      }
    } else {
      const auxLaneCount = recordVersion >= SAVE_RECORD_VERSION_V5 ? 1 : 2;
      for (let idx = 0; idx < 128; idx++) {
        const present = rec.phrases[idx] || rec.auxPhrases[0][idx] || rec.auxPhrases[1][idx];
        if (!present) continue;
        for (let lane = 0; lane < auxLaneCount; lane++) {
          const rows =
            auxLaneCount === 1
              ? mergeAuxPhraseRows(rec.auxPhrases[0][idx], rec.auxPhrases[1][idx])
              : rec.auxPhrases[lane][idx];
          const mask = new Uint8Array(2);
          const vals: number[] = [];
          for (let row = 0; row < PHRASE_ROWS; row++) {
            const note = rows ? rows[row] : NOTE_EMPTY;
            if (note !== NOTE_EMPTY) {
              mask[row >> 3] |= 1 << (row & 7);
              vals.push(note);
            }
          }
          chunks.push(mask, Uint8Array.from(vals));
        }
      }
    }
  }

  chunks.push(makeBits(rec.instruments, INST_COUNT));
  for (let idx = 0; idx < INST_COUNT; idx++) if (rec.instruments[idx]) chunks.push(rec.instruments[idx]!);

  chunks.push(makeBits(rec.tables, TABLE_COUNT));
  for (let idx = 0; idx < TABLE_COUNT; idx++) {
    const rows = rec.tables[idx];
    if (!rows) continue;
    const mask = new Uint8Array(2);
    const vals: number[] = [];
    for (let row = 0; row < TABLE_ROWS; row++) {
      const base = row * 6;
      const vol = rows[base];
      const transp = rows[base + 1];
      const fx1t = rows[base + 2];
      const fx1v = rows[base + 3];
      const fx2t = rows[base + 4];
      const fx2v = rows[base + 5];
      if (!(vol === TABLE_VOL_INHERIT && transp === 0 && fx1t === 0 && fx2t === 0)) {
        mask[row >> 3] |= 1 << (row & 7);
        vals.push(vol, transp, fx1t, fx1v, fx2t, fx2v);
      }
    }
    chunks.push(mask, Uint8Array.from(vals));
  }

  chunks.push(makeBits(rec.grooves, GROOVE_COUNT));
  for (let idx = 0; idx < GROOVE_COUNT; idx++) {
    const groove = rec.grooves[idx];
    if (!groove) continue;
    const len = groove[0];
    chunks.push(groove.slice(0, 1 + len));
  }

  const payloadLen = chunks.reduce((n, c) => n + c.length, 0);
  const out = new Uint8Array(SAVE_REC_HEADER + payloadLen);
  writeU16(out, 0, out.length);
  out.set(encodeName(rec.name), 2);
  out[10] = recordVersion;
  let pos = SAVE_REC_HEADER;
  for (const chunk of chunks) {
    out.set(chunk, pos);
    pos += chunk.length;
  }
  return out;
}
