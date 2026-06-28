#pragma once

#include <cstdint>
#include <vector>

#include "system/SystemTypes.hpp"

class SameBoySystem;

// Cycle-accurate-enough serial-link group of SameBoy systems.
//
// Linked emulators (same SameBoyConfig::linkGroupId, nonzero) need to step
// in lockstep so their serial-bit handshake (LSDJ master/slave, gameboy
// printer, etc.) doesn't desync. Each call to GB_run advances one CPU
// instruction (≈2-32 cycles); we round-robin instructions across members
// until they all reach the audio block boundary, then mix their outputs.
//
// The serial bit ferrying happens inside SameBoySystem::serialStart /
// serialEnd, which read/write peer state via the per-system `linkPeers_`
// cache. The cache is populated by Project::rebuildLinkGroups, called
// whenever the system list or any linkGroupId changes.
class LinkGroup {
public:
    explicit LinkGroup(std::uint8_t id) : id_(id) { members_.reserve(8); }

    std::uint8_t id() const noexcept { return id_; }

    void addMember(SameBoySystem* sys) { members_.push_back(sys); }
    const std::vector<SameBoySystem*>& members() const noexcept { return members_; }
    std::size_t                        size() const noexcept    { return members_.size(); }
    bool                               empty() const noexcept   { return members_.empty(); }

    // The audio-thread lockstep (prepare-all / round-robin stepIfBelowTarget /
    // finishBlock each member into its own routed bus) lives in runBlock() in
    // system/BlockRunner.cpp. LinkGroup is just the membership container.

private:
    std::uint8_t                  id_;
    std::vector<SameBoySystem*>   members_;
};
