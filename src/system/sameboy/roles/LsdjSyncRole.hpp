#pragma once

#include <cstdint>
#include <memory>
#include <string_view>

#include "rfl/Literal.hpp"

#include "system/RomRole.hpp"

// LSDJ MIDI sync modes. Step 08 shipped `Off` + `MidiSync`. Step 09 adds the
// Arduinoboy family. Enum values are append-only — the on-disk schema relies
// on the numeric ordering, so reordering or removing a value would break
// project state round-trips.
enum class LsdjSyncMode : std::uint32_t {
    Off                = 0,
    MidiSync           = 1,  // host PPQ → 0xF8 stream → LSDJ serial (step 08)
    MidiSyncArduinoboy = 2,  // input: notes 24/25 toggle play, 26-29 set divisor, 30+ → row byte
    MidiMap            = 3,  // input: ch0 NoteOn → row byte; ch1 → row+128; NoteOff → 0xFE
    Keyboard           = 4,  // placeholder; raw PC keyboard mapping is not implemented yet
    KeyboardMidi       = 5,  // input: MIDI notes → PS/2-equivalent scancodes via LSDJ keyboard map
    MidiPassthrough    = 6,  // input: raw 3-byte MIDI bytes → LSDJ serial (MGB-like on LSDJ)
    ArduinoboyMaster   = 7,  // output: LSDJ serial-out bytes → host MIDI (MI.OUT in PROJECT)
};

struct LsdjSyncConfig {
    using Tag = rfl::Literal<"lsdj-sync">;

    // Default-on for sniffed LSDJ ROMs so LSDJ syncs to the host out of the
    // box. Users can override per project via the LSDJ-mode menu cycle
    // (step 09).
    LsdjSyncMode mode = LsdjSyncMode::MidiSync;

    // 1/2/4/8. Used by MidiSync + MidiSyncArduinoboy to subdivide the 24-PPQN
    // clock that LSDJ expects (legacy: `24 / tempoDivisor`).
    std::uint8_t tempoDivisor = 1;

    // Scaffolded. Real implementation needs RAM access (offset table +
    // MemoryAccessor) which lands in step 10.
    bool autoplay = false;
};

// Forward decl so the role can own one without dragging the decoder into
// every translation unit that includes this header.
class ArduinoboyMaster;

// Per-mode handler. In step 09 modes are configuration on a single role
// rather than separate roles — they share enough transient state
// (arduinoboyPlaying_, effectiveDivisor_, lastRow_, keyboardOctave_) that
// splitting them just multiplies bookkeeping.
class LsdjSyncRole final : public RomRole {
public:
    explicit LsdjSyncRole(LsdjSyncConfig cfg);
    ~LsdjSyncRole() override;

    void onAttach     (SameBoySystem& system) override;
    void onMidi       (SameBoySystem& system,
                       const ::MidiEvent* events, std::uint32_t count) override;
    void onProcessBlock(SameBoySystem& system, const AudioBlockInfo& info) override;

    // Step 09: ArduinoboyMaster mode needs to consume LSDJ's serial-out bytes
    // when no link peer is wired. SameBoySystem polls this each block to know
    // whether to enable the serial-out byte accumulator.
    bool wantsSerialOut() const override;
    void onSerialOutByte(SameBoySystem& system, std::uint8_t byte) override;

    std::string_view kind() const override { return "lsdj-sync"; }

private:
    void handleArduinoboyInput(SameBoySystem& sys, const ::MidiEvent& ev);
    void handleMidiMap        (SameBoySystem& sys, const ::MidiEvent& ev);
    void handleKeyboardMidi   (SameBoySystem& sys, const ::MidiEvent& ev);
    void handlePassthrough    (SameBoySystem& sys, const ::MidiEvent& ev);

    LsdjSyncConfig cfg_;

    // Transient state. Reset on construction (so mode flips via
    // instantiateRoles() start clean) but NOT serialized.
    bool          prevPlaying_       = false;
    bool          arduinoboyPlaying_ = false;
    std::uint8_t  effectiveDivisor_  = 1;   // tracks runtime mutation from notes 26-29
    int           lastRow_           = -1;  // MidiMap NoteOff handshake
    std::uint8_t  keyboardOctave_    = 4;   // KeyboardMidi octave tracking
    bool          aboyBuild_         = false; // ROM detected as Arduinoboy build

    // Owned by unique_ptr so the header doesn't need ArduinoboyMaster's full
    // definition; constructed lazily when ArduinoboyMaster mode activates.
    std::unique_ptr<ArduinoboyMaster> master_;
};
