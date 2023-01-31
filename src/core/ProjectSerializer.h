#pragma once

#include "foundation/TypeRegistry.h"
#include "core/ProjectState.h"
#include "core/System.h"

namespace rp::ProjectSerializer {
	std::string serialize(const fw::TypeRegistry& typeRegistry, const ProjectState& state, const std::vector<SystemDesc>& systems);

	bool serialize(const fw::TypeRegistry& typeRegistry, std::string_view path, ProjectState& state, const std::vector<SystemDesc>& systems, bool updatePath);

	bool deserializeFromMemory(const fw::TypeRegistry& typeRegistry, std::string_view fileData, ProjectState& state, std::vector<SystemDesc>& systemSettings);

	bool deserializeFromFile(const fw::TypeRegistry& typeRegistry, std::string_view path, ProjectState& state, std::vector<SystemDesc>& systemSettings);
}
