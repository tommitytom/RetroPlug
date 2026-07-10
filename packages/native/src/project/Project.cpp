#include "project/Project.hpp"

#include <algorithm>
#include <vector>

#include "system/BlockRunner.hpp"
#include "system/sameboy/SameBoySystem.hpp"

SystemBase* Project::removeSystemAndRelease(SystemId id) {
    auto it = std::find_if(systems_.begin(), systems_.end(),
        [&](const std::unique_ptr<SystemBase>& s) { return s && s->id() == id; });
    if (it == systems_.end()) return nullptr;
    SystemBase* released = it->release();
    const std::size_t idx = static_cast<std::size_t>(it - systems_.begin());
    systems_.erase(it);
    if (idx < config_.systems.size())
        config_.systems.erase(config_.systems.begin() + idx);
    rebuildLinkGroups();
    return released;
}

void Project::removeSystem(SystemId id) {
    if (SystemBase* released = removeSystemAndRelease(id)) {
        released->onDeactivate();
        delete released;
    }
}

void Project::onActivate(double sampleRate) {
    for (auto& s : systems_) if (s) s->onActivate(sampleRate);
}

void Project::onDeactivate() {
    for (auto& s : systems_) if (s) s->onDeactivate();
}

void Project::onSampleRateChanged(double sampleRate) {
    for (auto& s : systems_) if (s) s->onSampleRateChanged(sampleRate);
}

void Project::onProcess(const AudioBlockInfo& info, float* const* outs) {
    // The Stereo path: every system sums into the one host stereo pair. Routing
    // and the unlinked/linked lockstep both live in the shared runner now.
    StereoRouter router(outs[0], outs[1]);
    runBlock(info, *this, router);
}

void Project::rebuildLinkGroups() {
    linkGroups_.clear();

    // Clear every SameBoy's peer cache up front. Single-member groups end
    // up with empty linkPeers_ and run via the standalone path.
    for (auto& s : systems_) {
        if (auto* sb = dynamic_cast<SameBoySystem*>(s.get()))
            sb->linkPeers_.clear();
    }

    // Walk systems, bucketing SameBoys by linkGroupId (skipping 0 = standalone).
    auto findGroup = [this](std::uint8_t id) -> LinkGroup* {
        for (auto& g : linkGroups_) {
            if (g.id() == id) return &g;
        }
        return nullptr;
    };

    for (auto& s : systems_) {
        auto* sb = dynamic_cast<SameBoySystem*>(s.get());
        if (!sb) continue;
        const std::uint8_t gid = sb->config_.linkGroupId;
        if (gid == 0) continue;
        if (auto* g = findGroup(gid)) {
            g->addMember(sb);
        } else {
            linkGroups_.emplace_back(gid);
            linkGroups_.back().addMember(sb);
        }
    }

    // Dissolve singleton groups (a group of one is the standalone case).
    // Doing this in a second pass keeps the find-or-create simple above.
    linkGroups_.erase(
        std::remove_if(linkGroups_.begin(), linkGroups_.end(),
                       [](const LinkGroup& g) { return g.size() < 2; }),
        linkGroups_.end());

    // Populate per-system peer lists from each surviving group.
    for (auto& g : linkGroups_) {
        for (auto* member : g.members()) {
            member->linkPeers_.clear();
            for (auto* peer : g.members()) {
                if (peer != member) member->linkPeers_.push_back(peer);
            }
        }
    }
}
