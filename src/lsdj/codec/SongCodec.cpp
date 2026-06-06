#include "lsdj/codec/SongCodec.hpp"

#include "lsdj/codec/Regions.hpp"
#include "lsdj/codec/SavView.hpp"

#include <cstring>
#include <type_traits>

// Faithful port of liblsdj's accessor logic for the modern (fmt22) layout.
// Each instrument arm reads only its own bytes (the 16-byte record is a
// type-tagged union of overlapping byte meanings); array regions are flat
// fixed-stride copies. Offsets come from Regions.hpp (== liblsdj, confirmed).
namespace rp::lsdj::codec {
namespace {

using namespace rp::lsdj::model;

template <typename E>
E toEnum(std::uint8_t raw, std::uint8_t maxValid) {
    return static_cast<E>(raw > maxValid ? 0 : raw);
}

WaveVolume decodeWaveVolume(std::uint8_t raw) {
    switch (raw) {
        case 0x60: return WaveVolume::V1;
        case 0x40: return WaveVolume::V2;
        case 0xA8: return WaveVolume::V3;
        default:   return WaveVolume::V0; // includes 0x00
    }
}

// On-disk command byte -> Command. fmt>=8 inserts B at slot 1 and shifts the
// rest up by one; fmt<8 stores the raw enum (no B).
Command decodeCommand(std::uint8_t b, FormatVersion fmt) {
    if (fmt < 8) return b <= 23 ? static_cast<Command>(b) : Command::None;
    if (b == 0) return Command::None;
    if (b == 1) return Command::B;
    const std::uint8_t v = b - 1;
    return v <= 23 ? static_cast<Command>(v) : Command::None;
}

Vibrato decodeVibrato(const SavView& v, std::size_t base, FormatVersion fmt) {
    Vibrato vib;
    vib.direction = toEnum<VibratoDirection>(v.bits(base + 5, 0, 1), 1);
    if (fmt >= 4) {
        vib.shape = toEnum<VibratoShape>(v.bits(base + 5, 1, 2), 2);
        const std::uint8_t b5 = v.u8(base + 5);
        vib.plvSpeed = (b5 & 0x80) ? PlvSpeed::Step
                     : (b5 & 0x10) ? PlvSpeed::Tick
                                   : PlvSpeed::Fast;
    } else {
        // fmt<4: shape and PLV speed share byte5[1,2].
        switch (v.bits(base + 5, 1, 2)) {
            case 0:  vib.shape = VibratoShape::Triangle; vib.plvSpeed = PlvSpeed::Fast; break;
            case 1:  vib.shape = VibratoShape::Sawtooth; vib.plvSpeed = PlvSpeed::Tick; break;
            case 2:  vib.shape = VibratoShape::Triangle; vib.plvSpeed = PlvSpeed::Tick; break;
            default: vib.shape = VibratoShape::Square;   vib.plvSpeed = PlvSpeed::Tick; break;
        }
    }
    return vib;
}

Adsr decodeAdsr(const SavView& v, std::size_t base, FormatVersion fmt) {
    Adsr a;
    a.initialLevel = v.bits(base + 1, 4, 4);
    a.attackSpeed  = (fmt >= 13) ? v.bits(base + 1, 0, 4) : v.bits(base + 1, 0, 3);
    a.attackLevel  = v.bits(base + 9, 4, 4);
    a.decaySpeed   = v.bits(base + 9, 0, 3);
    a.sustainLevel = v.bits(base + 0xA, 4, 4);
    a.releaseSpeed = v.bits(base + 0xA, 0, 3);
    return a;
}

InstrCommon decodeCommon(const SavView& v, std::size_t base) {
    InstrCommon c;
    c.panning   = toEnum<Panning>(v.bits(base + 7, 0, 2), 3);
    c.tableMode = (v.bits(base + 5, 3, 1) == 1) ? TableMode::Step : TableMode::Play;
    if (v.bits(base + 6, 5, 1) == 1) c.table = v.bits(base + 6, 0, 4); // enabled
    return c;
}

// Pulse/noise length: bit6==0 => infinite (nullopt); else (~bits[0,5]) & 0x3F.
std::optional<Byte> decodeLength(const SavView& v, std::size_t base) {
    if (v.bits(base + 3, 6, 1) == 0) return std::nullopt;
    return static_cast<Byte>((~v.bits(base + 3, 0, 5)) & 0x3F);
}

Instrument decodeInstrument(const SavView& v, std::size_t base, FormatVersion fmt) {
    const std::uint8_t type = v.u8(base + 0);
    const bool transpose = v.bits(base + 5, 5, 1) == 0; // stored inverted

    switch (type) {
        case 1: { // WAVE
            WaveInstrument w;
            w.common      = decodeCommon(v, base);
            w.vibrato     = decodeVibrato(v, base, fmt);
            w.transpose   = transpose;
            w.volume      = decodeWaveVolume(v.u8(base + 1));
            w.synth       = (fmt >= 16) ? v.bits(base + 3, 4, 4) : v.bits(base + 2, 4, 4);
            w.wave        = v.u8(base + 3);
            w.playMode    = (fmt >= 10) ? toEnum<WavePlayMode>((v.bits(base + 9, 0, 2) + 3) & 3, 3) // (raw-1)&3
                                        : toEnum<WavePlayMode>(v.bits(base + 9, 0, 2), 3);
            w.length      = (fmt >= 7) ? static_cast<Byte>(0xF - v.bits(base + 0xA, 0, 4))
                          : (fmt == 6) ? static_cast<Byte>(v.bits(base + 0xA, 0, 4))
                                       : static_cast<Byte>(v.bits(base + 0xE, 4, 4));
            w.speed       = (fmt >= 7) ? static_cast<Byte>(v.u8(base + 0xB) + 4)
                          : (fmt == 6) ? static_cast<Byte>(v.u8(base + 0xB) + 1)
                                       : static_cast<Byte>(v.bits(base + 0xE, 0, 4) + 1);
            w.loopPos     = (fmt >= 9) ? v.bits(base + 2, 0, 4)
                                       : static_cast<Byte>(v.bits(base + 2, 0, 4) ^ 0x0F);
            w.commandRate = v.u8(base + 8);
            return w;
        }
        case 2: { // KIT
            KitInstrument k;
            k.common     = decodeCommon(v, base);
            k.volume     = decodeWaveVolume(v.u8(base + 1));
            k.kit1       = v.bits(base + 2, 0, 5);
            k.kit2       = v.bits(base + 9, 0, 5);
            k.halfSpeed  = v.bits(base + 2, 6, 1) == 1;
            k.loop1      = v.bits(base + 2, 7, 1) ? KitLoopMode::Attack
                         : (v.bits(base + 5, 6, 1) ? KitLoopMode::On : KitLoopMode::Off);
            k.loop2      = v.bits(base + 9, 7, 1) ? KitLoopMode::Attack
                         : (v.bits(base + 5, 5, 1) ? KitLoopMode::On : KitLoopMode::Off);
            k.distortion = toEnum<KitDistortion>(v.bits(base + 0xA, 0, 2), 3);
            k.pitch      = v.u8(base + 8);
            k.length1    = v.u8(base + 3);
            k.offset1    = v.u8(base + 0xC);
            k.offset2    = v.u8(base + 0xD);
            return k;
        }
        case 3: { // NOISE
            NoiseInstrument n;
            n.common      = decodeCommon(v, base);
            n.adsr        = decodeAdsr(v, base, fmt);
            n.vibrato     = decodeVibrato(v, base, fmt);
            n.stability   = toEnum<NoiseStability>(v.bits(base + 2, 0, 1), 1);
            n.length      = decodeLength(v, base);
            n.shape       = v.u8(base + 4);
            n.commandRate = v.u8(base + 8);
            return n;
        }
        default: { // PULSE (type 0 and any unknown -> pulse)
            PulseInstrument p;
            p.common      = decodeCommon(v, base);
            p.adsr        = decodeAdsr(v, base, fmt);
            p.vibrato     = decodeVibrato(v, base, fmt);
            p.transpose   = transpose;
            p.pulseWidth  = toEnum<PulseWidth>(v.bits(base + 7, 6, 2), 3);
            p.finetune    = v.bits(base + 7, 2, 4);
            p.pulse2Tune  = v.u8(base + 2);
            p.sweep       = v.u8(base + 4);
            p.length      = decodeLength(v, base);
            p.commandRate = v.u8(base + 8);
            return p;
        }
    }
}

bool allocBit(const SavView& v, std::size_t tableOff, std::size_t index) {
    return (v.u8(tableOff + index / 8) & (1u << (index % 8))) != 0;
}

// ---- encode helpers (exact inverse of the decode helpers) ----------------

std::uint8_t encodeWaveVolume(WaveVolume v) {
    switch (v) {
        case WaveVolume::V1: return 0x60;
        case WaveVolume::V2: return 0x40;
        case WaveVolume::V3: return 0xA8;
        default:             return 0x00;
    }
}

std::uint8_t encodeCommand(Command c, FormatVersion fmt) {
    if (fmt < 8) return static_cast<std::uint8_t>(c); // raw enum (B doesn't occur on fmt<8)
    if (c == Command::None) return 0;
    if (c == Command::B)    return 1;
    return static_cast<std::uint8_t>(static_cast<std::uint8_t>(c) + 1);
}

void encodeVibrato(SavWriter& w, std::size_t base, const Vibrato& vib, FormatVersion fmt) {
    w.setBits(base + 5, 0, 1, static_cast<std::uint8_t>(vib.direction));
    if (fmt >= 4) {
        w.setBits(base + 5, 1, 2, static_cast<std::uint8_t>(vib.shape));
        w.setBits(base + 5, 7, 1, vib.plvSpeed == PlvSpeed::Step ? 1 : 0);
        w.setBits(base + 5, 4, 1, vib.plvSpeed == PlvSpeed::Tick ? 1 : 0);
    } else {
        // fmt<4: (shape,plv) pack into byte5[1,2] — exact inverse of decode's
        // switch for the 4 legal pairs (others fall back to Triangle/Fast).
        std::uint8_t bits = 0;
        if (vib.plvSpeed == PlvSpeed::Tick) {
            bits = (vib.shape == VibratoShape::Sawtooth) ? 1
                 : (vib.shape == VibratoShape::Triangle) ? 2 : 3;
        }
        w.setBits(base + 5, 1, 2, bits);
    }
}

void encodeAdsr(SavWriter& w, std::size_t base, const Adsr& a, FormatVersion fmt) {
    w.setBits(base + 1, 4, 4, a.initialLevel.value());
    w.setBits(base + 1, 0, (fmt >= 13) ? 4 : 3, a.attackSpeed.value()); // mirror 4-bit read @fmt>=13
    w.setBits(base + 9, 4, 4, a.attackLevel.value());
    w.setBits(base + 9, 0, 3, a.decaySpeed.value());
    w.setBits(base + 0xA, 4, 4, a.sustainLevel.value());
    w.setBits(base + 0xA, 0, 3, a.releaseSpeed.value());
}

void encodeCommon(SavWriter& w, std::size_t base, const InstrCommon& c) {
    w.setBits(base + 7, 0, 2, static_cast<std::uint8_t>(c.panning));
    w.setBits(base + 5, 3, 1, c.tableMode == TableMode::Step ? 1 : 0);
    if (c.table) {
        w.setBits(base + 6, 5, 1, 1);
        w.setBits(base + 6, 0, 4, c.table->value());
    } else {
        w.setBits(base + 6, 5, 1, 0);
    }
}

void encodeLength(SavWriter& w, std::size_t base, const std::optional<Byte>& length) {
    if (!length) {
        w.setBits(base + 3, 6, 1, 0); // infinite
    } else {
        w.setBits(base + 3, 6, 1, 1);
        w.setBits(base + 3, 0, 5, static_cast<std::uint8_t>((~*length) & 0x1F));
    }
}

void encodeInstrument(SavWriter& w, std::size_t base, const Instrument& inst, FormatVersion fmt) {
    inst.visit([&](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, WaveInstrument>) {
            w.setU8(base + 0, 1);
            encodeCommon(w, base, v.common.get());
            encodeVibrato(w, base, v.vibrato, fmt);
            w.setBits(base + 5, 5, 1, v.transpose ? 0 : 1);
            w.setU8(base + 1, encodeWaveVolume(v.volume));
            w.setU8(base + 3, v.wave);                       // wave = full byte 3
            // synth: byte3 hi-nibble (fmt>=16, written after wave) else byte2 hi-nibble.
            w.setBits((fmt >= 16) ? base + 3 : base + 2, 4, 4, v.synth.value());
            w.setBits(base + 9, 0, 2, (fmt >= 10)
                ? static_cast<std::uint8_t>((static_cast<int>(v.playMode) + 1) & 3)
                : static_cast<std::uint8_t>(v.playMode));
            if (fmt >= 7)      w.setBits(base + 0xA, 0, 4, static_cast<std::uint8_t>(0xF - v.length.value()));
            else if (fmt == 6) w.setBits(base + 0xA, 0, 4, v.length.value());
            else               w.setBits(base + 0xE, 4, 4, v.length.value());
            if (fmt >= 7)      w.setU8(base + 0xB, static_cast<std::uint8_t>(v.speed - 4));
            else if (fmt == 6) w.setU8(base + 0xB, static_cast<std::uint8_t>(v.speed - 1));
            else               w.setBits(base + 0xE, 0, 4, static_cast<std::uint8_t>(v.speed - 1));
            w.setBits(base + 2, 0, 4, (fmt >= 9) ? v.loopPos.value()
                                                 : static_cast<std::uint8_t>(v.loopPos.value() ^ 0x0F));
            w.setU8(base + 8, v.commandRate);
        } else if constexpr (std::is_same_v<T, KitInstrument>) {
            w.setU8(base + 0, 2);
            encodeCommon(w, base, v.common.get());
            w.setU8(base + 1, encodeWaveVolume(v.volume));
            w.setBits(base + 2, 0, 5, v.kit1.value());
            w.setBits(base + 9, 0, 5, v.kit2.value());
            w.setBits(base + 2, 6, 1, v.halfSpeed ? 1 : 0);
            w.setBits(base + 2, 7, 1, v.loop1 == KitLoopMode::Attack ? 1 : 0);
            w.setBits(base + 5, 6, 1, v.loop1 == KitLoopMode::On ? 1 : 0);
            w.setBits(base + 9, 7, 1, v.loop2 == KitLoopMode::Attack ? 1 : 0);
            w.setBits(base + 5, 5, 1, v.loop2 == KitLoopMode::On ? 1 : 0);
            w.setBits(base + 0xA, 0, 2, static_cast<std::uint8_t>(v.distortion));
            w.setU8(base + 8, v.pitch);
            w.setU8(base + 3, v.length1);
            w.setU8(base + 0xC, v.offset1);
            w.setU8(base + 0xD, v.offset2);
        } else if constexpr (std::is_same_v<T, NoiseInstrument>) {
            w.setU8(base + 0, 3);
            encodeCommon(w, base, v.common.get());
            encodeAdsr(w, base, v.adsr, fmt);
            encodeVibrato(w, base, v.vibrato, fmt);
            w.setBits(base + 2, 0, 1, static_cast<std::uint8_t>(v.stability));
            encodeLength(w, base, v.length);
            w.setU8(base + 4, v.shape);
            w.setU8(base + 8, v.commandRate);
        } else { // PulseInstrument
            w.setU8(base + 0, 0);
            encodeCommon(w, base, v.common.get());
            encodeAdsr(w, base, v.adsr, fmt);
            encodeVibrato(w, base, v.vibrato, fmt);
            w.setBits(base + 5, 5, 1, v.transpose ? 0 : 1);
            w.setBits(base + 7, 6, 2, static_cast<std::uint8_t>(v.pulseWidth));
            w.setBits(base + 7, 2, 4, v.finetune.value());
            w.setU8(base + 2, v.pulse2Tune);
            w.setU8(base + 4, v.sweep);
            encodeLength(w, base, v.length);
            w.setU8(base + 8, v.commandRate);
        }
    });
}

} // namespace

rfl::Result<model::Song> decodeSong(std::span<const std::uint8_t> songBytes) {
    if (songBytes.size() < kSongByteCount)
        return rfl::error("song body smaller than 0x8000 bytes");

    const SavView v(songBytes.data(), songBytes.size());
    const FormatVersion fmt = v.u8(kFormatVersionOff);
    const SongRegions& r = regions(fmt);

    Song song;
    song.formatVersion = fmt;

    // --- settings ---
    {
        SongSettings& s = song.settings;
        const std::uint8_t tempoByte = v.u8(r.tempo);
        s.tempo         = tempoByte < 40 ? static_cast<std::uint16_t>(tempoByte + 256) : tempoByte;
        s.transposition = v.u8(r.transposition);
        s.syncMode      = static_cast<SyncMode>(v.u8(r.syncMode)); // raw; may exceed enum
        s.cloneMode     = toEnum<CloneMode>(v.u8(r.cloneMode), 1);
        s.font          = v.u8(r.font);
        s.colorPalette  = v.u8(r.colorPalette);
        s.keyDelay      = v.u8(r.keyDelay);
        s.keyRepeat     = v.u8(r.keyRepeat);
        s.prelisten     = v.u8(r.prelisten) == 1;
        s.drumMax       = v.u8(r.drumMax);
    }

    // --- SONG-screen grid: rows x channels -> chain index (0xFF = empty) ---
    for (std::size_t row = 0; row < kSongRowCount; ++row)
        for (std::size_t ch = 0; ch < kChannelCount; ++ch) {
            const std::uint8_t c = v.u8(r.chainAssignments + row * kChannelCount + ch);
            if (c != 0xFF) song.rows[row].chains[ch] = c;
        }

    // --- chains (alloc bitset @ chainAllocations) ---
    for (std::size_t i = 0; i < kChainCount; ++i) {
        if (!allocBit(v, r.chainAllocations, i)) continue;
        Chain c;
        for (std::size_t step = 0; step < kChainLength; ++step) {
            const std::uint8_t ph = v.u8(r.chainPhrases + i * kChainLength + step);
            if (ph != 0xFF) c.phrases[step] = ph;
            c.transpositions[step] = v.u8(r.chainTranspositions + i * kChainLength + step);
        }
        song.chains[i] = c;
    }

    // --- phrases (alloc bitset @ phraseAllocations) ---
    for (std::size_t i = 0; i < kPhraseCount; ++i) {
        if (!allocBit(v, r.phraseAllocations, i)) continue;
        Phrase p;
        for (std::size_t step = 0; step < kPhraseLength; ++step) {
            const std::size_t idx = i * kPhraseLength + step;
            p.notes[step] = v.u8(r.phraseNotes + idx);
            const std::uint8_t ins = v.u8(r.phraseInstruments + idx);
            if (ins != 0xFF) p.instruments[step] = ins;
            p.commands[step]      = decodeCommand(v.u8(r.phraseCommands + idx), fmt);
            p.commandValues[step] = v.u8(r.phraseCommandValues + idx);
        }
        song.phrases[i] = p;
    }

    // --- instruments (alloc table is one byte each @ instrumentAllocTable) ---
    for (std::size_t i = 0; i < kInstrumentCount; ++i) {
        if (v.u8(r.instrumentAllocTable + i) == 0) continue;
        song.instruments[i] = decodeInstrument(v, r.instrumentParams + i * kInstrumentBytes, fmt);
    }

    // --- tables (alloc table is one byte each @ tableAllocTable) ---
    for (std::size_t i = 0; i < kTableCount; ++i) {
        if (v.u8(r.tableAllocTable + i) == 0) continue;
        Table t;
        for (std::size_t step = 0; step < kTableLength; ++step) {
            const std::size_t idx = i * kTableLength + step;
            t.volumes[step]        = v.u8(r.tableEnvelopes + idx);
            t.transpositions[step] = v.u8(r.tableTransposition + idx);
            t.command1[step]       = decodeCommand(v.u8(r.tableCommand1 + idx), fmt);
            t.command1Values[step] = v.u8(r.tableCommand1Value + idx);
            t.command2[step]       = decodeCommand(v.u8(r.tableCommand2 + idx), fmt);
            t.command2Values[step] = v.u8(r.tableCommand2Value + idx);
        }
        song.tables[i] = t;
    }

    // --- grooves (no alloc bitset; always present) ---
    for (std::size_t i = 0; i < kGrooveCount; ++i)
        for (std::size_t step = 0; step < kGrooveLength; ++step)
            song.grooves[i].steps[step] = v.u8(r.grooves + i * kGrooveLength + step);

    // --- synths ---
    for (std::size_t i = 0; i < kSynthCount; ++i) {
        const std::size_t b = r.synthParams + i * kSynthBytes;
        Synth& s = song.synths[i];
        s.waveform         = toEnum<SynthWaveform>(v.u8(b + 0), 2);
        s.filter           = toEnum<SynthFilter>(v.u8(b + 1), 3);
        s.resonanceStart   = (fmt >= 5) ? ((v.u8(b + 2) & 0xF0) >> 4) : (v.u8(b + 2) & 0x0F);
        s.resonanceEnd     = v.u8(b + 2) & 0x0F;
        s.distortion       = toEnum<SynthDistortion>(v.u8(b + 3), 2);
        s.phaseCompression = toEnum<SynthPhaseCompression>(v.u8(b + 4), 2);
        s.volumeStart = v.u8(b + 5);  s.cutoffStart = v.u8(b + 6);
        s.phaseStart  = v.u8(b + 7);  s.vshiftStart = v.u8(b + 8);
        s.volumeEnd   = v.u8(b + 9);  s.cutoffEnd   = v.u8(b + 10);
        s.phaseEnd    = v.u8(b + 11); s.vshiftEnd   = v.u8(b + 12);
        s.limitStart  = static_cast<Byte>(0xF - ((v.u8(b + 13) & 0xF0) >> 4));
        s.limitEnd    = static_cast<Byte>(0xF - (v.u8(b + 13) & 0x0F));
    }

    // --- waves (raw 16-byte frames) ---
    for (std::size_t i = 0; i < kWaveSlotCount; ++i)
        for (std::size_t b = 0; b < kWaveBytes; ++b)
            song.waves[i].frames[b] = v.u8(r.waves + i * kWaveBytes + b);

    return song;
}

std::vector<std::uint8_t> encodeSong(const model::Song& song,
                                     std::span<const std::uint8_t> templateBytes) {
    std::vector<std::uint8_t> out(kSongByteCount, 0);
    if (templateBytes.size() >= kSongByteCount)
        std::memcpy(out.data(), templateBytes.data(), kSongByteCount);

    SavWriter w(out.data(), out.size());
    const FormatVersion fmt = song.formatVersion;
    const SongRegions& r = regions(fmt);

    w.setU8(kFormatVersionOff, fmt);
    for (std::size_t off : {r.rb1, r.rb2, r.rb3}) { w.setU8(off, 'r'); w.setU8(off + 1, 'b'); }

    // --- settings ---
    {
        const SongSettings& s = song.settings;
        w.setU8(r.tempo, static_cast<std::uint8_t>(s.tempo >= 256 ? s.tempo - 256 : s.tempo));
        w.setU8(r.transposition, s.transposition);
        w.setU8(r.syncMode, static_cast<std::uint8_t>(s.syncMode));
        w.setU8(r.cloneMode, static_cast<std::uint8_t>(s.cloneMode));
        w.setU8(r.font, s.font);
        w.setU8(r.colorPalette, s.colorPalette);
        w.setU8(r.keyDelay, s.keyDelay);
        w.setU8(r.keyRepeat, s.keyRepeat);
        w.setU8(r.prelisten, s.prelisten ? 1 : 0);
        w.setU8(r.drumMax, s.drumMax);
    }

    // --- SONG grid ---
    for (std::size_t row = 0; row < kSongRowCount; ++row)
        for (std::size_t ch = 0; ch < kChannelCount; ++ch) {
            const auto& c = song.rows[row].chains[ch];
            w.setU8(r.chainAssignments + row * kChannelCount + ch, c ? *c : 0xFF);
        }

    // --- chains (regenerate 16-byte alloc bitset from optionals) ---
    for (std::size_t b = 0; b < 16; ++b) w.setU8(r.chainAllocations + b, 0);
    for (std::size_t i = 0; i < kChainCount; ++i) {
        if (!song.chains[i]) continue;
        w.setBits(r.chainAllocations + i / 8, i % 8, 1, 1);
        const Chain& c = *song.chains[i];
        for (std::size_t step = 0; step < kChainLength; ++step) {
            const std::size_t idx = i * kChainLength + step;
            w.setU8(r.chainPhrases + idx, c.phrases[step] ? *c.phrases[step] : 0xFF);
            w.setU8(r.chainTranspositions + idx, c.transpositions[step]);
        }
    }

    // --- phrases (regenerate 32-byte alloc bitset) ---
    for (std::size_t b = 0; b < 32; ++b) w.setU8(r.phraseAllocations + b, 0);
    for (std::size_t i = 0; i < kPhraseCount; ++i) {
        if (!song.phrases[i]) continue;
        w.setBits(r.phraseAllocations + i / 8, i % 8, 1, 1);
        const Phrase& p = *song.phrases[i];
        for (std::size_t step = 0; step < kPhraseLength; ++step) {
            const std::size_t idx = i * kPhraseLength + step;
            w.setU8(r.phraseNotes + idx, p.notes[step]);
            w.setU8(r.phraseInstruments + idx, p.instruments[step] ? *p.instruments[step] : 0xFF);
            w.setU8(r.phraseCommands + idx, encodeCommand(p.commands[step], fmt));
            w.setU8(r.phraseCommandValues + idx, p.commandValues[step]);
        }
    }

    // --- instruments (1-byte alloc table) ---
    for (std::size_t i = 0; i < kInstrumentCount; ++i) {
        if (!song.instruments[i]) { w.setU8(r.instrumentAllocTable + i, 0); continue; }
        w.setU8(r.instrumentAllocTable + i, 1);
        encodeInstrument(w, r.instrumentParams + i * kInstrumentBytes, *song.instruments[i], fmt);
    }

    // --- tables (1-byte alloc table) ---
    for (std::size_t i = 0; i < kTableCount; ++i) {
        if (!song.tables[i]) { w.setU8(r.tableAllocTable + i, 0); continue; }
        w.setU8(r.tableAllocTable + i, 1);
        const Table& t = *song.tables[i];
        for (std::size_t step = 0; step < kTableLength; ++step) {
            const std::size_t idx = i * kTableLength + step;
            w.setU8(r.tableEnvelopes + idx, t.volumes[step]);
            w.setU8(r.tableTransposition + idx, t.transpositions[step]);
            w.setU8(r.tableCommand1 + idx, encodeCommand(t.command1[step], fmt));
            w.setU8(r.tableCommand1Value + idx, t.command1Values[step]);
            w.setU8(r.tableCommand2 + idx, encodeCommand(t.command2[step], fmt));
            w.setU8(r.tableCommand2Value + idx, t.command2Values[step]);
        }
    }

    // --- grooves ---
    for (std::size_t i = 0; i < kGrooveCount; ++i)
        for (std::size_t step = 0; step < kGrooveLength; ++step)
            w.setU8(r.grooves + i * kGrooveLength + step, song.grooves[i].steps[step]);

    // --- synths ---
    for (std::size_t i = 0; i < kSynthCount; ++i) {
        const std::size_t b = r.synthParams + i * kSynthBytes;
        const Synth& s = song.synths[i];
        w.setU8(b + 0, static_cast<std::uint8_t>(s.waveform));
        w.setU8(b + 1, static_cast<std::uint8_t>(s.filter));
        if (fmt >= 5) {
            w.setBits(b + 2, 4, 4, s.resonanceStart.value());
            w.setBits(b + 2, 0, 4, s.resonanceEnd.value());
        } else {
            w.setU8(b + 2, static_cast<std::uint8_t>(s.resonanceStart.value() & 0x0F)); // whole byte, hi cleared
        }
        w.setU8(b + 3, static_cast<std::uint8_t>(s.distortion));
        w.setU8(b + 4, static_cast<std::uint8_t>(s.phaseCompression));
        w.setU8(b + 5, s.volumeStart);  w.setU8(b + 6, s.cutoffStart);
        w.setU8(b + 7, s.phaseStart);   w.setU8(b + 8, s.vshiftStart);
        w.setU8(b + 9, s.volumeEnd);    w.setU8(b + 10, s.cutoffEnd);
        w.setU8(b + 11, s.phaseEnd);    w.setU8(b + 12, s.vshiftEnd);
        w.setBits(b + 13, 4, 4, static_cast<std::uint8_t>(0xF - s.limitStart.value()));
        w.setBits(b + 13, 0, 4, static_cast<std::uint8_t>(0xF - s.limitEnd.value()));
    }

    // --- waves ---
    for (std::size_t i = 0; i < kWaveSlotCount; ++i)
        for (std::size_t b = 0; b < kWaveBytes; ++b)
            w.setU8(r.waves + i * kWaveBytes + b, song.waves[i].frames[b]);

    return out;
}

} // namespace rp::lsdj::codec
