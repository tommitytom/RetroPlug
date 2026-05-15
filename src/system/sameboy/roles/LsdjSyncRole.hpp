#pragma once

#include <cstdint>
#include <string_view>

#include "rfl/Literal.hpp"

#include "system/RomRole.hpp"

// LSDJ MIDI sync modes. Step 08 ships only `Off` and `MidiSync` (the simplest
// mode: host transport drives LSDJ tempo by injecting 0xF8 bytes into LSDJ's
// serial-in port at 24 PPQN). The richer Arduinoboy modes (`MidiSyncArduinoboy`,
// `MidiMap`, `KeyboardMidi`) arrive in step 09. The enum values are locked
// once step 12 lands — additions are append-only.
enum class LsdjSyncMode : std::uint32_t {
    Off      = 0,
    MidiSync = 1,
};

struct LsdjSyncConfig {
    using Tag = rfl::Literal<"lsdj-sync">;

    // Default-on for sniffed LSDJ ROMs so LSDJ syncs to the host out of the
    // box. Users can override per project once the role-config edit RPC
    // arrives (deferred from step 08; lands with step 09's Arduinoboy picker).
    LsdjSyncMode mode = LsdjSyncMode::MidiSync;

    // Scaffolded for step 09. The role does not read this in step 08 — RAM
    // access (port of MemoryAccessor + LSDJ offset table) lands with step 09
    // where Arduinoboy's "is playing" detection needs it anyway.
    bool autoplay = false;
};

// Generates MIDI clock (0xF8) bytes from the host transport's PPQ position
// and pushes them into the GB serial-in queue. The serial bytes are clocked
// out bit-by-bit at GB hardware rate by SameBoySystem::nextSerialInBit, which
// LSDJ's MIDI sync mode interprets as external clock ticks.
class LsdjSyncRole final : public RomRole {
public:
    explicit LsdjSyncRole(LsdjSyncConfig cfg) : cfg_(cfg) {}

    void onAttach(SameBoySystem& system) override;
    void onProcessBlock(SameBoySystem& system, const AudioBlockInfo& info) override;

    std::string_view kind() const override { return "lsdj-sync"; }

private:
    LsdjSyncConfig cfg_;
    bool           prevPlaying_ = false; // edge detection; used for stop-clock symmetry once step 09 wires onTransportChange
};
