#include "system/sameboy/roles/LsdjSyncRole.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "system/SystemTypes.hpp"
#include "system/sameboy/SameBoySystem.hpp"
#include "system/sameboy/roles/ArduinoboyMaster.hpp"
#include "system/sameboy/roles/LsdjKeyboardMap.hpp"
#include "util/PpqUtil.hpp"

namespace {

constexpr std::uint8_t kMidiClock = 0xF8;
constexpr std::uint8_t kMidiStart = 0xFA;
constexpr std::uint8_t kMidiStop  = 0xFC;
constexpr std::uint8_t kMidiNoteOffSentinel = 0xFE; // MidiMap NoteOff handshake

constexpr std::size_t kTitleOffset = 0x0134;
constexpr std::size_t kTitleSize   = 15;

const char* modeName(LsdjSyncMode m) {
    switch (m) {
        case LsdjSyncMode::Off:                return "Off";
        case LsdjSyncMode::MidiSync:           return "MidiSync";
        case LsdjSyncMode::MidiSyncArduinoboy: return "MidiSyncArduinoboy";
        case LsdjSyncMode::MidiMap:            return "MidiMap";
        case LsdjSyncMode::Keyboard:           return "Keyboard";
        case LsdjSyncMode::KeyboardMidi:       return "KeyboardMidi";
        case LsdjSyncMode::MidiPassthrough:    return "MidiPassthrough";
        case LsdjSyncMode::ArduinoboyMaster:   return "ArduinoboyMaster";
    }
    return "?";
}

// Look at the cartridge header title to detect an Arduinoboy build of LSDJ.
// Stock LSDJ titles are like "LSDj-v9.4.2"; the Arduinoboy build adds an
// "aboy" suffix, e.g. "LSDj-v9.3.3aboy". The check is informational only —
// the sniffer treats both ROMs the same way.
bool isArduinoboyBuild(const std::vector<std::uint8_t>& rom) {
    if (rom.size() < kTitleOffset + kTitleSize) return false;
    const std::string_view title(
        reinterpret_cast<const char*>(rom.data() + kTitleOffset), kTitleSize);
    return title.find("aboy") != std::string_view::npos;
}

// Return whether the mode wants emission of MIDI clock bytes into LSDJ's
// serial-in line each block (driven by host PPQ position).
bool emitsHostClock(LsdjSyncMode m, bool arduinoboyPlaying) {
    if (m == LsdjSyncMode::MidiSync) return true;
    if (m == LsdjSyncMode::MidiSyncArduinoboy) return arduinoboyPlaying;
    return false;
}

// Status helpers for the MidiMap mode. Operate on the raw MIDI status byte
// (DPF gives us the full event including channel in data[0]).
inline bool isNoteOn (std::uint8_t status) { return (status & 0xF0) == 0x90; }
inline bool isNoteOff(std::uint8_t status) { return (status & 0xF0) == 0x80; }
inline std::uint8_t channelOf(std::uint8_t status) { return status & 0x0F; }

// Translate a MidiMap (channel, note) pair to a LSDJ row index:
//   ch 0 → note
//   ch 1 → note + 128
//   other → -1 (skip)
int midiMapRowNumber(std::uint8_t channel, std::uint8_t note) {
    if (channel == 0) return note;
    if (channel == 1) return note + 128;
    return -1;
}

// LSDJ KeyboardMidi: pushing octave-shift scancodes to slide LSDJ's internal
// keyboard octave to match the incoming MIDI octave. Returns the new octave.
std::uint8_t slideKeyboardOctave(SameBoySystem& sys,
                                 std::uint8_t targetOctave,
                                 std::uint8_t currentOctave) {
    if (targetOctave == currentOctave) return currentOctave;
    int diff = static_cast<int>(targetOctave) - static_cast<int>(currentOctave);
    const std::uint8_t code = diff > 0 ? lsdj::kKeyboardOctUp : lsdj::kKeyboardOctDn;
    diff = diff < 0 ? -diff : diff;
    while (diff-- > 0) sys.serialIn_.push_back(code);
    return targetOctave;
}

} // namespace

LsdjSyncRole::LsdjSyncRole(LsdjSyncConfig cfg) : cfg_(cfg) {
    effectiveDivisor_ = cfg_.tempoDivisor > 0 ? cfg_.tempoDivisor : 1;
}

LsdjSyncRole::~LsdjSyncRole() = default;

void LsdjSyncRole::onAttach(SameBoySystem& system) {
    aboyBuild_ = isArduinoboyBuild(system.rom_);
    std::fprintf(stderr,
                 "[RetroPlug] LSDJ sync role attached (mode=%s, divisor=%u, autoplay=%d, build=%s)\n",
                 modeName(cfg_.mode), unsigned(cfg_.tempoDivisor),
                 cfg_.autoplay ? 1 : 0,
                 aboyBuild_ ? "arduinoboy" : "stock");
    if (cfg_.autoplay) {
        std::fprintf(stderr, "[RetroPlug] LSDJ autoplay flag set but unimplemented (needs step 10 RAM access)\n");
    }
    if (!aboyBuild_ && (cfg_.mode == LsdjSyncMode::MidiSyncArduinoboy ||
                       cfg_.mode == LsdjSyncMode::ArduinoboyMaster)) {
        std::fprintf(stderr,
                     "[RetroPlug] WARNING: %s mode selected but ROM is stock LSDJ; load an Arduinoboy build for this mode to work.\n",
                     modeName(cfg_.mode));
    }
    if (cfg_.mode == LsdjSyncMode::Keyboard) {
        std::fprintf(stderr,
                     "[RetroPlug] WARNING: raw Keyboard mode is a placeholder in step 09; falling back to Off behavior.\n");
    }
    if (cfg_.mode == LsdjSyncMode::ArduinoboyMaster) {
        master_ = std::make_unique<ArduinoboyMaster>();
    }
}

void LsdjSyncRole::onMidi(SameBoySystem& system,
                          const ::MidiEvent* events, std::uint32_t count) {
    if (events == nullptr || count == 0) return;
    for (std::uint32_t i = 0; i < count; ++i) {
        const ::MidiEvent& ev = events[i];
        if (ev.size == 0) continue;
        switch (cfg_.mode) {
            case LsdjSyncMode::MidiSyncArduinoboy: handleArduinoboyInput(system, ev); break;
            case LsdjSyncMode::MidiMap:            handleMidiMap        (system, ev); break;
            case LsdjSyncMode::KeyboardMidi:       handleKeyboardMidi   (system, ev); break;
            case LsdjSyncMode::MidiPassthrough:    handlePassthrough    (system, ev); break;
            // Off / MidiSync / Keyboard / ArduinoboyMaster don't consume input MIDI.
            default: break;
        }
    }
}

void LsdjSyncRole::onProcessBlock(SameBoySystem& system, const AudioBlockInfo& info) {
    // Transport edge handling. Arduinoboy slave mode bookends the clock
    // stream with 0xFA (start) on the playing edge and 0xFC (stop) on the
    // stopping edge — same convention the legacy `onTransportChange` used
    // (LsdjAudioHooks.cpp:83-101).
    if (info.transportPlaying != prevPlaying_) {
        if (cfg_.mode == LsdjSyncMode::MidiSyncArduinoboy) {
            system.serialIn_.push_back(info.transportPlaying ? kMidiStart : kMidiStop);
        }
        prevPlaying_ = info.transportPlaying;
    }

    if (!emitsHostClock(cfg_.mode, arduinoboyPlaying_)) return;

    // Tempo divisor — legacy uses `24 / divisor` so divisor=1 → 24 PPQN,
    // divisor=2 → 12 PPQN, etc. (LsdjAudioHooks.cpp:77).
    const std::uint8_t effective = effectiveDivisor_ > 0 ? effectiveDivisor_ : 1;
    const std::uint32_t resolution = 24u / effective;
    PpqUtil::eachTick(info, resolution, [&system](std::uint32_t, std::uint32_t) {
        system.serialIn_.push_back(kMidiClock);
    });
}

bool LsdjSyncRole::wantsSerialOut() const {
    return cfg_.mode == LsdjSyncMode::ArduinoboyMaster;
}

void LsdjSyncRole::onSerialOutByte(SameBoySystem& system, std::uint8_t byte) {
    if (cfg_.mode != LsdjSyncMode::ArduinoboyMaster || !master_) return;
    master_->feed(byte, system.midiOut());
}

// --- mode helpers --------------------------------------------------------

void LsdjSyncRole::handleArduinoboyInput(SameBoySystem& sys, const ::MidiEvent& ev) {
    if (!isNoteOn(ev.data[0])) return;
    const std::uint8_t note = ev.data[1];
    switch (note) {
        case 24: arduinoboyPlaying_ = true;  return;
        case 25: arduinoboyPlaying_ = false; return;
        case 26: effectiveDivisor_  = 1;     return;
        case 27: effectiveDivisor_  = 2;     return;
        case 28: effectiveDivisor_  = 4;     return;
        case 29: effectiveDivisor_  = 8;     return;
        default:
            if (note >= 30) {
                sys.serialIn_.push_back(static_cast<std::uint8_t>(note - 30));
            }
            return;
    }
}

void LsdjSyncRole::handleMidiMap(SameBoySystem& sys, const ::MidiEvent& ev) {
    const std::uint8_t status = ev.data[0];
    const std::uint8_t channel = channelOf(status);
    const std::uint8_t note = ev.size >= 2 ? ev.data[1] : 0;
    if (isNoteOn(status)) {
        const int row = midiMapRowNumber(channel, note);
        if (row < 0) return;
        sys.serialIn_.push_back(static_cast<std::uint8_t>(row));
        lastRow_ = row;
    } else if (isNoteOff(status)) {
        const int row = midiMapRowNumber(channel, note);
        if (row == lastRow_) {
            sys.serialIn_.push_back(kMidiNoteOffSentinel);
            lastRow_ = -1;
        }
    }
}

void LsdjSyncRole::handleKeyboardMidi(SameBoySystem& sys, const ::MidiEvent& ev) {
    if (!isNoteOn(ev.data[0])) return;
    std::uint8_t note = ev.data[1];

    if (note >= lsdj::kKeyboardNoteStart) {
        note -= lsdj::kKeyboardNoteStart;
        keyboardOctave_ = slideKeyboardOctave(sys, note / 12, keyboardOctave_);
        // Legacy splits MIDI notes into two rows of LSDJ keyboard keys: the
        // first row covers notes 0x00..0x0B; the second row 0x0C..0x17 maps
        // to the same kKeyboardNoteMap entries shifted by 12.
        const std::uint8_t idx = (note >= 0x3C) ? std::uint8_t((note % 12) + 0x0C)
                                                : std::uint8_t(note % 12);
        sys.serialIn_.push_back(lsdj::kKeyboardNoteMap[idx]);
    } else if (note >= lsdj::kKeyboardLowStart) {
        note -= lsdj::kKeyboardLowStart;
        const std::uint8_t command = lsdj::kKeyboardLowOctaveMap[note];
        if (lsdj::isExtendedScancode(command)) sys.serialIn_.push_back(0xE0);
        sys.serialIn_.push_back(command);
    }
}

void LsdjSyncRole::handlePassthrough(SameBoySystem& sys, const ::MidiEvent& ev) {
    // Raw 3-byte MIDI (status + 2 data) → LSDJ serial. SysEx (size > 4) is
    // skipped — the GB serial port can't usefully carry it without per-byte
    // chunking.
    if (ev.size > ::MidiEvent::kDataSize) return;
    for (std::uint32_t b = 0; b < ev.size; ++b) {
        sys.serialIn_.push_back(ev.data[b]);
    }
}
