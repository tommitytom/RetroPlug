#pragma once

#include <cstdint>
#include <string>
#include <string_view>

// How aggressively RetroPlug (re)writes the loose sibling `<rom>.sav` mirror. A
// global, sticky user preference (UserConfig), toggled from the Settings menu.
// See porting/23 (decision D2) and system/SramAutoSave.hpp.
//
//   Off           Never write the loose `.sav`; the DAW state chunk / `.rplg`
//                 project is the only source of truth. Zero collision risk when
//                 two independent instances share one ROM.
//   OnProjectSave Flush dirty SRAM to the sibling on host save (getState) and on
//                 deactivate()/quit, so the loose file stays fresh WITHOUT
//                 depending on the editor window being open. (The default.)
//   Continuous    OnProjectSave plus the throttled idle-tick writes while the
//                 editor is open — freshest, but writes far more often and
//                 reintroduces last-writer-wins between shared-ROM instances.

namespace rp {

enum class SramMirror : std::uint8_t {
    Off           = 0,
    OnProjectSave = 1,
    Continuous    = 2,
};

// Canonical wire/enum-name spellings. These match reflect-cpp's default enum
// serialization, so config.json and the RPC payload use the same vocabulary.
inline std::string_view sramMirrorToString(SramMirror m) {
    switch (m) {
        case SramMirror::Off:           return "Off";
        case SramMirror::OnProjectSave: return "OnProjectSave";
        case SramMirror::Continuous:    return "Continuous";
    }
    return "OnProjectSave";
}

inline SramMirror sramMirrorFromString(std::string_view s) {
    if (s == "Off")        return SramMirror::Off;
    if (s == "Continuous") return SramMirror::Continuous;
    return SramMirror::OnProjectSave; // "OnProjectSave" and anything unrecognised
}

// True when the loose `.sav` should be spilled at explicit save/quit moments
// (both OnProjectSave and Continuous mirror; Off never does).
inline bool sramMirrorFlushesOnSave(SramMirror m) {
    return m != SramMirror::Off;
}

} // namespace rp
