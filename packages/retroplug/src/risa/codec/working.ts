// Expand a risa song RECORD into (and read it back from) the firmware's working-song RAM image — WRAM
// banks 0..3 (0x0000..0x7FFF), the 32 KB the tracker edits live. (The saved RSAV catalog lives in banks
// 4..7 at 0x8000; see ./sav.ts.) This is the piece a future "Load song to working memory" needs: the
// firmware boots showing whatever song sits in banks 0..3, keyed by the 'N8T' magic at bank-1 0x1E80.
//
// writeWorking mirrors the firmware's own seq_data_save_load (src/seq_data_save.c): start from the
// seq_data_init defaults, overlay the record's present objects at their exact WRAM strides, stamp the
// magic, and (for records older than v7) apply the same in-place DMC-kit-field + envelope migrations the
// firmware applies on load. readWorking is the inverse, mirroring save_build_present_sets +
// save_write_current_song: a working object is "present" iff its content is non-default — plus, exactly
// as the firmware does, a table referenced by a present instrument is emitted even when empty. Composed
// with encodeRecord (./record.ts), readWorking(writeWorking(decodeRecord(rec))) reproduces a canonical
// v7 record byte-for-byte.

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
  PHRASE_EMPTY,
  NOTE_EMPTY,
  INST_EMPTY,
  TABLE_EMPTY,
  TABLE_VOL_INHERIT,
  FX_NONE,
  INST_TYPE_PULSE,
  INST_TYPE_NOISE,
  INST_TYPE_DMC,
  INST_TYPE_FIELD,
  INST_LAST_FIELD,
  INST_DMC_KIT_FIELD,
  INST_ENV_A_FIELD,
  INST_ENV_D_FIELD,
  INST_ENV_R_FIELD,
  SAVE_RECORD_VERSION,
  SAVE_RECORD_VERSION_V4,
  SAVE_RECORD_VERSION_V7,
  SONG_NAME_LEN,
  WRAM_BANK_SIZE,
  WORKING_SIZE,
  BANK_PHRASES,
  BANK_DATA,
  BANK_TABLES,
  BANK_PHRASES_HI,
  CHAIN_OFFSET,
  SONG_OFFSET,
  INST_OFFSET,
  GROOVE_OFFSET,
  SAVE_MAGIC_OFFSET,
  PROJECT_SETTINGS_OFFSET,
  SONG_NAME_OFFSET,
  TABLE_OFFSET,
  AUX_SHARED_OFFSET,
  GROOVE_SIZE,
  SAVE_MAGIC,
  SAVE_MAGIC_VER,
  DEFAULT_GROOVE_SPEED,
  DEFAULT_SETTINGS,
  UNTITLED,
} from "./constants";
import type { RisaRecord } from "./record";
import { decodeRecord } from "./record";

const CHAIN_STRIDE = 32; // 16 rows * 2
const PHRASE_STRIDE = 64; // 16 rows * 4
const TABLE_STRIDE = 128; // 16 rows * 8 (padded)
const TABLE_ROW = 8; // WRAM table row stride (6 meaningful + 2 pad)

/** Absolute working-image offset of `off` within `bank`. */
const bankOff = (bank: number, off: number): number => bank * WRAM_BANK_SIZE + off;
/** WRAM bank + offset of phrase `idx` (0..254): 0x00..0x7F in bank 0, 0x80..0xFE in bank 3. */
const phraseBase = (idx: number): number => bankOff(idx & 0x80 ? BANK_PHRASES_HI : BANK_PHRASES, (idx & 0x7f) << 6);
const chainBase = (idx: number): number => bankOff(BANK_DATA, CHAIN_OFFSET + idx * CHAIN_STRIDE);
const songBase = (track: number): number => bankOff(BANK_DATA, SONG_OFFSET + track * SONG_ROWS);
const instBase = (idx: number): number => bankOff(BANK_DATA, INST_OFFSET + idx * INST_SIZE);
const grooveBase = (idx: number): number => bankOff(BANK_DATA, GROOVE_OFFSET + idx * GROOVE_SIZE);
const tableBase = (idx: number): number => bankOff(BANK_TABLES, TABLE_OFFSET + idx * TABLE_STRIDE);
const auxBase = (idx: number): number => bankOff(BANK_TABLES, AUX_SHARED_OFFSET + idx * PHRASE_ROWS);

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

const grooveSerializedLen = (len: number): number => (len >= 1 && len <= 16 ? len : 16);

/** A blank 32 KB working image (banks 0..3), byte-for-byte as src/seq_data.c seq_data_init leaves it —
 *  every collection at its sentinel default. The 'N8T' magic is NOT stamped here (the firmware writes it
 *  separately on load); writeWorking stamps it. */
export function initWorkingDefaults(): Uint8Array {
  const w = new Uint8Array(WORKING_SIZE);

  // Banks 0 & 3: phrases — every 4-byte row [NOTE_EMPTY, INST_EMPTY, FX_NONE, 0].
  for (const bank of [BANK_PHRASES, BANK_PHRASES_HI]) {
    const base = bankOff(bank, 0);
    for (let o = 0; o < WRAM_BANK_SIZE; o += 4) {
      w[base + o] = NOTE_EMPTY;
      w[base + o + 1] = INST_EMPTY;
      w[base + o + 2] = FX_NONE;
      w[base + o + 3] = 0;
    }
  }

  // Bank 1: bulk 0xFF (song cells = CHAIN_EMPTY, instrument type = empty, magic/current-entry = 0xFF), then:
  const b1 = bankOff(BANK_DATA, 0);
  w.fill(0xff, b1, b1 + WRAM_BANK_SIZE);
  for (let idx = 0; idx < CHAIN_COUNT; idx++) {
    for (let row = 0; row < CHAIN_ROWS; row++) w[chainBase(idx) + row * 2 + 1] = 0; // transpose byte -> 0
  }
  for (let idx = 0; idx < GROOVE_COUNT; idx++) {
    const g = grooveBase(idx);
    w[g] = 2;
    w[g + 1] = DEFAULT_GROOVE_SPEED;
    w[g + 2] = DEFAULT_GROOVE_SPEED;
    for (let i = 3; i < GROOVE_SIZE; i++) w[g + i] = 0;
  }
  w.set(DEFAULT_SETTINGS, bankOff(BANK_DATA, PROJECT_SETTINGS_OFFSET));
  w.fill(0x20, bankOff(BANK_DATA, SONG_NAME_OFFSET), bankOff(BANK_DATA, SONG_NAME_OFFSET) + SONG_NAME_LEN);

  // Bank 2: bulk 0xFF (aux shared notes = NOTE_EMPTY), then table rows to [VOL_INHERIT, 0, 0, 0, 0, 0, 0, 0].
  const b2 = bankOff(BANK_TABLES, 0);
  w.fill(0xff, b2, b2 + WRAM_BANK_SIZE);
  for (let idx = 0; idx < TABLE_COUNT; idx++) {
    for (let row = 0; row < TABLE_ROWS; row++) {
      const t = tableBase(idx) + row * TABLE_ROW;
      w[t] = TABLE_VOL_INHERIT;
      for (let i = 1; i < TABLE_ROW; i++) w[t + i] = 0;
    }
  }
  return w;
}

/** Apply the firmware's post-load instrument migrations in place (src/seq_data_save.c save_migrate_*):
 *  <v4 moves the DMC kit index from byte 7 to byte 10; <v7 re-encodes pulse/noise volume envelopes to
 *  attack/decay/release. A no-op for a v7 record. */
function migrateInstruments(w: Uint8Array, version: number): void {
  const dmcMove = version < SAVE_RECORD_VERSION_V4;
  const envUpgrade = version < SAVE_RECORD_VERSION_V7;
  for (let idx = 0; idx < INST_COUNT; idx++) {
    const p = instBase(idx);
    const type = w[p + INST_TYPE_FIELD];
    if (type === 0xff) continue;
    if (dmcMove && type === INST_TYPE_DMC) {
      w[p + INST_DMC_KIT_FIELD] = w[p + INST_LAST_FIELD];
      w[p + INST_LAST_FIELD] = TABLE_EMPTY;
    }
    if (envUpgrade && (type === INST_TYPE_PULSE || type === INST_TYPE_NOISE)) {
      const vol = w[p + INST_ENV_A_FIELD] & 0x0f;
      const env = w[p + INST_ENV_D_FIELD] & 0x0f;
      if (env === 0 || env === 8) {
        w[p + INST_ENV_A_FIELD] = vol << 4;
        w[p + INST_ENV_D_FIELD] = vol << 4;
        w[p + INST_ENV_R_FIELD] = vol << 4;
      } else if (env < 8) {
        w[p + INST_ENV_A_FIELD] = (vol << 4) | (env << 1);
        w[p + INST_ENV_D_FIELD] = 0;
        w[p + INST_ENV_R_FIELD] = 0;
      } else {
        w[p + INST_ENV_A_FIELD] = (vol << 4) | ((16 - env) << 1);
        w[p + INST_ENV_D_FIELD] = 0xf0;
        w[p + INST_ENV_R_FIELD] = 0xf0;
      }
    }
  }
}

/** Overlay a decoded record onto a fresh working image (banks 0..3) and stamp the working-song magic —
 *  the byte-level equivalent of the firmware loading `rec` into working memory. */
export function writeWorking(rec: RisaRecord): Uint8Array {
  const w = initWorkingDefaults();

  // Magic + name + project settings (bank 1).
  w.set(SAVE_MAGIC, bankOff(BANK_DATA, SAVE_MAGIC_OFFSET));
  w[bankOff(BANK_DATA, SAVE_MAGIC_OFFSET) + 3] = SAVE_MAGIC_VER;
  w.set(encodeName(rec.name), bankOff(BANK_DATA, SONG_NAME_OFFSET));
  w.set(rec.settings.slice(0, 8), bankOff(BANK_DATA, PROJECT_SETTINGS_OFFSET));

  for (let track = 0; track < SEQ_TRACK_COUNT; track++) w.set(rec.song[track], songBase(track));

  for (let idx = 0; idx < CHAIN_COUNT; idx++) if (rec.chains[idx]) w.set(rec.chains[idx]!, chainBase(idx));

  for (let idx = 0; idx < PHRASE_COUNT; idx++) if (rec.phrases[idx]) w.set(rec.phrases[idx]!, phraseBase(idx));

  // Aux notes: merge the two decoded lanes into the single shared WRAM lane (bank 2), as the firmware does.
  for (let idx = 0; idx < PHRASE_COUNT; idx++) {
    const a0 = rec.auxPhrases[0][idx];
    const a1 = rec.auxPhrases[1][idx];
    if (!a0 && !a1) continue;
    const base = auxBase(idx);
    for (let row = 0; row < PHRASE_ROWS; row++) {
      const primary = a0 ? a0[row] : NOTE_EMPTY;
      w[base + row] = primary !== NOTE_EMPTY ? primary : a1 ? a1[row] : NOTE_EMPTY;
    }
  }

  for (let idx = 0; idx < INST_COUNT; idx++) if (rec.instruments[idx]) w.set(rec.instruments[idx]!, instBase(idx));
  migrateInstruments(w, rec.recordVersion);

  for (let idx = 0; idx < TABLE_COUNT; idx++) {
    const rows = rec.tables[idx];
    if (!rows) continue;
    const base = tableBase(idx);
    for (let row = 0; row < TABLE_ROWS; row++) w.set(rows.subarray(row * 6, row * 6 + 6), base + row * TABLE_ROW);
  }

  for (let idx = 0; idx < GROOVE_COUNT; idx++) {
    const groove = rec.grooves[idx];
    if (!groove) continue;
    const len = groove[0];
    const base = grooveBase(idx);
    w[base] = len;
    for (let i = 0; i < 16; i++) w[base + 1 + i] = i < len ? groove[1 + i] : 0;
  }

  return w;
}

/** Convenience: decode a record and expand it to a working image (banks 0..3). */
export function expandRecordToWorking(recordBytes: Uint8Array): Uint8Array {
  return writeWorking(decodeRecord(recordBytes));
}

function chainEmpty(w: Uint8Array, base: number): boolean {
  for (let row = 0; row < CHAIN_ROWS; row++) if (w[base + row * 2] !== PHRASE_EMPTY) return false;
  return true;
}
function phraseEmpty(w: Uint8Array, base: number): boolean {
  for (let row = 0; row < PHRASE_ROWS; row++) {
    const p = base + row * 4;
    if (w[p] !== NOTE_EMPTY || w[p + 1] !== INST_EMPTY || w[p + 2] !== FX_NONE || w[p + 3] !== 0) return false;
  }
  return true;
}
function tableEmpty(w: Uint8Array, base: number): boolean {
  for (let row = 0; row < TABLE_ROWS; row++) {
    const p = base + row * TABLE_ROW;
    if (w[p] !== TABLE_VOL_INHERIT || w[p + 1] !== 0 || w[p + 2] !== 0 || w[p + 4] !== 0) return false;
  }
  return true;
}
function grooveUsed(w: Uint8Array, base: number): boolean {
  if (w[base] !== 2 || w[base + 1] !== DEFAULT_GROOVE_SPEED || w[base + 2] !== DEFAULT_GROOVE_SPEED) return true;
  for (let i = 3; i < GROOVE_SIZE; i++) if (w[base + i] !== 0) return true;
  return false;
}
function auxEmpty(w: Uint8Array, base: number): boolean {
  for (let row = 0; row < PHRASE_ROWS; row++) if (w[base + row] !== NOTE_EMPTY) return false;
  return true;
}

/** Read a working image (banks 0..3) back into a current-version (v7) record model — the inverse of
 *  writeWorking, mirroring the firmware's save_build_present_sets + save_write_current_song. */
export function readWorking(w: Uint8Array): RisaRecord {
  const rec: RisaRecord = {
    recordVersion: SAVE_RECORD_VERSION,
    name: decodeName(w.subarray(bankOff(BANK_DATA, SONG_NAME_OFFSET), bankOff(BANK_DATA, SONG_NAME_OFFSET) + SONG_NAME_LEN)),
    settings: w.slice(bankOff(BANK_DATA, PROJECT_SETTINGS_OFFSET), bankOff(BANK_DATA, PROJECT_SETTINGS_OFFSET) + 8),
    song: Array.from({ length: SEQ_TRACK_COUNT }, (_v, track) => w.slice(songBase(track), songBase(track) + SONG_ROWS)),
    chains: Array.from({ length: CHAIN_COUNT }, () => null as Uint8Array | null),
    phrases: Array.from({ length: PHRASE_COUNT }, () => null as Uint8Array | null),
    auxPhrases: [
      Array.from({ length: PHRASE_COUNT }, () => null as Uint8Array | null),
      Array.from({ length: PHRASE_COUNT }, () => null as Uint8Array | null),
    ],
    instruments: Array.from({ length: INST_COUNT }, () => null as Uint8Array | null),
    tables: Array.from({ length: TABLE_COUNT }, () => null as Uint8Array | null),
    grooves: Array.from({ length: GROOVE_COUNT }, () => null as Uint8Array | null),
  };

  for (let idx = 0; idx < CHAIN_COUNT; idx++) {
    const base = chainBase(idx);
    if (!chainEmpty(w, base)) rec.chains[idx] = w.slice(base, base + CHAIN_STRIDE);
  }

  for (let idx = 0; idx < PHRASE_COUNT; idx++) {
    const base = phraseBase(idx);
    if (!phraseEmpty(w, base)) rec.phrases[idx] = w.slice(base, base + PHRASE_STRIDE);
  }

  for (let idx = 0; idx < PHRASE_COUNT; idx++) {
    const base = auxBase(idx);
    if (!auxEmpty(w, base)) rec.auxPhrases[0][idx] = w.slice(base, base + PHRASE_ROWS);
  }

  // Instruments, plus the set of tables they reference (an empty referenced table is still emitted).
  const referenced = new Uint8Array(TABLE_COUNT);
  for (let idx = 0; idx < INST_COUNT; idx++) {
    const base = instBase(idx);
    if (w[base + INST_TYPE_FIELD] === 0xff) continue;
    rec.instruments[idx] = w.slice(base, base + INST_SIZE);
    const t = w[base + INST_LAST_FIELD];
    if (t !== TABLE_EMPTY && t < TABLE_COUNT) referenced[t] = 1;
  }

  for (let idx = 0; idx < TABLE_COUNT; idx++) {
    const base = tableBase(idx);
    if (tableEmpty(w, base) && !referenced[idx]) continue;
    const rows = new Uint8Array(TABLE_ROWS * 6);
    for (let row = 0; row < TABLE_ROWS; row++) rows.set(w.subarray(base + row * TABLE_ROW, base + row * TABLE_ROW + 6), row * 6);
    rec.tables[idx] = rows;
  }

  for (let idx = 0; idx < GROOVE_COUNT; idx++) {
    const base = grooveBase(idx);
    if (!grooveUsed(w, base)) continue;
    const len = grooveSerializedLen(w[base]);
    const groove = new Uint8Array(1 + len);
    groove[0] = len;
    groove.set(w.subarray(base + 1, base + 1 + len), 1);
    rec.grooves[idx] = groove;
  }

  return rec;
}
