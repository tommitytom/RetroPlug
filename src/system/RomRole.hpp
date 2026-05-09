#pragma once

#include <cstdint>
#include <string_view>
#include <variant>

// Forward decl to avoid circularity with SameBoySystem.hpp.
class SameBoySystem;
struct AudioBlockInfo;

// Audio-thread-bound per-ROM-type behavior, composed onto a SameBoySystem.
// LSDJ sync, Arduinoboy emulation, MGB MIDI passthrough, etc. become RomRole
// subclasses; one system can own many. The first concrete role lands at
// Step 6 (MGB) — this header is the placeholder seam.
class RomRole {
public:
    virtual ~RomRole() = default;

    virtual void onAttach(SameBoySystem&) {}
    virtual void onMidi(SameBoySystem&,
                        const void* /*midiEvents*/,
                        std::uint32_t /*count*/) {}
    virtual void onProcessBlock(SameBoySystem&, const AudioBlockInfo&) {}
    virtual void onTransportChange(bool /*playing*/) {}
    virtual std::string_view kind() const = 0;
};

// Reserved for the variant of role configs (LsdjSyncConfig, MgbPassthroughConfig...).
// Empty in Step 1; the type exists so SameBoyConfig can hold it once roles land.
// using RoleConfig = std::variant<...>;
