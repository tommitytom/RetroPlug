#pragma once

// The Driver row's vocabulary (docs/modular-graph-phase-f5b-design.md sections 5.1 and 6.4): the Settings > Audio row
// lists one LABEL per driver, "<Backend>: <driver>", and an Apply hands that label back. This is the pure mapping
// between the two, so the row's contract is held by its own cases rather than by a device: "Auto", a label from the
// list, or a stale one (a driver that went away between the list being drawn and Apply being pressed).

#include <string>

#include "audio/AudioHost.hpp"

namespace rp::audio {

/** The label of the row that means "whatever the backend's own automatic choice is": the list's first entry. */
inline constexpr const char* kAutoDriverLabel = "Auto";

/** The Driver row's label for one driver of one backend. The backend is named because two backends may call a driver
 *  the same thing (PortAudio's host API "PipeWire", SDL's driver "pipewire"): the driver id alone does not say which
 *  device an Apply would open, and the labels are what the row shows anyway. */
inline std::string driverChoiceLabel(const std::string& backendLabel, const std::string& driverLabel) {
    return backendLabel + ": " + driverLabel;
}

/** What a Driver row's label names, in the vocabulary apply() takes: "auto" is the automatic choice. */
struct AudioChoice {
    std::string backend = "auto";
    std::string driver;
};

/** Map a row's label to the choice it names: "Auto" (or an empty label, which an older UI bundle sent) is the automatic
 *  choice, and a "<Backend label>: <driver label>" entry of `description` is that pair. A label the description does not
 *  carry is a STALE row and answers `current`, so an Apply that changed only the rate keeps the device the run already
 *  had - the same rule the pre-package hook had ("an unknown name keeps the current driver"). Pure: no device, no
 *  library, no allocation beyond the strings. */
inline AudioChoice choiceForDriverLabel(const std::string& label, const AudioDescription& description,
                                        const AudioChoice& current) {
    if (label.empty() || label == kAutoDriverLabel) return AudioChoice{};
    for (const AudioBackendDescription& backend : description.backends) {
        if (backend.id == "null") continue;
        for (const AudioDriverDescription& driver : backend.drivers)
            if (driverChoiceLabel(backend.label, driver.label) == label) return AudioChoice{backend.id, driver.id};
    }
    return current;
}

}  // namespace rp::audio
