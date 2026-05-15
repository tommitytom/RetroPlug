#pragma once

#include <cstdint>
#include <string_view>

#include "rfl/Literal.hpp"

#include "system/RomRole.hpp"

// Plain-data config for the MGB passthrough role. Empty for now; future
// fields (transpose offset, channel mask) belong here so projects round-trip
// them via reflectcpp without touching the variant wiring.
struct MgbRoleConfig {
    using Tag = rfl::Literal<"mgb">;
};

// Forwards every received MIDI byte verbatim to the GB's serial-in queue.
// SameBoy's serial-end callback drains those bytes bit-by-bit (MSB first).
// No timing alignment to MidiEvent::frame — bytes go in as the events arrive
// and are clocked out at GB hardware rate. Mirrors the legacy
// LsdjSyncMode::MidiPassthrough push path.
class MgbPassthroughRole final : public RomRole {
public:
    void onAttach(SameBoySystem&) override {}
    void onMidi(SameBoySystem& system,
                const ::MidiEvent* events,
                std::uint32_t      count) override;
    std::string_view kind() const override { return "mgb"; }
};
