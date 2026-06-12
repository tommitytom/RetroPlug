#pragma once

#include <cstdint>
#include <optional>

#include "FixedArray.hpp"
#include "Instrument.hpp"
#include "Types.hpp"

// The decompressed LSDj song (0x8000 bytes on disk) as a semantic tree.
// `std::optional` marks "not allocated" (chains/phrases/tables/instruments
// have allocation bitsets / sentinels); arrays are fixed to the on-disk counts.
namespace rp::lsdj::model {

struct Phrase {
    FixedArray<Byte, 16>                notes{};         // 0 = no note
    FixedArray<std::optional<Byte>, 16> instruments{};   // 0xFF = none
    FixedArray<Command, 16>             commands{};
    FixedArray<Byte, 16>                commandValues{};
};

struct Chain {
    FixedArray<std::optional<Byte>, 16> phrases{};        // 0xFF = none
    FixedArray<Byte, 16>                transpositions{};
};

struct Table {
    FixedArray<Byte, 16>    volumes{};
    FixedArray<Byte, 16>    transpositions{};
    FixedArray<Command, 16> command1{};
    FixedArray<Byte, 16>    command1Values{};
    FixedArray<Command, 16> command2{};
    FixedArray<Byte, 16>    command2Values{};
};

struct Groove {
    // LSDj's factory default groove is 6/6 (6 ticks per step, the rest "no
    // value"). A zero groove means zero step-duration, so a song authored
    // without explicit grooves never advances under playback/sync. Default to
    // the factory 6/6 so partial fixtures produce a valid, playable sav (matches
    // every stock LSDj .sav; decode overwrites all 16 bytes, so round-trips are
    // unchanged). 0 = no value.
    FixedArray<Byte, 16> steps{ .arr = { 6, 6 } };
};

struct Synth {
    SynthWaveform         waveform         = SynthWaveform::Saw;
    SynthFilter           filter           = SynthFilter::LowPass;
    Nibble                resonanceStart   = 0;
    Nibble                resonanceEnd     = 0;
    SynthDistortion       distortion       = SynthDistortion::Clip;
    SynthPhaseCompression phaseCompression = SynthPhaseCompression::Normal;
    Byte                  volumeStart = 0, cutoffStart = 0, phaseStart = 0, vshiftStart = 0;
    Byte                  volumeEnd   = 0, cutoffEnd   = 0, phaseEnd   = 0, vshiftEnd   = 0;
    Nibble                limitStart = 0, limitEnd = 0;
};

struct Wave {
    FixedArray<Byte, 16> frames{}; // 16 bytes = 32 4-bit samples
};

struct SongSettings {
    std::uint16_t tempo         = 128; // BPM 40..295
    Byte          transposition = 0;
    SyncMode      syncMode      = SyncMode::None;
    CloneMode     cloneMode     = CloneMode::Deep;
    Byte          font          = 0;
    Byte          colorPalette  = 0;
    Byte          keyDelay      = 0;
    Byte          keyRepeat     = 0;
    bool          prelisten     = false;
    Byte          drumMax       = 0;
};

// One SONG-screen row: a chain index per channel (0xFF = empty).
struct SongRow {
    FixedArray<std::optional<Byte>, 4> chains{};
};

struct Song {
    Byte         formatVersion = 22;
    SongSettings settings;

    FixedArray<SongRow, 256>                  rows{};
    FixedArray<std::optional<Chain>, 128>     chains{};
    FixedArray<std::optional<Phrase>, 256>    phrases{};
    FixedArray<std::optional<Instrument>, 64> instruments{};
    FixedArray<std::optional<Table>, 32>      tables{};
    FixedArray<Groove, 32>                    grooves{};
    FixedArray<Synth, 16>                     synths{};
    FixedArray<Wave, 256>                     waves{};

    // Raw byte regions, modeled as blobs so a no-template encode reproduces them
    // (their factory defaults are version-specific; values come from the sav).
    FixedArray<Byte, 0x40>  bookmarks{};       // 0x0FF0  4ch * 16 channel/row slots
    FixedArray<Byte, 0x540> words{};           // 0x1890  SPEECH instrument allophones
    FixedArray<Byte, 0xA8>  wordNames{};       // 0x1DD0  42 * 4-char word names
    FixedArray<Byte, 0x1A6> instrumentNames{}; // 0x1E7A  64 instrument names + trailing
    FixedArray<Byte, 0x02>  synthOverwrites{}; // 0x3FC4  2B bitset, one bit per synth
    FixedArray<Byte, 0x0A>  reserved3FC6{};    // 0x3FC6  reserved ("Empty"); 0xFF*4 + 0 on 9.x
};

} // namespace rp::lsdj::model
