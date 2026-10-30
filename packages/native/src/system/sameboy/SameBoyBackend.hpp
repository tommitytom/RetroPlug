#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "system/SystemFactory.hpp"

class SameBoySystem;

// Builds a real SameBoySystem (Game Boy) from the ROM and seed bytes a system node's resources hold and the config its
// settings made: the one place SameBoyConfig is constructed on the build path. SameBoy-only: a non-GB ROM is rejected
// (nullptr, with the reason in *why), the embedded marker included (it carries a cart's logo).
class SameBoyBackend final : public SystemBackend {
public:
    // The ROM gate, the seeds into cfg BEFORE construct (so a live core restores them inside onActivate, the savestate
    // last), onActivate, then enableStateSnapshot. cfg and the request's bytes are consumed.
    std::unique_ptr<SystemBase> build(SystemId id, SystemBuildRequest request,
                                      double sampleRate, std::string* why) override;
};
