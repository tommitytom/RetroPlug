// Song-relative region offsets + geometry for the 0x8000-byte LSDj song body —
// the pure-TS port of Regions.hpp. Offsets == liblsdj song_offsets.h (empirically
// confirmed against a real lsdj9_4_2.sav). `regions(fmt)` returns the single
// modern table for EVERY format version: the layout is version-stable across the
// whole supported range (fmt 0..22); only per-field bit arithmetic differs (see
// song.ts). Upstream liblsdj likewise has no per-version offset tables.

// Geometry (counts/lengths), version-stable.
export const kSongByteCount = 0x8000;
export const kFormatVersionOff = 0x7fff;

export const kChannelCount = 4;
export const kSongRowCount = 256; // CHAIN_ASSIGNMENTS = 1024B = 256*4
export const kPhraseCount = 0xff; // 255 addressable; arrays sized 256
export const kPhraseLength = 16;
export const kChainCount = 0x80; // 128 slots; chain 0x7F is addressable
export const kChainLength = 16;
export const kInstrumentCount = 0x40; // 64
export const kInstrumentBytes = 16;
export const kTableCount = 0x20; // 32
export const kTableLength = 16;
export const kGrooveCount = 0x20; // 32 physical slots (LSDj inits all 32 to 6/6)
export const kGrooveLength = 16;
export const kSynthCount = 0x10; // 16
export const kSynthBytes = 16;
export const kWaveSlotCount = 0x100; // 256 wave slots
export const kWaveBytes = 16;

export interface SongRegions {
  phraseNotes: number;
  bookmarks: number;
  grooves: number;
  chainAssignments: number;
  tableEnvelopes: number;
  words: number;
  wordNames: number;
  rb1: number;
  instrumentNames: number;
  tableAllocTable: number;
  instrumentAllocTable: number;
  chainPhrases: number;
  chainTranspositions: number;
  instrumentParams: number;
  tableTransposition: number;
  tableCommand1: number;
  tableCommand1Value: number;
  tableCommand2: number;
  tableCommand2Value: number;
  rb2: number;
  phraseAllocations: number;
  chainAllocations: number;
  synthParams: number;
  workHours: number;
  workMinutes: number;
  tempo: number;
  transposition: number;
  totalDays: number;
  totalHours: number;
  totalMinutes: number;
  totalTimeChecksum: number;
  keyDelay: number;
  keyRepeat: number;
  font: number;
  syncMode: number;
  colorPalette: number;
  cloneMode: number;
  fileChanged: number;
  powerSave: number;
  prelisten: number;
  synthOverwrites: number;
  reserved3FC6: number;
  drumMax: number;
  phraseCommands: number;
  phraseCommandValues: number;
  waves: number;
  phraseInstruments: number;
  rb3: number;
}

// The fmt22 / modern-layout table (== liblsdj song_offsets.h, confirmed).
export const kModernRegions: SongRegions = {
  phraseNotes: 0x0000,
  bookmarks: 0x0ff0,
  grooves: 0x1090,
  chainAssignments: 0x1290,
  tableEnvelopes: 0x1690,
  words: 0x1890,
  wordNames: 0x1dd0,
  rb1: 0x1e78,
  instrumentNames: 0x1e7a,
  tableAllocTable: 0x2020,
  instrumentAllocTable: 0x2040,
  chainPhrases: 0x2080,
  chainTranspositions: 0x2880,
  instrumentParams: 0x3080,
  tableTransposition: 0x3480,
  tableCommand1: 0x3680,
  tableCommand1Value: 0x3880,
  tableCommand2: 0x3a80,
  tableCommand2Value: 0x3c80,
  rb2: 0x3e80,
  phraseAllocations: 0x3e82,
  chainAllocations: 0x3ea2,
  synthParams: 0x3eb2,
  workHours: 0x3fb2,
  workMinutes: 0x3fb3,
  tempo: 0x3fb4,
  transposition: 0x3fb5,
  totalDays: 0x3fb6,
  totalHours: 0x3fb7,
  totalMinutes: 0x3fb8,
  totalTimeChecksum: 0x3fb9,
  keyDelay: 0x3fba,
  keyRepeat: 0x3fbb,
  font: 0x3fbc,
  syncMode: 0x3fbd,
  colorPalette: 0x3fbe,
  cloneMode: 0x3fc0,
  fileChanged: 0x3fc1,
  powerSave: 0x3fc2,
  prelisten: 0x3fc3,
  synthOverwrites: 0x3fc4,
  reserved3FC6: 0x3fc6,
  drumMax: 0x3fd0,
  phraseCommands: 0x4000,
  phraseCommandValues: 0x4ff0,
  waves: 0x6000,
  phraseInstruments: 0x7000,
  rb3: 0x7ff0,
};

// Every supported version maps to the modern table (region layout is fmt-stable).
export function regions(_fmt: number): SongRegions {
  return kModernRegions;
}
