#pragma once

#include <cstdint>

// Song-relative region offsets and geometry for the LSDj 0x8000-byte song body.
//
// EMPIRICALLY CONFIRMED for fmt22: a real lsdj9_4_2.sav carries the `rb`
// validity markers at exactly 0x1E78 / 0x3E80 / 0x7FF0 — liblsdj's
// song_offsets.h values — which pins every region to liblsdj's layout. The
// once-feared "v9.1.5 relocation" does not move these offsets, so for fmt22 the
// codec uses liblsdj's offsets verbatim. `regions(fmt)` exists so genuinely
// different OLD formats can branch later; today every supported (modern)
// version returns the same table. The golden round-trip vs the corpus is the
// validator if a future version turns out to differ.
namespace rp::lsdj::codec {

using FormatVersion = std::uint8_t;

// Geometry (counts/lengths), version-stable across the modern range.
inline constexpr std::size_t kSongByteCount   = 0x8000;
inline constexpr std::size_t kFormatVersionOff = 0x7FFF;

inline constexpr std::size_t kChannelCount    = 4;
inline constexpr std::size_t kSongRowCount     = 256;  // CHAIN_ASSIGNMENTS = 1024B = 256*4
inline constexpr std::size_t kPhraseCount      = 0xFF; // 255 addressable; arrays sized 256
inline constexpr std::size_t kPhraseLength     = 16;
inline constexpr std::size_t kChainCount       = 0x7F; // 127; arrays sized 128
inline constexpr std::size_t kChainLength      = 16;
inline constexpr std::size_t kInstrumentCount  = 0x40; // 64
inline constexpr std::size_t kInstrumentBytes  = 16;
inline constexpr std::size_t kTableCount       = 0x20; // 32
inline constexpr std::size_t kTableLength      = 16;
inline constexpr std::size_t kGrooveCount      = 0x20; // 32 physical slots (0x1090..0x1290); liblsdj addresses 0x1F but LSDj inits all 32 to 6/6
inline constexpr std::size_t kGrooveLength     = 16;
inline constexpr std::size_t kSynthCount       = 0x10; // 16
inline constexpr std::size_t kSynthBytes       = 16;
inline constexpr std::size_t kWaveSlotCount    = 0x100; // 256 wave slots
inline constexpr std::size_t kWaveBytes        = 16;
inline constexpr std::size_t kWavePerSynth     = 0x0F; // 15

// Region base offsets (song-relative). Names mirror liblsdj song_offsets.h.
struct SongRegions {
    std::size_t phraseNotes;
    std::size_t bookmarks;            // 64B = 4ch * 16 slots
    std::size_t grooves;
    std::size_t chainAssignments;     // row*4 + channel grid (1024B)
    std::size_t tableEnvelopes;
    std::size_t words;
    std::size_t wordNames;
    std::size_t rb1;
    std::size_t instrumentNames;      // 5 bytes each
    std::size_t tableAllocTable;
    std::size_t instrumentAllocTable;
    std::size_t chainPhrases;
    std::size_t chainTranspositions;
    std::size_t instrumentParams;
    std::size_t tableTransposition;
    std::size_t tableCommand1;
    std::size_t tableCommand1Value;
    std::size_t tableCommand2;
    std::size_t tableCommand2Value;
    std::size_t rb2;
    std::size_t phraseAllocations;    // 32B bitset
    std::size_t chainAllocations;     // 16B bitset
    std::size_t synthParams;
    std::size_t workHours;
    std::size_t workMinutes;
    std::size_t tempo;
    std::size_t transposition;
    std::size_t totalDays;
    std::size_t totalHours;
    std::size_t totalMinutes;
    std::size_t totalTimeChecksum;
    std::size_t keyDelay;
    std::size_t keyRepeat;
    std::size_t font;
    std::size_t syncMode;
    std::size_t colorPalette;
    std::size_t cloneMode;
    std::size_t fileChanged;
    std::size_t powerSave;
    std::size_t prelisten;
    std::size_t synthOverwrites;      // 2B bitset, one bit per synth
    std::size_t reserved3FC6;         // 10B reserved ("Empty" in liblsdj); 0xFF FF FF FF + 0 on 9.x
    std::size_t drumMax;
    std::size_t phraseCommands;
    std::size_t phraseCommandValues;
    std::size_t waves;
    std::size_t phraseInstruments;
    std::size_t rb3;
};

// The fmt22 / modern-layout table (== liblsdj song_offsets.h, confirmed).
inline constexpr SongRegions kModernRegions = {
    /*phraseNotes*/          0x0000,
    /*bookmarks*/            0x0FF0,
    /*grooves*/              0x1090,
    /*chainAssignments*/     0x1290,
    /*tableEnvelopes*/       0x1690,
    /*words*/                0x1890,
    /*wordNames*/            0x1DD0,
    /*rb1*/                  0x1E78,
    /*instrumentNames*/      0x1E7A,
    /*tableAllocTable*/      0x2020,
    /*instrumentAllocTable*/ 0x2040,
    /*chainPhrases*/         0x2080,
    /*chainTranspositions*/  0x2880,
    /*instrumentParams*/     0x3080,
    /*tableTransposition*/   0x3480,
    /*tableCommand1*/        0x3680,
    /*tableCommand1Value*/   0x3880,
    /*tableCommand2*/        0x3A80,
    /*tableCommand2Value*/   0x3C80,
    /*rb2*/                  0x3E80,
    /*phraseAllocations*/    0x3E82,
    /*chainAllocations*/     0x3EA2,
    /*synthParams*/          0x3EB2,
    /*workHours*/            0x3FB2,
    /*workMinutes*/          0x3FB3,
    /*tempo*/                0x3FB4,
    /*transposition*/        0x3FB5,
    /*totalDays*/            0x3FB6,
    /*totalHours*/           0x3FB7,
    /*totalMinutes*/         0x3FB8,
    /*totalTimeChecksum*/    0x3FB9,
    /*keyDelay*/             0x3FBA,
    /*keyRepeat*/            0x3FBB,
    /*font*/                 0x3FBC,
    /*syncMode*/             0x3FBD,
    /*colorPalette*/         0x3FBE,
    /*cloneMode*/            0x3FC0,
    /*fileChanged*/          0x3FC1,
    /*powerSave*/            0x3FC2,
    /*prelisten*/            0x3FC3,
    /*synthOverwrites*/      0x3FC4,
    /*reserved3FC6*/         0x3FC6,
    /*drumMax*/              0x3FD0,
    /*phraseCommands*/       0x4000,
    /*phraseCommandValues*/  0x4FF0,
    /*waves*/                0x6000,
    /*phraseInstruments*/    0x7000,
    /*rb3*/                  0x7FF0,
};

// For now every supported version maps to the modern table. Older formats
// (fmt<7) that genuinely differ will branch here, validated by golden savs.
inline constexpr const SongRegions& regions(FormatVersion /*fmt*/) {
    return kModernRegions;
}

} // namespace rp::lsdj::codec
