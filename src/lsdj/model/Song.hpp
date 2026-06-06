#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "Instrument.hpp"
#include "Types.hpp"

// The decompressed LSDj song (0x8000 bytes on disk) as a semantic tree.
// `std::optional` marks "not allocated" (chains/phrases/tables/instruments
// have allocation bitsets / sentinels); arrays are fixed to the on-disk counts.
namespace rp::lsdj::model {

struct Phrase {
    std::array<Byte, 16>                notes{};         // 0 = no note
    std::array<std::optional<Byte>, 16> instruments{};   // 0xFF = none
    std::array<Command, 16>             commands{};
    std::array<Byte, 16>                commandValues{};
};

struct Chain {
    std::array<std::optional<Byte>, 16> phrases{};        // 0xFF = none
    std::array<Byte, 16>                transpositions{};
};

struct Table {
    std::array<Byte, 16>    volumes{};
    std::array<Byte, 16>    transpositions{};
    std::array<Command, 16> command1{};
    std::array<Byte, 16>    command1Values{};
    std::array<Command, 16> command2{};
    std::array<Byte, 16>    command2Values{};
};

struct Groove {
    std::array<Byte, 16> steps{}; // 0 = no value
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
    std::array<Byte, 16> frames{}; // 16 bytes = 32 4-bit samples
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
    std::array<std::optional<Byte>, 4> chains{};
};

struct Song {
    Byte         formatVersion = 22;
    SongSettings settings;

    std::array<SongRow, 256>                  rows{};
    std::array<std::optional<Chain>, 128>     chains{};
    std::array<std::optional<Phrase>, 256>    phrases{};
    std::array<std::optional<Instrument>, 64> instruments{};
    std::array<std::optional<Table>, 32>      tables{};
    std::array<Groove, 31>                    grooves{};
    std::array<Synth, 16>                     synths{};
    std::array<Wave, 256>                     waves{};
};

} // namespace rp::lsdj::model
