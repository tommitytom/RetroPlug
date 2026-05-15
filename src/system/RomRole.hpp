#pragma once

#include <cstdint>
#include <string_view>
#include <variant>

#include "transport/MidiTypes.hpp"

// Forward decl to avoid circularity with SameBoySystem.hpp.
class SameBoySystem;
struct AudioBlockInfo;

// Audio-thread-bound per-ROM-type behavior, composed onto a SameBoySystem.
// LSDJ sync, Arduinoboy emulation, MGB MIDI passthrough, etc. become RomRole
// subclasses; one system can own many. The first concrete role lands at
// Step 7 (MGB) — this header is the placeholder seam.
class RomRole {
public:
    virtual ~RomRole() = default;

    virtual void onAttach(SameBoySystem&) {}
    virtual void onMidi(SameBoySystem&,
                        const ::MidiEvent* /*events*/,
                        std::uint32_t /*count*/) {}
    virtual void onProcessBlock(SameBoySystem&, const AudioBlockInfo&) {}
    virtual void onTransportChange(bool /*playing*/) {}

    // Roles that consume LSDJ's serial-out byte stream (step 09's
    // ArduinoboyMaster MI.OUT decoder) opt in here. The SameBoySystem checks
    // this each block; when true AND no link peer is wired, the system's
    // serialEnd callback accumulates bits into bytes and fans each completed
    // byte out to roles via `onSerialOutByte`.
    virtual bool wantsSerialOut() const { return false; }
    virtual void onSerialOutByte(SameBoySystem&, std::uint8_t /*byte*/) {}

    virtual std::string_view kind() const = 0;
};

// Reserved for the variant of role configs (LsdjSyncConfig, MgbPassthroughConfig...).
// Empty in Step 1; the type exists so SameBoyConfig can hold it once roles land.
// using RoleConfig = std::variant<...>;
