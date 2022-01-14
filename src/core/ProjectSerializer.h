#pragma once

#include "core/ProjectState.h"
#include "core/ResourceManager.h"
#include "core/System.h"

namespace rp::ProjectSerializer {
	bool serialize(std::string_view path, ProjectState& state, bool updatePath);

	bool deserialize(std::string_view path, ProjectState& state);
}
