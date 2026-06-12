#pragma once

#include <cstdint>

#include <rfl/Validator.hpp>
#include <rfl/comparisons.hpp> // rfl::Maximum

// Shared scalar types and enums for the LSDj song model.
//
// Enums are SEMANTIC (logical 0-based order); the codec is responsible for
// mapping them to/from the raw on-disk bytes (some of which are non-contiguous
// or version-remapped — e.g. the fmt>=10
// WavePlayMode +1 rotation, the fmt>=8 command remap). Keeping the model
// semantic keeps the JSON/zod shape clean and version-independent.
//
// Sub-byte fields use the bounded aliases below rather than a bare uint8_t, so
// the generated zod carries the range (a bare uint8_t reflects to an unbounded
// z.number().int(), which would let an out-of-range fixture "validate" and then
// be silently truncated by the codec). Enforced by a grep check in the tests.
namespace rp::lsdj::model {

using Nibble = rfl::Validator<std::uint8_t, rfl::Maximum<15>>; // 4-bit  (0..15)
using U5     = rfl::Validator<std::uint8_t, rfl::Maximum<31>>; // 5-bit  (0..31) — kit1/kit2
using U3     = rfl::Validator<std::uint8_t, rfl::Maximum<7>>;  // 3-bit  (0..7)
using U2     = rfl::Validator<std::uint8_t, rfl::Maximum<3>>;  // 2-bit  (0..3)
using Byte   = std::uint8_t;                                   // full 8-bit; documents intent

// --- Instruments ---------------------------------------------------------
enum class InstrumentType : std::uint8_t { Pulse = 0, Wave = 1, Kit = 2, Noise = 3 };

enum class Panning : std::uint8_t { None = 0, Left = 1, Right = 2, LeftRight = 3 };

enum class TableMode : std::uint8_t { Play = 0, Step = 1 };

enum class PulseWidth : std::uint8_t { W125 = 0, W25 = 1, W50 = 2, W75 = 3 };

enum class VibratoShape : std::uint8_t { Triangle = 0, Sawtooth = 1, Square = 2 };

enum class VibratoDirection : std::uint8_t { Down = 0, Up = 1 };

enum class PlvSpeed : std::uint8_t { Fast = 0, Tick = 1, Step = 2, Drum = 3 };

enum class WavePlayMode : std::uint8_t { Once = 0, Loop = 1, PingPong = 2, Manual = 3 };

enum class KitLoopMode : std::uint8_t { Off = 0, On = 1, Attack = 2 };

enum class KitDistortion : std::uint8_t { Clip = 0, Shape = 1, Shape2 = 2, Wrap = 3 };

enum class NoiseStability : std::uint8_t { Free = 0, Stable = 1 };

// --- Song settings -------------------------------------------------------
// Stored as a full byte at SYNC_MODE_OFFSET. aboy / newer builds expose extra
// on-screen entries; the codec preserves the raw byte for values beyond these.
enum class SyncMode : std::uint8_t {
    None = 0, Lsdj = 1, Midi = 2, Keyboard = 3, AnalogIn = 4, AnalogOut = 5
};

enum class CloneMode : std::uint8_t { Deep = 0, Slim = 1 };

// --- Synth ---------------------------------------------------------------
enum class SynthWaveform : std::uint8_t { Saw = 0, Square = 1, Triangle = 2 };

enum class SynthFilter : std::uint8_t { LowPass = 0, HighPass = 1, BandPass = 2, AllPass = 3 };

enum class SynthDistortion : std::uint8_t { Clip = 0, Wrap = 1, Fold = 2 };

enum class SynthPhaseCompression : std::uint8_t { Normal = 0, Resync = 1, Resync2 = 2 };

// --- Commands (phrase/table FX) ------------------------------------------
// In-memory order matches liblsdj lsdj_command_t. The on-disk byte differs
// (fmt>=8 inserts B at slot 1 and shifts the rest) — handled entirely by the
// codec; the model/JSON always uses these names.
enum class Command : std::uint8_t {
    None = 0, A = 1, C = 2, D = 3, E = 4, F = 5, G = 6, H = 7, K = 8, L = 9,
    M = 10, O = 11, P = 12, R = 13, S = 14, T = 15, V = 16, W = 17, Z = 18,
    N = 19, X = 20, Q = 21, Y = 22, B = 23
};

} // namespace rp::lsdj::model
