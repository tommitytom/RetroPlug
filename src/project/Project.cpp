#include "project/Project.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <vector>

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
    if (auto* sb = std::get_if<SameBoyConfig>(&config)) {
        std::vector<std::uint8_t> rom = slurpFile(sb->romPath);
        if (rom.empty()) {
            std::fprintf(stderr, "[Project] empty ROM for SameBoy id=%u path='%s'\n",
                         id, sb->romPath.c_str());
            return 0;
        }
        auto sys = std::make_unique<SameBoySystem>(id, *sb, std::move(rom));
        systems_.push_back(std::move(sys));
        config_.systems.push_back(*sb);
        return id;
    }

    return 0;
}

void Project::removeSystem(SystemId id) {
    auto it = std::find_if(systems_.begin(), systems_.end(),
        [&](const std::unique_ptr<SystemBase>& s) { return s && s->id() == id; });
    if (it == systems_.end()) return;
    (*it)->onDeactivate();
    const std::size_t idx = static_cast<std::size_t>(it - systems_.begin());
    systems_.erase(it);
    if (idx < config_.systems.size())
        config_.systems.erase(config_.systems.begin() + idx);
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

ProjectConfig Project::snapshotConfig() const {
    ProjectConfig out = config_;
    out.systems.clear();
    out.systems.reserve(systems_.size());
    for (const auto& s : systems_) {
        if (s) out.systems.push_back(s->snapshotConfig());
    }
    return out;
}
