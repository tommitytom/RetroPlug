#pragma once

#include <cstdint>
#include <string_view>
#include <variant>

#include "transport/MidiTypes.hpp"

// Forward decl to avoid circularity with SameBoySystem.hpp.
class SameBoySystem;
struct AudioBlockInfo;

// Audio-thread-bound per-ROM-type behavior, composed onto a SameBoySystem.
// LSDJ sync, Arduinoboy emulation, MGB MIDI passthrough, etc. are RomRole
// subclasses; one system can own many.
class RomRole {
public:
    virtual ~RomRole() = default;

    virtual void onAttach(SameBoySystem&) {}
    virtual void onMidi(SameBoySystem&,
                        const ::MidiEvent* /*events*/,
                        std::uint32_t /*count*/) {}
    virtual void onProcessBlock(SameBoySystem&, const AudioBlockInfo&) {}
    virtual void onTransportChange(bool /*playing*/) {}

    // Roles that consume LSDJ's serial-out byte stream (e.g. the
    // ArduinoboyMaster MI.OUT decoder) opt in here. The SameBoySystem
    // checks this each block; when true AND no link peer is wired, the
    // system's serialEnd callback accumulates bits into bytes and fans
    // each completed byte out to roles via `onSerialOutByte`.
    virtual bool wantsSerialOut() const { return false; }
    virtual void onSerialOutByte(SameBoySystem&, std::uint8_t /*byte*/) {}

    virtual std::string_view kind() const = 0;
};

// The variant of role configs (LsdjSyncConfig, MgbPassthroughConfig, ...)
// is declared in system/RoleConfig.hpp.
