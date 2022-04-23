#pragma once

#include "core/ProjectState.h"
#include "core/ResourceManager.h"
#include "core/SystemWrapper.h"

namespace rp::ProjectSerializer {
	std::string serialize(const ProjectState& state, const std::vector<SystemWrapperPtr>& systems);

	bool serialize(std::string_view path, ProjectState& state, const std::vector<SystemWrapperPtr>& systems, bool updatePath);

	bool deserialize(std::string_view path, ProjectState& state, std::vector<SystemSettings>& systemSettings);
}
