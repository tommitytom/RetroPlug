#include "project/Project.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <vector>

#include "rfl/Variant.hpp"

#include "system/mesen/GbaSystem.hpp"
#include "system/mesen/MesenSystem.hpp"
#include "system/sameboy/SameBoySystem.hpp"

namespace {

// Load the entire file at `path` into a byte buffer. Returns empty on failure.
std::vector<std::uint8_t> slurpFile(const std::string& path) {
    if (path.empty()) return {};
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        std::fprintf(stderr, "[Project] could not open ROM '%s'\n", path.c_str());
        return {};
    }
    const std::streamsize size = in.tellg();
    if (size <= 0) return {};
    in.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(size));
    if (!in.read(reinterpret_cast<char*>(buf.data()), size)) {
        std::fprintf(stderr, "[Project] failed to read ROM '%s'\n", path.c_str());
        return {};
    }
    return buf;
}

} // namespace

SystemId Project::addSystem(const SystemConfig& config) {
    const SystemId id = nextId_++;

    // Dispatch on the variant alternative. Each branch constructs the matching
    // runtime SystemBase subclass and pushes both into `systems_` and the
    // canonical `config_.systems`.
    if (const auto* sb = rfl::get_if<SameBoyConfig>(&config.variant())) {
        // Prefer embedded ROM bytes (round-tripped through DPF state) over
        // re-reading from disk. Only fall back to slurpFile when bytes are
        // absent — covers legacy/dev paths and the embedRom=false opt-out.
        std::vector<std::uint8_t> rom = sb->romBytes.bytes();
        if (rom.empty())
            rom = slurpFile(sb->romPath);
        if (rom.empty()) {
            std::fprintf(stderr, "[Project] no ROM bytes/path for SameBoy id=%u path='%s'\n",
                         id, sb->romPath.c_str());
            return 0;
        }
        auto sys = std::make_unique<SameBoySystem>(id, *sb, std::move(rom));
        systems_.push_back(std::move(sys));
        config_.systems.push_back(*sb);
        rebuildLinkGroups();
        return id;
    }

    if (const auto* mb = rfl::get_if<MesenConfig>(&config.variant())) {
        std::vector<std::uint8_t> rom = mb->romBytes.bytes();
        if (rom.empty())
            rom = slurpFile(mb->romPath);
        if (rom.empty()) {
            std::fprintf(stderr, "[Project] no ROM bytes/path for Mesen id=%u path='%s'\n",
                         id, mb->romPath.c_str());
            return 0;
        }
        auto sys = std::make_unique<MesenSystem>(id, *mb, std::move(rom));
        systems_.push_back(std::move(sys));
        config_.systems.push_back(*mb);
        // Mesen systems don't participate in LinkGroups (no GB serial); the
        // call is still safe — it just leaves any existing GB groups intact.
        rebuildLinkGroups();
        return id;
    }

    if (const auto* gb = rfl::get_if<GbaSystemConfig>(&config.variant())) {
        std::vector<std::uint8_t> rom = gb->romBytes.bytes();
        if (rom.empty())
            rom = slurpFile(gb->romPath);
        if (rom.empty()) {
            std::fprintf(stderr, "[Project] no ROM bytes/path for GBA id=%u path='%s'\n",
                         id, gb->romPath.c_str());
            return 0;
        }
        auto sys = std::make_unique<GbaSystem>(id, *gb, std::move(rom));
        systems_.push_back(std::move(sys));
        config_.systems.push_back(*gb);
        rebuildLinkGroups();
        return id;
    }

    return 0;
}

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

void Project::clearSystems() {
    onDeactivate();
    systems_.clear();
    config_.systems.clear();
    linkGroups_.clear();
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
    // Drive each system. Linked systems are intercepted by their LinkGroup
    // (which steps them in lockstep); their per-system onProcess short-
    // circuits because linkPeers_ is non-empty.
    for (auto& sys : systems_) {
        if (sys) sys->onProcess(info, outs);
    }
    for (auto& group : linkGroups_) {
        group.onProcess(info, outs);
    }
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

ProjectConfig Project::snapshotConfig() const {
    ProjectConfig out = config_;
    out.systems.clear();
    out.systems.reserve(systems_.size());
    for (const auto& s : systems_) {
        if (s) out.systems.push_back(s->snapshotConfig());
    }
    return out;
}
