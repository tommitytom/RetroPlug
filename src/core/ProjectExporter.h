#pragma once

#include "core/Project.h"
#include "foundation/DataBuffer.h"

namespace rp::ProjectExporter {
	struct Settings {
		bool project = true;
		bool includeFiles = false;
		bool samples = false;
	};

	bool exportProject(const Settings& settings, const fw::TypeRegistry& types, const ProjectState& project, const std::vector<SystemPtr>& systems, fw::Uint8Buffer& target);
}
