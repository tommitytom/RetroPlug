#pragma once

#include <optional>
#include <string>

#include <rfl/Flatten.hpp>
#include <rfl/Literal.hpp>
#include <rfl/TaggedUnion.hpp>

#include "Types.hpp"

// The LSDj instrument: a tagged union over the 4 channel types. On disk it is a
// 16-byte record whose bytes are reinterpreted per type (byte 1 = ADSR for
// pulse/noise but volume for wave/kit, byte 9 overlaps, etc.) — the codec
// decodes type-first and each arm reads only its own bytes. Here the model
// carries each type only the fields it actually has. Genuinely-shared fields
// live in InstrCommon and are flattened so they sit inline in the JSON.
namespace rp::lsdj::model {

// ADSR envelope — pulse and noise only (meaningful fmt>=11).
struct Adsr {
    // Each speed is the full low nibble of its byte: bits 0-2 are the GB envelope
    // period, bit 3 the direction. Modeled as one 4-bit field so the byte
    // round-trips exactly (older codecs dropped bit 3, losing the direction).
    Nibble initialLevel = 0;
    Nibble attackSpeed  = 0;
    Nibble attackLevel  = 0;
    Nibble decaySpeed   = 0;
    Nibble sustainLevel = 0;
    Nibble releaseSpeed = 0;
};

// Vibrato — pulse, wave, noise.
struct Vibrato {
    VibratoShape     shape     = VibratoShape::Triangle;
    VibratoDirection direction = VibratoDirection::Down;
    PlvSpeed         plvSpeed  = PlvSpeed::Fast;
};

// Fields present on every instrument type (flattened into the JSON object).
struct InstrCommon {
    Panning               panning   = Panning::None;
    std::optional<Nibble> table;                       // nullopt = table off
    TableMode             tableMode = TableMode::Play;
};

struct PulseInstrument {
    using Tag = rfl::Literal<"pulse">;
    std::string               name;
    rfl::Flatten<InstrCommon> common;
    Adsr                      adsr;
    Vibrato                   vibrato;
    bool                      transpose  = true;
    PulseWidth                pulseWidth = PulseWidth::W125;
    Nibble                    finetune   = 0;
    Byte                      pulse2Tune = 0;
    Byte                      sweep      = 0;
    std::optional<Byte>       length;                  // nullopt = infinite
    Byte                      commandRate = 0;
};

struct WaveInstrument {
    using Tag = rfl::Literal<"wave">;
    std::string               name;
    rfl::Flatten<InstrCommon> common;
    Vibrato                   vibrato;
    bool                      transpose = true;
    Byte                      volume    = 0xA8;  // raw byte-1 (8-bit; not a 4-level enum)
    Nibble                    synth     = 0;
    Byte                      wave      = 0;
    WavePlayMode              playMode  = WavePlayMode::Once;
    Nibble                    length    = 0;
    Byte                      speed     = 0;
    Nibble                    loopPos   = 0; // raw byte-2 nibble (repeat is its 0xF view)
    Byte                      commandRate = 0;
};

struct KitInstrument {
    using Tag = rfl::Literal<"kit">;
    std::string               name;
    rfl::Flatten<InstrCommon> common;
    Byte                      volume     = 0xA8;  // raw byte-1 (8-bit; kits use the full range)
    U5                        kit1       = 0;
    U5                        kit2       = 0;
    bool                      halfSpeed  = false;
    KitLoopMode               loop1      = KitLoopMode::Off;
    KitLoopMode               loop2      = KitLoopMode::Off;
    KitDistortion             distortion = KitDistortion::Clip;
    Byte                      pitch      = 0;
    Byte                      length1    = 0; // 0 = AUTO
    Byte                      offset1    = 0;
    Byte                      offset2    = 0; // byte 13 (offset2 XOR length2; raw round-trips)
};

struct NoiseInstrument {
    using Tag = rfl::Literal<"noise">;
    std::string               name;
    rfl::Flatten<InstrCommon> common;
    Adsr                      adsr;
    Vibrato                   vibrato;
    NoiseStability            stability = NoiseStability::Free;
    std::optional<Byte>       length;                  // nullopt = infinite
    Byte                      shape       = 0;
    Byte                      commandRate = 0;
};

using Instrument = rfl::TaggedUnion<"type", PulseInstrument, WaveInstrument,
                                            KitInstrument, NoiseInstrument>;

} // namespace rp::lsdj::model
