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
//
// This mirrors the legacy build's pattern (preserved here for traceability):
//
//   static void serialStart(GB_gameboy_t* gb, bool bit_received) {
//       SameBoyPlugState* s = (SameBoyPlugState*)GB_get_user_data(gb);
//       s->bitToSend = bit_received;
//   }
//
//   static bool serialEnd(GB_gameboy_t* gb) {
//       SameBoyPlugState* s = (SameBoyPlugState*)GB_get_user_data(gb);
//       bool ret = s->linkTargets.size() > 0
//           ? GB_serial_get_data_bit(s->linkTargets[0]->gb)
//           : true;
//       for (SameBoyPlugState* linkTarget : s->linkTargets) {
//           GB_serial_set_data_bit(linkTarget->gb, s->bitToSend);
//       }
//       return ret;
//   }
class LinkGroup {
public:
    explicit LinkGroup(std::uint8_t id) : id_(id) { members_.reserve(8); }

    std::uint8_t id() const noexcept { return id_; }

    void addMember(SameBoySystem* sys) { members_.push_back(sys); }
    const std::vector<SameBoySystem*>& members() const noexcept { return members_; }
    std::size_t                        size() const noexcept    { return members_.size(); }
    bool                               empty() const noexcept   { return members_.empty(); }

    // Audio-thread per-block entry. Drives all members in round-robin
    // GB_run() calls until each has produced `info.frames` samples, then
    // calls each member's mixInto(outs) to sum into the host buffers with
    // per-system gain applied.
    void onProcess(const AudioBlockInfo& info, float* const* outs);

private:
    std::uint8_t                  id_;
    std::vector<SameBoySystem*>   members_;
};
