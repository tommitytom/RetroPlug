#pragma once

#include "core/ProjectState.h"
#include "core/ResourceManager.h"
#include "core/System.h"

namespace rp::ProjectSerializer {
	bool serialize(std::string_view path, ProjectState& state, const std::vector<SystemSettings>& settings, bool updatePath);

	bool deserialize(std::string_view path, ProjectState& state, std::vector<SystemSettings>& systemSettings);
}
