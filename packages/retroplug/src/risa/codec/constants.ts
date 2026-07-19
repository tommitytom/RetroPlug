// Shared format constants for the risa song-record + working-RAM codecs. Ported verbatim from risa's
// tools/rom_patcher/src/save_manager/constants.js and src/seq.h / src/seq_data.h — the counts, sentinels,
// record-version gates, WRAM bank offsets, and init-time defaults the firmware itself uses.

// --- Record / catalog versions ---
export const SAVE_RECORD_VERSION = 7; // current record payload version
export const SAVE_RECORD_VERSION_V7 = 7;
export const SAVE_RECORD_VERSION_V6 = 6;
export const SAVE_RECORD_VERSION_V5 = 5;
export const SAVE_RECORD_VERSION_V4 = 4;
export const SAVE_RECORD_VERSION_V3 = 3;
export const SAVE_RECORD_VERSION_V2 = 2;

export const SONG_NAME_LEN = 8;
export const SAVE_REC_HEADER = 0x10; // 16-byte record header (length + name + version)

// --- Collection counts ---
export const SEQ_TRACK_COUNT = 5;
export const SONG_ROWS = 128;
export const CHAIN_ROWS = 16;
export const CHAIN_COUNT = 128;
export const PHRASE_ROWS = 16;
export const PHRASE_COUNT = 255;
export const INST_SIZE = 12;
export const INST_COUNT = 64;
export const TABLE_ROWS = 16;
export const TABLE_COUNT = 32;
export const GROOVE_COUNT = 16;

// --- Sentinels / field offsets ---
export const CHAIN_EMPTY = 0xff;
export const PHRASE_EMPTY = 0xff;
export const NOTE_EMPTY = 0xff;
export const INST_EMPTY = 0xff;
export const TABLE_EMPTY = 0xff;
export const TABLE_VOL_INHERIT = 0xff;
export const FX_NONE = 0;

export const INST_TYPE_PULSE = 0;
export const INST_TYPE_NOISE = 2;
export const INST_TYPE_DMC = 3;
export const INST_TYPE_FIELD = 6; // byte offset of type within a 12-byte instrument
export const INST_LAST_FIELD = 7; // table_idx (TABLE_EMPTY for none) — also legacy DMC kit index (<v4)
export const INST_DMC_KIT_FIELD = 10; // DMC kit index (v4+)
export const INST_ENV_A_FIELD = 1;
export const INST_ENV_D_FIELD = 2;
export const INST_ENV_R_FIELD = 10;

// --- Working-RAM (banks 0..3) layout — src/seq_data.h ---
export const WRAM_BANK_SIZE = 0x2000;
export const WORKING_SIZE = 0x8000; // banks 0..3 (the catalog lives in banks 4..7 at 0x8000)

export const BANK_PHRASES = 0; // phrases 0x00..0x7F
export const BANK_DATA = 1; // chains, song, instruments, grooves, settings, name
export const BANK_TABLES = 2; // tables + shared aux phrase notes
export const BANK_PHRASES_HI = 3; // phrases 0x80..0xFE

export const CHAIN_OFFSET = 0x0000; // in BANK_DATA: 128 * 32
export const SONG_OFFSET = 0x1000; // in BANK_DATA: 5 * 128
export const INST_OFFSET = 0x1280; // in BANK_DATA: 64 * 12
export const GROOVE_OFFSET = 0x1580; // in BANK_DATA: 16 * 17
export const SAVE_MAGIC_OFFSET = 0x1e80; // in BANK_DATA: 'N' '8' 'T' <ver>
export const PROJECT_SETTINGS_OFFSET = 0x1e84; // in BANK_DATA: 8 bytes
export const SONG_NAME_OFFSET = 0x1e8c; // in BANK_DATA: 8 bytes
export const SAVE_CURRENT_ENTRY_OFFSET = 0x1e94; // in BANK_DATA: 1 byte (0xFF = none)

export const TABLE_OFFSET = 0x0000; // in BANK_TABLES: 32 * 128 (row stride 8, only 6 serialized)
export const AUX_SHARED_OFFSET = 0x1000; // in BANK_TABLES: PHRASE_COUNT * 16 (1 note/row)

export const CHAIN_SIZE = 32; // 16 rows * 2
export const PHRASE_SIZE = 64; // 16 rows * 4
export const TABLE_WRAM_STRIDE = 128; // 16 rows * 8 (padded)
export const TABLE_ROW_SERIALIZED = 6; // vol, transpose, fx1t, fx1v, fx2t, fx2v
export const GROOVE_SIZE = 17; // 1 length + 16 steps

// --- Working-song magic + init defaults — src/seq_data.h / src/seq_data.c seq_data_init ---
export const SAVE_MAGIC = [0x4e, 0x38, 0x54]; // 'N' '8' 'T'
export const SAVE_MAGIC_VER = 0x0c;

export const DEFAULT_GROOVE_SPEED = 6;
export const DEFAULT_GROOVE = [2, DEFAULT_GROOVE_SPEED, DEFAULT_GROOVE_SPEED]; // len 2, steps 6 6, rest 0

export const DEFAULT_SETTINGS = [
  0x00, // tempo hi
  140, // tempo lo (DEFAULT_TEMPO_LO)
  0, // transpose
  0, // theme
  0xf4, // key repeat (delay 15, speed 4)
  1, // note preview
  0, // dirty
  0, // font
];

export const UNTITLED = "UNTITLED";
