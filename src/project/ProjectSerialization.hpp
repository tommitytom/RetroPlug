#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "rfl/json/read.hpp"
#include "rfl/json/write.hpp"

#include "project/ProjectConfig.hpp"

// Single source of truth for ProjectConfig <-> JSON. Used by
// LVGLPluginDSP::getState/setState and (later) by rpcpp's getProjectConfig.
inline std::string projectConfigToJson(const ProjectConfig& cfg) {
    return rfl::json::write(cfg);
}

inline std::optional<ProjectConfig> projectConfigFromJson(std::string_view json) {
    auto result = rfl::json::read<ProjectConfig>(json);
    if (!result) return std::nullopt;
    return std::move(result.value());
}
