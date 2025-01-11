#pragma once

#include "foundation/TypeRegistry.h"
#include "core/ProjectState.h"
#include "core/System.h"

namespace rp {
	const std::string_view PROJECT_VERSION = "1.0.0";
	const std::string_view RP_VERSION = "0.5.0";
}

namespace rp::ProjectSerializer {
	std::string serialize(const fw::TypeRegistry& typeRegistry, const ProjectState& state, const std::vector<const SystemDesc*>& systems);

	bool serialize(const fw::TypeRegistry& typeRegistry, std::string_view path, ProjectState& state, const std::vector<const SystemDesc*>& systems, bool updatePath);

	bool deserializeFromMemory(const fw::TypeRegistry& typeRegistry, std::string_view fileData, ProjectState& state, std::vector<SystemDesc>& systemSettings);

	bool deserializeFromFile(const fw::TypeRegistry& typeRegistry, std::string_view path, ProjectState& state, std::vector<SystemDesc>& systemSettings);
}
