#include "lsdj/codec/SongCodec.hpp"

#include "lsdj/codec/Regions.hpp"
#include "lsdj/codec/SavView.hpp"

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

// fmt>=8 on-disk command byte -> Command.
Command decodeCommand(std::uint8_t b) {
    if (b == 0) return Command::None;
    if (b == 1) return Command::B;
    const std::uint8_t v = b - 1;
    return v <= 23 ? static_cast<Command>(v) : Command::None;
}

Vibrato decodeVibrato(const SavView& v, std::size_t base) {
    Vibrato vib;
    vib.direction = toEnum<VibratoDirection>(v.bits(base + 5, 0, 1), 1);
    vib.shape     = toEnum<VibratoShape>(v.bits(base + 5, 1, 2), 2);
    const std::uint8_t b5 = v.u8(base + 5);
    vib.plvSpeed = (b5 & 0x80) ? PlvSpeed::Step
                 : (b5 & 0x10) ? PlvSpeed::Tick
                               : PlvSpeed::Fast;
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
            w.vibrato     = decodeVibrato(v, base);
            w.transpose   = transpose;
            w.volume      = decodeWaveVolume(v.u8(base + 1));
            w.synth       = v.bits(base + 3, 4, 4);
            w.wave        = v.u8(base + 3);
            w.playMode    = toEnum<WavePlayMode>((v.bits(base + 9, 0, 2) + 3) & 3, 3); // (raw-1)&3
            w.length      = static_cast<Byte>(0xF - v.bits(base + 0xA, 0, 4));
            w.speed       = static_cast<Byte>(v.u8(base + 0xB) + 4);
            w.loopPos     = v.bits(base + 2, 0, 4);
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
            n.vibrato     = decodeVibrato(v, base);
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
            p.vibrato     = decodeVibrato(v, base);
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
            p.commands[step]      = decodeCommand(v.u8(r.phraseCommands + idx));
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
            t.command1[step]       = decodeCommand(v.u8(r.tableCommand1 + idx));
            t.command1Values[step] = v.u8(r.tableCommand1Value + idx);
            t.command2[step]       = decodeCommand(v.u8(r.tableCommand2 + idx));
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
        s.resonanceStart   = (v.u8(b + 2) & 0xF0) >> 4;
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

} // namespace rp::lsdj::codec
